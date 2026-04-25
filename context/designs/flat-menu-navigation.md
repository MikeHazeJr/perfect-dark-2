# Flat menu navigation

Companion to `context/audits/flat-menu-navigation-audit-2026-04-25.md` (Priority L-a). Defines the system-wide menu rules every menu surface must conform to.

## The principle

> **A flat menu is one where focus traverses across panel containers transparently.** Visual panels exist for grouping; the controller treats panels as transparent. D-pad-right from a button-in-left-panel goes directly to a button-in-right-panel; the panel container is never engaged or exited.

This is an input-flow rule, not a layout rule. Visual layouts can stay close to current; what matters is whether the controller's focus traversal obeys the flat-traversal rule and the gamepad mapping below.

## The six rules every menu must conform to

### Rule 1 -- D-pad focus traversal across panels

Layout container `BeginChild` calls must use `ImGuiChildFlags_NavFlattened` so D-pad nav crosses panel boundaries without engage / disengage. The standard form:

```cpp
ImGui::BeginChild("##panel_id", ImVec2(panelW, panelH),
                  ImGuiChildFlags_Borders | ImGuiChildFlags_NavFlattened);
```

(Drop `ImGuiChildFlags_Borders` if the panel doesn't need a visible border.)

Scrollable list `BeginChild` calls keep their own nav scope -- D-pad navigates within the list. Don't add NavFlattened to a list.

The S388 / B-170 layout pass added NavFlattened to ~25 body containers. Subsequent menus and audit gaps are tracked in the L audit doc and brought up to spec by the L-fix-2 / L-fix-3 work.

### Rule 2 -- LB / RB cycles sibling tabs

At the top of multi-tab menus, LB and RB cycle backwards / forwards through sibling tabs. The actionmap actions `ACTION_MENU_TAB_PREV` / `ACTION_MENU_TAB_NEXT` translate to `ImGuiKey_PageUp` / `PageDown` via `pdguiDriveImGuiNav`; ImGui's TabBar consumes those for tab cycling automatically.

This matches the Forge sidebar scheme already shipped (`g_ImcForge`'s LB/RB tab cycle) -- system-wide, the same gamepad mapping applies.

### Rule 3 -- A acts on the focused control

Default ImGui behaviour. The K-d assertion (`MENUPOOL: drift --` LOG_WARNING) enforces that input authority is correctly attached to the focused surface.

### Rule 4 -- B exits the menu only at the top level

- At the top level of a menu, B closes the menu and returns to the prior surface (gameplay or the parent menu).
- Inside a modal / deeper tier / pushed sub-dialog, B steps back exactly one level.
- The user must never observe "B at level 2 jumped past level 1 and closed the whole thing" -- that's the K-class two-stack drift symptom and is structurally prevented by the K commits.

### Rule 5 -- Label placement above or to the LEFT, never on the right

ImGui's default widget rendering paints the label to the right of `Checkbox` / `Combo` / `SliderInt` / `SliderFloat` / `InputText`. That reads as wrong. Labels must be either:

- **Left**: `Text("Label") + SameLine(labelColW) + SetNextItemWidth(remaining) + Widget("##id", ...)`. Right-edge widget extends to the panel edge.
- **Above**: `Text("Label")` on its own line, then `Widget("##id", ...)` below at full width.

The reference helpers in `pdgui_menu_mainmenu.cpp` (`PdCheckbox` / `PdCombo` / `PdSliderInt` / `PdSliderFloat`) implement the left-aligned variant. Settings sub-tabs route every widget through these helpers; per-file pickup elsewhere is queued L-fix-6.

### Rule 6 -- Modals only where genuinely modal

A modal is appropriate when:

- The user must pick exactly one item from a list (arena picker, scenario picker, agent slot picker).
- The action is destructive and benefits from an explicit confirm (End Match, Abort Mission, Delete Theme).
- The sub-feature has its own focus model that would clash with the parent (Music playlist editor).

If those conditions don't apply, the surface should be either:

- **Inline rows** in the parent (D-pad navigates through the rows; A toggles the focused row).
- **Sibling tabs** of the parent (LB/RB cycles between tabs).

Non-committing settings panels that are pushed as modals today (Handicaps, Teams, Music in CS Room) are candidates for inlining.

## Standard gamepad mapping for flat-panel menus

| Input | Action |
|---|---|
| LB / RB | Cycle sibling tabs at the top |
| D-pad | Move focus between controls (across panels transparently) |
| A | Act on the focused control |
| B | Exit menu (top level only); step back one level inside a modal / deeper tier |
| Start | Pause toggle (in-game); ignored otherwise |

This is the same scheme used by Forge's sidebar (`g_ImcForge`). Apply system-wide.

## Standard label-placement style guide

| Widget | Label placement | Reference helper |
|---|---|---|
| `Checkbox` | LEFT (label `Text` then `SameLine` then `Checkbox("##id", ...)`) | `PdCheckbox` (mainmenu.cpp) |
| `Combo` | LEFT, widget extends to right edge | `PdCombo` |
| `SliderInt` / `SliderFloat` | LEFT, widget extends to right edge | `PdSliderInt` / `PdSliderFloat` |
| `InputText` | ABOVE (label on a row, input below at full width) -- conventionally clearer for free-form entry. |

The label-column width is set by `pdguiSettingsLabelColW` (~220px scaled). Adjust per-menu if a longer label requires a wider column, but keep alignment consistent within one panel.

## Reference implementations

- **Sibling panels with NavFlattened + LB/RB tabs**: `pdgui_menu_mainmenu.cpp` (Settings sub-tabs, post Priority L).
- **Modal**: `pdgui_menu_warning.cpp` (the canonical confirm-modal primitive).
- **Progressive-focus**: `pdgui_menu_solomission.cpp` (M-18 `s_FocusGroup` enum).

## Audit findings (2026-04-25)

The audit (`context/audits/flat-menu-navigation-audit-2026-04-25.md`) scored every ImGui menu against the six rules. Aggregate gap:

| Rule | Conforming | Non-conforming menus needing fix |
|---|---|---|
| 1. NavFlattened on layout containers | 7 | 7 (room ~~10/10~~ now 6 fixed, training, lobby ~~2/2~~ now 2 fixed, teamsetup ~~2/2~~ now 2 fixed, mpsettings, solomission, network/challenges minor) |
| 2. LB/RB tab cycling | all where applicable | none |
| 3. A acts | all | none |
| 4. B exits at top level | all (post-K) | none |
| 5. Label placement | 4 menus + Settings via helpers | 9 (per-file pickup queued L-fix-6) |
| 6. Modals only where genuinely modal | 11 | 3 (room sub-screens, queued L-fix-5) |

L-fix-1 (Settings labels via helpers), L-fix-2 (Room/Lobby/Teamsetup NavFlattened) ship in this batch. Remaining gaps (per-file label cleanup, Handicaps/Teams/Music inlining, BotSetup-as-inline, training/solomission per-site NavFlattened decisions) are tracked in the audit doc and queued.

## How to add a new menu surface

1. Decide which shape applies (sibling / modal / progressive-focus).
2. Implement using the canonical reference for that shape.
3. **Layout BeginChild calls get `ImGuiChildFlags_NavFlattened`** unless they're scrollable lists.
4. **Widgets get labels on the LEFT** via the `PdCheckbox` / `PdCombo` / `PdSliderInt` / `PdSliderFloat` helpers (or the equivalent pattern in your file).
5. **Multi-tab menus get LB/RB tab cycling** -- automatic if the tabs use ImGui::TabBar and the actionmap drives `ACTION_MENU_TAB_PREV` / `ACTION_MENU_TAB_NEXT`.
6. Hook through menupool (`menupoolAcquireDialog(def, &g_CtxImGuiMenu)` on appearance, `menuPopDialog()` on close). K's invariants apply.

## Verification

For each menu, the gamepad-only flow verification is:

1. Open the menu using only the gamepad.
2. D-pad to every interactive element on the screen. Confirm focus traverses across panels transparently (no engage/disengage step).
3. LB / RB cycles between sibling tabs at the top.
4. A on each focused element triggers the expected behaviour.
5. B from any element steps back exactly one level: a modal closes, a tier reverts, or the menu closes (at top level).
6. Every widget has its label to the LEFT or ABOVE -- never on the right.

If the flow fails any of the six points, the menu is non-conforming and is a refactor candidate.
