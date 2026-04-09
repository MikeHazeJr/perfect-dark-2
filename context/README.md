# Perfect Dark 2 -- Project Context Index

> **Last updated**: 2026-04-09, Session S186 (Context system overhaul. Deep audit 47 bugs fixed. M0-M2 DONE, P1-P10 DONE. Preparing for v0.1.0.)
> This file is the master hub. Read it first every session. Everything links from here.

## Onboarding

> **COLD START?** Read **[QUICKSTART.md](QUICKSTART.md)** first. Then come back here for deep dives.

**Project**: PC port of Perfect Dark (N64 FPS, Rare 2000). C11 codebase, CMake + MinGW/GCC.
**Developer**: Mike (sole dev, builds on Windows). AI writes code, Mike compiles and tests.
**Session start**: Read [QUICKSTART.md](QUICKSTART.md) -> [constraints.md](constraints.md) -> [session-log.md](session-log.md) (last 3) -> [tasks-current.md](tasks-current.md).

---

## Quick Status

| Area | Status | File |
|------|--------|------|
| **What to do next** | v0.1.0 release prep | [tasks-current.md](tasks-current.md) |
| **What's done** | M0-M2, P1-P10, deep audit (47 bugs) | [roadmap.md](roadmap.md) |
| **What we must respect** | Active/removed constraints | [constraints.md](constraints.md) |
| **Infrastructure phases** | D1-D16 execution status | [infrastructure.md](infrastructure.md) |
| **Long-term vision** | Milestone targets + dependency graph | [roadmap.md](roadmap.md) |
| **Release milestones** | v0.1.0 through v1.0.0 | [milestones.md](milestones.md) |
| **Open bugs** | One-off issues (open/fixed) | [bugs.md](bugs.md) |
| **Systemic patterns** | Architectural bug classes | [systemic-bugs.md](systemic-bugs.md) |
| **QC test checklist** | In-game verification items per build | [qc-tests.md](qc-tests.md) |

---

## Session History

Recent sessions in [session-log.md](session-log.md). Older session archives in `_archive/sessions/`.

| Sessions | Period | Focus |
|----------|--------|-------|
| S185-S186 | 2026-04-09 | Deep audit (47 bug fixes: 5C+7H+10M+9L), context system overhaul |
| S184 | 2026-04-08 | P10 D5.7: OG Menu Removal -- ImGui sole menu system |
| S181-S183 | 2026-04-07/08 | M0.2 Input System Unification (action maps, CK_* deleted, -823 lines) |
| S167-S180 | 2026-04-06/07 | M0.1a-f Catalog ID Migration, M1.1-M1.3, M2.1-M2.2, input bug fixes |
| S155-S166 | 2026-04-06 | Catalog ID Migration Phases 0-7, D5.0 visual layer, B-119/B-120 fixes |
| S140-S154 | 2026-04-04/06 | Lobby Unification U-1 to U-10, spawn stability, R-3 room networking |
| S119-S139 | 2026-04-02/04 | Catalog Universality A-G, bug audit, systemic sweeps, D5.0-D5.5 |

---

## Domain Files (load when working on that system)

| File | System | When to load |
|------|--------|-------------|
| [collision.md](collision.md) | Capsule sweep, floor/ceiling, geometry types | Collision/physics work |
| [movement.md](movement.md) | Jump physics, ground detection, airborne logic | Movement/jump work |
| [networking.md](networking.md) | ENet protocol, message types, resync, damage authority | Netcode work |
| [network-system-audit.md](network-system-audit.md) | **Definitive** networking audit: 39 SVC + 10 CLC, lifecycle, tick model | Netcode debugging |
| [imgui.md](imgui.md) | ImGui integration, PD-authentic styling, shimmer, palette | Menu/UI work |
| [build.md](build.md) | CMake, MSYS2/MinGW, build tool GUI, static linking | Build system work |
| [server-architecture.md](server-architecture.md) | Dedicated server: protocol, CLI, GUI, headless | Server work |
| [update-system.md](update-system.md) | D13: versioning, GitHub API, SHA-256, save migration | Update system work |
| [memory-modernization.md](memory-modernization.md) | Phase D-MEM: pool audit, stack->heap | Memory system work |

## Architecture Documents

| File | System | When to load |
|------|--------|-------------|
| [component-mod-architecture.md](component-mod-architecture.md) | D3R: Component mod system, asset catalog, INI format | Mod system work |
| [b12-participant-system.md](b12-participant-system.md) | Dynamic participant pool (replaces chrslots) | Bot/player slot work |
| [CRITICAL-PROCEDURES.md](CRITICAL-PROCEDURES.md) | Context management rules, build verification | Reference |

## Plan Files

| File | Phase | When to load |
|------|-------|-------------|
| [multiplayer-plan.md](multiplayer-plan.md) | Server-as-hub, rooms, federation, profiles | MP infrastructure |
| [lobby-flow-plan.md](lobby-flow-plan.md) | Room interior UX, tab layout, protocol integration | Room/match setup |
| [join-flow-plan.md](join-flow-plan.md) | Connect codes -> ENet -> lobby -> match | Join/connect work |
| [room-architecture-plan.md](room-architecture-plan.md) | R-1 to R-5: demand-driven rooms, leader/room_id | Room system |
| [master-server-plan.md](master-server-plan.md) | D16: Server registry, heartbeat, browser | Master server |
| [catalog-loading-plan.md](catalog-loading-plan.md) | Catalog architecture overview | Background reading |
| [plan-catalog-id-migration.md](plan-catalog-id-migration.md) | Game Director binding decision (D-1 FULL) | Catalog migration |
| [designs/d5-full-menu-overhaul.md](designs/d5-full-menu-overhaul.md) | 5 phases, binding UX guidelines | Menu work |
| [designs/d5-ui-polish-plan.md](designs/d5-ui-polish-plan.md) | D5.0-D5.8 sub-phase plan | D5 work |
| [designs/match-startup-pipeline.md](designs/match-startup-pipeline.md) | 8-phase match startup (Gather->Sync) | Match startup |
| [designs/session-catalog-and-modular-api.md](designs/session-catalog-and-modular-api.md) | Session catalog + typed query functions | Asset loading |
| [designs/menu-inventory.md](designs/menu-inventory.md) | 120 screens: status, file path, D5 phase | Menu QC |
| [designs/nat-traversal-architecture.md](designs/nat-traversal-architecture.md) | STUN, hole-punch, relay design | NAT reference |
| [designs/implementation-plan-mods-and-d5.md](designs/implementation-plan-mods-and-d5.md) | P1-P6 dependency graph | Mod/UI roadmap |
| [plans/catalog-activation-plan.md](plans/catalog-activation-plan.md) | C-0 to C-9 blueprint | Asset loading |

## Architecture Decision Records

| File | Decision |
|------|----------|
| [ADR-001](ADR-001-lobby-multiplayer-architecture-audit.md) | Network protocol audit: strncpy fixes |
| [ADR-002](ADR-002-component-filesystem-decomposition.md) | D3R-1: Component filesystem layout |
| [ADR-003](ADR-003-asset-catalog-core.md) | D3R-2: String-keyed hash table, catalogResolve() |
| [ADR-004](ADR-004-dev-window.md) | Build tool unification (PowerShell) |

## Archived Content

Completed audits, superseded plans, and old session logs are in `_archive/`. Subdirectories:
- `_archive/audits/` -- Completed security/null-guard/compliance audits
- `_archive/designs/` -- Superseded design documents
- `_archive/sessions/` -- Session logs S1-S119

---

## Key Facts

- **Language**: C11 game code, C++ port code. No C++ in `src/game/` or `src/lib/`.
- **Build**: CMake + MSYS2/MinGW. `devtools/build-headless.ps1` for AI.
- **Net**: Protocol **v32**, 60Hz tick. All wire fields use catalog ID strings. net_hash is dead.
- **Input**: Action map system (M0.2). `actionPressed()`/`actionHeld()`/`actionValue()`. No CK_*.
- **Menus**: ImGui sole system (P10 D5.7). `pdgui_menu_*.cpp`. Legacy rendering removed.
- **Limits**: MAX_MPCHRS=36, MAX_PLAYERS=4, MAX_BOTS=32.
- **Asset resolution**: Name-based only. All lookups through Asset Catalog. No integer identity at boundaries.
