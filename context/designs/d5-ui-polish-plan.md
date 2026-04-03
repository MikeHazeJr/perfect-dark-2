# Phase D5 UI Polish Plan — Mission Select, Pause Menu, Layout, OG Textures, Updater, Menu Conversion

## Status: PLANNED
Last updated: 2026-04-03 (S132)

> **Scope**: This plan covers the UI/UX polish half of Phase D5. The Settings half (audio
> volume layers, video settings, controls rebinding) is fully implemented and documented in
> `context/d5-settings-plan.md`. This document covers everything that remains for D5 to be
> considered **shippable quality**: mission select UX, solo pause menu, layout consistency,
> OG asset integration, update banner behavior, and a systematic OG menu conversion sweep.
>
> Bug references: B-90, B-91, B-92, B-93, B-94, B-95, B-96, B-97, B-98, B-99.

---

## Current State Summary

| Component | File | State |
|-----------|------|-------|
| Mission select | `pdgui_menu_solomission.cpp` | Shows all missions (no unlock filter); minimal popup; difficulty flow wrong; objectives "(No objectives)" |
| Solo pause menu | `pdgui_menu_solomission.cpp` — `g_SoloMissionPauseMenuDialog` | Only Resume/Options work; Abort wired but Inventory/Objectives fall back to legacy renderers |
| Inventory (solo) | `g_SoloMissionInventoryMenuDialog` | Registered with NULL renderer — full legacy 3D, traps player with no working exit |
| Pause menu IDs | `pdgui_menu_pausemenu.cpp` / solomission.cpp | Duplicate ImGui IDs on Resume/Options — causes "2 visible items with conflicting ID" warning |
| Mouse capture | `pdmain.c` / `input.c` | Not called on solo mission start — cursor visible during gameplay |
| Update banner | `pdgui_menu_update.cpp` | Persists during active gameplay/missions |
| Hardcoded positions | All `pdgui_menu_*.cpp` files | Mixed: some use `pdguiScale()` correctly, some still have hardcoded offsets causing overlap bugs |
| OG textures in ImGui | — | Not integrated; star indicators and mission briefing images are ROM textures, not yet catalog-registered as UI resources |

### Key Existing Infrastructure
- `isStageDifficultyUnlocked(stageindex, difficulty)` — already exists, just not used in the list renderer
- `sm_briefing.objectivenames[6]` + `objectivedifficulties[6]` — populated by legacy handlers before ImGui renderers run
- `g_GameFile.besttimes[21][3]` — per-stage, per-difficulty best times already accessible
- `pdguiScale()` / `pdguiScaleVec()` — scaling helpers already in use; the sweep is about consistency
- `ImGui::GetContentRegionAvail()` — available but underused

---

## Phase D5.1: Mission Select UX Redesign

**Bugs fixed**: B-90, B-91, B-96, B-97

### Problem

The current `renderSelectMission` is a flat scrollable tree. Selecting a mission opens a minimal
popup via `g_SoloMissionDifficultyMenuDialog`. Objectives are not shown until after difficulty
selection via `g_AcceptMissionMenuDialog`. All missions appear regardless of unlock status.
Special Assignments and Challenges are mixed into the main list. This is B-90, B-96, B-97.

The briefing struct (`g_Briefing`) has `objectivenames[6]` but the mission select renderer
doesn't read it at all — it renders an empty panel and prints "(No objectives)". This is B-91.

### New Design

Two-panel layout inside a single full-width ImGui window:

```
┌──────────────────────────────────────────────────────────────────┐
│  MISSION SELECT                                                    │
│                                                                    │
│  ┌─────────────────────┐  ┌──────────────────────────────────┐   │
│  │ MAIN MISSIONS        │  │  dataDyne Central — Defection    │   │
│  │ ─────────────────── │  │                                  │   │
│  │ > dataDyne Central  │  │  [BRIEFING IMAGE]                │   │
│  │   dataDyne Research │  │                                  │   │
│  │   Carrington Villa  │  │  ★ ★ ☆  (highest diff cleared)  │   │
│  │   …                 │  │                                  │   │
│  │                     │  │  OBJECTIVES                      │   │
│  │ SPECIAL ASSIGNMENTS │  │  > Disable security computer     │   │
│  │ ─────────────────── │  │  > Investigate lab               │   │
│  │   Maian SOS         │  │  ░ Recover dataDyne project list │   │
│  │   War!              │  │    (SA/PA only)                  │   │
│  │   …                 │  │                                  │   │
│  └─────────────────────┘  │  ──────────────────────────────  │   │
│                            │  Agent          2:14     [Start] │   │
│                            │  Special Agent  3:01     [Start] │   │
│                            │  Perfect Agent  -----    [Start] │   │
│                            └──────────────────────────────────┘   │
└──────────────────────────────────────────────────────────────────┘
```

**Left panel — Mission List**

- Render only missions where at least one difficulty is unlocked (`isStageDifficultyUnlocked(i, DIFF_A)`)
- Group into two sections: main missions (indices 0–8 in `g_SoloStages`) and Special Assignments
  (indices 9–20, matching `L_OPTIONS_132` group). Check via `func0f104720()` for special-stage
  membership.
- Each entry renders as a selectable row. Highlight the selected row. No nested tree.
- Clicking sets `s_MissionSelectIdx`; no dialog push — detail panel updates inline.
- This entirely replaces `g_SoloMissionDifficultyMenuDialog` as a separate screen for the
  common case. PD Mode settings remain a separate dialog (keep `g_PdModeSettingsMenuDialog`).

**Right panel — Detail Panel**

Shown when a mission is selected. Components:

1. **Mission name** — `langSafe(g_SoloStages[i].name1)` + `langSafe(g_SoloStages[i].name2)`
   rendered as a large heading with `ImGui::SetWindowFontScale()`.

2. **Briefing image** — Populated from the asset catalog (see D5.4). Sampled via
   `ImGui::Image(tex_handle, ImVec2(panel_w * 0.9f, panel_w * 0.4f))`. Falls back to a
   dark placeholder rect if the catalog entry is not yet registered.

3. **Star indicators** — Three star glyphs showing highest difficulty completed.
   - Read from `g_GameFile.besttimes[stageindex][d] > 0` for d ∈ {DIFF_A, DIFF_SA, DIFF_PA}.
   - Filled star = OG ROM star texture (see D5.4). Empty star = dimmed variant.
   - Falls back to Unicode ★/☆ while catalog integration is pending.

4. **Objectives panel** — Rendered from `g_Briefing.objectivenames[6]` and
   `g_Briefing.objectivedifficulties[6]`. The briefing struct is populated by legacy
   `menudialogdef` TICK handlers *before* ImGui runs, so it's always fresh.
   - Difficulty bits per objective: `objectivedifficulties[n]` is a bitmask where
     bit 0 = Agent, bit 1 = Special Agent, bit 2 = Perfect Agent.
   - Objectives relevant to the currently highlighted difficulty row are shown at full
     opacity. Objectives outside that difficulty are dimmed (alpha ~0.4) with a note
     "(SA/PA only)" or "(PA only)".
   - `s_HoverDiff` tracks which difficulty row the cursor is over, defaulting to the
     last completed difficulty or Agent if none.

5. **Difficulty rows** — Three rows: Agent / Special Agent / Perfect Agent.
   - Each row: difficulty name | best time (formatted via `formatBestTime()`) | [Start] button.
   - If the difficulty is locked, render the row at dim opacity with a lock icon and no [Start].
   - Use `##diff_a`, `##diff_sa`, `##diff_pa` suffixes on all buttons (fixes B-94 analog pattern).
   - Clicking [Start]: call `SM_SET_DIFFICULTY(&g_MissionConfig, d)`, then
     `menuhandlerAcceptMission(MENUOP_SET, NULL, NULL)`.
   - PD Mode row: only shown when PA is unlocked. Opens `g_PdModeSettingsMenuDialog`.

**New module state** (additions to `pdgui_menu_solomission.cpp`):

```cpp
static s32 s_HoverDiff = DIFF_A;   /* which difficulty row cursor is over */
static bool s_DetailVisible = false; /* whether right panel has a selection */
```

### Files Modified

| File | Change |
|------|--------|
| `pdgui_menu_solomission.cpp` | Rewrite `renderSelectMission()` with two-panel layout; remove `renderDifficultySelect()` inline logic (keep the dialog registered but skip push if using new inline path); add `renderMissionDetail()` helper |
| `pdgui_menu_solomission.cpp` | Update `renderAcceptMission()` to be a confirmation step (shown after Start click) rather than the primary objectives display — or collapse it into the detail panel entirely |

### Estimated LOC: ~350 net (rewrites ~180, adds ~170)

---

## Phase D5.2: Solo Mission Flow Fixes

**Bugs fixed**: B-92, B-93, B-94, B-98

### B-92 — Mouse Capture on Mission Start

**Root cause**: When transitioning from mission select → gameplay, `SDL_SetRelativeMouseMode(SDL_TRUE)` is not called. The cursor stays visible and mouse movement only works until the pointer hits the window edge.

**Fix location**: `pdmain.c` in the state machine branch that handles the transition from `MAINSTATE_FRONTEND` → `MAINSTATE_GAME`. Specifically, wherever `g_MainState` is set to the in-game state after the fade-in. This is the same pattern as B-66 (which fixed MP match start) — look at how that was resolved and apply the same `inputLockMouse(1)` call here.

**Verification**: After fix, entering any solo mission hides cursor immediately with no border-hunt required.

### B-93 + B-98 — Full Solo Pause Menu

**Current state**: `g_SoloMissionPauseMenuDialog` renders via `renderSoloPause()`. The pause
select items are: `{0=close, 1=Inventory, 2=Options, 3=Abort}`. Options works (pushes
`g_SoloMissionOptionsMenuDialog`). Abort works (pushes `g_MissionAbortMenuDialog`). But
`s_PauseSelectIdx == 1` (Inventory) pushes `g_SoloMissionInventoryMenuDialog`, which is
registered with `NULL` renderFn — it falls back to the legacy renderer which has no working
exit button in PC mode. This is B-98.

**Required pause menu items** and their implementations:

| Item | Target | ImGui implementation |
|------|--------|----------------------|
| Resume | Close pause menu | `SDL_SetRelativeMouseMode(SDL_TRUE)` + `menuPopDialog()` |
| Objectives | Inline panel or sub-screen | Show `g_Briefing.objectivenames[6]` with completion checkmarks (read from `g_GameFile.flags[]` bitmask) |
| Inventory | Sub-screen (new ImGui renderer) | List current weapon loadout; see below |
| Options | Push `g_SoloMissionOptionsMenuDialog` | Already working |
| Restart Mission | Danger confirmation | `menuhandlerAbortMission` + re-push difficulty dialog |
| Abort Mission | Danger confirmation | Push `g_MissionAbortMenuDialog` (already wired) |

**Inventory renderer** (converts `g_SoloMissionInventoryMenuDialog` from legacy to ImGui):

- Register a real `renderFn` for `g_SoloMissionInventoryMenuDialog`.
- Display: list of weapon slots with weapon name (from `langSafe()`) and ammo count.
- Weapon data: accessible via a new bridge function `pdguiSoloGetInventoryWeapon(slot, &name, &ammo)` in `pdgui_bridge.c` — this reads from player weapon slots without requiring types.h in the C++ file.
- Exit button calls `menuPopDialog()` — the current legacy path has no equivalent.
- This is a minimal "read-only" inventory display, not the full 3D model viewer. The 3D viewer is still registered separately for when it's intentionally invoked.

**Objectives completion** — read from `g_GameFile.flags[]`:
- Each mission has flags bytes; objective completion state is packed into these. The exact bit layout needs verification against `savefile.c` during implementation. Bridge function: `pdguiSoloGetObjectiveStatus(stageindex, obj_idx)` → bool.

**Restart Mission**:
- Not currently wired at all. Implementation:
  1. Show a "Are you sure? Restart will lose current progress." confirmation (PdDialog, Red palette).
  2. On confirm: `menuhandlerAbortMission(MENUOP_SET, NULL, NULL)` (aborts current run), then immediately push the difficulty select for the same stage so the player re-enters the mission.

### B-94 — ImGui Duplicate ID

**Root cause**: Multiple `ImGui::Button("Resume")` and `ImGui::Button("Options")` calls exist across the pause menu stack — one in `renderSoloPause()` and possibly one in `pdgui_menu_pausemenu.cpp`. ImGui tracks buttons by label string and window context; same label in same window frame = duplicate ID warning.

**Fix**: Apply `##id` suffixes consistently:
```cpp
// Before
ImGui::Button("Resume")
ImGui::Button("Options")
// After
ImGui::Button("Resume##solo_pause")
ImGui::Button("Options##solo_pause")
ImGui::Button("Inventory##solo_pause")
ImGui::Button("Abort##solo_pause")
ImGui::Button("Restart##solo_pause")
```

Also add `ImGui::PushID("solo_pause_menu")` / `ImGui::PopID()` at the scope of the entire
`renderSoloPause()` function as belt-and-suspenders protection for any sub-widgets.

**Audit scope**: Run a grep for duplicate label strings across `pdgui_menu_pausemenu.cpp`,
`pdgui_menu_solomission.cpp`, and `pdgui_menu_mpingame.cpp` after implementing D5.2.

### Files Modified

| File | Change |
|------|--------|
| `pdmain.c` | Add `inputLockMouse(1)` / `SDL_SetRelativeMouseMode` call on solo mission start transition (B-92) |
| `pdgui_menu_solomission.cpp` | Expand pause menu item list; add `renderSoloPause()` full implementation; add Restart confirmation; add `##id` suffixes (B-93, B-94, B-98) |
| `pdgui_menu_solomission.cpp` | Register a real renderFn for `g_SoloMissionInventoryMenuDialog` |
| `pdgui_bridge.c` | Add `pdguiSoloGetInventoryWeapon()` and `pdguiSoloGetObjectiveStatus()` bridge functions |
| `port/include/pdgui_menus.h` | Declare any new bridge functions if shared across TUs |

### Estimated LOC: ~220 new + ~30 modified

---

## Phase D5.3: Relative Layout System

**Bugs fixed**: B-60, settings overlap, version/update overlap

### Problem

Across all `pdgui_menu_*.cpp` files, there are three categories of layout issues:

1. **Hardcoded offsets** — `ImGui::SetCursorPosX(120)`, `ImGui::SetNextWindowPos(ImVec2(50, 80))`.
   These break at non-standard UI scales.
2. **Column widths that don't stretch** — table columns with fixed pixel widths clip text or leave
   dead space at wider windows.
3. **Window positioning that doesn't adapt** — update banner rendered at a fixed corner offset
   that overlaps version text at certain scales.

### Approach

This is a sweep pass, not an architectural rewrite. The rule is: **every layout measurement
must derive from `ImGui::GetContentRegionAvail()`, `viGetWidth()`/`viGetHeight()` via
`pdguiScale()`, or a fraction thereof.** No raw pixel literals for position or size.

**Patterns to enforce:**

```cpp
// Width of a panel — use fraction of available
float panel_w = ImGui::GetContentRegionAvail().x * 0.45f;

// Centering a button
float btn_w = pdguiScale(120.f);
ImGui::SetCursorPosX((ImGui::GetContentRegionAvail().x - btn_w) * 0.5f);
ImGui::Button("Accept##accept_btn", ImVec2(btn_w, 0));

// Stretch column in a table
ImGui::TableSetupColumn("Title", ImGuiTableColumnFlags_WidthStretch);
ImGui::TableSetupColumn("Size",  ImGuiTableColumnFlags_WidthFixed, pdguiScale(80.f));

// Positioned window relative to screen edge
ImGui::SetNextWindowPos(ImVec2(pdguiScale(10.f), viGetHeight() - pdguiScale(40.f)),
                        ImGuiCond_Always, ImVec2(0, 1)); /* anchor bottom-left */
```

**Known sites to fix** (from playtest bug list):

| Bug | Location | Fix |
|-----|----------|-----|
| B-60 | `pdgui_menu_mainmenu.cpp` — Settings Audio/Video tab rendering | Stray 'g'+'s': likely a `SetCursorPosX` that undershoots its tab content area; convert to `BeginTabItem` content region |
| Settings text/tab overlap | `pdgui_menu_mainmenu.cpp` — tab bar vs content boundary | Use `ImGui::BeginTabBar` / `EndTabBar` with no manual cursor positioning inside |
| Update banner overlap | `pdgui_menu_update.cpp` | Position banner using `viGetHeight()` anchor, not a hardcoded Y coordinate |
| Column clipping in Update tab | `pdgui_menu_update.cpp` | Title column already patched in S131; verify stretch behavior at non-1080p |

**Sweep order** (highest-impact first):

1. `pdgui_menu_mainmenu.cpp` (Settings — most-visited screen)
2. `pdgui_menu_update.cpp` (banner + update tab)
3. `pdgui_menu_solomission.cpp` (new two-panel layout from D5.1 should be authored correctly from the start)
4. `pdgui_menu_matchsetup.cpp` (busy layout — many columns)
5. All remaining `pdgui_menu_*.cpp` files

### Files Modified

All `port/fast3d/pdgui_menu_*.cpp` files touched by the sweep. Exact list determined at
implementation time via grep for raw pixel literals.

### Estimated LOC: ~80 changed lines across ~8 files (small edits, high leverage)

---

## Phase D5.4: OG Menu Texture / Effect Integration

**Dependencies**: D5.1 (mission select detail panel needs briefing images + star textures)

### Goal

Extract original Perfect Dark menu visual assets from the ROM and register them in the asset
catalog as UI resources. ImGui menus can then sample them via `ImGui::Image()`. Modders can
override any entry to retheme the UI.

### Assets Required

| Asset | ROM source | Catalog ID (proposed) | Used by |
|-------|------------|-----------------------|---------|
| Mission briefing images (21) | ROM texture bank, one per stage | `ui/briefing/datadyne_defection`, etc. | D5.1 detail panel |
| Filled star indicator | ROM UI texture sheet | `ui/stars/star_filled` | D5.1 star row |
| Empty star indicator | ROM UI texture sheet | `ui/stars/star_empty` | D5.1 star row |
| Menu scan-line overlay | ROM effect texture | `ui/fx/scanline` | Global — applied as background overlay in `pdgui_backend.cpp` |
| Translucent panel texture | ROM UI background | `ui/panels/blue_panel` | Optional panel borders |
| Color gradient (title bar) | ROM UI gradient | `ui/fx/title_gradient` | Menu headers |

### Catalog Registration

UI textures are a new catalog resource type. Registration approach:

1. **New catalog resource kind**: `CATALOG_KIND_UI_TEXTURE` (or reuse `CATALOG_KIND_TEXTURE`
   with a `ui/` prefix convention). This is purely a naming convention — the catalog engine
   doesn't need a new kind if the resolve path handles `ui/` prefixes.

2. **ROM extraction**: Add entries in `assetcatalog_base.c` that point to the ROM texture
   segments containing these assets. Use existing `catalogRegisterTexture()` pattern.
   The exact ROM offset and format (RGBA16, CI8, etc.) must be determined during implementation
   by cross-referencing the original PD texture decompilation data.

3. **ImGui texture handle**: After catalog resolution, the texture must be uploaded to OpenGL
   and its handle stored. Add a `pdguiGetUiTexture(const char *catalog_id)` function in
   `pdgui_bridge.c` that:
   - Calls `assetCatalogResolve(catalog_id)`
   - If the entry has a loaded texture handle, returns it as `ImTextureID`
   - If not yet loaded, triggers a load and returns NULL (graceful fallback)

4. **Fallback path**: All ImGui sites that use catalog textures must check for NULL and render
   a placeholder rect instead. This ensures the menus work even if catalog loading is partial.

### Scan-Line Effect

The scan-line overlay is applied once per frame in `pdgui_backend.cpp` after all menu content
is rendered but before `ImGui::Render()`. It's a full-screen quad at low alpha (~0.08) using
`ImGui::GetBackgroundDrawList()->AddImage()`. This gives menus their original CRT feel without
modifying individual menu renderers.

### Files Modified

| File | Change |
|------|--------|
| `port/src/assetcatalog_base.c` | Register UI texture entries |
| `port/fast3d/pdgui_bridge.c` | Add `pdguiGetUiTexture()` |
| `port/include/pdgui_menus.h` | Declare `pdguiGetUiTexture()` |
| `port/fast3d/pdgui_backend.cpp` | Add scan-line overlay pass |
| `port/fast3d/pdgui_menu_solomission.cpp` | Use briefing images + star textures |

### Estimated LOC: ~180 new (catalog registrations, bridge function, overlay pass, usage sites)

### Note on Implementation Order

D5.4 can partially proceed before D5.1 is complete. The catalog registration and bridge
function can be written first, with the ImGui usage sites added as D5.1 is authored. The
scan-line effect is fully independent.

---

## Phase D5.5: Update Banner Behavior

**Bugs fixed**: B-95, B-99

### B-95 — Auto-Dismiss During Gameplay

**Problem**: The update notification banner (`pdgui_menu_update.cpp`) renders every frame
regardless of game state. During solo missions and MP matches, it overlays the gameplay HUD.

**Fix**: Add a game-state gate in the banner render function:

```cpp
// In pdgui_menu_update.cpp — renderUpdateBanner() or equivalent
extern "C" s32 g_MainInGame;          /* or equivalent "is in mission" check */
extern "C" s32 pdguiIsGameplayActive(void); /* bridge function */

void renderUpdateBanner(void) {
    if (pdguiIsGameplayActive()) return; /* B-95: hide during missions/matches */
    // ... existing banner render ...
}
```

`pdguiIsGameplayActive()` is a new bridge function in `pdgui_bridge.c` that returns true
when the game is in a mission (solo) or active match (MP), false when in menus. Check
`g_MainState` or equivalent for the "in gameplay" condition.

The banner should re-appear when the player returns to the main menu or lobby.

### B-99 — Updater Extraction

**Problem**: The updater downloads the release zip correctly but the extraction step may fail
with the fixed v0.0.25 binaries. Needs an end-to-end retest.

**Verification checklist**:
1. Launch v0.0.24 (or a build that sees v0.0.25 as an update).
2. Navigate to Settings → Update tab → trigger download.
3. Monitor logs for extraction success/failure.
4. Verify the new binary replaces the old one on restart.
5. Check that `updater.c` properly handles the case where the zip contains a nested directory
   (GitHub release zips wrap files in a `perfect-dark-X.Y.Z/` subdirectory — confirm the
   extraction logic strips this prefix).

If the extraction fails, the likely fix is in `updater.c` where it constructs the output
path from zip entry names — ensure it strips any leading directory component.

### Files Modified

| File | Change |
|------|--------|
| `pdgui_menu_update.cpp` | Add `pdguiIsGameplayActive()` gate (B-95) |
| `pdgui_bridge.c` | Add `pdguiIsGameplayActive()` bridge function |
| `port/src/updater.c` | Fix zip path stripping if needed (B-99, investigation-first) |

### Estimated LOC: ~25 (B-95 is trivial; B-99 may be investigation-only)

---

## Phase D5.6: Systematic OG Menu Conversion

**Dependencies**: D5.3 (relative layout must be done first to author new menus correctly)

### Goal

Audit all remaining legacy (OG) menu screens that are still active. For each, decide:
- **Convert to ImGui**: screens that are functional but use the legacy renderer
- **Keep legacy + wrap**: screens with irreplaceable 3D content (weapon models, controller diagrams)
- **Remove**: dead screens with no current path to them

### Audit Procedure

For each legacy `menudialogdef` with a non-NULL `tickFn` but no ImGui `renderFn` registered:
1. Find the dialog definition in `src/game/*.c`
2. Determine what it displays and whether it has a working exit path in PC mode
3. Classify: Convert / Keep / Remove

### Priority: Trapping Screens First

Any screen that a player can enter but cannot exit (no working Back/Esc) is P0. Currently known:

| Screen | Bug | Action |
|--------|-----|--------|
| `g_SoloMissionInventoryMenuDialog` | B-98 | **Convert** (handled in D5.2) |
| `g_SoloMissionControlStyleMenuDialog` | Unknown | **Audit**: controller diagram — likely keep as legacy (3D content) but verify Back works |
| `g_FrWeaponsAvailableMenuDialog` | Unknown | **Audit**: training weapons list — verify Back works; convert if broken |

### Conversion Checklist (per screen)

For each screen being converted to ImGui:

- [ ] Register an ImGui `renderFn` via `pdguiHotswapRegister()`
- [ ] All data reads go through bridge functions or the catalog API (never direct `types.h` structs from C++)
- [ ] All buttons have `##id` suffixes scoped to this dialog
- [ ] All sizes use `pdguiScale()` / `GetContentRegionAvail()` (no raw pixels)
- [ ] Exit path: every screen must have a working "Back" / close path that calls `menuPopDialog()`
- [ ] If the screen previously used `menuPushDialog` to open sub-screens, those sub-screens must also have exit paths
- [ ] Palette: use `pdguiApplyPdStyle()` Blue for normal screens, Red for danger confirmations

### Secondary Conversions (after trapping screens)

Once P0 screens are resolved, sweep remaining dialogs in `pdgui_menu_solomission.cpp` and
other files. Low-priority candidates (no player-facing breakage, just visual inconsistency):

- Pre/post mission briefing screen — verify scroll works and text is readable
- Challenge completion screen — verify completion status displays correctly
- Training mode screens — low traffic, convert opportunistically

### Files Modified

Determined per-screen during the audit. Expected primary file: `pdgui_menu_solomission.cpp`.
Bridge additions in `pdgui_bridge.c`.

### Estimated LOC: ~150–300 (depends on how many screens need conversion; inventory is the biggest)

---

## Implementation Order and Dependencies

```
D5.1 (Mission Select Redesign)
  └── D5.4 (OG Textures) — catalog + bridge can be done in parallel;
        briefing images + stars plugged into D5.1 when both ready

D5.2 (Solo Flow Fixes)
  └── Independent of D5.1 (different dialogs, except both are in solomission.cpp)
  └── B-92 (mouse) is a one-liner; do it first

D5.3 (Relative Layout)
  └── Do as a sweep pass after D5.1/D5.2 are authored correctly from the start
  └── D5.6 depends on D5.3 being complete (new dialogs must follow the pattern)

D5.5 (Update Banner)
  └── Fully independent; can be done any time

D5.6 (OG Menu Conversion)
  └── Depends on D5.3 (layout patterns)
  └── Inventory conversion already done in D5.2
```

**Recommended sequence**:
1. B-92 mouse fix (15 min, high impact)
2. B-94 duplicate ID fix (30 min, eliminates console spam)
3. D5.2 — full pause menu (largest effort in this phase)
4. D5.1 — mission select redesign (second largest)
5. D5.5 — update banner gate (quick)
6. D5.3 — layout sweep (touches many files but each edit is small)
7. D5.4 — OG texture integration (can interleave with D5.1)
8. D5.6 — OG menu audit + conversion (ongoing, low urgency except trapping screens)

---

## Total Estimated Effort

| Sub-phase | Effort |
|-----------|--------|
| D5.1 Mission Select | ~350 LOC |
| D5.2 Solo Flow Fixes | ~250 LOC |
| D5.3 Layout Sweep | ~80 LOC |
| D5.4 OG Textures | ~180 LOC |
| D5.5 Update Banner | ~25 LOC |
| D5.6 OG Menu Conversion | ~150–300 LOC |
| **Total** | **~1,000–1,200 LOC** |

---

## Key Files Reference

| File | Sub-phases |
|------|------------|
| `port/fast3d/pdgui_menu_solomission.cpp` | D5.1, D5.2, D5.6 |
| `port/fast3d/pdgui_menu_pausemenu.cpp` | D5.2 (duplicate ID audit) |
| `port/fast3d/pdgui_menu_mainmenu.cpp` | D5.3 (settings layout) |
| `port/fast3d/pdgui_menu_update.cpp` | D5.3 (banner position), D5.5 |
| `port/fast3d/pdgui_backend.cpp` | D5.4 (scan-line overlay) |
| `port/fast3d/pdgui_bridge.c` | D5.2 (inventory/objectives bridge), D5.4 (UI texture bridge), D5.5 (gameplay gate) |
| `port/src/assetcatalog_base.c` | D5.4 (UI texture catalog registrations) |
| `port/src/pdmain.c` | D5.2 (B-92 mouse capture) |
| `port/src/updater.c` | D5.5 (B-99 investigation) |
