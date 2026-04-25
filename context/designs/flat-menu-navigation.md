# Flat menu navigation

Companion to `context/audits/flat-menu-navigation-audit-2026-04-25.md` (Priority L-a). Defines the rule for when a menu surface should be a sibling panel vs a modal vs a progressive-focus tier.

## The principle

> **D-pad navigates everything.** **A acts on the focused element only.** **B exits only at the top level of the menu.**

Three menu shapes carry the rule:

1. **Sibling panels** -- multiple sections inside one window, navigable via D-pad without an "enter via A" step. The user sees them all at once. This is the right shape for surfaces with three to six related controls / lists that the user wants to switch between freely.

2. **Modal** -- a separate dialog that appears over the parent for a constrained task: pick one item from a long list, confirm a destructive action, configure a sub-feature. A inside the modal acts on its focused widget; B closes the modal and returns to the parent. Modals nest (a modal can open a modal) but each level pops one tier on B.

3. **Progressive-focus tiers** -- one window, one tier marker (`s_FocusGroup` enum). The window shows the panels for the current tier; A advances to the next tier, B steps back one tier. This is the right shape for sequential-decision flows ("pick mission -> pick difficulty -> start").

If a surface fits the sibling-panel shape, **use sibling panels**. If a surface fits a modal, use a modal. If a surface fits progressive-focus, use that. Don't mix shapes within one screen.

## Reference implementations

- **Sibling panels**: `pdgui_menu_mainmenu.cpp`. Uses `s_MenuView` to switch between Hub, Solo, Settings, Modding, Online, Grid sub-views inside one window. LB/RB cycles between sub-views; D-pad navigates within the active sub-view; A acts; B closes the menu.
- **Modal**: `pdgui_menu_warning.cpp` (the canonical modal dialog primitive used for End Match / Abort Mission / Confirm Unlock / Delete Theme confirms). Built on `ImGui::BeginPopupModal` + `pdguiPopupDarkenBehind`.
- **Progressive-focus**: `pdgui_menu_solomission.cpp` (M-18, S389). `MissionFocusGroup` enum (`MISSION_LIST -> DIFFICULTY -> START`); A advances; B steps back one tier; Escape on the top tier pops the dialog; only START commits.

## Rules

### R1. Don't drill in for a sibling

If a section reads as "and also" rather than "step into", it's a sibling. Examples:
- CS Room's Players column + Match Settings column + Options column -- siblings.
- Pause menu's tabs (Mission / Inventory / Settings) -- siblings.
- Main Menu's Solo / Settings / Modding / Online / Grid sub-views -- siblings.

Don't push a sibling as a modal dialog. Don't require A to enter it. The user navigates with the D-pad.

### R2. Modals are for constrained tasks

A modal is appropriate when:
- The user must pick exactly one item from a list (arena picker, scenario picker, agent slot picker).
- The action is destructive and benefits from an explicit confirm (End Match, Abort Mission, Delete Theme).
- The sub-feature has its own focus model that would clash with the parent (Music playlist editor, Bot configuration -- *if* the bot config UI is too dense to inline).

If those conditions don't apply, prefer a sibling panel.

### R3. Progressive-focus for sequential flows

A progressive-focus tier model is appropriate when:
- The user must complete tier N before tier N+1 makes sense.
- Going back to tier N-1 lets them change a decision.
- B at tier N reverts to tier N-1; B at tier 0 closes the dialog.

Solo Mission Select is the canonical case (mission -> difficulty -> start). MP Setup hub is *not* progressive-focus (it's a hub-of-modal-pickers because the choices are independent).

### R4. B-button discipline

- At the top level of a menu, B closes the menu and returns to the prior surface (gameplay or the parent menu).
- One level inside (a modal, a deeper tier, a pushed sub-dialog), B steps back **one** level.
- The user must never observe "B at level 2 jumped past level 1 and closed the whole thing" -- that's the K-class two-stack drift symptom and is structurally prevented by the K commits' menupool / inputctx invariants. If observed, file a drift report (LOG_WARNING `MENUPOOL: drift --` prints in the log).

### R5. Controller-hint footers

Every menu shows a controller-hint footer that reflects its actual behaviour. The footer wording is one of:
- "[A] Select / [B] Back" -- progressive-focus or modal.
- "[D-pad] Navigate / [A] Activate / [B] Close" -- sibling panels at the top level.
- "[LB/RB] Switch tab / [D-pad] Navigate / [A] Activate / [B] Close" -- multi-tab sibling layout.

If a menu's actual behaviour diverges from one of these strings, fix the menu, not the footer.

## How to add a new menu surface

1. Decide which shape applies (sibling / modal / progressive-focus). Re-read R1-R3 if uncertain.
2. Implement using the canonical reference for that shape:
   - Sibling: ImGui::Begin window with internal sections + D-pad nav.
   - Modal: ImGui::BeginPopupModal + `pdguiPopupDarkenBehind` + the warning.cpp pattern.
   - Progressive-focus: the solo-mission `s_FocusGroup` pattern.
3. Hook through menupool: `menupoolAcquireDialog(def, &g_CtxImGuiMenu)` on appearance, `menuPopDialog()` on close. K's invariants apply.
4. Add the controller-hint footer using the matching R5 string.
5. Build and confirm gamepad-only flow: starting from the parent surface, the user reaches every interactive element with D-pad + A and exits cleanly with B. No "press A to enter Bots" hidden tier.

## Audit findings (2026-04-25)

The audit (`context/audits/flat-menu-navigation-audit-2026-04-25.md`) found:

- **Already flat / modal-correct / progressive-focus**: 11 menus need no refactor.
- **Refactor candidates**: 3 in the CS Room (BotSetup as sibling panel, Handicaps / Teams / Tunes inlining, progressive-focus tier markers in Room).
- **Likely-already-fixed by K**: Mike's "B exits the whole menu" symptom is most plausibly the two-stack drift class structurally closed by K-b1 + K-b3. A fresh playtest log is the next signal.

Items 1 and 2 of the refactor candidates are design decisions that need Mike's eye -- they're queued for a future L follow-up rather than burned through speculative this batch. Item 3 (Room tier markers) is mechanical and queued as L-follow-up alongside.

## Verification

For each menu, the gamepad-only flow verification is:

1. Open the menu from its parent surface using only the gamepad.
2. D-pad to every interactive element on the screen. Confirm focus is visible on each.
3. A on each focused element triggers the expected behaviour (open a modal, advance a tier, toggle, activate).
4. B from any element steps back exactly one level: a modal closes, a tier reverts, or the menu closes.
5. No element is reachable only via keyboard.

If the flow fails, the menu doesn't meet the spec and is a refactor candidate.
