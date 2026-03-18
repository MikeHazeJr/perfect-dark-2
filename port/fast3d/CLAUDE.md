# port/fast3d/ — Rendering & ImGui

GBI display list translator (N64 → OpenGL) and Dear ImGui overlay.

## Rendering Pipeline
`gfx_run()` processes one frame: game GBI commands → OpenGL → ImGui overlay → swap buffers.
Order: game render → `rapi->end_frame()` → GL state reset → `pdguiNewFrame()` → `pdguiRender()` → swap.

## Key Files
- **gfx_pc.cpp** — Main GBI command dispatcher. Decodes N64 display lists into OpenGL.
  - `C0(pos, width)` / `C1(pos, width)` — Extract bit fields from Gfx command words
  - G_TRI1 (0xBF): `C1(16,8)/10, C1(8,8)/10, C1(0,8)/10` → vertex indices
  - G_TRI4 (0xB1): 4 packed triangles, 4-bit indices from C0/C1
  - G_VTX (4): Vertex load command
  - G_ENDDL (0xB8): End display list

### ImGui Backend
- **pdgui_backend.cpp** — C-callable ImGui API (pdguiInit, pdguiRender, etc.)
  - SDL2 + OpenGL3 backends, GLSL 1.30
  - F12 toggles overlay, consumes event from game
  - Loads embedded Handel Gothic font (PD's original menu font)
- **pdgui_style.cpp** — PD-authentic ImGui styling
  - Blue palette from g_MenuColours[1]: dark blue panels, cyan text
  - Animated shimmer effect (20-second cycle white gradient on borders)
  - `pdguiApplyPdStyle()`, `pdguiRenderAllWindowShimmers()`

## ImGui Version
Dear ImGui v1.91.8. Vendored in `port/fast3d/imgui/`. Uses `imgui_internal.h` for
window access. Note: `TitleBarHeight` is a member variable (not function) in this version.

## Headers
- `port/include/pdgui.h` — Public C API (extern "C" guards)
- `port/include/pdgui_style.h` — Style functions (extern "C" guards required!)
- `port/include/pdgui_font_handelgothic.h` — Embedded font data (auto-generated C array)

## Model Geometry (for collision)
- `Vtx`: s16 x, y, z + flags + colour + s/t (from PR/gbi.h)
- Model nodes: MODELNODETYPE_DL (0x18) has vertices + display lists
- MODELNODETYPE_GUNDL (0x04) also has vertices
- MODELNODETYPE_BBOX (0x0a) has xmin/xmax/ymin/ymax/zmin/zmax
