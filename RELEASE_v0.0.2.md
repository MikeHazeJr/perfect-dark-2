# Perfect Dark 2 — v0.0.2 Release Notes

## Overview

Major menu system overhaul, networking audit, and gameplay bug fixes. This release replaces the N64-era menu system with a modern ImGui-based interface while preserving the authentic PD visual identity (gradients, shimmer, palettes). Full controller support, network lobby sidebar, and critical character loading fixes.

---

## New Features

### ImGui Menu System (F8 Hot-Swap)
- **F8 toggles** between new ImGui menus and original PD menus in real-time
- **Agent Select** — Full rewrite with agent cards, character preview thumbnails, and contextual actions (A: Load, X: Copy, Y: Delete, D-Pad: Navigate)
- **Agent Create** — New screen with name input, body/head carousel selection, 3D character preview via FBO render-to-texture, and opaque backdrop
- **Main Menu** — Restructured to Play / Settings / Quit with sub-navigation; Settings has Video/Audio/Controls/Game tabs with LB/RB bumper switching
- **Warning/Success Dialogs** — Type-based fallback system auto-handles all 48 DANGER (red) and SUCCESS (green) dialogs with dynamic title/item rendering
- **Delete/Copy Confirmation** — Inline prompt overlay ("Press A to confirm, B to cancel") — no nested windows, fully controller-accessible

### Network Lobby Sidebar
- Real-time player list overlay when hosting or joined to a network session
- Shows each player's name, connection state (connecting/auth/ready/in-game), team indicator
- Positioned top-right, auto-sizes, renders independently of menu system

### Character Preview System
- FBO-based 3D model rendering for character thumbnails
- Hooks into the GBI render phase via `pdguiCharPreviewRenderGBI()`
- Uses game's existing `menuRenderModel()` pipeline for authentic N64-quality renders
- Falls back to placeholder silhouette when FBO unavailable

### PD-Authentic Visual Style
- Pixel-accurate shimmer effects ported from `menugfx.c`
- 7 color palettes: Grey, Blue, Red, Green, White, Silver, Black & Gold
- Gradient title bars, animated border shimmer, text glow effects
- Theme system: palette + tint + glow + sound pack

### Hot-Swap Infrastructure
- Type-based fallback renderers (`pdguiHotswapRegisterType()`) — register one renderer for an entire dialog type
- Persistent active flag (`pdguiHotswapWasActive()`) bridges timing gap between GBI and input phases
- Per-menu override system (force NEW or OLD per dialog)

---

## Bug Fixes

### Critical
- **Skedar & Dr. Carroll mesh loading failure** — Duplicate array indices in `g_MpBodies` (0x39/0x3a) overwrote Skedar and Dr. Carroll entries with Bond actor bodies. Fixed with correct constants at indices 0x3d/0x3e. Removed stale DrCaroll special-case hacks in `mpGetBodyId`/`mpGetMpbodynumByBodynum`.
- **Delete confirmation crash** — Nested `ImGui::Begin` inside hotswap render callback corrupted the draw stack. Replaced with inline draw-list rendering.
- **pdguiNewFrame guard mismatch** — Missing `networkActive` check caused crash when lobby sidebar rendered without `ImGui::NewFrame()`.
- **Jump height setting had no effect** — `extplayerconfig.jumpheight` was never read by `bondwalk.c` (hard-coded 8.2f) and was missing from `PLAYER_EXT_CFG_DEFAULT`. Wired into jump impulse calculation.

### Menu & Input
- **Dropdown/combo widgets broken** — `SetNextWindowFocus()` called every frame stole focus from popup windows, immediately closing them. Fixed to `SetWindowFocus()` only on `IsWindowAppearing()`.
- **Agent Select required click to start navigating** — Same root cause as dropdown fix.
- **Movement not inhibited during Main Menu** — `pdguiIsActive()` didn't check hotswap state. Added persistent flag.
- **langGet(u16) signature mismatch** — All ImGui files declared `u16` parameter, actual function takes `s32`. Fixed in all 4 files.
- **Agent Create auto-loaded agent** — Now saves without auto-loading; player must explicitly select agent.
- **Network menu item overlap** — All `\\n` (literal backslash-n) in `netmenu.c` strings fixed to `\n` (actual newline) for proper PD menu item height calculation.
- **Overlay menus transparent** — Agent Create and typed dialogs now draw opaque backdrop before PD frame.

---

## Infrastructure

### New Files (14)
| File | Purpose |
|------|---------|
| `port/fast3d/pdgui_menu_agentselect.cpp` | Agent Select with actions, preview, delete |
| `port/fast3d/pdgui_menu_agentcreate.cpp` | Agent Create with name, head/body, preview |
| `port/fast3d/pdgui_menu_mainmenu.cpp` | Main Menu (Play/Settings/Quit) |
| `port/fast3d/pdgui_menu_warning.cpp` | Generic DANGER + SUCCESS dialog renderer |
| `port/fast3d/pdgui_lobby.cpp` | Network lobby player list sidebar |
| `port/fast3d/pdgui_charpreview.c` | FBO-based 3D character model preview |
| `port/fast3d/pdgui_bridge.c` | C bridge for safe struct access from C++ |
| `port/fast3d/pdgui_hotswap.cpp` | Hot-swap registry, queue, type fallbacks |
| `port/fast3d/pdgui_style.cpp` | PD palette, shimmer, gradient, theme |
| `port/fast3d/pdgui_audio.cpp` | Menu sound FX bridge |
| `port/include/pdgui_hotswap.h` | Hot-swap public API |
| `port/include/pdgui_charpreview.h` | Character preview API |
| `port/include/pdgui_menus.h` | Menu registration hub |
| `port/include/pdgui_audio.h` | Sound FX constants |

### Modified Core Files
| File | Change |
|------|--------|
| `src/game/menu.c` | Hot-swap check + charpreview GBI hook in `menuRenderDialog()` |
| `src/game/mplayer/mplayer.c` | Fixed g_MpBodies array (Skedar/DrCaroll), cleaned conversion functions |
| `src/game/bondwalk.c` | Wired `extplayerconfig.jumpheight` into jump impulse |
| `src/game/mplayer/mplayer.c` | Added `.jumpheight` to `PLAYER_EXT_CFG_DEFAULT` |
| `port/src/net/net.c` | Enhanced stage start/end logging with per-client detail |
| `port/src/net/netmenu.c` | Fixed `\\n` → `\n` in all menu item strings |
| `port/fast3d/gfx_pc.cpp` | ImGui overlay render calls after GBI phase |

---

## Menu Coverage Estimate

| Category | Count | Coverage |
|----------|-------|----------|
| Specific ImGui replacements | 4 | Agent Select, Main Menu (×2), Agent Create |
| Type-based fallback (DANGER) | 35 | All warning/error/delete/failure dialogs |
| Type-based fallback (SUCCESS) | 13 | All completion/success dialogs |
| **Total covered** | **52 / 233** | **~22%** |
| Remaining (DEFAULT type) | 157 | Rendered via original PD menu system |
| Port-added dialogs | 28 | Extended settings, net menus, manage setups |

---

## Known Issues
- Endscreen font/menu corruption after Combat Sim match (ON HOLD — GBI state issue)
- Head display names show "Head XX" instead of descriptive names
- Character preview camera/zoom needs tuning
- 157 DEFAULT-type dialogs still use original PD renderer (F8 toggles between old/new)

---

## Build
- **Platform**: Windows x86_64 (MSYS2/MinGW)
- **Compiler**: GCC via CMake
- **Dependencies**: SDL2, OpenGL (GLAD), ENet, ImGui (bundled)
