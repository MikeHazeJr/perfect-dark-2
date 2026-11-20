# Renderer 2.0: handoff and definition of done

Written 2026-11-20 by the session that built phase 2a step 1 (hub id
`claude-week-pd-bgfx-classic`), for the next session that continues the 2.0
renderer. State read at origin/dev `d43a7f72` (the canonical checkout's local
dev was `6e1fff4b`, ten local-only commits ahead).

The plan of record is [renderer-2.0-scope.md](renderer-2.0-scope.md) (the scope
doc); what exists is described in [bgfx-classic-gl.md](bgfx-classic-gl.md) (the
step 1 design doc). This file does not replace them: it says where things stand,
lists everything that has to be true for the renderer to be finished, and
collects what a fresh session would otherwise have to rediscover.

---

## 1. Start here

1. Read `AGENTS.md`, then the scoped startup checklist in `context/README.md`,
   then `context/constraints.md`. Then this file, the scope doc (at least
   sections 0, 1, 5, 6, 7, 8, 9, 10, 11 and 12) and the step 1 design doc.
2. Coordination: run the hub from the canonical checkout with `pwsh`
   (`Tools/CodexCoordination/CodexCoordination.ps1`, guide in
   `docs/CODEX_COORDINATION.md`). Reuse the coordination session id that the
   repository's lifecycle hook injects (Codex sessions with
   `.codex/hooks.json` trusted): the hook checks Workbench ownership against
   that identity, so a second one splits the coordination state and keeps
   edit tools blocked. Only a session with no hook-provided identity (a Claude
   Code session, for example) runs `status`, then `register` with a stable
   session id, goal, task and ETA (`AGENTS.md` standing order 3). Heartbeat at
   least every 20 minutes. Read the hub chat and the week note
   (`context/week-pd-coordination.md`, an untracked shared file in the
   canonical checkout) for other lanes' landings and the current shared smoke
   base.
3. Workbench (`http://127.0.0.1:8378`; follow `AGENTS.md` standing orders 1
   to 11): the 2.0 renderer item is **T-RENDERING-001** (created 2026-11-20).
   Mike's answers to the step 1 report are decisions **D-041** (parity:
   prove the causes, then accept the rest) and **D-042** (split-screen, with
   the renderer mode chosen per local player). Turn each question still open in
   section 4 into a decision item with two to four options; never hand-pick
   Workbench IDs. Commits: the commit-msg hook checks the item ID against the
   roadmap of the checkout it runs in. The canonical checkout's roadmap has
   T-RENDERING-001 (`Rendering - T-RENDERING-001: <summary>`, `Refs:
   T-RENDERING-001`); a worktree's tracked copy may predate it, so a worktree
   commit uses `Release - D-004: <summary>` with `Refs: D-004` (D-004 places
   2.0 after 1.0).
4. Exclusive resources (`AGENTS.md` order 7; T-TOOLING-004): builds go through
   the coordination queue and `devtools/build-session.ps1` (never `-NoQueue`,
   never shared `Build/`, never pd-server); pd-tests through the `test-runner`
   queue; the `game-runtime` slot only for a ready batch of game runs, released
   as soon as the game processes exit. Do not build or launch the game while
   another session's game run holds the slot.
5. Where to work. By default (`AGENTS.md`, "Codex live-session mode"),
   worktrees are disabled and a session works directly in the canonical
   checkout: claim exact Workbench item and file ownership of the surfaces
   before editing them, and take the serialized Git lane (source-freeze, then
   stage, commit and push only while it is yours). Step 1 ran in a worktree,
   `D:\PD2-wt\bgfx-classic`, because Mike directed it; a worktree is used only
   when Mike explicitly re-enables one for this lane, and then the canonical
   checkout is not edited at all. Either way the canonical checkout holds other
   lanes' uncommitted work and local-only commits: never switch its branch,
   never reset it, never run a bare `git stash` there, and leave files you do
   not own untouched.
6. Client launches by hand always pass `--portable` or `--savedir`. Deletions
   go to the Recycle Bin. Captures never go in git (BYOR).

---

## 2. Where it stands (2026-11-20)

### 2.1 Done: phase 2a step 1, on dev since 2026-11-19

Merge `a1751113` of `week-pd/bgfx-classic` (PR #9). Classic runs on bgfx's
OpenGL 4.3 backend behind `--renderer bgfx`; with no flag the 1.0 GL renderer
runs unchanged.

- bx, bimg and bgfx are shallow submodules under `third_party/bgfx` (the
  bgfx.cmake v1.139.9155-513 trio: bx `36ad6131`, bimg `b43fea9e`, bgfx
  `651c2031`), built by our own glue `third_party/bgfx-pd/CMakeLists.txt` as
  static libraries with only the GL 4.3 renderer, single-threaded. CMake
  fetches the submodules on its first configure; CI fetches them after
  checkout.
- Shaders (`port/fast3d/shaders/*.sc`) are compiled at build time by the lean
  `pd-shaderc` (GLSL path only) into C arrays. The combiner is one uber-shader
  (`vs_fast3d.sc`/`fs_fast3d.sc`); nothing writes GLSL at runtime in bgfx mode.
- `port/fast3d/gfx_bgfx.cpp` implements fast3d's backend table; the scene
  renderer (`scenario_scene_renderer_bgfx.cpp`), ImGui (`imgui_impl_bgfx.cpp`)
  and the pdgui textures (`gfxBgfxPdguiTex*`) draw through bgfx. The GPU swarm
  is compute only and unchanged; the mesh debug view reports that it is
  unavailable. Below GL 4.3 the client logs why and runs the 1.0 renderer.
- `--renderer-parity` draws each frame through both renderers in one process
  and compares them at every smoke screenshot (`gfx_tee.cpp`,
  `gfx_parity.cpp`).
- D6: `tools/bgfx_dependency_audit.py` reviews bgfx's library loads and the
  linked client; CI runs it after the build ("Review the bgfx dependency").
  The client contains no AGS, NVAPI, RenderDoc or Aftermath loader.
- Evidence at the landing: hosted CI green on the exact tip (2,525 cases), a
  landing proof on shared base 10 with all seven `renderer_*` smokes passing,
  and parity numbers (section 5.1).

### 2.2 Changed on dev since the landing

- CORE's B-1267 (cull modes) changed the scene renderer and mirrored it in the
  bgfx path: `sceneBgfxDraw` takes a `cull` argument. Its comment notes that a
  backend whose screen space is flipped against clip space (Direct3D) needs
  the cull direction checked again with its first capture.
- B-1266 (every room of every stage exported) and B-1290 (monitor screens) are
  extraction and game code; no new raw-GL drawing.
- An audit at `d43a7f72`: every file that creates raw GL textures has a bgfx
  branch (`pdgui_character_portrait.cpp`, `pdgui_charpreview.c`,
  `pdgui_menu_moddinghub.cpp`, `pdgui_skin_canvas.cpp`,
  `pdgui_skin_editor.cpp`, `pdgui_theme.cpp`,
  `scenario_scene_renderer.cpp`), except `swarm_gpu.cpp`, which only computes.
  The Grid browser preview was fixed by its own session after it handed raw GL
  names to ImGui (Workbench note N-1107).

### 2.3 Not done

Everything else in the phased plan: section 3 lists it as the definition of
done.

---

## 3. Definition of done

The 2.0 renderer is complete when every phase exit criterion in 3.2 is met and
every cross-cutting requirement in 3.1 holds. Only phase 2a step 1 is done.
The scope doc's estimates (low confidence): the whole scope 13 to 23 months of
focused work; a first shippable remastered mode (phases 1 to 5 plus AO and
bloom) 10 to 16 months. Phases 1 and 2a touch different code and can run in
parallel.

### 3.1 Cross-cutting requirements (every phase)

1. **One simulation.** Presentation reads game state and never writes it. The
   renderer choice never reaches the wire, saves, match manifests or mod
   checks; players in one match may use different modes (scope 5.1, 8.8).
2. **Classic is the reference and never changes its look.** On bgfx it stays
   pixel-identical to the 1.0 GL renderer on R9 captures, or every remaining
   difference is explained and accepted by Mike (D1, D5, R10). The 1.0 GL
   backend is removed only after that holds.
3. **The toggle (D1).** One instant switch, `Video.Renderer = classic |
   remastered`: a setting, a menu entry and one bindable action, effective at
   the next frame; developer-only per-pass overrides never shown to players;
   quality settings on the remastered side only; remastered resources built at
   stage load even in classic (a low-memory fallback may build them on the
   first toggle); remastered greyed out on hardware without a compute-capable
   backend, while classic still runs (scope 5.6).
   **The mode belongs to a player, not to the process (D-042, Mike
   2026-11-20).** Split-screen may come back, and then each local player picks
   classic or remastered: store the choice so it can become per player, let
   each player's toggle act on their own view only, and let one frame mix
   classic and remastered views. A classic view in a mixed frame stays
   pixel-identical to classic inside its viewport (scope 5.9).
4. **Built in (D4).** Remastered looks right with no packs installed; base
   materials get a built-in PBR derivation; art packs are ordinary mods.
5. **No prerequisites (D6).** Static linking by default; the graphics DLLs
   that ship with Windows (`opengl32`, `d3d11`, `dxgi`, `d3d12`) load from the
   system and are never bundled; release builds never try AMD AGS, NVAPI or
   RenderDoc; any other truly needed DLL ships in a `dll/` folder beside
   `data/` and `mods/`, loaded at runtime (`SetDefaultDllDirectories` with
   `AddDllDirectory`, or by full path), with `devtools/release.ps1`, the
   updater and the zero-DLL rule text updated in the same change. New
   `LoadLibrary`/`GetProcAddress` calls pass
   `tools/native_write_policy_guard.py`; the bgfx audit passes, and a bgfx
   update is re-reviewed.
6. **BYOR.** Nothing derived from the ROM ships. The built-in material
   derivation runs on the player's machine into private cache; packs are
   original art (scope 8.7).
7. **Public source stays the source.** GPU-ready products (compressed
   textures, generated normals and tangents, probes, lightmaps) are
   source-hashed private cache, never authored files (scope 5.1, 8.6;
   `AGENTS.md` c3842 rules; `tools/asset_native_source_guard.py`).
8. **The scope doc's section 9 rules** for all new code: no gameplay in draw
   functions; no gameplay reads of display lists; no renderer outside
   display-list order (inline commands like `G_SCENE_RENDER_EXT`); renderer
   approximations stay out of authored data; keep standard glTF material data
   at import; stable catalog identities for extracted materials; N64 limits
   out of public formats; per-instance state on the instance; visual
   randomness on a presentation RNG; declarative effects; no widening of
   `GfxRenderingAPI` for remastered features; static linking; versioned
   typed archives; classic stays capturable; no new raw OpenGL outside the
   backends; ImGui textures only through the pdgui texture helpers.
9. **Keep both paths in step.** Any change to a raw-GL drawer (today the scene
   renderer) lands with its bgfx path; any new ImGui texture is created
   through the helpers so bgfx mode gets a tagged id.
10. **Build and checks.** Pinned sources, static libraries, shaders compiled
    ahead of time, the MinGW toolchain; hosted CI green; pd-tests pass; the
    write guard, the bgfx audit, the bool layout audit and the commit hooks
    pass.

### 3.2 Phases and exit criteria (scope doc section 11)

| Phase | Scope | Exit criterion | Status |
|---|---|---|---|
| 0. 1.x guardrails | Section 9 rules; B-1172 landed; R3 and R6 if cheap; the standard-glTF materials plan | No new couplings in review; extracted materials carry catalog IDs | Partial: B-1172 is on dev; R3 and R6 are not done (no presentation RNG on dev; `extras.pd2_material` carries no texture catalog ID) |
| 1. Separation | R1, R2, R4, R5, R9; the presentation snapshot schema and its producers | Classic captures and gameplay logs unchanged; the listen host no longer builds remote views; the snapshot covers every opaque world draw | Not started; needs decision 11 (who owns R1) and a source freeze |
| 2a. Classic onto bgfx | Step 1: classic on bgfx GL (done). Step 2: D3D11 as the Windows default, GL 4.3 as fallback, Vulkan as an option, SDL windows without a GL context, the DLL rule | On bgfx GL, classic pixel-identical on R9 captures; on D3D11 every remaining difference explained and accepted; then the 1.0 GL backend is removed | Step 1 done; Mike's verdict (D-041): prove the causes of its parity differences, then accept the rest; the experiments run on `week-pd/bgfx-parity-proof` (5.1); step 2 not started |
| 2b. Remastered skeleton | bgfx views and passes; HDR target; `G_REMASTER_PASS_EXT`; the toggle; rooms in neutral shading; tone mapping | Toggling changes only pixels: gameplay logs identical in both modes | Not started |
| 3. Objects | GPU model cache; per-instance matrices, visibility and shading; characters, props, viewmodel, attachments; translucency order; opaque instancing | All models remastered; neutral mode matches classic composition | Not started |
| 4. Lighting, shadows, GI (D3) | Normals; clustered lights; fixtures as real lights bounded by rooms; the per-stage lighting file; cached fixture shadows and sun cascades; the per-light probe bake; one showcase stage first | Mike signs off the showcase stage's look; flash and light-op timing match classic | Not started |
| 5. Built-in materials (D4) | PBR shading; the built-in derivation; the translator for N64 blends and tints | Every base stage renders in remastered mode with no packs installed | Not started |
| 6. Post | AO, bloom, AA, color grading, native screen effects | The performance budget is met (decision 7) | Not started |
| 7. Packs | Schema 2 `.pdmaterial` and `.pdtexture`, targeting, modes, cache, Mod Manager conflicts; an original showcase pack if decision 4 says so | A pack changes only remastered mode, and old clients refuse it cleanly | Not started |
| 8. Sky and effects | Sky dome, stars, fog, particles, decals, beams, flares, x-ray and the vision modes | No classic fallback left in normal play | Not started |
| 9. Forge lighting | One light schema shared with Forge F4; optional lightmaps for custom maps | A custom map lit in both modes | Not started |
| 10. Qualification | Fallback hardware, split-screen with mixed classic and remastered views (D-042), listen host, Theater, memory, long sessions | Release gate | Not started |

### 3.3 What each remaining phase requires

**Phase 0.**
- R3: a separate presentation RNG for gunfire flicker, beams, rain, TV static,
  shields and cutscene static (scope 3.8). Small; it also makes phase 2b's
  "gameplay logs identical in both modes" exact.
- R6: write each texture's catalog ID (`base:texture_unlabeled_*`) into
  `extras.pd2_material` of `scene.glb` and name `.pdmesh` material slots, with
  conformance checks (scope 8.4). Extraction is CORE's code: coordinate.

**Phase 1 (separation).**
- R1: split each iteration of `lvRender`'s player loop into `lvTickView` (all
  gameplay statements, same order) and `lvRenderView` (GBI and the snapshot);
  keep the per-player interleave; skip `lvRenderView` for views that are not
  shown, so the listen host stops building remote views.
- R2: per-instance node visibility without rewriting `node->child` on shared
  definitions; an explicit LOD rule for hit tests.
- R4: the snapshot records matrices per view at record time, including the
  previous frame's (motion vectors, TAA).
- R5: a per-view room presentation step outside the draw (colors, flash,
  highlight, dyntex time, ageing), generalizing CORE's room-light capture.
- R9: the parity harness: the same scripted session before and after R1 and
  R2 (gameplay logs plus reference captures), later classic against
  remastered-neutral. `--renderer-parity` and the `renderer_parity_*` smokes
  are its renderer half today.
- The snapshot: `port/include/presentation_snapshot.h`, a C API for C and C++
  (shared declarations use `s32`, not `bool`: SP-75), double-buffered and
  copied at record time, with View, Environment, Rooms, Lights, Instances,
  Viewmodel, Effects and Screen effects records produced in the functions that
  emit the classic GBI (scope 5.3).
- R7 (declarative legacy effects) belongs before phase 8.

**Phase 2a step 2 (D3D11 default).**
- HLSL shaders from the same `.sc` sources. `pd-shaderc` is GLSL-only today;
  shaderc's HLSL path needs fxc or DXC as a host tool, a toolchain choice to
  settle (and to record against D6: host tools are not shipped).
- SDL windows without a GL context for non-GL backends, and backend choice at
  launch: D3D11 by default, GL 4.3 as the fallback, Vulkan as an option hidden
  where its loader is missing. Today's fallback chain down to GL 3.0 and 2.1
  (`gfx_sdl2.cpp`) goes away with the 1.0 backend.
- bgfx's D3D11 renderer patched so release builds never try AMD AGS or NVAPI
  (`renderer_d3d11.cpp`; `nvapi.cpp` and `dxgi.cpp` are excluded from today's
  build), DXGI and D3D11 loaded from the system, and the dependency audit
  extended to the renderers that are compiled in.
- Everything that still assumes a GL context: the GPU swarm (`swarm_gpu.cpp`)
  moves to bgfx compute or gets an explicit decision; screenshots and the smoke
  capture path, front-buffer copies (`GL_FRONT` reads; the final target that
  plays the front buffer already exists in bgfx mode) and parity need a
  backend-neutral readback; parity across two APIs needs two devices in one
  process or a capture comparison between runs; meshdebug may stay
  unavailable.
- B-1267's cull direction checked on D3D with its first capture.
- Exit: every remaining D3D11 difference explained and accepted by Mike; then
  the 1.0 GL backend is removed (`gfx_opengl.cpp`, the GL fallback chain,
  `imgui_impl_opengl3.cpp`, the raw-GL branches of the pdgui files and the
  scene renderer), once parity holds on every R9 capture.

**Phase 2b (remastered skeleton).** bgfx views per pass on one device;
`G_REMASTER_PASS_EXT(view slot, pass)` generalizing `G_SCENE_RENDER_EXT`
(passes: rooms opaque, props pre-BG, props post-BG, decals, rooms translucent,
props translucent, viewmodel, sky, post); during migration each producer emits
either classic GBI or the inline command, never both, and moves its side
effects out first (R3 to R5); the toggle of 3.1; rooms in neutral shading;
HDR target and tone mapping; drawing in the game's draw space with its
projection so depth stays compatible with fast3d passes (scope 5.7; the B-936
lesson). The mode is decided per view from the start (D-042): each view's
`lvRenderView` emits classic GBI or the remastered commands according to its
player's mode, the HDR target, tone mapping and post run only for remastered
views, and nothing a remastered view sets (bgfx view state, targets,
uniforms) leaks into a classic view of the same frame.

**Phase 3 (objects).** A GPU mesh cache per modeldef node (native modeldefs
first, then `.pdmesh` float packets) with per-vertex matrix indices;
per-instance matrix palettes and parameters in SSBOs; per-view grouping of
opaque draws by mesh and material; translucency order; special nodes
(gunfire, headspots); the viewmodel with its own projection and depth clear.

**Phase 4 (lighting, shadows, GI; D3).** Clustered forward+ lighting with a
compute pass for light lists; every fixture in `room_lights.json` as a real
area or spot light switched and faded by the game's own on, healthy and
light-op state; the sun on outdoor stages; muzzle flashes, explosions and
`.pdeffect` lights as point lights; normals generated at cache build; rooms
and portals as light boundaries (`_PD_ROOM` and the portal table); baked
vertex colors kept only as a hue tint and calibration guide; a public
per-stage lighting file written by the extractor, in the same light schema as
Forge F4; cached shadow maps of the static rooms per shadowed fixture plus
moving casters each frame, sun cascades, blob shadows as the fallback; a
per-light probe bake (indirect light = sum of each fixture's probes times its
current brightness), run on the player's machine and kept as cache, with a
progress screen on first load; characters and props lit by the same lights and
probes. One showcase stage first, with Mike's sign-off, before the other
stages; flash and light-op timing must match classic (`dlights.c`).

**Phase 5 (built-in materials; D4).** glTF metallic-roughness (GGX) shading
with image-based lighting from the probes and the sky; a translator for
classic inputs (N64 two-texture blends, env and prim tints for cloak, shields
and highlights, alpha modes, fog); the built-in derivation per base material:
roughness and metallic from the texture's `surface_type` (a public defaults
table), a normal map from the texture's brightness as height, AO from the
geometry, the original texture as albedo tinted by the baked hue; all derived
locally and cached (BYOR).

**Phase 6 (post).** HDR, tone mapping, bloom (emissive surfaces, flares,
muzzle flashes), ambient occlusion, SMAA or TAA (TAA needs R4), per-stage
color grading (Forge post-process volumes), native versions of the
`bondview.c` screen effects; the classic GBI screen effects run over the
remastered image until then; the vision modes treated explicitly.

**Phase 7 (packs).** Schema 2 `.pdmaterial` (`mode = remastered | both`,
`targets = <catalog ids>`, `textures` base_color/normal/orm, `uv.space =
source`) and `.pdtexture` `usage` and `color_space`; `pbr_metal_rough` behind
the version check, with older clients refusing schema 2 cleanly ("requires
2.0"); targeting by catalog ID (needs R6), and for classic `mode = both` a
reverse map from texture address to catalog ID; later mods win, the Modding
Hub shows conflicts; the private cache (mips, BC7 and BC5, ORM packing).

**Phase 8 (sky and effects).** A sky dome from the stage's environment data,
sun disc and flare with the existing line-of-sight intensity, stars, sky rooms
with the camera offset, per-pixel distance and height fog; GPU-instanced
billboards for sparks, smoke, explosions and shards (CPU simulation
unchanged); projected decals from collision-mesh hit normals (needs the
texture surface type on collision triangles); emissive beams and tracers;
remastered x-ray (Farsight), night vision, IR, cloak, shields and eyespy, or
an explicit per-frame classic fallback until each exists. PD's sky tricks (the
moon and planet rooms, the elevator shaft) keep working.

**Phase 9 (Forge lighting).** One public light schema shared by the
extractor's lighting files and Forge F4 (directional skylight with shadows,
point, spot and area lights, emissive surfaces, probes, fog and post volumes,
the 32 soft and 64 hard light budget); the same probe bake for custom maps;
optional lightmaps; classic shows custom lighting through a vertex-color bake
if decision 10 says so.

**Phase 10 (qualification).** Fallback hardware (classic everywhere,
remastered greyed out below the capability gate), split-screen (once it
exists; D-042: frames that mix classic and remastered views, each classic
view pixel-identical to classic in its viewport, each player's toggle acting
on their own view), the listen host, Theater, memory and VRAM (stage-scoped
residency, streaming), long sessions; the release gate.

---

## 4. Decisions still needed from Mike

Turn each open one into a Workbench decision item (two to four options with
trade-offs). The scope doc section 10 has the background.

1. **Step 1 parity verdict. Decided (D-041, Mike 2026-11-20): B**, prove the
   causes with the two experiments of 5.1, then accept what remains.
2. **Split-screen. Decided (D-042, Mike 2026-11-20):** "We may add Split
   Screen back in, but each player might want to play on a different mode
   (classic vs new)." Split-screen stays in scope with the mode chosen per
   local player (3.1 item 3); its parity check waits until split-screen
   exists (no split-screen frame can be drawn today) and then covers mixed
   frames (phase 10).
3. **Decision 6, hybrid frames.** May 2.0 ship with some passes still classic
   in remastered mode? (Scope recommends yes.)
4. **Decision 7, budgets.** How many remastered views (split-screen, listen
   host) at what frame rate on Mike's reference machine. Gates phase 6.
5. **Decision 8, frame interpolation** in 2.0 or 2.x (scope recommends 2.x).
6. **Decision 9, Theater** in both renderers in 2.0 (scope recommends yes).
7. **Decision 10, Forge F4 lighting** remastered only, or also baked for
   classic (scope recommends both, one schema).
8. **Decision 11, who owns R1** and when its source freeze runs.
9. **Decision 4 (open part):** an original showcase pack, and whether local
   tools such as AI upscaling of the player's own textures are acceptable.
10. **Default mode:** remastered where supported, or classic first (scope 5.6
    proposes remastered).
11. **Step 2 toolchain:** the HLSL compiler host tool (fxc or DXC), and the
    `dll/` folder name if a DLL is ever needed (D6 proposes `dll/`).

---

## 5. Open items the renderer owner holds now

### 5.1 Step 1 parity differences

Landing proof on shared base 10 (2026-11-19), differing pixels out of
3,686,400 per capture:

| Capture | Differing | Max delta | Cause |
|---|---|---|---|
| Agent Select (2) | 4,630; 4,581 | 8 | About 4,200 in one round, noise-like patch in the bright part of the menu's screen thumbnail; about 100 scattered. Not proven. Leading guess: a `sin()`-based noise hash in what the thumbnail draws, where a last-bit difference between the uber-shader and the generated GLSL becomes a different value. Test: a parity run with the noise term held constant. |
| Defection (3) | 264; 448; 180 | 15 | Scattered over room surfaces; rose from 82 to 95 when dev's room lighting reached the scene. Working explanation: its `floor()` steps turn last-bit differences into whole levels. Test: give the scene shader the classic GLSL 1.30 text (bgfx passes text that starts with `#version` verbatim). |
| Felicity (3) | 158; 172; 193 | 52 | Five edge pixels of one capture differ by 16 or more, as before. |

The captures, bgfx images, difference images and `.parity.json` files are in
the smoke installs of `D:\PD2-wt\bgfx-classic\.claude\smoke-storage` (not in
git). Mike's verdict (D-041, 2026-11-20): prove the causes, then accept what
remains. The experiments run on `week-pd/bgfx-parity-proof`: first the Agent
Select noise test (combiner files only); the Defection test after CORE's PR
21 (B-1268, reflective room surfaces) lands, because that PR changes the same
scene shader and scene renderer files.

### 5.2 Requests from other lanes

- **Blank Map floor (Workbench T-MODDING-021, notes N-1092 and N-1094).** The
  Grid session found that the synthetic Blank Map stage's floor (stage 0x1a,
  material `mat_001_tex_013e_c2`) selects normalized TEXCOORD_0 with clamped
  wraps, while its repeating TEXCOORD_1 is the intended mapping. It asks the
  renderer owner for one per-floor binding policy in both scene paths (classic
  GL and bgfx): UV1 with repeat S and T for the active synthetic Blank Map,
  gated on its empty Forge arena ID and exact scenario identity, not on canvas
  mode. Check the Workbench for its current state before starting.
- **ImGui textures.** N-1107: raw GL names handed to ImGui are rejected in
  bgfx mode (ids must be tagged). Any lane adding an ImGui image must create
  it through the pdgui texture helpers.

---

## 6. Technical reference

### 6.1 Files

| Path | What |
|---|---|
| `port/fast3d/gfx_bgfx.cpp`, `gfx_bgfx.h`, `gfx_bgfx_ext.h` | fast3d's backend on bgfx; views; final target; pdgui textures; tagged ImGui ids; parity capture |
| `port/fast3d/gfx_tee.cpp` | Parity mode: forwards every backend call to the GL and bgfx backends |
| `port/fast3d/gfx_parity.cpp` | Compares the two frames; writes `.bgfx.bmp`, `.diff.bmp`, `.parity.json`; logs `PARITY:` |
| `port/fast3d/imgui_impl_bgfx.cpp` | ImGui on bgfx (chosen by `pdgui_render_backend.cpp`) |
| `port/fast3d/scenario_scene_renderer_bgfx.cpp`, `.h` | The scene renderer's bgfx draws (probe stubs in `tools/scenario_scene_probe_stubs.cpp`) |
| `port/fast3d/shaders/` | `fast3d` (combiner uber-shader), `pdcopy`, `pdimgui`, `pdscene` `.sc` sources and varying files |
| `third_party/bgfx-pd/` | Build glue, `pd-shaderc` (`shaderc_lean.cpp`), `renderdoc_stub.cpp`, the anchored build-tree patches |
| `tools/bgfx_dependency_audit.py` | The D6 review of bgfx's loads and of the linked client |
| `tools/smoke-verify/tests/renderer_*.json` | The seven renderer smokes |
| `.github/workflows/pd2-ci.yml` | Submodule fetch step and the bgfx audit step |

### 6.2 Flags

| Flag | Effect |
|---|---|
| (none) | The 1.0 GL renderer; bgfx linked, never initialised |
| `--renderer bgfx` | bgfx's GL 4.3 backend presents |
| `--renderer-parity` | GL presents; bgfx renders the same frames offscreen on a second GL context; each smoke screenshot is compared |
| `--debug-bgfx-assume-gl 3.3` | Pretend the context is that version (tests the fallback) |

### 6.3 bgfx lessons learned on this code

- GLSL 4.30 shader binaries carry no uniform table: create every uniform
  before the program that uses it, or nothing binds.
- Render-target and MSAA flags share one 3-bit field: use `msaa ? msaa :
  BGFX_TEXTURE_RT`; a depth target needs `BGFX_TEXTURE_RT_WRITE_ONLY` ORed with
  it, or it gets no storage. `RT|READ_BACK` is refused: blit into a
  `BLIT_DST|READ_BACK` texture to read back.
- View state persists across frames: reset a view when it is allocated and
  restart view ids after `bgfx::frame()`. View rects are at least 1x1.
- The rect cache (per-draw scissors) is per frame: reuse the index from
  `setScissor` for repeats.
- Texture and dynamic buffer updates run before the frame's first view: one
  texture updated per view lights every view like the last (the scene renderer
  creates one room-light texture per pass and destroys it after the frame).
- Transient vertex layouts are never freed; keep the number of distinct
  layouts small.
- `BGFX_INVALID_HANDLE` is a braced initializer: not an operand of `?:`.
- No 32-bit integer vertex attributes (the scene's room index goes as a
  float, equal to GL's conversion); uniforms are only Vec4, Mat3, Mat4 and
  samplers.
- bgfx's GL loader passes shader text that starts with `#version` through
  verbatim.
- The scissor test state mirrors GL's (on after a clear, resolve or copy; off
  after the overlay reset); `gfxBgfxApplyScissor` gives external passes
  fast3d's current scissor.
- `GL_FRONT` reads: in bgfx mode a final target plays the front buffer (game
  image plus ImGui composed offscreen, copied to the window before
  `bgfx::frame()`).

### 6.4 Smoke and parity practice

- Use the shared base that pairs with dev (the hub chat and week note announce
  each new base; base 13, `D:\PD2-wt\shared-base-13`, since 2026-11-20 05:45
  UTC). An older base re-extracts every stage once.
- Fixtures run by an agent wait for `core_boot_ready` or gameplay readiness:
  `agent_select_ready` and the main-menu conditions need keyboard focus a
  background runner never gets.
- Parity numbers are the `PARITY:` lines in each run's
  `logs/game client/pd-client.log` inside its install copy; the images sit at
  the install root.
- Smoke storage: ask `tools/smoke-verify/storage.py status` (JSON request on
  stdin) before a batch; compact finished installs you created with its
  `compact` action (to the Recycle Bin); never `du` the storage on D:.
- A game run started by a background waiter can come up hours early: never
  commit or merge in a worktree while its own turn builds it (read the
  waiter's log in the same command).

---

## 7. Landing work on dev

- Hosted CI green on the exact tip that lands (the tip holds current dev); a
  game proof for code changes; documentation-only changes need static and diff
  checks only (`AGENTS.md`).
- Since 2026-11-19 evening the canonical checkout's local dev carries
  local-only commits from Codex sessions. Landings are merges pushed to
  origin/dev, then merged into local dev in the canonical checkout (for
  example origin/dev `d43a7f72`, then local dev `6e1fff4b`). Announce in the hub
  chat before touching the canonical checkout, keep every uncommitted file (a
  3-way merge of overlapping files, numstat and hashes compared before and
  after), and post "landed" when done. `context/procedures.md` has the Git
  safety rules; never `git reset --hard`, never a bare stash.

---

## 8. Suggested order for the next session

1. Startup reading, hub registration, and decision items for the questions
   still open in section 4 (T-RENDERING-001, D-041 and D-042 exist).
2. Finish the parity experiments of D-041 (5.1) if they are not done, and
   record each cause in 5.1 and the step 1 design doc.
3. Phase 2a step 2 (D3D11): first a design section like
   [bgfx-classic-gl.md](bgfx-classic-gl.md) (shader toolchain, windows
   without GL, D6 review of the D3D11 renderer, swarm, readback and parity
   across APIs, culling), then the backend behind a flag, with the 1.0 GL
   renderer still the default until Mike accepts the differences.
4. In parallel and cheap: R3, and R6 together with CORE (extraction).
5. Phase 1 only after decision 11 (R1's owner and freeze).

The Blank Map floor request (5.2) can be done at any point; it touches both
scene paths.
