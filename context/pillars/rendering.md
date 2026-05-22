# Rendering

> fast3d: N64 GBI display lists translated at runtime to OpenGL. Function-pointer-table backend abstraction (`GfxRenderingAPI` + `GfxWindowManagerAPI`). Dear ImGui v1.91.8 overlay through the same SDL2 + OpenGL context. Theme system decodes ROM textures to RGBA32 and uploads as GL textures.

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
- BG visual display-list extraction for map authoring (c3812-s9, 2026-05-21): `.pdscenario` archives export `visual/scene.obj`, `visual/scene.mtl`, `visual/materials.tsv`, and decoded `visual/textures/*.tga` by walking BG `G_VTX` / `G_TRI1` / `G_TRI4` display lists, C0/G_NOOP material commands, and `textureslist` / `texturesdata`. This is an offline asset-authoring export; runtime rendering still uses the fast3d translator.
- Discord Rich Presence (D7, shipped S348).
- Foreground drawlist primitives for toasts, glyphs, achievement banners.

---

## What is in flight

- B-366 Falcon 2 first-person stretch is fixed pending Mike visual retest. The remaining in-game artifact after the OBJ exporter fix was the Falcon laser-sight beam rendering while the first-person gun root was moving: the near end followed the animated laser node, while the far end stayed anchored at the crosshair and looked like barrel geometry stretching. `bgunShouldRenderLasersight()` now only allows the Falcon beam in steady aim-capable hand states and frees it during busy/non-steady gun animations. Focused `[bondgun][laser][static]` passed in isolated session `falconanim`, and the queued all-target build passed.
- B-346 credits/fog alpha artifact is fixed pending second playtest. Mike's first `gfxalpha` run showed the broad fog-alpha output-alpha path was wrong: characters/weapons became translucent and credits masks still rendered as solid colored quads. Fast3d now keeps `G_BL_A_FOG` as color-fog state only, material alpha is back to normal translucent `MEM,1MA`/texture-edge cases, strict `G_RM_ADD` additive fog uses only the exact `IN,FOG_ALPHA,MEM,1` tuple with additive blending, and `text0f153628()` explicitly resets texture enable/scale before CI4 glyph drawing. Mike is manually retesting credits particles/text and foggy stage effects from isolated build `gfxalpha`.
- HUD layer order discipline per [designs/menus/hud-layer-order.md](../designs/menus/hud-layer-order.md) (implemented).
- GPU swarm benchmark per [designs/in-flight/gpu-swarm-and-test-scenarios.md](../designs/in-flight/gpu-swarm-and-test-scenarios.md) (Phase 1 design; Phase 2 gated on Mike's call on F.1 GL 4.3 vs transform feedback).

---

## Known gaps

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
