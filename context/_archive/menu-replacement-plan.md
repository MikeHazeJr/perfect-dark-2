# Menu System Full Replacement Plan

> Design document for replacing all 240 legacy PD menu dialogs with ImGui.
> Back to [index](README.md)

---

## 1. Current State

**240 total menu definitions** across 13 source files.
**6 have ImGui replacements** (Agent Select, Agent Create, Main Menu x2, Match Setup, Network).
**234 remaining.**

The hot-swap system (pdgui_hotswap) allows per-dialog override — legacy and ImGui can
coexist during migration. F8 toggles between old and new for any registered dialog.

## 2. Strategy

**Replace by category, not individually.** Group related menus, replace the group as a
unit, test the full flow, then move to the next group. Each group is a self-contained
ImGui screen with its own render function registered via pdguiHotswapRegister.

**Wire through menu manager.** Every new ImGui menu uses menuPush/menuPop for transitions.
The menu manager's cooldown prevents double-press across ALL menus.

**Maintain feature parity.** Every button, slider, dropdown, and text input in the legacy
menu must have an equivalent in the ImGui replacement. Don't ship a replacement that's
missing functionality.

## 3. Replacement Groups (Priority Order)

### Group 1: Solo Mission Flow (11 menus) — HIGH PRIORITY
The most played path in the game.

| Legacy Dialog | What It Does |
|---------------|-------------|
| g_SelectMissionMenuDialog | Select which mission to play |
| g_SoloMissionDifficultyMenuDialog | Choose difficulty (Agent/Special/Perfect) |
| g_SoloMissionBriefingMenuDialog | Read mission briefing + objectives |
| g_SoloMissionControlStyleMenuDialog | Choose control layout |
| g_SoloMissionInventoryMenuDialog | Manage inventory before mission |
| g_AcceptMissionMenuDialog | Confirm and start |
| g_SoloMissionPauseMenuDialog | In-game pause |
| g_SoloMissionOptionsMenuDialog | In-game options |
| g_MissionAbortMenuDialog | Abort confirmation |
| g_PreAndPostMissionBriefingMenuDialog | Pre/post mission debrief |
| g_FrWeaponsAvailableMenuDialog | Training weapons |

**One ImGui file:** `pdgui_menu_solomission.cpp`

### Group 2: End Screens (13 menus)
Post-mission results for solo and 2-player.

| Legacy Dialog | What It Does |
|---------------|-------------|
| g_SoloMissionEndscreenCompletedMenuDialog | Mission complete |
| g_SoloMissionEndscreenFailedMenuDialog | Mission failed |
| g_SoloEndscreenObjectivesCompletedMenuDialog | All objectives done |
| g_SoloEndscreenObjectivesFailedMenuDialog | Objectives failed |
| g_RetryMissionMenuDialog | Retry option |
| g_NextMissionMenuDialog | Next mission |
| + 6 more 2-player variants | Same screens in splitscreen layout |

**One ImGui file:** `pdgui_menu_endscreen.cpp`

### Group 3: Options & Settings (11 menus)
Audio, video, controls — shared across all modes.

Already partially in the main menu ImGui (settings view).
Extend to cover all option dialogs including 2-player variants.

**Extend existing:** `pdgui_menu_mainmenu.cpp` settings view

### Group 4: Multiplayer Setup (68 menus) — LARGEST GROUP
Teams, weapons, simulants, challenges, save/load.

Many of these are already partially covered by the match setup ImGui.
The remaining dialogs are sub-screens accessed from the setup flow.

**Multiple ImGui files:**
- `pdgui_menu_teamsetup.cpp` — teams, auto-team, team names
- `pdgui_menu_simulants.cpp` — add/edit/change simulants
- `pdgui_menu_weapons.cpp` — weapon selection, random weapons
- `pdgui_menu_challenges.cpp` — challenge list, confirm, complete/failed
- `pdgui_menu_mpsettings.cpp` — limits, handicaps, soundtrack, save/load

### Group 5: Multiplayer In-Game (15 menus)
Pause, stats, rankings, endgame.

**One ImGui file:** `pdgui_menu_mpingame.cpp`

### Group 6: File Manager (25 menus)
File operations, save/load, error dialogs.

Agent Select and Agent Create are already done.
Remaining: operations menu, rename, delete, copy, error dialogs.

**One ImGui file:** `pdgui_menu_filemgr.cpp`

### Group 7: Training Mode (24 menus)
Free Training, Biographies, Datatheque, Hangar.

**One ImGui file:** `pdgui_menu_training.cpp`

### Group 8: Cheats (9 menus)
Cheat unlock, category menus.

**One ImGui file:** `pdgui_menu_cheats.cpp`

### Group 9: Misc & Error Dialogs (remaining)
PAK errors, 4MB menus (can be removed — N64 only), active menu, cinema.

**Extend existing warning dialog type-based system** for most of these.

## 4. Implementation Pattern

For each group:

1. Read all legacy dialog definitions (menudialogdef + menuitem arrays)
2. Map every menu item to an ImGui equivalent
3. Create the ImGui render function
4. Register via pdguiHotswapRegister for each dialog in the group
5. Wire through menu manager (menuPush/menuPop)
6. Test full flow with F8 toggle to compare old vs new
7. Once validated, set override to force NEW

## 5. Disabling the Legacy System

Once ALL 240 dialogs have ImGui replacements:

1. Set all hotswap overrides to force NEW
2. Remove F8 toggle (no longer needed)
3. The legacy menu rendering code (menuRenderDialog, dialogRender, etc.) becomes dead code
4. Can be removed in a cleanup pass, but not urgent — it's just unused code

The legacy system doesn't need to be "disabled" explicitly — the hotswap system
already suppresses it when ImGui is active for a given dialog. The transition is
gradual and reversible at every step.

---

*Created: 2026-03-26, Session 48*
