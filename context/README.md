# Perfect Dark 2 -- Project Context Index

> **Last updated**: 2026-04-13, S247 — Build-env self-heal (_build-env-prelude.ps1 + build-env.sh + CLAUDE.md). Prior: S246 gap closure (M-7.x smoke test, match_seed/B-19 DONE), S245 FIX-F/FIX-G (B-99/B-97 closed).
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
| S245-S247 | 2026-04-13 | S245 L7 FIX-F updater robustness (B-99) + FIX-G mission headers (B-97). S246 gap-closure (M-7.x smoke test + tasks refresh; match_seed/B-19 DONE). S247 build-env self-heal (prelude.ps1 + build-env.sh + CLAUDE.md). |
| S238-S244 | 2026-04-13 | S238 L2 universal spawn pool (L1-L4 fallback chain) + F-1.1/F-1.2/F-2.1 menu consistency. S239 spawn tracker. S240 L3 mod map import pipeline. S241/S242 L5 match lifecycle (co-op manifest, protocol v35, match_seed) + L6 rendering polish FIX-C.1/C.3 + menu polish tail. S243 FIX-C rendering tracker. S244 M-6 import UI. |
| S231-S237 | 2026-04-13 | S231 L0-BUILD ccache sloppiness fix + L0-LINK verify. S232 dev-window-v2 overhaul + release hang fix. S233 L0 manifest safety (L1-1, FIX-B.2) + B-72/B-21 closed. S234 FIX-A chr tick isolation + crash handler hardening (B-112/B-126 mitigated). S236 L1 networking safety baseline + menu consistency. |
| S221-S230 | 2026-04-12/13 | S221 9-issue playtest fix batch. S222 audio mod fixes. S223 S224 static-link DLL elimination + Ninja/ccache/PCH pipeline. S226-S229 architecture audits (match lifecycle, menu/input, spawn+import). S230 master orchestration plan (75 items, 8 layers). |
| S202-S220 | 2026-04-11/12 | Batch 4-11 MP menu ports (+920KB ImGui menus). S208 Opus 1M playtest (6 bugs). S218 controller bindings. S220 B-133 charpreview GBI crash fix. |
| S200-S201 | 2026-04-11 | S200 B-78/B-84 FIXED (chat size cap + dead tmp[1024]). S201 D5 P3 Batch 3 DONE: Sound Mode dropdown added to Settings → Audio; CI Options redirects verified complete. pdgui_menu_mainmenu.cpp 3100→3123. |
| S199 | 2026-04-11 | Updater parse failure diagnosis (B-99/D13). per_page 30→100; HTTP code + raw response instrumentation in updater.c. Root cause likely GitHub rate-limit 403 error object. |
| S196-S198 | 2026-04-10 | S196 Theme system: base-game template mod, nineslice pipeline, Settings → Video → UI Chrome Style toggle. S197a Input regressions post-S196 fixed. S198 B-129 agent save path FULLY FIXED (saveInit() wired in main.c + server_main.c); theme editor close lifecycle instrumentation (B-130). New bugs B-130/B-131 OPEN. |
| S191-S195 | 2026-04-10 | S191 B-112/B-126 instrumentation (chr index tracker, SIGABRT handler, NET.WATCHDOG dump). S192 D5 P3 Batch 0 (pdgui_layout primitive). S193 1080p baseline flip + Batch 1. S194 Batch 2 (Co-op/Counter-Op flow). S195 Batch 3 redirect plumbing (CI Options → unified Settings). |
| S189-S190 | 2026-04-10 | Input system complete: P0-only binding rework, usemask/B-door fix, unk14/canlookahead/FarSight/LSTICK fixes. B-128 sky tearing FIXED (sky.c:1244). B-129 mission-end crash partial fix (endscreen.c save path + filemgr noop dialogs); save path fully fixed in S198. SP-9 safeguard IMPLEMENTED in build-headless.ps1. |
| S187-S188 | 2026-04-09 | Three-bug debug (B-127 WASD, B-125 weapons, B-126 silent crash); full menu replacement plan (254 dialogs, 11 batches) |
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
| [designs/manifest-architecture.md](designs/manifest-architecture.md) | Manifest inclusion policy, 3 paths, stage coverage | Manifest/asset loading |
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
- **Net**: Protocol **v35**, 60Hz tick. All wire fields use catalog ID strings. net_hash is dead. match_seed synced via SVC_STAGE_START.
- **Input**: Action map system (M0.2). `actionPressed()`/`actionHeld()`/`actionValue()`. No CK_*.
- **Menus**: ImGui sole system (P10 D5.7). `pdgui_menu_*.cpp`. Legacy rendering removed.
- **Limits**: MAX_MPCHRS=36, MAX_PLAYERS=4, MAX_BOTS=32.
- **Asset resolution**: Name-based only. All lookups through Asset Catalog. No integer identity at boundaries.
