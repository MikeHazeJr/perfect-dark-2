# Modernization Roadmap

> Last updated: 2026-04-11 (S199 — Updater parse diagnosis; daily context audit)

## Current State

**Build**: v0.0.75 | **Protocol**: v32 | **Sessions**: 199+

The project has crossed the threshold from "port with mods" to "platform with a modern engine shell." The core identity migration is complete, the input system is unified, the legacy menu system is dead, and 47 deep audit bugs have been fixed. What remains is feature completion toward v0.1.0.

## Milestone Summary

| Milestone | Status | Key Result |
|-----------|--------|------------|
| **M0 — Foundation** | **DONE** | M0.1 (catalog ID migration a-f) + M0.2 (action map input system). All asset identity is catalog-native. |
| **M1 — Campaign** | **DONE** | M1.1 (mission select redesign), M1.2 (solo flow + pause), M1.3 (options sub-menus) |
| **M2 — Combat Sim** | **DONE** | M2.1 (UI catalog audit), M2.2 (match flow + endscreen + auto-save) |
| **P1–P10 — Menu Overhaul** | **DONE** | Input context stack, controller nav, 59/120 screens ported, OG menu rendering removed (P10 D5.7). ImGui is sole menu system. |
| **Deep Audit** | **DONE** | 5 critical security + 7 high + 10 medium + 9 low bug fixes. Path traversal, SHA-256 verification, overflow guards, unaligned reads, shutdown ordering. |
| **Infrastructure** | **DONE** | D1 (N64 strip), D3R (component mods 1-11), D8 (NAT traversal), D9 (dedicated server), MSP (match startup pipeline A-F), catalog universality A-G |

## What Remains for v0.1.0 "Foundation"

v0.1.0 target: **Stable single-player + local multiplayer + mod support + online play basics.**

### Must-Have

| Item | Effort | Detail |
|------|--------|--------|
| **D5 Phase 3 — Remaining menu screens** | L | 61/120 screens still need ImGui ports. Many are stubs/simple dialogs. |
| **D5 Phase 4 — Theme System** | M | Auto-extract base-ui textures at runtime. Mod themes. Debug menu rebuild. |
| **B-112 root cause** | M | Chr pointer corruption in 31-bot matches. Guards in place but root cause unknown. |
| **Build verification pass** | S | Clean build, all QC tests passing, no known crash bugs. |
| **D13 — Update System parse diagnosis** | S | IN PROGRESS (S199) — instrumentation deployed; awaiting next failed-check log to confirm GitHub HTTP code (likely 403 rate-limit). |

### Should-Have

| Item | Effort | Detail |
|------|--------|--------|
| **M3 — Online MP flow** | M | Lobby polish, room list UX, leader election, Quick Play. R-3 (room networking) done. |
| **Prop sync event-driven** | S | Currently CRC polling. Should fire on pickup/door events. |
| **B-78 chat rate limiting** | S | DoS amplification vector. |
| **B-81 JSON recursion guard** | S | Crafted save file → stack overflow crash. |

### Nice-to-Have

| Item | Effort | Detail |
|------|--------|--------|
| **D5 Phase 5 — Lobby scene** | L | Player portraits, connected player avatars, character preview. |
| **Killfeed bot kills** | S | Bot kills not appearing in killfeed. |
| **B-97 Special Assignments separation** | S | Mixed into main mission list. |

## Post v0.1.0 Roadmap

| Version | Codename | Goal |
|---------|----------|------|
| **v0.2.0** | "Connected" | Stable dedicated server, B-12 Phase 3 (remove chrslots), identity system, server passwords/bans |
| **v0.3.0** | "Community" | Social hub, room system polish, mod sharing, whitelists |
| **v0.4.0** | "Federation" | Mesh peer discovery, cross-server matchmaking, server browser |
| **v0.5.0** | "Studio" | Model Studio, Audio Tools, Level Tools (Forge) |
| **v0.6.0** | "Spectacle" | Counter-Op mode, spectator mode, co-op polish |
| **v1.0.0** | "Release" | Stats, Discord RP, accessibility, collision feel, audio polish |

## Dependency Graph

```
DONE ─── M0 (Catalog + Input) ─── M1 (Campaign) ─── M2 (Combat Sim)
  │
  ├── P1-P10 (Menu Overhaul) ─── DONE
  │     └── D5 Phase 3-5 (remaining screens, themes, lobby scene) ─── IN PROGRESS
  │
  ├── D13 (Update System) ─── code written, build test needed
  │
  ├── Deep Audit ─── DONE (47 bugs fixed)
  │
  └── v0.1.0 release ─── next
        │
        ├── M3 (Online MP) → v0.2.0
        ├── D14a (Counter-Op) → v0.6.0
        ├── D15 (Forge) → v0.5.0
        └── D16 (Master Server) → v0.4.0
```

## Completed Infrastructure Phases

| Phase | Name | Sessions |
|-------|------|----------|
| D1 | N64 Strip (672 guards, 114+ files) | S1 |
| D2a | Character Select Redesign | S15 |
| D3R-1..11 | Component Mod Architecture (full) | S27-S80 |
| D4 | Menu Migration (superseded by ImGui hotswap) | S22 |
| D8 | NAT Traversal (STUN + hole punch) | S83 |
| D9 | Dedicated Server | S47d |
| MSP | Match Startup Pipeline (Phases A-F) | S84-S90 |
| SA-1..7 | Session Catalog + Modular API | S91-S97 |
| D-STAGE | Stage Decoupling (all 3 phases) | S47c |
| Catalog Universality | Phases A-G, wire protocol v27-v32 | S119-S154 |
| M0.1a-f | Catalog ID Migration (all asset types) | S167-S180 |
| M0.2 | Input System Unification (action maps) | S181-S183 |
| P10 D5.7 | OG Menu Removal (ImGui sole system) | S184 |
| Deep Audit | 5C + 7H + 10M + 9L bug fixes | S185 |
