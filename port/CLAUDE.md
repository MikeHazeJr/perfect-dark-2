# port/ — PC Port Additions

All C++ and PC-specific code. Auto-discovered by CMake `GLOB_RECURSE port/*.cpp`.

## Structure
- `fast3d/` — GBI renderer, ImGui backend and styling (see fast3d/CLAUDE.md)
- `src/net/` — ENet networking: message handlers, server/client logic
- `src/` — Other PC port additions (system, config, input)
- `include/` — Headers for port code (pdgui.h, net/*.h)
- `external/` — Third-party: SDL2, glad, ENet, zlib

## Networking (port/src/net/)
- **net.c** — Server/client lifecycle, tick loop, resync system
- **netmsg.c** — All message encode/decode (SVC_* server→client, CLC_* client→server)
- **netmenu.c** — Network UI: server browser, host dialog, co-op setup
- Protocol version: 17. ENet UDP with reliable/unreliable channels.
- Server-authoritative: damage, spawns, score. Client predicts movement.

## Headers (port/include/)
- **pdgui.h** — ImGui overlay C API (init, render, shutdown, event, toggle)
- **pdgui_style.h** — PD-authentic styling (must have extern "C" guards!)
- **pdgui_font_handelgothic.h** — Embedded Handel Gothic font data
- **net/net.h** — Network constants, structs, globals
- **net/netmsg.h** — Message type defines (SVC_*, CLC_*)

## Build Notes
- C++ files: `.cpp` extension, auto-discovered by CMake GLOB_RECURSE
- C interop: All public APIs use `extern "C"` linkage
- ImGui v1.91.8 vendored in `fast3d/imgui/`, uses glad (not imgl3w)
- SDL2 + OpenGL3 backends, GLSL 1.30 (#version 130)

## Asset Catalog — Mandatory Rules (SA-7)

### Net message asset references
`catalogWriteAssetRef` / `catalogReadAssetRef` are the **only** permitted functions
for encoding/decoding asset references in net messages.  Never write or read raw
`bodynum`/`headnum` integers directly into message buffers.

### Catalog-first file resolution
All asset file resolution must go through catalog accessor functions:
- **Bodies**: `catalogGetSafeBody()`, `catalogGetSafeBodyPaired()`
- **Heads**: `catalogGetSafeHead()`
- **Filenames/filenums**: `catalogGetBodyFilenumByIndex()`, `catalogGetHeadFilenumByIndex()`
- **String IDs → entries**: `assetCatalogResolve()`

Never access `g_HeadsAndBodies[]` directly from networking, UI, or gameplay code.
Allowed read-sites are: `assetcatalog_base.c` (registration), `assetcatalog_api.c`
(implementation), `modelcatalog.c` (validation/thumbnail system).
