# Perfect Dark 2 — PC Port

## Project
Merged PC port combining AllInOneMods (GEX, Kakariko, Goldfinger 64, Dark Noon) + netplay (ENet).
PC only — x86_64 via MSYS2/MinGW + CMake. SDL2 + OpenGL rendering.

## Stack
- **Language**: C (game code), C++ (port/fast3d renderer, ImGui backend)
- **Build**: CMake with `file(GLOB_RECURSE)` auto-discovery. PowerShell build-gui.ps1 for Windows.
- **Rendering**: fast3d GBI translator (N64 display lists → OpenGL), Dear ImGui v1.91.8 overlay
- **Networking**: ENet (UDP), server-authoritative with client prediction
- **Platform**: Windows x86_64 (MinGW), no N64 constraints apply

## Critical Rules
- **PC-only target**: No N64 or Switch support. Modern hardware, no legacy constraints.
- **New code types**: Standard C types are fine (`bool`, `int`, `float`, `uint8_t`, `<stdbool.h>`).
- **Legacy types**: Existing decompiled code uses `s32/u8/f32` etc. from `PR/ultratypes.h` — these
  are just typedefs and mix safely with standard types. Modernize organically as files are touched.
- **No platform guards**: Zero `PLATFORM_N64` guards remain. All new code is unconditional.
- **Memory**: `mempAlloc(size, MEMPOOL_STAGE)` for stage-lifetime allocations (existing system).
  `IS4MB()` is compile-time `0` and `IS8MB()` is compile-time `1` — the compiler eliminates 4MB branches.
- **Modern HW**: Per-triangle collision, BVH, runtime raycasts — all trivially cheap. Prefer
  correctness over micro-optimization. Legacy workarounds exist because of N64 limits, not design.
- **Dead code removed**: All N64 assembly (.s), ultra/os, ultra/libc, and most ultra/io files removed.
  Only ultra/audio, ultra/gu, and 4 ultra/io vi mode files remain (all compiled).

## Architecture
- `src/` — Original decompiled game code (C). `src/game/`, `src/lib/`, `src/include/`
- `port/` — PC port additions (C/C++). `port/fast3d/`, `port/src/`, `port/include/`
- `include/PR/` — N64 SDK headers (ultratypes.h, gbi.h)
- `context.md` — Detailed project context with all completed phases and technical notes

## Key Subsystems (see subdirectory CLAUDE.md files)
- `src/game/` — Game logic: player movement, props, menus, multiplayer
- `src/lib/` — Engine libraries: collision, capsule physics, model loading
- `port/fast3d/` — Rendering: GBI translator, ImGui backend, PD-authentic styling
- `port/src/net/` — Networking: ENet integration, message handlers

## Repository
GitHub: https://github.com/MikeHazeJr/perfect-dark-2
