# Perfect Dark Mike — Project Context

## Onboarding Instructions (For AI Sessions)

You are working on a **PC port of Perfect Dark** (the N64 FPS by Rare, 2000). The project is called
**perfect_dark-mike**. It is a C11 codebase — a decompilation/port of the original game, now PC-only.

**The user (Mike)** is the sole developer. He builds on his Windows PC using MSYS2/MinGW. You cannot
compile this project — the build environment is Windows-only. You write code, Mike compiles and tests.

**Your role**: Collaborative engineering partner. Mike thinks in systems and phases. He values depth,
correctness, and iterative progress. When writing code: prefer robustness over micro-optimization.
When something is broken: diagnose root cause, don't patch symptoms. When unsure: say so.

**To get up to speed on a new session:**
1. Read **tasks.md** FIRST — it shows what was in progress and what's blocked
2. Read the **system file(s)** relevant to the current task (collision.md, movement.md, etc.)
3. Read **roadmap.md** if Mike asks about future work or planning
4. Do NOT re-read every file every session — only read what's needed for the active task
5. After reading, confirm what you understand and ask Mike what he'd like to do next

## IMPORTANT: Modern Hardware — No N64 Constraints
This is a **PC-only port running on modern x86_64 hardware**. The original N64's computational
constraints **do not apply**. Prefer correctness over micro-optimization. Legacy collision/physics
workarounds exist because of N64 limits, not because they're good design. Use proper geometric
solutions (per-triangle collision, BVH, runtime raycasts) rather than layering more hacks.

## Project Overview
Merged PC port combining:
- **AllInOneMods**: GEX, Kakariko, Goldfinger 64, Dark Noon, extra stages (community mod content)
- **Netplay**: ENet-based multiplayer (deathmatch, co-op campaign, counter-operative)
- **PC-only**: All 672 N64 platform guards stripped (Phase D1 complete). Zero `PLATFORM_N64` references remain.

**Language**: C11. **Build**: CMake + MinGW GCC. **Output**: `build/pd.x86_64.exe`

## File Index

| File | System | What It Covers |
|------|--------|----------------|
| [tasks.md](tasks.md) | Task Tracker | **Read first.** Current task, step-by-step progress, blocked items, testing status |
| [collision.md](collision.md) | Collision | Capsule sweep, floor/ceiling detection, legacy cdTestVolume, geometry types |
| [movement.md](movement.md) | Movement & Jump | Jump physics, vertical movement, ground detection, airborne logic |
| [networking.md](networking.md) | Networking | All completed phases (1-10, C1-C12), message types, resync, damage authority |
| [build.md](build.md) | Build & Infra | CMake, MSYS2/MinGW, Build Tool GUI, static linking, mod loading |
| [roadmap.md](roadmap.md) | Modernization | Planned: mod manager, NAT traversal, dedicated server, map editor, etc. |

## Key Architectural Facts
- **Net modes**: `g_NetMode` — NETMODE_NONE(0), SERVER(1), CLIENT(2)
- **Game modes**: `g_NetGameMode` — MP(0), COOP(1), ANTI(2)
- **Protocol version**: 18. **Tick rate**: 60 Hz
- **Channels**: Unreliable for position updates, reliable for state/events
- **Props** identified by `syncid` (offset 0x48 on prop struct, PC-only)
- **Bots**: PROPTYPE_CHR with `chr->aibot != NULL`
- **NPCs**: PROPTYPE_CHR with `chr->aibot == NULL` and not player-linked
- **Player capsule**: radius ~30 units, height = vv_headheight
- **Geometry types**: geotilei (s16 BG), geotilef (float lifts), geoblock (XZ poly + Y bounds), geocyl (cylinder)
- **Room-based geo**: `g_TileFileData` + `g_TileRooms[roomnum]` offsets
- **Build**: CMake + MSYS2/MinGW on Windows. User compiles — AI cannot.

## Working Conventions
- Mike builds and tests. You write and review code.
- Tasks proceed in phases. Each phase is self-contained and testable.
- **Always update tasks.md** when starting, completing, or blocking on a step.
- When writing new code: add it to the relevant system file (collision.md, movement.md, etc.)
- The old monolithic `context.md` in the repo root is preserved as historical reference.

## Reference Codebases (for comparison/debugging)
- `perfect_dark-netplay/perfect_dark-port-net/` — Unmodified netplay port
- `perfect_dark-AllInOneMods/perfect_dark-allinone-latest/` — Mod content source
- `perfect_dark-mike/` — This project (the merged working copy)
