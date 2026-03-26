# Perfect Dark Mike — Project Context Index

> **Last updated**: 2026-03-25, Session 48 (Mesh collision system active, dev window overhaul, project cleanup, debug visualization, unified release pipeline)
> This file is the master hub. Read it first every session. Everything links from here.

## Onboarding (For AI Sessions)

**Project**: PC port of Perfect Dark (N64 FPS, Rare 2000). C11 codebase, CMake + MinGW/GCC.
**Developer**: Mike (sole dev, builds on Windows via MSYS2). AI writes code, Mike compiles and tests.
**Role**: Collaborative engineering partner. Depth over shortcuts. Root cause over patches.

**Session start protocol**: Read this index → [constraints.md](constraints.md) → [session-log.md](session-log.md) (last 3) → [tasks-current.md](tasks-current.md). Load domain files only when relevant to the active task.

---

## Quick Status

| Area | Status | File |
|------|--------|------|
| **What to do next** | Active tasks + backlog | [tasks-current.md](tasks-current.md) |
| **What's done** | Completed work archive | [tasks-archive.md](tasks-archive.md) |
| **What we must respect** | Active/removed constraints | [constraints.md](constraints.md) |
| **Infrastructure phases** | D1–D16 execution status | [infrastructure.md](infrastructure.md) |
| **Long-term vision** | Priority ordering + dependency graph | [roadmap.md](roadmap.md) |
| **Release milestones** | Planned stable release builds | [milestones.md](milestones.md) |
| **Open bugs** | One-off issues (open/fixed) | [bugs.md](bugs.md) |
| **Systemic patterns** | Architectural bug classes | [systemic-bugs.md](systemic-bugs.md) |
| **QC test checklist** | In-game verification items per build | [qc-tests.md](qc-tests.md) |

---

## Session History

Recent sessions are in [session-log.md](session-log.md). Archives below.

| Sessions | Period | Focus | File |
|----------|--------|-------|------|
| 47a-48 | 2026-03-24-25 | MEM-1, B-12 P2, stage decoupling, SPF-1, mesh collision, dev window, cleanup | [session-log.md](session-log.md) |
| 22-46 | 2026-03-22-24 | D3R component mod architecture, asset catalog, participant system, bot customizer, network distribution | [sessions-22-46.md](sessions-22-46.md) |
| 14-21 | 2026-03-21-22 | Combat stabilization, memory modernization, menu Phase 2 | [sessions-14-21.md](sessions-14-21.md) |
| 7-13 | 2026-03-18-21 | Networking phases, model loading, dedicated server | [sessions-07-13.md](sessions-07-13.md) |
| 1-6 | 2026-03-01-18 | N64 strip, mod manager, ImGui foundation, char select | [sessions-01-06.md](sessions-01-06.md) |

---

## Domain Files (load when working on that system)

| File | System | When to load |
|------|--------|-------------|
| [collision.md](collision.md) | Capsule sweep, floor/ceiling, legacy cdTestVolume, geometry types | Collision/physics work |
| [movement.md](movement.md) | Jump physics, vertical movement, ground detection, airborne logic | Movement/jump work |
| [networking.md](networking.md) | ENet protocol, message types, resync, damage authority (phases 1–10, C1–C12) | Netcode work |
| [imgui.md](imgui.md) | ImGui integration, PD-authentic styling, shimmer, palette system, debug menu | Menu/UI work |
| [build.md](build.md) | CMake, MSYS2/MinGW, build tool GUI, static linking, mod loading | Build system work |
| [memory-modernization.md](memory-modernization.md) | Phase D-MEM: 6-phase plan, pool audit, magic numbers, stack→heap | Memory system work |
| [server-architecture.md](server-architecture.md) | Dedicated server: protocol interface, CLI, GUI, headless mode | Server work |
| [update-system.md](update-system.md) | D13: versioning, GitHub API, SHA-256, self-replace, save migration | Update system work |

## Architecture Documents (load when working on that system)

| File | System | When to load |
|------|--------|-------------|
| [component-mod-architecture.md](component-mod-architecture.md) | D3R: Component mod system, asset catalog, INI format, network distribution | Any mod system / asset loading work |
| [b12-participant-system.md](b12-participant-system.md) | Dynamic participant pool (replaces chrslots) | Bot/player slot work |

## Plan Files (load when starting that phase)

| File | Phase | When to load |
|------|-------|-------------|
| [d5-settings-plan.md](d5-settings-plan.md) | D5: Audio volumes, graphics, controls, QoL | Starting D5 |
| [master-server-plan.md](master-server-plan.md) | D16: Server registry, heartbeat, server browser | Starting D16 |
| [menu-storyboard.md](menu-storyboard.md) | D4: 113-menu inventory, component library, design tokens | Menu migration reference |
| [rendering-trace.md](rendering-trace.md) | Endscreen rendering pipeline trace, GBI translation | Endscreen/rendering bugs |

## Architecture Decision Records

| File | Decision |
|------|----------|
| [ADR-001-lobby-multiplayer-architecture-audit.md](ADR-001-lobby-multiplayer-architecture-audit.md) | Network protocol audit: strncpy fixes, protocol verification |
| [ADR-002-component-filesystem-decomposition.md](ADR-002-component-filesystem-decomposition.md) | D3R-1: Convert 5 bundled mods to component filesystem + shim loader |
| [ADR-003-asset-catalog-core.md](ADR-003-asset-catalog-core.md) | D3R-2: String-keyed hash table, catalogResolve() API, dynamic growth |

---

## Key Facts

- **Language**: C11 game code, C++ port code. No C++ in `src/game/` or `src/lib/`.
- **Build**: CMake + MSYS2/MinGW on Windows. AI builds via `build-headless.ps1` on dev. Game director tests in-game via playtest dashboard.
- **Net**: Protocol v21, 60Hz tick, NETMODE_NONE/SERVER/CLIENT, unreliable position + reliable state
- **Limits**: MAX_MPCHRS=36, MAX_PLAYERS=4, MAX_BOTS=32 (matchsetup.cpp)
- **Bots**: PROPTYPE_CHR with `chr->aibot != NULL`. Player capsule ~30 units radius.
- **Asset resolution**: Name-based only (S27 constraint). All lookups through Asset Catalog. No numeric ROM addresses or table indices for identity.
- **Mod architecture**: Component-based (S27). Each asset = own folder + `.ini`. See [component-mod-architecture.md](component-mod-architecture.md).
