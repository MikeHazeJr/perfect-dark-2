# B6 Polish -- Live 3D Head/Body Preview in Simulant Character Dialog

**Date:** 2026-04-11
**Worktree:** `.claude/worktrees/busy-lalande` on branch `claude/busy-lalande`
**Parent dev state:** a9a93e8d (Batch 7 merge, client 49,566,481 / server 22,771,518)

## Task

Restore the legacy 3D live preview for the character/head selection in the
Batch 6 ImGui Simulant Character dialog (`g_MpSimulantCharacterMenuDialog`
-> `renderMpSimulantCharacter`). Batch 6 dropped the 3D preview as an
intentional simplification on the grounds that wiring it up was scope creep.
Mike's direction is to do it now rather than waiting for Batch 11.

Design goal: reusable plumbing, not a one-off for the simulant dialog, so
future preview surfaces (weapon preview, vehicle preview, character
creator polish) pick it up for free.

## Key discovery: the reusable helper already exists

The FBO-backed model preview system was added in D5 P3 Batch 0 (S192) and
is already shared across four call sites:

| File | Caller | Notes |
|------|--------|-------|
| `port/fast3d/pdgui_menu_agentcreate.cpp:441` | `pdguiModelPreviewDraw` | Same pattern I adopted for Simulant Character |
| `port/fast3d/pdgui_menu_agentselect.cpp:474-538` | low-level `pdguiCharPreviewRequest` + `GetTextureId` | Grid-of-thumbnails UI |
| `port/fast3d/pdgui_menu_room.cpp:2575-2607` | low-level + manual rotation tick | Lobby bot modal, right-side portrait |
| `port/fast3d/pdgui_menu_moddinghub.cpp` | low-level | Hub thumbnail surface |

Two layers:

1. **`port/fast3d/pdgui_charpreview.c` + `port/include/pdgui_charpreview.h`**
   - Low-level FBO render to texture.
   - `pdguiCharPreviewInit()` creates a 256x256 framebuffer at startup
     (called from `pdgui_backend.cpp:267` after gfx init via
     `videoCreateFramebuffer`; lifetime = process lifetime, no explicit
     free because it uses the shared video FBO pool).
   - `pdguiCharPreviewRequest(head_id, body_id)` writes
     `g_Menus[0].menumodel.newparams` + rotation and marks a render as
     pending.
   - `pdguiCharPreviewRenderGBI(gdl, menu)` injects GBI commands from
     `menuRenderDialog` in `src/game/menu.c:3754` to switch the render
     target to the preview FBO, draw the menu model, and switch back.
     This is the legacy preview renderer's output being captured -- we
     do not touch its logic, we just redirect the target.
   - `pdguiCharPreviewGetTextureId()` returns the GL texture bound to
     the FBO color attachment. Cached at init time -- constant ID.
   - Generalized in Batch 0 to support PDGUI_PREVIEW_CHARACTER /
     WEAPON / VEHICLE / PROP via `pdguiCharPreviewRequestEx`.

2. **`port/fast3d/pdgui_model_preview.cpp` + `port/include/pdgui_model_preview.h`**
   - High-level self-contained ImGui widget built on top of layer 1.
   - `pdguiModelPreviewDraw(head_id, body_id, x, y, w, h, opts)` handles:
     - Selection-change detection (re-request on change, else idle
       rotate)
     - Idle rotation (reset on change, `opts.idleRotSpeed` radians/sec)
     - Background frame + border from the PD palette
     - ImGui::Image with Y-flipped UVs (FBO textures are upside down
       relative to ImGui)
     - Fallback placeholder silhouette when the FBO is not yet ready
     - Optional name labels beneath the model
   - Already type-parameterized via `ModelPreviewKind`.
   - Single static rotation slot (`s_IdleAngle`) -- fine for the
     current "one modal preview at a time" use pattern; the comment in
     the cpp explicitly flags this and points at the upgrade path if a
     future multi-panel screen needs independent states.

So the entire "build reusable FB viewport plumbing" concern from the task
brief is already handled by pre-existing infra. This polish reduces to
**one call site** inside `renderMpSimulantCharacter`.

## What changed in pdgui_menu_botsetup.cpp

File: `port/fast3d/pdgui_menu_botsetup.cpp`

1. Added `#include "pdgui_model_preview.h"` next to the other `pdgui_*`
   headers (outside the `extern "C"` block -- it is itself a C++-compatible
   header that does not pull in `types.h`).

2. Added `const char *catalogMpBodyId(s32 mpbodynum);` to the extern "C"
   block alongside the existing `catalogMpHeadId` declaration.

3. Rewrote `renderMpSimulantCharacter`:
   - Dialog size bumped from `0.55 x 0.58` to `0.62 x 0.62` to make room
     for the preview without squeezing the dropdowns.
   - Body is now a two-column `SameLine` layout:
     - **Left column**: 300x340 (1080p baseline, `pdguiScale`d) 3D
       preview via `pdguiModelPreviewDraw`. Selection is read from the
       existing `car_GetSelectedIndex` carousel accessors and resolved
       to catalog ID strings via `catalogMpHeadId` / `catalogMpBodyId`.
       Layout space reserved via `ImGui::Dummy(previewW, previewH)`
       because the widget draws into the window draw list without
       advancing the ImGui cursor.
     - **Right column**: the original Head + Body dropdowns, wrapped in
       `BeginGroup`/`EndGroup`, with a small `Dummy` Y-offset to
       vertically center the picker stack against the taller preview
       panel.
   - All legacy state mutation still goes through
     `menuhandlerMpSimulantHead` / `menuhandlerMpSimulantBody` -> s204
     shadow struct -> `mpCharacterHeadMenuHandler` /
     `mpCharacterBodyMenuHandler` -> `mpchrSetHeadByIndex` /
     `mpchrSetBodyByIndex`. Zero function loss.

No other files touched. `pdgui_menu_solomission.cpp`,
`pdgui_menu_mpadvanced.cpp`, `pdgui_menu_mpsetup.cpp`,
`pdgui_menu_cheats.cpp`, `pdgui_menu_mainmenu.cpp` all untouched (Batch 8
collision guard).

## Render path walkthrough (per frame while dialog is open)

1. `pdguiHotswap` routes the legacy dialog render through
   `renderMpSimulantCharacter`.
2. `renderMpSimulantCharacter` reads the current carousel selection and
   calls `pdguiModelPreviewDraw(head_id, body_id, ...)`.
3. `pdguiModelPreviewDraw` detects that (kind, id1, id2) are stable
   frame-to-frame, advances `s_IdleAngle`, calls
   `pdguiCharPreviewSetRotY` + `pdguiCharPreviewRequest`. The request
   writes `g_Menus[0].menumodel.newparams`.
4. Later in the same frame, `src/game/menu.c:3754` calls
   `pdguiCharPreviewRenderGBI(gdl, menu)`. This injects GBI commands
   that:
   a. Switch the framebuffer target to the preview FBO
   b. Set viewport + scissor to the FBO size
   c. Call `menuRenderModel` (the legacy renderer) to draw the character
   d. Switch framebuffer target back to the main surface
5. `gfx_run_dl` executes those GBI commands, populating the preview
   texture before the ImGui overlay pass runs.
6. Back in `pdguiModelPreviewDraw`, the next frame's
   `ImDrawList->AddImage` call samples the populated texture with a
   Y-flipped UV rect (`(0,1)` -> `(1,0)`) and draws it into the
   window. The first frame after opening the dialog shows the
   placeholder silhouette; every subsequent frame shows live geometry.

## MENUOP_11 coexistence

The Batch 6 audit flagged `menudialog0017ccfc::MENUOP_TICK` as firing
via the legacy runtime and writing `g_Menus[0].menumodel.newparams`.
That tick still runs -- we do not suppress it -- but every frame
`pdguiModelPreviewDraw -> pdguiCharPreviewRequest` overwrites
`newparams` with the same head/body composite the tick would have
produced. No conflict; the preview simply takes the value from whichever
path wrote last, which is our per-frame request.

If the carousel selection changes, `pdguiModelPreviewDraw` sees the
(id1, id2) tuple change, resets the idle angle to 0, and issues a
fresh request. The preview updates on the very next frame.

## Build verification

Build directory: `.claude/b6h-build/` (in the worktree; Unix Makefiles,
MSYS2 MinGW GCC x86_64). Fresh configure, clean cache.

Configure:
```
TEMP=/tmp cmake -G "Unix Makefiles"
  -DCMAKE_MAKE_PROGRAM=/c/msys64/usr/bin/make.exe
  -DCMAKE_C_COMPILER=/c/msys64/mingw64/bin/cc.exe
  -DCMAKE_CXX_COMPILER=/c/msys64/mingw64/bin/g++.exe
  -DCMAKE_BUILD_TYPE=Release
  -B .claude/b6h-build -S .
```

Client:
```
TEMP=/tmp cmake --build .claude/b6h-build --target pd -j 4
```
`[100%] Built target pd`. `PerfectDark.exe` = 49,562,897 bytes.
Delta vs Batch 7 dev (49,566,481): -3,584 bytes, within the known
fresh-cache-vs-incremental link-layout variance Batch 7 saw on the
server side.

Server:
```
TEMP=/tmp cmake --build .claude/b6h-build --target pd-server -j 4
```
`[100%] Built target pd-server`. `PerfectDarkServer.exe` =
22,771,006 bytes. Delta vs Batch 7 dev (22,771,518): -512 bytes,
variance. `pdgui_menu_botsetup.cpp` is NOT in `SRC_SERVER` so the
server binary is unaffected by construction; the small delta is link
layout only.

Symbol sanity check on `pdgui_menu_botsetup.cpp.obj`:
```
_ZL25renderMpSimulantCharacter...  t   (defined)
catalogMpBodyId                    U   (undefined -- resolved at link)
catalogMpHeadId                    U   (undefined -- resolved at link)
pdguiModelPreviewDraw              U   (undefined -- resolved at link)
```
All three external references resolve cleanly at link time (otherwise
the link would have failed).

## Gotchas encountered

1. **Devkitpro cmake in PATH**: the initial configure picked up
   `/c/devkitPro/msys2/usr/bin/cmake` before MSYS2's own cmake and tried
   to use a devkitpro `cmake.exe` path that did not exist, which broke
   `CMakeTestCCompiler`. Fix: explicit `PATH="/c/msys64/mingw64/bin:/c/msys64/usr/bin:$PATH"`
   and explicit `/c/msys64/mingw64/bin/cmake.exe` invocation. Recording
   this here for the next session that runs a headless build from a
   shell without the MSYS2 dev-window environment already loaded.

2. **Worktree redirect in build-headless.ps1**: the official build script
   detects a `.claude/worktrees/...` path and redirects to the main
   working copy, which would have built the OLD unmodified file. Not
   useful for verifying a worktree in isolation before merge. Raw cmake
   invocation from inside the worktree works and is what Batch 7 used
   too (`.claude/b7-build`).

3. **pdgui_model_preview has a single static rotation slot**. If a
   future screen ever needs two independent preview panels on the same
   frame (e.g. compare-two-bodies layout), this needs to be promoted to
   a caller-keyed table. Existing comment in `pdgui_model_preview.cpp:42`
   already flags this. Not an issue for the modal Simulant Character
   dialog.

## Line-count check

`port/fast3d/pdgui_menu_botsetup.cpp`: 1006 -> 1074 (+68 lines).
Breakdown: +1 include, +1 extern decl, +66 lines net in
`renderMpSimulantCharacter` (most of which is the two-column wrapping
and block comment explaining the render path; the dropdown bodies
themselves are unchanged character-for-character).

No other file was modified.
