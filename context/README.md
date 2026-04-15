# Perfect Dark 2 -- Project Context Index

> **Last updated**: 2026-04-14, S255 — Dev Window v2: **Pull** / **Push** buttons (`git pull` / `git push`), DPI awareness + WPF text/layout for font scaling. Context sync (README, session-log, tasks-current, infrastructure, roadmap).
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
| **Long-term vision + release milestones** | v0.1.0 through v1.0.0 + dependency graph | [roadmap.md](roadmap.md) |
| **Open bugs** | One-off issues (open/fixed) | [bugs.md](bugs.md) |
| **Systemic patterns** | Architectural bug classes | [systemic-bugs.md](systemic-bugs.md) |
| **QC test checklist** | In-game verification items per build | [qc-tests.md](qc-tests.md) |

---

## Session History

Recent sessions in [session-log.md](session-log.md). Older session archives in `_archive/sessions/`.

| Sessions | Period | Focus |
|----------|--------|-------|
| S255 | 2026-04-14 | Dev Window v2: utility-row **Pull** / **Push** (`git pull` / `git push`, log + status refresh; disabled during build/release). **Font scaling**: `SetProcessDPIAware` + `UseLayoutRounding` / `SnapsToDevicePixels` / `TextFormattingMode=Display` / `ClearTypeHint` on main window. |
| S254 | 2026-04-14 | Bug B fix: countdown-cancel-on-room-close. `netReadyGateOnClientLeft()` + `netReadyGateAbortForRoom()` in `roomLeave()`; defensive room-missing guard in `readyGateTickCountdown()`. No protocol bump. SP-14 (room-bound server state must be cleaned on teardown) added to systemic-bugs.md; constraint added to constraints.md. Build-verified (pd + pd-server). Needs in-game playtest. v0.0.95 pre-release commit. |
| S248-S253 | 2026-04-13 | MP lobby & mod stabilization drop. S248 mod persistence + room name + countdown + songs F-2.1 (B-135/B-136/B-137/B-138/B-139). S249 B-140 Issue A playlist auto-advance + B-134 spawn validator railing trap. S250 input authority Phase 1 (`gameplayInputSuppressed()` + focus handling). S251 B-141 audio telemetry (drop/underrun/hitch counters). S252 B-143 End-Game-Crash (`manifestClear` in netDisconnect) + B-142 false kills NULL-guard + modal confirm UX. S253 MP lobby residual — Issue 7 SVC_ROOM_SETTINGS, Weapons F-2.1, Issue 2/8 theme rescan, B-140 Issue B two-panel Select Tunes. Parallel: dev-window-v2 font/control polish. Forensic detail in `scratch/archive/2026-04-13/`. |
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
| [networking.md](networking.md) | ENet protocol, message types, resync, damage authority (cheatsheet) | Netcode work |
| [network-architecture.md](network-architecture.md) | **Consolidated** architecture (hub, rooms, connect codes, lobby UX, master server, profiles, federation) | Network design, any lobby/room work |
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

## Plan / Design Files (active)

| File | Phase / scope | When to load |
|------|---------------|-------------|
| [network-architecture.md](network-architecture.md) | Consolidated networking roadmap (replaces the former 5 plan files — multiplayer / master-server / join-flow / lobby-flow / room-architecture, all archived) | MP infrastructure, any room / lobby / join work |
| [designs/d5-full-menu-overhaul.md](designs/d5-full-menu-overhaul.md) | 5 phases, binding UX guidelines | Menu work |
| [designs/d5-ui-polish-plan.md](designs/d5-ui-polish-plan.md) | D5.0-D5.8 sub-phase plan | D5 work |
| [designs/match-startup-pipeline.md](designs/match-startup-pipeline.md) | 8-phase match startup (Gather→Sync) | Match startup |
| [designs/session-catalog-and-modular-api.md](designs/session-catalog-and-modular-api.md) | Session catalog + typed query functions | Asset loading |
| [designs/menu-inventory.md](designs/menu-inventory.md) | 120 screens: status, file path, D5 phase | Menu QC |
| [designs/manifest-architecture.md](designs/manifest-architecture.md) | Manifest inclusion policy, 3 paths, stage coverage | Manifest/asset loading |
| [designs/nat-traversal-architecture.md](designs/nat-traversal-architecture.md) | STUN, hole-punch, relay design | NAT reference |
| [designs/implementation-plan-mods-and-d5.md](designs/implementation-plan-mods-and-d5.md) | P1-P6 dependency graph | Mod/UI roadmap |
| [designs/input-authority-and-menu-pool-2026-04-13.md](designs/input-authority-and-menu-pool-2026-04-13.md) | ADR: input bleed-through + menu-pool discipline. Phase 1 shipped S250; Phase 2 queued. | Input/menu work |
| [designs/spawn-system-architecture-2026-04-13.md](designs/spawn-system-architecture-2026-04-13.md) | L1-L4 spawn pool architecture + capsule-radius invariant | Spawn/MP-load work |
| [designs/hud-layer-order.md](designs/hud-layer-order.md) | HUD render ordering + context-aware gating | HUD work |
| [designs/mod-enablement-policy.md](designs/mod-enablement-policy.md) | Mod loading policy | Mod system work |
| [designs/studio-platform-design.md](designs/studio-platform-design.md) | v0.5.0 Studio feature set | Studio roadmap |
| [designs/visual-scripting-node-taxonomy.md](designs/visual-scripting-node-taxonomy.md) | Future scripting layer | Long-horizon |
| [designs/skin-editor-design.md — archived](_archive/designs/skin-editor-design.md) | (historical — S-1 → S-9 shipped 2026-04-12) | Reference only |
| [designs/audio-mod-menu-design.md — archived](_archive/designs/audio-mod-menu-design.md) | (historical — A-1 → A-7 shipped 2026-04-12) | Reference only |

## Architecture Decision Records

| File | Decision |
|------|----------|
| [ADR-001](ADR-001-lobby-multiplayer-architecture-audit.md) | Network protocol audit: strncpy fixes |
| [ADR-002](ADR-002-component-filesystem-decomposition.md) | D3R-1: Component filesystem layout |
| [ADR-003](ADR-003-asset-catalog-core.md) | D3R-2: String-keyed hash table, catalogResolve() |
| [ADR-004](ADR-004-dev-window.md) | Build tool unification (PowerShell) |

## Archived Content

Completed audits, superseded plans, and old session logs in `_archive/`:

- `_archive/audits/` — completed security / null-guard / compliance audits (catalog-ID-compliance, infrastructure-integrity, legacy-hacks, pipeline-compliance, asset-reference-audit, mod-system-features-and-todos, older init-order / netsend / rendering / player-count audits).
- `_archive/designs/` — superseded design docs (Skin Editor, Audio Mod Menu, scaling baseline, D5 settings plan, menu replacement plan, input flow chart, input repair plan, HUD score panel, state-transition audit, dev-window-v2, menu storyboard / asset audit, menu replacement, plan-bot-crash-fixes, rendering-trace, roadmap-synthesis).
- `_archive/designs/2026-04-13/` — 2026-04-13 stabilization-drop design docs (build-pipeline-improvements, infrastructural-repair-plan, master-orchestration-plan, match-lifecycle audit + fix plan, menu-input audit + fix plan, mod-map-import-pipeline, spawn-and-import-fix-plan, static-link-dll-elimination).
- `_archive/plans/` — closed plan docs (plan-catalog-id-migration, catalog-loading-plan, catalog-activation-plan, multiplayer-plan, master-server-plan, join-flow-plan, lobby-flow-plan, room-architecture-plan — the last five consolidated into [network-architecture.md](network-architecture.md)).
- `_archive/reviews/` — solo-online-parity review (2026-04-13).
- `_archive/builds/` — smoke-verify checklists (2026-04-13).
- `_archive/sessions/` — session logs S1-S119.
- `_archive/roadmap-v1.md` — earlier unified engineering roadmap (S166). Current roadmap is [roadmap.md](roadmap.md).
- `scratch/archive/2026-04-13/` — session-state handoffs from the MP lobby / mod stabilization drop. See the dated README in that directory.
- `scratch/archive/2026-04-11/` and `scratch/archive/2026-04-10/` — earlier dated scratch handoffs.

---

## Key Facts

- **Language**: C11 game code, C++ port code. No C++ in `src/game/` or `src/lib/`.
- **Build**: CMake + MSYS2/MinGW. `devtools/build-headless.ps1` for AI.
- **Net**: Protocol **v35**, 60Hz tick. All wire fields use catalog ID strings. net_hash is dead. match_seed synced via SVC_STAGE_START.
- **Input**: Action map system (M0.2). `actionPressed()`/`actionHeld()`/`actionValue()`. No CK_*.
- **Menus**: ImGui sole system (P10 D5.7). `pdgui_menu_*.cpp`. Legacy rendering removed.
- **Limits**: MAX_MPCHRS=36, MAX_PLAYERS=4, MAX_BOTS=32.
- **Asset resolution**: Name-based only. All lookups through Asset Catalog. No integer identity at boundaries.
