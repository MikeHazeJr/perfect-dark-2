# Classic Perfect Dark on bgfx's OpenGL backend

2.0 renderer, phase 2a step 1: decision D5 and refactor R10 in
[renderer-2.0-scope.md](renderer-2.0-scope.md). Branch `week-pd/bgfx-classic`
(worktree `D:\PD2-wt\bgfx-classic`), started 2026-11-16. Not merged into dev.

**Goal.** The classic renderer runs on bgfx's OpenGL 4.3 backend and is
pixel-identical to the 1.0 GL renderer (`port/fast3d/gfx_opengl.cpp`), which
stays the default. bgfx is chosen at launch with a flag. Nothing about the
classic look changes.

**Status.** Built and audited. The results of the in-game checks are in
[Results](#results).

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
  rectangles convert from GL's bottom-left origin.
- **Framebuffer 0.** The 1.0 renderer draws the game straight into the window.
  Here framebuffer 0 is an offscreen RGBA8 target with a D24S8 depth, so the
  game can still copy from it (motion blur, blurred menu backgrounds).
  `gfxBgfxResetForOverlay` copies it to the window with a `texelFetch` shader
  before ImGui draws.
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
  the GL backend's depth clamp.
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
| Scenario scene renderer (`scenario_scene_renderer.cpp`) | `scenario_scene_renderer_bgfx.cpp` draws the same vertex buffer and textures with `vs_pdscene.sc`/`fs_pdscene.sc` (the same arithmetic as the GL shader) and the same state, as views placed in fast3d's sequence where the GL path draws. Scene loading is shared. |
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

- A viewport or scissor outside bgfx's 16-bit range is clamped.
- MSAA sample counts round to a power of two (bgfx's levels).
- The debug wireframe toggle is global in bgfx, per draw in GL (debug only).
- A copy from framebuffer 0's front buffer copies the frame in progress; the
  game always asks for the back buffer.
- A copy from a multisampled source with a flip or offset is skipped, as GL
  rejects it (`GL_INVALID_OPERATION`).
- `set_depth_range` other than 0..1 is ignored; fast3d never calls it.
- `G_TX_MIRROR | G_TX_CLAMP` samples as mirrored repeat; fast3d moves mirrored
  clamps into the shader, so it is not reached.
- More than 4096 views in a frame share the last view.

## Results

Recorded after the game-runtime turn; see the session log entry for the run.

## Next steps (phase 2a, from section 11 of the scope doc)

1. D3D11 as the Windows default, GL 4.3 as the fallback, Vulkan as an option.
   That needs: HLSL shaders from the same `.sc` sources (shaderc's HLSL path
   needs fxc or DXC as a host tool); SDL windows without a GL context; the
   AGS and NVAPI loads patched out of `renderer_d3d11.cpp` (D6); and swarm
   compute ported to bgfx compute or kept GL-only.
2. Remove the 1.0 GL backend once parity holds on every R9 capture.
3. Split-screen parity once split-screen exists (today a single local player
   is the active constraint, so no split-screen frame can be drawn).
