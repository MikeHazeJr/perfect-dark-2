# D5.0 — ImGui Menu Visual Layer: Revised Implementation Plan

> **Status**: PLANNED (replaces the D5.0 section in d5-ui-polish-plan.md)
> **Last updated**: 2026-04-06 (S156 investigation)
> **Prerequisite**: D5.0a technical spike — **DONE (S135)**
> **Depends on**: texInit() init ordering fix (see Phase 1 below)

---

## Executive Summary

The D5.0 visual layer gives every ImGui menu the authentic PD look: tinted translucent
panels, animated shimmer borders, palette-driven coloring, optional ROM texture overlays,
scanline CRT effect, and mod-overridable themes. This plan is based on a deep investigation
of what already exists vs. what needs to be built.

**Key finding**: ~70% of D5.0 is already implemented. The theme module (`pdgui_theme.cpp`),
style module (`pdgui_style.cpp`), and texture decode pipeline all exist and work. What
remains is: (1) fixing the init ordering so ROM textures actually decode at startup,
(2) expanding texture registrations beyond the 2 currently registered, (3) wiring theme
draw functions into menu renderers, and (4) adding mod theme override support.

---

## What Already Exists

### pdgui_theme.cpp / pdgui_theme.h (D5.0 — BUILT)

Full N64 texture decode pipeline:
- `PdTexConfig` struct mirroring `struct textureconfig` from types.h
- Decoders for RGBA16, IA16, IA8, IA4, CI8, CI4 (all N64 UI formats)
- `s_uploadGLTex()` → GL texture from RGBA32 buffer
- `s_decodeAndUpload()` → full decode + upload pipeline
- `s_registerTexConfig()` → decode, upload, register in asset catalog as ASSET_UI
- `pdguiThemeGetTexture(catalog_id)` → cached GL texture lookup
- Draw functions: `pdguiThemeDrawPanel`, `pdguiThemeDrawBorder`, `pdguiThemeDrawHeader`,
  `pdguiThemeDrawButton`, `pdguiThemeDrawStars`, `pdguiThemeDrawScanline`
- Blue palette hardcoded (k_PalBlue[15])

**References**: `port/fast3d/pdgui_theme.cpp:1-742`, `port/include/pdgui_theme.h:1-128`

### pdgui_style.cpp / pdgui_style.h (BUILT)

Complete PD-authentic style system:
- 7 color palettes: Grey(0), Blue(1), Red(2), Green(3), White(4), Silver(5), BlackGold(6)
- `pdguiDrawPdDialog()` — full dialog frame: title gradient, body fill, borders, shimmer
- `pdguiRenderAllWindowShimmers()` — per-frame perimeter shimmer on all active windows
- `pdguiDrawButtonEdgeGlow()` — animated focus glow on hovered buttons
- `pdguiDrawTextGlow()` — multi-layer soft glow behind text
- Theme state: `s_Theme` struct with palette index, tint strength/color, text glow, sound pack
- `pdguiThemeSetPalette()`, `pdguiThemeSetTint()`, `pdguiThemeSetTextGlow()`, `pdguiThemeSetSoundPack()`
- `PdColor()` converter: PD 0xRRGGBBAA → ImGui IM_COL32

**References**: `port/fast3d/pdgui_style.cpp:43-895`, `port/include/pdgui_style.h:1-83`

### pdgui_backend.cpp (BUILT)

- `pdguiGetUiTexture(id)` → delegates to `pdguiThemeGetTexture()` (line 108-111)
- `pdguiThemeInit()` called during `pdguiInit()` (line 232)
- `pdguiThemeShutdown()` called during `pdguiShutdown()` (line 543)
- Handel Gothic font loaded as embedded C array
- Resolution-independent scaling via pdgui_scaling.h

**References**: `port/fast3d/pdgui_backend.cpp:108-111, 232, 543`

### ROM Texture System (GAME CODE — READ-ONLY)

- `texInit()` in `src/game/texinit.c:9` — loads texture list segment from ROM via DMA
- `texReset()` in `src/game/texreset.c:45-96` — populates all 20 texture config groups
  from `src/textureconfig.c` ROM data tables via `bcopy()` to `MEMPOOL_STAGE` memory
- `g_TexGeneralConfigs` — 56 entries (textureconfig.c:263-320):
  - [1]: 1x1 RGBA32 solid pixel (color fills)
  - [6]: 64x64 IA8 green haze background (menugfxRenderBgGreenHaze)
  - [9, 12-29]: 56x36 RGBA16 briefing mission images
  - [34-36]: Small RGBA16/IA8 menu icons
  - [37+]: Various menu UI textures (32x32 to 64x64)
- `g_TexScreenConfigs` — 96 entries (loading bars, HUD elements, in-game screens)
- All configs store `texnum` (ROM index) initially; `texptr` only valid after GBI decode

### OG Menu Rendering (GAME CODE — READ-ONLY)

- `menugfxRenderDialogBackground()` (`src/game/menugfx.c:172-203`):
  Body fill via `gDPFillRectangleScaled` + 3 border lines via `menugfxDrawDialogBorderLine()`
  with shimmer. Colors from `g_MenuColours[dialog->type]` with `colourBlend()` transitions.
- `menugfxRenderBgGreenHaze()` (`menugfx.c:211-326`): Dual rotating IA8 layers at ~50% alpha
- `menugfxDrawDialogBorderLine()` (`menugfx.c:1106`): Line + shimmer per border edge
- `menugfxRenderGradient()` (`menugfx.c:669`): 3-color vertical gradient (title bars)

### Mod Texture System (BUILT)

- `modTextureLoad()` (`port/src/mod.c:38-89`): Two-tier — catalog override → legacy `textures/XXXX.bin`
- `catalogResolveTexture(texnum)` (`port/src/assetcatalog_load.c:197-223`): O(1) reverse-index
  lookup via `s_TexnumOverride[texnum]` array
- `CatalogResolveResult`: `{path, catalog_id, is_mod_override}` — mod file vs ROM routing
- Scanner: `assetcatalog_scanner.c` auto-discovers mod components, registers with source_texnum
- Modpack hot-registration on `.pdpack` import — no restart required

---

## What Needs to Be Built / Fixed

### Problem 1: Init Ordering (CRITICAL)

**Current state**: `pdguiInit()` runs at `port/src/main.c:167`, but `texInit()` runs later
inside `pdmainInit()` at `port/src/pdmain.c:358`. When `pdguiThemeInit()` fires,
`g_TexGeneralConfigs` is NULL → the assert fires and no ROM textures are decoded.

**Evidence**: The log message "g_TexGeneralConfigs is NULL -- texInit() has not been called"
appears at startup. `pdguiThemeInit()` returns early, `s_ThemeTexCache` stays empty.

**Root cause**: The PC port bootstrap order is:
1. `main()` → `videoInit()` → `pdguiInit()` → `pdguiThemeInit()` ← HERE (too early)
2. ... later ... `romdataInit()` → game bootstrap → `pdmainInit()` → `texInit()` → `texReset()`

**Fix**: Defer ROM texture decode to a lazy-init call after texInit() has run.

### Problem 2: Only 2 Textures Registered

`pdguiThemeInit()` currently registers only `base:ui_bg_haze` (index 6) and
`base:ui_particles` (index 1). Many more ROM UI textures are available but unregistered.

### Problem 3: texnum vs texptr

All `g_TexGeneralConfigs` entries store a `texnum` (small ROM index integer), not a memory
pointer. The decoders expect `texptr` to be a valid heap pointer. The entries only become
real pointers after the GBI pipeline processes the texture during a render frame — but theme
textures need to be available before any menu renders.

**Solution**: We need to use the ROM data loading path to get raw pixel data for each texnum,
then decode ourselves. This means calling the texture decompression system or DMA-loading
the raw data from the ROM texture table.

### Problem 4: Theme Draw Functions Not Wired Into Menus

`pdguiThemeDrawPanel()`, `pdguiThemeDrawBorder()`, etc. exist but no menu renderer calls
them yet. All current ImGui menus use `pdguiDrawPdDialog()` from pdgui_style.cpp (which
does solid-color rendering without ROM textures) or plain ImGui styling.

### Problem 5: No Mod Theme Override

The palette system is hardcoded. There's no mechanism for mods to provide custom palettes,
tint colors, or replacement UI textures through the catalog.

---

## Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                     Menu Renderer (e.g. pdgui_menu_room.cpp)     │
│                                                                   │
│  pdguiThemeDrawPanel(x,y,w,h, "base:ui_bg_haze")                │
│  pdguiThemeDrawHeader(x,y,w,h, "Room Setup", -1)                │
│  pdguiThemeDrawBorder(x,y,w,h, -1)                               │
│  pdguiThemeDrawScanline(0,0,screenW,screenH, 0.8f)              │
└────────────────────────┬────────────────────────────────────────┘
                         │ calls
┌────────────────────────▼────────────────────────────────────────┐
│  pdgui_theme.cpp — Theme Draw Layer                              │
│                                                                   │
│  pdguiThemeGetTexture("base:ui_bg_haze")                         │
│    → s_ThemeTexCache["base:ui_bg_haze"] → GLuint                 │
│    → (ImTextureID)(uintptr_t)gl_id                               │
│                                                                   │
│  ImGui::GetWindowDrawList()->AddImage(texId, ...)                │
│  ImGui::GetWindowDrawList()->AddRectFilled(...)                  │
│  ImGui::GetWindowDrawList()->AddRectFilledMultiColor(...)        │
└────────────────────────┬────────────────────────────────────────┘
                         │ texture lookup
┌────────────────────────▼────────────────────────────────────────┐
│  Texture Pipeline                                                 │
│                                                                   │
│  texInit() → texReset() → g_TexGeneralConfigs populated          │
│  pdguiThemeLateInit() → for each registered texconfig:           │
│    s_isRealPtr(cfg)?  YES → decodeN64Tex*() → s_uploadGLTex()   │
│                       NO  → texLoadRawData(texnum) → decode → GL │
│                                                                   │
│  Result: s_ThemeTexCache["base:ui_bg_haze"] = GLuint 42          │
└────────────────────────┬────────────────────────────────────────┘
                         │ mod override (future)
┌────────────────────────▼────────────────────────────────────────┐
│  Asset Catalog + Mod System                                       │
│                                                                   │
│  catalogResolveTexture(texnum) → CatalogResolveResult            │
│    is_mod_override=1? → load from filesystem path                │
│    is_mod_override=0? → decode from ROM (base game)              │
│                                                                   │
│  Mod theme.ini: palette overrides, custom UI textures            │
└─────────────────────────────────────────────────────────────────┘
```

---

## Implementation Phases

### Phase 1: Fix Init Ordering + Deferred Decode (~60 LOC)

**Goal**: ROM textures actually decode and become GL textures at startup.

**Approach**: Split `pdguiThemeInit()` into two stages:
1. **Early init** (during `pdguiInit()`): Set `s_ThemeInitDone = true`, log intent. No decode.
2. **Late init** `pdguiThemeLateInit()`: Called after `texInit()` has run. Decodes ROM textures.

**Where to call late init**: Add a hook in `pdmainInit()` after `texInit()`:
```c
// port/src/pdmain.c, after texInit() at line 358:
texInit();
pdguiThemeLateInit();  // NEW: decode ROM UI textures now that texconfigs are populated
```

**texnum → raw data**: The textureconfigs from `g_TcGeneralConfigs` (src/textureconfig.c)
store texnum indices. After `texReset()` copies them into `g_TexGeneralConfigs`, the entries
still hold texnums (not pointers). We need to convert texnum → raw pixel data.

**Option A (recommended)**: Use `texLoadFromTextureList(texnum)` or equivalent ROM load
function to get raw data for each texnum, then decode with our existing decoders.

**Option B**: Hook into the GBI pipeline's texture cache in gfx_pc.cpp — when a UI texture
is first rendered by the OG renderer, capture the decoded RGBA data and upload to our GL cache.
This avoids duplicating the decode logic but requires the OG renderer to render first.

**Option C (simplest, no ROM dependency)**: Generate procedural textures that approximate
the OG look (noise patterns for haze, gradient for scanlines). Not authentic but works
immediately.

**Decision needed from game director**: Option A gives authentic ROM textures but requires
understanding the ROM texture load path. Option B piggybacks on existing decode but has
timing issues. Option C ships fastest but isn't faithful.

**Files**:
| File | Change |
|------|--------|
| `port/fast3d/pdgui_theme.cpp` | Split init into early + late; add `pdguiThemeLateInit()` |
| `port/include/pdgui_theme.h` | Declare `pdguiThemeLateInit()` |
| `port/src/pdmain.c:~359` | Call `pdguiThemeLateInit()` after `texInit()` |

### Phase 2: Expand Texture Registrations (~40 LOC)

**Goal**: Register all useful ROM UI textures, not just 2.

**Textures to register** (from `g_TexGeneralConfigs`, cross-ref textureconfig.c:263-320):

| Catalog ID | Config Index | Size | Format | OG Usage |
|------------|-------------|------|--------|----------|
| `base:ui_bg_haze` | [6] | 64x64 | IA8 | Green haze background (menugfxRenderBgGreenHaze) |
| `base:ui_particles` | [1] | 1x1 | RGBA32 | Solid pixel (color fills, shimmer) |
| `base:ui_grad_bar` | [3] | 2x8 | IA8 | Gradient bar element |
| `base:ui_mirror_tile` | [4] | 8x8 | IA8 | Mirror-tiled UI element |
| `base:ui_noise_sm` | [0] | 16x16 | IA8 | Small noise/grain texture |
| `base:ui_noise_lg` | [2] | 16x16 | IA8 | Large noise/grain texture (different texnum) |
| `base:ui_dot_tile` | [7] | 8x8 | IA8 | Small tiled dot pattern |
| `base:ui_nuke` | [10] | 64x64 | IA8 | Nuke detonation effect |
| `base:ui_bg_alt` | [11] | 64x64 | IA8 | Alternative background texture |
| `base:ui_icon_a` | [34] | 14x14 | RGBA16 | Menu icon A |
| `base:ui_icon_b` | [35] | 11x11 | IA8 | Menu icon B |
| `base:ui_icon_c` | [36] | 14x14 | RGBA16 | Menu icon C |
| `base:ui_deco` | [37] | 32x32 | RGBA16 | Decorative texture |

**Briefing images** (indices 9, 12-29): 18 mission briefing images at 56x36 RGBA16.
Register as `base:ui_briefing/{stage_short}` — e.g. `base:ui_briefing/ame`, `base:ui_briefing/crad`.
Map index → stage abbreviation using the stage table.

**Files**:
| File | Change |
|------|--------|
| `port/fast3d/pdgui_theme.cpp` | Add registrations in `pdguiThemeLateInit()` |

### Phase 3: Unify Theme Draw Path (~80 LOC)

**Goal**: `pdguiDrawPdDialog()` in pdgui_style.cpp and `pdguiThemeDraw*()` in pdgui_theme.cpp
should use a single code path. Currently they're parallel implementations.

**Current duplication**:
- `pdguiDrawPdDialog()` (style.cpp:376-475) — full dialog rendering with shimmer, palette
- `pdguiThemeDrawPanel()` (theme.cpp:552-574) — body fill + optional texture overlay
- `pdguiThemeDrawBorder()` (theme.cpp:586-611) — border lines (no shimmer)
- `pdguiThemeDrawHeader()` (theme.cpp:621-652) — title gradient (no shimmer)

**Approach**: Enhance `pdguiDrawPdDialog()` to optionally composite a ROM texture in the
body fill region. The theme draw functions become lower-level building blocks that
`pdguiDrawPdDialog()` can call internally.

```cpp
// Enhanced pdguiDrawPdDialog signature (or add a variant):
void pdguiDrawPdDialog(float x, float y, float w, float h,
                       const char *title, int focused);
// Internally: after body fill, calls pdguiThemeDrawPanel() overlay
// if a background texture is set in the theme config
```

**Theme config** drives which texture is composited:
```cpp
void pdguiThemeSetBackgroundTexture(const char *catalog_id);  // e.g. "base:ui_bg_haze"
```

When set, `pdguiDrawPdDialog()` adds an `AddImage()` call at low opacity over the body fill.
When NULL, pure solid color (current behavior). This is the hook point for mods.

**Also**: Make `pdguiThemeDrawBorder()` call `pdguiDrawShimmerExact()` from style.cpp
to get animated shimmer on borders. Currently theme borders are static lines only.

**Files**:
| File | Change |
|------|--------|
| `port/fast3d/pdgui_style.cpp` | Integrate ROM texture overlay into `pdguiDrawPdDialog()` |
| `port/fast3d/pdgui_theme.cpp` | Add `pdguiThemeSetBackgroundTexture()` |
| `port/include/pdgui_theme.h` | Declare new config function |
| `port/include/pdgui_style.h` | No change needed (API stable) |

### Phase 4: Scanline Pass Integration (~20 LOC)

**Goal**: Subtle CRT scanline overlay on all menu screens.

**Current state**: `pdguiThemeDrawScanline()` exists in pdgui_theme.cpp:724-741, draws
horizontal lines at 2px intervals. Not called from anywhere.

**Integration point**: `pdgui_backend.cpp`, in the render function after all ImGui windows
are submitted but before `ImGui::Render()`. Use the foreground draw list so scanlines
render on top of all content.

```cpp
// In pdguiRender(), after pdguiRenderAllWindowShimmers():
if (pdguiIsActive() && pdguiThemeGetScanlineEnabled()) {
    ImDrawList *fg = ImGui::GetForegroundDrawList();
    // pdguiThemeDrawScanline uses its own draw list — need to adapt to use fg
    pdguiThemeDrawScanlineFg(fg, 0, 0, screenW, screenH, 0.8f);
}
```

**Scanline opacity**: Configurable via `pdguiThemeSetScanlineAlpha(float alpha)`.
Default 0.8 (~13% darkening). Setting 0.0 disables scanlines. Mod-overridable.

**Files**:
| File | Change |
|------|--------|
| `port/fast3d/pdgui_backend.cpp` | Call scanline pass in render loop |
| `port/fast3d/pdgui_theme.cpp` | Add `pdguiThemeDrawScanlineFg()` variant that takes a draw list |
| `port/include/pdgui_theme.h` | Declare scanline config functions |

### Phase 5: Multi-Palette Support in Theme (~40 LOC)

**Goal**: `pdguiThemeDraw*()` functions respect the active palette from pdgui_style.cpp.

**Current state**: pdgui_theme.cpp has a hardcoded `k_PalBlue[15]` and `s_activePal()` always
returns it. Meanwhile pdgui_style.cpp has 7 full palettes and `pdguiGetPalette()` API.

**Fix**: Replace `k_PalBlue` in theme.cpp with a call to the style palette API. The theme
draw functions should delegate to `pdguiGetPalette()` + read from the active palette struct.

**Approach**: Export a `pdguiGetActivePaletteColors()` function from style.cpp that returns
a pointer to the active palette's 15-field color array. Theme.cpp calls it instead of
using its own hardcoded copy.

**Files**:
| File | Change |
|------|--------|
| `port/fast3d/pdgui_style.cpp` | Export `pdguiGetActivePaletteColors()` |
| `port/fast3d/pdgui_theme.cpp` | Replace `k_PalBlue`/`s_activePal()` with style API call |
| `port/include/pdgui_style.h` | Declare new function |

### Phase 6: Mod Theme Override (~100 LOC, FUTURE)

**Goal**: Mods can override the visual theme: custom palettes, UI textures, tint colors.

**Mechanism**: A mod component of type `ui` can include a `theme.ini` file:

```ini
[theme]
palette = custom           ; use custom palette instead of Blue
tint_strength = 0.3
tint_color = 0xFF200080    ; purple tint
scanline_alpha = 0.5
text_glow_intensity = 0.8
text_glow_color = 0xFF00FFFF

[palette_custom]
dialog_border1 = 0xFF400080
dialog_titlebg = 0x200030FF
dialog_border2 = 0xFF8000FF
; ... all 15 palette fields
```

**Texture overrides**: A mod with a `ui/` component category can provide replacement textures
that shadow base catalog IDs. The existing catalog override mechanism
(`catalogResolveTexture()` → `is_mod_override=1`) handles this automatically if the mod
registers entries with matching `source_texnum` values.

**For custom UI textures** (not ROM replacements): Mod provides PNG/BIN files that are
loaded via `stb_image` or raw RGBA32, uploaded to GL, and registered in `s_ThemeTexCache`
with custom catalog IDs.

**Files**:
| File | Change |
|------|--------|
| `port/fast3d/pdgui_theme.cpp` | INI parser for theme.ini, custom palette loading |
| `port/src/assetcatalog_scanner.c` | Recognize `ui` category components |
| `port/include/pdgui_theme.h` | Declare mod theme loading API |

---

## Tinting Architecture

### How the OG Game Does It

The OG PD menu tinting is entirely palette-driven. There are no "tint shaders" — the
`g_MenuColours[dialog->type]` palette selects which colors are used for every element.
The `dialog->type` field (0-5) maps to a palette, and `colourBlend()` interpolates between
palettes during transitions. All visual variety comes from palette choice.

The green haze background (`g_TexGeneralConfigs[6]`) is an IA (intensity-alpha) texture
rendered with a green `gDPSetPrimColor` tint. The intensity channel provides the pattern,
the primitive color provides the hue. This is the core N64 tinting technique: IA textures
+ primitive color = any color.

### How Our ImGui System Does It

We have two complementary approaches:

1. **Palette coloring** (existing, pdgui_style.cpp): `pdguiSetPalette(index)` → all ImGui
   style colors update immediately. `pdguiDrawPdDialog()` uses palette colors for all fills,
   borders, and text. This matches the OG approach.

2. **Tint overlay** (existing API, pdgui_style.cpp): `pdguiThemeSetTint(strength, color)` →
   additional color burn/tint applied on top of palette colors. Strength 0.0 = pure palette,
   1.0 = full tint. This extends beyond what the OG game did.

3. **Texture tinting** (to build in Phase 3): When rendering IA textures via `AddImage()`,
   use the tint_col parameter to colorize them. IA textures are greyscale — the tint color
   replaces the primitive color from the N64 pipeline:
   ```cpp
   dl->AddImage(texId, min, max, uv0, uv1,
                IM_COL32(tintR, tintG, tintB, 80));  // tint + alpha
   ```
   The IA texture's intensity modulates the tint color's brightness, matching N64 behavior.

### Theme Tinting Flow

```
Mod theme.ini (optional)
    ↓ loads
pdguiThemeSetPalette(index)  → palette colors for all UI elements
pdguiThemeSetTint(0.3, purple) → additional color burn on body fills
    ↓ applied in
pdguiDrawPdDialog() → body fill = palette bodybg, blended with tint
                    → optional texture overlay with tint_col
                    → borders = palette border1/border2
                    → shimmer = palette accent colors
```

---

## Border Textures

### OG Approach

The OG PD does NOT use texture-based borders. Borders are 1-pixel-wide filled rectangles
(`gDPFillRectangleScaled`) with solid palette colors + animated shimmer overlay
(`menugfxDrawShimmer`). The shimmer is a white highlight that travels the perimeter.

### Our Approach

Match the OG: solid-color borders + shimmer. Already implemented in `pdguiDrawPdDialog()`
(style.cpp:420-475). The perimeter-aware shimmer calculates a single position traveling
clockwise around left → bottom → right edges.

**Not adding texture-based borders** — there are no border textures in the ROM, and the
solid + shimmer approach is authentic. If mods want textured borders in the future, they
can provide RGBA textures registered as `base:ui_border_*` catalog entries and the theme
system can optionally render them via AddImage on the border regions.

---

## Implementation Schedule

| Phase | Description | LOC | Depends On |
|-------|-------------|-----|------------|
| **1** | Fix init ordering + deferred decode | ~60 | Nothing |
| **2** | Expand texture registrations | ~40 | Phase 1 |
| **3** | Unify theme draw path (texture overlay in dialogs) | ~80 | Phases 1-2 |
| **4** | Scanline pass integration | ~20 | Phase 3 |
| **5** | Multi-palette support in theme functions | ~40 | Phase 3 |
| **6** | Mod theme override (FUTURE) | ~100 | Phases 1-5 |
| **Total** | | **~340** | |

Phases 1-5 are a single work session. Phase 6 is deferred until after D5.3+.

---

## Key Decisions Needed from Game Director

1. **ROM texture loading method** (Phase 1): Use texLoadFromTextureList to DMA raw
   pixel data from ROM, or hook into GBI pipeline's texture cache, or generate procedural
   approximations? Recommendation: try the texLoad path first, fall back to procedural if
   ROM data format is too complex.

2. **Background texture default**: Should all dialog panels show the haze texture overlay
   by default (OG-authentic), or should it be opt-in per menu? Recommendation: default ON
   for all dialogs at 10-15% opacity, matching the OG's dual-layer haze.

3. **Scanline default**: On by default at 80% (subtle ~13% darkening), or off by default?
   Recommendation: ON by default, with a Settings toggle.

---

## File Reference

| File | Phases | Existing LOC | Status |
|------|--------|-------------|--------|
| `port/fast3d/pdgui_theme.cpp` | 1,2,3,4,5,6 | 742 | MODIFY |
| `port/include/pdgui_theme.h` | 1,4,5,6 | 128 | MODIFY |
| `port/fast3d/pdgui_style.cpp` | 3,5 | ~900 | MODIFY |
| `port/include/pdgui_style.h` | 5 | 83 | MODIFY |
| `port/fast3d/pdgui_backend.cpp` | 4 | ~550 | MODIFY |
| `port/src/pdmain.c` | 1 | ~400 | MODIFY (1 line) |
| `port/src/assetcatalog_scanner.c` | 6 | ~540 | MODIFY (future) |
