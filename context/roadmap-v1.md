# Perfect Dark 2 — Unified Engineering Roadmap to v1.0.0

> **Created**: 2026-04-06, Session S166
> **Owner**: Mike Hays (Game Director) + AI Engineering Partner
> **Status**: ACTIVE — this is the master plan. All work derives from here.
> Back to [index](README.md)

---

## Philosophy

This roadmap sequences every system toward a real 1.0.0 release. Each milestone has a clear gate — a set of conditions that must be true before the next milestone begins. The ordering is driven by dependencies: foundational systems first, user-facing features built on top, polish and tooling last.

The catalog is the spine. Everything — menus, mods, networking, the level editor — depends on catalog ID strings being the sole identity everywhere. That migration must complete before anything built on top of it can be considered stable.

---

## Milestone Overview

| # | Milestone | Gate | Est. Sessions |
|---|-----------|------|---------------|
| **M0** | Foundation Lock | Catalog native, input unified, build tooling solid | ~12 |
| **M1** | Playable Campaign | Solo missions start-to-finish, menus complete, no showstoppers | ~15 |
| **M2** | Combat Sim Complete | Full MP with bots, all arenas, endscreen flow, stats | ~10 |
| **M3** | Online Multiplayer | Lobby, rooms, NAT punch, spectator, dedicated server polish | ~10 |
| **M4** | Mod Platform | Mod browser, creation tools, theme system, distribution | ~12 |
| **M5** | Forge (Level Editor) | In-client map creation, testing loop, sharing via mods | ~15 |
| **M6** | Polish & Release | Collision/physics feel, audio, accessibility, QA, v1.0.0 | ~10 |

**Total estimate**: ~84 sessions to v1.0.0

---

## M0: Foundation Lock

**Goal**: Every foundational system is complete and stable. No more "we deferred this" surprises. The engine is catalog-native, input is unified, tools work reliably.

### M0.1 — Catalog Signature Migration (CRITICAL PATH)

Complete the migration from integer asset IDs to catalog ID strings at all public function boundaries. This is the single most impactful architectural change remaining — it eliminates the entire class of index-mismatch bugs (B-119, B-120, and every future variant).

| Task | Detail | Files | Status |
|------|--------|-------|--------|
| **Stage functions (M0.1a)** | `g_SoloStages[]` catalog-native, endscreen `missionSetStageByCatalog()`, bg.c/mplayer.c/ingame.c/bodyreset.c all converted. stagenum only at `mainChangeToStage()` handoff. Commit `270d57c`. | mainmenu.c, endscreen.c, bg.c, mplayer.c, ingame.c, bodyreset.c, types.h | **DONE S167** |
| **Body/Head functions (M0.1b)** | All 6 conversion wrappers eliminated/internalized. Public API string-only (`catalogValidateBodyId/HeadId`). ~30 enumeration helpers remain (display, not identity). Commit `0184b80`. | assetcatalog.c, assetcatalog.h | **DONE S169** |
| **Weapon functions (M0.1c)** | `weapon_ids[6][64]` + `spawn_weapon_id[64]` in matchconfig (PRIMARY). Dynamic spawn weapon list from catalog. Save/load, CLC_LOBBY_START updated. Wire protocol unchanged (already v31). Commit `76e0b00`. | matchsetup.h/c, room.cpp, netmsg.c, scenario_save.c | **DONE S171** |
| **Remaining asset types (M0.1d)** | Audited all 7: only **gamemode** needed migration (9 files, protocol v32). `scenario_id[64]` PRIMARY in matchconfig. Texture/audio/animation/lang/prop/HUD are internal-only. Commit `8a0a64b`. | matchsetup.h/c, room.cpp, netmsg.c, scenario_save.c, pdgui_menu_combatsim.cpp, pdgui_menu_room.cpp, savefile.c, scenarios.c | **DONE S173** |
| **Catalog as data provider** | Catalog serves weapon stats, body properties, head metadata directly. ROM arrays become internal implementation detail. | assetcatalog.c | NOT STARTED |

**Gate**: Zero integer asset IDs cross any public function boundary. Zero conversion wrapper calls remain. `catalogIdByRuntime()` is the only bridge and lives inside catalog internals.

### M0.2 — Input System Unification

Replace the current layered input system (CK_* keycodes + ImGui gamepad nav + SDL events + input context stack) with a single Source of Truth.

| Task | Detail | Status |
|------|--------|--------|
| **Input SSOT spec** | Tap/hold/double-tap recognition, per-context action maps, fully rebindable. Design doc committed S163. | SPEC DONE |
| **Action map implementation** | Replace CK_* + hardcoded SDL checks with declarative action maps per input context. | NOT STARTED |
| **Rebinding system** | UI for rebinding keyboard/mouse/controller. Save to pd.ini. | NOT STARTED |
| **Fix B-124 (Esc race)** | `push_tick` + `inputCtxShouldSuppressKey()` — systemic key suppression in input context framework. | **DONE S170 (M1.2)** |
| **Fix B-122 (endscreen mouse)** | `inputCtxSyncMouseMode()` per-frame enforcement + `pdguiIsActive()` deferred flush guard. Manual SDL calls stripped from endscreen. | **DONE S170 (M1.2)** |

**Gate**: One input system. Every key/button/axis maps through action maps. Rebinding works. No hardcoded key checks outside the input layer.

### M0.3 — Build Tooling

| Task | Detail | Status |
|------|--------|--------|
| **_DevWindow replacement** | Smart builds (incremental default, clean on demand), PATH fix, visual redesign. | CODE SESSION ACTIVE |
| **build-headless.ps1 sync** | Keep cmake flags in sync with _dev-window.ps1. | ONGOING |
| **CI pipeline** | GitHub Actions: build on push, run headless build, report errors. | NOT STARTED |

**Gate**: Builds are fast (incremental), reliable (no CMake cache bugs), and automated (CI catches regressions).

---

## M1: Playable Campaign

**Goal**: A player can pick up PD2, select any solo mission, play it start to finish, see objectives, complete or fail, advance to the next mission. The full campaign flow works.

**Depends on**: M0.1 (catalog stages), M0.2 (input fixes for B-122, B-124)

### M1.1 — Campaign Mission UI (D5.2)

| Task | Detail | Status |
|------|--------|--------|
| **Mission select redesign** | Two-panel layout: mission list (left, 38%) + detail (right, 62%). Unlock filter (B-90), chapter headings, blip dots. Commit `15f726b`. | **DONE S168** |
| **Difficulty flow** | Inline difficulty picker with color badges + best times. Single-screen flow replaces 3-dialog chain (B-96). | **DONE S168** |
| **Objectives display** | `soloLoadBriefingForStageId()` loads from game data (B-91). Filtered by difficulty. Completion icons. | **DONE S168** |
| **Special assignments separation** | Challenges and special assignments in their own section (B-97). | PLANNED |

### M1.2 — Solo Mission Flow

| Task | Detail | Status |
|------|--------|--------|
| **Fix B-123 (next mission)** | `missionSetStageByCatalog()` resolves from catalog. Last mission shows Main Menu. | **DONE S167 (M0.1a) + verified S170 (M1.2)** |
| **Mission complete screen** | Objectives review, time, completion stats. Advance to next or replay. | PLANNED |
| **Mission failed screen** | Retry, abort, objectives status. Fix B-122 (mouse). | PARTIAL |
| **Briefing screens** | Pre-mission and post-mission briefing with PD-authentic layout. | PLANNED |
| **Inventory during mission** | Pause → Inventory shows collected weapons/items. | PLANNED (D5.3) |

### M1.3 — Pause Menu Polish (D5.3)

| Task | Detail | Status |
|------|--------|--------|
| **Pause menu** | Resume, Restart, Inventory, Options, Abort — all working. | DONE S164 |
| **Options sub-menu** | Audio/video/controls accessible from pause. Tabbed ImGui panel (3 tabs). | **DONE S174** |
| **Objective checklist** | Difficulty-filtered, completion icons. | DONE S164 |

**Gate**: Full campaign playable Agent through Perfect Agent. Mission select, briefings, gameplay, pause, complete/fail, progression — all working with ImGui menus, catalog-native.

---

## M2: Combat Sim Complete

**Goal**: Local multiplayer with bots is fully featured. All arenas, game modes, weapon sets, bot configuration, endscreen flow, persistent stats.

**Depends on**: M0.1 (catalog weapons, bodies), M1.2 (endscreen flow patterns)

### M2.1 — Combat Sim UI (D5.5)

| Task | Detail | Status |
|------|--------|--------|
| **Arena selection** | All arenas browsable with catalog metadata. Preview images need base-ui textures (Phase 4). | **DONE S176** (browsing verified catalog-native) |
| **Weapon set configuration** | Select/customize weapon loadouts per arena. Catalog-native (M0.1c). | **DONE S172** |
| **Bot configuration** | Head/body picker (fixed S138), trait sliders (S153), name dictionaries (S144). | **DONE** |
| **Game mode selection** | All 6 base game modes selectable. `scenario_id` PRIMARY set via catalog (M0.1d). | **DONE S176** (verified all 6 modes) |

### M2.2 — Match Flow

| Task | Detail | Status |
|------|--------|--------|
| **Match start → gameplay** | Stage load, spawn, weapon distribution. | WORKING |
| **Scoreboard** | Accuracy, team sort, dual exit buttons. | DONE S139 |
| **Endscreen flow** | Local player stats (kills, accuracy bar, shot breakdown), auto-save on exit. Commit `555f50f`. | **DONE S175** |
| **Fix B-115 (post-game mouse)** | Legacy Save Player + Confirm Name dialogs suppressed — auto-save handles PC saving. | **DONE S176** |
| **Fix B-117 (crash on match exit)** | Root cause: stale `g_CtxImGuiMenu` context survived stage transition. Fixed with `inputCtxPopDeferred` guard. Commit `555f50f`. | **DONE S175** |

### M2.3 — Stats & Progression

| Task | Detail | Status |
|------|--------|--------|
| **Persistent stats** | Kill/death/accuracy per weapon, per mode. JSON save. | CODED (D6), needs wiring |
| **Achievements** | Query layer on stats (D6 Phase 2). | NOT STARTED |
| **Challenges** | Challenge progression tracking, unlocks. | NOT STARTED |

**Gate**: 4-player local with 31 bots, all game modes, all arenas, full endscreen, stats saving. Feels like a complete local multiplayer game.

---

## M3: Online Multiplayer

**Goal**: Players can host and join online matches reliably. Lobby, rooms, NAT traversal, dedicated servers all polished.

**Depends on**: M2 (match flow must work locally first)

### M3.1 — Lobby & Rooms

| Task | Detail | Status |
|------|--------|--------|
| **Room system** | Create/join/leave rooms. Room-scoped match start. | DONE (R-3, S143) |
| **Lobby polish (D5.7)** | Disable unsupported tabs, room nav cleanup, Quick Play button. | PLANNED |
| **Room list broadcast** | SVC_ROOM_LIST from server to all lobby clients. | PLANNED |
| **Leader election** | SVC_LOBBY_LEADER broadcast on leader change. | PLANNED |

### M3.2 — Network Stability

| Task | Detail | Status |
|------|--------|--------|
| **NAT traversal** | STUN, hole punch, relay fallback. | DONE (D8, S83) |
| **Prop sync event-driven** | Replace CRC polling with pickup/door event sync. | PLANNED |
| **B-112 chr corruption** | Root-cause the 31-bot crash. | OPEN — guard in place |
| **B-78 chat rate limiting** | Rate limit rebroadcast. | OPEN |
| **B-79 chunk ordering** | Mod distribution chunk reassembly. | OPEN |

### M3.3 — Spectator Mode (D10)

| Task | Detail | Status |
|------|--------|--------|
| **Free camera** | Spectator can fly around the map. | NOT STARTED |
| **Player follow** | Lock camera to a player. | NOT STARTED |
| **HUD overlay** | Spectator info, player list, minimap. | NOT STARTED |

### M3.4 — Dedicated Server Polish (D9)

| Task | Detail | Status |
|------|--------|--------|
| **Server GUI** | ImGui tabbed interface (Server + Hub). | DONE |
| **Connect codes** | 4-word sentence encoding, clipboard copy. | DONE |
| **Master server (D16)** | Server browser, matchmaking. | NOT STARTED |

**Gate**: Two players on different networks can connect, play a full match, see endscreen, return to lobby. NAT punch works. Dedicated server runs headless. No crashes in 30-minute sessions.

---

## M4: Mod Platform

**Goal**: Players can browse, install, create, and share mods. The mod pipeline is proven end-to-end.

**Depends on**: M0.1 (catalog is the mod foundation), M2 (mods need a working game to modify)

### M4.1 — Mod Browser & Management

| Task | Detail | Status |
|------|--------|--------|
| **Mod menu (P1)** | Gateway UI for browsing/enabling/disabling mods. | PLANNED (~5 sessions) |
| **Mod manager polish** | D3R-6 done but needs UX pass. | PARTIAL |
| **Mod validation** | SHA-256 integrity, dependency checking. | PARTIAL |

### M4.2 — Theme System (D5 Phase 4)

| Task | Detail | Status |
|------|--------|--------|
| **Auto-extract ROM textures** | Runtime extraction to mods/base-ui/. | DONE S157 (self-healing works) |
| **Theme selection UI** | Settings → Theme picker. | PLANNED |
| **UI texture mods (P4)** | 9-slice, effects, fonts via mod. | PLANNED (~6 sessions) |
| **Theme creation interface (P5)** | In-game theme editor. | PLANNED (~2 sessions) |

### M4.3 — Content Mods

| Task | Detail | Status |
|------|--------|--------|
| **Bot name dictionary mods (P2)** | First real content mod, proves pipeline. | PLANNED (~3 sessions) |
| **Character mods** | Custom bodies/heads via catalog. | DEPENDS ON M0.1 |
| **Arena mods** | Custom maps loaded via catalog. | DEPENDS ON M5 |

### M4.4 — Mod Distribution (D14b)

| Task | Detail | Status |
|------|--------|--------|
| **Network distribution** | Host → server → clients. PDCA archives. | DONE (D3R-9, S44) |
| **Mod pack export/import** | PDPK format, zlib compression. | DONE (D3R-10, S45a) |
| **Large mod approval prompt** | Configurable threshold, no hard ceiling. | PLANNED |

**Gate**: A player can create a UI theme mod, share it as a PDPK file, another player installs it, and it works. The full create → distribute → install → play loop is proven.

---

## M5: Forge (Level Editor)

**Goal**: Players can create maps in-client, test them immediately, and share them as mods.

**Depends on**: M4 (mods are the distribution mechanism), M0.1 (catalog for asset references)

### M5.1 — Editor Foundation

| Task | Detail | Status |
|------|--------|--------|
| **Editor mode** | Separate game state for editing (no gameplay logic running). | NOT STARTED |
| **Camera controls** | Free-fly camera with zoom, orbit, pan. | NOT STARTED |
| **Grid system** | Snap-to-grid placement, configurable grid size. | NOT STARTED |
| **Paradox template** | Stage 0x5e as blank canvas (minimal geometry). | IDENTIFIED (context notes) |

### M5.2 — Geometry & Props

| Task | Detail | Status |
|------|--------|--------|
| **BSP/geometry editing** | Place, move, scale, rotate geometry primitives. | NOT STARTED |
| **Prop placement** | Drag-drop props from catalog browser. | NOT STARTED |
| **Spawn point placement** | Player and bot spawn pads. | NOT STARTED |
| **Collision generation** | Auto-generate collision from geometry. | DEPENDS ON M6.1 |

### M5.3 — Testing & Sharing

| Task | Detail | Status |
|------|--------|--------|
| **Play-test loop** | One-click switch from editor to playtest, back to editor. | NOT STARTED |
| **Export as mod** | Save map as catalog-registered mod component. | DEPENDS ON M4 |
| **Publish** | Share via mod distribution network. | DEPENDS ON M4.4 |

**Gate**: A player can create a simple arena, place spawn points, test it with bots, export it, and share it with friends. The creation → test → share loop works.

---

## M6: Polish & Release

**Goal**: The game feels good to play. Movement, collision, audio, visual polish, accessibility, and quality assurance.

**Depends on**: M1-M3 (gameplay must be feature-complete)

### M6.1 — Collision & Physics

| Task | Detail | Status |
|------|--------|--------|
| **Capsule collision** | Capsule sweep system (capsule.c). Stationary jumping works. | PARTIAL (D2b) |
| **Coyote time + jump buffering** | 250ms window for both. Event-driven. | PLANNED |
| **Ground detection** | Reliable ground snap, slope handling. | PARTIAL |
| **Movement feel** | Acceleration curves, air control, friction. | NOT STARTED |

### M6.2 — Audio Polish

| Task | Detail | Status |
|------|--------|--------|
| **Menu sounds** | UI feedback: hover, click, cancel, success/failure. | NOT STARTED |
| **Music transitions** | Smooth crossfades between menu/gameplay/endscreen. | PARTIAL |
| **B-82 sample rate** | Verify 22020 vs 22050 Hz. | OPEN |

### M6.3 — Visual Polish

| Task | Detail | Status |
|------|--------|--------|
| **Settings layout sweep (D5.6)** | Zero hardcoded pixel offsets, proper scaling. | PLANNED |
| **Scanline/CRT effects** | Configurable post-processing. | DONE (theme system) |
| **Resolution scaling** | Proper support for ultrawide, 4K, etc. | PARTIAL (safe area done) |

### M6.4 — Memory & Stability

| Task | Detail | Status |
|------|--------|--------|
| **Memory modernization** | M2-M6: stack→heap, IS4MB collapse, ALIGN16 strip, thread safety. | NOT STARTED |
| **Shutdown sequence (B-83)** | Flush saves, ENet, SDL audio on quit. | OPEN |
| **Update system (D13)** | Self-updater, GitHub releases, dual channels. | CODE WRITTEN, needs compile |

### M6.5 — QA & Release

| Task | Detail | Status |
|------|--------|--------|
| **QC playtest checklist** | Systematic test of every screen, every mode. | TOOLING DONE (dev window) |
| **Bug sweep** | Close all Tier 2 and Tier 3 bugs. | ~12 open |
| **OG menu removal (D5.8)** | Strip all legacy menu render paths. | PLANNED |
| **Release packaging** | Client as zip bundle, server standalone. Zero DLLs. | RULES ESTABLISHED |
| **v1.0.0 tag** | First stable release. | — |

**Gate**: v1.0.0 — campaign playable, combat sim complete, online works, mods supported, editor functional, no known crashers, clean shutdown, self-updater working.

---

## Dependency Graph

```
M0 Foundation Lock
├── M0.1 Catalog Signature Migration ←── CRITICAL PATH, blocks everything
├── M0.2 Input System Unification
└── M0.3 Build Tooling
    │
    ├──→ M1 Playable Campaign (needs M0.1 stages, M0.2 input)
    │    │
    │    ├──→ M2 Combat Sim (needs M1 endscreen patterns)
    │    │    │
    │    │    ├──→ M3 Online MP (needs M2 working locally)
    │    │    │
    │    │    └──→ M4 Mod Platform (needs M0.1 catalog, M2 working game)
    │    │         │
    │    │         └──→ M5 Forge (needs M4 mod distribution)
    │    │
    │    └──→ M6 Polish (needs M1-M3 feature-complete)
    │
    └──→ M6.5 QA & Release → v1.0.0
```

---

## Active Work Mapping

Where current sessions and tasks fit in this roadmap:

| Current Work | Milestone | Phase |
|-------------|-----------|-------|
| M0.1a Stage signatures | M0.1 | **DONE (S167)** — `270d57c` |
| M0.1b Body/Head wrappers | M0.1 | **DONE (S169)** — `0184b80` |
| M0.1c Weapon signatures | M0.1 | **DONE (S171)** — `76e0b00` |
| M1.1 Mission select redesign | M1.1 | **DONE (S168)** — `15f726b` |
| M1.2 Solo mission flow + B-122/B-124 | M1.2 + M0.2 | **DONE (S170)** — `8e9dea8` |
| M2.1 Combat Sim UI audit | M2.1 | **DONE (S172)** — `25fd13d` (verified, no changes needed) |
| M0.1d Remaining asset types | M0.1 | **DONE (S173)** — `8a0a64b` |
| M1.3 Options sub-menu | M1.3 | **DONE (S174)** — `64929c2` |
| D5 Phase 3 (menu roster port) | M1.1-M1.3 | IN PROGRESS — ~55 remaining |
| _DevWindow replacement | M0.3 | Code complete, needs deploy |
| Playtest bug triage | Cross-cutting | QA feedback loop |

---

## Session Numbering Convention

Sessions continue the existing S-numbering (currently at S174). Each session should note which milestone/phase it's working on in the session log. Example: "S167 — M0.1: Stage function signature migration"

---

## Revision History

| Date | Change |
|------|--------|
| 2026-04-06 | v1.0 — Initial unified roadmap created from scattered phase plans, design docs, and memory. Consolidates D5 menu overhaul, catalog migration, mod 