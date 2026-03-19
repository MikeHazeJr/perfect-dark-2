# Task Tracker

## Quick Context
Project: Perfect Dark PC port (C11, CMake + MinGW). User (Mike) compiles on Windows — AI cannot.
Read README.md for full project context. Read the relevant system file for whatever task is active below.

## Purpose
Track the current task, its steps, and progress. Updated at each step start/stop to ensure continuity across AI session resets.

---

## Current Task: Menu System Phase 2 — Bugs, Restructure, and Features

### Status: IN PROGRESS

### Phase 2a: Critical Bug Fixes

| # | Bug | Root Cause | Fix | Status |
|---|-----|-----------|-----|--------|
| 1 | Dropdowns/combos don't work (mouse or controller) | SetNextWindowFocus() called every frame steals focus from combo popup windows | Only call SetWindowFocus on IsWindowAppearing() | DONE |
| 2 | Agent Select requires click/A to start navigating | Same root cause — focus must be set only on first appearance | Same fix as #1 | DONE |
| 3 | Controller nav goes to "panel" not elements | ImGui nav focuses window first, then items. SetItemDefaultFocus on first selectable | Add SetItemDefaultFocus on first button/selectable | DONE |
| 4 | Jump height setting has no effect | extplayerconfig.jumpheight never read by bondwalk.c. Hard-coded 8.2f. Missing from default macro | Wired into bondwalk.c + added to PLAYER_EXT_CFG_DEFAULT | DONE |
| 5 | Movement not inhibited during Main Menu | pdguiIsActive() didn't check hotswap state | pdguiHotswapWasActive() persistent flag | DONE |
| 6 | PD-authentic window rendering (gradient, shimmer, title) | Default ImGui chrome | pdguiDrawPdDialog() with NoTitleBar+NoBackground | DONE, needs texturing polish |

### Phase 2b: Menu Restructure

Main Menu layout change: from 2-tab (Play/Settings) to 3-button + sub-dialogs:
- **Play** → opens Play sub-menu (Solo Missions, Combat Simulator, Co-Op, Counter-Op, Network Game / Host)
- **Settings** → opens Settings sub-menu (Video, Audio, Controls, Game tabs with LB/RB bumper switching)
- **Quit Game** → exits

Play sub-menu hosting features:
- "Host Game" button shows server info (public IP + port) at bottom edge
- Drop-in/drop-out: other players can join host at any point (lobby, match, mission)
- Campaign hosting = seamless co-op/counter-op (no gameplay interruption on join)
- Combat Simulator hosting = same drop-in behavior

Settings tab navigation:
- LB/RB (controller bumpers) switch between Video/Audio/Controls/Game tabs
- Requires reading SDL controller button events in the ImGui render callback

### Phase 2c: Agent Create Screen — DONE

Agent creation flow (from Agent Select "New Agent..."):
- Name input field (InputText, 15 chars, keyboard focus on appear) — DONE
- Body selection carousel (left/right arrows, LB/RB, auto-selects matching head) — DONE
- Head selection carousel (manual override, "Auto" reset button) — DONE
- 3D character preview via FBO render-to-texture — DONE
  - pdgui_charpreview.c creates a 256x256 FBO at init
  - pdguiCharPreviewRequest() triggers menu model load (MENUMODELPARAMS_SET_MP_HEADBODY)
  - pdguiCharPreviewRenderGBI() hooks into menuRenderDialog to render model to FBO via GBI commands
  - ImGui displays FBO texture via dl->AddImage with Y-flip UVs
  - Falls back to placeholder silhouette if FBO not ready
- Create button → writes g_GameFile.name + player config head/body → filemgrPushSelectLocationDialog — DONE
- Cancel button / B / Escape → menuPopDialog — DONE
- C bridge (pdgui_bridge.c) for safe struct access from C++ — DONE
- Registration in pdgui_menus.h → pdguiMenusRegisterAll() — DONE
- Replaces g_FilemgrEnterNameMenuDialog via hotswap — DONE
- TODO: Head display names (currently shows "Head XX" with internal ID)
- TODO: Tune camera/zoom for character preview framing

### Phase 2e: Agent Select Enhancements — DONE

- Contextual action buttons (Load, Duplicate, Delete) appear when agent row is focused — DONE
- Delete confirmation inline with red warning text — DONE
- Uses g_FilemgrFileToDelete + filemgrDeleteCurrentFile() for safe deletion — DONE
- Duplicate loads agent then triggers filemgrPushSelectLocationDialog to save copy — DONE
- Character preview thumbnail on selected agent (uses charpreview FBO) — DONE
- Mouse hover updates selection (bidirectional: hover selects, keyboard selects) — DONE
- Selected row expands to show action buttons, unselected rows compact — DONE
- Save slot count: 30 max (hardcoded in filelist struct, cannot change without breaking struct layout) — BY DESIGN
- Footer shows slot count warning when near capacity — DONE

### Phase 2f: Typed Dialog System (Warning + Success) — DONE

- Extended hotswap with type-based fallback renderers (pdguiHotswapRegisterType) — DONE
- Generic renderTypedDialog() core: palette switch, dynamic title/items, centered buttons — DONE
- MENUDIALOGTYPE_DANGER (2): Red palette, error sound, yellow title — DONE
  - File manager warnings, delete confirm, errors, file lost/in-use, training failures
- MENUDIALOGTYPE_SUCCESS (3): Green palette, success chime, yellow title — DONE
  - Training complete, mission success, device training results, MP ready
- Dynamically reads dialog title (langGet or literal) + iterates menu items — DONE
- Renders LABEL as centered wrapped text, SELECTABLE as centered buttons, SEPARATOR as dividers — DONE
- Unknown item types shown as disabled placeholder text — DONE
- Fallback OK button if no SELECTABLE items found — DONE
- B/Escape dismisses, unique window IDs for multiple simultaneous dialogs — DONE

### Phase 2g: Network Multiplayer Audit & Lobby — IN PROGRESS

**Network system audit (ENet, protocol v18, server-authoritative):**
- Full message protocol documented: CLC_AUTH/MOVE/SETTINGS/RESYNC, SVC_STAGE_START/END/PLAYER_MOVE/SCORES/etc.
- 60Hz tick rate, unreliable movement every frame, reliable state every 15-30 frames
- Desync detection via XOR checksums (3 consecutive → automatic resync)
- Player reconnection with 5-minute identity preservation
- Kill/score notifications: hudmsg system runs locally, triggered by damage events (server-authoritative)
- Post-game stats: SVC_PLAYER_SCORES syncs kills/deaths/points per character → endscreen reads directly — VERIFIED

**Agent character auto-wiring** — VERIFIED ALREADY WORKING:
- Agent load → g_PlayerConfigsArray[0] → netClientReadConfig (called every frame) → CLC_SETTINGS → server → other clients
- Head/body from loaded agent flows automatically to network layer

**Menu fixes this session:**
- Agent Create: pops dialog on submit, plays success sound, auto-saves — DONE
- Agent Select delete: now a proper popup overlay with red PD frame, controller-accessible (A=delete, B=cancel) — DONE
- Agent Select clone/copy: X button on controller — DONE

**Lobby player list sidebar (pdgui_lobby.cpp):**
- Renders when network session active (host or client)
- Shows each connected player: name, state dot (connecting/auth/ready/in-game), team, (you) indicator
- Positioned top-right, auto-sizes to player count
- Renders independently of hotswap (visible during old PD menus too)
- Bridge functions in pdgui_bridge.c for safe access to netclient data

**Remaining for full playtest readiness:**
- Test actual Host → Join → Configure → Start Match → Play → Endscreen flow end-to-end
- Verify simulant spawning and AI behavior in networked match
- Verify team colors apply correctly in networked play
- Verify pickup/prop sync (weapons, ammo, shields on map)
- Test lobby sidebar appearance during actual networked session

### Phase 2d: Network Hosting Architecture (PLAN — future)

Vision: Drop-in/drop-out for all modes beyond initial lobby-based hosting.
- Host starts server, joiners connect anytime (lobby, match, mission)
- Campaign: seamless co-op/counter-op without gameplay interruption
- Spectate mode for all modes
- Requires: mid-game state sync, spectator camera, drop-in slot allocation

### Previous Steps (Phase 1 — COMPLETE)

| # | Step | Status |
|---|------|--------|
| 1 | F8 hot-swap infrastructure | DONE |
| 2 | Agent Select ImGui replacement | DONE |
| 3 | Input routing fix | DONE |
| 4 | Main Menu / CI Hub + Unified Settings | DONE |
| 5 | Agent Select FILEOP bug fix (1→100) | DONE |
| 6 | Controller/gamepad support | DONE |
| 7 | Menu sound FX (pdgui_audio bridge) | DONE |
| 8 | Theme system (tint, glow, sound pack) | DONE |
| 9 | PD-authentic dialog rendering (pdguiDrawPdDialog) | DONE |
| 10 | Movement inhibition (pdguiHotswapWasActive) | DONE |

### Key Files
- `port/include/pdgui_hotswap.h` — Public C API + pdguiHotswapWasActive() + pdguiHotswapRegisterType()
- `port/fast3d/pdgui_hotswap.cpp` — Registry, queue, badge, toggle, persistent active flag, type fallbacks
- `port/fast3d/pdgui_backend.cpp` — F8 handler, input gating, NewFrame/Render with hotswap awareness
- `port/fast3d/pdgui_menu_agentselect.cpp` — Agent Select with contextual buttons, char preview, delete
- `port/fast3d/pdgui_menu_mainmenu.cpp` — Main Menu with Play/Settings structure
- `port/fast3d/pdgui_menu_agentcreate.cpp` — Agent Create screen (name, head/body, 3D preview)
- `port/fast3d/pdgui_charpreview.c` — FBO-based 3D character model preview system
- `port/include/pdgui_charpreview.h` — Character preview public API
- `port/fast3d/pdgui_menu_warning.cpp` — Generic DANGER dialog renderer (type-based fallback)
- `port/fast3d/pdgui_lobby.cpp` — Network lobby player list sidebar overlay
- `port/fast3d/pdgui_bridge.c` — C bridge for safe game struct + netclient access from C++
- `port/fast3d/pdgui_style.cpp` — Palette, shimmer, gradient, text glow, theme
- `port/include/pdgui_audio.h` / `port/fast3d/pdgui_audio.cpp` — Sound FX bridge
- `src/game/menu.c` — Hook in menuRenderDialog() for hot-swap check

### Key Decisions
- F8 defaults ON — new menus show by default, toggle to see old
- Hot-swap is live (real game state flows through)
- Controller input consumed by ImGui when hot-swap is active
- Theme: palette index + tint color/strength + text glow color/intensity + sound FX pack
- PD dialog rendering via pdguiDrawPdDialog() with NoTitleBar+NoBackground ImGui windows
- SetNextWindowFocus only on IsWindowAppearing() to avoid combo/popup focus stealing

### Bug Fixes Log
- **FILEOP_LOAD_GAME was 1, should be 100**: Caused agent selection failure
- **Gamepad events not forwarded**: Now consumed when hotswap active
- **Movement not inhibited**: pdguiHotswapWasActive() persistent flag added
- **Combo/dropdown broken**: SetNextWindowFocus every frame stole popup focus — FIXED
- **Jump height ignored**: extplayerconfig.jumpheight never wired into bondwalk.c — FIXED
- **Old menu extended settings "regression"**: NOT a bug. With F8 ON, inline Settings replaces the old path. F8 OFF still works with old menus normally.
- **Delete confirmation crash**: Nested ImGui::Begin inside hotswap render callback corrupted draw stack. Fixed by using inline draw-list rendering instead of a separate window.
- **langGet(u16) signature mismatch**: Fixed to langGet(s32) in all 4 ImGui files to match lang.c
- **pdguiNewFrame guard mismatch**: Missing networkActive check caused crash when lobby sidebar tried to render without ImGui::NewFrame. Fixed.
- **Agent Create auto-load**: Agent now saves but does NOT auto-load. Player must select from Agent Select list.
- **Skedar/DrCaroll mesh loading failure**: Duplicate array indices in g_MpBodies (0x39/0x3a) overwrote Skedar+DrCaroll with Bond actors. Fixed by moving them to proper indices 0x3d/0x3e with correct BODY_SKEDAR/BODY_DRCAROLL constants. Removed DrCaroll special-case hacks in mpGetBodyId/mpGetMpbodynumByBodynum. Fix applies to MP character select, Agent Create carousel, and network sync.
- **Net menu item overlap**: All `\\n` in netmenu.c literal strings fixed to `\n` for proper PD menu item height calculation.

### Menu Dialog Coverage Audit (205 total in game + 28 port-added)

**Covered by specific ImGui replacements (4 dialogs):**
| Dialog | File | Status |
|--------|------|--------|
| g_FilemgrFileSelectMenuDialog | Agent Select | DONE — full rewrite with actions, preview |
| g_CiMenuViaPcMenuDialog | Main Menu | DONE — Play/Settings/Quit structure |
| g_CiMenuViaPauseMenuDialog | Main Menu (pause) | DONE — same renderer |
| g_FilemgrEnterNameMenuDialog | Agent Create | DONE — name, head/body, preview |

**Covered by type-based fallback renderers (48 dialogs):**
| Type | Count | Palette | Status |
|------|-------|---------|--------|
| MENUDIALOGTYPE_DANGER (2) | 35 | Red | DONE — renderDangerDialog |
| MENUDIALOGTYPE_SUCCESS (3) | 13 | Green | DONE — renderSuccessDialog |

**NOT yet covered (157 DEFAULT-type dialogs):**
These render via the original PD menu system (F8 OFF shows them natively).
Key categories:
- **Solo mission flow** (~15): Select Mission, Briefing, Difficulty, Pause, Inventory, Abort
- **Combat Simulator setup** (~50): Player Setup, Simulants, Teams, Weapons, Arenas, Limits, Challenges, Scenarios
- **Options/Control** (~20): Video, Audio, Display, Control, Options (for each mode context)
- **File manager** (~10): Select Location, Copy, Delete, Rename, Game Notes, Pak menus
- **Training** (~10): FR/DT/HT lists, details, difficulty
- **CI/Hangar** (~8): Bio lists, profiles, hangar, holograph
- **Endscreen** (~14): Mission complete/failed, stats, rankings, game over
- **MP in-game** (~15): Pause, inventory, stats, rankings, end game, save player
- **Port-added** (~28): Extended settings, net menus, manage setups, bind keys

**Priority for next ImGui conversions:**
1. Combat Simulator setup flow (most player-facing)
2. Solo mission flow (pause, briefing)
3. Port-added extended settings (already functional inline in Main Menu)

### Future Enhancements
- **Contextual Tap / Input Enhancement**: Context-sensitive tap for interact vs reload on controller
- **Tint system integration**: Per-dialog color identity blended with global palette
- **Mod Menu** (D3e): Entirely new menu, no PD equivalent
- **Agent snapshot system**: pheadGetTexture → OpenGL texture → ImGui::Image

---

## Previous Task: Font Rendering Corruption in Endscreen Menu

### Status: ON HOLD — Deprioritized in favor of D4 menu migration
After a Combat Simulator match ends, the endscreen menu ("Game Over" / "Game Setup") initially renders correctly with the original PD font, then transitions to a corrupted version where characters appear as blocky rectangles. The font IS Handel Gothic (from ROM bitmap data) but the pixel data or TLUT appears mangled during the transition.

### What We Know
- The correct PD native menu renders first (clean "Game Over", proper gradient, proper proportions)
- Then it transitions to a DIFFERENT-LOOKING rendering: different border style, proportions, gradient quality, and font (blocky rectangles instead of clean text)
- The ENTIRE dialog window changes — not just the font. Color, border, shape, size all differ.
- CONFIRMED: No ImGui game menu code exists. Both renders are the PD native menu system (menugfx.c / textRender / GBI translator)
- The change happens within 1-2 frames during the endscreen transition
- Font integrity checking (textVerifyFontIntegrity CRC32) was added to mpPushEndscreenDialog
- Font loading uses persistent memory cache (mempPCAlloc) across stage transitions
- Text rendering uses CI 4-bit indexed textures with IA16 TLUT palette through GBI→OpenGL translator
- Likely cause: GBI rendering state corruption, resolution/scale change (g_ScaleX?), or viSetMode during endscreen transition

### Steps

| # | Step | Status | Notes |
|---|------|--------|-------|
| 1 | Trace End Game → mpEndMatch → endscreen dialog push | DONE | func0f0f820c(NULL,-6) → menutick.c:548 → mpPushEndscreenDialog |
| 2 | Check font integrity logs from textVerifyFontIntegrity | WAITING | Need user to run match, check console/log output |
| 3 | Investigate TLUT/CI4 conversion in gfx_pc.cpp | IN PROGRESS | import_texture_ci4, palette_to_rgba32, linear_filter mode |
| 4 | Check if texture filtering mode is correct for font textures | PENDING | Should be G_TF_POINT, not bilinear |
| 5 | Check if font pointer fixups survive the stage transition | PENDING | Cache hit path vs fresh DMA path |
| 6 | Apply fix | PENDING | |
| 7 | Test fix | PENDING | User compiles and tests |

### Blockers
- Need font integrity log output from a test run to determine if font memory is corrupted vs rendering issue

---

## Previous Task: ImGui Debug Menu & PD-Authentic Styling (D3d Polish)

### Status: COMPLETE (2026-03-18)
Full PD-authentic ImGui styling system with shimmer effects, palette system, and debug menu.

### What Was Done
- Rewrote pdgui_style.cpp with pixel-accurate shimmer math ported from menugfx.c
- Palette system mirroring original menucolourpalette struct (15 u32 fields, 0xRRGGBBAA)
- 7 built-in palettes: Grey, Blue, Red, Green, White, Silver, Black & Gold
- Theme selector in debug menu with dynamic highlight color
- Black & Gold palette: near-black backgrounds with gold text/borders only
- Button color derivation uses dark body bg (not bright accent) for readability across all themes
- F12 debug menu mouse capture fix (save/restore SDL relative mouse mode)
- Guards in input.c and gfx_sdl2.cpp to prevent game re-grabbing mouse while overlay active
- Font size increased from 16pt to 24pt for crisper rendering

### Key Files Modified
- port/fast3d/pdgui_style.cpp — Complete rewrite with shimmer, palette, style derivation
- port/fast3d/pdgui_debugmenu.cpp — Theme section, highlight fix
- port/fast3d/pdgui_backend.cpp — Mouse grab logic, font size
- port/src/input.c — Guards for overlay-active state
- port/fast3d/gfx_sdl2.cpp — Cursor visibility guard

---

## Previous Task: Build System Improvements

### Status: COMPLETE (2026-03-18)

### What Was Done
- Added `-k` (keep-going) flag to build command for full error lists on failure
- Updated build-gui.ps1: blue progress bar during compile → green on success → red on failure
- Run Game / Run+Log only enabled after successful build (or existing exe found)
- Real-time game process monitoring via 2-second timer
- Fixed compile errors: added `#include "system.h"` to game_1531a0.c, mplayer/ingame.c, lib/mempc.c

---

## Previous Task: Capsule Collision System — Extended Testing

### Status: WAITING FOR USER
User confirmed stationary jumping works. Full movement testing requires user at PC.

### Steps

| # | Step | Status | Notes |
|---|------|--------|-------|
| 1 | Implement capsuleSweep() | DONE | 16-step sweep, cdTestVolume at each step |
| 2 | Implement capsuleFindFloor() | DONE | BG floor + binary search prop surfaces |
| 3 | Implement capsuleFindCeiling() | DONE | BG ceiling + binary search upward |
| 4 | Create capsule.h header | DONE | API, struct capsulecast, constants |
| 5 | Add capsule.c to CMakeLists.txt | DONE | Line 279, before collision.c |
| 6 | Integrate floor detection in bondwalk.c | DONE | ~line 994-1028, replaces prop surface hack |
| 7 | Integrate pre-move sweep in bondwalk.c | DONE | ~line 1204-1248, clamps vertical movement |
| 8 | Integrate ceiling detection in bondwalk.c | DONE | ~line 1259-1302, replaces legacy ceiling-only |
| 9 | Test stationary jumping | DONE | User confirmed: "jump seems to work much better" |
| 10 | Test movement during jumping | WAITING | User needs to be at PC |
| 11 | Test standing on props (desks, crates) | WAITING | User needs to test |
| 12 | Test jumping near ceilings/overhangs | WAITING | User needs to test |
| 13 | Test collisions with moving objects | WAITING | cdTestVolume tests CDTYPE_ALL, should work |

### Blockers
- User must compile and test on Windows PC (cannot compile from Linux VM)

---

## Previous Task: Context Restructuring

### Status: COMPLETE

### Steps

| # | Step | Status | Notes |
|---|------|--------|-------|
| 1 | Create context/ folder | DONE | |
| 2 | Create context/README.md | DONE | Index, architectural facts, usage guide |
| 3 | Create context/collision.md | DONE | Capsule system, legacy system, integration points |
| 4 | Create context/movement.md | DONE | Jump physics, vertical pipeline, ground/ceiling detection |
| 5 | Create context/networking.md | DONE | All phases 1-10 + C1-C12, message tables, damage authority |
| 6 | Create context/build.md | DONE | CMake, MSYS2/MinGW, static linking, mod loading |
| 7 | Create context/roadmap.md | DONE | D1-D8 phases, dependencies, TODOs |
| 8 | Create context/tasks.md | DONE | This file |

---

## Session Fixes Log (Post-D1 Runtime)

| Fix | File | Issue | Status |
|-----|------|-------|--------|
| FIX-1 | CMakeLists.txt | Missing -static-libgcc | APPLIED, TESTED OK |
| FIX-2 | botinv.c | Bot weapon table misaligned | APPLIED, NEEDS TESTING |
| FIX-3 | main.c | Jump height config not registered | APPLIED, NEEDS TESTING |
| FIX-4 | fs.c | Default mod directories | APPLIED, NEEDS TESTING |
| FIX-5 | CMakeLists.txt | Static link libwinpthread | APPLIED, TESTED OK |
| FIX-6 | setup.c | Character screen layout overlap | APPLIED, NEEDS TESTING |
| FIX-7 | mplayer.c | Missing g_NetMode guard in mpEndMatch | APPLIED, NEEDS TESTING |
| FIX-8 | mplayer.c | Missing g_NetMode guard in mpHandicapToDamageScale | APPLIED, NEEDS TESTING |
| FIX-9 | mplayer.c | Missing server chrslots setup in mpStartMatch | APPLIED, NEEDS TESTING |
| FIX-10 | menutick.c | Misplaced chrslots loop + missing condition | APPLIED, NEEDS TESTING |
| FIX-11 | ingame.c | Missing save dialog guard | APPLIED, NEEDS TESTING |
| FIX-12 | netmenu.c | Join screen button overlap | APPLIED, NEEDS TESTING |

### Additional Session Fixes

| Fix | File | Issue | Status |
|-----|------|-------|--------|
| FIX-7b | setup.c | Dr Carroll head crash protection | APPLIED, NEEDS TESTING |
| FIX-8b | menu.c, player.c | Null model protection | APPLIED, NEEDS TESTING |
| FIX-9b | setup.c | Simulant spawning debug logging | APPLIED, NEEDS TESTING |
| FIX-10b | setup.c | Character select screen redesign | APPLIED, NEEDS TESTING |
| FIX-11b | ingame.c | Pause menu scrolling | APPLIED, NEEDS TESTING |

### Session Fixes (2026-03-18 — ImGui & Build)

| Fix | File | Issue | Status |
|-----|------|-------|--------|
| FIX-13 | game_1531a0.c, ingame.c, mempc.c | Missing `#include "system.h"` for sysLogPrintf | APPLIED, TESTED OK |
| FIX-14 | pdgui_style.cpp | Black & Gold theme: buttons dark with gold text/borders only | APPLIED, NEEDS TESTING |
| FIX-15 | pdgui_style.cpp | Button derivation: dark body bg instead of bright accent (all themes) | APPLIED, NEEDS TESTING |
| FIX-16 | pdgui_backend.cpp, input.c, gfx_sdl2.cpp | F12 debug menu mouse capture/release | APPLIED, TESTED OK |
| FIX-17 | pdgui_debugmenu.cpp | Theme selector highlight uses current theme color | APPLIED, NEEDS TESTING |
| FIX-18 | pdgui_backend.cpp | Font base size increased 16pt → 24pt for crisper rendering | APPLIED, NEEDS TESTING |
| FIX-19 | build-gui.ps1 | Build tool: colored progress, gated run buttons, process monitoring, -k flag | APPLIED, TESTED OK |

---

## Known Issues (Outstanding)

| Issue | Description | Likely Fix | Status |
|-------|-------------|------------|--------|
| Bots not moving/respawning | Multiple root causes | FIX-2 + FIX-8 + FIX-9 | NEEDS TESTING |
| End-game crash | Crash at match end | FIX-7 + FIX-2 + FIX-9 | NEEDS TESTING |
| Jump not working (non-capsule) | Pre-capsule issue | FIX-3 + capsule system | Capsule approach working |
| Character screen layout | Overlap of list and preview | FIX-6 + FIX-10b | NEEDS TESTING |
| Invisible character | Missing model file | FIX-8b (crash prevented) | Model availability TBD |
| Simulants not spawning | chrslots bits or allocation | FIX-9b logging added | NEEDS LOG OUTPUT |
| Blue geometry at Paradox | Visual bug at center pit | Not investigated | LOW PRIORITY |
| Endscreen font/menu corruption | After match end, "Game Over" dialog renders correctly first then switches to blocky/different rendering. ENTIRE window changes — border style, proportions, gradient, font — not just font. Both renders are PD native menu system (no ImGui game menus exist). Suggests GBI state corruption or rendering resolution/scale change during endscreen transition. | Investigate g_ScaleX, viSetMode, GBI state persistence, TLUT | HIGH PRIORITY |

---

## How To Use This File
1. **Starting a new task**: Add a new "Current Task" section at the top with steps
2. **At each step start**: Update the step status to IN PROGRESS
3. **At each step completion**: Update to DONE with notes
4. **On blockers**: Note the blocker and what's needed to unblock
5. **On session reset**: Read this file FIRST to know where we left off
6. **Move completed tasks**: Shift finished tasks down under "Previous Task" sections
