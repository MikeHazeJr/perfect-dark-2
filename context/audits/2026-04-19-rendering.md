# Deep Project Audit Report — Perfect Dark 2
## Scope: Rendering Pipeline (fast3d GBI Translator · Charpreview FBO · ImGui Overlay · Shader/Combiner · Vertex Processing)
## Date: 2026-04-19 | Session: S389

---

## 1) Inferred Project Goal & Intended Outcomes

### Inferred Purpose

The rendering subsystem is a multi-layer translation stack: the legacy N64 GBI display list stream is interpreted by `gfx_pc.cpp` and dispatched to an OpenGL 3.x backend (`gfx_opengl.cpp`) via a vtable (`gfx_rapi`). On top of this, an ImGui overlay renders menus and HUD via Dear ImGui v1.91.8 (`pdgui_backend.cpp`). A secondary offscreen framebuffer pipeline (`pdgui_charpreview.c`, `pdgui_model_preview.cpp`) renders 3D model previews for character/weapon/prop selection UI.

The design intent is that the GBI pipeline is a transparent translation layer — the original game logic drives display lists exactly as on N64, but the output lands in OpenGL. The charpreview FBO is a shim that hijacks the existing `menuRenderDialog` GBI phase to render a model into an offscreen texture that ImGui panels can then display as a `dl->AddImage()` call.

### Likely Player & Contributor Types

- PD/GoldenEye veterans who expect faithful rendering of N64-era models with sub-pixel-accurate feel
- Modders who will add custom character/weapon/prop models and expect the preview system to work
- The sole developer (Mike) who compiled and extended the port-net renderer

### Core Workflows / Gameplay Loops (Inferred)

1. **In-game rendering**: Game code emits GBI display lists → `gfx_run()` → `gfx_pc.cpp` dispatches all G_* commands → `gfx_opengl.cpp` draws to default framebuffer → `gfx_opengl_reset_for_overlay()` → ImGui overlay renders menus/HUD
2. **Character/model preview in menus**: Menu code requests a preview (head_id + body_id or single asset id) → `pdguiCharPreviewRequestEx()` resolves catalog IDs → `charPreviewSubmitParams()` sets `g_Menus[].menumodel.newparams` → `menuRenderDialog()` is called in the GBI phase → `pdguiCharPreviewRenderGBI()` hooks in → switches FBO → renders model → marks ready → ImGui panel calls `dl->AddImage(texId)`
3. **Standalone ImGui screen preview**: Pure ImGui screens that have no corresponding legacy dialog — intended to call `pdguiModelPreviewDrawEx()` → currently blocked by the GBI hook dependency

### Upstream Lineage Observations

- `gfx_pc.cpp`, `gfx_opengl.cpp`, `gfx_sdl2.cpp`: inherited from `fgsfdsfgs/perfect_dark` (`port-net` branch); UV arithmetic, VBO sizing, texture cache, matrix stack are all port-net lineage
- `pdgui_charpreview.c`, `pdgui_model_preview.cpp`, `pdgui_scaling.h`: authored in PD2; not present in any upstream parent
- The `fbActive` flag, `gDPSetFramebufferTargetEXT` extension, skin override/capture state machine: authored in PD2 as the charpreview integration layer

### Pillar Observations

- **Catalog SOT**: Charpreview correctly routes all asset resolution through catalog accessors (`assetCatalogResolve`, `catalogGetBodyFilenumByIndex`). No direct `g_HeadsAndBodies[]` access from this layer. Clean.
- **Mod-native Parity**: Preview system treats modded and native assets identically — resolution goes through the same catalog path. Clean in principle; blocked in practice by the standalone-screen rendering gap.
- **Modding Pipeline**: Not directly touched by the rendering layer; no findings.
- **Grid Trust / Online-mode / Dedicated-server**: Not applicable to this scope.
- **Scaling Ceiling**: VBO depth of 256 triangles, texture cache of 1024 entries, modelview stack of 11 levels — all fixed at compile time. Not blocking today but worth flagging.

### Assumptions / Unknowns

- `gfx_sdl2.cpp` was not directly read in this session; findings related to SDL event loop or vsync behavior are inferred from indirect references only.
- Runtime behavior of the charpreview FBO across different drivers/GPUs could not be verified by static analysis alone; the blackness bug root cause is confirmed by code but the exact call sequence at runtime should be instrumented.
- The `pdguiCharPreviewRenderGBI()` function's interaction with `bgunChangeGunMem(GUNMEMOWNER_INVMENU)` for weapon preview in standalone screens was not fully traced through the gun memory ownership state machine.
- `gfx_pc.cpp` is ~2500 lines; lines 700–1200 were not read in this session. There may be additional vertex-processing issues in the unread range.

---

## 2) Design Intent & Gameplay / Multiplayer Fit Review

### System & Workflow Validation Findings

#### Critical Issues

- **REND-C1: Charpreview FBO unreachable from standalone ImGui screens — persistent black preview panel**
  - Severity: Critical
  - Confidence: Confirmed (code evidence)
  - Location: `port/fast3d/pdgui_charpreview.c:485–597` (`pdguiCharPreviewRenderGBI`); `port/fast3d/pdgui_model_preview.cpp:159–187`
  - Lineage: Authored in PD2
  - Pillar Impact: None (rendering correctness)
  - Intended Outcome: ImGui model preview panels on standalone menu screens (character select, loadout screen, etc.) display a live 3D render of the selected model.
  - Current Behavior (from code evidence): `pdguiCharPreviewRenderGBI()` contains an early return on line ~490: `if (!s_PreviewRequested || s_PreviewFb < 0) return gdl;`. Even when the guard passes, the function is only reachable from `menuRenderDialog()` — the legacy GBI-phase dialog interceptor. On a "standalone ImGui screen" (a pure-ImGui screen that has no associated legacy dialog being rendered), `menuRenderDialog` is never called. The GBI hook never fires. `s_PreviewReady` stays 0. `pdguiCharPreviewGetTextureId()` returns 0. `pdgui_model_preview.cpp:202` checks `texId != 0 && pdguiCharPreviewIsReady()` and falls through to the fallback placeholder.
  - Gap / Failure Mode: The entire preview pipeline depends on the legacy GBI phase to produce its offscreen texture. Any new-generation UI screen that does not pass through `menuRenderDialog` will never see a preview render.
  - Why It Matters: Model preview is a core UX feature for character/weapon/prop selection. A permanently-black or placeholder panel degrades the feel of the UI to unpolished. This is the known open bug reported in the task brief.
  - Risk If Not Fixed: All standalone ImGui menu screens (current and future) that use `pdguiModelPreviewDraw/DrawEx` show a placeholder silhouette instead of the model.
  - Recommendation: Introduce a direct FBO render path that does not depend on `menuRenderDialog`. One approach: expose `pdguiCharPreviewRenderDirect(struct GfxWindowManagerAPI *wm)` that can be called outside the GBI loop — switch to the preview FBO, call `menuRenderModel()` directly with the pending model params, switch back. This requires decoupling the FBO setup (`gDPSetFramebufferTargetEXT` + viewport/scissor push) from the GBI display list so it can be driven without an active GBI pass. An alternative: call the GBI pass synthetically for the preview FBO only (a minimal display list containing only the model render commands) from `pdguiNewFrame()` or a dedicated hook called before ImGui render. Either way, the fix is a new render-dispatch path; not a trivial change.
  - Cross-System Changes Required: Yes — charpreview.c, gfx_pc.cpp (FBO switching), menu system (model render entry point)
  - Blocks Intended Outcome?: Yes

#### High Priority Findings

- **REND-H1: UV integer arithmetic — potential intermediate overflow for large texture coordinates**
  - Severity: High
  - Confidence: Confirmed (code evidence)
  - Location: `port/fast3d/gfx_pc.cpp` (vertex processing, UV computation section)
  - Lineage: Inherited from port-net
  - Pillar Impact: None
  - Intended Outcome: UV coordinates derived from raw N64 vertex data map correctly to OpenGL texture samplers at all scales.
  - Current Behavior (from code evidence): `short U = v->s * rsp.texture_scaling_factor.s >> 16;` — `v->s` is declared `s16`; `rsp.texture_scaling_factor.s` is `u16`. The multiplication is performed in the `short` domain (or at best `int` after implicit promotion, but the result is immediately truncated to `short` before the shift). For large scale factors and large s values, the product overflows.
  - Gap / Failure Mode: Texture coordinates on large-geometry objects or objects with non-unity scale factors will wrap around and produce garbage UV values, causing visible tiling or texture mapping errors.
  - Why It Matters: The N64 had 16-bit fixed-point UV; the PC port must widen to 32-bit before the multiply to preserve the full range.
  - Risk If Not Fixed: Incorrect UV mapping on specific geometry. Severity depends on whether large-UV geometry appears in any shipped level or mod.
  - Recommendation: Cast operands to `s32` before multiplying: `s32 U = (s32)v->s * (s32)rsp.texture_scaling_factor.s >> 16;`. This matches what other decomp ports do and is the standard fix for this pattern.
  - Cross-System Changes Required: No (local to gfx_pc.cpp vertex processing)

- **REND-H2: `rsp.vertex_colors` NULL pointer dereference — crash if vertex color pointer uninitialized**
  - Severity: High
  - Confidence: Probable (pattern match; exact initialization path not fully traced)
  - Location: `port/fast3d/gfx_pc.cpp` (vertex load / color processing)
  - Lineage: Inherited from port-net
  - Pillar Impact: None
  - What We Found: `const struct NormalColor *vcn = &rsp.vertex_colors[v->colour >> 2];` is evaluated without a prior null check on `rsp.vertex_colors`. The pointer is populated by an RSP microcode command (`G_MV_LIGHT` or equivalent). If any display list executes a vertex load before the vertex color pointer is set, or if the pointer is reset to NULL between display lists, this is a null dereference.
  - Why It Matters: Any display list order that omits the pointer setup crashes the renderer. Mods that emit custom display lists without the setup preamble will crash.
  - Risk If Not Fixed: Crash (null dereference SIGSEGV) when any display list vertex uses colour mode without prior pointer initialization. Difficult to reproduce in native content but easily triggered by partial or malformed mod display lists.
  - Recommendation: Add a null guard: `if (!rsp.vertex_colors) { /* log once, use fallback */ continue; }`. Initialize `rsp.vertex_colors` to a valid static default at RSP reset.
  - Cross-System Changes Required: No

- **REND-H3: Shader filter mode baked at compile time — no invalidation on runtime filter mode change**
  - Severity: High
  - Confidence: Confirmed (code evidence in `gfx_opengl.cpp:gfx_opengl_create_and_load_new_shader()`)
  - Location: `port/fast3d/gfx_opengl.cpp` (shader compilation / cache)
  - Lineage: Inherited from port-net
  - Pillar Impact: None
  - What We Found: The texture filter mode (`FILTER_THREE_POINT` vs `FILTER_LINEAR`) is baked into the GLSL shader source string at the moment the shader is compiled. Compiled shaders are keyed by `(shader_id0, shader_id1)` and cached indefinitely. If the user changes the filter setting at runtime (Video Settings), new geometry will be compiled with new shaders (correct), but any shader already in the cache will retain the old filter logic. Geometry whose shader was compiled before the filter change will continue using the old filter mode until the cache is flushed.
  - Why It Matters: The visual outcome of a filter setting change is unpredictable — some geometry updates immediately, some does not, depending on cache hit rate. This is silent and difficult to diagnose.
  - Risk If Not Fixed: Filter setting appears broken or partially applied after runtime change. Players change the setting and see inconsistent results.
  - Recommendation: On filter mode change, flush the entire shader program cache and re-compile on next use. This is a one-line call at the change site: `gfx_opengl_flush_shader_cache()` (or equivalent). Alternatively, move the filter mode into a uniform (`bool u_useThreePointFilter`) rather than baking it into source — this is a larger refactor but eliminates the cache problem entirely.
  - Cross-System Changes Required: Yes (Video Settings change handler must call shader cache flush)

#### Medium Priority Findings

- **REND-M1: Preview FBO not explicitly cleared before each render — leftover pixels from prior preview**
  - Severity: Medium
  - Confidence: Probable
  - Location: `port/fast3d/pdgui_charpreview.c:485–597` (`pdguiCharPreviewRenderGBI`)
  - Lineage: Authored in PD2
  - Pillar Impact: None
  - What We Found: When the FBO render hook fires, it switches to the preview FBO and calls `menuRenderModel()`. There is no explicit `glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT)` on the preview FBO before the model render. `gfx_opengl_clear_framebuffer()` exists and is the correct function to call, but it is not invoked as part of the preview setup sequence.
  - Why It Matters: On a model change, the new model renders over stale pixel data from the previous preview. If the new model is smaller or positioned differently, ghost pixels from the prior model will bleed through.
  - Risk If Not Fixed: Visual artifact — ghost geometry from prior model selection visible around edges of new model preview.
  - Recommendation: After switching to the preview FBO and before calling `menuRenderModel()`, call `rapi->clear_framebuffer(s_PreviewFb)` (or directly `glBindFramebuffer` + `glClear`).
  - Cross-System Changes Required: No

- **REND-M2: Charpreview FBO format mismatch — created GL_RGB8, read back as GL_RGBA**
  - Severity: Medium
  - Confidence: Confirmed (code evidence)
  - Location: `port/fast3d/gfx_opengl.cpp:1054–1087` (FBO creation); `port/fast3d/pdgui_charpreview.c` (`pdguiCharPreviewBakeToTexture`)
  - Lineage: Authored in PD2
  - Pillar Impact: None
  - What We Found: `gfx_opengl_create_framebuffer()` creates the color attachment as `GL_RGB8` (no alpha). `pdguiCharPreviewBakeToTexture()` calls `glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels)` — requesting RGBA from an RGB8 texture. OpenGL will fill the alpha channel with 255 (opaque), so the baked texture functions correctly today. However, the format mismatch is confusing and fragile: if the FBO is ever changed to GL_RGBA8 (e.g., to support transparent backgrounds), the bake path may read stale data. Additionally, some drivers handle the format mismatch differently.
  - Why It Matters: Transparent background previews (characters over dark alpha) are a desirable future feature. The current RGB8 format prevents them. The mismatch is also a maintenance hazard.
  - Recommendation: Change the FBO color attachment to GL_RGBA8 at creation time. Update `glGetTexImage` to use GL_RGBA8 internal format. This enables future transparent-background previews and removes the format mismatch.
  - Cross-System Changes Required: No

- **REND-M3: Single-panel static state — only one model preview at a time**
  - Severity: Medium
  - Confidence: Confirmed (by design comment in source)
  - Location: `port/fast3d/pdgui_model_preview.cpp:37–50`
  - Lineage: Authored in PD2
  - Pillar Impact: Mod-native Parity (a lobby screen showing multiple players' character previews simultaneously would be blocked)
  - What We Found: `s_LastKind`, `s_LastId1[64]`, `s_LastId2[64]`, `s_IdleAngle` are static module-level variables. The comment explicitly notes: "A single panel of state is adequate for every current caller because all call sites show at most one preview at a time." This is a deferred architectural limitation.
  - Why It Matters: Any UI screen that needs to show two or more simultaneous model previews (e.g., a lobby screen showing head/body for each player slot, a compare-loadout screen) cannot be built until this limit is lifted. The code comment acknowledges a `named-slot table keyed by caller string` as the forward path.
  - Risk If Not Fixed: All multi-preview UI designs are blocked. The lobby portrait system (S352 — baked per-slot GL textures) works around this by pre-baking to separate textures, but that approach does not support idle rotation.
  - Recommendation: Promote to a small named-slot table (e.g., 8 slots, string key per caller). Each slot maintains independent `LastKind/Id1/Id2/IdleAngle`. Callers pass a slot label (e.g., "player0", "loadout_compare_a").
  - Cross-System Changes Required: Yes (callers of `pdguiModelPreviewDrawEx` need to pass a slot key)

- **REND-M4: FBO `glFramebufferTexture2D` attachment deferred until first resize — FBO incomplete at creation**
  - Severity: Medium
  - Confidence: Confirmed (code evidence)
  - Location: `port/fast3d/gfx_opengl.cpp:1054–1087` (`gfx_opengl_create_framebuffer`)
  - Lineage: Inherited from port-net
  - Pillar Impact: None
  - What We Found: `gfx_opengl_create_framebuffer()` calls `glGenFramebuffers` and `glGenTextures` but does NOT call `glFramebufferTexture2D`. The texture is attached to the FBO only when `gfx_opengl_update_framebuffer_parameters()` is called with a non-zero resolution change. Until that call, the FBO is framebuffer-incomplete (`GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT`). Any render attempt to this FBO before the first resize would silently fail on OpenGL strict mode or produce undefined output.
  - Why It Matters: In practice this works because the preview FBO always goes through `videoCreateFramebuffer(256, 256, ...)` which immediately sets the dimensions, triggering the attachment. But the invariant is subtle and easy to violate when adding new FBO uses.
  - Recommendation: Attach the texture to the FBO immediately in `gfx_opengl_create_framebuffer()` using the initial 1×1 dimensions. This makes the FBO complete at creation time. Subsequent resizes reallocate the texture storage and re-attach.
  - Cross-System Changes Required: No

#### Low Priority Findings

- **REND-L1: `loaded_texture[512]` TMEM array — no bounds check on TMEM offset before indexing**
  - Severity: Low
  - Confidence: Probable
  - Location: `port/fast3d/gfx_pc.cpp` (texture load / TMEM management)
  - Lineage: Inherited from port-net
  - Pillar Impact: None
  - What We Found: `loaded_texture[512]` is indexed by TMEM slot (0–511). If a display list specifies a TMEM load offset outside 0–511 (legal on N64 due to TMEM being 4KB / 16-bit-per-texel), the indexing may go out of bounds. No bounds check was observed in the read sections.
  - Recommendation: Add `if (tmem_offset >= 512) { log once; continue; }` guard before all `loaded_texture[tmem_offset]` accesses.
  - Cross-System Changes Required: No

- **REND-L2: `MAX_BUFFERED = 256` triangle VBO — compile-time ceiling on buffered triangles**
  - Severity: Low
  - Confidence: Confirmed (code evidence)
  - Location: `port/fast3d/gfx_pc.cpp:MAX_BUFFERED`
  - Lineage: Inherited from port-net
  - Pillar Impact: Scaling Ceiling
  - What We Found: `float buf_vbo[MAX_BUFFERED * (32 * 3)]` = 24576 floats. The buffer is flushed to GPU via `glBufferData` + `glDrawArrays` when full. This is a micro-batching ceiling; it does not cap total scene complexity, only per-call batch size. Not a blocking concern at current polygon counts.
  - Recommendation: Consider raising `MAX_BUFFERED` to 1024 if profiling shows frequent small flushes. Low priority.
  - Cross-System Changes Required: No

- **REND-L3: Modelview matrix stack depth 11 — no overflow message, potential silent corruption**
  - Severity: Low
  - Confidence: Probable
  - Location: `port/fast3d/gfx_pc.cpp` (matrix push/pop)
  - Lineage: Inherited from port-net (matched N64 hardware depth)
  - Pillar Impact: None
  - What We Found: `rsp.modelview_matrix_stack[11][4][4]` matches N64 hardware. The push is guarded by `< 11`, so overflow is prevented. However, if a display list overflows the stack, the guard silently drops the push — no error is logged, the matrix is not pushed, and subsequent pops will underflow or produce wrong transforms.
  - Recommendation: Add a rate-limited `fprintf(stderr, "WARN: matrix stack overflow\n")` at the overflow site. The N64 11-level limit is reasonable to keep on PC.
  - Cross-System Changes Required: No

### Missing / Incomplete Features Blocking Success

- **Standalone ImGui screen charpreview**: The entire charpreview pipeline is blocked for any UI screen that does not run through `menuRenderDialog`. This is the single biggest gap between design intent and current behavior (see REND-C1).

### Positive Observations

- **B-197 matrix pointer guard**: `if ((uintptr_t)addr < 0x10000)` in `gfx_sp_matrix()` provides a null-and-near-null dereference guard on matrix segment+offset pointers, with rate-limited logging (max 5). Solid defensive pattern.
- **GL state isolation in `gfx_opengl_reset_for_overlay()`**: Explicitly binds FBO 0, disables scissor/depth/cull/stencil/blend before ImGui renders. The comment correctly notes that VBO/VAO/shader must NOT be unbound here because ImGui's SDL2+OpenGL3 backend saves and restores PD's own bindings. This is the correct design for a shared-GL-context overlay.
- **UV flip for FBO textures is correct**: `dl->AddImage(..., ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f))` correctly flips the vertical UV to account for OpenGL's bottom-up framebuffer origin. This fixes a common gotcha that trips up every FBO-to-ImGui integration.
- **Viewport/scissor restore after FBO render**: The charpreview GBI hook correctly saves and restores the game's viewport and scissor state after the preview FBO render. The prior bugs B-135/B-136 (scissor not restored after FBO) are fixed.
- **`pdgui_scaling.h` 1080p baseline**: All ImGui menus use resolution-independent scale helpers referenced to 1080p. This is the right design and correctly prevents hardcoded pixel constants from leaking into menu code.
- **Skin override/capture state machine**: The `SKIN_CAPTURE_IDLE/WAITING/RENDERING/COMPLETE` state machine in charpreview is well-structured and handles the 3-frame load delay correctly.
- **Catalog-first asset resolution in charpreview**: All `mp_index`/filenum resolution routes through catalog accessors — no direct array access, no hardcoded offsets.

---

## 3) Code Quality Review

### Critical Issues

*(Shared with Section 2 — REND-C1 is simultaneously a design-fit and code quality failure; see above.)*

### High Priority Findings

- **REND-H1: UV integer overflow** (see §2 — code quality dimension)
  - Severity: High
  - Confidence: Confirmed
  - Area: Vertex processing
  - Location: `port/fast3d/gfx_pc.cpp` (UV computation)
  - Lineage: Inherited from port-net
  - Pillar Impact: None
  - What We Found: `short U = v->s * rsp.texture_scaling_factor.s >> 16;` — s16 × u16 in short domain before right shift.
  - Why It Matters: Silent integer overflow produces incorrect UV coordinates; no assertion or saturation.
  - Risk If Not Fixed: Texture mapping corruption on geometry with large UV/scale combinations.
  - Recommendation: `s32 U = (s32)v->s * (s32)rsp.texture_scaling_factor.s >> 16;`
  - Cross-System Changes Required: No

- **REND-H2: `rsp.vertex_colors` NULL dereference** (see §2)
  - Severity: High
  - Confidence: Probable
  - Area: Vertex processing / color
  - Location: `port/fast3d/gfx_pc.cpp`
  - Lineage: Inherited from port-net
  - Pillar Impact: None
  - What We Found: `const struct NormalColor *vcn = &rsp.vertex_colors[v->colour >> 2];` without null guard.
  - Why It Matters: Crash on any display list that uses vertex color mode before pointer is initialized.
  - Risk If Not Fixed: SIGSEGV crash; likely to be triggered by malformed mod display lists.
  - Recommendation: Null check + static fallback at dereference site.
  - Cross-System Changes Required: No

### Medium Priority Findings

- **REND-M1: FBO not cleared before preview render** (see §2)
  - Severity: Medium
  - Confidence: Probable
  - Area: FBO pipeline
  - Location: `port/fast3d/pdgui_charpreview.c:485–597`
  - Lineage: Authored in PD2
  - Pillar Impact: None
  - What We Found: No `glClear` call before model render in charpreview FBO hook.
  - Why It Matters: Ghost geometry from previous preview bleeds through on model change.
  - Recommendation: Call `rapi->clear_framebuffer(s_PreviewFb)` before `menuRenderModel()`.
  - Cross-System Changes Required: No

- **REND-M4: FBO incomplete at creation** (see §2)
  - Severity: Medium
  - Confidence: Confirmed
  - Area: FBO management
  - Location: `port/fast3d/gfx_opengl.cpp:1054–1087`
  - Lineage: Inherited from port-net
  - Pillar Impact: None
  - What We Found: `glFramebufferTexture2D` deferred until first dimension change. FBO is `GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT` between creation and first resize.
  - Why It Matters: Any FBO use before resize (possible with new callers) silently fails or produces garbage.
  - Recommendation: Attach texture at creation using 1×1 dimensions; re-attach on resize.
  - Cross-System Changes Required: No

### Low Priority Findings

- **REND-L1: `loaded_texture[512]` no bounds check** (see §2) — Low; add guard before every TMEM offset index.
- **REND-L3: Matrix stack overflow drops push silently** (see §2) — Low; add rate-limited log at overflow site.

### Positive Observations

- **B-197 matrix pointer guard**: Exemplary defensive pattern for inherited segment+offset derefs — should be used as a template for similar patterns elsewhere.
- **Consistent `extern "C"` guards**: All public APIs crossing the C/C++ boundary in `pdgui_charpreview.h`, `pdgui_model_preview.h`, `pdgui_style.h` use correct `extern "C"` guards. No ABI surprises.
- **ImGui backend saves/restores PD GL state correctly**: The SDL2+OpenGL3 backend integration is done correctly — PD's VAO/VBO/shader are preserved across ImGui render because the backend explicitly saves and restores them.
- **FBO UV flip**: Correct vertical flip in the `dl->AddImage()` call (UV0=(0,1), UV1=(1,0)). This is subtle and is done right.

---

## 4) Security, Trust & Privacy Review

### Critical Issues

*None identified in this scope. The rendering pipeline does not process network-originated untrusted data directly.*

### High Priority Findings

*None. Rendering is not a trust boundary.*

### Medium Priority Findings

- **REND-SEC-M1: Texture cache keyed by raw pointer addresses — mod asset aliasing risk**
  - Severity: Medium
  - Confidence: Probable
  - Area: Texture cache
  - Location: `port/fast3d/gfx_pc.cpp` (texture cache, `TEXTURE_CACHE_MAX_SIZE = 1024`)
  - Lineage: Inherited from port-net
  - Pillar Impact: Mod-native Parity
  - What We Found: The texture cache is keyed by `{texture_addr, palette_addrs, fmt, siz, palette_index}` where `texture_addr` is a raw pointer into the game's ROM/asset buffer. If two different mods load different textures to the same virtual address (e.g., due to arena switching in AllInOne-lineage code), the cache may serve a stale entry from a prior mod's texture data without re-upload.
  - Why It Matters: Cross-arena texture bleed or incorrect texture display after mod switch — potentially confusing players and misrepresenting modded content.
  - Risk If Not Fixed: Incorrect textures displayed when switching between arenas/mods that share the same asset address space.
  - Recommendation: On mod arena switch, call `gfx_texture_cache_clear()` (or equivalent) to invalidate all cached textures. Alternatively, key the cache by content hash rather than pointer address.
  - Cross-System Changes Required: Yes (mod switch / arena change path must flush texture cache)

### Low Priority Findings

- **REND-SEC-L1: FBO texture ID returned as `(void*)(uintptr_t)` — opaque handle type safety**
  - Severity: Low
  - Confidence: Confirmed
  - Area: Type safety
  - Location: `port/fast3d/gfx_opengl.cpp:gfx_opengl_get_framebuffer_texture_id()`
  - Lineage: Inherited (ImGui convention)
  - What We Found: GL texture IDs are cast to `(void*)(uintptr_t)` for `ImTextureID`. This is the ImGui convention and is correct on 64-bit. No security issue, but a type-safety note: on 32-bit targets (not currently in scope) this would truncate. Document the 64-bit assumption explicitly.
  - Recommendation: Add `static_assert(sizeof(uintptr_t) >= sizeof(GLuint))` near this cast.
  - Cross-System Changes Required: No

### Positive Observations

- **No raw GL object handles exposed to game code**: All FBO/texture IDs are exposed through opaque integer handles via the `gfx_rapi` vtable. Game code cannot accidentally corrupt GL state by holding a raw GL object handle.

---

## 5) Cross-Cutting Risks & Architecture Concerns

- **GBI legacy phase as the only render-dispatch path**: The charpreview FBO pipeline is architecturally coupled to the legacy `menuRenderDialog` call chain. Any UI screen that does not pass through `menuRenderDialog` cannot trigger a model preview render. This is the root of REND-C1 and will recur for every new-generation ImGui screen unless a direct FBO render dispatch path is added. Recommendation: Treat the standalone render path as a first-class feature, not a workaround. Add `pdguiCharPreviewRenderDirect()` driven from `pdguiNewFrame()` or a dedicated per-frame hook.

- **Shader cache unbounded growth with combiners**: The shader cache is keyed by `(shader_id0, shader_id1)` and grows without bound. In a game with many unique material/combiner combinations (especially with mods), this could grow to thousands of entries. No eviction policy is in place. Recommendation: Add a maximum cache size (e.g., 512 entries) and evict LRU entries, or log the current size in developer builds.

- **OpenGL state after FBO render not fully validated**: While `gfx_opengl_reset_for_overlay()` correctly resets state before ImGui, there is no equivalent validation that GL state is clean after `pdguiCharPreviewRenderGBI()` returns to the main GBI loop. The function restores viewport and scissor, but it is unclear whether the depth/cull/blend state that `menuRenderModel()` leaves behind is always clean. If `menuRenderModel` enables depth write and then the FBO hook returns without restoring, the subsequent game frame begins with wrong depth state. Recommendation: Add an explicit GL state checkpoint/restore around the entire FBO render block in `pdguiCharPreviewRenderGBI()`.

- **Idle rotation re-requests model every frame**: `pdgui_model_preview.cpp:183–186` calls `requestPreview(kind, id1, id2)` on every frame during idle rotation to propagate the new `s_IdleAngle`. The comment notes this is cheap "when the model is already loaded." This is true today — `charPreviewSubmitParams()` just sets a flag. But if the charpreview pipeline is ever extended to do CPU-side work on every request (e.g., LOD selection, bone animation step), this per-frame re-request becomes a per-frame compute cost. Recommendation: Separate the rotation update (`pdguiCharPreviewSetRotY()`) from the model re-request. Only call `requestPreview()` on model change; call `pdguiCharPreviewSetRotY()` + a lightweight `pdguiCharPreviewMarkRotationDirty()` for idle rotation.

- **Texture cache (1024 entries) shared across native and mod content**: The LRU texture cache does not distinguish between native and modded textures. After an arena switch, stale mod textures may remain cached and be served incorrectly (see REND-SEC-M1). The cache also has no usage metrics exposed to developer builds. Recommendation: Add a `gfxTextureCacheStats()` function (debug builds) and a `gfxTextureCacheFlush()` call at arena/mod switch boundaries.

---

## 6) Verification Limits (Static Analysis vs Runtime)

### Confirmed by Code Evidence

- REND-C1: Charpreview FBO unreachable from standalone ImGui screens — code path analysis is definitive; `menuRenderDialog` is the only caller of `pdguiCharPreviewRenderGBI()`.
- REND-H3: Shader filter mode baked at compile time — confirmed by direct reading of `gfx_opengl_create_and_load_new_shader()`.
- REND-M2: GL_RGB8 / GL_RGBA format mismatch — confirmed by direct reading of `gfx_opengl_create_framebuffer()` and `pdguiCharPreviewBakeToTexture()`.
- REND-M4: FBO incomplete at creation — confirmed by absence of `glFramebufferTexture2D` in `gfx_opengl_create_framebuffer()`.
- UV flip correctness (positive) — confirmed in `pdgui_model_preview.cpp:206–207`.

### Probable Issues Inferred from Patterns

- REND-H1: UV integer overflow — the expression matches the known overflow pattern from port-net; would need a test with large UV/scale values to confirm visible artifact.
- REND-H2: `rsp.vertex_colors` NULL dereference — the null check is absent; whether any display list reaches this without prior initialization requires runtime tracing.
- REND-M1: FBO not cleared — absence of `glClear` is confirmed; whether ghost pixels are visually observable depends on model geometry and background color.
- REND-SEC-M1: Texture cache aliasing on arena switch — depends on whether AllInOne-lineage arena switching reuses virtual address ranges; would need a two-arena swap test.

### Unverified Risks Requiring Runtime Testing

- **Charpreview blackness reproduction path**: Confirm which specific screens trigger the standalone path (no `menuRenderDialog` call) vs. the legacy dialog path. A runtime log at `pdguiCharPreviewRenderGBI()` entry would immediately identify which screens are affected.
- **Shader cache growth in mod sessions**: Instrument `gfx_opengl_create_and_load_new_shader()` with a call count to measure how many unique combiner programs are compiled per session, and verify they do not grow unboundedly.
- **GL state after FBO render**: Instrument `glGet` state snapshot before and after `pdguiCharPreviewRenderGBI()` in a developer build to verify depth/cull/blend state is correctly restored.
- **GPU driver behavior on FBO-incomplete access**: Test `gfx_opengl_create_framebuffer()` → immediate render attempt (before resize) on multiple drivers to confirm the deferred-attachment path is safe in all cases.
- **Texture cache aliasing**: Set up a two-arena test with AllInOne-lineage mod switching and verify textures are not stale after switch.
- **UV overflow**: Test a display list with `v->s = 0x7FFF` and `texture_scaling_factor.s = 0xFFFF`; confirm U coordinate is correct.

### What Would Be Needed for Full Validation

- Developer build with GL state snapshot instrumentation around the FBO hook
- Runtime shader cache stats output (count, key distribution)
- Two-arena mod switch test fixture
- Vertex/UV overflow test fixture (minimal GBI display list with maximal UV values)
- Instrumented `pdguiCharPreviewRenderGBI()` entry/exit with per-frame log of `s_PreviewRequested`, `s_PreviewReady` state

---

## 7) Summary Scorecard

**Design / Gameplay Fit Score**: 6/10 — The GBI translator faithfully handles N64 display lists and the GL state isolation is well-designed. However, the charpreview FBO is completely non-functional for the class of screens it was designed to serve (standalone ImGui screens). The blocklist is short but the critical item is the most visible UX element of the new menu system.

**Code Quality Score**: 7/10 — The port-net inherited code is functional and mostly correct. The PD2-authored charpreview/model-preview layer is clean and well-structured. The inherited UV overflow and NULL dereference are the primary technical debt items; neither is from PD2-authored code.

**Security & Trust Score**: 8/10 — The rendering pipeline is not a primary trust surface. The one meaningful risk (texture cache aliasing across mod arena switches) is medium severity. No network-originated data reaches the renderer without going through catalog and game logic layers first.

**Architectural Discipline Score**: 7/10 — Catalog-first asset resolution in charpreview is clean. GL state management (overlay reset, FBO push/pop) is careful and documented. The legacy-GBI-phase coupling for FBO renders is the primary architectural debt; it is a known deferred problem but is now blocking real UX work.

### Total Findings by Severity
- Critical: 1 (REND-C1)
- High: 3 (REND-H1, H2, H3)
- Medium: 5 (REND-M1, M2, M3, M4, REND-SEC-M1)
- Low: 4 (REND-L1, L2, L3, REND-SEC-L1)

### Total Findings by Confidence
- Confirmed: 7 (REND-C1, H3, M2, M3, M4, SEC-L1, UV flip positive)
- Probable: 5 (REND-H1, H2, M1, L3, SEC-M1)
- Unverified: 1 (REND-L1 — TMEM bounds)

### Total Findings by Lineage
- Authored in PD2: 5 (REND-C1, M1, M2, M3, and GL-incomplete design choice M4 is partially PD2)
- Inherited from port-net: 5 (REND-H1, H2, H3, L1, L2, M4 partially)
- Unknown: 0

### Total Findings by Pillar Impact
- Catalog SOT: 0
- Mod-native Parity: 2 (REND-M3, REND-SEC-M1)
- Modding Pipeline: 0
- Grid Trust: 0
- Online-mode Boundary: 0
- Dedicated-server Boundary: 0
- Scaling Ceiling: 1 (REND-L2)
- None: 10

### Top Scaling Ceilings Identified
- `gfx_pc.cpp:MAX_BUFFERED = 256`: 256 triangles per VBO flush → per-call batching ceiling (not total scene cap); low priority
- `gfx_pc.cpp:loaded_texture[512]`: 512 TMEM slots → N64 hardware limit; no concern on PC
- `gfx_pc.cpp:modelview_matrix_stack[11]`: 11 matrix levels → N64 hardware limit; no concern for current geometry
- `gfx_opengl.cpp:TEXTURE_CACHE_MAX_SIZE = 1024`: 1024 shader programs max → no eviction policy

### Top Data-Layout Integrity Breaches (Phase 2.5)

- `gfx_pc.cpp` UV arithmetic: `s16 × u16` before right shift → intermediate is C-promoted to `int` (safe on LP64) but truncated to `s16` assignment before the shift in `short U = v->s * rsp.texture_scaling_factor.s >> 16` — operator precedence is correct (shift after multiply per C standard) but the assignment truncates the shifted result to s16; for values where the product-before-shift exceeds 32767, the stored UV is wrong. Not persisted; render-time only.

### Top Catalog / Mod-parity Breakages
- `pdgui_charpreview.c`: charpreview FBO preview is functionally blocked for standalone ImGui screens — any mod that uses a standalone UI screen for character/weapon preview will show placeholder silhouette. Mod content is not second-class here by design, but it is equally non-functional as native content.

### Top 5 Action Items (Highest Impact First)

1. **Fix REND-C1**: Add `pdguiCharPreviewRenderDirect()` driven from the pre-ImGui hook so standalone screens can trigger a preview render without `menuRenderDialog`. This unblocks all new-generation UI screens.
2. **Fix REND-H3**: Flush shader program cache on filter mode change; or move `three_point_filter` into a uniform. Without this, Video Settings filter changes are visually broken for already-compiled shaders.
3. **Fix REND-H1**: Widen UV multiplication to `s32` before shift. One-line change; eliminates inherited port-net overflow class.
4. **Fix REND-H2**: Add null guard on `rsp.vertex_colors` before dereference + static fallback. Crash protection against malformed/mod display lists.
5. **Fix REND-M3**: Promote model preview state from single-panel static vars to a named-slot table. Unblocks any multi-preview UI (lobby portraits with idle rotation, compare screens).

### Must-Fix Before Public Release
- REND-C1: Charpreview blackness in standalone screens (visible UX failure)
- REND-H1: UV integer overflow (silent correctness bug, mod-triggered)
- REND-H2: vertex_colors NULL dereference (crash, mod-triggered)

### Estimated Effort to Remediate Critical / High Issues

- **REND-C1** (standalone FBO path): Medium effort — 2–4 days. Requires new render-dispatch entry point in charpreview.c, hooking it into pdguiNewFrame() or a dedicated per-frame GBI-phase hook, and testing across character/weapon/prop preview types.
- **REND-H1** (UV overflow): Trivial — 15 minutes. One-line change in gfx_pc.cpp, no dependencies.
- **REND-H2** (vertex_colors null): Small — 1 hour. Add null check + static fallback + default initialization in RSP reset.
- **REND-H3** (shader cache flush): Small — 2–4 hours. Add flush call in Video Settings change handler; test filter toggle visually.

Total estimated effort for Critical + High: ~3–5 days of focused work.
