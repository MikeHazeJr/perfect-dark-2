# Modernization Roadmap

> Last updated: 2026-04-17 (daily maintenance audit — S311 follow-up merged; covers S293–S313 marathon wave: Grid level editor, per-agent audio, pd.ini audit, UI polish marathon, modeldef defensive guards, glyph system)

## Current State

**Build**: v0.0.109 | **Protocol**: v36 | **Sessions**: 313+

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
| **B-112 root cause** | M | Chr pointer corruption in 31-bot matches. S234 FIX-A MITIGATED (stack depth cap + generation tokens + hardened handler). Awaiting 31-bot repro test to confirm crash eliminated. |
| **Build verification pass** | S | Clean build, all QC tests passing, no known crash bugs. |
| **D13 — Update System parse diagnosis** | S | **DONE (S245 FIX-F)** — curlGet returns HTTP code; 403 rate-limit path + user message; fsFullPath fallback for empty installDir; 1MB min size check on extracted exe. B-99 closed. |

### Should-Have

| Item | Effort | Detail |
|------|--------|--------|
| **M3 — Online MP flow** | M | Lobby polish, room list UX, leader election, Quick Play. R-3 (room networking) done. L5 match lifecycle (co-op manifest, protocol v35, match_seed) DONE S241. |
| **Prop sync event-driven** | S | Currently CRC polling. Should fire on pickup/door events. |
| **B-78 chat rate limiting** | S | **DONE (2026-04-11)** — CHAT_MSG_MAX_LEN 255 + length check before rate-limit ring. |
| **B-81 JSON recursion guard** | S | **DONE (2026-04-11)** — S_MAX_DEPTH 64 + 256KB file cap. |

### Nice-to-Have

| Item | Effort | Detail |
|------|--------|--------|
| **D5 Phase 5 — Lobby scene** | L | Player portraits, connected player avatars, character preview. |
| **Killfeed bot kills** | S | Bot kills not appearing in killfeed. |
| **B-97 Special Assignments separation** | S | **DONE (S242/S245 FIX-G)** — SeparatorText headers with completion counters for Campaign + SA sections. |

## Release Milestones (detailed)

### v0.1.0 "Foundation"

**Goal**: Stable single-player and local multiplayer with mod support.

**Features**:
- All D3R phases complete (component mod architecture, asset catalog)
- Combat Simulator with custom bot traits (D3R-8)
- Modding hub accessible from main menu (D3R-7)
- Mod pack export/import (D3R-10)
- Legacy mod paths removed (D3R-11)
- Network distribution protocol (D3R-9)
- UI scaling across resolutions
- Death loop and crash fixes
- Bot count limit raised to match UI

**Release criteria**: All QC tests passing, clean build on dev, no known crash bugs.

**Effort**: S — mostly done, needs QC pass and stabilization.

---

### v0.2.0 "Connected"

**Goal**: Multiplayer with friends, stable dedicated server.

**Features**:
- Dedicated server stable and functional
- Player identity system (`pd-identity.dat`)
- Phonetic / sentence IP encoding for easy connection
- Lobby leader system
- Server password protection and bans
- B-12 participant system fully migrated (Phases 2-3, `chrslots` removed)
- Stage decoupling Phases 2-3

**Depends on**: v0.1.0.
**Effort**: L (~4-6 weeks).

---

### v0.3.0 "Community"

**Goal**: Social hub and content sharing.

**Features**:
- Social hub / lounge on dedicated server
- Room system (concurrent independent sessions)
- Public / private mod sharing with versioning
- Direct player-to-player content sharing
- Whitelists (user-ID-based, cross-server)
- Server content library (cached public mods)

**Depends on**: v0.2.0.
**Effort**: L (~4-6 weeks).

---

### v0.4.0 "Federation"

**Goal**: Mesh networking and cross-server play.

**Features**:
- Mesh peer discovery protocol
- Cross-server matchmaking
- Signed transfer tokens
- Server browser via mesh (see [network-architecture.md](network-architecture.md) §7 for master server D16 design)
- Trust levels for mesh peers

**Depends on**: v0.3.0.
**Effort**: XL (~6-8 weeks).

---

### v0.5.0 "Studio"

**Goal**: Full internal mod creation pipeline.

**Features**:
- Model Studio (import/export, texture replacement, skins)
- Audio Tools (export/import)
- Level Tools (export/import)

**Depends on**: v0.1.0 (can run parallel with networking track).
**Effort**: L (~4-6 weeks).

---

### v0.6.0 "Spectacle"

**Goal**: Counter-Op mode, spectator mode, co-op polish.

**Features**:
- Counter-Op mode (D14a)
- Spectator mode (D10)
- Co-op polish (D12)

**Depends on**: v0.3.0.
**Effort**: M.

---

### v1.0.0 "Forge"

**Goal**: Complete creative platform.

**Features**:
- Forge-style level editor (separate main menu entry)
- D5 Settings / Graphics / QoL complete
- Update system (D13) self-updating — shipped as S245 FIX-F; playtest-verify pending
- Persistent stats (D6 full) + Discord Rich Presence (D7)
- Full polish pass (accessibility, collision feel, audio polish)

**Depends on**: v0.4.0 + v0.5.0 converged.
**Effort**: XL (~6-10 weeks).

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
