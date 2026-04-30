# Phase D5 — Full Menu System Replacement (D5 + D9 Merged)

## Status: PLANNED
Last updated: 2026-04-03 (S135)

> **Scope**: This is the complete, infrastructure-first menu system replacement. D5 (Settings/QoL)
> and D9 (Menu Migration) are merged into a single deliberate sweep. The Settings half
> (audio volume layers, video settings, controls rebinding) is already done — documented in
> `context/d5-settings-plan.md`. This document covers everything that remains to achieve
> **zero legacy menu screens in any player-facing flow**.
>
> Core principle: build the visual layer and input ownership boundary FIRST, then build every
> screen on top of that foundation. Not incremental patches — a planned infrastructure sweep.
>
> Execution order: D5.0a → D5.0 → D5.1 → D5.2 → D5.3 → D5.4 → D5.5 → D5.6 → D5.7

---

## Current State Summary

| Component | File | State |
|-----------|------|-------|
| Menu visual theme | — | Not built; all ImGui menus use plain styled colors, no OG textures |
| Fast3D texture bridge | `pdgui_bridge.c` | Not validated; D5.0a spike required before D5.0 |
| Input ownership | `pdmain.c` / `input.c` | No clean MENU/GAMEPLAY boundary; Esc double-push, Tab conflicts, mouse capture timing issues |
| Mission select | `pdgui_menu_solomission.cpp` | All missions shown regardless of unlock; minimal popup; objectives "(No objectives)"; difficulty flow wrong |
| Solo pause menu | `pdgui_menu_solomission.cpp` | Only Resume/Options work; Abort wired; Inventory/Objectives fall back to legacy renderers |
| Inventory (solo) | `g_SoloMissionInventoryMenuDialog` | Registered with NULL renderer — full legacy 3D, traps player with no working exit |
| End game screens | — | Not built in ImGui; post-mission and post-match use legacy screens |
| Combat Sim setup | `pdgui_menu_matchsetup.cpp` | Bot heads/bodies independently resolved (mismatch bug — fix folded into D5.2); generic bot names |
| Online lobby | `pdgui_menu_room.cpp` / lobby | Co-Op / Counter-Op / Solo tabs visible but unsupported |
| OG menu removal | — | No systematic removal pass; many legacy screens still active |

---

## Phase D5.0a: Technical Spike (FIRST — Single Session)

**Before ANY other D5 work: validate the pipeline.**

### Goal

Prove that an OG PD ROM texture can be rendered inside an ImGui window via the Fast3D texture
bridge. This is a single-session proof of concept. If it works, the full D5.0 visual layer plan
is viable. If it does not, build the missing bridge code first, then re-run the spike.

### Spike Task

1. Pick one simple UI texture from the ROM (e.g. the menu panel background or a star sprite).
2. Extract it through the asset catalog (`assetCatalogResolve("ui/panels/blue_panel")`).
3. Upload it to OpenGL via the existing texture upload path in `gfx_pc.cpp`.
4. Render it inside an ImGui window using `ImGui::Image()`.

### Pass/Fail Criteria

- **Pass**: Texture appears correctly in an ImGui window. Proceed to D5.0.
- **Fail**: Pipeline gap identified. Build the missing Fast3D texture bridge, then re-run the
  spike before starting D5.0.

### Files

| File | Change |
|------|--------|
| `port/fast3d/pdgui_bridge.c` | Add `pdguiGetUiTexture()` spike implementation |
| `port/src/assetcatalog_base.c` | Register one test `ui/` entry |

### Estimated LOC: ~50 (spike only — full implementation in D5.0)

---

## Phase D5.0: Menu Visual Layer (FOUNDATION)

**Do this second (after spike passes). Everything else builds on top of it.**

### Goal

The authentic PD menu aesthetic: translucent blue panels, scan-line effects, color gradients,
corner decorations. These are ROM textures we can extract and render via the catalog. All menus
must use this theme. Modders can retheme the entire UI by swapping catalog UI texture entries.

### New Module: `pdgui_theme.h` / `pdgui_theme.cpp`

Defines the menu visual layer. All drawing uses ImGui's draw list API (`AddImage`,
`AddRectFilled`, `AddRect`, etc.) to overlay OG textures.

**Public API:**

```cpp
// Draw a translucent PD-style panel at screen coordinates
void pdguiThemeDrawPanel(float x, float y, float w, float h);

// Draw PD corner/edge border decorations over a panel region
void pdguiThemeDrawBorder(float x, float y, float w, float h);

// Draw a header bar with PD style gradient + text
void pdguiThemeDrawHeader(const char *text);

// Draw a PD-style menu button (selected = highlighted state)
bool pdguiThemeDrawButton(const char *label, bool selected, ImVec2 size);

// Draw star indicators (0–3 filled) using OG ROM star textures
void pdguiThemeDrawStars(int filled_count, int total_count);

// Full-screen scan-line pass — call once per frame after all menu content
void pdguiThemeApplyScanline(void);
```

### Catalog Registration

UI textures are registered in `assetcatalog_base.c` as `ASSET_UI` type entries. Proposed IDs:

| Asset | ROM source | Catalog ID |
|-------|-----------|------------|
| Translucent blue panel | ROM UI bg | `ui/panels/blue_panel` |
| Panel border/corner set | ROM UI border | `ui/panels/border` |
| Header gradient | ROM UI gradient | `ui/fx/title_gradient` |
| Scan-line overlay | ROM effect texture | `ui/fx/scanline` |
| Filled star indicator | ROM UI sprite sheet | `ui/stars/star_filled` |
| Empty star indicator | ROM UI sprite sheet | `ui/stars/star_empty` |
| Mission briefing images (21) | ROM texture bank | `ui/briefing/<stage_id>` |
| Difficulty icons (Agent/SA/PA) | ROM UI sprites | `ui/icons/diff_agent`, etc. |

`pdguiGetUiTexture(const char *catalog_id)` in `pdgui_bridge.c` — resolves a catalog entry to
an `ImTextureID`.

### Fallback Policy

**Base game UI textures** (all `ui/` catalog entries backed by the ROM) are **always available**.
There is no fallback to solid colors for base content. If `pdguiGetUiTexture()` returns NULL for
a base entry, it is a pipeline bug:

- Log `LOG_ERROR` with the catalog ID.
- `assert(tex != NULL)` in debug builds.
- File a bug and fix the pipeline. Do NOT substitute solid colors — that hides the root cause.

**Mod UI textures** (entries overridden by a mod catalog) fall back to the base game equivalent
via the normal catalog override/fallback mechanism. A mod that fails to provide a texture gets
the ROM-backed base game texture automatically.

This policy ensures the base game always looks correct, mods can safely override any texture,
and pipeline bugs surface immediately rather than being masked.

### Scan-Line Pass

Called once per frame in `pdgui_backend.cpp` after all menu content is rendered, before
`ImGui::Render()`. Uses `ImGui::GetBackgroundDrawList()->AddImage()` — a full-screen quad at
~8% alpha using `ui/fx/scanline`. Gives all menus the original CRT texture without touching
individual renderers.

### Files

| File | Change |
|------|--------|
| `port/fast3d/pdgui_theme.cpp` | New — full theme implementation |
| `port/include/pdgui_style.h` | Add `pdgui_theme.h` declarations (or new header) |
| `port/fast3d/pdgui_backend.cpp` | Call `pdguiThemeApplyScanline()` per frame |
| `port/src/assetcatalog_base.c` | Register all `ui/` catalog entries |
| `port/fast3d/pdgui_bridge.c` | Full `pdguiGetUiTexture()` implementation |
| `port/include/pdgui_menus.h` | Declare `pdguiGetUiTexture()` |

### Estimated LOC: ~400 (theme module ~200, catalog registrations ~80, bridge ~40, backend ~30, declarations ~50)

---

## Phase D5.1: Input Ownership Boundary

**Do this third. Clean input ownership eliminates an entire class of bugs.**

### Two Modes, Clean Handoff

**MENU mode** — ImGui owns all input:
- Legacy PD menu input completely suppressed
- SDL events routed to ImGui only
- Tab navigation disabled (arrow keys / Enter / Space / Esc / controller only)
- Esc = toggle pause menu: single push, edge-detected, one input path only

**GAMEPLAY mode** — Game input system owns everything:
- ImGui receives no input events
- Mouse captured (SDL relative mode)
- HUD overlay renders but does not consume input

**Transition rules:**
- Last ImGui menu closes → switch to GAMEPLAY (call `inputLockMouse(1)`)
- Esc pressed in gameplay → switch to MENU (open pause menu)
- Only one place in the codebase handles each transition direction

### What This Eliminates

| Bug class | Root cause | After D5.1 |
|-----------|-----------|------------|
| Double Esc push (B-21) | Two code paths handle Esc | One path, edge-detected |
| Tab conflicts | Tab reaches both ImGui and game | Tab suppressed in MENU mode |
| Mouse capture timing (B-92) | No canonical "gameplay start" event | inputLockMouse(1) on GAMEPLAY transition |
| Legacy input interference | Legacy menu tick still polling during ImGui menus | Suppressed at mode boundary |

### Implementation

New enum in `pdmain.c`:

```c
typedef enum {
    INPUTMODE_MENU,     /* ImGui owns SDL events */
    INPUTMODE_GAMEPLAY  /* game input owns, ImGui HUD only */
} InputOwnerMode;

extern InputOwnerMode g_InputMode;
```

`pdmainSetInputMode(InputOwnerMode mode)` — canonical transition function. On switch to
GAMEPLAY: calls `inputLockMouse(1)` + `SDL_SetRelativeMouseMode(SDL_TRUE)`. On switch to MENU:
calls `inputLockMouse(0)` + `SDL_SetRelativeMouseMode(SDL_FALSE)`.

SDL event pump in `pdmain.c` checks `g_InputMode` before dispatching to legacy input vs. ImGui.

### Files

| File | Change |
|------|--------|
| `port/src/pdmain.c` | Add `g_InputMode`, `pdmainSetInputMode()`, gate SDL dispatch |
| `port/src/input.c` | Respect `g_InputMode` — suppress polling in MENU mode |
| `port/fast3d/pdgui_backend.cpp` | Only pass SDL events to ImGui when `g_InputMode == INPUTMODE_MENU` |

### Estimated LOC: ~80 new + ~40 modified

---

## Phase D5.2: Pause Menu + Sub-screens + Bot Sync Fix

**Fourth — unblocks gameplay immediately.**

_(Was D5.3 in the original plan. Sequenced before mission select because pause menu unblocks
in-game play. Bot head/body sync bug fix folded in here — it is a bug fix, not polish.)_

### Full ImGui Pause Menu

Replaces ALL legacy pause screens. Uses `pdguiThemeDrawPanel` / `pdguiThemeDrawBorder` from D5.0.

| Item | Target | Implementation |
|------|--------|----------------|
| Resume | Close pause menu | `pdmainSetInputMode(INPUTMODE_GAMEPLAY)` + `menuPopDialog()` |
| Objectives | Inline panel | `g_Briefing.objectivenames[6]` + completion checkmarks from `g_GameFile.flags[]` |
| Inventory | Sub-screen | List weapon slots + ammo; bridge via `pdguiSoloGetInventoryWeapon()` |
| Options | Push existing | `g_SoloMissionOptionsMenuDialog` (already working) |
| Restart Mission | Danger confirmation | Confirm → `menuhandlerAbortMission` + re-push difficulty |
| Abort Mission | Danger confirmation | Push `g_MissionAbortMenuDialog` (already wired) |

**Inventory renderer** — registers a real `renderFn` for `g_SoloMissionInventoryMenuDialog`
(currently NULL, causing B-98). Bridge function `pdguiSoloGetInventoryWeapon(slot, &name, &ammo)`
in `pdgui_bridge.c` reads weapon slots without requiring `types.h` in C++. Read-only display.
Exit button calls `menuPopDialog()`.

**Objectives completion** — `pdguiSoloGetObjectiveStatus(stageindex, obj_idx)` bridge function
reads completion bits from `g_GameFile.flags[]`.

**Duplicate ID fix (B-94)** — `PushID("solo_pause_menu")` scope + `##solo_pause` suffixes on
all buttons. Audit propagation across `pdgui_menu_pausemenu.cpp`, `pdgui_menu_solomission.cpp`,
`pdgui_menu_mpingame.cpp`.

### Bot Head/Body Sync Fix (folded from old D5.5)

Current state: body and head are resolved independently from catalog, allowing mismatched pairs
(e.g. Joanna head on a guard body). Fix: build a dependency graph so head selection filters to
compatible heads for the chosen body, and vice versa.

Bridge function `pdguiGetCompatibleHeads(const char *body_id, char **out_ids, int *out_count)`.
Dependency graph encoded in catalog metadata or a companion JSON.

### Files

| File | Change |
|------|--------|
| `pdgui_menu_solomission.cpp` | Full `renderSoloPause()` — all items, `##id` suffixes, Restart confirmation |
| `pdgui_menu_solomission.cpp` | Register real renderFn for `g_SoloMissionInventoryMenuDialog` |
| `pdgui_bridge.c` | `pdguiSoloGetInventoryWeapon()`, `pdguiSoloGetObjectiveStatus()`, `pdguiGetCompatibleHeads()` |
| `port/include/pdgui_menus.h` | Declare new bridge functions |
| `pdgui_menu_matchsetup.cpp` | Bot head/body filtering UI |

### Estimated LOC: ~300 new + ~30 modified

---

## Phase D5.3: Mission Select Redesign

**Fifth — uses the visual layer from D5.0.**

_(Was D5.2 in the original plan.)_

### Two-Panel Layout

```
┌─────────────────────────────────────────────────────────────────┐
│  MISSION SELECT                                                   │
│                                                                   │
│  ┌────────────────────┐  ┌─────────────────────────────────────┐ │
│  │ MAIN MISSIONS       │  │  dataDyne Central — Defection       │ │
│  │ ─────────────────  │  │                                     │ │
│  │ > dataDyne Central  │  │  [BRIEFING IMAGE via catalog]       │ │
│  │   dataDyne Research │  │                                     │ │
│  │   Carrington Villa  │  │  ★ ★ ☆  (highest diff cleared)     │ │
│  │   …                 │  │                                     │ │
│  │                     │  │  OBJECTIVES                         │ │
│  │ SPECIAL ASSIGNMENTS │  │  > Disable security computer        │ │
│  │ ─────────────────  │  │  > Investigate lab                  │ │
│  │   Maian SOS         │  │  ░ Recover project list (SA/PA)     │ │
│  │   War!              │  │                                     │ │
│  └────────────────────┘  │  ─────────────────────────────────  │ │
│                           │  Agent          2:14      [Start]   │ │
│                           │  Special Agent  3:01      [Start]   │ │
│                           │  Perfect Agent  -----     [Start]   │ │
│                           └─────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────────┘
```

**Left panel** — render only unlocked missions (`isStageDifficultyUnlocked(i, DIFF_A)`). Group:
main missions (indices 0–8) and Special Assignments (indices 9–20). Each entry is a selectable
row. Selection updates detail panel inline — no dialog push for the common case.

**Right panel** — mission detail:
1. Mission name — large heading
2. Briefing image — `ImGui::Image()` from `ui/briefing/<stage_id>` catalog entry; assert/log if not loaded (base content must always resolve)
3. Star indicators — `pdguiThemeDrawStars()` from D5.0; reads `g_GameFile.besttimes[stageindex][d]`
4. Objectives — from `g_Briefing.objectivenames[6]` + `objectivedifficulties[6]` bitmask; relevance-dimmed by hover difficulty
5. Difficulty rows — Agent/SA/PA with best time + `[Start]` button; locked = dimmed + no Start; `##diff_a`, `##diff_sa`, `##diff_pa` suffixes

All panels use `pdguiThemeDrawPanel()` and `pdguiThemeDrawBorder()`.

### Files

| File | Change |
|------|--------|
| `pdgui_menu_solomission.cpp` | Rewrite `renderSelectMission()` — two-panel layout, unlock filter, objectives, star indicators, inline difficulty rows |
| `pdgui_menu_solomission.cpp` | `renderAcceptMission()` becomes confirmation step, not primary objectives display |

### Estimated LOC: ~350 net

---

## Phase D5.4: End Game Flow + Online Lobby Navigation

**Uses the visual layer from D5.0. Online lobby tab gating folded in here (one-liner).**

### Mission Complete Screen

- Objectives met/failed checklist
- Mission time + accuracy stats
- Buttons: "Next Mission" / "Retry" / "Main Menu"
- Style: `pdguiThemeDrawPanel` + `pdguiThemeDrawBorder`
- Registered as `g_MissionCompleteMenuDialog` ImGui renderer

### MP Match End Screen

- Final scoreboard — all players/bots, kills/deaths/score
- Buttons: "Play Again" / "Change Setup" / "Leave"
- Reuses match setup data already accessible via bridge

### Online Lobby Tab Gating (folded from old D5.7)

Disable Co-Op, Counter-Op, Solo tabs in room screen — Combat Sim only for now. Tabs render
but are grayed out with "(Coming Soon)" tooltip. One `BeginTabItem(..., ImGuiTabItemFlags_Disabled)`
change per unsupported tab. Room navigation cleanup — Back/Esc behavior consistent; no stuck states.

### Files

| File | Change |
|------|--------|
| `pdgui_menu_endscreen.cpp` | Rewrite/complete mission complete renderer |
| `pdgui_menu_mpingame.cpp` | Add post-match scoreboard renderer |
| `pdgui_bridge.c` | Any new bridge functions for end-game stats |
| `pdgui_menu_room.cpp` | Disable unsupported tabs (one-liner) |
| `pdgui_menu_lobby.cpp` | Navigation cleanup |

### Estimated LOC: ~200 new + ~10 changed

---

## Phase D5.5: Combat Sim Setup Polish

_(Bot head/body sync fix moved to D5.2. This phase covers the remaining polish.)_

### Bot Name Dictionary

Replace "MeatSim-1" style generated names with a word-list dictionary. Two arrays (adjectives,
nouns), seeded deterministically from bot slot index. ~100 adjectives × ~100 nouns = 10,000
combinations. Stored in `port/src/botvariant.c` or a new `port/src/botnames.c`.

### Arena and Weapon Set Verification

Arena list from catalog already working (S128). Verify all arenas render correctly in the
match setup preview. Weapon set selection uses catalog IDs — verify all weapon set entries
resolve.

### Files

| File | Change |
|------|--------|
| `port/src/botvariant.c` | Bot name dictionary |
| `pdgui_menu_matchsetup.cpp` | Arena/weapon set verification sweep |

### Estimated LOC: ~100 new

---

## Phase D5.6: Settings & QoL

**Settings tabs already work. This is polish only.**

### Known Issues to Fix

| Issue | Location | Fix |
|-------|----------|-----|
| Settings text overlaps tabs at some resolutions | `pdgui_menu_mainmenu.cpp` | All positions derived from `GetContentRegionAvail()`; no hardcoded pixel literals |
| Scroll indicators too small | All `pdgui_menu_*.cpp` with scrollable lists | Fit content to window; when scrollbars needed, use wide explicit scrollbar |
| Update banner overlap | `pdgui_menu_update.cpp` | Position using `viGetHeight()` anchor, not hardcoded Y |
| B-60 stray 'g'+'s' | `pdgui_menu_mainmenu.cpp` Settings Audio/Video | Convert to `BeginTabItem` content region |
| Update banner during gameplay (B-95) | `pdgui_menu_update.cpp` | Gate on `pdguiIsGameplayActive()` bridge function |

### Layout Sweep Rule

Every layout measurement derives from `ImGui::GetContentRegionAvail()`,
`viGetWidth()`/`viGetHeight()` via `pdguiScale()`, or a fraction thereof. No raw pixel
literals for position or size. Sweep order: `pdgui_menu_mainmenu.cpp` (highest traffic) →
`pdgui_menu_update.cpp` → all remaining `pdgui_menu_*.cpp` files.

### Settings Apply Visual Theme

Settings screen uses `pdguiThemeDrawPanel` + `pdguiThemeDrawBorder` like all other screens.
Settings tabs already use `BeginTabBar`/`EndTabBar` — verify no manual cursor positioning
inside tab content areas.

### Files

All `port/fast3d/pdgui_menu_*.cpp` touched by layout sweep. `pdgui_bridge.c` for
`pdguiIsGameplayActive()`. `port/src/updater.c` for B-99 extraction verification.

### Estimated LOC: ~100 changed lines across ~8 files

---

## Phase D5.7: Systematic OG Menu Removal

**Final pass — only after ALL screens have ImGui renderers.**

### Process

For each legacy `menudialogdef` with a non-NULL `tickFn`:
1. Verify an ImGui `renderFn` is registered and tested
2. Remove the legacy `tickFn` render path (keep `tickFn` only if it drives game logic, not rendering)
3. Remove `menuPush`/`menuPop` calls to the converted screen from other renderers
4. Clean up dead struct fields used only for legacy rendering

### Priority: Trapping Screens First (P0)

Any screen a player can enter but cannot exit is P0:

| Screen | Status after D5.2 | Action |
|--------|-------------------|--------|
| `g_SoloMissionInventoryMenuDialog` | Fixed in D5.2 | Verify then remove legacy path |
| `g_SoloMissionControlStyleMenuDialog` | Unknown | Audit: controller diagram — verify Back works; if legacy 3D, keep; if text-only, convert |
| `g_FrWeaponsAvailableMenuDialog` | Unknown | Audit: training weapons list — verify Back; convert if broken |

### Conversion Checklist (per screen)

- [ ] ImGui `renderFn` registered via `pdguiHotswapRegister()`
- [ ] All data reads through bridge functions or catalog API (no direct `types.h` structs from C++)
- [ ] All buttons have `##id` suffixes scoped to this dialog
- [ ] All sizes use `pdguiScale()` / `GetContentRegionAvail()` (no raw pixels)
- [ ] Working Back/close path that calls `menuPopDialog()`
- [ ] Sub-screens also have exit paths
- [ ] Palette: Blue for normal, Red for danger confirmations
- [ ] Theme: `pdguiThemeDrawPanel` + `pdguiThemeDrawBorder`

Reference `context/designs/menu-inventory.md` for the complete list of screens to audit.

### Files Modified

Determined per-screen during audit. Primary: `pdgui_menu_solomission.cpp`, `pdgui_bridge.c`.

### Estimated LOC: ~200–400 (depends on audit findings)

---

## Execution Blocks

Work is organized into four blocks, each ending with a **playtest gate** before the next block begins.

### Block 1 — Foundation
**D5.0a (tech spike) + D5.0 (visual layer) + D5.1 (input boundary)**

Validate the texture pipeline before writing the theme module. Build the input mode boundary.

_Playtest gate_: Verify scan-lines appear, panels render with OG ROM textures, Esc edge-detect works, no double-push.

### Block 2 — Gameplay-Critical
**D5.2 (pause menu + bot sync fix) + D5.3 (mission select)**

Unblocks in-game play. Pause menu fully functional. Mission select shows correct unlock state. Bot head/body sync fixed in Combat Sim.

_Playtest gate_: Complete a mission start-to-finish from the menu. Verify all pause sub-screens. Verify bot setup has no mismatched head/body.

### Block 3 — Flow Completion
**D5.4 (end game + online lobby nav) + D5.5 (combat sim polish) + D5.6 (settings)**

All player-facing flows reachable and correct. No stuck states. Online lobby tabs gated correctly.

_Playtest gate_: Run a full multiplayer match start-to-finish. Verify end screens. Verify online lobby tab gating. Verify settings at multiple resolutions.

### Block 4 — Cleanup
**D5.7 (OG menu removal) + final verification**

Remove all legacy menu paths. Audit every screen against `context/designs/menu-inventory.md`.

_Playtest gate_: Full regression — every screen in the menu inventory checked off.

---

## Execution Order and Dependencies

```
D5.0a (Technical Spike — validate Fast3D texture bridge)
  │
  └── D5.0 (Visual Layer — theme module, catalog UI textures, scan-line)
        │
        └── D5.1 (Input Ownership Boundary — MENU/GAMEPLAY modes, Esc edge-detect)
              │
              └── D5.2 (Pause Menu + Sub-screens + Bot Sync Fix)
                    │
                    ├── D5.3 (Mission Select Redesign — uses theme + clean input)
                    │
                    ├── D5.4 (End Game Flow + Online Lobby Tab Gating)
                    │
                    ├── D5.5 (Combat Sim Polish — bot names, arena verification)
                    │
                    ├── D5.6 (Settings & QoL — layout sweep, banner fix)
                    │
                    └── D5.7 (OG Menu Removal — only after all screens converted)
```

---

## Success Criteria

- Zero legacy menu screens active in any player-facing flow
- All menus use the PD visual theme from D5.0 (panels, borders, scan-lines)
- Input ownership is clean: no double Esc pushes, no Tab conflicts, no mouse capture timing issues
- Modders can retheme the entire UI by replacing catalog `ui/` texture entries
- Every menu transition is smooth: no tint bleed, no stuck states, no trapping screens
- Settings text never overlaps tabs at any supported resolution
- Update banner suppressed during gameplay

---

## Total Estimated Effort

| Sub-phase | Description | Effort |
|-----------|-------------|--------|
| D5.0a | Technical Spike | ~50 LOC |
| D5.0 | Menu Visual Layer | ~400 LOC |
| D5.1 | Input Ownership Boundary | ~120 LOC |
| D5.2 | Pause Menu + Sub-screens + Bot Sync Fix | ~300 LOC |
| D5.3 | Mission Select Redesign | ~350 LOC |
| D5.4 | End Game Flow + Online Lobby Tab | ~210 LOC |
| D5.5 | Combat Sim Polish | ~100 LOC |
| D5.6 | Settings & QoL | ~100 LOC |
| D5.7 | OG Menu Removal | ~200–400 LOC |
| **Total** | | **~1,830–2,030 LOC** |

---

## Key Files Reference

| File | Sub-phases |
|------|------------|
| `port/fast3d/pdgui_theme.cpp` (new) | D5.0 |
| `port/include/pdgui_style.h` | D5.0 |
| `port/fast3d/pdgui_backend.cpp` | D5.0 (scan-line pass) |
| `port/src/assetcatalog_base.c` | D5.0 (UI texture catalog entries) |
| `port/fast3d/pdgui_bridge.c` | D5.0a, D5.0, D5.1, D5.2, D5.5, D5.6 |
| `port/include/pdgui_menus.h` | D5.0, D5.2 |
| `port/src/pdmain.c` | D5.1 (input mode) |
| `port/src/input.c` | D5.1 (mode gating) |
| `port/fast3d/pdgui_menu_solomission.cpp` | D5.2, D5.3, D5.7 |
| `port/fast3d/pdgui_menu_pausemenu.cpp` | D5.2 (duplicate ID audit) |
| `port/fast3d/pdgui_menu_mainmenu.cpp` | D5.6 (settings layout) |
| `port/fast3d/pdgui_menu_update.cpp` | D5.6 (banner, B-95) |
| `port/fast3d/pdgui_menu_endscreen.cpp` | D5.4 |
| `port/fast3d/pdgui_menu_mpingame.cpp` | D5.4 (post-match scoreboard) |
| `port/fast3d/pdgui_menu_matchsetup.cpp` | D5.2 (bot head/body filter), D5.5 (polish) |
| `port/fast3d/pdgui_menu_room.cpp` | D5.4 (tab gating) |
| `port/fast3d/pdgui_menu_lobby.cpp` | D5.4 (navigation cleanup) |
| `port/src/botvariant.c` | D5.5 (bot name dictionary) |
| `port/src/updater.c` | D5.6 (B-99 extraction) |
