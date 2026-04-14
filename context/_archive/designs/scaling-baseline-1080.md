# Scaling Baseline: 720p → 1080p Migration (S193)

> **Created**: 2026-04-10 (S193)
> **Status**: IMPLEMENTED
> **Applies to**: `port/fast3d/pdgui_scaling.h` and every file that calls
> `pdguiScale*` helpers.

---

## Summary

`pdgui_scaling.h` originally computed its resolution-independent scale factor
against a **720p** reference (`scale = displayH / 720.0f`).  The authoritative
design spec in [d5-full-menu-overhaul.md](d5-full-menu-overhaul.md) (see
"Menu UX Guidelines > UI Scaling") specifies **1080p** (`scale = displayH /
1080.0f`).  This discrepancy was flagged during S192 Batch 0 but not resolved
because the fix touches every menu file.  S193 performs the fix.

**After S193**, pixel constants in menu code represent values at 1080p.  A
button declared as `pdguiScale(64.0f)` renders as 64 px at 1080p, 85 px at
1440p, and 128 px at 4K, matching the d5 reference table.

## Reference Values (from d5-full-menu-overhaul.md, 1080p column)

| Element                | 1080p (px) |
| ---------------------- | ---------- |
| Menu heading           | 36         |
| Body text / menu items | 24         |
| Small UI labels        | 16         |
| Button height          | 64         |
| Min focus/click target | 48         |
| Window padding         | 16         |
| Item spacing           |  8         |
| Scrollbar width        | 14         |

Use these values directly in new menu code.  Wrap in `pdguiScale()` so they
scale at other resolutions automatically.

## What Changed

### `port/fast3d/pdgui_scaling.h`

- `pdguiScaleFactor()` now computes `scale = displayH / 1080.0f`.
- `pdguiMenuWidth()` cap is `1800 * scaleFactor` (was `1200 * scaleFactor`),
  preserving the visual proportion at every resolution.
- `pdguiBaseFontSize()` baseline is `24.0f` (body text at 1080p) instead of
  `16.0f` (body text at 720p).  Minimum floor still `12.0f`.
- Added `PDGUI_REF_WIDTH` / `PDGUI_REF_HEIGHT` macros at the top of the file.

### `port/fast3d/pdgui_backend.cpp`

- `pdguiGetSafeArea()` fallback display size is `1920x1080` (was `1280x720`).

### `port/fast3d/pdgui_layout.cpp`

Action bar metrics flipped from 720p- to 1080p-referenced, with comment
expanded to cross-reference the d5 table:

| Constant                       | 720p | 1080p |
| ------------------------------ | ---- | ----- |
| `PDGUI_AB_BASE_HEIGHT_PX`      | 56   | 84    |
| `PDGUI_AB_MIN_HEIGHT_PX`       | 48   | 48    |
| `PDGUI_AB_BUTTON_HEIGHT_PX`    | 42   | 64    |
| `PDGUI_AB_BODY_GAP_PX`         |  8   | 12    |
| `PDGUI_AB_BODY_MIN_PX`         | 60   | 90    |

The min floor remains an absolute-pixel floor, not scaled, so the action bar
is still tappable on very small displays.

### All menu files (17 files, 312 `pdguiScale()` call sites)

Every `pdguiScale(X.Yf)` literal multiplied by `1.5` (= 1080/720) so visual
output is **identical** at every resolution before and after the flip.  The
transformation was mechanical and applied via
`context/scratch/s193_scale_flip.py`.

Files touched:
- `pdgui_countdown.cpp`
- `pdgui_menu_challenges.cpp`
- `pdgui_menu_endscreen.cpp`
- `pdgui_menu_lobby.cpp`
- `pdgui_menu_mainmenu.cpp`
- `pdgui_menu_modmgr.cpp`
- `pdgui_menu_mpingame.cpp`
- `pdgui_menu_mpsettings.cpp`
- `pdgui_menu_network.cpp`
- `pdgui_menu_pausemenu.cpp`
- `pdgui_menu_room.cpp`
- `pdgui_menu_solomission.cpp`
- `pdgui_menu_stats.cpp`
- `pdgui_menu_teamsetup.cpp`
- `pdgui_menu_training.cpp`
- `pdgui_menu_update.cpp`
- `pdgui_menu_warning.cpp`

One expression case was handled manually: `pdgui_menu_solomission.cpp:1835`
contained `pdguiScale(38.0f * 5.0f)` and was rewritten as
`pdguiScale(57.0f * 5.0f)` (row height 38→57, count unchanged).

### Files NOT touched (different scaling convention)

- `pdgui_hud.cpp` — in-world HUD uses `winW / 640.0f` (game-viewport
  scaling, N64 480p framebuffer convention).  Unrelated to menu scaling.
- `pdgui_lobby.cpp` dedicated-server overlay — uses `winH / 480.0f`
  (game-viewport scaling).  Unrelated to menu scaling.
- `pdgui_menu_matchsetup.cpp.retired` — retired file, not compiled.
- `pdgui_charpreview.c` / `pdgui_model_preview.cpp` — internal FBO sizes and
  GL coordinates, not menu layout pixels.  No `pdguiScale()` calls.

## Validation

### Visual preservation (what we tested)

Because every literal was multiplied by the exact ratio `1080/720 = 1.5`, the
product `literal * scaleFactor` produces the same pixel value as before the
flip at every resolution:

  Before: `literal_720 * (displayH / 720)`
  After : `(literal_720 * 1.5) * (displayH / 1080)`
        = `literal_720 * displayH / (1080 / 1.5)`
        = `literal_720 * displayH / 720`
        = same as before

This is a pure rename of the reference resolution, not a visual retuning.
Menus look identical at every supported resolution; only the literal values
in source code change to match the d5 spec convention.

### Batch 0 primitives re-verified

The action bar primitive and popup scrim were verified under the 1080p
baseline:

- `pdguiActionBarHeight()` returns the same pixel count as before at every
  supported resolution (84 @ 1080p = 56 * 1.5 ; 56 @ 720p = 84 * 0.667).
- `pdguiBodyHeightForActionBar(avail)` reserves the same body-vs-bar split.
- Mission Select, Mission Difficulty, and Challenges docking screens from
  S192 were re-verified against the flipped baseline — all pixel values
  move together, so the docked-CTA layout remains intact.

### Build

Verified via `devtools/build-headless.ps1` — see S193 session log for the
client binary size delta.

## Rules for New Code

- **Always** use `pdguiScale(N.Nf)` (wrapped) for pixel sizes in menus.
- **Use the 1080p value from the d5 table** as the argument — not a hand-tuned
  value that looks right at 720p.
- **Never** compare `DisplaySize.y` against a literal without dividing by
  `PDGUI_REF_HEIGHT`.
- **Never** add raw pixel fallbacks like `disp.y > 0 ? disp.y : 720.0f` — use
  `PDGUI_REF_HEIGHT` or `PDGUI_REF_WIDTH` from `pdgui_scaling.h`.
- Prefer named tiers from the d5 table (24 for body, 36 for headings, 64 for
  button height, 16 for padding, 8 for spacing, etc.) over ad-hoc tuning.

## Out-of-Scope Follow-ups

- Unwrapped decorative offsets (e.g. `pos.x + 8.0f` glow insets) still exist
  in a few menu files.  These render at a fixed pixel size regardless of
  resolution and should eventually be wrapped in `pdguiScale()`.  They are
  small enough that the visual error is not a blocker.  Audit opportunistic.
- A `pdguiScaleX()` / `pdguiScaleY()` pair (separate horizontal/vertical
  scale factors) for aspect-ratio-independent layout is contemplated in the
  d5 ultrawide section but not yet implemented.  Current code uses
  height-only scaling for all dimensions.
- `pdgui_hud.cpp` and the dedicated server overlay in `pdgui_lobby.cpp`
  should eventually be unified under a consistent viewport scaling helper.
  Out of scope for this session.
