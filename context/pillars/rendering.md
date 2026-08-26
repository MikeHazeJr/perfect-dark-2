# Rendering

> fast3d: N64 GBI display lists translated at runtime to OpenGL. Function-pointer-table backend abstraction (`GfxRenderingAPI` + `GfxWindowManagerAPI`). Dear ImGui v1.91.8 overlay through the same SDL2 + OpenGL context. Theme system decodes ROM textures to RGBA32 and uploads as GL textures.

## 2026-08-26 B-1105 material-slot activation repair (production verified)

Source now preserves the complete ordered `model.mtl` material domain before
indexed `model.render.json` consumption. This corrects the Mauler high-detail
first-person model and the propagated `base_model_cheaddarkaqua` head without
special cases; sparse OBJ use order no longer collapses unused slots or shifts
later render commands. A failed first-person model load also remains latched
instead of retrying activation each frame. One shared public geometry-alias
helper removes top-level/nested source-choice drift.

Exact client `395D48A6...` passes focused 132/14, full 67,152/1,223,
native-source, two-archive, fixture, and 23/23 unchanged-source verification.
Strict fresh-cache ordinary receipt `results-20260826T152922Z.json` passes
54/54 with one nested 13-material compile/activation/load/render chain, 11 real
Mauler shots, no runtime native model boundary, and clean exit. Direct review
of all four gameplay captures shows stable held placement and scale, readable
silver/green materials, no endscreen, and no huge white first-person
obstruction. B-1105 is now a V-009/V-010 regression gate. B-1086's separate
Carrington Institute solid-white door report remains repro-required.

## 2026-08-13 generated display-list ownership

Generated model payloads and their texture-loaded clones now register exact,
queryable GDL byte spans instead of relying on a geometry-count fuse as a
normal traversal boundary. Registration retains generated base ownership,
`GUNDL` base bytes and offsets, independent opaque/translucent ranges, and the
terminal `ENDDL`; malformed, unowned, or out-of-range pointers fail closed.
Character and object hit walkers consume the same span contract, so render
cloning cannot turn a valid generated model into an unbounded memory walk.

The source-frozen two-cycle Combat Simulator proof is
`.claude/smoke-verify-runs/results-20260813T153846Z.json` (56/56, two natural
generated-character hits, no GDL rejection, clean exit). Future gameplay
captures must retain V-009 as a visual regression gate: no huge white
first-person obstruction, correct held placement/scale and material colour,
and readable effect colour.

## 2026-08-10 public `.pdeffect` presentation

Generic v1 public effects now publish immutable growable presentation commands
inside the same all-or-nothing transaction as gameplay and audio. Supported
channels are catalog-textured screen passes, world decals, room-light plus
additive halos, source-to-target beams, and source-textured multi-particles.
Target, attachment, spatial/room, tint, secondary tint, material shading,
roughness, metallic, emissive, texture/UV, fixed shader, size, glow, speed,
width, priority, lifetime, and sampled intensity are copied into production
renderer state. Missing, disabled, replaced, wrong-typed, or unsupported
resources fail before commit; presentation retains no raw prop pointers.
Automated Wave11 evidence is in
`context/evidence/2026-08-10-wave11-pdeffect-production-runtime.md`; live
edited-source visual proof remains Workbench `V-009`.

## 2026-08-08 catalog weapon reticles

Public `.pdweapon` presentation can now select an embedded `.pdui` by catalog
ID. The real sight render path queues the selected image at the existing
crosshair coordinates; the ImGui overlay maps 320x240 HUD space into the live
game viewport, clips it, and renders it without replacing later target-tracking
work. The native reticle is suppressed only after strict weapon registration
has proved the selected row is enabled UI with a readable PNG/TGA source and
valid header. Hot local/network UI rows decode lazily from their catalog source;
decode failure does not synthesize a custom fallback. The overlay reads no SDL
or action-map input, so MKB/controller bindings and live glyph ownership remain
with their existing systems. Workbench `T-ASSETS-023` is implemented; final
combined compilation and ordinary-client visual/device proof remain.

---

## What it is

The renderer translates the original N64 graphics binary interface (GBI) display lists to OpenGL at runtime. The game continues to emit GBI commands as it would on N64; the fast3d translator at [port/fast3d/gfx_pc.cpp](../../port/fast3d/gfx_pc.cpp) (3068 lines) consumes them and dispatches to a function-pointer-table backend.

A second backend (Vulkan, D3D12) drops in by replacing one file thanks to the vtable.

Code:

- Renderer core (GBI translator): [port/fast3d/gfx_pc.cpp](../../port/fast3d/gfx_pc.cpp).
- Rendering backend vtable: [port/fast3d/gfx_rendering_api.h](../../port/fast3d/gfx_rendering_api.h) (57 lines).
- Window manager vtable: [port/fast3d/gfx_window_manager_api.h](../../port/fast3d/gfx_window_manager_api.h) (54 lines).
- OpenGL backend: [port/fast3d/gfx_opengl.cpp](../../port/fast3d/gfx_opengl.cpp), [gfx_opengl.h](../../port/fast3d/gfx_opengl.h).
- SDL2 window manager backend: [port/fast3d/gfx_sdl2.cpp](../../port/fast3d/gfx_sdl2.cpp), [gfx_sdl.h](../../port/fast3d/gfx_sdl.h).
- Color combiner: [port/fast3d/gfx_cc.cpp](../../port/fast3d/gfx_cc.cpp), [gfx_cc.h](../../port/fast3d/gfx_cc.h).
- ImGui backend: `port/fast3d/pdgui_backend.cpp` (single SDL event entry at line 1272).
- Theme system: [port/fast3d/pdgui_theme.cpp](../../port/fast3d/pdgui_theme.cpp) (3298 lines), [pdgui_theme_loader.cpp](../../port/fast3d/pdgui_theme_loader.cpp).

---

## Vtable abstraction

`GfxRenderingAPI` at [gfx_rendering_api.h:17](../../port/fast3d/gfx_rendering_api.h:17) defines a 30+ slot vtable for the rendering backend (texture upload, draw triangles, set viewport, etc.). `GfxWindowManagerAPI` at [gfx_window_manager_api.h:20](../../port/fast3d/gfx_window_manager_api.h:20) is symmetric for window/event/swap.

`gfx_pc.cpp:241` consumes both via `static struct GfxWindowManagerAPI* gfx_wapi; static struct GfxRenderingAPI* gfx_rapi;`. The application picks one of each at startup; today that is OpenGL + SDL2.

---

## Generated chr-body render path + scene-renderer compositing (B-936, live gotchas)

The held CI menu backdrop composites two renderers into one framebuffer each frame, and several non-obvious rules fell out of fixing the months-long "Joanna not appearing" bug (B-936, all fixed on dev 2026-06-24):

- **Frame order** (`gfx_pc.cpp` `gfx_run`): clear fbo -> `scenarioSceneRendererRender` (raw-GL room, its OWN projection) -> `gfx_run_dl` (the game's GBI world: bg/sky fill, props, chrs). The room is drawn FIRST, so anything fast3d draws afterward can cover it.
- **CORE PROFILE / VAO discipline.** This is a core-profile GL context (GLSL 430 core), so fast3d MUST have its `opengl_vao` bound to draw -- VAO 0 reads no attrib pointers and rasterises nothing. The raw-GL scene renderer binds its own VAO/VBO and leaves `vao=0`/`arrbuf=0` on return. `gfx_opengl_draw_triangles` now **rebinds `opengl_vao`+`opengl_vbo` before every upload+draw** so each fast3d draw is self-sufficient regardless of external GL state. Any future raw-GL renderer is covered by this. The scene renderer also saves/restores `GL_ARRAY_BUFFER_BINDING` (state hygiene). Symptom if this regresses: world draws issue (correct CPU clip, `glDrawArrays` runs) but ZERO pixels.
- **Generated chr bodies are lit, not flat.** `modasset_compiler.c` (`generatedModeldefBuildPayload`, gated `mcount==3`): a chr body's per-vertex "colour" table is NORMALS; keep `G_LIGHTING` ON so fast3d lights them. Don't override the stock 2-cycle `G_CC_CUSTOM_17/18` combine that `modelApplyRenderModeType3` set -- the per-material emitters (gated by `s_genLitChrBody`) only enable texturing. The combine is `[lerp(TEXEL0, ENV, shade_alpha)] * lit_shade`.
- **Env-lift is DERIVED.** The stock lift of a dark combat suit is the per-chr room shade carried in FOG (`var80062a48`={64,10,10} env + `fogcolour`, traced via `--debug-chr-env`). The generated DL can't carry per-vertex fog, so we supply a derived env (160) + mid shade_alpha (140). Critically, the stock `G_RM_FOG_PRIM_A` mode WASHES the body to pure black when the scene fog colour is near-black, so generated chr bodies are forced to **fog-independent opaque** (`G_RM_PASS, G_RM_AA_ZB_OPA_SURF2`). Caveat: distant generated chr bodies won't fade into scene fog.
- **Sky fill vs scene-renderer backdrop.** `skyRender` (sky.c:266) under `!clouds_enabled` (indoor) does a `G_CYC_FILL` fullscreen fill with the env sky colour (blue for CITRAINING). The `skyRender` call (lv.c:1658) is gated on `!scenarioSceneRendererIsActive()` so the scene-renderer's tiled room is the backdrop when it provides one.
- **Diagnostics (gated, off by default):** `--debug-chr-env`, `--debug-scene-glstate` (scene renderer exit GL state), `--debug-firstdraw-glstate` (state of the first world draws), `--debug-clear-depth-after-scene`, `--debug-texsample`, plus the older `--debug-force-chr-prim` / `--debug-track-chr` / `--debug-hide-scene` / `--debug-show-only-mesh`.
- **OPEN (B-942):** the generated chr body's held POSE is skeletally distorted (off-body spike-limb) -- a bone-weight/transform bug in the `.pdbody` skeletal pipeline, separate from the render path above. Model + textures are correct.

---

## ImGui integration

Dear ImGui v1.91.8 vendored at `port/include/imgui/`. Renders through the same SDL2 + OpenGL context as the game GBI translator, drawn after the game frame, with a foreground drawlist for overlays (toasts, glyphs, achievement banners).

Single SDL event entry point at [port/fast3d/pdgui_backend.cpp:1272](../../port/fast3d/pdgui_backend.cpp:1272). Event flow: `ImGui_ImplSDL2_ProcessEvent` -> `WantCaptureKeyboard` gate -> `actionmapDispatch` -> `inputCtxDispatch`. See [pillars/input.md](input.md) and [pillars/menus.md](menus.md) for the input authority story.

---

## Theme system

[port/fast3d/pdgui_theme.cpp](../../port/fast3d/pdgui_theme.cpp) (3298 lines). Decodes ROM textures to RGBA32, uploads as GL textures. 24-element working palette with bundle support (palette + chrome style + font in one activation, S305 Theme Bundling).

Components:

- **Theme** = persistent visual identity (user-chosen, draws first).
- **Tint** = transient overlay (composites on top of theme, cleared on pop).
- **Menu Style** (formerly Nine-Slice) - chrome borders / corners.
- **Title Bar Style** - 5 procedural styles (Classic, Solid, Vertical Bars, Scanlines, Diagonal Stripes), persisted via `Video.UiTitleBarStyle`.
- **Font** - imported via Font Mod system, persisted via `Video.FontId`.

UI texture mod overrides (S351): `pdguiThemeScanModUiTextures` / `pdguiThemeApplyEnabledModUiTextures` allow enabled mods to override `"type": "ui"` catalog textures at runtime. See [pillars/menus.md](menus.md) Theme System section for the editor side.

---

## C++ stdlib usage in rendering

Per the codebase architecture rating audit Section 4: [port/fast3d/gfx_pc.cpp:11](../../port/fast3d/gfx_pc.cpp:11) imports `<map>`, `<set>`, `<unordered_map>`, `<vector>`, `<list>`, `<stack>`. `std::map<ColorCombinerKey, struct ColorCombiner>` (line 100) for the shader pool. `std::map<int, FBInfo>` (line 253) for framebuffers.

This is intentional and stays in the C++ port layer. Game code in `src/game/` and `src/lib/` is pure C; never include C++ stdlib there.

---

## Active invariants

Per [constraints.md](../constraints.md):

- **C11 game / C++ port code.** Game logic in C11; port/renderer in C++. Must not introduce C++ in `src/game/` or `src/lib/`.
- **Static linking discipline.** OpenGL is the only dynamic dependency (`opengl32.dll` per CMakeLists.txt:830). No DLL surface for SDL2, zlib, libcurl, etc.
- **GL texture size and cache lifetime** (SP-15). Any code path that uploads a user-controlled image to a `GLuint` texture must (a) bound dimensions against `GL_MAX_TEXTURE_SIZE` (or compile-time cap), and (b) free via `glDeleteTextures` on the cache's lifetime boundary. A rescan that rebuilds the cache without deleting old GL names leaks textures on every reload. Reference fixes: `pdguiThemeRescanChromeStyles` (S-6, S294), `s_DownrezPreview` in `pdgui_skin_editor.cpp` (S-5, S294).
- **Modern HW: prefer correctness over micro-optimization.** Legacy workarounds in the renderer that exist because of N64 limits should be replaced with standard approaches when revisited.

---

## What is done

Per [audits/codebase-architecture-rating-2026-04-27.md](../audits/codebase-architecture-rating-2026-04-27.md):

- Function-pointer-table abstraction is crisp; backends drop-in replaceable.
- OpenGL backend with shader pool, framebuffer management, color combiner.
- Theme system with bundle support, mod-supplied themes, mod UI texture overrides, 5 procedural title bar styles.
- ImGui v1.91.8 integration with custom backend; PD-authentic styling.
- Lobby portrait baking pipeline (charpreview FBO, S352).
- BG visual display-list extraction for map authoring (c3812-s9, 2026-05-21; current source contract updated 2026-06-07): `.pdscenario` archives now use public `scene.glb` plus semantic JSON Scenario members and optional `collision.obj` as the editable/source-facing map payload. Old `visual/scene.obj`, `visual/scene.mtl`, `visual/materials.tsv`, and decoded `visual/textures/*.tga` sidecars are stale archive-era outputs, not current public source. Runtime source-scene rendering consumes the GLB attributes directly, including runtime `TEXCOORD_1`, `COLOR_0`, and alpha-mask material metadata, while legacy fast3d remains the parity/reference path where still active.
- Scenario source-scene CPU probing (B-801, 2026-06-08): `scenario-scene-probe` runs the same `scene.glb` CPU build path as renderer activation without creating a window, touching global renderer state, or uploading GPU resources. The first full extracted sweep passed 87/87 `.pdscenario` archives with nonzero image counts after fixing material parsing to use direct parsed-object lookups instead of slow generic JSON value searches; live visual issues after a CPU pass should be investigated in camera, GPU upload, draw ordering, or render-state usage.
- Scenario-scene-probe is now a first-class wrapper target + repeatable sweep (Gate 1 CPU pre-flight, 2026-06-09): build it with `build-session.ps1 -Session <id> -Target probe` (maps to the `scenario-scene-probe` CMake target), then run `devtools/scenario-scene-probe-sweep.ps1 -Session <id>` to probe every `.pdscenario` in a data tree (default the fresh install tree), asserting `ok=1` with `images>0` and `vertices>0` per archive and a non-zero exit on any miss. This is the mandatory CPU pre-flight before any narrow live renderer pass under B-801 and guards against a regeneration silently dropping scene content. Verified 87/87 on the fresh `ntsc-final` install tree.
- Scenario source-scene draw target/state bridge (B-867, 2026-06-08): source-scene rendering must use the active game draw area dimensions (`gfx_current_dimensions`), not the outer window dimensions, and must restore `GL_VIEWPORT` before returning to the fast3d display-list renderer. This keeps valid public `scene.glb` payloads aligned with the current framebuffer and prevents raw GL viewport state from leaking into later props, sky, or UI display-list work.
- Scenario source-scene shader diagnostics (B-892, 2026-06-08): source-scene rendering must check GL shader creation, compile status, and program link status before GPU upload/draw. Shader failures log `SCENARIO.RENDER: shader compile failed` or `SCENARIO.RENDER: shader link failed` with the GL info log and fail closed for that scene, so broken visuals can be traced to the actual shader stage instead of masquerading as bad extracted geometry.
- Discord Rich Presence (D7, shipped S348).
- Foreground drawlist primitives for toasts, glyphs, achievement banners.

---

## What is in flight

- c3844 custom body render proof is green. Source-backed custom `.pdbody` /
  `.pdhead` slots survive to match setup and bot allocation; the smoke runner
  uses the isolated client and scoped mask; the signed head-slot overflow,
  manifest-owned lifecycle unload, sparse loader-pool, manager lifetime, nested
  mesh source, and generated body modeldef issues are all fixed. Final root
  cause for B-923 was render-matrix timing: the debug-placed bot could reach
  `chrRender` before normal character matrix allocation populated
  `model->matrices`, so generated render audit crashed before `MODASSET.RENDER`.
  `chrRender` now prepares matrices on demand before `modelRender` when needed.
  Verification passed native-source guard, focused c3844 static tests, isolated
  `c3844mat` all-target build, and bounded `custom_body_live_render_smoke`
  41/41 with scripted exit and `MODASSET.RENDER` for `example:tri_body`.
  Rendering follow-up under c3844 now moves to remaining family proof, not the
  custom body/head handoff.
- B-366 Falcon 2 first-person stretch is fixed pending Mike visual retest. The remaining in-game artifact after the OBJ exporter fix matched Mike's "tip stays fixed while the model moves" repro: the Falcon laser-sight near end followed the animated muzzle/root, while the far endpoint was projected from the crosshair. `bgunUpdateLasersight()` now transforms both near and far endpoints from the muzzle matrix/local forward vector before projection, and the steady-state laser gate keeps equip/busy stale beams hidden. Focused `[bondgun][laser][static]` passed 25 assertions / 4 cases in isolated session `falconbeam5`, the queued all-target build passed, and final `boot_smoke` passed 14/14.
- B-346 credits/fog alpha artifact is patched with visual proof. Mike's first `gfxalpha` run showed the broad fog-alpha output-alpha path was wrong: characters/weapons became translucent and credits masks still rendered as solid colored quads. Fast3d now keeps `G_BL_A_FOG` as color-fog state only, material alpha is back to normal translucent `MEM,1MA`/texture-edge cases, strict `G_RM_ADD` additive fog uses only the exact `IN,FOG_ALPHA,MEM,1` tuple with additive blending, and `text0f153628()` explicitly resets texture enable/scale before CI4 glyph drawing. The 2026-06-30 diagnostics showed the CI4 glyph path was decoding correctly (`rdp.palette_fmt == G_TT_IA16`, `use_alpha=1`, palette alpha 0/255, shaped row masks), so the visible solid quads were not a TLUT decode failure. Root cause is frame-start render-state cache drift: `gfx_opengl_start_frame()` disables `GL_BLEND`, but Fast3D's `rendering_state.alpha_blend` cache could remain true from the prior frame, causing the first translucent credits draw to skip `set_use_alpha()` and draw opaque. The patch invalidates Fast3D's alpha/modulate/additive blend cache after backend frame start, keeps the TLUT fallback local to palette loading, and keys CI texture cache entries by palette format. Verification: isolated `b346tlut` build PASS, focused B-346 static tests PASS (27 assertions / 4 cases), asset-native-source guard PASS, and delayed credits screenshots in `.claude/smoke-verify-runs/screenshots/20260630T025340-credits_alpha_smoke_delayed_local/` show shaped glyphs and masked particle trails. The smoke harness result is 7/8 with exit code 0 because only a stale required log-line pattern is missing.
- HUD layer order discipline per [designs/menus/hud-layer-order.md](../designs/menus/hud-layer-order.md) (implemented).
- GPU swarm benchmark per [designs/in-flight/gpu-swarm-and-test-scenarios.md](../designs/in-flight/gpu-swarm-and-test-scenarios.md) (Phase 1 design; Phase 2 gated on Mike's call on F.1 GL 4.3 vs transform feedback).
- B-1097/SP-61 has accepted frozen automation: per-character
  modular model clones now preserve the packed sorted part-number sidecar used
  by hand, hat, and other named-part lookups in the same allocation as the
  cloned definition and nodes. Part lookup rejects null/empty tables and
  searches only the exact key domain. Weapon equip resolves and requires its hand node
  before publishing either attachment pointer or held ownership, so a malformed
  body cannot leave an invisible one-sided held weapon. The exact reconnect
  serializer remains the fail-closed regression detector. Frozen builds,
  focused 655/14, full 61,455/1,164, and native-source guard pass.
- The one B-1097 replacement receipt is retained but rejected before networking
  at 14/57. Correct part lookup exposed B-1098/SP-62: generated relation targets
  could replace private edges with cached-source nodes and corrupt the attached
  HEADSPOT rwdata route. The correction collects the canonical graph, gives each
  DISTANCE/TOGGLE/REORDER relation a private remapped record, preflights every
  indexed record, discards foreign attached-head state, and unifies random and
  catalog heads on one fail-closed transactional clone. Frozen B-1098 builds,
  focused 1,541/16, full 63,731/1,165, and the native-source guard pass. The
  exact follow-up `results-20260814T110358Z.json` completes both stage loads and
  reconnect commit without the former access violation; its rejected
  post-commit gameplay state is separately owned by B-1099.

---

## Known gaps

- **Needler held-model source render proof refreshed (2026-06-17).** The
  custom weapon path now resolves first-person held models through the catalog
  model row registered from the source-owned `.pdmesh` in the installed
  `.pdweapon` chain. Needler smoke proof
  `.claude\smoke-verify-runs\results-20260617T214449Z.json` passed 40/40 with
  `BONDGUN.SOURCE` and `MODASSET.RENDER` for `mod_needler:needler_model`.
  The smoke runner captured screenshot artifacts for the proof, but the
  authoritative rendering proof remains the scoped source/render log assertions.
- **`gfx_sdl2.cpp` has 3 raw hotkeys.** Lines 335-345: Alt+Enter (fullscreen), F10 (mesh debug), backquote (console toggle). Processed before `pdguiProcessEvent`. Backquote duplicates `ACTION_CONSOLE_TOGGLE`. See [pillars/input.md](input.md) Known Gaps for full F-key bypass story.
- **Renderer is OpenGL-only today.** The vtable supports a Vulkan or D3D12 backend, but only OpenGL is implemented. Adding a second backend is feasible (single file replacement) but not scoped.
- **Theme system is large** (3298 lines in `pdgui_theme.cpp`). Mostly justified by the scope (chrome styles + palette + fonts + bundle + UI overrides + texture decode), but a candidate for split into `pdgui_theme_palette.cpp` + `pdgui_theme_chrome.cpp` + `pdgui_theme_loader.cpp` (loader is already split out).

---

## Active design references

- [designs/menus/hud-layer-order.md](../designs/menus/hud-layer-order.md) - HUD render ordering + context-aware gating.
- [designs/in-flight/gpu-swarm-and-test-scenarios.md](../designs/in-flight/gpu-swarm-and-test-scenarios.md) - GPU compute boid swarm benchmark.
- [designs/menus/activemenu-radial-architecture.md](../designs/menus/activemenu-radial-architecture.md) - the legacy GBI radial system (not ImGui).

---

## Where to look

- For ImGui menu work: [pillars/menus.md](menus.md).
- For input authority through the SDL event entry: [pillars/input.md](input.md).
- For theme bundles + mod-supplied themes: [pillars/modding.md](modding.md).
- For build / static linking: [pillars/build-dev-tooling.md](build-dev-tooling.md).
