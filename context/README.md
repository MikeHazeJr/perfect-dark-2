# Perfect Dark Mike — Project Context Index

> **Last updated**: 2026-03-30, Session S83 (NAT traversal D8 DONE, protocol v23, connect code port encoding, mouse capture fix, Solo Room screen, B-54 online MP crash fixed, spawn weapon logging)
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
| S83 | 2026-03-30 | NAT traversal D8 DONE (STUN+hole-punch+relay), protocol v23, connect code port encoding, mouse capture fix, B-54 online MP crash fixed, spawn weapon logging | [session-log.md](session-log.md) |
| S82 | 2026-03-30 | Solo Play → Room screen routing (Combat Simulator button → Room screen in offline mode) | [session-log.md](session-log.md) |
| 81 | 2026-03-30 | B-49 VERIFIED FIXED (footstepChooseSound loop guard); weapon spawn fix + protocol v22; server build guards; B-50 hub timer; J-1 join flow verified; 6 branches merged | [session-log.md](session-log.md) |
| 80 | 2026-03-29 | Full TODO sweep (60+ items), enet ABA fix, server history UI, n_resample divide-by-zero, gfx_destroy, modding pipeline design doc | [session-log.md](session-log.md) |
| 72–79 | 2026-03-29 | Asset reference audit, B-44/B-26 bot names, B-39/B-40/B-41/B-42/B-43 fixes, Combat Sim scenario save, B-46/B-47 fixes, C-7 file SFX | [session-log.md](session-log.md) |
| 71 | 2026-03-28 | Combat Sim scenario save/load: scenario_save.c/h (new), matchsetup.c deduplication, room UI Save/Load buttons + JSON format | [session-log.md](session-log.md) |
| 70 | 2026-03-28 | B-43 first-tick NULL guards + scenarioTick/botTick first-tick safety | [session-log.md](session-log.md) |
| 69 | 2026-03-28 | Player count constants audit: 5 wrong values fixed (NET_MAX_CLIENTS, MAX_BOTS_PM struct layout, HUB_MAX_CLIENTS, SAVE_MAX_BOTS), audit doc created | [session-log.md](session-log.md) |
| 62 | 2026-03-27 | Definitive network-system-audit.md: full protocol catalog, lifecycle trace, multi-room impossibility, security, recommendations | [session-log.md](session-log.md) |
| 61 | 2026-03-27 | netSend audit + 3 critical netcode fixes: CLC_RESYNC_REQ dropped, g_Lobby.inGame, NPC broadcast guard | [session-log.md](session-log.md) |
| 60 | 2026-03-27 | Five playtest fixes: Leave Room, Start Match (netSend bug), bot modal labels, score slider, lobby player count | [session-log.md](session-log.md) |
| 59 | 2026-03-27 | Match Start root cause (SVC_STAGE_START async g_MainChangeToStageNum), UX audit | [session-log.md](session-log.md) |
| 58 | 2026-03-27 | Build pipeline architecture, S57 worktree merge (pdgui_lobby.cpp), §3 CRITICAL-PROCEDURES | [session-log.md](session-log.md) |
| 57 | 2026-03-27 | Room interior UX -- pdgui_menu_room.cpp (3-tab match setup), lobby-flow-plan.md created | [session-log.md](session-log.md) |
| 52–56 | 2026-03-27 | R-1 Foundation, hub/lobby fixes B-28–35, network audit, room architecture plan | [session-log.md](session-log.md) |
| 50–51 | 2026-03-26/27 | S50: B-27 (9-fix server crash), build system hardening, v0.0.7 release; S51: room architecture plan audit | [session-log.md](session-log.md) |
| 49 | 2026-03-26 | Join flow audit, SPF-3 lobby+join, connect codes, architecture capture | [session-log.md](session-log.md) |
| 47a-48 | 2026-03-24-25 | MEM-1, B-12 P2, stage decoupling, SPF-1, mesh collision, dev window, cleanup | [session-log.md](session-log.md) |
| 22-46 | 2026-03-22-24 | D3R component mod architecture, asset catalog, participant system, bot customizer, network distribution | [sessions-22-46.md](sessions-22-46.md) |
| 14-21 | 2026-03-21-22 | Combat stabilization, memory modernization, menu Phase 2 | [sessions-14-21.md](sessions-14-21.md) |
| 7-13 | 2026-03-18-21 | Networking phases, model loading, dedicated server | [sessions-07-13.md](sessions-07-13.md) |
| 1-6 | 2026-03-01-18 | N64 strip, mod manager, ImGui foundation, char select | [sessions-01-06.md](sessions-01-06.md) |

---

## Domain Files (load when working on that system)

| File | System | When to load |
|------|--------|-------------|
| [init-order-audit.md](init-order-audit.md) | **S65 Audit 3**: Full networked stage load sequence — Phase 0/1/2 with dependency graph, N64 vs PC differences, crash analysis, recommendations | Stage load crash debugging, init ordering questions |
| [null-guard-audit-players.md](null-guard-audit-players.md) | **S64 Audit 1/4**: PLAYERCOUNT() sparse-slot null-guard audit — 2 CRITICAL + 5 HIGH fixed | Any crash related to player/chr access during stage load |
| [null-guard-audit-props.md](null-guard-audit-props.md) | **S65 Audit 2/4**: prop->chr, g_Rooms[] OOB — 7 CRITICAL/HIGH fixed (propobj.c, explosions.c, smoke.c) | CCTV/laser fence/explosion crashes |
| [null-guard-audit-bots.md](null-guard-audit-bots.md) | **S66 Audit 4/4**: 28 CRITICAL/HIGH bot/AI crashes on dedicated server — currentplayer NULL, players[-1], chrGetTargetProp()->chr, g_MpAllChrPtrs bounds | Any bot/simulant crash on dedicated server |
| [player-count-constants-audit.md](player-count-constants-audit.md) | **S69 Audit**: Full catalog of MAX_PLAYERS/MAX_BOTS/NET_MAX_CLIENTS/MATCH_MAX_SLOTS hierarchy — 5 wrong values fixed, deferred items, guidelines for future code | Any work touching player counts, match slots, or bot limits |
| [collision.md](collision.md) | Capsule sweep, floor/ceiling, legacy cdTestVolume, geometry types | Collision/physics work |
| [movement.md](movement.md) | Jump physics, vertical movement, ground detection, airborne logic | Movement/jump work |
| [networking.md](networking.md) | ENet protocol, message types, resync, damage authority (phases 1–10, C1–C12) | Netcode work |
| [network-system-audit.md](network-system-audit.md) | **Definitive** networking audit (S62): full protocol catalog (39 SVC + 10 CLC), connection lifecycle, tick model, lobby/bot/room sync, multi-room impossibility finding, mod distribution, performance, security, prioritized recommendations. Supersedes network-audit.md + netsend-audit.md. | Netcode debugging, planning protocol work, architecture decisions |
| [network-audit.md](network-audit.md) | ~~Superseded by network-system-audit.md~~ (S57 deep audit — kept for historical reference) | — |
| [netsend-audit.md](netsend-audit.md) | ~~Superseded by network-system-audit.md~~ (S61 send-site audit — kept for historical reference) | — |
| [menu-asset-audit.md](menu-asset-audit.md) | **S62 deep audit**: menu architecture, hotswap registry (22 entries), controller support per screen, asset loading gateway status (C-4 through C-7 PENDING), 8 bugs identified | Menu/UI work or asset loading work |
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
| [multiplayer-plan.md](multiplayer-plan.md) | SPF-2+: Server-as-hub, rooms, federation, profiles, phonetic, menus | Starting any multiplayer infrastructure work |
| [lobby-flow-plan.md](lobby-flow-plan.md) | Room interior UX: tab layout, Combat Sim/Campaign/Counter-Op settings, network protocol integration | Room interior / match setup UI work |
| [join-flow-plan.md](join-flow-plan.md) | Server/client join flow: connect codes → ENet → lobby → match. Audit + gap plan | Any join/connect/lobby work |
| [room-architecture-plan.md](room-architecture-plan.md) | R-1–R-5: Demand-driven rooms, leader/room_id, protocol messages, GUI redesign | Any room system / hub work |
| [catalog-loading-plan.md](catalog-loading-plan.md) | Catalog design overview (S48, preserved) | Background reading on catalog design |
| [plans/catalog-activation-plan.md](plans/catalog-activation-plan.md) | **C-0 through C-9**: Full implementation blueprint — dependency graph, per-phase specs, risk register, mod integration points | Any asset loading / catalog activation work |
| [menu-replacement-plan.md](menu-replacement-plan.md) | Full ImGui replacement of all 240 legacy menus | Any menu migration work |
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
- **Net**: Protocol v21, 60Hz tick, NETMODE_NONE/SERVER/CLIENT, unreliable position + reliable state. Joining: 4-word sentence codes only (no raw IP). B-12 Phase 3 → v22. R-3 room sync → v22 or v23 (coordinate with B-12 P3).
- **Limits**: MAX_MPCHRS=36, MAX_PLAYERS=4, MAX_BOTS=32 (matchsetup.cpp)
- **Bots**: PROPTYPE_CHR with `chr->aibot != NULL`. Player capsule ~30 units radius.
- **Asset resolution**: Name-based only (S27 constraint). All lookups through Asset Catalog. No numeric ROM addresses or table indices for identity.
- **Mod architecture**: Component-based (S27). Each asset = own folder + `.ini`. See [component-mod-architecture.md](component-mod-architecture.md).
