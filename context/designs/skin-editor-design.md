# Skin Editor — Design Document

> **Created**: 2026-04-11
> **Purpose**: Design for an in-game character skin editor with layer-based compositing, controller-friendly painting, live 3D model preview, image import, UV remap, PD-style downrez, and save-to-catalog-as-mod.
> **Status**: DESIGN — no implementation code yet. Phase 2 sessions implement from this doc.
> **Companion docs**: [menu-replacement-plan.md](menu-replacement-plan.md), [component-mod-architecture.md](../component-mod-architecture.md)

---

## 1. Reference Survey

### 1.1 Aseprite — Layer/Blend Compositor

Aseprite composites layers bottom-to-top using per-pixel blend functions with signature `color_t blender(backdrop, src, opacity)`. Supports 19 blend modes including Normal, Multiply, Screen, Color Burn, Color Dodge, HSL Hue, HSL Saturation, HSL Color, HSL Luminosity. The `Render::renderSpriteLayers()` method iterates the layer stack, calling `composite_image()` for each with its blend mode. Flattening composites all visible layers onto a single destination buffer using the same per-pixel blender.

**Key insight**: The `blender(backdrop, src, opacity)` signature is trivially portable. Each blend mode is a pure function on two RGBA values. For PD skins at N64 resolution, we only need Normal, Multiply, Screen, and Hue at launch.

**Sources**: [aseprite/src/doc/blend_funcs.cpp](https://github.com/aseprite/aseprite/blob/main/src/doc/blend_funcs.cpp) (MIT), [aseprite/src/doc/blend_mode.h](https://github.com/aseprite/aseprite/blob/main/src/doc/blend_mode.h). License: blend_funcs.cpp is MIT.

### 1.2 Krita — Blend Mode Formulas

Krita documents blend modes with per-channel RGB formulas. Color Burn: `1 - ((1 - base) / blend)`. Color Dodge: `base / (1 - blend)`. For HSX modes, Krita provides HSI/HSV/HSL/HSY variants. The **Hue** blend mode takes only Hue from the top layer, preserving Saturation and Luminosity from below. The **Saturation** blend mode takes only Saturation from top.

**Key insight**: Hue blend mode is the most useful for skin editing — it lets players recolor an outfit while preserving shading. Requires only RGB-to-HSL conversion, component swap, and HSL-to-RGB.

**Sources**: [Krita blend mode docs](https://docs.krita.org/en/reference_manual/blending_modes/hsx.html). License: CC-BY-SA (docs), GPL (Krita).

### 1.3 libimagequant — Color Quantization for PD-style Downrez

libimagequant converts 32-bit RGBA images to palette-indexed 8-bit images. API: (1) create `liq_attr`, (2) `liq_set_max_colors(16)` for CI4 or 256 for CI8, (3) `liq_image_create_rgba(attr, bitmap, w, h, gamma)`, (4) `liq_image_quantize()`, (5) `liq_write_remapped_image()`. Dithering controlled via `liq_set_dithering_level(0.0–1.0)`.

**Key insight**: For PD-style CI4 textures, `liq_set_max_colors(16)` quantizes the 32-bit canvas to a 16-color palette matching the N64 format. Built-in dithering handles the downrez gracefully.

**Sources**: [libimagequant](https://github.com/ImageOptim/libimagequant), [pngquant.org/lib](https://pngquant.org/lib/). License: Dual GPL v3 / commercial. **Note**: commercial license required for non-GPL projects. Alternative: implement our own median-cut quantizer (~200 lines C) to avoid GPL.

### 1.4 Floyd-Steinberg / Bayer Dithering

**Floyd-Steinberg**: Error-diffusion dithering. Per-pixel: quantize to nearest palette color, distribute error to 4 neighbors (7/16 right, 3/16 below-left, 5/16 below, 1/16 below-right). Smooth results, "worm" artifacts in gradients.

**Bayer/ordered dithering**: Fixed 4x4 threshold matrix tiled across the image. Produces characteristic crosshatch pattern — deterministic, no error propagation. More authentically "N64/retro" because it matches real N64 hardware texture compression artifacts.

**Key insight**: Bayer dithering is the default for "Convert to PD Style" — it looks more period-appropriate. Floyd-Steinberg offered as a "Smooth" option. Both are trivial to implement (~30 lines each).

**Sources**: [Wikipedia: Floyd-Steinberg](https://en.wikipedia.org/wiki/Floyd%E2%80%93Steinberg_dithering), [Wikipedia: Ordered dithering](https://en.wikipedia.org/wiki/Ordered_dithering).

### 1.5 Splatoon 2/3 Controller Painting UI

Splatoon's post editor uses gyro for fine-tuning + stick for coarse cursor movement. In docked mode without touchscreen, cursor control relies on analog stick with gyro-assisted fine adjustment.

**Key insight**: Gyro is the gold standard for controller-based precision, but PD2 targets desktop (mouse + gamepad) — gyro is niche on PC gamepads. For PD2: mouse is the primary painting input; stick painting is secondary with snap-to-pixel at N64 resolution.

### 1.6 Dreams PS4 — Dual-Stick Creator Controls

Dreams uses an "imp" cursor controlled by motion controls or dual sticks. The left stick controls XY position, right stick handles depth/zoom. Complex creation is viable with a gamepad but motion controls significantly improve precision.

**Key insight**: For small textures (32x64), stick control with snap-to-pixel and zoom is sufficient — motion controls are overkill at that resolution. Cursor speed should scale with zoom level.

### 1.7 Mario Maker 2 — Controller Painting

Mario Maker 2 uses a grid-snapped cursor controlled by left analog stick or D-pad. Tool selection uses shoulder buttons and face buttons.

**Key insight**: Grid-snapped cursor directly applicable. PD skin textures are low-res (e.g. 32x64), so snapping to pixel boundaries eliminates precision issues entirely. D-pad for single-pixel movement, stick for fast traversal.

### 1.8 Blender Texture Paint — Input Mapping

Blender's Texture Paint provides: Draw, Soften, Smear, Clone, Fill. Brush radius via F+drag, strength via Shift+F+drag. Color picker via eyedropper. Fill has a configurable threshold.

**Key insight**: For a gamepad skin editor, the essential four tools are Draw, Fill, Eyedropper, and Erase. Brush size mapped to triggers. Skip Smear/Clone for v1.

---

## 2. How PD2 Works Today — Grounded Analysis

### 2.1 Character Body/Head Texture Pipeline

- **Model loading**: `menuRenderModel()` in `src/game/menu.c` renders character models using the N64 GBI display list pipeline. The model's texture is embedded in the ROM data file loaded via `fileLoadToNew()`.
- **Texture binding**: The GBI display list calls `gDPSetTextureImage` / `gSPLoadBlock` etc. to bind textures. The fast3d renderer (`port/fast3d/gfx_pc.cpp`) intercepts these commands and uploads the texture data to OpenGL via `glTexImage2D`.
- **Texture override**: The catalog-load system (`port/src/assetcatalog_load.c`) has a reverse-index that maps `source_filenum` and `source_texnum` to mod file paths. When `r.is_mod_override` is true, the mod file's texture is loaded instead of the ROM texture.
- **No runtime texture replacement API today**: There is no function like `textureReplace(texId, newPixels)` or `glTexSubImage2D` call exposed to game code. The texture override only works at load time (when a model is first loaded from ROM).

### 2.2 ASSET_SKIN in Catalog

- **Enum value**: `ASSET_SKIN` = index 3 in `asset_type_e` (`port/include/assetcatalog.h:55`).
- **Ext union**: `ext.skin.target_id[CATALOG_ID_LEN]` — soft reference to the target character (e.g., `"base:joanna_dark"`).
- **Registration**: `assetCatalogRegisterSkin(id, target_id)` exists (`port/include/assetcatalog.h:398`) but is NEVER called anywhere. Zero skin entries in the catalog today.
- **Query API**: `assetCatalogGetSkinsForTarget(target_id, out, maxout)` exists (`port/include/assetcatalog.h:603`) — iterates catalog for ASSET_SKIN entries matching a target character. Returns count.
- **Scanner support**: `assetcatalog_scanner.c:208` maps `"skins"` directory → `ASSET_SKIN`. Line 338 parses `skin.ini` with `target` field.

### 2.3 Live 3D Preview Infrastructure

- **FBO**: 256x256 offscreen framebuffer created by `pdguiCharPreviewInit()` in `port/fast3d/pdgui_charpreview.c:64`. Fixed size, not tied to window.
- **Request API**: `pdguiCharPreviewRequest(head_id, body_id)` resolves catalog IDs to runtime indices, sets `g_Menus[playernum].menumodel.newparams`.
- **Render hook**: `pdguiCharPreviewRenderGBI()` in `pdgui_charpreview.c:332` switches render target to preview FBO, calls `menuRenderModel()`, switches back.
- **ImGui widget**: `pdguiModelPreviewDraw(head_id, body_id, x, y, w, h, opts)` in `port/fast3d/pdgui_model_preview.cpp:284` — handles change detection, idle rotation, palette-derived styling, UV-flipped sampling.
- **Bake API**: `pdguiCharPreviewBakeToTexture()` at `pdgui_charpreview.c:267` reads FBO pixels via `glGetTexImage`, creates a new standalone GL texture from those pixels. This is used for thumbnails.
- **Key gap**: No way to inject a custom texture override into the preview render. The model uses whatever texture is baked into its ROM data (or the mod override at load time).

### 2.4 Input Context Stack

- **Existing contexts**: `g_CtxGameplay`, `g_CtxImGuiMenu`, `g_CtxPauseMenu`, `g_CtxDebugOverlay` in `port/src/inputctx.c`.
- **IMCs**: `g_ImcGameplay` (priority 0), `g_ImcVehicle` (5), `g_ImcMenu` (10), `g_ImcPauseMenu` (11), `g_ImcDebugOverlay` (20), `g_ImcTextInput` in `port/src/actionmap.cpp`.
- **Analog access**: `actionValue(player, ACTION_AXIS_MOVE_X/Y)` returns -1.0 to 1.0 for left stick. `actionValue(player, ACTION_AXIS_AIM_X/Y)` for right stick / mouse.
- **Cross-contamination fix**: S208 `break;` fix in `actionmapLoadBinds()` — first-match-wins. Adding a new IMC with shared actions would risk re-breaking this. **Must not add actions to existing IMCs.**

### 2.5 Theme Editor Save Pattern

`saveThemeAsMod()` in `pdgui_menu_theme_editor.cpp:119` demonstrates the create-mod-from-editor workflow: sanitize name → `fsCreateDir("mods/<slug>")` → write JSON → call `pdguiThemeRegisterModDir()` for immediate catalog availability.

### 2.6 Mod Component Save Pattern

A skin mod on disk would look like:
```
mods/my-skin/
  skin.ini
  texture.tga    (or .png)
```

**skin.ini**:
```ini
[skin]
type = skin
name = Blue Joanna
target = base:joanna_dark
```

The scanner at `assetcatalog_scanner.c:338` handles this format already.

---

## 3. Concrete Design Proposal

### 3.1 Skin Editor Architecture Overview

```
+--[Skin Editor Window]----------------------------------------------+
|                                                                     |
| +---[2D Canvas]--------+ +---[3D Preview]----+ +--[Tool Panel]---+ |
| | UV-unwrapped texture | | Live model with   | | Color picker    | |
| | Direct pixel editing | | current skin      | | Brush size      | |
| | Layer indicators     | | Idle rotation     | | Tool select     | |
| | Grid overlay         | |                   | | Layer list      | |
| |                      | |                   | | Blend mode      | |
| |                      | |                   | | Opacity         | |
| +----------------------+ +-------------------+ +-----------------+ |
|                                                                     |
| [Import Image] [Export PNG] [Save as Mod] [Convert to PD Style]     |
+---------------------------------------------------------------------+
```

**Three-panel layout**: 2D canvas (left, ~40% width), 3D preview (center, ~30%), tool panel (right, ~30%).

### 3.2 Canvas System — In-Memory Texture Editing

The canvas is a stack of RGBA32 pixel buffers:

```
New file: port/fast3d/pdgui_skin_canvas.cpp

struct SkinLayer {
    u8 *pixels;              // RGBA32 pixel data (w * h * 4)
    char name[32];           // "Base", "Layer 1", etc.
    f32 opacity;             // 0.0-1.0
    s32 blend_mode;          // BLEND_NORMAL, BLEND_MULTIPLY, etc.
    s32 visible;             // 0/1
    s32 locked;              // 0/1 (base layer locked by default)
};

struct SkinCanvas {
    SkinLayer layers[MAX_SKIN_LAYERS]; // MAX = 8
    s32 num_layers;
    s32 active_layer;
    s32 width, height;       // Texture dimensions (e.g. 32x64)
    u8 *composite;           // Flattened RGBA32 result
    u32 gl_texture;          // GL texture for ImGui display + preview
    s32 dirty;               // Flag: recomposite + upload needed
};
```

**Compositing**: Bottom-to-top layer iteration. For each pixel:
```c
rgba_t composite_pixel(rgba_t backdrop, rgba_t src, s32 blend_mode, f32 opacity) {
    rgba_t blended;
    switch (blend_mode) {
    case BLEND_NORMAL:    blended = src; break;
    case BLEND_MULTIPLY:  blended.r = (backdrop.r * src.r) / 255; ... break;
    case BLEND_SCREEN:    blended.r = 255 - ((255-backdrop.r)*(255-src.r))/255; ... break;
    case BLEND_HUE:       /* RGB->HSL, swap H, HSL->RGB */ break;
    case BLEND_BURN:      blended.r = clamp(255 - (255-backdrop.r)*255/(src.r+1)); break;
    case BLEND_SATURATION: /* RGB->HSL, swap S, HSL->RGB */ break;
    }
    // Alpha blend with opacity
    f32 a = (src.a / 255.0f) * opacity;
    result.r = (u8)(backdrop.r * (1-a) + blended.r * a);
    ...
}
```

**GL texture upload**: When `dirty` is set, recomposite all layers into `composite`, then `glTexSubImage2D(gl_texture, ...)` to update the GL texture. This texture is used both for the 2D canvas display in ImGui and for the live 3D preview override.

**Self-critique**: At PD texture sizes (32x64 = 2048 pixels), compositing is trivially fast — even 8 layers at 60fps is ~130K pixel ops/frame. No performance concern.

### 3.3 Live 3D Preview with Skin Override

**Problem**: The existing `pdguiCharPreviewRenderGBI()` renders the character with its ROM texture. We need to inject the editor's canvas texture instead.

**Proposed mechanism**: Add a texture override hook in the fast3d renderer.

```
New API in pdgui_charpreview.h:
  void pdguiCharPreviewSetSkinOverride(u32 glTexId, s32 texWidth, s32 texHeight);
  void pdguiCharPreviewClearSkinOverride(void);
```

**Implementation**: In `pdguiCharPreviewRenderGBI()`, after switching to the preview FBO and before calling `menuRenderModel()`, set a global flag that the fast3d GBI translator checks. When the flag is set and the texture being loaded matches the body's texture slot, substitute the override GL texture.

```
In gfx_pc.cpp (or gfx_opengl.cpp):
  // During texture image processing for the preview FBO:
  if (s_SkinOverrideActive && current_fb == preview_fb) {
      // Bind the editor's canvas texture instead of the ROM texture
      glBindTexture(GL_TEXTURE_2D, s_SkinOverrideTexId);
      return;
  }
```

**Self-critique**: This is the most architecturally invasive part. It touches the fast3d renderer's texture pipeline. Alternative: render the 3D model normally, then in a second pass use a shader to replace the body texture region based on UV coordinates. The shader approach is cleaner but requires writing a custom GL shader. **Recommend the override approach for v1** — it's simpler and the override flag is scoped to the preview FBO render, so it cannot affect normal game rendering.

### 3.4 Tool System

**Core tools** (v1):

| Tool | Description | Keyboard | Controller |
|------|-------------|----------|------------|
| **Draw** | Paint single pixels or brush strokes | Left click | A button + cursor |
| **Fill** | Flood-fill contiguous same-color region | F key | X button |
| **Eyedropper** | Pick color from canvas | Alt+click | Y button |
| **Erase** | Set pixels to transparent | E key / Right click | B button (hold) + cursor |
| **Line** | Draw straight line between two points | L key, click-click | LT + A (start) → move → A (end) |

**Brush size**: 1-8 pixels. Controlled by `[` / `]` keys or RT (hold) + D-pad Up/Down.

**Future tools** (v2+): Rectangle select, gradient fill, smudge.

### 3.5 Color Picker

**Layout**: HSV color wheel (hue ring + saturation/value triangle) in the tool panel. Below it: recent colors row (last 8 used), palette row (current texture's extracted palette).

**Controller interaction**: When the color picker has focus, left stick moves the selector within the wheel/triangle. A to confirm color. LB/RB to cycle between Hue ring and SV triangle.

### 3.6 Image Import + UV Remap

**Import flow**:
1. User specifies image path (text input or file browser)
2. Load image via `stbi_load()` (stb_image already vendored? check — if not, add as public-domain single-header)
3. Display import preview with source image on left, target UV layout on right
4. User can choose:
   - **Stretch to fit**: Scale source to target texture dimensions
   - **UV remap**: User selects UV regions on the source to map to body parts
   - **Direct overlay**: Place source as a new layer at cursor position

**UV remap feature**: The editor shows the UV layout of the body model as a wireframe overlay on the 2D canvas. This helps users understand which pixels map to which body parts. The UV data is extracted from the model's vertex data at load time.

```
New function:
  void skinEditorExtractUVs(const char *body_id, f32 *uv_out, s32 *num_verts);
  // Reads the body model's vertex UVs and stores them for wireframe overlay
```

**Self-critique**: UV extraction from the N64 model format is non-trivial — the vertex data is in the GBI display list, not in a simple vertex buffer. The model's UV coordinates are packed in `Vtx` structs (s16 tc[2] values). We'd need to walk the model's display list and extract vertex positions + UVs. This is feasible but requires understanding the model node tree structure. Could defer UV remap to v2 and ship v1 with "stretch to fit" only.

### 3.7 PD-Style Downrez ("Convert to PD Style")

When the user clicks "Convert to PD Style", the editor:

1. **Flatten** all layers to a single RGBA32 buffer
2. **Quantize** to 16 colors (CI4 format) or 256 colors (CI8) using median-cut:
   - Build color histogram
   - Recursively split along the axis with largest range
   - Each leaf = one palette entry
   - Remap each pixel to nearest palette color
3. **Dither** using Bayer 4x4 (default) or Floyd-Steinberg (option):
   - Bayer: per-pixel threshold comparison against the 4x4 matrix
   - Floyd-Steinberg: error diffusion to 4 neighbors
4. **Preview** the result in a split view (before/after) before committing
5. If user confirms, replace the canvas content with the quantized result

**Implementation**: ~200 lines of C for median-cut + dithering. No external dependency needed — avoids libimagequant's GPL license issue.

```
New file: port/fast3d/pdgui_skin_quantize.cpp (~200 lines)

void skinQuantize(const u8 *src, u8 *dst, s32 w, s32 h,
                  s32 max_colors, s32 dither_mode);
// dither_mode: 0 = none, 1 = Bayer 4x4, 2 = Floyd-Steinberg
```

**Self-critique**: Median-cut at 16 colors on a 32x64 image is trivial performance-wise. The real question is visual quality — Bayer dithering on very small textures can look noisy. Recommend providing a "No dither" option for users who prefer flat-shaded looks.

### 3.8 Save as Mod

When the user clicks "Save as Mod":

1. Prompt for mod name + display name
2. Flatten canvas to RGBA32
3. Write texture as TGA to `mods/<slug>/texture.tga` (TGA writer is trivial — 18-byte header + raw pixels)
4. Write `skin.ini`:
   ```ini
   [skin]
   type = skin
   name = Blue Joanna
   target = base:joanna_dark
   ```
5. Register in catalog: `assetCatalogRegisterSkin(id, target_id)` + set `dirpath`, `enabled = 1`
6. Call `catalogRefreshMods()` or direct registration (same pattern as theme editor)

**File format choice**: TGA is the simplest format to write (no compression, just header + pixels), and the existing base-ui mod already uses TGA textures (`mods/base-ui/textures/ui_bg_haze.tga`). This is the established convention.

---

## 4. Controller Input Model

### 4.1 Input Context Design

The skin editor uses `g_CtxImGuiMenu` (the standard menu context). **No new IMC.** All tool switching is handled within the ImGui renderer via ImGuiKey checks, not through the action map system. This avoids any risk of cross-contamination with the just-fixed input system.

### 4.2 Controller Layout — Option A: "Paint Mode" (Recommended)

This layout optimizes for painting — the most common action in the editor.

```
+---[Left Side]---+     +---[Right Side]---+
|                  |     |                  |
| L Stick: Cursor  |     | R Stick: Zoom/  |
|   movement       |     |   Pan canvas    |
|                  |     |                  |
| D-pad: Pixel-    |     |                  |
|   precise cursor |     |                  |
|                  |     |                  |
| LB: Prev tool    |     | RB: Next tool   |
| LT: Hold for     |     | RT: Hold for    |
|   brush size     |     |   line mode     |
|   (+ D-pad U/D)  |     |   (start→end)  |
+------------------+     +------------------+

+---[Face Buttons]---+
| Y: Eyedropper     |
| X: Fill tool       |
| A: Draw / Confirm  |
| B: Erase / Back    |
+--------------------+

Start: Save as Mod dialog
Select/Back: Undo (last stroke)
```

**Rationale**: A is the primary painting button (draw), consistent with A = USE across the game. B = erase maintains the "B = cancel/undo" mental model. LB/RB for tool cycling matches the tab-cycling convention used everywhere else (ACTION_MENU_TAB_PREV/NEXT). D-pad for pixel-precise movement is borrowed from Mario Maker 2's proven grid-snap approach.

### 4.3 Controller Layout — Option B: "Mode Switch"

This layout separates navigation from painting via a mode toggle.

```
MODE: NAVIGATE (default)                    MODE: PAINT (toggle with LT)
  L Stick: Pan canvas                         L Stick: Move cursor
  R Stick: Zoom                               R Stick: Not used
  D-pad: Select tool / layer                  D-pad: Pixel-precise cursor
  A: Enter paint mode / Confirm               A: Draw pixel
  B: Back / Close editor                      B: Erase pixel
  X: Color picker                             X: Fill
  Y: Undo                                     Y: Eyedropper

  LT: Hold = paint mode                       LT: Release = back to navigate
  RT: Not used                                RT: Hold = brush size + D-pad
  LB/RB: Cycle layers                         LB/RB: Cycle tools
```

**Rationale**: Separating navigation from painting avoids accidental strokes while panning. The downside is the mode toggle adds cognitive overhead. Not recommended for PD's audience.

### 4.4 Recommendation

**Option A** is recommended. The single-mode approach is simpler and more intuitive. PD skin textures are small enough (32x64 max) that accidental strokes while navigating are unlikely — the canvas is fully visible without panning at most zoom levels. R Stick for zoom/pan handles the rare need to navigate a zoomed-in view.

### 4.5 Mouse + Keyboard (primary)

| Action | Input |
|--------|-------|
| Draw | Left click + drag |
| Erase | Right click + drag |
| Eyedropper | Alt + click |
| Fill | F + click |
| Line | L + click (start) → click (end) |
| Pan canvas | Middle click + drag |
| Zoom | Scroll wheel |
| Brush size | `[` / `]` |
| Undo | Ctrl+Z |
| Redo | Ctrl+Y |
| Tool cycle | 1-5 number keys |
| Color picker | C (opens/focuses) |
| Save | Ctrl+S |

### 4.6 Mode Transitions (Input Context Flow)

```
Main Menu
  → Modding Hub (pushes g_CtxImGuiMenu)
    → Skin Editor tab (no additional push — same context)
      → Save dialog (ImGui popup — no context change)
      → Import dialog (ImGui popup — no context change)
    → Close editor (no pop — still in modding hub)
  → Close modding hub (pops g_CtxImGuiMenu)
```

No input context push/pop within the skin editor. All tool switching is ImGui-internal. This is the safest approach given the recent cross-contamination fix.

---

## 5. Phased Implementation Plan

### Batch S-1: Canvas System + 2D Editor (foundation)
**Scope**: In-memory canvas with layers, compositing, GL texture upload, basic ImGui 2D view with pixel grid, Draw + Erase tools, color picker.
**Files touched**: NEW `port/fast3d/pdgui_skin_canvas.cpp` (~400 lines), NEW `port/fast3d/pdgui_skin_editor.cpp` (~600 lines), NEW `port/include/pdgui_skin_editor.h` (~30 lines)
**Dependencies**: None
**Network**: No wire changes
**Build impact**: Client +25KB, server unchanged
**Acceptance**: Open editor from Modding Hub. Create a new blank canvas. Draw colored pixels. Add/remove layers. Compositing produces correct result.

### Batch S-2: Live 3D Preview with Skin Override
**Scope**: Add `pdguiCharPreviewSetSkinOverride()` API. Wire canvas GL texture into preview FBO render. Character model shows editor's canvas texture in real-time.
**Files touched**: `port/fast3d/pdgui_charpreview.c` (+30 — override state + hook in RenderGBI), `port/include/pdgui_charpreview.h` (+5 — new API decls), `port/fast3d/gfx_pc.cpp` or `port/fast3d/gfx_opengl.cpp` (+20 — texture substitution during preview FBO render), `port/fast3d/pdgui_skin_editor.cpp` (+30 — wire preview)
**Dependencies**: Batch S-1
**Network**: No wire changes
**Build impact**: Client +2KB
**Acceptance**: Editing a pixel on the 2D canvas immediately updates the 3D model preview. Idle rotation works with the custom skin.

### Batch S-3: Fill + Line + Brush Size Tools
**Scope**: Flood-fill tool, line drawing tool, variable brush size (1-8px). Undo/redo stack (ring buffer of canvas snapshots).
**Files touched**: `port/fast3d/pdgui_skin_editor.cpp` (+200 — tool implementations + undo stack)
**Dependencies**: Batch S-1
**Network**: No wire changes
**Build impact**: Client +5KB
**Acceptance**: Fill floods contiguous regions correctly. Line draws between two points. Brush size changes visible. Ctrl+Z undoes last stroke.

### Batch S-4: Save as Mod + Catalog Registration
**Scope**: "Save as Mod" dialog. Flatten canvas → TGA file. Write skin.ini. Register in catalog. Skin appears in character selection.
**Files touched**: `port/fast3d/pdgui_skin_editor.cpp` (+150 — save dialog, TGA writer, catalog registration), `port/fast3d/pdgui_bridge.c` (+20 — skin catalog bridge accessors)
**Dependencies**: Batch S-1, S-2
**Network**: Skin catalog ID stored in matchslot as body_id/head_id (existing field). When a skin mod overrides a body texture, the body's catalog ID stays the same — the skin is a visual-only overlay. No new wire fields needed.
**Build impact**: Client +4KB
**Acceptance**: Save creates a valid mod directory under `mods/`. Skin appears in Modding Hub INI editor. Character using the skin body shows the custom texture in-game.

### Batch S-5: Image Import + Stretch-to-Fit
**Scope**: Import an image (PNG/TGA/BMP) as a new layer. Scale to canvas dimensions. Requires stb_image (single-header, public domain, if not already vendored).
**Files touched**: `port/fast3d/pdgui_skin_editor.cpp` (+100 — import dialog, stbi_load, scale), possibly NEW `port/external/stb_image.h` (vendored)
**Dependencies**: Batch S-1
**Network**: No wire changes
**Build impact**: Client +30KB (stb_image if newly added)
**Acceptance**: Import a PNG file. It appears as a new layer scaled to canvas dimensions.

### Batch S-6: PD-Style Downrez (Quantize + Dither)
**Scope**: "Convert to PD Style" button. Median-cut quantization to 16/256 colors. Bayer + Floyd-Steinberg dithering. Before/after preview.
**Files touched**: NEW `port/fast3d/pdgui_skin_quantize.cpp` (~200 lines), `port/fast3d/pdgui_skin_editor.cpp` (+50 — downrez dialog)
**Dependencies**: Batch S-1
**Network**: No wire changes
**Build impact**: Client +5KB
**Acceptance**: "Convert to PD Style" with 16 colors + Bayer dithering produces a visually retro result. Before/after preview works.

### Batch S-7: Blend Mode Extensions
**Scope**: Add Multiply, Screen, Hue, Burn, Saturation blend modes to the compositor. Layer blend mode dropdown in tool panel.
**Files touched**: `port/fast3d/pdgui_skin_canvas.cpp` (+100 — blend mode implementations)
**Dependencies**: Batch S-1
**Network**: No wire changes
**Build impact**: Client +2KB
**Acceptance**: Hue blend mode recolors a body texture while preserving shading.

### Batch S-8: UV Remap + Advanced Import (v2)
**Scope**: Extract UV wireframe from body model. Display UV overlay on 2D canvas. Import with UV-aware region mapping. Requires walking the GBI display list to extract vertex UVs.
**Files touched**: NEW `port/fast3d/pdgui_skin_uv.cpp` (~300 lines), `port/fast3d/pdgui_skin_editor.cpp` (+100)
**Dependencies**: Batch S-1, S-5
**Network**: No wire changes
**Build impact**: Client +8KB
**Acceptance**: UV wireframe visible on 2D canvas. User can see which pixels map to which body parts.

---

## 6. Open Questions for Mike

### 6.1 Edit-in-Memory vs Save-on-Disk
Does the existing mod system write eagerly (every change hits disk) or on-save? The skin editor needs to edit in-memory (the canvas is a pixel buffer) and only commit to disk on explicit "Save as Mod". This means the catalog might temporarily have a skin entry that doesn't have a disk-backed texture file yet. **Proposed**: The canvas is purely in-memory until save. The catalog entry is only created at save time — no half-done skins pollute the catalog. The 3D preview uses the GL texture directly, not the catalog texture path.

### 6.2 Live Texture Replacement at Runtime
Does the catalog texture path support live texture replacement at runtime? Specifically: if I overwrite `mods/my-skin/texture.tga` on disk while the game is running, will the model update? Today the answer is no — textures are loaded once at `fileLoadToNew()` time. The skin editor bypasses this by injecting the canvas GL texture directly into the preview FBO render. But for the saved skin to appear on the actual in-game character (not just the preview), we'd need either: (A) a texture reload mechanism, or (B) return to title screen after saving (like `modmgrApplyChanges()` does today). **Recommend option B for v1.**

### 6.3 Skin-to-Body Mapping
A skin mod targets a specific body (via `ext.skin.target_id`). But bodies have different texture layouts (some use one texture file, some use multiple). How does the game decide which texture file to replace when a skin is applied? The design assumes each body has one primary texture file, and the skin replaces it entirely. Is that correct, or do some bodies have multi-file textures that would need separate skin layers?

### 6.4 stb_image Availability
Is `stb_image.h` already vendored somewhere in the repo? If not, it's a single public-domain header file that adds PNG/TGA/BMP/JPG loading. Needed for image import in Batch S-5.

### 6.5 Texture Resolution
What are the actual texture dimensions for PD character bodies? The design assumes small (32x64 or 64x64 based on N64 era). The exact dimensions determine canvas size and whether pixel-level controller painting is practical.

### 6.6 Skin Application in Multiplayer
When a player uses a custom skin, should other players see it? Options:
- **Local-only**: Only the player sees their own skin. No network sync needed. Simplest.
- **Network-synced**: All players download and see the skin. Requires texture transfer protocol. Very complex.
**Recommend local-only for v1.** Network skin sync is a v2+ feature that requires a content distribution mechanism.

---

## 7. Data Structures Summary

### New Files
| File | Purpose | Lines (est.) |
|------|---------|-------------|
| `port/fast3d/pdgui_skin_canvas.cpp` | Canvas system, layer compositing, GL upload | ~400 |
| `port/fast3d/pdgui_skin_editor.cpp` | Editor UI, tools, import/save | ~800 |
| `port/include/pdgui_skin_editor.h` | Public API for skin editor | ~30 |
| `port/fast3d/pdgui_skin_quantize.cpp` | Median-cut quantizer + dithering | ~200 |
| `port/fast3d/pdgui_skin_uv.cpp` (v2) | UV extraction from GBI models | ~300 |

### Modified Files
| File | Change | Lines (est.) |
|------|--------|-------------|
| `port/fast3d/pdgui_charpreview.c` | Skin override hook | +30 |
| `port/include/pdgui_charpreview.h` | SetSkinOverride/ClearSkinOverride decls | +5 |
| `port/fast3d/gfx_pc.cpp` or `gfx_opengl.cpp` | Texture substitution during preview render | +20 |
| `port/fast3d/pdgui_menu_moddinghub.cpp` | Skin Editor tab (tab index 4) | +50 |
| `port/fast3d/pdgui_bridge.c` | Skin catalog bridge accessors | +20 |
| `port/include/pdgui_menus.h` | Registration forward decl | +2 |

### No Changes To
- `pdgui_menu_solomission.cpp` (standing rule)
- `port/src/actionmap.cpp` (no new actions/IMCs — avoids cross-contamination)
- `port/src/inputctx.c` (no new input contexts)
- `port/src/net/*` (no wire changes for v1 — skins are local-only)
- Build files (CMake GLOB_RECURSE auto-discovers new .c/.cpp)
