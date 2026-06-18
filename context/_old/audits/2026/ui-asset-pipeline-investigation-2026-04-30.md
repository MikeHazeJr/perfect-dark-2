# UI Asset Extraction + .pdui Pipeline Investigation (2026-04-30)

> Phase 1 (research) plus Phase 2 (schema + implementation plan) for the UI asset extraction track. Triggered by `context/audits/rom-extraction-audit-2026-04-30.md` Section 3.10 G-5 (procedural chrome bumped to high severity) plus 3.18 priority item 6 (investigate ROM addresses for OG chrome art).

Methodology gates per `context/procedures.md`: possibility framing, file:line evidence, no em-dashes, LOUDFAIL discipline. Truncation discipline: this doc is authored in chunks; each chunk has a sentinel marker for verification.

---

## Executive summary

**Investigation surfaced one finding that fundamentally reshapes the work scope:**

The ROM extraction audit's Section 3.10 G-5 ("procedural chrome generator is incomplete implementation, not aspirational") and Section 3.18 priority item 6 ("investigate ROM addresses for PD's dialog chrome source art") are based on a false premise. **There is no source texture for PD's dialog chrome in the ROM, on N64 or anywhere else.**

PD's N64 dialog chrome (`menugfxRenderDialogBackground` at `src/game/menugfx.c:187-218`) is rendered procedurally by the engine using:

1. `gDPFillRectangleScaled` for the body fill (palette colour `dialog_bodybg`).
2. `menugfxDrawDialogBorderLine` for the three border edges (left, right, bottom, plus title bar drawn separately) using `dialog_border1` and `dialog_border2` palette colours.
3. `menugfxDrawShimmer` overlaid on each border (`menugfx.c:1012-1119`), drawing a white-on-clear gradient quad whose position cycles through the border every ~3.3 seconds, computed each frame from the global `g_20SecIntervalFrac`.

The PC port's procedural chrome generator at `pdgui_theme.cpp:2856-2980` is structurally a correct port of this approach, with one notable artistic divergence: it composites the result into a 64x64 nineslice texture at startup and reuses it, rather than recomputing per-frame. The "TODO: Replace with real ROM texture extraction" comment at `:2845` should be retired and replaced with documentation of the actual mechanism.

**The audit's broader directive (extract real ROM data; bundle into `.pdui`; wire through catalog; loud-fail on errors) remains valid and is the right shape for the work.** What changes is the specific scope of what gets extracted: the 14 already-extracted textures plus the palette tables plus the animation parameter set, NOT a non-existent chrome source asset.

**Important correction added 2026-04-30 (post-Mike-empirical-observation):** the procedural chrome the PC port renders today is visibly inadequate ("static and blue, no animation, looks nothing like OG except color theme"). My initial assessment that it "faithfully reconstructs" the OG mechanism was wrong; that assessment was based on reading code in isolation, not on observing the rendered output. A second pass traced the gap precisely (Section 1.2 / 1.5): the port has the building blocks but assembles them into a quieter render than the OG (perimeter walker instead of four independent edge shimmers, no item focus pulse, static haze instead of OG-equivalent animation, no backdrop animations). Mike's observation reshapes Section 5 Q-A: the question is now "what is the minimum work to make the in-game chrome actually look like OG, animated?" rather than "what should we do with the chrome ROM-extraction directive?" Recommended order: a "Stage 0" renderer fix lands BEFORE Stage A bundling, so the visual baseline matches OG before we package it.

**Proposed pipeline (this audit Phase 2):**

1. **Extraction.** Continue extracting the 14 IA8 / RGBA16 / RGBA32 textures the renderer references. Add the 6 menu colour palettes from `g_MenuColours[]` and the animation parameter table (frequencies and amplitudes) as data. Output: `data/ui/pd-original.pdui` (single ZIP archive).
2. **Schema.** New `.pdui` compound mod format conforming to the audit's Section 3.2 / 3.3 / 3.16.0 architecture. Bakes Mike's 8 extensibility rules from the start.
3. **Catalog wiring.** Catalog discovers `.pdui` files in `data/`, `base/`, and `mods/`. One canonical loader. Registers the textures as `ASSET_UI` entries (already an asset type; see `port/include/assetcatalog_types.h`). Registers palettes and animation params as new sub-asset types or as theme-block fields.
4. **Render wiring.** `port/fast3d/pdgui_style.cpp` consumes texture references, palette colours, and animation parameter table from the active `.pdui` instead of hard-coded literals. The N64 `menugfxRenderBgGreenHaze` recipe (counter-rotating two-layer haze) is also surfaced as a theme-selectable backdrop variant.
5. **LOUDFAIL.** Extraction errors, schema violations, and missing-asset cascades route through the `LOUDFAIL.EXTRACT.*` / `LOUDFAIL.PROC.*` / `LOUDFAIL.CATALOG.*` channels per audit Section 3.8.

**Open questions for Mike before implementation starts:** see Section 6 at the end of this doc.

<!-- SENTINEL: Executive summary end -->

---

## 1. Phase 1 findings

### 1.1 What's actually extractable from the ROM

**Source array**: `g_TcGeneralConfigs[]` at `src/textureconfig.c:263-320`. **56 entries** total (indices 0..55). Each row is `{ texturenum (u32), w, h, level, fmt, depth, s, t }`.

Today the runtime extractor at `port/fast3d/pdgui_theme.cpp:2419` hard-codes 14 indices. Those 14 plus their ROM origins (verified by walking the texture pipeline at `src/game/texdecompress.c:2188-2262`):

| Idx | Catalog id | texnum | W x H | Format | Wrap | Notes |
|-----|------------|--------|-------|--------|------|-------|
| 0   | base:ui_noise_sm    | 0x001b | 16x16 | IA8   | CLAMP  | Small noise tile |
| 1   | base:ui_particles   | 0x0001 | 1x1   | RGBA32| WRAP   | Solid fill, used by particles in success endscreen |
| 2   | base:ui_noise_lg    | 0x0c97 | 16x16 | IA8   | CLAMP  | Large noise tile |
| 3   | base:ui_grad_bar    | 0x001c | 2x8   | IA8   | CLAMP  | Gradient bar |
| 4   | base:ui_mirror_tile | 0x001d | 8x8   | IA8   | MIRROR | Mirror tile |
| 6   | base:ui_bg_haze     | 0x01e5 | 64x64 | IA8   | WRAP   | The haze backdrop for credits + success endscreen |
| 7   | base:ui_dot_tile    | 0x0c98 | 8x8   | IA8   | WRAP   | Dot pattern |
| 10  | base:ui_nuke        | 0x063b | 64x64 | IA8   | WRAP   | N-bomb dome (in-game, not menu) |
| 11  | base:ui_bg_alt      | 0x0c9a | 64x64 | IA8   | WRAP   | Alternate background |
| 34  | base:ui_icon_a      | 0x0858 | 14x14 | RGBA16| CLAMP  | Icon A |
| 35  | base:ui_icon_b      | 0x084e | 11x11 | IA8   | CLAMP  | Icon B |
| 36  | base:ui_icon_c      | 0x08f4 | 14x14 | RGBA16| CLAMP  | Icon C |
| 37  | base:ui_deco        | 0x060a | 32x32 | RGBA16| WRAP   | Decoration |
| 38  | base:ui_stars       | 0x0c9b | 64x64 | IA8   | WRAP   | Credits stars background |

Indices 5, 8, 9, 12-33, 39-55 are also UI / menu textures and are not currently extracted. The gap is non-trivial: `menu.c:4118-4137` preloads indices 1, 6, 51, 52, 53, 54, 55 plus 34, 35, 36 for the file-select and MP-setup screens, and `menuitem.c:4185-4201` uses index 55 for the mission-line UV-step animation.

**The texture data path** (verified at `src/game/texdecompress.c:2188-2262`):
1. `texLoadFromConfig(cfg)` resolves `cfg->texturenum` to an offset in `g_Textures[]` (the directory table in the `textureslist` segment, NTSC-final ROM offset `0x1ff7ca0`).
2. DMAs `(thisoffset, nextoffset - thisoffset)` bytes from the `texturesdata` segment (NTSC-final ROM offset `0x1d65f40`).
3. Header byte is `u-z-llllll`: `z` = zlib-compressed flag, `l` = LOD count. Inflates via `rzipInflate` if compressed.
4. Stitches the resulting raw N64-format byte buffer into the union, replacing the texturenum.

**The build-time extractor at `tools/extract:251-264`** (`extract_textures`) already dumps every entry of `g_Textures[]` to `src/assets/<romid>/textures/<hex>.bin` as raw N64-format blobs. The catalog at `src/assets/<romid>/textures.json` lists every entry with `{id, file, flag00, surfacetype}`. So the source-side data already exists on disk; the runtime extractor just doesn't consult it.

**Critical implication**: extending UI texture coverage beyond the 14-index hardcoded list requires only iterating more rows of `g_TcGeneralConfigs` (and optionally the 17 sibling tables `g_TcWallhitConfigs`, `g_TcShieldConfigs`, etc. at `src/textureconfig.c:74-322` for HUD / weapon FX / radar / etc). All width / height / format metadata is already in memory after `texReset()`. No ROM-spelunking, no address-table research is required.

### 1.2 What's NOT in the ROM (the chrome correction)

The audit's Section 3.10 G-5 and Section 3.18 priority item 6 assume there are extractable ROM addresses for PD's dialog chrome source art. There are not.

`src/game/menugfx.c:187-218` (`menugfxRenderDialogBackground`) confirms: no texture is sampled. Sequence is:

```
gdl = textSetPrimColour(gdl, colour1);              // colour1 = pal->dialog_bodybg
gDPFillRectangleScaled(gdl++, x1, y1, x2, y2);      // solid body fill
gdl = menugfxDrawDialogBorderLine(...);             // right border
gdl = menugfxDrawDialogBorderLine(...);             // left border
gdl = menugfxDrawDialogBorderLine(...);             // bottom border
// title bar drawn separately
```

`menugfxDrawDialogBorderLine` at `src/game/menugfx.c:1124` is itself a thin call site that draws the line geometry then layers `menugfxDrawShimmer` on top.

`menugfxDrawShimmer` at `src/game/menugfx.c:1012-1119` is the canonical PD chrome animation. Per-frame, no source texture, pure CPU math:

```c
if (reverse) v0 = 6.0f * g_20SecIntervalFrac * 600.0f;
else         v0 = (1.0f - g_20SecIntervalFrac) * 6.0f * 600.0f;
if (y2 - y1 < x2 - x1) {                  // horizontal edge
    v0 += (u32)(y1 + x1);                 // phase offset
    v0 %= 600;                            // 600px modulo
    shimmerleft  = x1 + v0 - arg7;        // arg7 = 10 for borders, 40 for title
    shimmerright = shimmerleft + arg7;
}
// draws a white gradient quad via menugfxDrawTri2
```

`freq = 6` cycles per 20 seconds = a single shimmer crossing each border every ~3.33 seconds. The `(y1 + x1)` term de-syncs the four borders so they pulse at staggered phases. Border colour is `g_MenuColours[type].dialog_border1` and `dialog_border2`, blended with the optional second palette type via `dialog->colourweight`.

**CORRECTION (per Mike's empirical observation 2026-04-30):** my initial assessment that the PC port "faithfully reconstructs" the OG chrome was wrong. Mike, looking at the actual rendered output, reports the menus "look nothing alike except in color theme" and the background is "static and blue, no animation." A second pass of the code traces the gap precisely:

The PC port has the building blocks (palette colors, shimmer math) but assembles them into a visibly inadequate result. Specifically:

1. **`s_generateChromeFrameBgra` (`pdgui_theme.cpp:2856-2980`) is dead code on the default code path.** It bakes a static nineslice ONLY when chrome is enabled. Default `s_CfgUiChromeEnabled = 0` (`pdgui_theme.cpp:1445`), so the bake-once-at-startup nineslice does not render in default config.
2. **Default body fill (`pdgui_style.cpp:691-694`) is a solid `dialog_bodybg` rect.** The blue palette's `dialog_bodybg` is `0x00002f9f` (deep navy at 62% alpha). Static. Matches OG body fill mechanism.
3. **Default haze overlay (`pdgui_style.cpp:706-727`) is a static-UV tile.** Tinted `IM_COL32(0, 80, 0, 32)` (alpha 32 = ~12.5%). The texture coordinates are computed once from dialog dimensions; never animated. The OG `menugfxRenderBgGreenHaze` recipe (counter-rotating two-layer haze with alpha fade and zoom envelope) is NOT what the PC port draws. The port draws a static tinted tile that the OG never showed in dialogs at all.
4. **Body border shimmer is replaced with a perimeter walker (`pdgui_style.cpp:740-840`).** One 20px-wide white rectangle travels the full perimeter every 5 seconds. The OG had FOUR independent shimmers per dialog (one per edge, each 3.33s cycle, full edge length). The port's perimeter walker is significantly less dense and visually quieter than the OG four-edge variant. `pdguiDrawShimmerExact` (the four-edge math) exists in the file at `:352-472` but is only called for the title bar (`:606-608`), not for the body borders.
5. **Title bar shimmer uses `pdguiDrawShimmerExact`** (`:606-608`). Time-base `ImGui::GetTime() / 20`. This DOES animate per frame.
6. **Item focus pulse is not implemented.** `pdguiDrawItemHighlight` (`pdgui_style.cpp:858-863`) draws a solid `item_focused_outer` rectangle. The OG ran every focused row through `menuGetSinOscFrac(40)` blending `item_focused_inner` with `item_focused_outer` at 2 Hz. The breathing pulse on the selected row is one of the strongest PD identity cues; its absence makes selection feel inert.
7. **Backdrop animations are not implemented.** `menugfxRenderBgGreenHaze` / `BgFailure` / `BgCone` / `BgSuccess` (the screens-specific animated backdrops) have no port equivalents.

**Net:** every OG animation that gave PD's UI its identity is either missing (item pulse, backdrops, four-edge shimmer) or simplified to a much quieter variant (perimeter walker instead of four-edge). The body itself was static in OG too, so that part is matched. Mike's "static and blue" assessment is accurate description of what a user sees today.

**The shimmer codepath is reached for title bar only.** For body, the lower-density perimeter walker runs. The shimmer phase IS updating (`ImGui::GetTime()` always advances), but on a small dialog the perimeter walker takes 5 seconds to traverse (slower than OG's 3.33s/edge cycle), and only one slim rectangle is in flight at a time, so it can read as "no animation" without a careful look.

The N64-side `g_20SecIntervalFrac` global is irrelevant in the port; menu render no longer goes through `menugfx.c` (per `context/constraints.md` "ImGui is the sole menu system"). The port's animation clock is `ImGui::GetTime()` end-to-end. So whether `g_20SecIntervalFrac` ticks or not has no bearing on what Mike sees on screen.

**Earlier (now-retired) text in this section claimed:** "the PC port faithfully reconstructs this, with two divergences." That claim was based on reading the code in isolation, not on observing the rendered output. Reverted.

**For reference**, the shimmer math in the port (verified to be running per frame):

```cpp
int v0;
if (reverse) v0 = (int)(6.0f * frac * (float)edgeLen);
else         v0 = (int)((1.0f - frac) * 6.0f * (float)edgeLen);
v0 += (int)((int)y1 + (int)x1);
v0 %= edgeLen;
```

The PC port uses `edgeLen` instead of N64's hardcoded 600px modulo (a correctness fix for higher resolutions). Width is `width * 2` and alpha is boosted 1.8x to compensate for the higher pixel density at modern resolutions (the original was subtle at 240p). However this math is currently invoked ONLY for the title bar; body borders use the simpler perimeter walker described above.

**Revised conclusion**: the chrome IS "procedurally rendered both in OG and in our port, with the same math" at the conceptual level (palette colors plus per-frame shimmer math, no source texture). But the port's assembly of those building blocks omits the most identifiable PD UI animations. The audit's "find ROM addresses for chrome" premise is correctly retired, but a separate gap remains: the procedural chrome IS visually inadequate, not because there's missing source art, but because the port has not surfaced the OG's four-edge body shimmer, item focus pulse, or backdrop animations onto the active render path. The minimum work to actually look like OG is enumerated in Section 1.5.

<!-- SENTINEL: Section 1.2 end -->

### 1.3 Animation mechanics: formulas, not data

PD's UI animations are **all** functions of one global: `g_20SecIntervalFrac` (`src/game/game_006900.c:18`), a `f32` ramping 0..1 every 20 seconds, ticked in `menuTickTimers()`. Three derived helpers:

```c
// src/game/game_006900.c:94-126
f32 menuGetSinOscFrac(f32 freq) { return sinf((freq * g_20SecIntervalFrac * 2) * M_PI) / 2 + 0.5; }
f32 menuGetCosOscFrac(f32 freq) { return cosf((freq * g_20SecIntervalFrac * 2) * M_PI) / 2 + 0.5; }
f32 menuGetLinearIntervalFrac(f32 freq) { return frac(g_20SecIntervalFrac * freq); }
```

`freq` = oscillations per 20 seconds. So `freq = 20` is 1 Hz pulse; `freq = 4` is 5-second period; `freq = 6` is the shimmer cadence (~3.33s sweep across each border).

**Inventory of every UI animation in PD's menu code:**

| Animation | Source | Formula | Notes |
|-----------|--------|---------|-------|
| Border / title shimmer | `menugfx.c:1012-1119` | `freq = 6` linear sweep, edge-phase-offset | White comet across each border |
| Filled rect shimmer | `menugfx.c:1133` | same | Used on filled accent strips |
| Item focus pulse (small) | `menuitem.c:464,873,...` | `menuGetSinOscFrac(40)` = 2 Hz | Vertex-colour blend between item_focused_inner and item_focused_outer |
| Item focus pulse (large) | `menuitem.c:873,2498` | `menuGetSinOscFrac(20)` = 1 Hz | Slower variant |
| Chevron / arrow grow | `menu.c:3330,5976` | `menuGetSinOscFrac(20)` and `(10)` | Cursor / arrow size pulse |
| Mission-list 4-frame UV step | `menuitem.c:4185-4201` | `(s32)(-g_20SecIntervalFrac * 4 * 50) % 4` | UV `s` stepped by 4 over 20s; uses g_TexGeneralConfigs[55] |
| Bg haze (counter-rotating) | `menugfxRenderBgGreenHaze` (`menugfx.c:226-341`) | `f26 = M_BADTAU * g_20SecIntervalFrac`; layer 1 spins opposite + phase 0.5 offset; alpha fade-in at 0..0.2 and fade-out at 0.9..1 | Two layers of `ui_bg_haze` (idx 6) at counter-rotating UVs; comment at :222 says "unused" but the recipe is canonical |
| Bg failure (rotating fans) | `menugfxRenderBgFailure` (`menugfx.c:1334-1437`) | `spb4 = M_TAU * g_20SecIntervalFrac`; `menuGetCosOscFrac(4)` for vertex alpha | Solo mission failure backdrop |
| Bg cone (combat sim) | `menugfxRenderBgCone` (`menugfx.c:1445-1516`) | `M_TAU * g_20SecIntervalFrac * 2` and `* 1`; `menuGetSinOscFrac(1.0)` red-channel hue cycle | Combat Sim main menu backdrop |
| Bg success (3D particles) | `menugfxRenderBgSuccess` (`menugfx.c:1698-1915`) | per-particle z motion + `sinf(f0*M_BADTAU + ...)` quad rotation | Solo mission success endscreen |
| Credits BG layers | `creditsDrawBackgroundLayer` (`credits.c:381-383`) | `rotation = bglayer.rotspeed * g_CreditsCurFrame2 * 0.25`; `pan = bglayer.panspeed * ...` | Per-layer rotation + UV pan; uses tex 0x04, 0x05, 0x06, 0x07, 0x26 (mirror, dot, haze, stars) |
| N-bomb dome UV scroll | `nbomb.c:330` | `(s32)(g_20SecIntervalFrac * 64 * 32 * 16) % 0x800` | UV `s` scroll on 3D dome mesh; in-game, not menu |

**Categorization of animation kinds:**

- **No sprite-frame animations.** Every "animated" texture is a static IA8 / RGBA16 tile; the motion comes from changing UVs.
- **No palette cycles.** Palette colours are static per dialog type; the only "colour animation" is vertex-colour modulation per frame.
- **Three core mechanics:**
  1. **UV scroll / rotation** against a static texture (haze, failure fans, cone, n-bomb, credits BG, mission-list strip).
  2. **Vertex-colour or PRIM/ENV-colour modulation** by `g_20SecIntervalFrac`-derived oscillators (item pulse, chevron grow, shimmer brightness, hue cycle).
  3. **CPU-side procedural geometry** drawn each frame, position computed from `g_20SecIntervalFrac` (the white-comet shimmer; the success-endscreen 3D particles).

**Implication for `.pdui` schema**: the format does NOT need to capture frame sequences, palette cycle tables, or sprite atlases. It needs to capture:
- Texture references (catalog IDs that resolve to extracted texture bytes).
- Palette tables (six menu palettes, each 13 colours; plus the wave1 / wave2 secondary palettes used during dialog crossfades).
- Animation parameter tables (per-effect frequency, amplitude, texture refs, blend mode). Renderer evaluates these against a phase-variable equivalent to `g_20SecIntervalFrac`.

### 1.4 Current PD2 port behaviour

The port's render path is in `port/fast3d/pdgui_style.cpp` (1500+ lines). Master clock is `ImGui::GetTime()` (wall-clock seconds since context init).

**Currently implemented animations:**

- Shimmer (`pdguiDrawShimmerExact`, `:352-472`): cycle `t/20`, edge-length-aware, alpha boosted 1.8x, width doubled vs N64. Faithful port of `menugfxDrawShimmer`.
- Perimeter shimmer variant (`:740-840`): one shimmer walking the entire perimeter every 5 seconds. Different aesthetic from N64's four independent edge shimmers.
- Title text glow (`pdguiDrawTextGlow`, `:1460-1488`): three-pass expanding rounded rect, alpha pulsed via `pdguiGetTitleGlow()`.
- Button breathing pulse (`pdguiDrawButtonEdgeGlow`, `:1498-1554`): `0.5 + 0.5 * sinf(t * 4.0)` for ~4 Hz breathing.

**Currently consumed textures (out of 14 extracted):**

| Catalog id | Consumed where | Used as |
|------------|----------------|---------|
| `base:ui_bg_haze` | `pdgui_style.cpp:706-727` (dialog body overlay) | Tinted IM_COL32(0, 80, 0, 32) green tile over body fill |
| `base:ui_chrome_frame` | `pdgui_style.cpp:670-687` (when chrome enabled) | Nineslice frame around dialog |
| Other 12 (particles, noise_sm/lg, grad_bar, mirror, dot, nuke, bg_alt, deco, icons) | nowhere | Registered in catalog but unused at draw time |

**Currently UNimplemented animations vs OG:**

- `menugfxRenderBgGreenHaze` two-layer counter-rotating haze with alpha fade. The PC port has a static green-tinted haze tile but no rotation, no second layer, no fade.
- `menugfxRenderBgFailure` rotating triangle fans + horizontal blue bands.
- `menugfxRenderBgCone` two cone fans with hue cycle.
- `menugfxRenderBgSuccess` 3D star-particle field with depth attenuation.
- Item focus pulse via `menuGetSinOscFrac(40)`. The port has button-edge-glow breathing (4 Hz) but not the per-item vertex-colour pulse that highlights the focused menu row.
- Mission-list 4-frame UV step (this is a one-off and probably not worth porting unless we restore the briefing UI).
- Credits BG layers (the credits screen would need its own renderer pass).

### 1.5 Gap analysis: PC port vs OG aesthetic

Concrete element-by-element comparison from the running game (Mike's empirical observation 2026-04-30 plus code trace):

| Element | OG behaviour | PC port today | Verdict |
|---------|--------------|---------------|---------|
| Body fill | Solid `dialog_bodybg` (deep navy) | Solid `dialog_bodybg` (`pdgui_style.cpp:691-694`) | Match |
| Body haze overlay | None on dialogs | Static green-tinted tile, no UV motion (`pdgui_style.cpp:706-727`) | Port adds an OG-absent layer |
| Border lines (left, right, bottom) | Solid `dialog_border1` / `dialog_border2`, 1-2 px wide | Solid 1px lines (`pdgui_style.cpp:731-737`) | Match |
| Body border shimmer | Four independent edge sweeps; each cycle ~3.33s; full edge length traversed; bright-alpha gradient | One perimeter walker; 5s full-perimeter cycle; 20px wide; only one in flight at a time (`pdgui_style.cpp:740-840`) | Port is significantly less dense and slower |
| Title bar shimmer (top + bottom edges) | Animated white sweep | Animated white sweep via `pdguiDrawShimmerExact` (`pdgui_style.cpp:606-608`) | Match (this one IS the OG mechanism) |
| Selected-item focus pulse | `menuGetSinOscFrac(40)` blend of `item_focused_inner` and `item_focused_outer`, 2 Hz | Static solid `item_focused_outer` rect (`pdgui_style.cpp:858-863`) | Port omits the breathing pulse entirely |
| Chevron / arrow size pulse | `menuGetSinOscFrac(20)` size modulation | Not implemented | Port omits |
| Title text glow | Subtle multi-pass glow | Multi-pass glow (`pdgui_style.cpp:1460-1488`) | Match |
| Chrome (when enabled) | n/a (OG had no chrome enable concept) | Bake-once 64x64 nineslice via `s_generateChromeFrameBgra`; replaces body fill + haze + borders + shimmer (`pdgui_style.cpp:670-687`) | Off by default (`s_CfgUiChromeEnabled = 0`); when on, replaces every animated element with a static texture |
| Combat-sim backdrop (cone) | Two cone fans rotating opposite directions, hue cycle | ImGui default (no PD backdrop) | Missing |
| Solo failure backdrop | Three rotating triangle fans + horizontal bands | ImGui default | Missing |
| Solo success backdrop | 3D star-particle field with depth attenuation | ImGui default | Missing |
| Credits backdrops | Per-layer rotation + UV pan over `ui_bg_haze`, `ui_stars`, etc | None / ImGui default | Missing |

**Mike's "menus look nothing alike except in color theme" assessment is empirically accurate.** The body matches, the title shimmer matches, the colors are roughly right. Almost nothing else does. The three biggest contributors to the gap, in identity-cue order:

1. **Item focus pulse missing** (highest impact). On any menu with a list, the selected row should breathe at 1-2 Hz. The port draws a static highlight, so selection feels frozen.
2. **Body border shimmer is wrong cadence and wrong density.** OG runs 4 independent shimmers (one per edge, each ~3.33s, full edge length). Port runs 1 perimeter walker (5s, 20px wide). Net visual: where OG had constant motion on every dialog edge, the port has a slow rectangle that travels around once every 5 seconds. Easy to miss.
3. **Body haze is OG-absent and static.** Port adds a green tile (artistic license), but the tile is a static UV; no animation. So instead of looking like the OG (no haze on dialogs), it looks like a faintly tinted static body. Either back it out (match OG) or animate it (match what `menugfxRenderBgGreenHaze` does on the screens that DID have haze).

**Class B (backdrops):** lower priority because they're screen-specific. Combat sim main menu, solo failure, solo success, credits each had distinct animated backdrops; the port uses ImGui defaults. Restoring these is per-screen work and doesn't block the dialog-chrome gap above.

**Class C (unused extracted assets):** 12 of 14 extracted textures are registered in the catalog but consumed by zero draw sites. The `.pdui` repackaging should preserve them (mods may want them as templates) but the dialog-chrome gap above has higher priority than wiring these into the renderer.

**Minimum work to make in-game chrome actually look like OG, animated:**

- Replace the body's perimeter walker (`pdgui_style.cpp:740-840`) with four independent calls to `pdguiDrawShimmerExact` (already in the file, already used for title bar). One call per body edge: left, right, bottom, plus optionally top if a top border line is present. Math: `freq = 6` (matches N64), edge-relative phase offset.
- Implement `pdguiDrawItemFocusPulse(rect, freq_per_20s = 40)`: vertex-color blend of inner / outer using `0.5 + 0.5 * sin(t * freq * 2 * pi / 20)`. Replace `pdguiDrawItemHighlight` callers, or call alongside it.
- Decide on the body haze (Q-B in Section 5). Either remove the static overlay entirely (matches OG dialogs exactly) or make it animated via the haze-rotation recipe (matches the OG `menugfxRenderBgGreenHaze` math, which only ran on certain non-dialog screens).
- Document that `s_generateChromeFrameBgra` is opt-in via `Video.UiChromeEnabled = 1`, and is a static-texture override of the procedural chrome (intentionally trades animation for a custom look).

These three changes are mechanical, fit inside `pdgui_style.cpp`, and need no `.pdui` infrastructure. They should land BEFORE Stage A (`.pdui` extraction) so the visual baseline matches OG before we package it as a redistributable bundle.

<!-- SENTINEL: Section 1.5 end -->

---

## 2. Phase 2 proposal: `.pdui` schema

### 2.1 Extensibility contract (universal across `.pdXXX` types)

Per Mike's directive (2026-04-30), every `.pdXXX` compound mod format ships from day one with the following eight extensibility guarantees. `.pdui` is the first format to adopt them, setting the pattern for `.pdwpn`, `.pdmesh`, `.pdcharacter`, `.pdscenario`, etc.

**Rule 1: `schema_version` field at root.** Every compound's `mod.json` carries a `schema_version` integer at the top level. Loaders branch on it. Missing field = treat as `1` (v1 schema).

**Rule 2: Unknown fields tolerated.** Loader silently ignores keys it does not recognise; never errors. Round-trippable editors preserve unknown keys on save so future-version content survives an old-tool save without data loss.

**Rule 3: Required vs optional explicit.** Required fields per major version are frozen for that major's lifetime. New optional fields permitted at any minor bump. New required fields require a major bump and a migration tool that walks existing content.

**Rule 4: No semantic changes to existing fields.** Once a field ships, its meaning is permanent. Behaviour changes require a new field with a new name, not a redefinition.

**Rule 5: Reserved namespaces.**
- Top-level keys without prefix are reserved for canonical engine schema. Engine validates and consumes.
- Top-level keys prefixed `x_` are vendor or mod-author extensions. Engine ignores; tools preserve.
- Top-level keys prefixed `_` are engine-internal (e.g. cached hashes, last-load timestamp). Engine writes; tools should not author.

**Rule 6: Engine compat declared.** Every compound declares `min_engine_version` and optionally `max_engine_version` (both semver strings). Engine declares its own supported schema range per asset type. Mismatch = LOUDFAIL with a clear message naming the file, the declared range, and the engine's range. No silent fail. No silent ignore.

**Rule 7: Composition over inheritance.** New asset variations get new compound types or new optional fields. Never breaking changes to existing types. A `.pdui_extended` variant is preferred to a v2 incompatible `.pdui`.

**Rule 8: Validate at registration.** Catalog registration runs the schema validator. LOUDFAIL on `schema_version` unsupported, missing-required, type-mismatch, unresolvable cross-references. Runtime never sees an invalid row.

**Recommendation: a follow-up addendum to `context/audits/rom-extraction-audit-2026-04-30.md` formalising these eight rules as a "Section 3.19 Schema extensibility contract".** Out of scope for this Phase 2 sketch but flagged for Mike to greenlight.

### 2.2 `.pdui` archive layout

A `.pdui` is a ZIP archive (same container as `.pdmod` per `port/src/modarchive.c`). Layout:

```
mod.json                                     # manifest (root document)
textures/<name>.tga                          # extracted RGBA32 textures (top-down 32-bit)
textures/<name>.png                          # PNG mirror for modder editing tools
textures/<name>.9slice                       # optional INI; nineslice insets per texture
palettes/<name>.json                         # palette tables (one per palette type)
animations/<name>.json                       # animation parameter tables (one per effect family)
themes/<theme_id>.json                       # full theme bundles (composition of palette + textures + anims)
README.md                                    # optional human-readable overview
```

Granularity: per-texture for textures, per-palette for palettes, per-effect-family for animations, per-bundle for themes. Modders editing a single texture or palette don't have to repack the whole theme.

`mod.json` example for `data/ui/pd-original.pdui` (the extracted base UI):

```json
{
    "schema_version": 1,
    "id": "base:pd-original",
    "version": "1.0.0",
    "display_name": "Perfect Dark Original UI",
    "description": "Authentic N64 Perfect Dark UI textures, palettes, and animation parameters extracted from ROM at first launch.",
    "author": "Rare / PD2 Team",
    "type": "ui_bundle",
    "min_engine_version": "0.0.176",

    "internal_assets": [
        {
            "catalog_id": "base:ui_bg_haze",
            "type": "texture",
            "path": "textures/ui_bg_haze.tga",
            "format": "ia8",
            "source": { "rom_texturenum": 485 },
            "metadata": { "wrap_s": "wrap", "wrap_t": "wrap" }
        },
        {
            "catalog_id": "base:ui_particles",
            "type": "texture",
            "path": "textures/ui_particles.tga",
            "format": "rgba32",
            "source": { "rom_texturenum": 1 }
        }
        // ... 12 more texture entries omitted for brevity
    ],

    "palettes": [
        {
            "catalog_id": "base:palette_blue",
            "type": "menu_palette",
            "path": "palettes/blue.json"
        },
        {
            "catalog_id": "base:palette_red",
            "type": "menu_palette",
            "path": "palettes/red.json"
        }
        // ... 4 more (green, white, lightgrey, disabled-grey) plus wave1 / wave2
    ],

    "animations": [
        {
            "catalog_id": "base:anim_shimmer_default",
            "type": "shimmer",
            "path": "animations/shimmer_default.json"
        },
        {
            "catalog_id": "base:anim_haze_counterrotate",
            "type": "bg_haze",
            "path": "animations/haze_counterrotate.json"
        },
        {
            "catalog_id": "base:anim_focus_pulse_default",
            "type": "focus_pulse",
            "path": "animations/focus_pulse.json"
        }
    ],

    "themes": [
        {
            "catalog_id": "base:theme_blue_default",
            "path": "themes/blue_default.json",
            "is_default": true
        }
    ]
}
```

`min_engine_version` lets us bump the running engine forward without breaking older `.pdui` bundles, and lets a future `.pdui` declare features only the new engine understands.

### 2.3 Sub-document schemas

**`palettes/blue.json`**:

```json
{
    "schema_version": 1,
    "catalog_id": "base:palette_blue",
    "type": "menu_palette",
    "display_name": "Blue (Classic)",

    "dialog_border1":            "#0060BF",
    "dialog_titlebg":            "#000000",
    "dialog_border2":            "#00F0FF",
    "dialog_titlefg":            "#FFFFFF",
    "dialog_bodybg":             "#00002F",
    "item_unfocused":            "#7F7F7F",
    "item_disabled":             "#404040",
    "item_focused_inner":        "#A0E0FF",
    "checkbox_checked_unfocused":"#80C0E0",
    "item_focused_outer":        "#FFFFFF",
    "listgroup_headerbg":        "#001040",
    "listgroup_headerfg":        "#80FFFF"
}
```

Hex color strings (`#RRGGBB` or `#RRGGBBAA`). Captures every member of `struct menucolourpalette` (`src/include/types.h:5366-5382`) except the four `unused*` fields, which are intentionally dropped from schema (rule 4 protects: if a future PD revision reuses them, that's a new field with a new name).

**`animations/shimmer_default.json`**:

```json
{
    "schema_version": 1,
    "catalog_id": "base:anim_shimmer_default",
    "type": "shimmer",

    "freq_per_20s": 6.0,
    "width_px": 10,
    "alpha_base": 0.86,
    "color": "#FFFFFF",
    "phase_per_edge_offset": true,
    "edge_length_modulo": "edge_length",
    "alpha_boost_pc": 1.8,
    "width_scale_pc": 2.0
}
```

`freq_per_20s` matches the N64 unit `freq` from `menuGetSinOscFrac`. `phase_per_edge_offset = true` instructs the renderer to add `(y1 + x1)` per edge so the four borders desynchronise. `alpha_boost_pc` and `width_scale_pc` are PC-specific compensations for higher resolution; defaults match the current `pdgui_style.cpp` values. A modder authoring an HD-only theme can override.

**`animations/haze_counterrotate.json`**:

```json
{
    "schema_version": 1,
    "catalog_id": "base:anim_haze_counterrotate",
    "type": "bg_haze",

    "texture_id": "base:ui_bg_haze",
    "layer_count": 2,
    "rotation_period_s": 20.0,
    "layer1_phase_offset": 0.5,
    "layer1_direction": "reverse",
    "alpha_envelope": [
        { "frac": 0.0, "alpha": 0.0 },
        { "frac": 0.2, "alpha": 0.5 },
        { "frac": 0.9, "alpha": 0.5 },
        { "frac": 1.0, "alpha": 0.0 }
    ],
    "color_corners": ["#00AF0000", "#FFFF0000", "#FFFF0000", "#00AF0000"],
    "zoom_envelope": { "amplitude": 0.1, "base_scale": 15.0 }
}
```

Captures the `menugfxRenderBgGreenHaze` recipe (`menugfx.c:226-341`) as data. A modder can ship `.pdui` with their own `bg_haze` parameters (orange tint, 4 layers, 60s rotation, etc.) without touching renderer code.

**`themes/blue_default.json`**:

```json
{
    "schema_version": 1,
    "catalog_id": "base:theme_blue_default",
    "display_name": "Blue (Classic Default)",

    "palette_id": "base:palette_blue",
    "palette_secondary_id": "base:palette_blue_wave1",

    "dialog_chrome": {
        "shimmer_id": "base:anim_shimmer_default",
        "border_color_top": "dialog_border2",
        "border_color_bottom": "dialog_border1",
        "border_thickness_px": 1,
        "haze_overlay_id": null,
        "_note": "haze_overlay_id null = OG behaviour (no haze on dialogs); set to base:anim_haze_counterrotate for the modern look"
    },

    "items": {
        "focus_pulse_id": "base:anim_focus_pulse_default",
        "chevron_pulse_freq_per_20s": 20.0
    },

    "backdrops": {
        "main_menu_id": null,
        "combat_sim_id": "base:anim_bg_cone_default",
        "solo_failure_id": "base:anim_bg_failure_default",
        "solo_success_id": "base:anim_bg_success_default",
        "credits_id": "base:anim_credits_default"
    }
}
```

Themes compose palette IDs, animation IDs, and per-screen backdrop IDs. A modder shipping a custom theme `.pdui` can reuse base palettes and animations, override only what they want different, and deliver a single coherent identity.

### 2.4 Reserved namespaces

Per rule 5:

- `base:*` is the base game namespace. Reserved for assets shipping in the canonical PD2 release plus first-launch ROM extraction.
- `data:*` is reserved for content the first-launch extractor produces from the user's ROM (in case future asset classes need to disambiguate from base).
- `mod:<author>:*` is the modder namespace prefix. Mods author into their own slug (e.g. `mod:halo_team:ui_chrome_master_chief`).
- Top-level mod.json keys without `x_` or `_` prefix are reserved for engine schema. Future extensions add new keys without breaking existing.

`x_*` examples: `x_blender_export_version` (set by a Blender plugin), `x_color_palette_picker_state` (a future GUI tool's state). Engine ignores; tools preserve.

`_*` examples: `_archive_sha256` (engine-stamped at extraction), `_origin_catalog_id` (per audit Section 3.16 E-23 provenance). Engine writes; tools should treat as read-only.

### 2.5 Validation behaviour

At catalog registration time, every `.pdui` and every internal sub-document gets validated:

1. **`schema_version` check.** Engine looks up the supported range for the asset type. Out-of-range = `LOUDFAIL.CATALOG.SCHEMA_UNSUPPORTED` with file path, declared version, supported range. Skip the entire compound; do not register any of its assets.
2. **`min_engine_version` / `max_engine_version` check.** Engine version vs declared compatibility window. Out-of-range = `LOUDFAIL.CATALOG.ENGINE_INCOMPATIBLE`. Skip the compound.
3. **Missing-required-field check.** Per asset type, validate the required field set for that schema version. Missing = `LOUDFAIL.CATALOG.SCHEMA_REQUIRED_MISSING` with field path. Skip the failing internal asset; continue with siblings.
4. **Type-mismatch check.** Field declared `string` but value is integer = `LOUDFAIL.CATALOG.SCHEMA_TYPE_MISMATCH`. Skip the failing asset.
5. **Cross-reference resolution.** Theme references `palette_id`; if unresolvable = `LOUDFAIL.CATALOG.UNRESOLVED_REF`. Skip the failing asset (the palette is known to exist by load time per the topological sort in audit Section 3.16.7).
6. **Catalog ID uniqueness.** Per audit Section 3.5 D-2; duplicate ID = `LOUDFAIL.CATALOG.DUPLICATE_ID`. First-loaded wins (audit Section 3.5 E-25); the second LOUDFAILs.

Validation errors do NOT prevent the rest of the compound from registering. A single broken texture entry inside a 14-texture `.pdui` LOUDFAILs that one entry and registers the other 13. This honours the "additive only" architecture; partial mods still contribute what they can.

<!-- SENTINEL: Section 2.5 end -->

---

## 3. Phase 2 implementation pipeline

The pipeline staged so each step delivers a verifiable artifact and can be reviewed in isolation. Stop conditions per stage are flagged.

### 3.1 Stage A: extractor emits `.pdui` archive

Replace today's `pdguiThemeExtractRomTextures` write-loose-files behaviour with a write-ZIP-archive behaviour. The archive lands at `data/ui/pd-original.pdui`.

**Code surface affected:**
- `port/fast3d/pdgui_theme.cpp:2405` (`pdguiThemeExtractRomTextures`): replaces direct `fopen` + `fwrite` with a ZIP-builder helper. The TGA / PNG / nineslice JSON encoders stay; they now write into the archive instead of the filesystem.
- New helper `port/src/pdui_archive.c` (or similar): wraps the existing `modarchive` ZIP infrastructure for the write side. Same library `modarchive.c` uses for the read side; need to verify whether the existing code includes a compatible writer or if we add one.
- `mod.json` template generation: replace the inline string literal at `pdgui_theme.cpp:3201-3239` with a structured builder that emits the schema-compliant manifest defined in Section 2.

**Output:** `data/ui/pd-original.pdui` (single ZIP archive, conforming to Section 2 schema, schema_version = 1, min_engine_version = current build).

**Verification:** unzip the archive, confirm `mod.json` parses, confirm 14 textures present in `textures/`, confirm 6 palettes present in `palettes/`, confirm at least the shimmer + haze animation descriptors present in `animations/`, confirm the default theme present in `themes/`. Schema validator gives a clean pass.

**Stop condition:** if the existing `modarchive.c` doesn't have a writer (read-only archive support), Stage A becomes a write-side ZIP infrastructure task. Likely small (zlib + miniz are linked; `port/fast3d/pdgui_theme.cpp:2205` already has a `s_writePng` using miniz).

### 3.2 Stage B: catalog provider for `.pdui`

The catalog needs to discover `.pdui` files in `data/`, `base/`, and `mods/`, parse their manifests, and register their internal assets.

**Code surface affected:**
- `port/src/assetprovider_data.c` (new): per audit Section 3.11 A-3, sibling to `assetprovider_rom.c` and the existing mod provider. Reads `.pdui` from `data/` filesystem; provides bytes via the standard provider interface.
- `port/src/modmgr.c::modmgrScanDirectory`: recognise `.pdui` extension as a valid compound (it already recognises `.pdmod`; needs a small extension allowlist update plus dispatch to the right loader).
- `port/src/modarchive.c`: ensure ZIP read works for `.pdui` (it should; same container format).
- `port/src/assetcatalog_load.c`: register internal asset entries with the right asset types. New types may be needed: `ASSET_PALETTE`, `ASSET_ANIMATION_DEFINITION`, `ASSET_THEME_BUNDLE`. (Today only `ASSET_UI` exists per `port/include/assetcatalog_types.h`; verify and extend.)

**Cross-cutting decision:** the audit recommends `data/` is read-only after extraction (Section 3.7). Stage B should respect that: the data provider opens `.pdui` files for read-only access; the extractor at Stage A is the only path that writes. Acquire-writable-during-extraction wraps the Stage A entry point.

**Verification:** at startup, log `CATALOG: register base:ui_bg_haze from data:pd-original.pdui` for each registered asset. Run a `pd-tests` extraction smoke test that loads a mock `.pdui` fixture and asserts every internal asset shows up in the catalog.

**Stop condition:** if the catalog asset-type enum needs to grow significantly to fit palettes / animations / themes, this becomes a catalog architecture change instead of a UI-only change. May warrant Mike's design review before extending.

### 3.3 Stage C: render-side wiring

`port/fast3d/pdgui_style.cpp` currently hard-codes shimmer math, haze tint, palette colours. Convert to consume from the active theme:

**Code surface affected:**
- `pdguiThemeGetTexture(catalog_id)`: already exists at `port/fast3d/pdgui_theme.cpp:1807`. Extend with `pdguiThemeGetPalette(catalog_id)`, `pdguiThemeGetAnimation(catalog_id)`, `pdguiThemeGetCurrentTheme()`.
- `pdguiDrawShimmerExact` (`pdgui_style.cpp:352`): take `freq_per_20s`, `width_px`, `alpha_base`, `color`, `alpha_boost_pc`, `width_scale_pc` from the active shimmer animation descriptor instead of hardcoding.
- `pdguiDrawPdDialog` (`pdgui_style.cpp:670` and surrounding): take dialog body fill from active palette's `dialog_bodybg`. Take border colours from `dialog_border1` / `dialog_border2`. Take haze overlay tint and texture from active theme's `dialog_chrome.haze_overlay_id` (null = skip the overlay = OG behaviour).
- New: surface `menugfxRenderBgGreenHaze` recipe as a renderable backdrop. The `bg_haze` animation descriptor (Section 2.3) carries every parameter needed.
- New: item focus pulse via `pdguiDrawItemFocusPulse(rect, freq_per_20s)` callable from menu render code. Implements the `menuGetSinOscFrac(40)` vertex-colour modulation.

**Verification:** in-game playtest (Mike); compare against original PD reference video. Specifically verify the four-edge-shimmer cadence (~3.33s per edge), the haze tint when enabled, and the focus pulse on selected menu rows.

**Stop condition:** the four `menugfxRenderBg*` backdrops would each need a render path. Failure / cone / success backdrops are screen-specific; surface them as distinct `bg_*` animation types but only wire the ones whose host screens are actually live in the port. Defer the rest.

### 3.4 Stage D: LOUDFAIL channel integration

Per audit Section 3.8, every recovery surface logs a LOUDFAIL line with structured context.

**Channels used by the UI pipeline:**
- `LOUDFAIL.EXTRACT.ROM_BAD_FORMAT`: `cfg->fmt` / `cfg->siz` not in the supported list (RGBA16, RGBA32, IA16, IA8, IA4). Today this is a `LOG_WARNING`; promote to LOUDFAIL.
- `LOUDFAIL.EXTRACT.ROM_DIMS_INVALID`: `w == 0 || h == 0 || w > 256 || h > 256`. Today logged; promote.
- `LOUDFAIL.EXTRACT.ROM_DECODE_FAILED`: `s_decodeAndUpload` returned 0. Today logged; promote.
- `LOUDFAIL.EXTRACT.WRITE_FAILED`: `s_writeTga` / `s_writePng` failed. Today silent; add LOUDFAIL.
- `LOUDFAIL.PROC.SUBSTITUTED`: procedural fallback fired. Today logged at LOG_NOTE; promote and include the specific reason (missing-from-rom / decode-failed / write-failed).
- `LOUDFAIL.CATALOG.SCHEMA_*`: per Section 2.5 of this doc.
- `LOUDFAIL.HEAL.QUARANTINED`: per audit Section 3.6, when a manifest hash mismatch triggers self-heal.

**Implementation:** a new `sysLogPrintf(LOG_LOUDFAIL, "...")` level or a parallel macro `LOUDFAIL(channel, "...")` that routes to both the log file and a future in-game LOUDFAIL diagnostics panel (audit Section 3.8 E-6).

**Verification:** induce each failure mode in a unit test, assert the LOUDFAIL channel emitted the right code with the right context.

### 3.5 Suggested staging order

For Mike to confirm:

1. **Stage A first (extractor emits archive).** Lowest risk; the existing extraction logic is preserved and only the output container changes. Validates the schema design end-to-end.
2. **Stage B (catalog provider).** Once `.pdui` archives exist, wire them through the catalog. Today the loose-files path can stay as a fallback while the new path is verified.
3. **Stage D LOUDFAIL channel** in parallel with B. Small, isolated, and unblocks the rest of the diagnostics work in the audit.
4. **Stage C (render-side wiring).** Last because it requires both A and B to be solid. Render code consumes from catalog. Conservative: keep existing hard-coded values as renderer-side defaults if catalog lookup fails.

Each stage is a single PR-sized chunk. Total session count estimate: 4-6 sessions for Stages A-D, then per-screen wiring of the missing animations (Class B gaps from Section 1.5) as a separate cadence.

<!-- SENTINEL: Section 3 end -->

---

## 4. LOUDFAIL channel taxonomy (UI pipeline subset)

Cross-references the audit's Section 3.8 channel taxonomy. Specific to the UI pipeline:

| Channel | Trigger | Recovery action | Severity |
|---------|---------|-----------------|----------|
| `LOUDFAIL.EXTRACT.ROM_NOT_LOADED` | `g_TexGeneralConfigs == NULL` (texReset not yet run) | Skip extract; retry next frame | Recoverable |
| `LOUDFAIL.EXTRACT.ROM_BAD_TEXNUM` | `cfg->texturenum` out of range | Skip this entry; substitute procedural | Per-asset; recoverable |
| `LOUDFAIL.EXTRACT.ROM_BAD_FORMAT` | `fmt` / `siz` not in decoder allowlist | Skip; substitute procedural | Per-asset; recoverable |
| `LOUDFAIL.EXTRACT.ROM_BAD_DIMS` | `w == 0 || h == 0 || w > 256 || h > 256` | Skip; substitute procedural | Per-asset; recoverable |
| `LOUDFAIL.EXTRACT.DECODE_FAILED` | `s_decodeAndUpload` returned 0 | Skip; substitute procedural | Per-asset; recoverable |
| `LOUDFAIL.EXTRACT.WRITE_FAILED` | TGA / PNG / archive write failed | Skip; substitute procedural | Per-asset; recoverable |
| `LOUDFAIL.EXTRACT.ARCHIVE_BUILD_FAILED` | ZIP encoder error during `.pdui` build | Fallback to loose-files for this run; retry next launch | Compound-level; recoverable |
| `LOUDFAIL.PROC.SUBSTITUTED` | Procedural fallback texture written in lieu of ROM-extracted | None; the substitution succeeds | Informational LOUDFAIL; the user / modder needs to know |
| `LOUDFAIL.CATALOG.SCHEMA_UNSUPPORTED` | `.pdui` schema_version out of supported range | Skip the compound entirely | Compound-level; not recoverable for this compound |
| `LOUDFAIL.CATALOG.ENGINE_INCOMPATIBLE` | min/max_engine_version mismatch | Skip the compound | Compound-level |
| `LOUDFAIL.CATALOG.SCHEMA_REQUIRED_MISSING` | Required field absent from internal asset entry | Skip the failing asset; continue siblings | Per-asset |
| `LOUDFAIL.CATALOG.SCHEMA_TYPE_MISMATCH` | Field type wrong | Skip the failing asset | Per-asset |
| `LOUDFAIL.CATALOG.UNRESOLVED_REF` | Cross-ref to unresolvable catalog id | Skip the failing asset | Per-asset |
| `LOUDFAIL.CATALOG.DUPLICATE_ID` | Two compounds register same id | First wins; second's conflicting asset skipped | Per-asset |
| `LOUDFAIL.HEAL.HASH_MISMATCH` | manifest hash != actual file hash | Quarantine; re-extract | Per-asset; recoverable |
| `LOUDFAIL.HEAL.REEXTRACT_FAILED` | Self-heal re-extraction failed | Substitute procedural; surface to user | Per-asset; degraded |

Each LOUDFAIL line carries: channel name, file path or catalog id, expected vs actual values, suggested action ("rename your mod's catalog id", "supply the correct ROM version", etc.).

<!-- SENTINEL: Section 4 end -->

---

## 5. Open questions for Mike (require direction before implementation)

These need answers before Stage A starts. Each has a recommended default if Mike is OK with the audit's defaults.

**Q-A: What is the minimum work to make in-game chrome actually look like OG, animated?** This question is the load-bearing one. The "no ROM source art exists" finding (Section 1.2) is correct but it does NOT mean "the current procedural chrome is fine." Per Mike's empirical observation (corrected from my initial reading-the-code-only assessment), the procedural chrome currently produces a visibly inadequate result: "static and blue, no animation, looks nothing like OG except color theme." The gap analysis in Section 1.5 enumerates the missing identity cues. Three paths forward:

- **Path 1 (chrome-fix-first):** before any `.pdui` work, land the three minimum-viable renderer changes from Section 1.5: four-edge body shimmer (replaces perimeter walker), item focus pulse (`pdguiDrawItemFocusPulse`), haze decision. ~200-400 lines port-side. Then proceed to Stage A bundling once the visual baseline matches OG. This means the `.pdui` we ship represents a chrome that actually looks like PD.
- **Path 2 (bundle-then-fix):** continue with Stages A-D as previously sketched. The chrome continues to look wrong while the bundling lands. Fix the renderer in a follow-up. This means we ship a `.pdui` representing the wrong-looking chrome, then have to update both the bundle and the renderer afterward.
- **Path 3 (redirect entirely):** abandon the `.pdui` track for now and focus exclusively on chrome render fix. Once chrome looks right, revisit whether the bundle is worth doing at all (mods can drive their own theme params via existing `.pdmod` infrastructure).

Recommendation: **Path 1.** The renderer fix is both simpler than the bundling work and a prerequisite to proving the bundle does the right thing. Stage A through D from Section 3 should run AFTER the chrome-fix-first work lands. Concretely:

1. **Stage 0 (new, prerequisite):** `pdgui_style.cpp` chrome render fix. Four-edge body shimmer. Item focus pulse. Haze decision (recommend Q-B Path 1: remove for OG authenticity). One PR. Mike playtest validates the chrome looks OG.
2. **Stage A through D (per Section 3):** unchanged in shape, but now layered on top of a renderer that already looks right. The `.pdui` extracts the textures plus the palette tables; the renderer consumes them; the visual stays correct.

Path 3 (abandon bundle) is not recommended. The bundle work is independently valuable for the mod ecosystem (per audit Section 3.13) and unlocks `.pdwpn` / `.pdmesh` / `.pdscenario` extension follow-ons. Keep it on the queue; just gate it behind Stage 0.

**Open question inside Q-A**: should the "static-texture chrome override" (`s_generateChromeFrameBgra` / `Video.UiChromeEnabled`) be kept at all once Stage 0 lands and the procedural chrome looks right? It was originally written as a substitute for the under-implemented procedural chrome. With Stage 0 in place, it becomes a Settings option that trades animation for a custom static look. Possibly worth keeping for modders who ship a custom static frame, possibly worth removing as cruft. Mike's call.

**Q-B: The artistic-license green haze on dialogs.** Section 1.5 Class A. The PC port adds a tinted haze overlay to all dialogs (`pdgui_style.cpp:706-727`); the OG does not. Two paths:

- **Path 1**: back it out. Match OG exactly (solid `dialog_bodybg` fill, no haze).
- **Path 2**: keep it as the default; surface it as a theme parameter (`dialog_chrome.haze_overlay_id`) so themes can opt in or out.

Recommendation: **Path 2**. Surface as a theme parameter with `null` (no haze) being the OG-authentic default; the current PC port behaviour becomes a separate theme `base:theme_blue_modern` that ships alongside `base:theme_blue_classic`. User chooses; modders override.

**Q-C: 12 unused extracted textures.** Section 1.5 Class C. They're extracted, registered in the catalog, and unused at draw time. Two paths:

- **Path 1**: keep extracting them (mods may want them).
- **Path 2**: stop extracting them (the only consumed ones are bg_haze and chrome_frame; everything else is deadweight).

Recommendation: **Path 1**. Per the audit's Section 3.13 M-1 / M-3, modders need source-shape access to base content for templates. Even if the engine doesn't draw them today, modders authoring custom themes will reference them. The `.pdui` archive should contain all 14.

**Q-D: Scope of the `.pdui` catalog asset types.** Section 3.2 introduces `ASSET_PALETTE`, `ASSET_ANIMATION_DEFINITION`, `ASSET_THEME_BUNDLE` as new catalog types. Two paths:

- **Path 1**: actually grow the catalog enum; each becomes a first-class asset type that mods can override or extend.
- **Path 2**: keep the catalog typed only at `ASSET_UI` (which exists today); the palettes and animations live as named blocks inside the `.pdui` and are accessed via theme-system APIs, not catalog APIs.

Recommendation: **Path 1**, but stage it. Stage A and B can use Path 2 (palettes and animations are blocks inside the `.pdui` manifest, addressed by string id within the bundle). Stage C onward, when modders want to ship "just a palette" without a whole UI bundle, promote to first-class catalog types. This avoids bloating the catalog enum on day one and keeps Stage A/B small.

**Q-E: Schema extensibility addendum to the audit.** The 8-rule extensibility contract from Mike's directive (Section 2.1 of this doc) is not in the current audit. Two paths:

- **Path 1**: add it as a new "Section 3.19 Schema extensibility contract" addendum to `context/audits/rom-extraction-audit-2026-04-30.md`.
- **Path 2**: leave it in this UI-pipeline doc; cross-reference from future format design docs.

Recommendation: **Path 1**. The 8 rules apply to every `.pdXXX`, not just `.pdui`. The audit is the canonical architectural spec; the extensibility contract belongs there.

**Q-F: First-implementation ROM hash work.** Audit Section 3.18 priority items 2 (populate ROM SHA-256 hashes) and item 6 (this work) interact: the extraction we're doing is currently NTSC-final-only because `port/src/romdata.c:42-62` only supports that ROM. If we want the `.pdui` extraction to be multi-ROM-ready (per audit Section 3.11 A-6), we should populate the hash table now so the extractor can pick the right offsets. Two paths:

- **Path 1**: do hash work as a prerequisite to Stage A.
- **Path 2**: defer multi-ROM until the single-ROM path is proven; today's extractor already assumes NTSC-final.

Recommendation: **Path 2**. Single-ROM extraction is the immediate goal. Hash table populates as a future audit-priority-2 task. The `.pdui` format already accommodates multi-ROM via the `data/<romid>/` directory layout (audit Section 3.4); structurally ready, just not exercised today.

<!-- SENTINEL: Section 5 end -->

---

## 6. Where to look

**Existing code (consumed by this work)**
- Runtime UI extractor: `port/fast3d/pdgui_theme.cpp:2405` (`pdguiThemeExtractRomTextures`)
- Texture config table: `src/textureconfig.c:263-320` (56-entry `g_TcGeneralConfigs`)
- Texture config struct: `src/include/types.h:3867-3880`
- Texture decode pipeline: `src/game/texdecompress.c:2188-2262`
- Menu palette struct: `src/include/types.h:5366-5382`
- Menu palette tables: `src/game/menu.c:103-138` (6 palettes plus wave1 / wave2)
- N64 dialog render: `src/game/menugfx.c:187-218` (`menugfxRenderDialogBackground`)
- N64 shimmer: `src/game/menugfx.c:1012-1119` (`menugfxDrawShimmer`)
- N64 haze recipe: `src/game/menugfx.c:226-341` (`menugfxRenderBgGreenHaze`)
- N64 backdrops: `src/game/menugfx.c:1334-1437` (failure), `:1445-1516` (cone), `:1698-1915` (success)
- N64 animation clock: `src/game/game_006900.c:18` (`g_20SecIntervalFrac`), `:94-126` (helpers)
- PC port shimmer: `port/fast3d/pdgui_style.cpp:352-472` (`pdguiDrawShimmerExact`)
- PC port dialog render: `port/fast3d/pdgui_style.cpp:670-737`
- PC port chrome bake: `port/fast3d/pdgui_theme.cpp:2856-2980` (`s_generateChromeFrameBgra`)
- PC port theme registration: `port/fast3d/pdgui_theme.cpp:1711-1785` (`pdguiThemeLateInit`)
- PC port texture cache: `port/fast3d/pdgui_theme.cpp:1807` (`pdguiThemeGetTexture`)
- Catalog asset types: `port/include/assetcatalog_types.h`
- Mod archive read: `port/src/modarchive.c`, `port/src/modvfs.c`
- Mod manager scan: `port/src/modmgr.c`

**Architectural references**
- Master spec: `context/audits/rom-extraction-audit-2026-04-30.md` (especially 3.4, 3.6, 3.7, 3.8, 3.16, 3.17, 3.18)
- Pillar docs to update on landing: `context/pillars/menus.md`, `context/pillars/modding.md`, `context/pillars/catalog.md`
- Constraints to respect: `context/constraints.md` (especially "Catalog ID strings at all interface boundaries", "ImGui is the sole menu system", "name-based asset resolution only")
- Truncation discipline: `context/procedures.md` Section "Truncation guard discipline"
- Build verify: `devtools/build-session.ps1 -Session jovial -Target all` (NEVER cmake / ninja directly from worktree per worktree guard)

**Upstream parity**
- Upstream extract.py: https://raw.githubusercontent.com/fgsfdsfgs/perfect_dark/port/tools/extract (byte-identical to PD2's; no extra UI tooling exists upstream)
- Upstream textureconfig.c: https://raw.githubusercontent.com/fgsfdsfgs/perfect_dark/port/src/textureconfig.c (same 56-entry table; no naming layer)

**Related context**
- Tasks block 1a (`context/tasks.md:35-41`): "Texture deployment + extraction investigation". Mike's earlier framing was about deployment fragility; this audit subsumes that with a more architectural framing.
- Worked example for compound-mod authoring: `context/audits/rom-extraction-audit-2026-04-30.md` Section 3.17 (Halo fusion-coil prop mod).
- F11-F13 catalog cadence (just shipped): the `.pdbase` pattern (`base/weapons.pdbase` plus `loader_pdbase.c`) is structurally analogous to what `.pdui` should become for UI assets.

<!-- SENTINEL: Document end -->
