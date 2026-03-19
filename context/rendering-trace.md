# Rendering Trace: Endscreen Menu Pipeline

## Purpose
Cross-reference map of every file involved in rendering the endscreen "Game Over" dialog. Built to prevent misidentification of rendering systems and make future code traces efficient.

Last updated: 2026-03-18

---

## The Definitive Answer: What Renders the Endscreen

**Both the "correct" first render and the "corrupted" second render are the PD native menu system.** There is no ImGui game menu code anywhere in the codebase. The ImGui system (pdgui_*.cpp) is used exclusively for the F12 debug overlay.

The pdgui_style.cpp file contains prepared-but-unused helper functions (`pdguiDrawPdDialog`, `pdguiDrawItemHighlight`) that were built as infrastructure for future ImGui game menus (Phase D4+), but they are **never called** from anywhere.

---

## 1. Call Chain: Match End → Endscreen Dialog

```
mpEndMatch() [mplayer.c:2744]
  → musicStartMenu()
  → mpSetPaused(MPPAUSEMODE_GAMEOVER)
  → mpCalculateAwards()
  → func0f0f820c(NULL, -6) [menu.c:3576]  — closes all menus, sets prevmenuroot = -6

NEXT FRAME: menuTick() [menutick.c:548]
  → detects g_MenuData.prevmenuroot == -6
  → calls func0f0fd548(4)
  → for each player slot:
      mpPushEndscreenDialog(playernum, i) [ingame.c:891]
        → selects dialog based on mode:
            Individual: g_MpEndscreenIndGameOverMenuDialog (MENUDIALOGTYPE_DEFAULT)
            Team: g_MpEndscreenTeamGameOverMenuDialog (MENUDIALOGTYPE_DEFAULT)
            Challenge Won: g_MpEndscreenChallengeCompletedMenuDialog (MENUDIALOGTYPE_SUCCESS)
            Challenge Lost: g_MpEndscreenChallengeFailedMenuDialog (MENUDIALOGTYPE_DANGER)
        → menuPushRootDialog(dialog, MENUROOT_MPENDSCREEN) [menu.c:3626]
            → sets numdialogs = 0
            → sets bg = MENUBG_BLUR (or MENUBG_FAILURE for DANGER type)
            → menuPushDialog() [menu.c:1512]
                → creates menudialog struct
                → state = MENUDIALOGSTATE_PREOPEN
                → transitionfrac = -1 (no color transition)
```

---

## 2. Render Chain: Dialog → Pixels

```
EVERY FRAME:
menuRender() [menu.c:5412]
  → sets g_ScaleX = (g_ViRes == VIRES_HI) ? 2 : 1  [line 5414, NTSC only]
  → menuRenderDialogs() [menu.c:3754]
      → for MENUROOT_MPENDSCREEN: sets projection center from dialog pos
      → menuRenderDialog(gdl, dialog, menu) [menu.c:3728]
          → textSetWaveBlend()
          → dialogRender(gdl, dialog, menu) [menu.c:2585]
              1. State animation (PREOPEN: slide in from top)
              2. Resolve title string
              3. Fetch colors from g_MenuColours[dialog->type] via MIXCOLOUR macro
              4. Render 3D projection planes (menugfxDrawPlane)
              5. Render title bar gradient (menugfxRenderGradient + menugfxDrawShimmer)
              6. Set wave text colors
              7. Render title text with textRender()
              8. menugfxRenderDialogBackground() [menugfx.c:172]
                  → gDPFillRectangleScaled (uses g_ScaleX for X coords)
                  → menugfxDrawDialogBorderLine() [menugfx.c:1106]
                      → menugfxDrawLine() + menugfxDrawShimmer()
              9. Render menu items
             10. Render scrollbar/chevrons if needed
          → textResetBlends()
  → resets g_ScaleX = 1 [line 5761]
```

---

## 3. Text Rendering Chain

```
textRender() [game_1531a0.c:2403]
  → *x *= g_ScaleX  [line 2415]  ← CRITICAL: scales X position
  → sets TLUT mode: G_TT_IA16
  → sets tile format: G_IM_FMT_CI, G_IM_SIZ_4b
  → sets 2-cycle combiner for CI4 rendering
  → for each character:
      textRenderChar() [game_1531a0.c:2301]
        → gDPSetTextureImage (CI4 bitmap data from ROM font)
        → gDPLoadBlock (load texture block)
        → gSPTextureRectangleEXT (draw character quad)
            → GBI display list sent to fast3d translator

GBI → OpenGL Translation [gfx_pc.cpp]:
  → import_texture_ci4() [line 794]
      → reads rdp.texture_tile[tile].palette (0-15)
      → reads rdp.palette[pal_idx * 16 + index] for each pixel
      → palette_to_rgba32() [line 773]
          → IA16 format: intensity = low byte, alpha = high byte
          → outputs RGBA32 for OpenGL texture upload
  → gfx_rapi->upload_texture() to GPU
```

---

## 4. g_ScaleX Reference Map

### Definition
- `data.h:416` — `extern s32 g_ScaleX;`
- `gbiex.h:95-117` — macros that multiply X coordinates by g_ScaleX

### Where It's Set (all locations)

| File | Line | Function | Value | Context |
|------|------|----------|-------|---------|
| menu.c | 5414 | menuRender() | 2 or 1 | **Start of menu rendering** |
| menu.c | 5761 | menuRender() | 1 | End of menu rendering |
| activemenu.c | 1261 | amRender() | 2 or 1 | Active menu (combat HUD) |
| activemenu.c | 1699 | amRender() | 1 | End of active menu |
| bondgun.c | 12789 | HUD render | 2 or 1 | Gun/weapon HUD |
| bondgun.c | 13020,13143 | HUD cleanup | 1 | Reset |
| sight.c | 1574 | Sight render | 2 or 1 | Crosshair/sight |
| sight.c | 1627 | Sight cleanup | 1 | Reset |
| radar.c | 279 | Radar render | 2 or 1 | Minimap |
| radar.c | 425 | Radar cleanup | 1 | Reset |
| mplayer.c | 1575 | MP init | 2 or 1 | Multiplayer startup |
| mplayer.c | 1679 | MP cleanup | 1 | Reset |

### Where It's Read (key locations)

| File | Context |
|------|---------|
| game_1531a0.c:2415 | textRender() — `*x *= g_ScaleX` |
| gbiex.h (gDPFillRectangleScaled) | All filled rectangles in menugfx.c |
| mplayer/setup.c | Multiplayer menu draw calls |
| mplayer/ingame.c | In-game MP HUD |
| trainingmenus.c | Training scene display |

### Rule
g_ScaleX is always `(g_ViRes == VIRES_HI) ? 2 : 1`. It's set at the beginning of each rendering function and reset to 1 at the end. During endscreen, `menuRender()` controls it.

---

## 5. Dialog Type → Visual Appearance

### Type Constants (constants.h:1665-1669)
```
MENUDIALOGTYPE_DEFAULT = 1  (Blue/cyan)
MENUDIALOGTYPE_DANGER  = 2  (Red)
MENUDIALOGTYPE_SUCCESS = 3  (Green)
MENUDIALOGTYPE_4       = 4
MENUDIALOGTYPE_WHITE   = 5
```

### Color Resolution
```c
// constants.h:76
#define MIXCOLOUR(dialog, property) \
  dialog->transitionfrac < 0.0f ? \
    g_MenuColours[dialog->type].property : \
    colourBlend(g_MenuColours[dialog->type2].property, \
                g_MenuColours[dialog->type].property, \
                dialog->colourweight)
```

When `transitionfrac < 0` (normal state): use `g_MenuColours[type]` directly.
When `transitionfrac >= 0` (transitioning): blend between `type` and `type2` palettes.

### Color Palette Array
`g_MenuColours[]` in menu.c:95 — indexed by dialog type (1-5). Each entry has 15 properties: borders, title bg/fg, body bg, item fg, focus colors, etc.

### Dialog State Machine (constants.h:1660-1663)
```
MENUDIALOGSTATE_PREOPEN    = 0  (Entering screen — slide-in animation)
MENUDIALOGSTATE_OPENING    = 1  (Opening animation — expanding)
MENUDIALOGSTATE_POPULATING = 2  (Menu items animating in)
MENUDIALOGSTATE_POPULATED  = 3  (Fully open and stable)
```

Each state transition uses `dialog->statefrac` (0.0→1.0) for animation interpolation.

### Type Transition (menu.c:4140-4148)
When dialog type changes mid-life (e.g., DEFAULT → DANGER):
- `dialog->type2 = newType`
- `dialog->colourweight = 0`
- `dialog->transitionfrac = 0` (enables MIXCOLOUR blending)
- Each frame, `colourweight` increments → smooth color shift

---

## 6. File Cross-Reference Matrix

### Core Rendering Files

| File | Includes | Included By | Key Functions |
|------|----------|-------------|---------------|
| **menugfx.c** | ultra64, constants, tex, menu, game_1531a0, gfxmemory, vi, main, video, menugfx.h | — | menugfxRenderDialogBackground, menugfxDrawShimmer, menugfxDrawDialogBorderLine, menugfxDrawFilledRect |
| **menugfx.h** | — | menu.c, menuitem.c, menutick.c, menustop.c, endscreen.c, credits.c, hudmsg.c, modeldef.c, boot.c, sched.c | All menugfx public API |
| **menu.c** | menugfx.h, game_1531a0.h, vi.h | — | menuRender, menuRenderDialog, dialogRender, menuPushRootDialog, menuPushDialog |
| **game_1531a0.c** | ultra64, constants, tex, gfxmemory, vi | — | textLoadFont, textRender, textRenderChar, textVerifyFontIntegrity |
| **menutick.c** | menugfx.h, menu.h | — | menuTick (prevmenuroot==-6 handling) |
| **ingame.c** | menu.h | — | mpPushEndscreenDialog, dialog struct definitions |
| **endscreen.c** | menugfx.h, menu.h | — | endscreenPushCoop, endscreenPushAnti, particle memory |

### GBI Translation Files

| File | Role | Key Functions |
|------|------|---------------|
| **gfx_pc.cpp** | GBI command interpreter | import_texture_ci4, palette_to_rgba32, texture cache |
| **gfx_opengl.cpp** | OpenGL backend | upload_texture, draw calls, gfx_opengl_reset_for_overlay |
| **gfx_sdl2.cpp** | Window management | SDL window, GL context, swap buffers |
| **gbiex.h** | Extended GBI macros | gDPFillRectangleScaled (uses g_ScaleX) |

### Video/Resolution Files

| File | Role | Key Symbols |
|------|------|-------------|
| **vi.c** | Video interface | viSetMode (sets buffer w/h), g_ViBackData, g_ViModeWidths/Heights |
| **player.c** | Resolution control | playerConfigureVi, playerTick (sets g_ViRes) |
| **video.h** | Video declarations | Resolution constants |
| **data.h** | Global data | g_ScaleX declaration |

### ImGui Files (F12 debug overlay ONLY)

| File | Role | References Game Code? |
|------|------|-----------------------|
| **pdgui_backend.cpp** | ImGui lifecycle, F12 toggle | Only SDL/OpenGL — no game rendering |
| **pdgui_debugmenu.cpp** | Debug panels | Reads g_NetMode, g_StageNum, etc. (extern "C") |
| **pdgui_style.cpp** | PD-authentic styling | Self-contained — no game rendering calls |
| **pdgui_font_handelgothic.h** | Embedded TTF data | N/A |

**Critical fact**: pdguiNewFrame() and pdguiRender() both return immediately if `!g_PdguiActive`. ImGui renders NOTHING when F12 overlay is closed. The overlay runs AFTER `gfx_opengl_reset_for_overlay()`, in a separate viewport/projection.

---

## 7. Endscreen Bug Investigation Leads

### What changes between "correct" and "corrupted" renders

The first render shows the standard PD menu with clean text and proper proportions. Within 1-2 frames, the ENTIRE dialog window changes: border style, proportions, gradient quality, and font (blocky rectangles).

### Suspects (ranked by likelihood)

1. **Dialog state transition** — The dialog goes through PREOPEN → OPENING → POPULATING → POPULATED. Each state renders differently (e.g., OPENING uses `menugfxRenderDialogBackground(... 1.0f)` vs POPULATED using `... -1.0f`). The visual change could be the normal state machine progression, but the text corruption suggests something deeper.

2. **g_ScaleX change** — If `g_ViRes` changes between frames (e.g., from a delayed `playerConfigureVi()` call), `g_ScaleX` would change, causing all X coordinates in text and filled rectangles to shift. The text rendering multiplies X position by g_ScaleX at line 2415.

3. **TLUT palette corruption** — The RDP palette (`rdp.palette[]` in gfx_pc.cpp) is shared state. If another rendering operation between frames overwrites the palette entries that the font TLUT expects, the CI4 → RGBA32 conversion would produce wrong colors. The menu background rendering uses different texture modes than the font rendering.

4. **GBI state leakage** — The GBI translator in gfx_pc.cpp maintains state (combine mode, texture filter, cycle type, etc.). If the endscreen background rendering leaves the GBI state in a configuration that's wrong for subsequent font rendering, the text could appear corrupted. Key states: texture filter mode (should be G_TF_POINT for fonts, bilinear would blur them), cycle type (2-cycle for CI4).

5. **Viewport/scissor change** — `dialogRender()` sets scissor regions for menu items. If the scissor doesn't match the new dialog dimensions during state transition, content could be clipped or rendered at wrong positions.

### Diagnostic Plan
- Add `sysLogPrintf` in `menuRender()` at line 5414 to log `g_ViRes` and `g_ScaleX` every frame during MENUROOT_MPENDSCREEN
- Add `sysLogPrintf` in `dialogRender()` to log `dialog->state`, `dialog->statefrac`, `dialog->type`, `dialog->transitionfrac` for the endscreen dialog
- Add `sysLogPrintf` in `textRender()` to log the g_ScaleX value when rendering endscreen text
- Check if `viSetMode()` is called between the two renders

---

## 8. Files That Call Into Each Other (Adjacency List)

```
mplayer.c
  → menu.c (func0f0f820c)
  → menutick.c (via prevmenuroot flag)

menutick.c
  → ingame.c (mpPushEndscreenDialog)
  → endscreen.c (endscreenPushCoop, endscreenPushAnti)
  → menugfx.c (menugfxFreeParticles)

ingame.c
  → menu.c (menuPushRootDialog)

menu.c
  → menugfx.c (menugfxRenderDialogBackground, menugfxDrawDialogBorderLine, etc.)
  → game_1531a0.c (textRender, textSetWaveColours, textResetBlends)
  → menuitem.c (renderMenuItem)
  → vi.c (viewport setup)

menugfx.c
  → game_1531a0.c (uses g_ScaleX via gbiex.h macros)
  → vi.c (g_ViBackData for framebuffer ops)

game_1531a0.c (textRender)
  → GBI display list → gfx_pc.cpp (import_texture_ci4 → palette_to_rgba32)
  → uses g_ScaleX for coordinate scaling

gfx_pc.cpp
  → gfx_opengl.cpp (upload_texture, draw calls)
  → gfx_sdl2.cpp (window dimensions)

pdgui_backend.cpp
  → gfx_opengl.cpp (gfx_opengl_reset_for_overlay)
  → pdgui_style.cpp (pdguiApplyPdStyle, pdguiRenderAllWindowShimmers)
  → pdgui_debugmenu.cpp (pdguiDebugMenuRender)
  [ISOLATED from game menu rendering — only active when F12 toggled]
```
