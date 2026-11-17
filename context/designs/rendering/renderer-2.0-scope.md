# Renderer 2.0 scope: classic and remastered renderers over one game

**Status:** scope only. This was a read-only investigation: no code was changed,
built or run, and no game tests were run. Every claim carries a tag (legend
below). Updated 2026-11-16 with Mike's decisions D1 to D7 (section 0): D2 picked
option B (our own renderer on a cross-API library, bgfx), and D5 to D7 settle
the graphics API order, DLLs and ray tracing.

**Milestone:** 2.0. D-004 limits 1.0 to the base game plus Theater, so this work
is post-1.0 ([D] `tools/Workbench/data/roadmap.json`, item D-004).

**Code read:** `origin/dev` at `361abab8a` (2026-11-16). `origin/dev` moved from
`55812690e` to `361abab8a` while this was being written (the crystal pistol
merge), and every dev line number below was re-checked at `361abab8a`. Also
read through git objects only, without touching its worktree:
`origin/week-pd/core` at `ed1756cf1` (CORE's unmerged B-1172 work).

**Workbench:** no 2.0 or rendering item exists yet. This document is committed
under D-004. Section 10 lists the decisions that should become Workbench
decision items.

The request, in Mike's words: "how practical would it be to layer a new
renderer over the game... PBR, improved lighting, improved instancing", like
Halo CE Anniversary's dual renderer, with a classic/remastered toggle. "Scope it
fully, we will do so for the 2.0 milestone."

## Tags

| Tag | Meaning |
|---|---|
| **[V]** | Verified in code on `origin/dev` `361abab8a` at the `file:line` given. **[V search]** means a search of the named files found nothing. |
| **[V core]** | Verified in code on `origin/week-pd/core` `ed1756cf1`; not on dev yet. |
| **[D]** | Stated in a project document at the `file:line` given and not re-checked in code. Treat it as inferred. |
| **[I]** | Inferred: reasoning from verified facts, not seen in code or in game. Every effort estimate is [I]. |
| **[W]** | From an external web source checked on 2026-11-16 (listed in the appendix); not verified locally. |
| *Proposal* | A recommendation, not a claim about the code. |

Effort sizes are for one focused stream of work (AI sessions plus Mike's
review), excluding playtest acceptance and art: **S** up to 1 week, **M** 1 to 3
weeks, **L** 3 to 6 weeks, **XL** over 6 weeks. [I]

Unless a path says otherwise, `bg.c`, `lv.c`, `chr.c`, `prop.c`, `propobj.c`,
`dlights.c`, `sky.c`, `env.c`, `bondgun.c`, `bondview.c` and `player.c` are in
`src/game/`; `model.c`, `vi.c` and `rng_c.c` are in `src/lib/`; `gfx_*.cpp` and
`scenario_scene_renderer.cpp` are in `port/fast3d/`.

---

## 0. Decisions (Mike, 2026-11-16)

These answer questions 1, 2, 3, 5, 12, 13 and 14 of section 10 and settle part
of 4. The sections they change are listed at the end.

- **D1. The toggle works like Halo: Combat Evolved Anniversary.** One switch
  between classic and remastered, and it is instant. In Anniversary the original
  engine kept the gameplay, Saber's own engine drew the new visuals, and the
  switch was instant ([W]). The same shape here: one simulation, two renderers,
  one switch, with quality settings on the remastered side only. One lesson
  from that game: its classic mode later had to be fixed because it no longer
  matched the original ([W]). So classic stays the tested reference (R9).
- **D2. Engine and graphics API: option B.** Our own renderer on a thin
  cross-API library, with classic moved onto the same library (5.8). The
  working library is bgfx: BSD-2, MinGW-built inside MAME, with D3D11, D3D12,
  Vulkan and OpenGL 4.3+ backends ([W]). Consequences:
  - **Correction to 5.8:** bgfx lists no ray tracing support ([W], nothing in
    its feature list), so B with bgfx brings D3D11, D3D12 and Vulkan, not
    hardware ray-traced GI. If ray tracing ever matters, the library would be
    Diligent or NVRHI instead [I]. The GI plan in 7.3 does not need it.
  - Classic moves first, and must stay pixel-identical (R10). Its biggest
    change: fast3d writes GLSL at runtime for each color-combiner state
    (`gfx_opengl.cpp:236-302`, called from `gfx_pc.cpp:308-316` [V]), while
    bgfx only loads shaders compiled ahead of time [I]. Classic therefore needs
    a combiner "uber-shader" driven by uniforms, or a fixed set of precompiled
    variants.
  - bgfx's OpenGL backend needs GL 4.3+ ([W]), so today's GL 3.0 fallback
    goes away; older Windows GPUs would use D3D11 instead [I].
  - bgfx's D3D11 backend loads `d3d11.dll` and DXGI at runtime and also tries
    optional vendor libraries (AMD AGS, NVAPI) ([W]); D6 below sets the rule
    for that.
- **D3. Remastered base stages get PBR materials and dynamic lighting.** This is
  the full-relight direction from the earlier question, not "keep the baked
  lighting and add to it". Fixture lights become real lights, materials become
  PBR, and the baked vertex lighting no longer defines the remastered look.
  Consequences: indirect light (GI) and shadows from static geometry are now
  required for base stages, not only for Forge maps (7.1 to 7.3).
- **D4. The remastered renderer is built in.** It is an engine layer that each
  player can toggle, not a mod. It must look right with no packs installed, so
  base materials need a built-in PBR derivation (7.4, 8.9). Art packs are
  ordinary mods under the existing mod rules (8.8), so R8 is dropped.
- **D5. Graphics API order: OpenGL first, then D3D11 by default.** Classic moves
  onto bgfx's OpenGL backend first, so the parity captures compare two
  renderers on the same driver. Then D3D11 becomes the Windows default, with
  GL 4.3 as the fallback and Vulkan as an option. Vulkan needs nothing from
  us: drivers that support it install its loader, and where it is missing the
  option is hidden [I]. Exact pixel identity may not hold across APIs, so on
  D3D11 each remaining difference is explained and accepted (phase 2a,
  section 11) [I].
- **D6. DLLs: nothing for players to install, and a DLL folder if one is ever
  needed.** In Mike's words: "DLL's if needed will go in a DLL folder. It should
  be inclusive so people who download it can play it without prerequisites."
  The rule:
  - Static linking stays the default, as today (`CMakeLists.txt:802-809` [V]).
  - Graphics DLLs that ship with Windows (`opengl32`, `d3d11`, `dxgi`, `d3d12`)
    are loaded from the system at runtime and never bundled.
  - Our copy of bgfx is patched so release builds never try AMD AGS, NVAPI or
    RenderDoc.
  - Any other DLL that is truly needed ships inside the release, in a `dll/`
    folder at the install root beside `data/` and `mods/` (*proposal* for the
    name). A download then runs with nothing to install: no Visual C++
    redistributable, no DirectX installer, no SDK.
  - Windows does not search subfolders of the program's folder for DLLs ([W]).
    The client adds `dll/` to its search path at startup
    (`SetDefaultDllDirectories` with `AddDllDirectory` [W]) or loads each DLL
    by full path. DLLs in the program's import table are resolved before our
    code runs, so anything in `dll/` must be loaded at runtime or delay-loaded
    [I].
  - When the first such DLL ships, these change with it [V]: the release
    layout in `devtools/release.ps1` (executables at 716-725, the install
    layout text at 863-875); the zero-DLL rule at
    `context/pillars/build-dev-tooling.md:339` and `:352` and
    `context/pillars/rendering.md:224`; and the comment at
    `CMakeLists.txt:802-809`. The updater must carry the folder too (not
    checked).
  - New `LoadLibrary` or `GetProcAddress` calls in client code must pass
    `tools/native_write_policy_guard.py`, which fails on them unless reviewed
    (route list at 23-48, reviewed entries from 52) and scans every source of
    the `pd` target plus the repository headers they include (125-165) [V]. A
    separately built static library is outside that scan; the guard's notes
    send new dependencies to the native dependency import audit (lines 1-6)
    [V].
- **D7. Ray tracing waits for 3.0.** 2.0 keeps bgfx and gets GI from the
  per-light probe bake (7.3). A ray-traced path in 3.0 would need a library
  that supports it, such as Diligent or NVRHI, or bgfx gaining it; bgfx lists
  none today ([W]), and the rest is [I]. To keep that door open, 2.0 keeps
  lights, probes and materials as data that does not depend on the library
  (5.3, 8.2) [I].

Sections changed by these decisions: 1, 4.2, 5.4, 5.6, 5.8, 6 (R8 dropped, R10
added), 7.1 to 7.4, the feature summary, 8.8, 8.9, 9, 10, 11, 12 and the
appendix.

---

## 1. Summary

A dual renderer is practical, but not as a second fast3d backend, and the toggle
only becomes cheap after a separation refactor. [I]

- The 3D world on dev is already drawn by two renderers: rooms by a
  GPU-resident `scene.glb` renderer, and everything else by fast3d interpreting
  the display list that game code builds each frame (`bg.c:2098-2105`,
  `gfx_pc.cpp:3297-3305`). [V]
- Rooms are already separated from gameplay. Room boxes, portals, fixture
  lights and collision come from public scenario files, and room bullet hits
  use the collision mesh (sections 2.3 and 3.4). [V] Props, characters, guns,
  effects and the sky are not separated. [V]
- The deepest coupling is that `lvRender` runs much of each player's game tick
  inside the draw function, and in netplay it builds a display list for every
  player and then throws all but the first away (`lv.c:1853-2003`,
  `lv.c:1629-1632`, `lv.c:2376-2378`). [V]

| Claim to check | Verdict | Why (details in section 4) |
|---|---|---|
| A second backend could read the same room/scene state as the current renderer, so the toggle is cheap. | **Rooms: yes. Everything else: not yet.** | Rooms are GPU-resident data, and CORE now captures their per-view state. Props, effects and the sky have no state outside the display list being built. The toggle becomes cheap once a snapshot exists (R1 to R5). [I] |
| Lighting gives the best payoff on original assets. | **Mostly yes, with care.** | Characters and props gain most: today they get fixed default lights plus a flat room-brightness shade. Rooms have their lighting baked into vertex colors, so new lights must add to it, not replace it. A bounce bake mostly pays off for new maps. [I] |
| Instancing becomes straightforward once draws take scene objects. | **Yes, after two fixes.** | Meshes reach the screen as display lists, and the shared model definition is rewritten per instance during the draw. With those fixed, batching is routine. The payoff on base content is modest. [I] |
| PBR needs new albedo/normal/roughness art, shipped as material override files in .pd archives, so remastered assets work like mods. | **Yes for packs; D4 adds a built-in baseline.** | `.pdmaterial`, `.pdtexture` and `.pdskin` already exist. They lack texture slots and a stable way to target base materials. BYOR rules out shipping art derived from the ROM, so the built-in baseline (D4) is derived on the player's machine and packs are original art. [I] |
| The biggest unknown is how cleanly the draw path is separated from game logic, and B-1172 may show it. | **Confirmed.** | B-1172 is exactly this: moving rooms out of the display list lost every piece of state the list carried implicitly (sky order, camera space, room lighting, flash, fog, sky rooms, animated textures). [D core] |

**Recommended shape** (*proposal*):

1. One simulation. Each frame, the same code that builds classic display lists
   also records a per-view presentation snapshot.
2. "Classic" means the 1.0 look: fast3d plus the scene renderer in its classic
   shading. It stays the reference.
3. "Remastered" is a built-in renderer (D4) that draws at inline display-list
   commands (a generalization of CORE's `G_SCENE_RENDER_EXT`) into the same
   framebuffer and depth buffer. Anything it doesn't draw yet stays classic.
   It is built on bgfx, with classic moved onto the same library first (D2,
   5.8).
4. Remastered base stages use PBR materials and dynamic lighting (D3). Base
   materials are derived on the player's machine from their own extracted
   textures (D4, BYOR-safe); art packs are ordinary mods that improve them,
   using an extended `.pdmaterial` and `.pdtexture` that target base content by
   stable catalog IDs.
5. Order of work: separation, then skeleton and toggle, objects, lighting with
   shadows and GI, built-in materials, post, sky and effects, and Forge
   lighting (section 11).

**Biggest risks** (section 12): gameplay regressions from splitting `lvRender`;
keeping classic pixel-identical through the move to bgfx; the two presentation
paths drifting apart; how a full relight looks on original geometry, and the
per-stage tuning it needs; BYOR limiting art to original or locally generated
work. [I]

---

## 2. Current render path, end to end

### 2.1 One frame

1. The live main tick is `mainTick` in `port/src/pdmain.c:774`. `src/lib/main.c`
   also defines one but is not compiled: the explicit `SRC_LIB` list
   (`CMakeLists.txt:520-566`) includes `src/lib/model.c`, `rng_c.c` and `vi.c`
   but not `main.c`. [V]
2. `frametimeCalculate()` measures the frame in 240 Hz units (`pdmain.c:941`,
   `timing.c:31-57`), and the game advances one variable step per drawn frame
   (`g_Vars.lvupdate240 = g_Vars.diffframe240`, `lv.c:2819`). [V]
3. `lvTick()` runs the global tick (`pdmain.c:977`), then `lvTickPlayer()` runs
   once per player (`pdmain.c:1005-1018`). [V]
4. `lvRender(gdl)` builds the frame's display list (`pdmain.c:1163`,
   `lv.c:1556`), followed by the console and net debug overlays
   (`pdmain.c:1169`). `rdpCreateTask` (`pdmain.c:1181`) hands the list to the
   port scheduler, which calls `videoSubmitCommands` and then `gfx_run`
   (`port/src/pdsched.c:281`, `port/src/video.c:130-133`). [V]
5. `gfx_run` (`gfx_pc.cpp:3268`) starts the frame and clears the game
   framebuffer (3282-3293), draws the scenario scene with raw GL (3297-3303),
   interprets the display list (3304-3305), resolves MSAA (3308-3325), draws
   ImGui on top (3345-3346), and swaps. [V]

```
mainTick (port/src/pdmain.c)
 |- lvTick()                          global simulation
 |- lvTickPlayer() x players
 '- lvRender(gdl)                     per player: more simulation + GBI emission
      '- rdpCreateTask -> gfx_run (port/fast3d/gfx_pc.cpp)
           |- clear game framebuffer
           |- scenarioSceneRendererRender    rooms from scene.glb, raw GL, one camera
           |- gfx_run_dl(display list)       fast3d: sky fill, props, characters,
           |                                 guns, effects, screen effects, HUD
           '- pdguiRender                    ImGui menus and overlays
```

### 2.2 fast3d, the GBI interpreter

- Vertices are transformed on the CPU by the current model-projection matrix
  (`gfx_pc.cpp:1465-1468`), lit on the CPU with N64 lights when `G_LIGHTING` is
  set (1519-1550), and fogged per vertex (1641-1656). [V]
- Triangles collect in a CPU buffer of up to 256 (`gfx_pc.cpp:59`, 277) and
  flush on state changes (300-306). Each flush is one
  `glBufferData(GL_STREAM_DRAW)` plus `glDrawArrays`
  (`gfx_opengl.cpp:907-908`). [V]
- The backend seam is the `GfxRenderingAPI` function table
  (`gfx_rendering_api.h:27-65`). Its draw entry takes vertices that are already
  transformed (`draw_triangles(float buf_vbo[], ...)`, line 46). [V] A backend
  behind this seam never sees objects, materials, world positions or normals:
  it can change the graphics API, not the look. [I] The rendering pillar's
  statement that a second backend "drops in by replacing one file" is true only
  in that sense ([D] `context/pillars/rendering.md:150`).
- Texture identity is the source memory address plus palette and format
  (`TextureCacheKey`, `gfx_pc.cpp:1079-1084`). `import_texture` takes an
  `importReplacement` flag that nothing uses (1004-1090). [V] Catalog texture
  identity never reaches the draw. [I]
- The port already has its own GBI extension opcodes, such as framebuffer set
  and copy, image rectangles and depth clear (`src/include/gbiex.h:185-197`).
  [V] They are the established way for game code to ask the port renderer for
  something N64 GBI can't express. [I]
- SDL tries a GL 4.3 core context first (compute and SSBOs), then 3.0
  compatibility, 4.1 and 3.2 core, ES 3 and 2.1 (`gfx_sdl2.cpp:186-200`). [V]
  The GPU swarm code already gates GL 4.3 features at runtime, loads them
  through `SDL_GL_GetProcAddress` and falls back to the CPU
  (`swarm_gpu.cpp:1-30`). [V]

### 2.3 Rooms: the scenario scene renderer

On dev every stage's background comes from its `.pdscenario`:

- `bgReset` activates the scenario source and returns. If that fails it raises
  a fatal fallback error and returns, so the ROM background code after it is
  unreachable (`bg.c:2060`, 2098-2105). [V]
- The room tables come from public scenario files: room boxes and rules from
  `rooms.json` (`bg.c:1704`), portals from `portals.json` (`bg.c:1463`, 1770),
  and fixture lights from `room_lights.json`, which keeps every field of
  `struct light` (`bg.c:1772`; `port/src/romextract_pdarena.c:7362`). Collision
  is the scenario's collision mesh (`bg.c:1411-1460`). [V] Per-stage room and
  light rules such as render-always rooms and base brightness also come from
  these files now (`dlights.c:375`, 413). [V]
- No room display lists exist in this mode: `gfxdata` and `vtxbatches` are NULL
  (`bg.c:1659`, 1663), and `bgLoadRoom` only marks a room loaded
  (`bg.c:3414-3419`). [V]
- Portal culling, draw slots and `ROOMFLAG_ONSCREEN` still come from the classic
  code running on the public portal table (`bgTickPortals`, `bg.c:6682`). [I]
- `scenario_scene_renderer.cpp` (1,808 lines) loads `scene.glb` into one
  flattened, world-space vertex buffer with position, two UV sets and vertex
  color (`Vertex`, lines 83-95; attributes read at 629-631; build at 780-931).
  It draws material groups with `glDrawArrays` (1678) and shades
  `texture × vertex color`, mixing an optional second texture 50/50
  (1151-1193, 1184-1187). It reads `baseColorTexture`,
  `extras.pd2_material.secondaryTexture` and `alphaMode` (438-516). It has no
  normals, no lighting and no fog. [V]
- It draws once per frame, before the display list, with whichever camera
  `bgRenderScene` handed it last (`bg.c:1035-1075`, `gfx_pc.cpp:3297-3303`).
  [V] `lvRender` runs the world branch for every player in netplay
  (`lv.c:1629-1632`, 1638), and the net player order is plain ascending
  (`playermgr.c:1081-1085`). [V] So wherever more than one player's world
  branch runs in a frame, rooms can be drawn from a different camera than the
  display list that is kept. [I, not seen in game] CORE's per-player camera
  slots remove this design (below).
- When the scene renderer is active, `lvRender` skips the sky fill
  (`lv.c:1848-1850`), and `bgRenderScene` skips the always-drawn sky rooms, the
  starfield (`bg.c:1117-1130`, 1170) and the room passes (1257-1261,
  1307-1309). [V] Props still draw per room in portal order (1228-1324). [V]
- The extracted base `scene.glb` is a single mesh (`scenario_visual_scene`,
  `romextract_pdarena.c:6405`). Its primitives carry POSITION, TEXCOORD_0,
  TEXCOORD_1, COLOR_0 and `_PD_ROOM`, and no NORMAL (6414-6417; the file never
  writes NORMAL). [V] Materials are written as standard glTF
  `pbrMetallicRoughness` (metallic 0, roughness 1, base color texture) plus
  `extras.pd2_material` with the N64 tile state (6456-6479). [V]

The classic room path (still in the code, unused on scenario stages) shows what
the display list used to carry implicitly. `bgRenderRoomPass` (`bg.c:3776`)
animates vertex UVs during the draw (`dyntexTickRoom`, 3787), recolors the room
(`roomHighlight`, 3792), and sorts block halves against the camera (3828).
`bgRenderRoomOpaque` (3855) sets the room's lights (3863) and resets the room's
age (`loaded240`, 3871). [V]

**CORE's B-1172 work (unmerged, `week-pd/core`):**

- A port command `G_SCENE_RENDER_EXT` (`src/include/gbiex.h:198`, 313) draws the
  scene at its place in the display list: the opaque pass before the props, and
  the translucent pass after the props' opaque pass (`bg.c:1228`, 1336), plus
  backdrop draws for the held menu and the lock screen (`lv.c:1815`, 1828).
  fast3d's handler passes the current viewport, scissor, fog and the queuing
  player's camera slot (`gfx_pc.cpp:3241-3285`). [V core]
- Depth now matches because the scene draws in the game's own draw space,
  `(p - draw_offset) * world_scale` (`port/include/scenario_scene_camera.h:8-24`).
  [V core] The game builds its camera in that space in
  `playerAllocateMatrices` (`player.c:6550-6573`). [V]
- Room lighting is captured per player view into a table with the
  `roomHighlight` formula (`bgSetScenarioSceneRoomLights`, `bg.c:1462-1511`).
  Fog, sky rooms and animated-surface time are handed to the renderer
  (`port/include/scenario_scene_renderer.h:59-104`). [V core] Animated textures
  became scene data (`extras.pd2_material.animation` and `_PD_DYNTEX`)
  ([D core] `context/designs/modding/animated-surfaces.md`).
- Its ledger records sky, fog, stars and animated textures seen in game on base
  data, with a matrix-probe rerun queued ([D core] `context/bugs.md:1318`).

### 2.4 Props, characters and weapons

- `bgRenderScene` interleaves by room: the props' opaque pre-BG pass, the
  room's opaque pass, the props' post-BG pass, then wall hits, then the rooms'
  translucent passes far to near, each followed by the props' translucent pass
  (`bg.c:1228-1324`). Each room is scissored to its portal screen box (1254,
  1304). [V]
- `propsRender` walks the onscreen list near to far for opaque passes and far
  to near for translucent ones (`prop.c:461-535`). `propRender` dispatches by
  prop type (`prop.c:416-441`). [V]
- Every model goes through `modelRender` (`model.c:3943`). It points a segment
  at the instance's matrices (4103) and walks the node tree (4105-4170). Display
  list nodes pick one of four render-mode types (3529-3542) and call the node's
  display list (3544). [V]
- Model matrices are per view and relative to the camera. `chrTick` builds them
  from the current player's world-to-screen matrix into per-frame display-list
  memory (`chr.c:3177-3209`, `gfxmemory.c:150-190`). [V] Distance LOD reads
  the node matrix's view-space z (`model.c:1353`). [V] `chrRender` builds the
  matrices on demand if they are missing (`chr.c:4196-4208`). [V]
- The model compiler turns public `.pdmesh` sources back into native modeldefs
  with GBI display lists (`gSPMatrix`, `gSPVertex`, `gSPEndDisplayList`,
  `port/src/modasset_compiler.c:7686-7979`). [V] Mod meshes and extracted meshes
  both reach the screen as display lists. [I]
- Lit models, such as character bodies with normals, use the fixed default N64
  lights: one ambient and one directional light with a fixed direction, set
  once for the world pass (`lightsSetDefault`, `bg.c:2811`;
  `dlights.c:363-372`). [V] Room brightness reaches props and characters as a
  flat per-instance shade color: the floor tile's color times the room
  brightness (`propCalculateShadeColour`, `propobj.c:2136-2200`), computed in
  tick (`chr.c:1164`, 1705) and again in render (`chr.c:3991`), and merged into
  the color the model draws with (`chr.c:3998-4060`). [V] The per-room light
  set (`lightsSetForRoom`, `dlights.c:331-361`) is only used for room geometry
  and glass and wood shards (`bg.c:3863`; `shards.c:299`, 421). [V]
- Shadows are radial blobs only (`gfxRenderRadialShadow`, `chr.c:4279`;
  `propobj.c:15691-15694`). [V]
- The first-person gun clears depth and sets its own projection: near 1.5, far
  1000, field of view 60 (`bondgun.c:12720`, 12726, 12733; `vi.c:500-513`).
  [V]

### 2.5 Sky, sun, stars and fog

- `skyRender` (`sky.c:206`) builds sky and water polygons on the CPU in screen
  space (`skyConvertVertex` and `skyRenderTri`, `sky.c:1313`, 1399). Indoors it
  is a full-viewport fill with the sky color (267-279). [V]
- Suns and lens flares (`skyRenderSuns`, `sky.c:2469`) scale by an occlusion
  fraction (2651). The PC port computes it with a gameplay line-of-sight ray
  (`artifactTestLos` calls `shotTestLos`, `artifact.c:182-204`), not a depth
  readback. [V] So flare occlusion already works with any renderer. [I]
- Per-stage environment data: near and far planes, fog min and max, sky color,
  suns with position, color and sizes, clouds and water (`types.h:3276`, 3286;
  `env.c:192-237`). [V] Fog uses the sky color and `gSPFogPosition`
  (`env.c:378-396`). [V]
- The starfield draws only in the classic pre-pass (`stars.c:199`, called at
  `bg.c:1170`). [V] Overexposure is a full-screen fill (`sky.c:3024`, called at
  `lv.c:2354`). [V]

### 2.6 The classic lighting model

- Each room's baked vertex colors are scaled down to the room's settled
  regional brightness, plus an additive flash, the scenario highlight tint, and
  greyscale for night vision. The result is written each frame into per-frame
  color memory (`roomHighlight`, `dlights.c:1622`; allocation 1650; night
  vision 1670; brightness clamp 1691; flash 1711; highlight 1717). [V]
- A room's brightness follows its fixture lights and its light operations.
  `struct light` holds a 4/4/4/4 color, brightness, on/healthy/vulnerable bits,
  a direction and a four-corner quad (`types.h:5386`). The room's brightness
  range, flash and light-op fields are in `struct room` (`types.h:3670-3740`).
  [V]
- Gunfire flashes the player's room and its neighbours (`lightsTick`,
  `dlights.c:1541-1551`; `roomFlashLighting`, 1563), but only rooms that are
  currently onscreen (1598). `.pdeffect` light commands also become room
  flashes (`effect_presentation_renderer.c:278-298`). [V]
- No positional, per-pixel light exists anywhere. [I, from the above] Room
  geometry carries no normals (section 2.3), so the vertex color is the
  lighting. [I]

### 2.7 Effects and screen effects

- Wall hits draw per room (`bg.c:1284-1292`). Beams (`propsRenderBeams`,
  `propobj.c:13461`), shards, sparks, effect presentation and weather draw after
  the background (`lv.c:2006-2012`). Glares are calculated early on PC "before
  prop matrices turn into garbage" (`lv.c:1870-1871`). [V]
- `.pdeffect` presentation already works from a per-frame snapshot of typed
  commands. Its channels are screen, decal, light and beam, and each command
  carries material fields (`port/include/effect_presentation_runtime.h:28-55`).
  The renderer reads them through `effectPresentationRuntimeSnapshot`
  (`effect_presentation_renderer.c:300-308`). [V] This is the
  renderer-neutral pattern 2.0 needs everywhere. [I]
- Screen effects are GBI drawn over the finished world with the port's
  framebuffer commands: motion blur, zoom blur, static, interlace, night vision
  and IR lenses, and the horizon scanner (`bondview.c:189-249`; framebuffer
  texture and image rectangle at 242-246). [V] They would therefore also work
  over a remastered image in the same framebuffer. [I]

### 2.8 HUD and menus

- The HUD is GBI. `playerRenderHud` (`player.c:6783`) draws the gun
  (`bgunRender`, 6831), the laser dot, shields, the night-vision and IR lenses
  (6871) and the health bar. The active menu is `amRender` (`lv.c:2355`). [V]
- Menus are ImGui only. The legacy menu path queues dialogs to ImGui and still
  renders character previews into an FBO through GBI (`menu.c:4047-4065`). [V]
  This matches the constraint "ImGui is the sole menu system"
  ([D] `context/constraints.md:84`).
- ImGui draws after the game frame (`gfx_pc.cpp:3345-3346`), and its call order
  is the HUD z-stack ([D] `context/designs/menus/hud-layer-order.md:44-78`).
  [V]

### 2.9 How assets reach the screen

| Source | Path to the screen | Evidence |
|---|---|---|
| `.pdscenario` `scene.glb` | Scene renderer, raw GL, GPU-resident. | `bg.c:1411-1460`; `scenario_scene_renderer.cpp:780-931`. [V] |
| `.pdmesh` (props, characters, weapons) | Compiled into native modeldef display lists, then fast3d. | `modasset_compiler.c:7364`, 7686-7979. [V] |
| Textures | `texLoad` resolves texture numbers into a pool (`struct tex` pairs `texturenum` with `data`), then fast3d caches by address. | `texdecompress.c:2275`, 2082; `types.h:6109`; `gfx_pc.cpp:1079-1084`. [V] |
| `.pdmaterial` | Scalar fields only: `shading_model`, `base_color`, `roughness`, `metallic`, `emissive`. | `port/src/asset_runtime.c:940-976`. [V] |
| `.pdtexture` | Image plus `texture.ini` properties: `catalog_id`, `surface_type`, `sound_surface_type`, tile offsets, mask reductions, `properties_version`. | `port/src/texture_source_properties.c:8-12`, 61-98. [V] |
| `.pdeffect` | Presentation commands with roughness, metallic, emissive and shading model; classic folds them into a tint. | `effect_presentation_runtime.h:52-55`; `port/src/effect_presentation_runtime.c:65-86`. [V] |

### 2.10 File map

| Area | Files (lines at `361abab8a`) |
|---|---|
| Frame loop | `port/src/pdmain.c`; `src/game/lv.c` (3,304); `src/game/timing.c` |
| GBI interpreter and GL backend | `port/fast3d/gfx_pc.cpp` (3,517); `gfx_opengl.cpp` (1,422); `gfx_rendering_api.h`; `gfx_sdl2.cpp`; `src/include/gbiex.h` |
| Rooms | `port/fast3d/scenario_scene_renderer.cpp` (1,808); `src/game/bg.c` (7,415); `port/src/scenario_source_runtime.c` |
| Models | `src/lib/model.c` (5,453); `src/game/chr.c` (7,457); `src/game/propobj.c` (24,477); `src/game/prop.c` (3,975); `port/src/modasset_compiler.c` (10,304) |
| Lighting and environment | `src/game/dlights.c` (2,173); `src/game/env.c`; `src/game/sky.c` (3,085); `src/game/stars.c`; `src/game/artifact.c` |
| Effects | `src/game/wallhit.c`, `sparks.c`, `smoke.c`, `explosions.c`, `shards.c`, `splat.c`, `gunfx.c`, `weather.c`, `effect_presentation_renderer.c`; `port/src/effect_presentation_runtime.c` |
| First person and screen effects | `src/game/bondgun.c` (15,203); `src/game/bondview.c` (2,687); `src/game/player.c` |
| Extraction | `port/src/romextract_pdarena.c` (10,240), which writes `scene.glb` and `room_lights.json` |

---

## 3. Where game logic touches drawing

### 3.1 `lvRender` runs gameplay

Inside the world branch of `lvRender`, between GBI emission: portal and
visibility tick `bgTick` (`lv.c:1853`), `lightsTick` (1854) and
`propsTickPlayer` (1855). The latter ticks characters, bots, objects,
explosions and smoke for this player's view (`prop.c:2215`; `chrTick` 2466,
`objTickPlayer` 2474). After them come `propsSort` (1861), `autoaimTick` (1862),
`handsTickAttack` (1863), the aim target (`propFindAimingAt`, 1878),
interaction (1976), reloads (1995) and pickups (2003). The same branch fires
eyespy darts and bombs, including `projectileCreate` and `chrBeginDeath`
(1948, 1961), respawns the player (`playerStartNewLife`, 2370) and changes
stage during cutscene autoplay (`mainChangeToStage`, 2432). It also decays the
drug blur (1672), runs the combat-boost state that drives slow motion
(`speedpillchange` and `speedpillon`, 2219, 2233), resets a vision mode (2174)
and loads assets (`bgunLoadAll`, 1721). [V]

### 3.2 Netplay builds every player's view

In netplay `lvRender` runs the world branch for every player and throws away
the display list for all but the first (`lv.c:1629-1632`, 2376-2378). [V] A
search of `bg.c`, `prop.c`, `sky.c`, `model.c`, `chr.c`, `bondgun.c` and
`propobj.c` found no `g_NetMode` or `g_NetDedicated` test on any line that
builds display lists, so no draw code skips work for those players. [V search]
A listen host therefore builds a full GBI view for each remote player every
frame just to run their tick. [I] Splitting tick from draw would save that CPU
in both modes. [I]

### 3.3 Per-view matrices are gameplay data

Matrices are built in tick from the current camera into per-frame memory
(`chr.c:3177-3209`). Character hit tests walk the model's display list and look
up those matrices (`bgTestHitOnChr`, `bg.c:4599-4680`; matrix check at 4676),
and LOD reads them (`model.c:1353`). [V] The memory is a per-frame arena that
is reused, and the code itself notes that prop matrices "turn into garbage"
later in the frame (`gfxmemory.c:150-190`; `lv.c:1870`). [V] Since each
player's tick rebuilds them, `model->matrices` holds the set for the view
ticked last. [I] Any renderer must therefore copy matrices per view when it
records them, and the classic code must keep building them whatever renderer
draws. [I]

### 3.4 Display lists are gameplay geometry

Character and object hit tests walk display lists (`bg.c:4599-4680`). Rooms
did too: `bgTestHitInVtxBatch` walks G_VTX, G_TRI1 and G_TRI4 commands
(`bg.c:4950-5015`). [V] Scenario stages have no room display lists, so
`bgTestHitInRoom` finds nothing (`bg.c:5300-5304`). Room hits now go through
the collision mesh (`bgTestHitScenarioMesh`, `bg.c:5520-5560`; shots use it at
`prop.c:794` and 1195). [V] That fix is B-1187 and B-1189, which landed on dev
with the crystal pistol merge. Its note records that the gap had left shots,
splats, spawn checks and scorch marks without walls ([D]
`context/bugs.md:62`, 101). This is the pattern to copy for characters: give
gameplay its own geometry so the renderer is free. [I]

### 3.5 Shared definitions are rewritten per instance

`modelRender` writes `node->child` on the shared model definition
(`model.c:4116`, 4119), and so does the LOD update (1363, 1369). [V] Until that
state moves per instance, instances can't be batched by definition or drawn in
parallel. [I] Hit tests also traverse whatever LOD was chosen last. [I]

### 3.6 Presentation state is computed during the draw

Room colors (`roomHighlight` from the room pass, `bg.c:3792`), UV animation
(`dyntexTickRoom`, 3787) and room ageing (`loaded240`, 3871) are all computed
during the draw. [V] CORE had to capture each one explicitly for the scene
renderer (section 2.3). [V core]

### 3.7 Visibility is a gameplay input

Props in onscreen rooms tick every frame; others tick round-robin
(`prop.c:2359-2381`, 2466-2474). [V] Room flashes apply only to onscreen rooms
(`dlights.c:1598`). [V] Portal visibility must stay classic and
renderer-independent. [I]

### 3.8 One RNG for gameplay and drawing

One global seed (`rng_c.c:8-19`; `RANDOMFRAC`, `src/include/constants.h:98`) is
also drawn from by draw functions: gunfire nodes (`model.c:3700-3704`, 3818,
3873), beams (`gunfx.c:365-368`), rain (`weather.c:1001`, 1029-1030), shields
(`chr.c:7247`), TV screens (`propobj.c:15239`) and cutscene static
(`lv.c:2121`, 2138). [V] Single-player is seeded from the clock
(`pdmain.c:535`), so this is about reproducibility, not correctness. [V for the
seed; I for the conclusion] Still, replacing draw code would change later
gameplay random numbers. [I]

### 3.9 One step per frame

Rendering cost directly limits the simulation step rate (section 2.1), and the
listen host pays for every player's view (3.2). [I]

### 3.10 What B-1172 showed

When rooms moved to the scene renderer, everything the display list carried
implicitly went missing: draw order against the sky, the game's scaled and
camera-room-relative draw space, the depth buffer, room brightness and flash,
fog, sky rooms, the starfield and animated textures ([D core]
`context/bugs.md:1318`). Each had to be rebuilt as explicit data or as an
inline command ([V core], section 2.3). Expect the same work for every other
object class that moves to the remastered renderer. [I]

### 3.11 Seams that are already clean

- Room render data is separate from room gameplay data (2.3, 3.4). [V]
- GBI extension commands can place non-GBI draws in display-list order (2.2;
  `G_SCENE_RENDER_EXT` on core). [V], [V core]
- `.pdeffect` presentation is already a typed per-frame snapshot (2.7). [V]
- Flare occlusion uses gameplay rays, not the depth buffer (2.5). [V]
- glTF material fields are already in public sources: base scenes are written
  as `pbrMetallicRoughness` (2.3), and `.pdmaterial` has roughness, metallic and
  emissive (2.9). [V]

### 3.12 Verdict

The separation is clean for rooms, partial for effects and absent for models,
the sky and screen effects. The real seams are the functions that already
gather a draw's inputs: `modelRender`, `bgRenderScene`'s draw slots, the
room-brightness and shade-color calculations (`roomHighlight`,
`propCalculateShadeColour`), `skyRender`'s environment reads, and the effect
render functions. A snapshot recorded there sees the same visible set, matrices and
shading as the classic draw, which keeps the two renderers consistent by
construction. [I]

---

## 4. The claims in detail

**4.1 "A second backend could read the same room/scene state."**
- True for rooms. Both modes would read the same GPU-resident `scene.glb`
  buffers (2.3) and the same per-view room-light table that CORE now captures
  ([V core] `bg.c:1462-1511`). Toggling room shading is a shader switch. [I]
- False for the rest today. The visible node set, per-instance shade color,
  alpha, render mode and gunfire only exist while the display list is being
  built (2.4, 3.5, 3.6). [V], [I] A backend behind `GfxRenderingAPI` only sees
  transformed triangles (2.2). [V]
- What makes it true: the snapshot (5.3) and refactors R1 to R5 (section 6).
  Once both modes consume the same snapshot, the toggle costs nothing per frame,
  provided the remastered GPU resources stay resident. [I]

**4.2 "Lighting gives the best payoff on original assets."**
- Characters and props gain most: today lit models get fixed default lights,
  and room brightness arrives only as a flat shade color, the floor tint times
  room brightness (2.4). [V] Lighting them from the room's real fixtures (2.6)
  and the sun (2.5) is a large visible change that needs no new art. [I]
- For rooms the original look *is* the baked vertex color (2.6). New lights
  should add specular and transient light, such as flashes and explosions, on
  top of it. Recomputing the lighting would double-light rooms or replace
  Rare's work. [I]
- Shadows from moving things (characters, props, the viewmodel) onto static
  rooms pay off well. Static shadows onto static rooms conflict with the baked
  lighting. [I]
- A bounce bake mostly pays off for Forge and custom maps, which have no baked
  vertex colors. For base stages, a light-probe volume for moving objects gives
  most of the benefit. [I]
- Cheap wins that also help original assets: ambient occlusion, bloom on
  emissive surfaces and flashes, tone mapping, and better fog (7.6, 7.7). [I]
- **Decided (D3):** remastered base stages get PBR and dynamic lighting, so the
  rooms are relit rather than kept on their baked colors. The two bullets above
  about rooms and bounce baking now describe the trade-off D3 accepted; 7.1 to
  7.3 describe how the relight is done.

**4.3 "Instancing becomes straightforward once draws take scene objects."**
- Two blockers first: meshes reach the screen as display lists (2.4), and the
  shared definition is rewritten per instance (3.5). [V]
- After that, skinning is simple. In GBI each vertex is transformed by the one
  matrix that is current when it loads (`gfx_pc.cpp:1465-1468`), and the
  compiler emits per-matrix runs (`modasset_compiler.c:7686-7979`). [V] So each
  vertex has exactly one bone, and a GPU mesh with a per-vertex matrix index
  plus a per-instance matrix palette in an SSBO is routine. [I]
- Translucent passes keep PD's per-room, far-to-near order (2.4), so batching
  mainly helps opaque passes. [I]
- The payoff on base content is modest, since PD scenes are small. It matters
  for the listen host, high refresh rates, dense mod content and swarm
  scenarios. [I]

**4.4 "PBR needs new art, shipped as override files in .pd archives."**
- Partly verified. `.pdmaterial` (`asset_archive_policy.c:60`), `.pdtexture`
  (61) and `.pdskin` (75) exist. [V] Catalog entries support an override source
  (`catalogSetOverride`, `assetcatalog.h:1059-1060`). [V] The archive contract
  already reserves "optional PBR-ready fields" for materials, color space and
  compression intent for textures, and material-slot overrides for skins ([D]
  `context/designs/modding/asset-archive-clean-formats.md:124`, 125, 103).
- Gaps: no texture slots in `.pdmaterial` (2.9) [V]; shading models are a
  closed set, `classic_lit`, `classic_alpha` and `classic_emissive`
  (`tools/asset_archive_conformance.py:3327-3331`) [V]; base materials have no
  stable public identity to target (8.4) [V]; and nothing classifies a pack as
  presentation-only for multiplayer (8.8) [V search; D], which D4 makes
  unnecessary.
- The best PBR needs new art. BYOR rules out shipping anything derived from ROM
  textures, so remastered art is either original or generated on the player's
  machine (8.7). [I] D4 makes the generated baseline built in, starting from
  `surface_type` (8.9). [I]

**4.5 "The separation of draw path and game logic is the biggest unknown."**
- Confirmed and measured: section 3. B-1172 is the worked example (3.10). [V],
  [D core] The unknown is now a list: R1 to R9 in section 6. [I]

---

## 5. Proposed architecture

### 5.1 Principles (*proposal*)

1. **One simulation.** Presentation code reads game state and never writes it.
   The classic renderer stays exactly as 1.0 ships it.
2. **One snapshot, two consumers.** Snapshot producers run in both modes, so a
   toggle changes only which consumer draws.
3. **Classic is the reference.** Remastered must be able to show a "neutral"
   result that matches classic composition (same visible set, same placement,
   same order), so parity can be tested.
4. **Additive delivery.** Remastered draws whole passes at inline commands, and
   fast3d keeps drawing everything remastered doesn't own yet.
5. **Per-client and cosmetic.** The renderer choice never reaches the wire,
   saves, match manifests or mod checks. Art packs are ordinary mods (D4).
6. **Public source stays the authored source.** GPU-ready products (compressed
   textures, generated normals, light probes, lightmaps) are source-hashed
   cache ([D] `context/constraints.md:61`).

### 5.2 Frame flow after the change (*proposal*)

```
mainTick
 |- lvTick()
 |- per player: lvTickView(p)        simulation only (R1)
 '- per rendered view: lvRenderView(v)
      |- record snapshot(v)          camera, rooms, room lights, fixtures,
      |                              instances, effects, env, screen effects
      '- emit GBI:
           classic-owned passes  -> normal GBI
           remastered passes     -> G_REMASTER_PASS_EXT(view, pass)
           HUD, 2D, screen fx    -> normal GBI (unchanged)
gfx_run
 '- gfx_run_dl: on G_REMASTER_PASS_EXT, flush, then the remastered module
    draws that pass for that view into the same framebuffer and depth
```

### 5.3 The presentation snapshot

C API in `port/include/presentation_snapshot.h`, shared by C and C++. Shared
declarations spell `s32`, not `bool` (SP-75, [D] `context/constraints.md:48`).
Records are double-buffered per frame and copied at record time, because the
per-frame memory they point into is reused (3.3). (*proposal*)

| Record | Contents | Producer (today's function) |
|---|---|---|
| View | Viewport and scissor; camera eye, look and up; FOV, aspect, near and far; draw-space offset and BG scale; vision mode | `bgRenderScene` camera hand-off (`bg.c:1035-1075`); `playerAllocateMatrices` (`player.c:6550-6573`) |
| Environment | Fog min, max and color; sky color; clouds and water; suns with flare intensity; starfield rule | `env.c:192-237`; `sky.c:2469`, 2651 |
| Rooms | Draw slots with order and screen boxes; per-room brightness, flash, tint, unlit and greyscale; sky rooms with offset | `bgRenderScene` (`bg.c:1228-1324`); CORE's room-light capture ([V core] `bg.c:1462-1511`) |
| Lights | Fixture quad, direction, color, brightness times light-op state, on and healthy; muzzle flashes with position; explosion and `.pdeffect` lights | `struct light` (`types.h:5386`); `lightsTick` (`dlights.c:1541-1551`); `effect_presentation_renderer.c:278-298` |
| Instances | Prop identity, modeldef, node-visibility bits, matrices (draw space, plus last frame's for motion vectors), passes (pre-BG, post-BG, translucent, on top), shade inputs (floor color, room brightness, env and fog colors), alpha, cloak and shield, parent attachment | `modelRender` (`model.c:3943`); `chrRender` (`chr.c:3798`); `objRender` (`propobj.c:15700`) |
| Viewmodel | Hand and gun instances with their own projection | `bgunRender` (`bondgun.c:12660`) |
| Effects | Decals (position, normal, texture, size, alpha), beams, particles, `.pdeffect` commands | `wallhitRenderBgHits`, `sparksRender`, `smokeRender`, `explosionRender`, `shardsRender`, `beamRender`; the `.pdeffect` snapshot already exists |
| Screen effects | Blur amount, zoom blur, static, fades, teleport white, overexposure | `lvRender` (`lv.c:1686`, 2121-2236, 2354) |

### 5.4 Consumers (*proposal*)

- **Classic:** unchanged fast3d plus the scene renderer in classic shading. In
  the first phases it keeps reading GBI. Later it can read the snapshot too, so
  both modes share one source of truth.
- **Remastered:** a new C++ module, for example `port/fast3d/remaster/`, with a
  frame orchestrator, the scene (absorbing `scenario_scene_renderer.cpp`), a GPU
  model cache, materials, clustered lights, shadows, post, effects and sky.
  With bgfx (D2), classic and remastered submit to separate bgfx views on one
  device, so the hand-written GL state save and restore the scene renderer
  needs today (`scenario_scene_renderer.cpp:1562-1607` [V]) goes away. [I]

### 5.5 Inline commands (*proposal*)

- Generalize CORE's `G_SCENE_RENDER_EXT` into `G_REMASTER_PASS_EXT(view slot,
  pass)`, where the pass is one of rooms opaque, props pre-BG, props post-BG,
  decals, rooms translucent, props translucent, viewmodel, sky or post. [V core
  for the pattern]
- During migration, producers emit either the classic GBI for a pass or the
  inline command, never both. The display list is rebuilt from scratch every
  frame (`pdmain.c:948`, `lv.c:1556`), so switching modes costs nothing on the
  GBI side. [V for the rebuild; I for the conclusion]
- Before a producer can skip its GBI, its side effects must move out (`roomHighlight`,
  `dyntexTickRoom`, `loaded240`, the RNG draws, on-demand matrices). Those are
  R3 to R5.

### 5.6 The toggle (D1; details are *proposals*)

- One switch, as in Halo: Combat Evolved Anniversary (D1): a setting
  `Video.Renderer = classic | remastered`, a menu entry, and one bindable
  action. It takes effect at the next frame. Developer-only per-pass overrides
  exist for debugging and are never shown to players.
- Quality settings (shadows, GI, post) belong to the remastered side only.
  Classic has none: it is fixed to the 1.0 look.
- Instant means the remastered resources for the stage are always resident on
  hardware that supports remastered: build them at stage load even when the
  player is in classic. A low-memory fallback may build them on the first
  toggle, with a short hitch. [I]
- Default mode: remastered on hardware that supports it, classic otherwise.
  (*Proposal*; Mike may prefer classic first.)
- Capability gate: with bgfx (D2), remastered needs a backend with compute
  shaders (D3D11, D3D12, Vulkan, or GL 4.3) [W], [I]; otherwise the option is
  greyed out and classic still runs. bgfx's GL backend itself needs GL 4.3+
  ([W]), so today's fallback chain down to GL 3.0 and 2.1
  (`gfx_sdl2.cpp:186-200` [V]) is replaced by D3D11 on older Windows GPUs
  (D5). [I]
- Safety: a scripted smoke runs once per mode and compares gameplay logs, such
  as SHOT_RESULT lines, kills and positions. That needs R3 (separate RNG) to be
  exact. [I]
- Halo: Combat Evolved Anniversary kept the original engine in charge of
  gameplay and drew the new visuals with Saber's own engine, with an instant
  switch ([W]). This design matches that model: one simulation, two renderers.
  Anniversary's second renderer was its developer's own engine fed by the
  original game, which corresponds to options A and B in 5.8, not to
  embedding someone else's engine. [W], [I]

### 5.7 Draw space and depth (*proposal*)

- The remastered module draws in the game's own draw space,
  `(p - draw_offset) * world_scale` relative to the camera's room ([V core]
  `scenario_scene_camera.h:8-24`; [V] `player.c:6563-6573`), using the game's
  projection. That keeps depth compatible with fast3d passes in the same frame,
  which was the B-936 lesson, and keeps coordinates near the camera. [I]
- Lights, shadow cameras and probes are transformed into that space per view.
  World-space data such as fixture quads is converted once per view. [I]
- Once every 3D pass of a view is remastered, it may move to reversed-Z float
  depth, but only behind a clear, because the viewmodel and fast3d passes must
  not share that buffer. [I]

### 5.8 Engine and graphics API options (decided: B, D2)

**Decided 2026-11-16: option B, with bgfx as the working library.** The
comparison below is kept as the record of the choice; section 0 lists what B
implies, and R10 (section 6) is the classic port it requires.

What any option has to satisfy:

- **One window and one frame with classic,** so the switch is instant and the
  HUD, ImGui menus and screen effects draw over either renderer (D1).
- **PBR, many dynamic lights, shadows and GI** (D3).
- **The project's toolchain:** MSYS2 MinGW GCC with CMake, static linking, and
  `opengl32` as the only runtime DLL today ([D]
  `context/pillars/build-dev-tooling.md:352`; [V] `CMakeLists.txt:479`).
- **A licence that allows static linking into the release.** MIT, BSD, zlib and
  Apache-2.0 all do, as long as their notices ship with it. [I]

**A. Our own renderer on modern OpenGL, in the same context as classic.**
- Fit: the best. It shares the framebuffer, depth buffer and ImGui with
  classic, so hybrid frames (some passes still classic) just work. MinGW and
  zero-DLL stay as they are, and GL 4.3 compute is already in use
  (`swarm_gpu.cpp:1-30`). [V], [I]
- Cost: we write every feature: clustered lights, shadows, PBR with
  image-based lighting, the GI bake and post effects. These are
  well-documented techniques. [I]
- Risk: OpenGL gets no new features, so no hardware ray tracing or mesh
  shaders, and Windows GL drivers vary in quality. [I]

**B. Our own renderer on a thin cross-API library, with classic moved onto it.**
- Libraries: bgfx (BSD-2 [W]; MAME builds its bundled copy with MSYS2 MinGW
  [W]; it can adopt an existing GL context [W]); Diligent Core (Apache-2.0 [I];
  it can attach to the current GL context, but its build notes say MinGW has
  reduced capability and is not advised [W]); sokol_gfx (zlib) is thinner still
  [I].
- Fit: D3D11, D3D12, Vulkan or GL from one code base. Classic and remastered
  share one device, so the instant switch and hybrid frames still work. With
  bgfx the move can start on our existing GL context and change API later.
  [W], [I]
- Cost: port classic's 35-function backend table (`gfx_rendering_api.h:27-65`)
  and the GLSL generator in `gfx_opengl.cpp` (1,422 lines), the ImGui backend,
  the scene renderer and the other raw-GL users (the swarm compute shaders,
  screenshots); add a shader cross-compile step to the build; still write every
  feature ourselves. [V for the files], [I]
- Risk: the DLL rule, now settled by D6 (section 0): D3D11 and D3D12 ship with
  Windows, like `opengl32`; Vulkan stays an option because its loader comes
  with GPU drivers (D5). Classic must be re-proven pixel for pixel on the new
  backend. [I]

**C. A renderer-only engine: Filament, Ogre-Next, or DiligentFX on Diligent.**
- Fit: PBR, clustered lights, shadows, ambient occlusion, bloom, TAA and
  image-based lighting come ready-made. But the library owns the 3D frame, so
  the whole 3D view switches at once (no hybrid frames), and classic's HUD and
  screen effects are composited over its output. [I]
- Cost: map the snapshot into its scene and material model every frame, and
  re-express PD specifics in it: two-texture N64 blends, per-room translucency
  order, sky rooms, x-ray and the vision modes. [I]
- Risk: the toolchain. Filament's Windows build uses clang or MSVC, not MinGW
  [W]; a C++ library built by another compiler can't be linked with MinGW GCC,
  so the whole project would move compilers (for example to MSYS2's clang
  environment or to MSVC) [I]. Diligent: MinGW not advised [W]. Ogre-Next: no
  MinGW statement found [W search]. The binary grows, and we follow their
  releases. [I]

**D. A second full engine beside ours: the Halo model with someone else's
engine, realistically Godot 4 (MIT).**
- Fit: the richest feature set, including GI, volumetric fog and an editor
  artists already know. [I]
- Cost: two engines in one process, which means deciding who owns the window,
  input and GPU device, mirroring every object into Godot's scene each frame,
  and compositing classic's HUD and ImGui with Godot's output. [I]
- Risk: embedding is new. Building Godot as a library landed in Godot 4.6, and
  embedding it in another app's UI works only on Apple platforms so far [W].
  Godot's full renderers run on Vulkan or D3D12 (the DLL rule again), and we
  would follow Godot's release cycle [I]. Unreal, Unity and O3DE are not
  practical: licence terms, Windows builds that need MSVC, and none is built
  to be embedded in another game [I].

Running the remastered renderer as a separate process that shares GPU textures
is a variant of C or D with more latency and more moving parts. Not
recommended. [I]

**Recommendation** (*proposal*): **A**, with a thin internal layer for
resources and passes so that a later move to B stays contained.

- Halo's second renderer was written for the job and fed by the original game
  (D1). That is A or B, not D. [W], [I]
- A keeps the instant switch and hybrid frames simple, keeps MinGW and
  zero-DLL, and needs no port of classic. [I]
- The most expensive part of 2.0, separating the game from drawing (phase 1),
  costs the same under every option, and the renderer features are
  well-understood work. [I]
- Pick **B** instead if D3D12 or Vulkan matters for 2.0 itself, for example
  hardware ray-traced GI or a Linux or Steam Deck build. bgfx is the most
  MinGW-proven of the libraries above. [W], [I] *Correction after D2:* bgfx
  lists no ray tracing support ([W]), so ray-traced GI is not a reason for B
  with bgfx; Diligent or NVRHI would be needed for that [I].
- C and D trade feature work for toolchain and integration risk, and lose
  hybrid frames. [I]

### 5.9 Split-screen, netplay, Theater and Forge

- Split-screen: the snapshot holds one record per view, and the remastered
  module renders N views with a per-view cost budget, falling back to classic
  above N. [I]
- Netplay: the renderer only snapshots views that will be drawn locally. R1
  stops the host building views for remote players. This respects
  receiver-local presentation identity ([D] `context/constraints.md:29`). [I]
- Theater: playback is presentation of recorded state, so either renderer can
  show it. Theater uses an authoritative recorder with checkpoints and events
  ([D] `context/pillars/connectivity.md:466-484`). [I]
- Forge: its lighting plan already names a directional skylight with shadows,
  point, spot and area lights, emissive surfaces, light probes, fog and
  post-process volumes, and a 32-light soft and 64-light hard budget ([D]
  `context/designs/modding/forge-level-editor.md:222-227`, 256, 566-640).
  Classic can't show point lights, so Forge F4 needs the remastered lighting,
  or a bake into vertex colors for classic. Define one public light schema used
  by both. [I]

---

## 6. What must be refactored first

| ID | Change | Why (evidence) | Size | Risk |
|---|---|---|---|---|
| R1 | Split each iteration of `lvRender`'s player loop into `lvTickView` (all gameplay statements in their current order) and `lvRenderView` (GBI and the snapshot only). Keep the per-player interleave, and skip `lvRenderView` for views that won't be shown. | Gameplay inside the draw (3.1). The host builds remote views (3.2). Per-view matrices force the interleave (3.3). | L-XL | High: ordering inside the branch. Needs a parity harness (R9). |
| R2 | Per-instance node visibility, without rewriting `node->child` on shared definitions; hit tests get an explicit LOD rule. | 3.5 | M | Medium: hit tests rely on the last LOD. |
| R3 | A separate presentation RNG for gunfire flicker, beams, rain, TV static, shields and cutscene static. | 3.8 | S | Low. |
| R4 | The snapshot records matrices per view at record time, including last frame's for motion vectors. | 3.3 | S-M | Low. |
| R5 | A per-view "room presentation" step that computes room colors, flash, highlight, dyntex time and ageing outside the draw, generalizing CORE's capture. | 3.6; [V core] `bg.c:1462-1511` | S-M | Low to medium. |
| R6 | Stable public identity for extracted base materials and textures: write each texture's catalog ID into `extras.pd2_material`, and give `.pdmesh` material slots names. | Names today are `mat_%03u_tex_%04x_c%d` and `visual/textures/tex_%04x.tga` (`romextract_pdarena.c:5585-5592`, 5477). [V] Catalog texture IDs exist (`base:texture_unlabeled_*`, `port/src/catalog_readable_ids.c:214-218`). [V] Numeric identity is not allowed in public references ([D] `context/constraints.md:53`). | M | Low: extractor plus conformance. |
| R7 | Express legacy effects (sparks, smoke, explosions, shards, beams, wall hits) as declarative presentation records, like `.pdeffect`. | 2.7 | M-L | Medium: effect timing and look. |
| R8 | **Dropped (D4).** It was a presentation-only archive class kept out of match manifests and mod checks; art packs are now ordinary mods. | Enabled mods go into MP match manifests ([D] `context/designs/connectivity/manifest-architecture.md:21`), and no presentation-only class exists [V search for cosmetic and client-optional flags in `port/include`, `port/src/net` and `context/pillars/modding.md`]. So an enabled pack is distributed like any mod; revisit only if large packs cause problems. [I] | none | none |
| R9 | A parity harness: the same scripted session in classic before and after R1 and R2, comparing gameplay logs and reference captures; later classic against remastered-neutral. | Needed to make R1 and R2 safe. | M | Low. |
| R10 | Move classic onto bgfx (D2): fast3d's 35-function backend table on bgfx; the runtime GLSL generator replaced by a combiner uber-shader or a precompiled variant set; ImGui drawn through bgfx, with ImGui textures as bgfx handles; the scene renderer and the other raw-GL users ported; an SDL window without a GL context for non-GL backends; bgfx run single-threaded with SDL. Keep the 1.0 GL backend only until parity is proven, then remove it. | Raw GL calls at `361abab8a` [V]: `gfx_opengl.cpp` 190, `scenario_scene_renderer.cpp` 157, `swarm_gpu.cpp` 65, `meshdebug.c` 51, and 74 across seven pdgui files (skin canvas, character preview, theme, character portrait, modding hub, skin editor, backend), plus `gfx_sdl2.cpp` and ImGui's vendored OpenGL backend (`port/fast3d/imgui/imgui_impl_opengl3.cpp`). `server_gui.cpp` is not in the client build (`CMakeLists.txt:575`). bgfx warns that custom windowing needs care with its multithreaded renderer ([W]). | L-XL | High: classic must stay pixel-identical; R9 captures prove it. |

---

## 7. Features

### 7.1 Lighting (D3: dynamic lighting on base stages)

- **What:** clustered forward+ lighting with a compute pass for light lists.
  Every fixture in `room_lights.json` becomes a real light: its quad and
  direction give an area or spot light, its color and brightness give its
  intensity, and its on, healthy and light-op state switch and fade it the way
  the game already does (`types.h:5386`, 3670-3740 [V]). The sun lights outdoor
  stages, and muzzle flashes, explosions and `.pdeffect` lights become point
  lights. Normals are generated at cache build, because rooms and many props
  have none (2.3). (*proposal*)
- **Rooms as light boundaries:** a fixture lights only its own room and the
  rooms visible through its portals, using the `_PD_ROOM` vertex attribute and
  the public portal table (2.3) [V for the data]. That stops light leaking
  through walls before shadows are even considered, and keeps light lists
  short. [I]
- **The baked colors:** in remastered mode they stop being the lighting (D3).
  Their hue can stay as a material tint (the color divided by its brightest
  channel), and their brightness can guide each stage's light calibration.
  Until GI exists (7.3), a scaled-down copy can stand in for indirect light.
  [I]
- **Calibration as data:** PD's light values are artistic: a brightness byte, a
  multiplier, and per-room brightness ranges (`types.h:5386`, 3699-3735 [V]).
  The extractor writes default physical intensity, range and shadow settings
  for each fixture into a public lighting file in each `.pdscenario`, using the
  same light schema as Forge F4 (5.9), so stages are tuned as data, not code.
  (*proposal*)
- **Characters and props:** lit by the same lights plus the GI probes (7.3),
  instead of today's fixed default lights and flat shade color (2.4). [I]
- **Payoff:** high everywhere; this is the remastered look D3 asks for. [I]
- **Size:** XL, including normals, the light-list compute pass, the lighting
  file, and per-stage calibration with playtests. [I]
- **Risks:** a full relight of low-poly, 2000-era geometry can look flat or
  wrong until tuned, and tuning takes time per stage. Flash and light-op timing
  must match classic (`dlights.c:1563-1620` [V]). Mitigation: finish one
  showcase stage and get Mike's sign-off on its look before scaling up. [I]
- **Depends on:** R1, R4, R5, phase 3 (GPU models).

### 7.2 Shadows

- **What:** with the rooms relit (D3), static geometry must cast shadows too.
  Fixtures never move and only change brightness [I], so each shadowed fixture
  keeps a cached shadow map of the static rooms, rendered once and reused, and
  moving things (characters, props, the viewmodel) are added to it each frame.
  Cascaded sun shadows cover outdoor stages, with the sun direction from
  `struct sun` (`types.h:3276`) [V]. A per-view budget picks which nearby
  fixtures get shadows; the rest rely on the room boundaries (7.1) and GI. Blob
  shadows remain the fallback. (*proposal*)
- **Payoff:** high: it replaces the radial blobs (2.4) and makes the relight
  believable. [I]
- **Size:** L. [I]
- **Risks:** soft shadows from quad-shaped lights; cascade tuning on large
  outdoor maps; acne and peter-panning on low-poly geometry. [I]
- **Depends on:** 7.1, phase 3.

### 7.3 GI (now required on base stages, D3)

- **What:** a per-light probe bake. Fixtures never move and only change
  brightness [I], so the bake stores each fixture's bounce light separately in
  probes spread through each room's box (`types.h:3670-3740` [V]), plus sky and
  sun bounce for outdoor stages. At runtime the indirect light is the sum of
  each fixture's probes times that fixture's current brightness, so shooting
  out a light, or a light op, also dims its bounce: dynamic GI for the cost of
  a weighted sum. The bake runs on the player's machine, during cache build or
  on a stage's first load, and is kept as source-hashed cache ([D]
  `context/constraints.md:61`). Forge and custom maps use the same bake.
  (*proposal*, [I])
- **Payoff:** high. Without indirect light, a relight leaves everything outside
  a light's reach black. [I]
- **Size:** L for the probe bake and runtime. Lightmaps for finer detail would
  add L-XL (an unwrapper and a baker, linked statically). [I]
- **Risks:** bake time on first load (a progress screen, and the cache makes it
  once per stage); probe resolution against thin walls (helped by the room
  boundaries in 7.1); memory for rooms with many lights. [I]
- **Depends on:** 7.1.

### 7.4 PBR materials

- **What:** glTF metallic-roughness shading (GGX), matching `scene.glb` and
  `.pdmaterial` (2.3, 2.9) [V], with image-based lighting from the GI probes
  and the sky. A translator maps classic material inputs into it: N64
  two-texture blends, env and prim tints for cloak, shields and highlights,
  alpha modes and fog. (*proposal*)
- **Built in (D4):** every base material gets PBR inputs with no pack
  installed. They are derived on the player's machine from their own extracted
  textures and cached: roughness and metallic start from the texture's
  `surface_type` (8.9), a normal map comes from the texture's brightness used
  as height, and ambient occlusion comes from the geometry. Nothing derived
  from the ROM is shipped, so this stays within BYOR (8.7). Art packs replace
  any of these per material (section 8). (*proposal*, [I])
- **Payoff:** high together with dynamic lighting. How well derived maps look
  on PD's small, low-resolution textures is the open quality question. [I]
- **Size:** M-L for the material system, M for the built-in derivation; pack
  art is open-ended. [I]
- **Risks:** N64 combiner semantics, such as the 50/50 second-texture
  approximation (`scenario_scene_renderer.cpp:1184-1187`) [V]; a "plastic" look
  when derived values are wrong; noisy normal maps derived from tiny textures.
  [I]
- **Depends on:** R6, phase 3, section 8. It aligns with the September
  standard-glTF materials plan, which proposes a "source-owned float draw
  packet" and puts normals and PBR after base color ([D]
  `context/designs/modding/standard-gltf-runtime-materials-2026-09.md:59`, 71).

### 7.5 Instancing

- **What:** a GPU mesh cache per modeldef node (from native modeldefs at first,
  then from `.pdmesh` float packets) with per-vertex matrix indices.
  Per-instance matrix palettes and parameters go in SSBOs, and opaque draws are
  grouped by mesh and material per view. (*proposal*)
- **Payoff:** CPU time in remastered mode, dense mod scenes, and the swarm and
  setpiece goals ([D] `context/roadmap.md:939`). [I]
- **Size:** M after phase 3. [I]
- **Risks:** translucency order (4.3); special nodes such as gunfire and
  headspots. [I]
- **Depends on:** R2, phase 3.

### 7.6 Post effects

- **What:** an HDR target, tone mapping, bloom (emissive surfaces, flares,
  muzzle flashes), ambient occlusion (a large win for flat-lit original
  geometry), SMAA or TAA (TAA needs R4's previous matrices), per-stage color
  grading (Forge post-process volumes [D]), and native versions of the
  `bondview.c` effects. Until those exist, the classic GBI screen effects run
  over the remastered image (2.7). (*proposal*)
- **Size:** M. [I]
- **Risks:** TAA ghosting against PD's variable step; vision modes (night
  vision, IR, x-ray) need explicit treatment. [I]
- **Depends on:** phase 2.

### 7.7 Sky and atmosphere

- **What:** a sky dome driven by the stage's environment data (sky color,
  clouds, water, suns; 2.5) [V], a sun disc and flare that reuse the existing
  line-of-sight intensity (2.5), stars, sky rooms with a camera offset (CORE),
  and per-pixel distance and height fog with the same parameters. (*proposal*)
- **Size:** M. [I]
- **Risks:** PD's sky tricks (the moon and planet rooms, the elevator shaft)
  must keep working; CORE's `rooms.json` sky and render-always rows cover them
  ([D core]). [I]

### 7.8 Particles, decals, beams and flares

- **What:** GPU-instanced billboards with soft depth fade for sparks, smoke,
  explosions and shards, with the CPU simulation unchanged; projected decals for
  wall hits, using the collision-mesh hit normal (`bgTestHitScenarioMesh`,
  `bg.c:5520-5560`) [V]; emissive beams and tracers. (*proposal*)
- **Size:** L in total. [I]
- **Risks:** scenario-mesh hits carry texture -1 for surface sound and sparks
  ([D] `context/bugs.md:62-82`), so material-specific impacts need the texture
  surface type mapped onto collision triangles. [I]
- **Depends on:** R7.

### 7.9 Viewmodel and special views

- The viewmodel keeps its own projection and depth clear (2.4) [V]; it gains
  lighting from the player's room and the shadows it receives. [I]
- X-ray (Farsight), night vision and IR, cloak, shields and eyespy all have
  classic-only paths (`bgRenderSceneInXray`, `bg.c:912`, called at 1077-1078;
  `chrRenderCloak`, `chr.c:7053`; `chrRenderShield`, `chr.c:7211`;
  `bondview.c`) [V]. Each needs a remastered equivalent or an explicit fallback
  to classic for that frame. [I]

**Feature summary** [I]:

| Feature | Payoff (original assets) | Size | Needs |
|---|---|---|---|
| Dynamic lighting (D3) | High | XL | R1, R4, R5, phase 3 |
| Shadows, static and moving | High | L | Lighting |
| GI, per-light probes (required by D3) | High | L | Lighting |
| PBR with built-in materials (D4) | High with lighting; packs improve it | M-L + M, plus pack art | R6, phase 3 |
| Instancing | Low to medium | M | R2, phase 3 |
| Post effects | High (AO, bloom, AA) | M | Phase 2 |
| Sky | Medium | M | Phase 2 |
| Effects | Medium | L | R7 |

---

## 8. Material and asset override format

### 8.1 What exists

- `.pdmaterial` is `material.ini` (`catalog_id`, `name`, `material_file`,
  optional `texture_archive` and `effect_archive`), as in the scanner template
  (`port/src/assetcatalog_scanner.c:532-541`), plus a JSON file with
  `shading_model`, `base_color`, `roughness`, `metallic` and `emissive`
  (`asset_runtime.c:940-976`). [V]
- Base materials are `material_default` (`classic_lit`), `material_translucent`
  (`classic_alpha`) and `material_emissive` (`classic_emissive`)
  (`port/src/assetcatalog_base_extended.c:269-278`). [V]
- `.pdtexture` is `texture.ini` plus an image, with properties `surface_type`
  (default, stone, wood, metal, glass, shallow_water, snow, dirt, mud, tile,
  metal_object, character, glass_translucent, none, deep_water),
  `sound_surface_type`, tile and mask fields and `properties_version`
  (`texture_source_properties.c:8-12`, 61-98). [V]
- `.pdskin` is `skin.ini` with `target`, `texture_file` and `material_archive`
  (`assetcatalog_scanner.c:553-562`). [V]
- Typed archives must be self-contained, with dependencies embedded; `.pdmod`
  is transport only ([D] `context/constraints.md:60`).

### 8.2 Proposed `.pdmaterial` schema 2 (*proposal*)

```ini
; material.ini
[material]
catalog_id = mod_remaster_villa:material_villa_floor_stone
name = Villa floor stone (remastered)
schema = 2
material_file = material.json
mode = remastered            ; remastered (default for schema 2) | both
targets = base:texture_unlabeled_xxxx   ; one or more catalog IDs (8.4)
```

```json
{
  "catalog_id": "mod_remaster_villa:material_villa_floor_stone",
  "shading_model": "pbr_metal_rough",
  "base_color": [1.0, 1.0, 1.0, 1.0],
  "roughness": 0.85,
  "metallic": 0.0,
  "emissive": false,
  "textures": {
    "base_color": "dependencies/assets/textures/floor_albedo.pdtexture",
    "normal":     "dependencies/assets/textures/floor_normal.pdtexture",
    "orm":        "dependencies/assets/textures/floor_orm.pdtexture"
  },
  "uv": { "space": "source", "scale": [1.0, 1.0] }
}
```

- Texture archives gain `usage` (base_color, normal, orm, emissive) and
  `color_space` (srgb or linear) in `texture.ini`. The contract already plans
  for color space and compression intent ([D]
  `asset-archive-clean-formats.md:125`).
- `uv.space = source` means the original mapping, with N64 texel units, shift
  and wrap from `extras.pd2_material`, so a higher-resolution texture lines up
  without re-authoring the mesh. [I]
- A schema 2 material never changes gameplay surface data. Visual overrides
  can't change footstep sounds or impacts in multiplayer. [I]
- Old clients must refuse schema 2 cleanly ("requires 2.0") rather than
  mis-load. Conformance closes the shading-model set today
  (`asset_archive_conformance.py:3327-3331`) [V]; schema 2 adds
  `pbr_metal_rough` behind the version check. [I]

### 8.3 Modes (*proposal*)

- `mode = remastered`: applies only in the remastered renderer, so classic keeps
  the original look. This is the default for PBR packs.
- `mode = both`: a classic-visible replacement, like today's mod textures,
  needing classic-compatible images.
- The weapon material and grip override files retired from v1 "until a real
  renderer/attachment backend exists" ([D]
  `asset-archive-clean-formats.md:121`) return here as schema 2 materials.

### 8.4 Targeting base content

- Today extracted materials are named by a per-scene ordinal and texture number
  (`mat_%03u_tex_%04x_c%d`, `romextract_pdarena.c:5585-5592`), and images by
  texture number (5477). [V] Ordinals shift when extraction changes, and
  numbers are not allowed as public identity ([D] `context/constraints.md:53`).
- Target by the texture's catalog ID (`base:texture_unlabeled_*`,
  `catalog_readable_ids.c:214-218`) [V], which needs R6 to carry that ID into
  `scene.glb` and `.pdmesh` material records. More specific targets would be
  `base:model_*#slot` or `base:scenario_*#material`. (*proposal*)
- At runtime, remastered resolves its own materials from the snapshot's
  material identity. Classic `mode = both` overrides would also need a
  reverse map from address to texture number to catalog ID (from `struct tex`,
  2.9), because fast3d keys textures by address (2.2). [I]

### 8.5 Packaging, precedence and enablement (*proposal*)

- A "remaster pack" is an ordinary mod: `.pdmod` transport containing typed
  `.pdmaterial` and `.pdtexture` archives, scanned, enabled by default and
  toggled in the Mod Manager like other mods ([D]
  `context/designs/modding/mod-enablement-policy.md`).
- When several packs target one ID, the later pack in mod order wins. The
  Modding Hub shows conflicts. The catalog override source
  (`assetcatalog.h:1059-1060`) is the binding point. [V for the API]

### 8.6 Cache

- Mip chains, BC7 (color) and BC5 (normal) compression, ORM packing, generated
  normals and tangents, probes and lightmaps are private cache keyed by the
  source bytes, never shipped ([D] `context/constraints.md:61`).

### 8.7 BYOR

- Remastered art in releases or shared packs must be original. Nothing derived
  from ROM textures ships (BYOR is a hard constraint). [I]
- The built-in material derivation (D4, 7.4) is local processing of the
  player's own extracted textures, written only to private cache and never
  exported, the same kind of work extraction already does. Heavier local tools,
  such as AI upscaling of the player's own textures, remain a separate legal
  and product question for Mike. [I]

### 8.8 Multiplayer (D4)

- The remastered renderer is built in and toggled per player. The choice never
  reaches the wire, saves or match manifests (5.1), so players in one match can
  use different modes. [I]
- Art packs are ordinary mods under the existing rules. Enabled mods go into the
  match manifest and are distributed ([D]
  `context/designs/connectivity/manifest-architecture.md:21`), so a large pack
  enabled by the host reaches every client. Revisit only if pack size becomes a
  problem (R8 was dropped). [I]

### 8.9 Built-in materials (D4)

- The remastered look must stand without packs, so the engine derives PBR inputs
  for every base material (7.4). `surface_type` gives the starting values, for
  example metal to metallic about 0.9 and roughness 0.35, glass to roughness
  0.05, stone to roughness 0.85. The original texture is the albedo, tinted by
  the hue of the baked vertex colors (7.1). [I]
- The derivation runs locally and is kept as cache, so nothing from the ROM is
  shipped (8.7). Packs override it per material. [I]
- The `surface_type` defaults table is public data, so it can be tuned without
  code changes. (*proposal*)

---

## 9. What 1.0 should avoid (so 2.0 stays easy)

These are *proposals*; each one points at the evidence that motivates it.

1. **No new gameplay in draw functions.** New per-frame logic goes in tick
   paths, not in `lvRender` or `*Render` (3.1).
2. **No new gameplay reads of display lists or render output.** New hit or
   visibility queries use collision data, the B-1189 pattern (3.4).
3. **No more renderers outside display-list order.** Use an inline extension
   command with captured per-view state, CORE's B-1172 pattern, never a draw at
   frame start (2.3, 3.10).
4. **Keep renderer approximations out of authored data.** Material fields such
   as roughness and metallic stay raw in source and runtime records; classic
   tints stay in the classic consumer, as `.pdeffect` already does (2.9).
5. **Don't strip standard glTF material data at import.** Keep normals, PBR
   channels and extra texture sets in source even while they are ignored.
   T-ASSETS-052 rejects only required extensions, sparse and normalized
   accessors, and other minimum versions (commit `55812690e`) [V]; keep it that
   narrow.
6. **Give extracted materials stable catalog identities** (R6), not ordinals or
   texture numbers (8.4).
7. **Keep N64 limits out of public formats.** The 64-color palette, 255-pixel
   texture caps (`struct tex` stores width and height as `u8`, `types.h:6109`
   [V]) and s16 coordinates stay inside the native adapter ([D]
   `context/designs/modding/standard-gltf-runtime-materials-2026-09.md:24-30`;
   `context/constraints.md:62-63`).
8. **New per-instance render state goes on the instance,** not on shared
   definitions (3.5).
9. **New visual randomness uses a presentation RNG,** or at least not the
   gameplay one (3.8).
10. **New effects are declarative presentation commands,** like `.pdeffect`,
    not hand-emitted GBI (2.7).
11. **Don't widen `GfxRenderingAPI` for remastered features;** it sits below
    the point where objects are known (2.2).
12. **Prefer static linking, and follow D6 for anything else.** Graphics DLLs
    that ship with Windows load from the system; any other DLL that is truly
    needed goes in the release's `dll/` folder, so players install nothing.
    Load GL 4.3+ features with the swarm pattern.
13. **Version typed archives** so older clients refuse newer material schemas
    cleanly (8.2).
14. **Keep the classic path capturable** (reference screenshots and logs), since
    it is the parity baseline (R9).
15. **No new raw OpenGL outside fast3d's backend.** Under D2 every raw-GL user
    is port work (R10); route new GPU drawing through fast3d, or keep it in one
    small, isolated module.
16. **Create ImGui textures through one helper,** so that switching ImGui
    textures from GL names to bgfx handles (R10) changes one place instead of
    seven files.

Two cheap things worth doing in 1.x: R3 (presentation RNG, S) and R6 (stable
material identity, M). [I]

---

## 10. Open questions for Mike

Each of these should become a Workbench decision item. *Recommendations are
proposals.*

1. **Toggle granularity. Decided (D1):** one switch, like Halo: Combat Evolved
   Anniversary, with developer-only per-pass overrides for debugging.
2. **Engine and graphics API. Decided (D2):** option B, our own renderer on
   bgfx, with classic moved onto it (5.8, R10). Its follow-ups D5 to D7 are
   decided too (items 12 to 14).
3. **Look on base stages. Decided (D3):** PBR materials and dynamic lighting,
   the full-relight direction.
4. **Source of remastered art. Partly decided (D4):** the baseline is the
   built-in derivation, and packs are ordinary mods. Still open: whether the
   project makes an original showcase pack, and whether heavier local tools
   such as AI upscaling are acceptable (8.7).
5. **Packs in multiplayer. Decided (D4):** packs are ordinary mods under the
   existing rules; the renderer itself is built in and chosen per player.
6. **Shipping hybrid frames.** May 2.0 ship with some passes still classic in
   remastered mode, for example effects? (A) Yes: ship sooner. (B) No: ship
   only when every 3D pass is remastered. *Recommend A.*
7. **Split-screen and listen-host budgets.** How many remastered views must
   run, and at what frame rate, on Mike's reference machine?
8. **Frame interpolation.** Should 2.0 decouple drawing from the simulation step
   and interpolate snapshots for high refresh rates, or leave that for 2.x?
   *Recommend 2.x; R4 keeps it possible.*
9. **Theater.** Must Theater playback support both renderers in 2.0?
   *Recommend yes; it follows from the snapshot design.*
10. **Forge F4 lighting.** Remastered only, or also baked into vertex colors for
    classic? *Recommend both, through one light schema.*
11. **Who owns R1** (the `lvRender` split)? It touches gameplay timing across
    every lane and should run during a source freeze with the parity harness.
12. **D5. Default graphics API on Windows. Decided:** classic moves onto bgfx's
    OpenGL backend first, then D3D11 becomes the default with GL 4.3 as
    fallback and Vulkan as an option (section 0).
13. **D6. The no-DLL rule. Decided:** graphics DLLs that ship with Windows,
    vendor extras patched out of release builds, and a `dll/` folder for
    anything else truly needed, so a download needs no prerequisites
    (section 0).
14. **D7. Ray tracing. Decided:** deferred to 3.0; 2.0 keeps bgfx and the probe
    bake (section 0).

---

## 11. Phased plan

| Phase | Scope | Exit criteria | Size |
|---|---|---|---|
| 0. 1.x guardrails | Section 9 rules; land B-1172 from core; R3 and R6 if cheap; continue the standard-glTF materials plan. | No new couplings in review; extracted materials carry catalog IDs. | S-M [I] |
| 1. Separation | R1, R2, R4, R5, R9; snapshot schema and producers for views, rooms, lights, environment, instances and `.pdeffect`. | Classic captures and gameplay logs unchanged; the listen host no longer builds remote views; the snapshot covers every opaque world draw. | XL [I] |
| 2a. Classic onto bgfx (D2, D5, R10) | First on bgfx's OpenGL backend: fast3d backend on bgfx, the combiner uber-shader, ImGui on bgfx, the scene renderer and the other raw-GL users. Then D3D11 as the default with GL 4.3 as fallback and Vulkan as an option, SDL windows without a GL context, and the DLL rule (D6). | On bgfx's GL backend, classic is pixel-identical to the 1.0 GL renderer on R9 captures; on D3D11 every remaining difference is explained and accepted; then the old GL backend is removed. | L-XL [I] |
| 2b. Remastered skeleton | bgfx views and passes; HDR target; `G_REMASTER_PASS_EXT`; the Halo-style switch (D1); rooms in neutral shading; tone mapping. | Toggling changes only pixels: gameplay logs identical in both modes. | M-L [I] |
| 3. Objects | GPU model cache, per-instance matrices, visibility and shading; characters, props, viewmodel and attachments; translucency order; opaque instancing. | All models remastered; neutral mode matches classic composition. | L-XL [I] |
| 4. Lighting, shadows and GI (D3) | Normals; clustered lights; fixtures as real lights bounded by rooms; the per-stage lighting file; cached fixture shadows and sun cascades; the per-light probe bake. One showcase stage first, then the rest. | Mike signs off the showcase stage's look; flash and light-op timing match classic. | XL [I] |
| 5. Built-in materials (D4) | PBR shading, the built-in derivation, the translator for N64 blends and tints. | Every base stage renders in remastered mode with no packs installed. | M-L [I] |
| 6. Post | AO, bloom, AA, color grading, native screen effects. | Performance budget met (decision 7). | M [I] |
| 7. Packs | Schema 2 `.pdmaterial` and `.pdtexture`, targeting, modes, cache, Mod Manager conflicts; an original showcase pack if decision 4 says so. | A pack changes only remastered mode, and old clients refuse it cleanly. | M-L [I] |
| 8. Sky and effects | Sky dome, stars, fog, particles, decals, beams, flares, x-ray and vision modes. | No classic fallback left in normal play. | L [I] |
| 9. Forge lighting | Shared light schema with Forge F4; optional lightmaps for custom maps. | A custom map lit in both modes. | M-L [I] |
| 10. Qualification | Fallback hardware, split-screen, listen host, Theater, memory, long sessions. | Release gate. | M [I] |

With B (D2), the whole scope is roughly 13 to 23 months of focused work, and a
first shippable remastered mode (phases 1 to 5 plus AO and bloom) roughly 10 to
16 months. All ranges have low confidence. D3 is the main reason they grew (a
full relight needs GI, static shadows and per-stage tuning), and D2 adds the
classic port in phase 2a. Phases 1 and 2a touch different code (game logic in
`src/game` against the renderer in `port/fast3d`), so they can run in parallel.
[I]

### 11.1 Progress

- **Phase 2a step 1 (2026-11-17, branch `week-pd/bgfx-classic`, not merged).**
  Classic runs on bgfx's OpenGL 4.3 backend behind `--renderer bgfx`; the 1.0
  GL renderer stays the default and runs unchanged without the flag. bgfx,
  bx and bimg build from pinned source as static libraries in our CMake and
  MinGW build, with shaders compiled ahead of time; no DLL is added, and the
  release build contains no AMD AGS, NVAPI or RenderDoc loader (D6). The
  combiner is one precompiled uber-shader. The scene renderer, ImGui and the
  pdgui textures draw through bgfx; the GPU swarm is compute only and
  unchanged; the mesh debug view reports that it is unavailable.
  `--renderer-parity` draws one frame's commands through both renderers and
  compares the frames (the R9 role for this step). First results: gameplay
  frames on Defection and Felicity differ in under 0.014% of pixels, almost
  all by one level (working explanation: floating-point rounding of
  differently written shader source on one driver); Agent Select differed in
  a thumbnail the game copies from the front buffer, fixed but not yet
  re-run; split-screen cannot be drawn (one local player). Design, D6 review
  and results: [bgfx-classic-gl.md](bgfx-classic-gl.md).

---

## 12. Risks

| Risk | Effect | Mitigation |
|---|---|---|
| R1 changes gameplay order | Subtle AI, combat or netplay regressions | Keep the per-player interleave, move statements without reordering, run the R9 parity harness, land it in a freeze. [I] |
| Two presentation paths drift | Remastered shows a different visible set or placement | Record the snapshot in the same functions that emit classic GBI (3.12); neutral-mode parity captures. [I] |
| Shared depth and draw space | B-936-style invisible or misordered objects | Draw in the game's draw space (5.7); CORE's matrix probe as a test. [I], [V core] |
| Classic parity on bgfx (D2) | Classic looks different after the port, the problem Halo's classic mode later had ([W]) | Port on bgfx's GL backend first; check the combiner uber-shader against R9 captures from the 1.0 renderer; keep the old backend until parity holds. [I] |
| Runtime DLL loads (D2) | bgfx loads graphics DLLs and tries vendor extras at runtime ([W]) | D6 (section 0): system graphics DLLs only, vendor extras patched out of release builds, a `dll/` folder for anything else, and new load calls reviewed under the write-policy guard. [I] |
| Full relight look (D3) | Base stages look flat or wrong on original geometry | One showcase stage with Mike's sign-off before scaling; calibration as data; GI and AO from the start. [I] |
| Per-stage tuning time | 20-plus stages each need light calibration | Extractor defaults from the original light data, tuned in data files, budgeted inside phase 4. [I] |
| BYOR | No shippable remastered base art | Built-in derivation runs locally (D4); packs must be original art. [I] |
| Hardware floor | Some players can't use remastered | Classic runs everywhere; remastered is greyed out below the API level D2 sets (5.6). [I] |
| VRAM and load time | Large packs; both renderers resident for the instant switch (D1) | Stage-scoped residency, texture streaming, and a low-memory fallback that builds remastered resources on the first toggle (5.6). [I] |
| Multiplayer mixing | A large pack enabled by the host is sent to every client | Packs are ordinary mods (D4), so they follow normal mod distribution; the renderer choice itself never reaches the wire (5.1). Revisit if pack size becomes a problem. [I] |
| Special effects (cloak, shields, x-ray, vision modes) | Missing or wrong in remastered | Per-frame classic fallback for that view until native versions exist (7.9). [I] |
| Scope creep | 2.0 slips | The phase exits in section 11; ship hybrid frames (decision 6). [I] |

---

## Appendix: not checked

- No build, run, capture or performance measurement was made, so all costs and
  sizes are estimates.
- CORE's B-1172 work was read from git objects at `ed1756cf1`. Its in-game
  status comes from its ledger, not from this session.
- The camera mismatch in 2.3 (rooms drawn with the last player's camera
  wherever more than one world branch runs) comes from code reading only and
  was not seen in game. CORE's camera slots address it.
- Halo: Combat Evolved Anniversary was checked only at the level of public
  descriptions (sources below), not its internals.
- The third-party library facts in 5.8 come from the sources below and were not
  tried here: nothing was built with them.
- How the updater applies a release that gains a new folder (`dll/`, D6) was
  not checked.

## Appendix: web sources for [W] claims (checked 2026-11-16)

- Halo: Combat Evolved Anniversary uses Saber's engine for the visuals while
  the original engine keeps gameplay:
  https://en.wikipedia.org/wiki/Halo:_Combat_Evolved_Anniversary
- Its graphics switch is instant:
  https://gameinformer.com/games/halo_combat_evolved_anniversary/b/xbox360/archive/2011/07/22/video-shows-instant-graphical-hot-swapping
  and https://pcgamer.com/halo-ce-switch-graphics
- Its classic mode was later fixed to match the original:
  https://retrorgb.com/halo-combat-evolved-classic-graphics-in-mcc-gets-fixed.html
- Godot as a library (basic support in 4.6; embedding in a host UI is Apple-only
  so far): https://talks.godotengine.org/godotcon-ams-2026/talk/DQXZZC/ and
  https://github.com/migeran/libgodot
- Filament on Windows builds with clang or MSVC:
  https://github.com/google/filament/issues/12 and
  https://www.mintlify.com/google/filament/platforms/desktop
- bgfx licence (BSD-2), backends (D3D11, D3D12, Vulkan, OpenGL 4.3+), listed
  compilers, the multithreaded-renderer note, and no ray tracing listed:
  https://bkaradzic.github.io/bgfx/overview.html
- bgfx's D3D11 backend loads `d3d11.dll` and DXGI at runtime and tries AMD AGS
  and NVAPI:
  https://raw.githubusercontent.com/bkaradzic/bgfx/master/src/renderer_d3d11.cpp
- MAME builds with MSYS2 MinGW and bundles bgfx:
  https://docs.mamedev.org/initialsetup/compilingmame.html and
  https://git.redump.net/mame/tree/3rdparty/bgfx/README.md
- bgfx adopts a caller-supplied GL context:
  https://git.redump.net/mame/plain/3rdparty/bgfx/src/glcontext_wgl.cpp?h=mame0226
- Diligent attaches to the current GL context; its build notes call MinGW
  reduced-capability and not advised:
  https://diligentgraphics.github.io/docs/d2/df3/DiligentCore_Graphics_GraphicsEngineOpenGL_readme.html
- Ogre-Next: no MinGW statement found (search only):
  https://wiki.ogre3d.org/Specific+Build+Guides
- Windows searches the program's folder, the system folders, the current folder
  and `PATH` for DLLs, not subfolders; `SetDefaultDllDirectories` and
  `AddDllDirectory` add folders (D6):
  https://learn.microsoft.com/en-us/windows/win32/dlls/dynamic-link-library-search-order
