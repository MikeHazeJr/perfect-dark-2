# Flat menu navigation audit -- 2026-04-25

Priority L-a deliverable. Maps current nested-vs-flat structure across every ImGui menu. Drives the L-b refactor scope and the L-f methodology doc.

Branch: `claude/stoic-wing-35829b`. HEAD when audit captured: post-Priority-K (`2b69cc12`).

## Mike's pain point (verbatim)

> "Combat Simulator's Bots section requires drill-in via A, and B exits the whole menu instead of popping a level. The fix is the flat-panel model: D-pad navigates everything, A acts only on focused element, B exits only at top."

The directive is system-wide -- main menu, CS, Settings, Pause, MP Setup, every menu module. Convert nested chains to sibling panels where panels should be siblings; preserve genuine modals.

## Classification

Each menu file is one of:

- **Already flat** -- single window, sibling panels navigable via D-pad. Refactor is a no-op.
- **Modal-correct** -- nested dialog pushes are confirms or settings panels that should stay modal. Refactor would degrade UX.
- **Refactor candidate** -- nested dialog pushes that should be sibling panels in the parent. The L-b target.
- **Progressive-focus** -- M-18..M-21 `s_FocusGroup` enum pattern; B-pop steps back one tier. Already the right shape.

## Per-menu findings

### `pdgui_menu_room.cpp` (CS Room) -- 4092 LOC -- **Refactor candidate (Mike's named pain point)**

**Top-level structure**: single ImGui window with three columns: Players (left, with inline bot list + Add Bot button), Match Settings (middle), Options (right). Plus right-click context menu on each player/bot row.

**Drill-in dialogs pushed via menuPushDialog**:
- `g_MpHandicapsMenuDialog` (line 2702) -- "Player Handicaps..." button. Modal-correct: configures per-slot handicap values.
- `g_MpTeamsMenuDialog` (line 2706) -- "Team Setup..." button. Modal-correct: configures team naming.
- `g_MpSelectTunesMenuDialog` (line 2713) -- "Select Music..." button. Modal-correct: per-player playlist editor.

**Add Bot path**: `pdgui_menu_botsetup.cpp` -- pushed when "Add Bot" is clicked. The path is `Add Bot button -> BotSetup dialog (configure new bot) -> close returns to Room`. **BotSetup is the "Bots section" Mike named.** B in BotSetup calls `menuPopDialog()` (line 513) -- correct, pops one level.

**B-button behavior**: B in Room calls `menuPopDialog`, which closes the Room dialog -> returns to Main Menu (the Room's parent). Correct one-level pop *from the Room's perspective*. From the user's perspective in Mike's framing, the Room IS the menu, and B exits "the whole menu" because the Room is the top of its own stack.

**Mike's complaint reconciled**: the symptom name "B exits the whole menu" is most likely referring to one of these:

1. BotSetup -> some sub-modal drilling deeper -> B pops everything back to Room or further. This was the K-b territory: pre-K, force-close paths could pop too aggressively. K-b1 + K-b3 should have closed it. Needs a fresh playtest log to confirm.
2. CS Room -> Add Bot -> BotSetup pushes work, but Mike feels BotSetup should be a "tab" in the Room's column -- i.e. the bot config UI should NOT be a separate dialog at all. This is the genuine flat-refactor candidate.

**Refactor candidate (L-b)**: option (2) above. Convert BotSetup from a pushed dialog into an expandable panel inside the Room's Players column (or a third "Bot Editor" column when a bot is selected). Significant UI restructuring; would require Mike's design eye on the resulting layout.

**Modal-correct**: Handicaps, Team Setup, Music pickers stay as pushed dialogs (configuration panels with their own focus model).

### `pdgui_menu_mainmenu.cpp` (Main Menu) -- 5650 LOC -- **Already flat**

Top-level structure: single ImGui window with `s_MenuView` enum (0 = hub, 1 = Solo, 2 = Settings, 3 = Modding, 4 = Online, 5 = (reserved), 6 = Grid). Each view is a sibling panel rendered conditionally inside the same window. LB/RB cycles between views. D-pad navigates within a view.

Drill-in dialogs pushed:
- `g_ChangeAgentMenuDialog` (line 4649) -- agent select push. Modal-correct (agent slot picker).
- `g_CheatsMenuDialog` (line 4673) -- cheats list push. Modal-correct (its own scrollable list).

Top-level B: closes the menu (correct for the top-level menu).

**Verdict**: already meets Mike's spec. No refactor needed.

### `pdgui_menu_pausemenu.cpp` (solo pause) -- 1300 LOC -- **Already flat**

Tab-based UI (Mission, Inventory, Settings, Game). Sibling tabs navigated via LB/RB or D-pad. A acts on focused widget. B closes the pause menu (top of its stack).

Drill-in: nothing significant. Settings opens as an expandable panel inline.

**Verdict**: already meets Mike's spec.

### `pdgui_menu_mppause.cpp` (MP pause) -- 1227 LOC -- **Modal-correct (with Tier-1 popups)**

Single window with tab-style content. End Game button opens a modal popup confirm (M-1 / S385 work). The popup is genuinely modal -- it's a destructive confirm.

`menuPushDialog` called once at line 563 for sub-dialog target. B in sub-dialog pops correctly (line 510).

**Verdict**: modal-correct as-is.

### `pdgui_menu_mpsetup.cpp` (MP Setup hub) -- 1355 LOC -- **Modal-correct (hub-of-pickers)**

Hub of six pickers (arena, scenario, weapons, limits, etc.) each pushed via legacy menu handlers. The legacy push pattern is described in S389 M-19 as "hub-of-pickers ... progressive focus is realised via push/pop rather than tier-changes-within-a-menu."

Each picker is a modal selection screen with `CLOSEONSELECT` -- A picks, dialog auto-pops back to MP Setup hub. B cancels and pops back. **Correct.**

`g_ExtGameOptionsMenuDialog` push at line 1259 -- modal options panel.

**Verdict**: modal-correct. The pickers are genuinely modal selection screens. Refactor would harm UX (six pickers can't all be visible siblings on one screen at readable size).

### `pdgui_menu_mpadvanced.cpp` -- **Modal-correct (selectable -> sub-dialog handlers)**

Selectable rows that open sub-dialogs via `menuPushDialog(target)` (line 436). Each sub-dialog is a configuration panel; B pops back. Modal-correct.

### `pdgui_menu_mpsettings.cpp` -- **Modal-correct**

Stacking sub-dialogs (e.g. Select Tunes nested inside Music settings). Each level pushes via `menuPushDialog`; B pops one level. Modal-correct.

### `pdgui_menu_solomission.cpp` (solo mission select) -- 3667 LOC -- **Progressive-focus (correct)**

S389 M-18 introduced a `MissionFocusGroup` enum (`FOCUS_MISSION_LIST -> FOCUS_DIFFICULTY -> FOCUS_START`). Within a single window:
- Mission list on the left.
- Difficulty selector on the right (visible after a mission is picked).
- Start button (visible after a difficulty is unlocked).
- Escape steps back one group; START -> DIFFICULTY -> MISSION_LIST -> popDialog.

This is exactly the model Mike named: D-pad navigates within a tier, A advances a tier, B steps back one tier. The whole thing is one window with sibling panels and a tier marker. **Reference implementation.**

Drill-in dialogs at lines 1392, 1569, 1643, 1840 -- `g_PdModeSettingsMenuDialog`, `g_AcceptMissionMenuDialog`, `g_CoopOptionsMenuDialog` -- all modal-correct (settings + confirm).

In-mission dialogs at 2897, 2901, 2905 -- inventory, options, abort confirm. Modal-correct (these open from the in-mission pause menu).

### `pdgui_menu_botsetup.cpp` (BotSetup) -- 1090 LOC -- **Refactor candidate (L-b option)**

Pushed when "Add Bot" is clicked from CS Room. Single ImGui window configures one bot's body / head / type / difficulty / team. B closes (line 513).

**Refactor option**: rather than push BotSetup as a separate dialog, embed it as a "Bot Editor" panel inside the CS Room's Players column when a bot is selected. This matches Mike's "siblings, not children" framing for the Bots section. Cost: substantial Room UI restructuring + needs Mike's design eye on the resulting layout.

**Alternative**: leave BotSetup as a modal dialog (its existing form). The user's complaint may be about a deeper drill (bot character / type sub-pickers), each of which is a CLOSEONSELECT picker -- those are modal-correct.

### `pdgui_menu_agentselect.cpp`, `pdgui_menu_agentcreate.cpp` -- **Modal-correct**

Agent slot selector + new-agent name entry. Both modal-correct (filemgr-style name dialog with confirm/cancel).

### `pdgui_menu_cheats.cpp` -- **Modal-correct**

Cheats list with confirm-unlock popup (M-3, S385 work). Modal-correct.

### `pdgui_menu_training.cpp` -- 12 menuPushDialog sites, **Modal-correct (deeply nested by design)**

The training hub has FR / DT / HT / Bio / Hangar sub-modes, each with progressive-focus internally (M-21 / S389). Multi-level pushes are intentional: a Hangar vehicle row pushes Vehicle Details, which pushes Vehicle Holograph -- each is its own panel with its own back affordance.

**Verdict**: modal-correct. The training hub is genuinely deep.

### `pdgui_menu_lobby.cpp` (Social Lobby) -- **Already flat**

Single window with sibling sections (rooms list, chat, friends, social). No menuPushDialog from the lobby; sub-actions are inline.

### `pdgui_menu_endscreen.cpp` -- **Already flat**

Single window with stat sections + action bar (Next Mission / Retry / Main Menu). No drill-ins.

## Refactor candidates ranked by user-visible impact

| Rank | Menu | Refactor | Estimated effort | Mike-design-decision needed |
|------|------|----------|------------------|----------------------------|
| 1 | CS Room + BotSetup | Embed BotSetup into Room as a Bot Editor sibling panel rather than pushed dialog. | Multi-day. Room layout already crowded; needs a column or tab-mode redesign. | YES -- multiple plausible layouts. |
| 2 | CS Room sub-screens (Handicaps / Teams / Music) | Convert pushed dialogs to expandable inline panels. | Half-day each. Lower confidence the inline form would be readable on the existing Room layout. | YES -- whether to inline or keep modal. |
| 3 | Inline progressive-focus tier markers in CS Room | Add an `s_RoomFocusGroup` enum (Players -> Settings -> Options -> Start Match) so D-pad cycles through the columns explicitly with a visible focus marker. | Half-day. Pure consistency with solo mission's M-18 pattern. | NO -- mechanical adoption of the M-18 pattern. |

Items below #3 are modal-correct or already flat per the per-menu findings; no refactor.

## What this audit confirms

- The vast majority of the menus already meet Mike's spec.
- The genuine refactor candidates are concentrated in the **CS Room and its BotSetup drill-in**.
- Mike's specific "B exits the whole menu" symptom is most plausibly explained by the K-b drift class -- which the K commits already structurally close. **A fresh playtest log post-K is the next signal**: if the symptom persists, escalate to a Bots-as-sibling-panel refactor (item 1 above); if it does not, the refactor is opt-in cosmetic.

## Plan for L-b/c/d/e/f

Given that #1 and #2 above are design decisions Mike should drive, I will:

- **L-b (light)**: implement #3 above (CS Room progressive-focus tier markers) as a mechanical adoption of the solo-mission pattern. This unifies the focus model across the two main gameplay-entry menus without changing any visual layout. ~1 day of work, covered in this batch.
- **L-c**: confirm modals respect K's invariant (already verified by the K-d assertion -- modal pushes go through menupool with ctx).
- **L-d**: update controller-hint footers in the CS Room to reflect the M-18 / new tier model.
- **L-e**: gamepad-only flow verification -- inspect the path Main Menu -> CS Room -> Add Bot -> Configure -> Start Match. If any step has a drill-in that requires keyboard or has a B-jump-too-far symptom, surface it.
- **L-f**: methodology doc `context/designs/flat-menu-navigation.md` documenting the "siblings vs modals" rule and pointing readers at solomission.cpp + room.cpp as canonical reference implementations.

The deeper #1 / #2 refactors are out of scope for this batch -- they need Mike's per-menu design call. Surfacing them as queued L-follow-up rather than burning hours on speculative restructure.

## Co-existence

No menu file in this audit is on the FP-weapon off-limits list. K's input-authority invariants (post-2026-04-25) are honoured by every menu listed above.
