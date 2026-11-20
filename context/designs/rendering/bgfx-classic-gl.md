# Classic Perfect Dark on bgfx's OpenGL backend

2.0 renderer, phase 2a step 1: decision D5 and refactor R10 in
[renderer-2.0-scope.md](renderer-2.0-scope.md). Branch `week-pd/bgfx-classic`
(worktree `D:\PD2-wt\bgfx-classic`), started 2026-11-16, on dev since
2026-11-19 (merge `a1751113`, PR #9). The next session starts from
[renderer-2.0-handoff.md](renderer-2.0-handoff.md), which also lists what the
whole 2.0 renderer needs to be finished.

**Goal.** The classic renderer runs on bgfx's OpenGL 4.3 backend and is
pixel-identical to the 1.0 GL renderer (`port/fast3d/gfx_opengl.cpp`), which
stays the default. bgfx is chosen at launch with a flag. Nothing about the
classic look changes.

**Status (2026-11-20).** On dev, opt-in. Built, audited and run in game; the
parity check works in one process on menus, a solo stage and a Combat
Simulator match. The landing proof (2026-11-19, shared base 10) passed all
seven renderer smokes; the remaining parity differences wait for Mike's
verdict. See "Results (2026-11-19, landing proof)" below; the 2026-11-17
results are kept as history.

## Running it

| Flag | Effect |
|---|---|
| (none) | The 1.0 GL renderer, unchanged. bgfx is linked but never initialised. |
| `--renderer bgfx` | fast3d, the scene renderer, ImGui and the pdgui textures draw through bgfx's GL 4.3 backend on SDL's GL context. |
| `--renderer-parity` | The 1.0 GL renderer presents; bgfx draws the same frames offscreen on a second GL context. Each smoke screenshot compares the two frames ([Parity](#parity-mode)). |
| `--debug-bgfx-assume-gl 3.3` | Treats the context as that OpenGL version, to test the GL 4.3 fallback on a newer driver. |

The dedicated server window always uses the 1.0 renderer (`port/src/video.c`).

Smoke fixtures (`tools/smoke-verify/tests/`):

| Fixture | Checks |
|---|---|
| `renderer_parity_menu_smoke` | Parity on Agent Select and the Main Menu (ImGui over the 3D menu background). |
| `renderer_parity_solo_smoke` | Parity in first-person play on Defection. |
| `renderer_parity_mp_smoke` | Parity in a Felicity Combat Simulator match with two simulants, one in view. |
| `renderer_bgfx_boot_smoke` | bgfx presenting on its own: Agent Select and the Main Menu. |
| `renderer_bgfx_swarm_gpu_smoke` | The GPU swarm compute on bgfx's GL context. |
| `renderer_bgfx_gl_fallback_smoke` | `--renderer bgfx` on a "3.3" context logs why and runs the 1.0 renderer. |
| `renderer_classic_default_smoke` | No flag: no bgfx code path runs or logs. |

Captures and parity outputs are written in the run's install copy and kept by
`retain_artifacts` in the smoke receipts, never in git (BYOR).

## Build

### Sources and pins

`third_party/bgfx/{bx,bimg,bgfx}` are shallow submodules pinned to the trio
that bgfx.cmake v1.139.9155-513 tests together:

| Repo | Commit |
|---|---|
| bx | `36ad6131f4d292f62db64236d1d7f9f70eb26b13` |
| bimg | `b43fea9eae0e6a98118454a6d17c6cb25f5e6403` |
| bgfx | `651c2031169bae79d81d5035295e1a4b886797ba` |

The first configure fetches them if they are missing
(`git submodule update --init --depth 1 -- third_party/bgfx/bx third_party/bgfx/bimg third_party/bgfx/bgfx`);
CI runs that command after checkout.

### Glue (`third_party/bgfx-pd/CMakeLists.txt`)

Our own CMake, not bgfx.cmake. The top-level `CMakeLists.txt` adds it before
the game's global definitions, include directories and warnings, so
`-DVERSION=...`, the `include/sys` folder and `-Werror`-style flags never reach
third-party code. Everything is built `-O2 -g1 -w -fno-strict-aliasing`.

- `pd_bx`, `pd_bimg`, `pd_bgfx`: static libraries. bgfx is configured with
  `BGFX_CONFIG_RENDERER_OPENGL=43` and no other renderer,
  `BGFX_CONFIG_MULTITHREADED=0`, `BX_CONFIG_DEBUG=0`, no debug annotations,
  no profiler, `BGFX_CONFIG_PREFER_DISCRETE_GPU=0` (the client already exports
  `NvOptimusEnablement` and `AmdPowerXpressRequestHighPerformance` from
  `port/src/system.c`), up to 4096 views, 16384 textures and 256 frame
  buffers. The other renderers' sources still compile, as bgfx's stubs.
- Left out of the build: `nvapi.cpp`, `dxgi.cpp` and `debug_renderdoc.cpp`,
  replaced by `renderdoc_stub.cpp`. AMD AGS is only in `renderer_d3d11.cpp`,
  which is a stub in this build. So the client contains no code that loads
  `renderdoc.dll`, `nvapi64.dll`, `amd_ags_x64.dll` or the Nsight Aftermath
  library (D6).
- `pd-shaderc`: a lean shaderc host tool with only the GLSL path (the fcpp
  preprocessor; no glslang, glsl-optimizer, SPIR-V, Metal, HLSL or WGSL). For
  `-p 430` shaderc passes the preprocessed GLSL through unchanged, so no
  optimizer touches the shader arithmetic. `shaderc_lean.cpp` supplies the
  allocator and a `compileGLSLShader` that refuses GLSL below 4.00.
- `pd_bgfx_compile_shaders()`: compiles the `.sc` shaders in
  `port/fast3d/shaders/` at build time into C arrays
  (`<build>/generated/bgfx_shaders/<name>.bin.h`, symbol `<name>_glsl`).
  Nothing compiles GLSL at runtime; bgfx hands the stored text to the driver.

### Patches

Upstream files stay untouched. A patched file is read, changed at exact
anchors (configure fails if an anchor is missing or not unique) and written to
`<build>/third_party/bgfx-pd/patched/`, and that copy is compiled instead:

1. `renderer_gl.cpp`: a state bit, `PD_BGFX_STATE_POLYGON_OFFSET`
   (`port/fast3d/gfx_bgfx_ext.h`), that turns on `glPolygonOffset(-2, -2)` for
   decal draws as the GL backend does. bgfx has no polygon-offset state.
2. `bx/src/debug.cpp`: bx built its dbghelp symbol resolver as a
   namespace-scope static, so merely linking bx loaded `dbghelp.dll` and
   called `SymInitialize` with the Microsoft symbol-server path before
   `main()`, in every renderer mode and beside the game's own crash handler.
   The patched copy builds it on first use and never gives dbghelp a
   symbol-server path. Its only caller is bx's assert handler, which
   `BX_CONFIG_DEBUG=0` compiles out.

### Dependency review (D6)

The pd link applies the write-policy `--wrap` list to every object, bgfx's
included, so wrapped routes (fopen, fwrite, `_mkdir`, CreateProcessA, ...) are
enforced in bgfx too. `tools/native_write_policy_guard.py` only scans the `pd`
target's sources, so `tools/bgfx_dependency_audit.py` reviews the rest, as the
guard's notes require for a new dependency:

- Source review: the 253 files compiled into or included by pd_bgfx, pd_bimg
  and pd_bx. Every file with an unwrapped write, process or library-load route
  (31 files) has a reviewed entry bound to its call-context fingerprint, with
  the reason. Most hits are bx's own stream API named `write`/`open`; the rest
  are branches compiled out for this configuration (other platforms, other
  renderers, `BX_CRT_NONE`, the profiler). The loads that remain on Windows:
  `opengl32.dll` (already loaded by SDL), `RtlGetVersion` from the loaded
  ntdll, and the unreachable assert-path resolver. Nsight detection only lists
  loaded modules.
- Binary review (`--exe`, optional `--baseline-exe`): no forbidden name
  (RenderDoc, NVAPI, AGS, Aftermath, Superluminal's `PerformanceAPI.dll`,
  `msdl.microsoft.com`) anywhere in the client; the import table has only
  Windows system DLLs and, against a classic build, no new DLL; every DLL name
  inside the bgfx archives is one of the reviewed five. The D3D, DXGI and
  Vulkan DLL names in the client come from the static SDL2, as in the classic
  build.

CI runs the audit after the build. A bgfx update changes the fingerprints and
fails the audit until the new call sites are reviewed.

## How the backend maps fast3d onto bgfx

`port/fast3d/gfx_bgfx.cpp` implements fast3d's `GfxRenderingAPI` table beside
`gfx_opengl.cpp`. `gfx_init` asks `gfxBgfxSelectRenderingApi` for the backend
once the GL context exists.

- **Context and frame.** bgfx renders through the GL context SDL created
  (`PlatformData` with the context and no window handle), single-threaded
  (`bgfx::renderFrame()` before `bgfx::init`), and never swaps or makes a
  context current. `gfxBgfxSubmitFrame()` runs `bgfx::frame()` at the start of
  `gfx_sdl_swap_buffers_begin`, then SDL captures and swaps as before.
- **Views.** bgfx records draws and executes the frame in `bgfx::frame()`.
  Every view is sequential, so draws keep submission order. A new view opens
  when fast3d changes render target or viewport, and for every clear and
  framebuffer copy, which bgfx can only do at the start of a view. View
  rectangles convert from GL's bottom-left origin. bgfx keeps a view's
  settings from frame to frame, so every view is reset when it is allocated,
  and view ids restart after `bgfx::frame()` (bgfx's frame boundary; pdgui
  can issue work before fast3d's `start_frame`). The ImGui view opens when
  ImGui first draws, so a pdgui bake made while ImGui builds its frame runs
  before ImGui samples it, as the GL backend's immediate bake does.
- **Framebuffer 0.** The 1.0 renderer draws the game straight into the window.
  Here framebuffer 0 is an offscreen RGBA8 target with a D24S8 depth, so the
  game can still copy from it (motion blur, blurred menu backgrounds).
  `gfxBgfxResetForOverlay` copies it with a `texelFetch` shader into a
  window-sized final target, ImGui draws there, and the finished frame is
  copied to the window just before `bgfx::frame()`. The final target plays
  the window's front buffer: the game copies `GL_FRONT` (the last presented
  frame) at Agent Select, and that copy runs, in view order, before this
  frame's present, so it reads the previous finished frame, as GL does.
  Render-target flags are one 3-bit field
  (`BGFX_TEXTURE_RT` is value 1, the MSAA modes the rest), so colour and
  depth use `RT` or the MSAA value, never both; a depth target flagged only
  `RT_WRITE_ONLY` would get no storage at all in bgfx's GL renderer. A frame
  buffer bgfx refuses is logged once and its views draw into a 1x1 sink.
- **Textures.** Every upload creates a new bgfx texture: bgfx creates
  resources before a frame's draws and destroys them after, so a draw keeps
  the pixels it was submitted with even when fast3d reuses the id mid-frame.
  Sampler state is stored per texture object, as GL texture parameters are.
- **State.** Depth: `INTER` is `LEQUAL`; opaque and translucent use `LEQUAL`
  with a primitive depth source, else `LESS`; `DEC` adds the polygon-offset
  bit; no depth test means no depth writes, as in GL. Blend: additive
  `(SRC_ALPHA, ONE)`, modulate `(DST_COLOR, ZERO)`, otherwise
  `(SRC_ALPHA, INV_SRC_ALPHA)`. `BGFX_STATE_MSAA` is always set
  (`GL_MULTISAMPLE` is on by default), and `BGFX_RESET_DEPTH_CLAMP` matches
  the GL backend's depth clamp. Scissor follows the GL backend's state, not
  just its box: `set_scissor` moves the box, the test is off after
  `reset_for_overlay` and on again after a clear, MSAA resolve or copy. An
  unchanged scissor rect reuses its entry in bgfx's per-frame rect cache.
- **Per-program uniforms.** In GL the frame count, noise scale and the two
  three-point "linear" flags are uniforms of each program, set only when
  fast3d loads it (`gfx_opengl_set_uniforms`). The bgfx path takes the same
  snapshot at the same points and draws with it, not with live values.
- **Uniform creation order.** For GLSL 4.30 shaderc stores no uniform table,
  and bgfx's GL renderer binds a program's uniforms by name when the program
  is created, so every uniform is created before its program.
- **Colour combiner.** The 1.0 renderer writes one GLSL program per combiner
  state at runtime (`gfx_opengl_create_and_load_new_shader`). bgfx loads only
  precompiled shaders, so `fs_fast3d.sc` is one uber-shader driven by
  uniforms. Operands are selected by their `gfx_cc.h` item code (selection
  moves values and does no arithmetic). Each formula is written once per
  "shape" (12 of them: `D`, `A*C`, the six `mix` forms, `(A-B)*C` plus 0, 1 or
  `D`, and `(A-B)+1`) with the same literal constants as the generated text,
  and the first add of the WRAP that follows each cycle sits inside the same
  expression. That keeps the driver's constant folding and multiply-add
  fusion the same as for the generated shader. Fog, texture edge, noise,
  grayscale, alpha threshold and invisibility follow in the legacy order.
- **Debug views.** The mesh debug collision view (`meshdebug.c`, raw GL)
  logs once that it is unavailable with `--renderer bgfx`.

## The other raw-GL users

| User | bgfx mode |
|---|---|
| Scenario scene renderer (`scenario_scene_renderer.cpp`) | `scenario_scene_renderer_bgfx.cpp` draws the same vertices and textures with `vs_pdscene.sc`/`fs_pdscene.sc` (the GL shader's arithmetic line for line: B-1172's sky offset, roomHighlight's room lighting and the stage fog) and the same state, as views placed in fast3d's sequence where the GL path draws: the viewport, scissor, camera, light slot and passes of each `G_SCENE_RENDER_EXT` command. Scene loading, the sky flags and the animated surfaces' UVs are shared; each frame's changed vertices go to both backends. The vertices sit in a dynamic buffer with the room as a float (bgfx has no 32-bit integer attributes; the value equals GL's conversion of the integer). Each pass gets its own room lighting texture, because bgfx applies texture updates before a frame's first view and one texture updated per view would light every view like the last. Grid layout revisions prepare bgfx resources too. |
| ImGui | `imgui_impl_bgfx.cpp`, chosen by `pdgui_render_backend.cpp`: the same matrix, separate blend, scissor rounding and viewport as `imgui_impl_opengl3.cpp`. ImGui texture ids are tagged integers `(index << 3) | kind` resolved at draw time. |
| pdgui textures (theme, character portrait, skin canvas, skin editor, modding hub preview, character preview bake, skin capture) | Each GL branch is unchanged; a `gfxBgfxActive()` branch uses `gfxBgfxPdguiTex*` with the same filtering and wrap. |
| GPU swarm (`swarm_gpu.cpp`) | Unchanged. It only computes (dispatch, then CPU readback) and draws nothing, between frames on the same context; bgfx sets all the state it needs at the start of each frame. Its pixel-store resets do not matter to bgfx's RGBA8 uploads. |
| Mesh debug (`meshdebug.c`) | Reports that it is unavailable (developer view). |
| Dedicated server window | 1.0 renderer. |

## OpenGL 4.3

bgfx's GL renderer is built for GL 4.3. `gfxBgfxSelectRenderingApi` reads the
context version after glad loads; below 4.3 it logs
`bgfx: the bgfx renderer needs OpenGL 4.3, but this context is OpenGL X.Y (...); using the classic OpenGL renderer`
and returns `gfx_opengl_api`, before any bgfx call. `pdguiInit` runs after
`gfx_init`, so ImGui and pdgui then take their GL paths. On D3D11 (phase 2a
step 2) the fallback becomes the GL 4.3 backend instead (D5).

## Parity mode

Two separate runs never produce the same frame (time-based ImGui animation,
AI, random numbers), so parity draws one frame's commands through both
backends in one process:

- `gfx_tee.cpp` is a backend that forwards every call to `gfx_opengl_api` and
  `gfx_bgfx_api`. The GL backend presents to the window as usual. bgfx renders
  on its own GL context, which shares no objects with the main one, into an
  offscreen final target, so the two cannot disturb each other's GL state. Texture and program ids
  returned to fast3d are the GL backend's; the tee maps them to bgfx's.
  Framebuffer ids are equal in both backends.
- ImGui draws through both. Textures arrive as GL names: a framebuffer colour
  maps to bgfx's copy of that framebuffer, anything else is imported once per
  frame with `glGetTexImage` on the main context.
- The scene renderer draws through both (it checks `gfxBgfxRunning()`).
- On a smoke screenshot event, bgfx reads back its finished frame (a
  `readTexture` issued before `bgfx::frame()`), and the GL capture path then
  calls `gfxParityCompare` with both images (`port/fast3d/gfx_parity.cpp`). It
  writes `<shot>.bgfx.bmp`, `<shot>.diff.bmp` (absolute difference x64) and
  `<shot>.parity.json` (`schema pd2.renderer-parity.v1`: differing pixels,
  largest channel difference, a histogram and the bounding box), and logs
  `PARITY: <shot> size=WxH differing=N maxdelta=D hist(1,2-3,4-15,16+)=... bbox=...`.

Parity checks bgfx against the 1.0 renderer on the same driver and GPU, which
is the D5 condition. It does not check another GPU vendor; a run on AMD or
Intel hardware would.

## Known differences, if they are ever reached

Each logs once when it happens, so a parity run shows whether it was reached:

- A viewport with a negative origin is clamped to the target. GL keeps the
  full transform and clips; bgfx's clamped rect rescales the geometry
  instead. Emulating GL would need a clip-space offset and scale in
  `vs_fast3d.sc` (identity, so exact, in the normal case). A viewport or
  scissor past bgfx's 16-bit range is clamped too.
- The debug wireframe toggle (developer only) is global in bgfx, so it also
  draws the present quad and ImGui as lines.
- MSAA sample counts round to a power of two (bgfx's levels).
- A front-buffer copy before any frame was presented copies the frame in
  progress (GL's front buffer is undefined then).
- A copy from a multisampled source with a flip or offset is skipped, as GL
  rejects it (`GL_INVALID_OPERATION`).
- `set_depth_range` other than 0..1 is ignored; fast3d never calls it.
- `G_TX_MIRROR | G_TX_CLAMP` samples as mirrored repeat; fast3d moves mirrored
  clamps into the shader, so it is not reached.
- More than 4096 views in a frame share the last view.

## Results (2026-11-17)

### Build and checks

- `devtools/build-session.ps1` built `pd` and `pd-tests` at 51acf115 with
  exit 0; pd-tests passed all 1,918 test cases (384,502 assertions).
- Hosted CI on draft PR #9 (run 44013926966, GitHub's merge of 5bc1608e with
  dev 8f41913f) passed every step: the bgfx submodules were fetched at the
  pins, `pd` and `pd-tests` built, the write-policy guard, the bgfx
  dependency audit, the SP-75 bool layout check, and 2,028 test cases
  (404,493 assertions).
- `tools/native_write_policy_guard.py` passed. `tools/bgfx_dependency_audit.py`
  with `--exe` and `--baseline-exe` passed: 31 reviewed files and no
  failures; no RenderDoc, NVAPI, AGS, Aftermath, Superluminal or
  symbol-server string in the client; the import table is the same 19
  Windows system DLLs as a classic build's.
- The bool layout audit cannot spawn the preprocessor from this machine's
  agent shell (every unit reports a preprocessing error), so CI's run is the
  evidence for it.

### In-game runs

One game-runtime turn (05:01 to 05:56 UTC), binary 51acf115 frozen into
`.claude/smoke-ref`, a D: copy of shared base 4, 2560x1440 window:

| Smoke | Result |
|---|---|
| `renderer_bgfx_gl_fallback_smoke` | Pass 12/12: the GL 4.3 check logged why and the classic renderer ran. |
| `renderer_parity_menu_smoke` | Pass 13/13 (pixels below). |
| `renderer_parity_solo_smoke` | Pass 14/14. |
| `renderer_parity_mp_smoke` | Pass 14/14. |
| `renderer_bgfx_swarm_gpu_smoke` | The swarm's compute shader ran on bgfx's context (8, then 16 boids at about 57 ticks a second), the window was captured and the game exited cleanly; it failed only an assertion for a 32-boid level the run's timing did not reach (fixed). |
| `renderer_classic_default_smoke`, `renderer_bgfx_boot_smoke` | Both reached Agent Select; in bgfx mode the Carrington Institute scene was drawn through bgfx with no backend warning but the front-buffer copy below. Both failed only their wait: `agent_select_ready` needs keyboard focus, which a runner in the background does not get. The fixtures now wait for `core_boot_ready`. With no flag, nothing from the bgfx code path appeared in the log. |

### Parity

Differing pixels out of 3,686,400 per capture:

| Scene | Captures | Differing | Where and why |
|---|---|---|---|
| Agent Select | 2 | 68,712; 68,784 | 67,496 are the screen thumbnail at the bottom left, which the game copies from `GL_FRONT` (the last presented frame). The run's binary copied the frame in progress instead; fixed afterwards (the final target now plays the front buffer). About 1,200 more, max delta 4, lie along the thumbnail's filtered edge and in the translucent panel; same source. Not yet re-run. |
| Defection | 3 | 89; 95; 82 | About 98% differ by 1 in one channel, scattered over textured surfaces; one or two pixels per frame differ by up to 31, at edges. |
| Felicity Combat Simulator | 3 | 430; 379; 503 | About 97% differ by 1, mostly over the room surfaces drawn by the scene renderer; up to six pixels per frame differ by 4 to 48, on the first-person gun's edges. |
| Split-screen | 0 | none | Not drawable: one local player only (`context/constraints.md`, no split-screen; the 2P dialogs are dead code). |

Gameplay cause, a working explanation not yet proven: floating-point
rounding. The two renderers run arithmetically identical shaders, but the
driver compiles differently written source (the uber-shader against the
per-combiner generated GLSL; the scene shader as GLSL 4.30 against the
classic one's GLSL 1.30), and its fused multiply-add choices can differ in
the last bit. That flips the rare 8-bit rounding (the scattered 1s) and, at
alpha-tested or triangle edges, a discard or coverage decision (the few large
deltas). Checked and the same on both sides: driver and GPU, the depth
format (24-bit with 8-bit stencil), the scene textures' bytes and filtering
(linear, no mipmaps) and the dither state.

A next experiment can settle it: bgfx's GL renderer compiles shader text
that starts with `#version` verbatim, so the scene shader can be given the
classic GLSL 1.30 text (with uniforms bgfx can bind) and the Felicity
capture compared again.

## Dev merge (2026-11-19)

Merging dev 730aa8bb into the branch brought CORE's B-1172 scene renderer:
the scene now draws at its display-list command into each player's view,
with a 52-byte vertex (a sky flag and an integer room), sky rooms drawn
relative to the camera's room, roomHighlight's lighting from a per-view room
texture, the stage fog, animated surfaces and Grid layout swaps. The bgfx
scene path was rebuilt to match (the table above); the classic GL path is
dev's, with two refactors that change nothing it draws: the room lighting
choice moved into `viewRoomLights`, and the sky and animated-surface vertex
updates are computed once per draw for both backends before the GL upload.
`gfxBgfxApplyScissor` gives the scene's bgfx draws fast3d's scissor, which
dev's call site now sets before the scene draws.

## Results (2026-11-19, landing proof)

One game-runtime turn (q_20261119191534312_82e5bc62, 20:49 to 21:49 UTC) on
the branch merged with dev fdea04a9 (CORE's slice 5), shared base 10, a
2560x1440 window. Its client is stamped ac84c3bc. Dev 92200bbd was merged
into the worktree while that client built (a mistake: the turn started
earlier than its queue position suggested). The one compiled file that merge
changed, `port/src/smoke_readiness.c`, was compiled after it, so every
compiled source is the 66878814 tree's, the tree hosted CI built green (run
44613111811, 2,525 cases).

- `devtools/build-session.ps1` built the client and pd-tests with exit 0;
  pd-tests passed 2,525 cases (570,422 assertions); the write-policy guard
  and the bgfx dependency audit passed.

| Smoke | Result |
|---|---|
| `renderer_classic_default_smoke` | Pass 10/10 (no flag: no `bgfx:` or `PARITY:` line in the log) |
| `renderer_bgfx_boot_smoke` | Pass 12/12 |
| `renderer_parity_solo_smoke` | Pass 14/14 (the scene drawn through bgfx with dev's room lighting: 168 rooms) |
| `renderer_parity_mp_smoke` | Pass 14/14 |
| `renderer_parity_menu_smoke` | Pass 13/13 |
| `renderer_bgfx_gl_fallback_smoke` | Pass 12/12 |
| `renderer_bgfx_swarm_gpu_smoke` | Pass 15/15 |

Parity, differing pixels out of 3,686,400 per capture (deltas 1, 2-3, 4-15,
16 and more):

| Scene | Differing | Max delta | Histogram | Where and why |
|---|---|---|---|---|
| Agent Select | 4,630; 4,581 | 8; 8 | 3,305/1,162/163/0; 3,256/1,162/163/0 | The front-buffer fix removed the thumbnail difference of 11-17 (68,712). About 4,200 of the rest lie in one round, noise-like patch in the bright part of the menu's screen thumbnail (the small TV-style picture at the bottom left); about 100 more are scattered over the screen. Cause not proven. Leading guess: a noise term in what the thumbnail draws (a `sin()`-based hash, which turns a last-bit difference between the uber-shader and the generated shader into a different value); a parity run with the noise term held constant would confirm or rule it out. |
| Defection | 264; 448; 180 | 15; 14; 1 | 199/22/43/0; 328/80/40/0; 180/0/0/0 | Scattered over the room surfaces. More than on 11-17 (82 to 95), when the scene had no room lighting yet. Working explanation, not proven: dev's room lighting rounds with `floor()` at several steps, where a last-bit difference between the two compiled shaders becomes a whole level. |
| Felicity Combat Simulator | 158; 172; 193 | 52; 1; 1 | 153/0/0/5; 172/0/0/0; 193/0/0/0 | Fewer than on 11-17 (379 to 503); five pixels of one capture differ by 16 or more, at edges, as before. |
| Split-screen | none | | | Not drawable: one local player (`context/constraints.md`). |

The captures, bgfx images and difference images stay in the worktree's smoke
storage (not in git).

## Next steps (phase 2a, from section 11 of the scope doc)

1. D3D11 as the Windows default, GL 4.3 as the fallback, Vulkan as an option.
   That needs: HLSL shaders from the same `.sc` sources (shaderc's HLSL path
   needs fxc or DXC as a host tool); SDL windows without a GL context; the
   AGS and NVAPI loads patched out of `renderer_d3d11.cpp` (D6); and swarm
   compute ported to bgfx compute or kept GL-only.
2. Remove the 1.0 GL backend once parity holds on every R9 capture.
3. Split-screen parity once split-screen exists (today a single local player
   is the active constraint, so no split-screen frame can be drawn).

The full list, with the other phases, the open decisions and the requests
from other lanes, is in [renderer-2.0-handoff.md](renderer-2.0-handoff.md).
