# ImGui Integration & PD-Authentic Styling

## Status: FOUNDATION COMPLETE (D3d), POLISH ONGOING
Dear ImGui v1.91.8 vendored and integrated. PD-authentic styling with shimmer effects and palette system implemented. Debug menu functional with F12 toggle. Game menu migration (D3e+) not yet started.

## Architecture

### Files
```
port/fast3d/imgui/           — Vendored Dear ImGui v1.91.8
port/fast3d/pdgui_backend.cpp — Init, frame lifecycle, event handling, F12 toggle, mouse grab
port/fast3d/pdgui_style.cpp   — PD-authentic styling: shimmer, gradients, palette, style derivation
port/fast3d/pdgui_debugmenu.cpp — F12 debug overlay: perf, network, memory, themes, game controls
port/include/pdgui.h          — Public C API (pdguiInit, pdguiIsActive, pdguiToggle, etc.)
port/include/pdgui_style.h    — Style API (pdguiApplyPdStyle, pdguiSetPalette, pdguiDrawPdDialog, etc.)
port/include/pdgui_font_handelgothic.h — Embedded Handel Gothic OTF (24,648 bytes) for ImGui text
```

### Integration Points
- **Init**: `pdguiInit(videoGetWindowHandle())` in `main.c` after `videoInit()`
- **Render**: Inside `gfx_run()` after `rapi->end_frame()`, before `wapi->swap_buffers_begin()`
- **Events**: `pdguiProcessEvent()` in `gfx_sdl2.cpp` event loop, before PD's handler
- **Toggle**: F12 key (consumed by pdguiProcessEvent — PD never sees it)
- **GL state reset**: `gfx_opengl_reset_for_overlay()` between PD scene and ImGui
- **Input isolation**: `pdguiWantsInput()` gates PD's keyboard/mouse handlers

### Mouse Grab Fix (2026-03-18)
When F12 debug overlay opens:
- `pdguiUpdateMouseGrab(true)`: saves SDL relative mouse mode, disables it, shows cursor, warps to center
- `pdguiUpdateMouseGrab(false)`: restores saved mouse mode and cursor state

Guards prevent game from re-grabbing while overlay active:
- `port/src/input.c`: `inputLockMouse()` and `inputMouseShowCursor()` skip when `pdguiIsActive()`
- `port/fast3d/gfx_sdl2.cpp`: `gfx_sdl_set_cursor_visibility()` skips hide when overlay active

## PD-Authentic Styling System (pdgui_style.cpp)

### Design Philosophy
The original PD menu rendering is entirely procedural — no pre-rendered textures. Uses vertex-colored triangles for gradients, 1px border lines with shimmer overlays, solid fill for backgrounds. We faithfully port this math to ImGui's draw API.

### Shimmer (ported from menugfxDrawShimmer)
- 20-second cycle via fractional time (matches g_20SecIntervalFrac)
- 6x travel multiplier: 6 * frac * 600 = 3600px total, modulo 600px period
- Alpha formula: `tailcolour = ((baseAlpha * (255 - alpha)) / 255) | 0xffffff00`
- Position-offset phase: `v0 += (y1 + x1)` for unique shimmer on each edge
- Horizontal and vertical variants for all four border edges + title bar edges

### Palette System
Mirrors original `struct menucolourpalette` (15 u32 fields, 0xRRGGBBAA format):
```
dialog_border1, dialog_titlebg, dialog_border2, dialog_titlefg,
dialog_bodybg, unused14, item_unfocused, item_disabled,
item_focused_inner, checkbox_checked, item_focused_outer,
listgroup_headerbg, listgroup_headerfg, unused34, unused38
```

7 built-in palettes (matching original g_MenuColours[] indices):
| Index | Name | Notes |
|-------|------|-------|
| 0 | Grey | Background/faded dialogs |
| 1 | Blue | PD's signature look (default) |
| 2 | Red | Combat Simulator / enemy |
| 3 | Green | |
| 4 | White | |
| 5 | Silver | |
| 6 | Black & Gold | Campaign completion reward (new for PD2) |

### Style Derivation (pdguiApplyPdStyle)
All ImGui colors derived from active palette:
- **Window/frame bg**: dialog_bodybg with alpha variations
- **Borders**: dialog_border1
- **Title bar**: dialog_titlebg (bg) / dialog_border1 (active)
- **Text**: item_unfocused (normal) / item_disabled (greyed)
- **Buttons**: dialog_bodybg base (dark) → dialog_border1 on hover/active
- **Check marks**: dialog_border2 (bright accent)
- **Headers**: dialog_bodybg base → dialog_border1 on hover

Key design decision: Buttons use dark body bg, NOT bright border accent, for readability across all themes (especially Black & Gold).

### Black & Gold Palette Design
Near-black backgrounds with gold only on text/borders:
- dialog_bodybg: `0x0a06009f` (near-black)
- dialog_titlebg: `0x1408007f` (near-black warm tint)
- dialog_border1: `0xbf8f207f` (gold at 50% alpha)
- dialog_titlefg: `0xffd060ff` (bright gold text)
- item_unfocused: `0xdda830ff` (gold text)
- item_focused_outer: `0x2a1800ff` (very dark gold bg for focused items)

## Debug Menu (pdgui_debugmenu.cpp)

### Sections
1. **Performance**: FPS, frame time, stage number
2. **Network State**: mode, clients, tick, game mode, port, connection state
3. **Memory Diagnostics**: persistent pool stats, heap usage, validation
4. **Theme Switcher**: 7 palette buttons (2 per row), active theme highlighted with dynamic color
5. **Network Controls**: Host Lobby, Start Match, End Match, Disconnect

### Theme Selector
- Buttons arranged 2 per row using `ImGui::SameLine()`
- Active theme highlighted using current theme's ButtonHovered color (not hardcoded blue)
- Switching themes calls `pdguiSetPalette()` which re-applies all ImGui colors

## Font

### ImGui Font
- Handel Gothic OTF embedded as C array in `pdgui_font_handelgothic.h`
- Loaded at init: `io.Fonts->AddFontFromMemoryTTF(fontCopy, size, 24.0f, &cfg)`
- Base size: 24pt (increased from 16pt for crisper rendering at modern resolutions)

### Game's Native Font (SEPARATE from ImGui)
The game's own menu text uses bitmap fonts loaded from ROM segments:
- Loaded via `textLoadFont()` in `game_1531a0.c`
- DMA from ROM → persistent memory (mempPCAlloc) with font cache
- Rendered as CI 4-bit indexed textures with IA16 TLUT palette
- Through GBI→OpenGL translator in `gfx_pc.cpp`
- Font integrity checking: `textVerifyFontIntegrity()` computes CRC32 checksum

**KNOWN BUG**: Font renders correctly initially, then becomes corrupted (blocky rectangles) during endscreen transition. See tasks.md for investigation status.

## C++ / types.h Conflict
`types.h` defines `#define bool s32` which breaks C++. All C++ files (pdgui_*.cpp, gfx_*.cpp) must avoid including game headers. Use `extern "C"` declarations for C-callable functions.

## What's NOT Yet Built
- dynmenu.c fluent builder API (D3e)
- modmenu.c mod manager screen (D3e)
- ImGui versions of any game menus (D4+ — preview system planned)
- 3D model preview FBO→ImGui image widget (planned for D11)
- Any game menu ImGui replacements — all game menus still use the original PD native rendering pipeline
