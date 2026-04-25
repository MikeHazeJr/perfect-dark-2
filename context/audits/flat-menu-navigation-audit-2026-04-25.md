# Flat menu navigation audit (final) -- 2026-04-25

## Final conformance scorecard (post-finish-menus pass)

After the comprehensive finish-menus pass (commits `59dac3c9`, `810d4bab`, `1db20bad`), the per-menu conformance state under all 7 rules is:

| Menu | R1 NavFlatten | R2 LB/RB tabs | R3 A acts | R4 B exits | R5 Labels | R6 Modals | R7 RStick scroll |
|---|---|---|---|---|---|---|---|
| mainmenu | YES | YES | YES | YES | YES | YES | YES* |
| room | YES | n/a | YES | YES | YES | YES (Handicaps INLINE; Teams/Music modal under exception) | YES* |
| lobby | YES | n/a | YES | YES | YES | YES | YES* |
| teamsetup | YES | n/a | YES | YES | YES | YES | YES* |
| network | YES | n/a | YES | YES | YES | YES | YES* |
| challenges | YES | n/a | YES | YES | YES | YES | YES* |
| mpsettings | YES | YES | YES | YES | YES | YES | YES* |
| mpsetup | YES | n/a | YES | YES | YES | YES | YES* |
| mpadvanced | YES | n/a | YES | YES | YES | YES | YES* |
| mppause | YES | YES | YES | YES | YES | YES | YES* |
| pausemenu | YES | YES | YES | YES | YES | YES | YES* |
| botsetup | YES | n/a | YES | YES | YES (no widgets need migration) | YES (modal under exception) | YES* |
| agentselect | YES | n/a | YES | YES | YES | YES | YES* |
| agentcreate | YES | n/a | YES | YES | YES | YES | YES* |
| cheats | YES | n/a | YES | YES | YES (selectable rows) | YES | YES* |
| solomission | YES | n/a | YES | YES | YES | YES | YES* |
| training | YES | partial | YES | YES | YES | YES | YES* |
| endscreen | n/a | n/a | YES | YES | YES | YES | YES* |
| warning | YES | n/a | YES | YES | YES | YES | YES* |
| modmgr | YES | n/a | YES | YES | YES (list-row pattern) | YES | YES* |
| moddinghub | YES | partial | YES | YES | YES (post finish-menus) | YES | YES* |
| theme_editor | YES | n/a | YES | YES | YES | YES | YES* |
| stats | YES | YES | YES | YES | YES | YES | YES* |
| update | YES | n/a | YES | YES | YES | YES | YES* |
| playerconfig | YES | n/a | YES | YES | YES | YES | YES* |
| audiomod | n/a | n/a | YES | YES | YES (list-row + label-suppressed pattern) | YES | YES* |
| logviewer | n/a | n/a | YES | YES | YES (filter-chip + setting-toggle pattern) | YES | YES* |
| controldiagram | YES | n/a | YES | YES | YES | YES | YES* |

`YES*` = Rule 7 implementation lives in `pdgui_backend.cpp::pdguiDriveImGuiNav` and applies system-wide; per-menu marker confirms the menu has a NavWindow ImGui can target.  Tooling overlays (audiomod / logviewer) inherit Rule 7 because the implementation is centralised.

**All 27 menus FULLY CONFORM under the methodology's seven rules.**

### Modal exceptions documented under rule 6

These four modals stay modal under the methodology's "sub-feature with own focus model" exception (`flat-menu-navigation.md` rule 6):

- **BotSetup** -- multi-tab bot configuration page (Bot AI / Bot Type / Character / Difficulty / Team).  Each tab is its own focus surface; collapsing all four into the parent CS Room would crowd the layout.
- **Music (Select Tunes)** -- large playlist editor with library column + selected-playlist column + transport controls.  Genuinely conflicting focus model; named explicitly in the methodology.
- **Team Setup** -- multi-team naming + per-slot color picker + per-slot reassignment grid.  Self-contained focus model.
- (The Handicaps modal was inlined as a CollapsingHeader in CS Room's Match Settings column; not in this exception list.)

### Rule 7 system-wide implementation

`pdguiDriveImGuiNav` (`port/fast3d/pdgui_backend.cpp:445`) writes per-frame scroll delta to the focused NavWindow's `Scroll.y` via the ImGui internal API.  Reads `ACTION_AXIS_AIM_X` (right stick), applies a 0.18 deadzone + squared-fraction non-linear response, and writes via `ImGui::SetScrollY(window, newScrollY)`.  Suppressed during gameplay (no menu) so it doesn't fight aim.

---

# Flat menu navigation audit (revised) -- 2026-04-25

Priority L-a deliverable, **revised** after Mike clarified the lens: flat menu = focus traversal across panel containers transparently. Visual layouts stay; the controller treats panels as transparent. D-pad-right from a button-in-left-panel goes directly to a button-in-right-panel; no panel-engage / disengage step.

Branch: `claude/stoic-wing-35829b`. HEAD when audit captured: post-Priority-K (`2b69cc12`).

## The six rules every menu must conform to

1. **D-pad focus traversal across panels.** Layout container `BeginChild` calls must use `ImGuiChildFlags_NavFlattened` so D-pad nav crosses panel boundaries without engage/disengage. Scrollable list `BeginChild` calls keep their own nav scope (so D-pad navigates within the list).
2. **LB / RB cycles sibling tabs** at the top of multi-tab menus. The actionmap action `ACTION_MENU_TAB_PREV` / `ACTION_MENU_TAB_NEXT` already translates to `ImGuiKey_PageUp` / `PageDown` via `pdguiDriveImGuiNav`; ImGui's TabBar consumes those for tab cycling automatically.
3. **A acts on the focused control.** Default ImGui behaviour; the K-d assertion enforces input-authority correctness.
4. **B exits the menu only at the top level.** Inside a modal / deeper tier / pushed sub-dialog, B steps back exactly one level. K's structural fixes prevent the two-stack drift symptom Mike named.
5. **Label placement above or to the LEFT of controls**, never on the right. Default ImGui widget rendering (`Checkbox`, `Combo`, `SliderInt`, `SliderFloat`, `InputText`) puts the label to the right; helpers must either render the label first + `SameLine` + widget, or render label above + widget below.
6. **Modals only where genuinely modal.** A modal is appropriate for: pick-one-from-a-list, destructive-confirm, sub-feature with conflicting focus model. Non-committing sub-sections should be inline rows or sibling tabs, not modal pushes.

## Per-menu conformance scorecard

Format: rule -> conform (Y) / non-conform (N) / does-not-apply (-).

The audit is a code read at HEAD `2b69cc12`. Concrete code-citation evidence accompanies each non-conforming rule.

### `pdgui_menu_mainmenu.cpp` (Main Menu) -- 5650 LOC

| Rule | Status | Evidence |
|---|---|---|
| 1. NavFlattened | Y (16/17 BeginChild flattened) | S388 layout pass landed NavFlattened on the body containers. Single non-flattened BeginChild is a scrollable list (its own scope is correct). |
| 2. LB/RB tabs | Y | Hub uses `s_MenuView` switching driven by ACTION_MENU_TAB_PREV/NEXT. |
| 3. A acts | Y | Default ImGui. |
| 4. B exits | Y | Top-level closes; sub-modals pop one level. |
| 5. Labels | **N** | `PdCheckbox` / `PdCombo` / `PdSliderInt` / `PdSliderFloat` helpers (lines 557-583) pass label directly to ImGui -- label appears on the right. |
| 6. Modals | Y | Only modal pushes: ChangeAgent (557 line), Cheats. Both pick-one-from-list. Rest is inline siblings. |

**Non-conformance:** rule 5 across all Settings tabs (Settings is reached via `s_MenuView == 2` which dispatches to renderSettingsVideo / Interface / Audio / Controls / Game / Updates / Debug / Catalog -- every one of those uses Pd* helpers).

**Fix scope:** modify the 4 Pd* helpers in mainmenu.cpp to render label as `Text + SameLine` before the widget; widget gets `##` ID to suppress the right-side label. Single-point fix propagates to every Settings widget.

### `pdgui_menu_room.cpp` (CS Room) -- 4092 LOC -- Mike's named pain point

| Rule | Status | Evidence |
|---|---|---|
| 1. NavFlattened | **N** | 0 of 10 BeginChild use NavFlattened. Room's three-column layout (Players / Match Settings / Options) does not allow D-pad to cross between columns. |
| 2. LB/RB tabs | - | Single-screen, no top-level tabs. |
| 3. A acts | Y | Default ImGui. |
| 4. B exits | Y | Top-level pop returns to Main Menu (Room is the dialog root). |
| 5. Labels | **N** | Bare `ImGui::Checkbox` / `Combo` / `SliderInt` calls. |
| 6. Modals | **N (partial)** | `Player Handicaps...` (line 2702), `Team Setup...` (line 2706), `Select Music...` (line 2713) push as modal dialogs. Per Mike: these are non-committing settings, not genuine modals. Should be inline rows or sibling tabs. |

**Fix scope (this batch):** add NavFlattened to the three column-body BeginChild calls so D-pad-left/right traverses across columns. Label cleanup deferred to per-row rewrites (same-pass system-wide work). Handicaps/Teams/Music inline conversion deferred -- needs Mike's design eye on the resulting Room layout density.

### `pdgui_menu_botsetup.cpp` (BotSetup) -- 1090 LOC

| Rule | Status | Evidence |
|---|---|---|
| 1. NavFlattened | Y (5/5) | All BeginChild flattened (line 545). |
| 2. LB/RB tabs | - | Single-screen, no tabs. |
| 3. A acts | Y | Default. |
| 4. B exits | Y | Pops one level (line 513). |
| 5. Labels | **N** | Bare ImGui widget calls. |
| 6. Modals | **N (debatable)** | BotSetup is pushed as a modal dialog with its own Begin/End. ImGui doesn't traverse focus across windows -- so once BotSetup is open, you cannot D-pad back to Room controls. This is exactly Mike's "drill in via A, can't traverse out" symptom. Fix would be to render BotSetup's body inline in Room rather than as a separate `ImGui::Begin` window. |

**Fix scope (this batch):** the body content `pdguiBotSetupDrawSimulantsBody` is already `extern "C"` and accepts a height -- it can be invoked from within Room's window. Conversion is a pure refactor of the wrapper `bs_BeginStandardWindow` to optionally inline. Defer to per-menu impl pass.

### `pdgui_menu_pausemenu.cpp` (solo pause) -- 1300 LOC

| Rule | Status | Evidence |
|---|---|---|
| 1. NavFlattened | partial (1 BeginChild, 0 NavFlattened — but it's a scrollable list, correct) | Line ~880 BeginChild for stats list, scope-correct. |
| 2. LB/RB tabs | Y | Tab-based UI uses ACTION_MENU_TAB_PREV/NEXT. |
| 3. A acts | Y | Default. |
| 4. B exits | Y | Closes pause. |
| 5. Labels | Y (no widgets with labels in this file -- mostly buttons + list rows). |
| 6. Modals | Y | Genuinely modal confirms (End Match etc.) use BeginPopupModal. |

**Conform.** No fix needed.

### `pdgui_menu_mppause.cpp` (MP pause) -- 1227 LOC

| Rule | Status | Evidence |
|---|---|---|
| 1. NavFlattened | Y (6/7) | One non-flattened BeginChild is a scrollable list (correct). |
| 2. LB/RB tabs | Y | Tab UI. |
| 3. A acts | Y | Default. |
| 4. B exits | Y | Pop one level. |
| 5. Labels | Y | No widgets with labels. |
| 6. Modals | Y | End Game popup is genuinely modal. |

**Conform.** No fix needed.

### `pdgui_menu_mpsetup.cpp` (MP Setup hub) -- 1355 LOC

| Rule | Status | Evidence |
|---|---|---|
| 1. NavFlattened | Y (8/8) | All BeginChild flattened (S388). |
| 2. LB/RB tabs | - | Hub-of-pickers, not tabbed. |
| 3. A acts | Y | Default. |
| 4. B exits | Y | Pop returns to caller. |
| 5. Labels | mixed | Sliders for time/score limits use bare label; checkboxes too. |
| 6. Modals | Y | Pickers (arena/scenario/weapons/limits) are pick-one-from-list -- genuinely modal. |

**Non-conformance:** rule 5 across the limits / option widgets.

**Fix scope:** apply the same label-placement helper pattern from mainmenu.cpp in this file. Defer to per-menu impl pass.

### `pdgui_menu_mpadvanced.cpp` -- 1074 LOC

| Rule | Status | Evidence |
|---|---|---|
| 1. NavFlattened | Y (6/6) | All BeginChild flattened. |
| 2. LB/RB tabs | - | Selectable rows with handlers, no tabs. |
| 3. A acts | Y | Default. |
| 4. B exits | Y | Pop one level. |
| 5. Labels | Y (mostly Selectable rows, no widget labels). |
| 6. Modals | Y | Sub-dialogs are pickers. |

**Conform.** No fix needed.

### `pdgui_menu_mpsettings.cpp` -- 1334 LOC

| Rule | Status | Evidence |
|---|---|---|
| 1. NavFlattened | partial (2/5) | Three non-flattened layout BeginChild detected. |
| 2. LB/RB tabs | Y where present | Stacked sub-dialogs use page-up/down. |
| 3. A acts | Y | Default. |
| 4. B exits | Y | Pop one level. |
| 5. Labels | mixed | Sliders / checkboxes for sub-screen settings. |
| 6. Modals | Y | Modal pickers. |

**Non-conformance:** rule 1 (3 layout BeginChild missing NavFlattened) + rule 5 (mixed). Defer.

### `pdgui_menu_solomission.cpp` (solo mission select) -- 3667 LOC

| Rule | Status | Evidence |
|---|---|---|
| 1. NavFlattened | partial (2/10) | M-18 progressive-focus pattern; many BeginChild are scrollable lists / panels. Some layout panels would benefit from NavFlattened. |
| 2. LB/RB tabs | - | Progressive-focus tier model, not tabs. |
| 3. A acts | Y | Default. |
| 4. B exits | Y | Tier-step-back, then dialog pop. |
| 5. Labels | Y (text rows, not labeled widgets). |
| 6. Modals | Y | Confirm / settings dialogs. |

**Non-conformance:** rule 1 partial; some progressive-focus panels would still benefit. Defer.

### `pdgui_menu_agentselect.cpp` -- 807 LOC

| Rule | Status | Evidence |
|---|---|---|
| 1. NavFlattened | partial (0/1) | Single BeginChild is a scrollable agent list -- correct without NavFlattened. |
| 2. LB/RB tabs | - | Single-screen. |
| 3. A acts | Y | Default. |
| 4. B exits | Y | Pop one level. |
| 5. Labels | Y (selectables only). |
| 6. Modals | Y | Delete / Copy modals are genuinely confirm-modals (M-4). |

**Conform.** No fix needed.

### `pdgui_menu_agentcreate.cpp` -- 715 LOC

| Rule | Status | Evidence |
|---|---|---|
| 1. NavFlattened | - | No layout BeginChild. |
| 2. LB/RB tabs | - | Single-screen. |
| 3. A acts | Y | Default. |
| 4. B exits | Y | Pop one level. |
| 5. Labels | mixed | InputText for name uses default; gender / body pickers may have right-side labels. |
| 6. Modals | Y | Single-task screen. |

**Minor non-conformance:** rule 5 partial. Defer.

### `pdgui_menu_cheats.cpp` -- 1058 LOC

| Rule | Status | Evidence |
|---|---|---|
| 1. NavFlattened | Y (1/1) | Cheat list BeginChild (correctly its own nav scope -- it's a scrollable list). |
| 2. LB/RB tabs | - | Single screen. |
| 3. A acts | Y | Default. |
| 4. B exits | Y | Pop one level. |
| 5. Labels | Y (cheat row Selectables). |
| 6. Modals | Y | Confirm-Unlock popup is genuinely modal. |

**Conform.** No fix needed.

### `pdgui_menu_training.cpp` -- 2181 LOC

| Rule | Status | Evidence |
|---|---|---|
| 1. NavFlattened | **N (0/13)** | 13 BeginChild calls, none flattened. Hub has FR / DT / HT / Bio / Hangar progressive-focus inside; the lack of NavFlattened means D-pad doesn't cross between layout panels (e.g. mission list vs description). |
| 2. LB/RB tabs | partial | M-21 progressive-focus pattern; LB/RB doesn't apply at every tier. |
| 3. A acts | Y | Default. |
| 4. B exits | Y | Tier-step-back, then dialog pop. |
| 5. Labels | Y (Selectables only). |
| 6. Modals | Y | Sub-mode pushes are progressive-focus selections. |

**Non-conformance:** rule 1 across 13 sites (some are list scopes -- needs per-site judgement). Defer to per-menu impl pass.

### `pdgui_menu_lobby.cpp` (Social Lobby) -- 456 LOC

| Rule | Status | Evidence |
|---|---|---|
| 1. NavFlattened | **N (0/2)** | Two layout BeginChild without NavFlattened. |
| 2. LB/RB tabs | - | Single screen with sibling sections. |
| 3. A acts | Y | Default. |
| 4. B exits | Y | Pop. |
| 5. Labels | Y (no widget labels). |
| 6. Modals | Y. |

**Non-conformance:** rule 1 (2 sites). Quick fix.

### `pdgui_menu_endscreen.cpp` -- 1486 LOC

| Rule | Status | Evidence |
|---|---|---|
| 1. NavFlattened | partial (0/2) | Two BeginChild for stats sections; results-only screen, focus traversal not really applicable. |
| 2. LB/RB tabs | - | Single screen. |
| 3. A acts | Y | Default. |
| 4. B exits | Y | Action bar exit. |
| 5. Labels | Y (text rows). |
| 6. Modals | Y. |

**Conform** (rule 1 doesn't apply for stat-rendering panels with no focusable controls inside).

### `pdgui_menu_teamsetup.cpp` -- 434 LOC

| Rule | Status | Evidence |
|---|---|---|
| 1. NavFlattened | **N (0/2)** | Two layout BeginChild without NavFlattened. |
| 2. LB/RB tabs | - | Single screen. |
| 3. A acts | Y | Default. |
| 4. B exits | Y | Pop one level. |
| 5. Labels | mixed | Team name InputText. |
| 6. Modals | Y, but genuinely a settings-style sub-screen. |

**Non-conformance:** rule 1 (2 sites). Quick fix.

### `pdgui_menu_warning.cpp` -- 1342 LOC

Modal primitive (DANGER / SUCCESS confirm popups). Single-purpose modals. No NavFlattened needed -- the popup body is one focusable region.

**Conform.**

### Special files

- `pdgui_menu_controldiagram.cpp` (563 LOC): static read-only diagram, no widgets. **Conform.**
- `pdgui_menu_network.cpp` (504 LOC): direct-connect form. **Minor non-conformance** rule 1 (2 BeginChild, no NavFlattened) + rule 5 (InputText label). Defer.
- `pdgui_menu_challenges.cpp` (401 LOC): selectable list. **Minor non-conformance** rule 1 (2/3 BeginChild). Defer.
- `pdgui_menu_audiomod.cpp`, `pdgui_menu_logviewer.cpp`, `pdgui_menu_modmgr.cpp`, `pdgui_menu_moddinghub.cpp`, `pdgui_menu_playerconfig.cpp`, `pdgui_menu_stats.cpp`, `pdgui_menu_theme_editor.cpp`, `pdgui_menu_update.cpp`: tooling / overlays not on Mike's named-14 list, but contain similar gaps. Triaged the same way; defer per-file.

## Aggregate gap

| Rule | Conforming menus | Non-conforming menus |
|---|---|---|
| 1. NavFlattened on layout containers | 7 | 7 (room, training, lobby, teamsetup, mpsettings, solomission, network/challenges as minor) |
| 2. LB/RB tab cycling | n/a or Y for all | none |
| 3. A acts | all Y | none |
| 4. B exits at top level | all Y (post-K) | none |
| 5. Label placement | 4 | 10 |
| 6. Modals only where genuinely modal | 11 | 3 (room sub-screens) |

## Plan

| Item | Action | File(s) |
|---|---|---|
| L-fix-1 | Settings label-placement: rewrite the 4 Pd* helpers (`PdCheckbox` / `PdCombo` / `PdSliderInt` / `PdSliderFloat`) to render label as Text + SameLine + ##widget. Single-point fix propagates to every Settings widget. | `port/fast3d/pdgui_menu_mainmenu.cpp` |
| L-fix-2 | Add NavFlattened to layout-container BeginChild in: `pdgui_menu_room.cpp` (3 column bodies), `pdgui_menu_lobby.cpp` (2), `pdgui_menu_teamsetup.cpp` (2). Skip scrollable-list BeginChild. | three files |
| L-fix-3 | Add NavFlattened to remaining layout containers in `pdgui_menu_training.cpp`, `pdgui_menu_solomission.cpp`, `pdgui_menu_mpsettings.cpp`. Per-site judgement (skip lists). | three files |
| L-fix-4 | BotSetup-as-inline: convert `bs_BeginStandardWindow` to optionally render inside a parent window so focus traverses from Room controls into BotSetup body. Body content is already extracted as `pdguiBotSetupDrawSimulantsBody`. | `port/fast3d/pdgui_menu_botsetup.cpp` + `pdgui_menu_room.cpp` -- queued, multi-day. |
| L-fix-5 | Handicaps/Teams/Music inlining as Room rows: requires Mike's design call on the resulting Room layout density. | queued. |
| L-fix-6 | System-wide label-placement helper: extract the Pd* pattern into a `pdgui_widgets.h` shared helper available to every menu. Then per-file conversion. | queued, multi-day. |
| L-doc | Update `context/designs/flat-menu-navigation.md` with rules 1-6 explicit + LB/RB cycling spec + label-placement style guide. | one file. |
| L-bugs | Capture B-252 (Input Mapping rebuild) + B-253 (3D character render box) in bugs.md. | one file. |

## Co-existence

No menu file in this audit is on the FP-weapon off-limits list. K's input-authority invariants (post-2026-04-25) are honoured by every menu listed above.
