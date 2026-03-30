# ADR-001: Menu Storyboard & ImGui Migration System

**Status:** Proposed
**Date:** 2026-03-18
**Deciders:** Mike (sole developer)
**Phase:** D4 (ImGui Menu Preview System)

---

## Context

Perfect Dark has ~100 menu dialogs rendered by the original procedural system (vertex-colored triangles, CI4 bitmap fonts, GBI display lists → OpenGL translator). We need to systematically rebuild each menu in ImGui while preserving the PD aesthetic — same gradients, colors, shimmer — but with modern rendering and the flexibility to extend (custom themes, resolution independence, etc.).

The challenge: rebuilding 100 menus is a multi-month effort. We need a way to work on them iteratively, preview old vs. new side-by-side, and track which ones are production-ready. This ADR defines the **Menu Storyboard** — a controller-navigable carousel that decouples menu appearance from functionality, letting us build and QA each menu visually before wiring it into the game.

### Forces at Play
- Can't wire new menus into the game until they look right
- Need to see old vs. new to judge fidelity
- Some menus need mock data to look correct (e.g., player rankings need names/scores)
- Controller must work (this is a console-origin game, mouse is secondary)
- Custom themes (Black & Gold, etc.) must tint properly on new menus
- Must be `#ifdef PD_DEV_PREVIEW` — ships as dev tooling, not in release builds

---

## Decision

Build the Menu Storyboard as a standalone ImGui mode (F11 key), separate from the F12 debug overlay. It renders a left-panel catalog and a right-panel preview area. Controller input navigates the catalog, X toggles old/new rendering, and Y cycles a per-menu quality rating. The system lives in two new files: `pdgui_storyboard.cpp` (UI + navigation) and `pdgui_menubuilder.cpp` (ImGui rebuilds of individual menus).

---

## Architecture

### System Overview

```
┌──────────────────────────────────────────────────────────┐
│  F11 Storyboard Mode (fullscreen ImGui, replaces scene)  │
│                                                          │
│  ┌─────────────┐  ┌──────────────────────────────────┐   │
│  │  CATALOG     │  │  PREVIEW AREA                    │   │
│  │             │  │                                  │   │
│  │ ▸ Game Over │  │  ┌────────────────────────────┐  │   │
│  │   Rankings  │  │  │  [OLD] / [NEW] badge       │  │   │
│  │   Stats     │  │  │                            │  │   │
│  │   ───────── │  │  │  Game Over                 │  │   │
│  │   Pause     │  │  │  ════════════════════      │  │   │
│  │   End Game  │  │  │                            │  │   │
│  │   ───────── │  │  │  1st  DarkStar    47      │  │   │
│  │   Game Setup│  │  │  2nd  Jo Dark     38      │  │   │
│  │   Arena     │  │  │  3rd  SpySim#2    22      │  │   │
│  │   Weapons   │  │  │                            │  │   │
│  │   Players   │  │  │  [Press START to exit]     │  │   │
│  │   Options   │  │  └────────────────────────────┘  │   │
│  │             │  │                                  │   │
│  │  [Y] Rate:  │  │  State: ■ Good  □Fine  □Inc  □Redo │
│  │  ■ Good     │  │  Theme: Blue (active)            │   │
│  └─────────────┘  └──────────────────────────────────┘   │
└──────────────────────────────────────────────────────────┘
```

### Rendering Modes

When viewing a menu entry:
- **OLD mode** (default): Renders the original PD dialog using the existing native menu system (GBI pipeline). The storyboard calls into `menuPushDialog` with the real `menudialogdef`, captures its output to a framebuffer, and composites it into the preview area. The menu is decoupled from game logic — handlers are stubbed.
- **NEW mode** (X toggle): Renders the ImGui rebuild of the same menu. Uses `pdgui_menubuilder.cpp` functions that draw the equivalent layout using ImGui widgets styled by `pdgui_style.cpp`.

### Color Tinting Strategy

The NEW ImGui menus inherit from the PD palette system already in `pdgui_style.cpp`, but with a twist for dialog-type awareness:

Each original PD dialog has a `MENUDIALOGTYPE` (DEFAULT=blue, DANGER=red, SUCCESS=green). The ImGui rebuild applies a **tint layer** from the original dialog's color palette on top of the custom theme. This way:
- If you're in the Black & Gold theme viewing a DANGER dialog, the gold tones shift warm/red
- The `g_MenuColours[dialogType]` palette provides the tint source
- The active custom theme provides the base
- Blend: `finalColor = lerp(themeColor, dialogTintColor, 0.3)` — 30% tint, 70% theme

This is implemented as a new function: `pdguiApplyDialogTint(MENUDIALOGTYPE type)`.

### Quality Rating System

Each menu entry has a persistent quality state saved to `storyboard_ratings.json`:

| State | Value | Meaning | Color |
|-------|-------|---------|-------|
| Good | 1 | Production-ready, matches or improves on OG | Green |
| Fine | 2 | Acceptable, minor polish needed | Blue |
| Incomplete | 3 | Partially built, missing elements | Yellow |
| Redo | 4 | Needs full rebuild | Red |

Y button cycles: Good → Fine → Incomplete → Redo → Good.

Ratings persist across sessions via JSON file in the project root. The storyboard header shows aggregate stats: "24/98 Good, 15/98 Fine, 8/98 Incomplete, 3/98 Redo, 48/98 Unrated".

---

## File Structure

```
port/fast3d/pdgui_storyboard.cpp     — Storyboard UI, catalog, navigation, rating persistence
port/fast3d/pdgui_menubuilder.cpp    — ImGui rebuilds of individual menus (the big file)
port/include/pdgui_storyboard.h      — Public C API (pdguiStoryboardInit, Toggle, Render, IsActive)
port/include/pdgui_menubuilder.h     — Registry of menu builder functions
storyboard_ratings.json              — Persisted quality ratings (generated at runtime)
```

### Integration Points

```c
// pdgui_storyboard.h
void pdguiStoryboardInit(void);        // Called from pdguiInit()
void pdguiStoryboardToggle(void);      // F11 key
void pdguiStoryboardRender(void);      // Called from pdguiRender() when active
s32  pdguiStoryboardIsActive(void);    // Gate for input isolation
s32  pdguiStoryboardProcessEvent(void *sdlEvent);  // Controller input
```

```c
// In pdgui_backend.cpp:
void pdguiRender(void) {
    if (!g_PdguiActive && !pdguiStoryboardIsActive()) return;
    // ... existing debug menu render ...
    if (pdguiStoryboardIsActive()) {
        pdguiStoryboardRender();  // Takes over full screen
    }
}
```

### Controller Mapping

| Button | Action |
|--------|--------|
| D-pad Up/Down | Navigate catalog list |
| D-pad Left/Right | Switch category tabs |
| A | Select entry → show in preview |
| B | Back / deselect |
| X | Toggle OLD ↔ NEW rendering |
| Y | Cycle quality rating (1→2→3→4→1) |
| LB/RB | Cycle theme (palette 0-6) |
| START | Exit storyboard |

---

## Menu Inventory (98 dialogs, 12 categories)

### Category 1: Combat Simulator — Endscreen (7 menus)
| # | Variable | Title | Type | Mock Data Needed |
|---|----------|-------|------|-----------------|
| 1 | g_MpEndscreenIndGameOverMenuDialog | "Game Over" | DEFAULT | Player names, scores |
| 2 | g_MpEndscreenTeamGameOverMenuDialog | "Game Over" | DEFAULT | Team names, scores |
| 3 | g_MpEndscreenPlayerRankingMenuDialog | "Player Ranking" | DEFAULT | 4 ranked players |
| 4 | g_MpEndscreenTeamRankingMenuDialog | "Team Ranking" | DEFAULT | Team standings |
| 5 | g_MpEndscreenPlayerStatsMenuDialog | "Stats For..." | DEFAULT | Kill/death/accuracy |
| 6 | g_MpEndscreenChallengeCompletedMenuDialog | "Challenge Completed!" | SUCCESS | Challenge name |
| 7 | g_MpEndscreenChallengeFailedMenuDialog | "Challenge Failed!" | DANGER | Challenge name |

### Category 2: Combat Simulator — Pause (6 menus)
| # | Variable | Title | Type | Mock Data |
|---|----------|-------|------|-----------|
| 8 | g_MpPauseControlMenuDialog | Scenario Name | DEFAULT | Scenario title |
| 9 | g_MpPauseInventoryMenuDialog | "Inventory" | DEFAULT | Weapon list |
| 10 | g_MpPausePlayerStatsMenuDialog | "Stats For..." | DEFAULT | Kill/death stats |
| 11 | g_MpPausePlayerRankingMenuDialog | "Player Ranking" | DEFAULT | Rankings |
| 12 | g_MpPauseTeamRankingsMenuDialog | "Team Ranking" | DEFAULT | Team standings |
| 13 | g_MpEndGameMenuDialog | "End Game" | DANGER | — |

### Category 3: Combat Simulator — Setup (~12 menus)
| # | Variable | Title | Type | Mock Data |
|---|----------|-------|------|-----------|
| 14 | g_MpArenaMenuDialog | "Arena" | DEFAULT | Arena list |
| 15 | g_MpWeaponsMenuDialog | "Weapons" | DEFAULT | Weapon sets |
| 16 | g_MpGameSetupMenuDialog | "Game Setup" | DEFAULT | Options |
| 17 | g_MpPlayerSetupMenuDialog | Player name | DEFAULT | Body/head |
| 18 | g_MpSimulantListMenuDialog | "Simulants" | DEFAULT | Bot list |
| 19 | g_MpAddSimulantMenuDialog | "Add Simulant" | DEFAULT | Types |
| 20 | g_MpChangeSimulantMenuDialog | "Change Simulant" | DEFAULT | Types |
| 21 | g_MpEditSimulantMenuDialog | "Edit Simulant" | DEFAULT | Personality |
| 22 | g_MpLimitsMenuDialog | "Limits" | DEFAULT | — |
| 23 | g_MpHandicapsMenuDialog | "Handicaps" | DEFAULT | Player list |
| 24 | g_MpDropOutMenuDialog | "Drop Out" | DANGER | — |
| 25 | g_MpSaveSetupNameMenuDialog | "Save Setup" | DEFAULT | Keyboard |

### Category 4: Solo Mission (8 menus)
| # | Variable | Title | Type | Mock Data |
|---|----------|-------|------|-----------|
| 26 | g_PreAndPostMissionBriefingMenuDialog | "Briefing" | DEFAULT | Mission text |
| 27 | g_SoloMissionEndscreenCompletedMenuDialog | Stage name | SUCCESS | Objectives |
| 28 | g_SoloMissionEndscreenFailedMenuDialog | Stage name | DANGER | Objectives |
| 29 | g_SoloEndscreenObjectivesCompletedMenuDialog | "Objectives" | SUCCESS | Checklist |
| 30 | g_SoloEndscreenObjectivesFailedMenuDialog | "Objectives" | DANGER | Checklist |
| 31 | g_RetryMissionMenuDialog | Mission name | DEFAULT | — |
| 32 | g_NextMissionMenuDialog | Mission name | DEFAULT | — |
| 33 | g_MissionContinueOrReplyMenuDialog | Stage name | DEFAULT | — |

### Category 5: Co-op / Counter-op (8 menus)
| # | Variable | Title | Type | Mock Data |
|---|----------|-------|------|-----------|
| 34 | g_2PMissionEndscreenCompletedHMenuDialog | Stage name | SUCCESS | — |
| 35 | g_2PMissionEndscreenFailedHMenuDialog | Stage name | DANGER | — |
| 36 | g_2PMissionEndscreenCompletedVMenuDialog | "Completed" | SUCCESS | — |
| 37 | g_2PMissionEndscreenFailedVMenuDialog | "Failed" | DANGER | — |
| 38 | g_2PMissionEndscreenObjectivesFailedVMenuDialog | "Objectives" | DANGER | — |
| 39 | g_2PMissionEndscreenObjectivesCompletedVMenuDialog | "Objectives" | SUCCESS | — |
| 40 | g_2PMissionInventoryHMenuDialog | "Inventory" | DEFAULT | Weapons |
| 41 | g_2PMissionInventoryVMenuDialog | "Inventory" | DEFAULT | Weapons |

### Category 6: Training (22 menus)
| # | Variable | Title | Type | Mock Data |
|---|----------|-------|------|-----------|
| 42 | g_FrWeaponListMenuDialog | "Weapon" | DEFAULT | Weapon list |
| 43 | g_FrDifficultyMenuDialog | "Difficulty" | DEFAULT | — |
| 44 | g_FrTrainingInfoPreGameMenuDialog | "Training Info" | DEFAULT | Weapon info |
| 45 | g_FrTrainingInfoInGameMenuDialog | "Training Info" | DEFAULT | Weapon info |
| 46 | g_FrCompletedMenuDialog | "Training Stats" | SUCCESS | Score/time |
| 47 | g_FrFailedMenuDialog | "Training Stats" | DANGER | Score/time |
| 48 | g_DtListMenuDialog | "Device List" | DEFAULT | Device list |
| 49 | g_DtDetailsMenuDialog | Device name | DEFAULT | Description |
| 50 | g_DtCompletedMenuDialog | "Training Stats" | SUCCESS | Score/time |
| 51 | g_DtFailedMenuDialog | "Training Stats" | DANGER | Score/time |
| 52 | g_HtListMenuDialog | "Holotraining" | DEFAULT | Training list |
| 53 | g_HtDetailsMenuDialog | Training name | DEFAULT | Description |
| 54 | g_HtCompletedMenuDialog | "Training Stats" | SUCCESS | Score/time |
| 55 | g_HtFailedMenuDialog | "Training Stats" | DANGER | Score/time |
| 56 | g_BioListMenuDialog | "Information" | DEFAULT | Bio categories |
| 57 | g_BioProfileMenuDialog | "Character Profile" | DEFAULT | Character data |
| 58 | g_BioTextMenuDialog | Character name | DEFAULT | Bio text |
| 59 | g_HangarListMenuDialog | "Hangar Information" | DEFAULT | Vehicle list |
| 60 | g_HangarVehicleDetailsMenuDialog | Vehicle name | DEFAULT | Description |
| 61 | g_HangarLocationDetailsMenuDialog | Location name | DEFAULT | Description |
| 62 | g_HangarVehicleHolographMenuDialog | "Holograph" | DEFAULT | — |
| 63 | g_NowSafeMenuDialog | "Cheats" | DEFAULT | Cheat list |

### Category 7: File Management (20 menus)
| # | Variable | Title | Type | Mock Data |
|---|----------|-------|------|-----------|
| 64 | g_FilemgrFileSelectMenuDialog | "Perfect Dark" | DEFAULT | Agent files |
| 65 | g_FilemgrEnterNameMenuDialog | "Enter Agent Name" | DEFAULT | Keyboard |
| 66 | g_FilemgrOperationsMenuDialog | "Game Files" | DEFAULT | File list |
| 67 | g_FilemgrRenameMenuDialog | "Change File Name" | DEFAULT | Keyboard |
| 68 | g_FilemgrDuplicateNameMenuDialog | "Duplicate File Name" | DEFAULT | — |
| 69 | g_FilemgrSelectLocationMenuDialog | "Select Location" | DEFAULT | Locations |
| 70 | g_FilemgrCopyMenuDialog | "Copy File" | DEFAULT | File info |
| 71 | g_FilemgrDeleteMenuDialog | "Delete File" | DEFAULT | File info |
| 72 | g_FilemgrConfirmDeleteMenuDialog | "Warning" | DANGER | — |
| 73 | g_FilemgrFileInUseMenuDialog | "Error" | DANGER | — |
| 74 | g_FilemgrErrorMenuDialog | "Error" | DANGER | — |
| 75 | g_FilemgrFileSavedMenuDialog | "Cool!" | SUCCESS | — |
| 76 | g_FilemgrSaveErrorMenuDialog | Error title | DANGER | — |
| 77 | g_FilemgrFileLostMenuDialog | Error title | DANGER | — |
| 78 | g_FilemgrSaveElsewhereMenuDialog | "Save" | DANGER | — |
| 79 | g_PakNotOriginalMenuDialog | Error title | DANGER | — |
| 80 | g_PakChoosePakMenuDialog | "Controller Pak Menu" | DEFAULT | Pak list |
| 81 | g_PakGameNotesMenuDialog | "Game Notes" | DEFAULT | Note list |
| 82 | g_PakDeleteNoteMenuDialog | "Delete Game Note" | DANGER | — |
| 83 | g_ChooseLanguageMenuDialog | Language title | DEFAULT | Languages |

### Category 8: Controller Pak Errors (6 menus)
| # | Variable | Title | Type | Mock Data |
|---|----------|-------|------|-----------|
| 84 | g_PakDamagedMenuDialog | "Damaged Controller Pak" | DANGER | — |
| 85 | g_PakFullMenuDialog | "Full Controller Pak" | DANGER | — |
| 86 | g_PakRemovedMenuDialog | "Error" | DANGER | — |
| 87 | g_PakCannotReadGameBoyMenuDialog | "Error" | DANGER | — |
| 88 | g_PakDataLostMenuDialog | "Error" | DANGER | — |
| 89 | g_PakRepairSuccessMenuDialog | "Repair Successful" | SUCCESS | — |
| 90 | g_PakRepairFailedMenuDialog | "Repair Failed" | DANGER | — |
| 91 | g_PakAttemptRepairMenuDialog | "Attempt Repair" | DANGER | — |

### Category 9: Extended Options / Port (11 menus)
| # | Variable | Title | Type | Mock Data |
|---|----------|-------|------|-----------|
| 92 | g_ExtendedMenuDialog | "Extended Options" | DEFAULT | — |
| 93 | g_ExtendedMouseMenuDialog | "Extended Mouse Options" | DEFAULT | Sliders |
| 94 | g_ExtendedStickMenuDialog | "Analog Stick Settings" | DEFAULT | Sliders |
| 95 | g_ExtendedControllerMenuDialog | "Controller Options" | DEFAULT | Bindings |
| 96 | g_ExtendedVideoMenuDialog | "Extended Video Options" | DEFAULT | Settings |
| 97 | g_ExtendedAudioMenuDialog | "Extended Audio Options" | DEFAULT | Volumes |
| 98 | g_ExtPlayerGameOptionsMenuDialog | "Game Options" | DEFAULT | Toggles |
| 99 | g_ExtendedGameCrosshairColourMenuDialog | "Crosshair Colour" | DEFAULT | Color picker |
| 100 | g_ExtendedBindsMenuDialog | "Bindings" | DEFAULT | Key list |
| 101 | g_ExtendedBindKeyMenuDialog | "Bind" | SUCCESS | — |
| 102 | g_ExtendedSelectPlayerMenuDialog | "Select Player" | DEFAULT | Player list |

### Category 10: Network (8 menus)
| # | Variable | Title | Type | Mock Data |
|---|----------|-------|------|-----------|
| 103 | g_NetMenuDialog | "Network Game" | DEFAULT | — |
| 104 | g_NetHostMenuDialog | "Host Network Game" | DEFAULT | Settings |
| 105 | g_NetCoopHostMenuDialog | "Host Co-op Mission" | DEFAULT | Settings |
| 106 | g_NetJoinMenuDialog | "Join Network Game" | DEFAULT | Server list |
| 107 | g_NetJoinAddressDialog | "Enter Address" | SUCCESS | IP input |
| 108 | g_NetJoiningDialog | "Joining Game..." | SUCCESS | Status |
| 109 | g_NetPauseControlsMenuDialog | "Controls" | DEFAULT | — |
| 110 | g_NetRecentServersMenuDialog | "Recent Servers" | DEFAULT | Server list |

### Category 11: Challenge Special
| # | Variable | Title | Type | Mock Data |
|---|----------|-------|------|-----------|
| 111 | g_MpEndscreenChallengeCheatedMenuDialog | "Challenge Cheated!" | DANGER | — |
| 112 | g_MpEndscreenSavePlayerMenuDialog | "Save Player" | DEFAULT | Name input |
| 113 | g_MpEndscreenConfirmNameMenuDialog | "Player Name" | DEFAULT | — |

---

## Design System: ImGui Menu Components

### Design Tokens (from pdgui_style.cpp palette system)

All tokens derive from the active `menucolourpalette` struct. No hardcoded colors.

| Token | Source | Usage |
|-------|--------|-------|
| `--pd-border-primary` | dialog_border1 | Window borders, separators |
| `--pd-border-accent` | dialog_border2 | Check marks, active indicators |
| `--pd-title-bg` | dialog_titlebg | Title bar gradient background |
| `--pd-title-fg` | dialog_titlefg | Title text color |
| `--pd-body-bg` | dialog_bodybg | Window body background |
| `--pd-item-normal` | item_unfocused | Normal menu item text |
| `--pd-item-disabled` | item_disabled | Greyed-out text |
| `--pd-item-focus-inner` | item_focused_inner | Focused item highlight |
| `--pd-item-focus-outer` | item_focused_outer | Focus outer glow |
| `--pd-check` | checkbox_checked | Checkbox/radio check color |
| `--pd-group-bg` | listgroup_headerbg | List group header background |
| `--pd-group-fg` | listgroup_headerfg | List group header text |

### Spacing Tokens

| Token | Value | Usage |
|-------|-------|-------|
| `PD_PAD_DIALOG` | 4px | Dialog internal padding |
| `PD_PAD_ITEM` | 2px | Space between menu items |
| `PD_TITLE_HEIGHT` | 20px | Title bar height |
| `PD_BORDER_WIDTH` | 1px | Border line width |
| `PD_SHIMMER_WIDTH` | 10px | Shimmer overlay width |
| `PD_ITEM_HEIGHT` | 16px | Standard menu item row height |
| `PD_SEPARATOR_HEIGHT` | 1px | Separator line |

### Typography Tokens

| Token | Value | Usage |
|-------|-------|-------|
| `PD_FONT_TITLE` | HandelGothic 24pt | Dialog titles |
| `PD_FONT_ITEM` | HandelGothic 18pt | Menu item text |
| `PD_FONT_SMALL` | HandelGothic 14pt | Stats, fine print |
| `PD_FONT_WAVE` | HandelGothic 24pt + wave shader | Animated title text |

### Component Library

#### PdDialog (window frame)
```
┌─ border1 ────────────────────── shimmer ─┐
│ ▓▓▓▓▓ Title Bar (gradient) ▓▓▓▓▓▓▓▓▓▓▓▓│
│─── border1 ──────────────────────────────│
│                                          │
│  Body area (dialog_bodybg)               │
│                                          │
│  [menu items rendered here]              │
│                                          │
└─ border1 ────────────────────── shimmer ─┘
```
Props: title (string), type (MENUDIALOGTYPE), width, height, focused (bool)

#### PdMenuItem (selectable row)
```
┌──────────────────────────────────────────┐
│  Label text                    [value]   │  ← unfocused: item_unfocused color
│ ▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓ │  ← focused: item_focused_inner bg
│  Label text                    [value]   │
│  Label text (disabled)                   │  ← disabled: item_disabled color
└──────────────────────────────────────────┘
```
Props: label, value (optional), disabled (bool), focused (bool), type (label/slider/checkbox/dropdown)

#### PdCheckbox
Props: label, checked (bool), disabled (bool)
Colors: checkbox_checked for check mark, border1 for box outline

#### PdSlider
Props: label, value (f32), min, max, step
Colors: border2 for fill, bodybg for track

#### PdDropdown
Props: label, options[], selectedIndex
Colors: Uses menugfxDrawDropdownBackground gradient logic

#### PdListGroup (section header)
Props: title
Colors: listgroup_headerbg, listgroup_headerfg

#### PdSeparator
A 1px horizontal line using border1 color.

#### PdRanking (player ranking row)
```
│  1st  PlayerName    47 kills │
```
Props: rank (int), name (string), score (int), isLocalPlayer (bool)

#### PdObjectiveRow
```
│  ✓  Destroy the dataDyne server  │  ← completed: green check
│  ✗  Escape the building          │  ← failed: red X
│  ○  Collect the evidence          │  ← incomplete: grey circle
```
Props: text, state (complete/failed/incomplete)

---

## Mock Data System

Menus that need populated data to look right use a `StoryboardMockData` struct:

```cpp
struct StoryboardMockData {
    // Player data (for rankings, stats)
    struct { char name[32]; s32 kills; s32 deaths; s32 accuracy; } players[8];
    s32 numPlayers;

    // Weapon list
    struct { char name[32]; s32 ammo; } weapons[8];
    s32 numWeapons;

    // Objectives
    struct { char text[64]; s32 state; } objectives[6];
    s32 numObjectives;

    // Generic strings
    char stageTitle[64];
    char scenarioName[64];
    char challengeName[64];
};
```

A single `g_StoryboardMock` global is initialized with plausible data at storyboard init:
- 4 players: "DarkStar" (47 kills), "Jo Dark" (38), "SpySim#2" (22), "TrentEaston" (15)
- Weapons: "Falcon 2", "CMP150", "Dragon", "Laptop Gun"
- Objectives: 3 completed, 1 failed, 2 incomplete
- Stage: "dataDyne Central - Defection"

---

## Implementation Phases

### Phase 1: Storyboard Shell (D4a-D4c)
- `pdgui_storyboard.cpp`: F11 toggle, catalog panel, preview area, controller input
- `pdgui_storyboard.h`: Public API
- Registry: Static array of `MenuPreviewEntry` with variable name, title, category, dialogtype
- OLD mode: Static placeholder image (screenshot or text description)
- NEW mode: Empty frame with "Not yet implemented" text
- Rating system with JSON persistence
- **Deliverable**: Navigate 113 menus with controller, rate them, toggle placeholder old/new

### Phase 2: OLD Mode Rendering (D4b)
- Capture original PD dialog to offscreen framebuffer
- Render into ImGui preview area as texture
- Requires stubbing dialog handlers (prevent game logic from executing)
- Menu items visible but non-interactive
- **Deliverable**: See every original PD menu rendered in the preview pane

### Phase 3: Component Library (D4c parallel)
- Implement PdDialog, PdMenuItem, PdCheckbox, PdSlider, PdDropdown, PdListGroup, PdSeparator
- All components use palette tokens — no hardcoded colors
- Shimmer animation on PdDialog borders (reuses pdguiDrawShimmerExact)
- Dialog tint blending for type-awareness on custom themes
- **Deliverable**: Component primitives ready for menu builders

### Phase 4: Menu Builders — Priority Menus (D4d)
Build ImGui versions of the most visible/complex menus first:
1. "Game Over" endscreen (current bug context — understanding this deeply)
2. "Player Ranking" (needs ranking component)
3. Pause menu (combat simulator)
4. Game Setup (complex: multiple options)
5. Extended Options (PC port — already uses literal text, easier to rebuild)
- **Deliverable**: 10-15 menus toggleable between OLD and NEW

### Phase 5: Complete Coverage (D4e-D4f)
- Work through remaining menus category by category
- Use quality ratings to prioritize (Redo first, then Incomplete)
- Training menus (large count but similar structure)
- File management menus
- Error/warning dialogs (simple, DANGER type)
- **Deliverable**: All 113 menus with NEW implementations

### Phase 6: Migration (D4g)
- Wire NEW menus into the actual game (replace `menuPushDialog` calls)
- `g_PdUseImGuiMenus` global toggle (or per-menu)
- Gradual rollout: enable NEW for menus rated "Good", keep OLD for others
- **Deliverable**: Game running with ImGui menus in production

---

## Consequences

### What Becomes Easier
- Visual QA of every menu without playing through the game
- A/B comparison of old vs. new rendering
- Theme testing across all menus simultaneously (LB/RB cycling)
- Priority tracking for the migration effort
- Onboarding: any session can see the full menu inventory and current state

### What Becomes Harder
- OLD mode framebuffer capture adds rendering complexity
- Mock data must be maintained as menus evolve
- Two rendering paths exist simultaneously until migration completes

### What We'll Need to Revisit
- Once migration is complete, OLD mode becomes archive/reference only
- The `#ifdef PD_DEV_PREVIEW` guard means storyboard doesn't ship in release
- Mock data system might evolve into a proper test fixture system
- Controller input mapping needs to not conflict with game bindings (F11 + dedicated mode helps)

---

## Action Items

1. [ ] Create `port/fast3d/pdgui_storyboard.cpp` with F11 toggle and catalog UI
2. [ ] Create `port/include/pdgui_storyboard.h` with public API
3. [ ] Add storyboard entry registry (static array of all 113 menus)
4. [ ] Implement controller navigation (SDL gamepad events)
5. [ ] Implement quality rating with JSON persistence
6. [ ] Create `port/fast3d/pdgui_menubuilder.cpp` with component library
7. [ ] Create `port/include/pdgui_menubuilder.h` with builder function registry
8. [ ] Implement PdDialog component with shimmer
9. [ ] Implement PdMenuItem, PdCheckbox, PdSlider, PdDropdown components
10. [ ] Implement dialog tint blending (`pdguiApplyDialogTint`)
11. [ ] Build first 5 priority menus in NEW mode
12. [ ] Implement OLD mode framebuffer capture
13. [ ] Update CMakeLists.txt to compile new files (add to port/*.cpp glob)
14. [ ] Add `#ifdef PD_DEV_PREVIEW` guards
15. [ ] Update context/roadmap.md with D4 sub-phase progress
