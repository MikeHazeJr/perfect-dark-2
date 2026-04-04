# Infrastructure Phase Tracker

> Execution status for all modernization phases. For the long-term vision and priority ordering, see [roadmap.md](roadmap.md).
> For milestone targets, see [milestones.md](milestones.md).
> Back to [index](README.md)

> **Last updated**: 2026-04-02, Session S130 — Wire protocol v27 (all net_hash removed), SAVE-COMPAT stripped, comprehensive bug audit (4 critical/high fixes)

---

## Branch Strategy (updated S84)

| Branch | Purpose |
|--------|---------|
| `dev` | Default branch, active development. All PRs merge here. |
| `stable` | Releases only. Created from `dev` S84. |

`main` was deleted S84 (locally + GitHub remote). GitHub default branch changed to `dev`. Stale remote worktree branches also deleted S84.

**WorktreeCreate hook** (`.claude/settings.local.json`): Blocks Claude Code from creating new worktrees (exit code 2). All Claude work happens directly in the main working copy.

## Build Tooling (updated S84)

| Tool | Location | Notes |
|------|----------|-------|
| `tools/build.sh` | Cross-session clean build. `--target both` builds client + server. |
| `tools/build-cleanup.sh` | Removes `ClaudeBuilds/` after successful build. |
| `tools/parse-log.sh` | Filters `sysLogPrintf` output by tag prefix. |

Build test directories go under `ClaudeBuilds/` (changed S84 from `build_test_*/`). Fully `rm -rf`'d after completion. `.gitignore` tracks `ClaudeBuilds/`.

---

## Phase Status Summary

| Phase | Name | Status | Last Touched |
|-------|------|--------|-------------|
| D1 | N64 Strip | ✅ **DONE** | S1 |
| D2 | Jump / Bot AI / Char Select | 🔶 Partial | S15 |
| D3 | Mod Manager (legacy) | ♻️ Redesigned → D3R | S24 |
| D3R | Component Mod Architecture | ✅ **ALL DONE** (D3R-1–11, S46a, S46b) | S80 |
| D4 | Menu Migration | ♻️ Superseded (ongoing, no longer blocks) | S22 |
| D5 | Settings / Graphics / QoL | 🔶 Partial (UI Scaling done S97) | S97 |
| D6 | Persistent Stats | 🔶 Partial | S49 |
| D7 | Discord Rich Presence | 📋 Planned | — |
| D8 | NAT Traversal / LAN | ✅ **DONE** | S83 |
| D9 | Dedicated Server | 🔶 Largely done | S47d |
| D10 | Spectator Mode | 📋 Planned | — |
| D11 | Simulant Creator | 📋 Planned | — |
| D12 | Co-op Polish | 📋 Planned | — |
| D13 | Update System | ⏳ Code written, needs build | S11 |
| D14a | Counter-Op Mode | 📋 Planned | — |
| D14b | Mod Distribution | 📋 Planned | — |
| D15 | Map Editor / Char Creator / Skins | 📋 Planned | — |
| D16 | Master Server | 📋 Planned | — |
| MSP | Match Startup Pipeline | ✅ Phases A–F done (S84–S90), SA-1–SA-7 ALL DONE (S91–S97), Manifest Lifecycle Sprint Phases 0–6 ALL DONE (S110–S115) | S115 |
| D-MEM | Memory Modernization | 🔶 M0–M1 done, MEM-1/2/3 DONE, M2–M6 (stack→heap) remain | S47a |
| D-STAGE | Stage Decoupling | ✅ **ALL 3 PHASES DONE** | S47c |
| B-12 | Dynamic Participant System | 🔶 Phase 1–2 done, Phase 3 (remove chrslots) next | S47b |
| SPF | Server Platform Foundation | 🔶 SPF-1–3 coded, R-series planned (R-1 next), QC pending | S51 |

---

## Detailed Status

### D1: N64 Strip — ✅ DONE
672 platform guards removed across 114+ files. Zero `PLATFORM_N64` references remain.

### D2: Jump / Bot AI / Char Select — 🔶 PARTIAL
- **D2a Char Select Redesign**: ✅ Done. Scrollable body list, live 3D preview, head detection.
- **D2b Capsule Collision**: Testing. Capsule sweep system in capsule.c. Stationary jumping works.
- **D2c Bot Jump AI**: Not started. Depends on D2b.
- **D2d Custom Simulants**: Not started. Feeds D11.

### D3: Mod Manager — ♻️ REDESIGNED as D3R (Session 27)
- **D3a–D3d**: ✅ Done. Core modmgr, shadow arrays, fs.c refactor, ImGui foundation. (Legacy — replaced by D3R)
- **D3e–D3g**: Superseded by D3R.
- **S23 fix**: Mod manager path resolution (CWD → exe dir → base dir). Stage range check widened.
- **S24 fix**: Bundled mod ID mismatch corrected. `g_NotLoadMod` init fix.

### D3R: Component Mod Architecture — ✅ CORE COMPLETE (Sessions 27–46)
Full design in [component-mod-architecture.md](component-mod-architecture.md). Replaces monolithic D3 with component-based system.
- **D3R-1 Decompose mods**: ✅ DONE (S29) — 56 maps, 42 chars, 5 tex packs
- **D3R-2 Asset Catalog**: ✅ DONE (S28) — FNV-1a + CRC32, open addressing, 20-function API
- **D3R-3 Base game cataloging**: ✅ DONE (S30/31) — 87 stages + 63 bodies + 75 heads
- **D3R-4 Scanner + loader**: ✅ DONE (S30/31) — INI parser, category scan
- **D3R-5 Callsite migration**: ✅ DONE (S38/39) — All 6 modmgr accessors catalog-backed, 62 callsites, zero caller changes
- **D3R-6 Mod Manager UI**: ✅ DONE (S39/40) — Browse/toggle, validation, `.modstate` persistence, embedded in Modding Hub
- **D3R-7 Modding Hub**: ✅ CODED (S40) — Hub with Mod Manager, INI Editor, Model Scale Tool. Needs build test.
- **D3R-8 Bot Customizer**: ✅ DONE (S43) — Trait editor, `botvariant.c/h`, save-as-preset, hot-register
- **D3R-9 Network distribution**: ✅ DONE (S44) — Protocol v20→v21, PDCA archives, zlib chunks, crash recovery, download prompt UI
- **D3R-10 Mod Pack export/import**: ✅ DONE (S45a) — `modpack.h/c`, PDPK format, zlib, 4th tab in Modding Hub
- **D3R-11 Legacy cleanup**: ✅ DONE (S45b) — g_ModNum removed, modconfig.txt removed, shadow arrays removed, catalog-only accessors
- **S46a Asset Catalog expansion**: ✅ DONE — ASSET_ANIMATION/TEXTURE/GAMEMODE/AUDIO/HUD + rich ext structs. 47 weapons, 8 props, 6 gamemodes, 6 HUD elements.
- **S46b Full enumeration**: ✅ DONE (S80) — 1207 animations, 3503 textures, 1545 audio entries registered in `assetcatalog_base_extended.c`.

### D4: Menu Migration — ♻️ SUPERSEDED
Original F11 storyboard plan superseded by direct ImGui hotswap. Component library evolves organically.

**Built so far**: Agent Create, Agent Select, Match Setup, Pause Menu, Scorecard Overlay, Network/Multiplayer, Lobby, Server GUI, Update UI, Debug Menu, Typed Dialogs (Danger + Success), Mod Manager, Modding Hub, Bot Customizer, Mod Pack tab.

### D8: NAT Traversal / LAN — ✅ DONE (S83)
All 4 phases implemented in S83:
- **STUN client** (`port/src/net/netstun.c`): RFC 5389 Binding Request → `g_StunPublicIP`/`g_StunPublicPort`.
- **Query advertising** (`netlobby.c`): `SVC_ADDR_QUERY` / `CLC_ADDR_REPORT` — server broadcasts STUN-discovered external IP+port to all lobby clients.
- **Hole punch** (`port/src/net/netholepunch.c`): Symmetric hole-punch handshake (`CLC_PUNCH_REQ` / `SVC_PUNCH_REPLY`). 5 probe packets, 3s timeout, relay fallback.
- **NAT diagnostics**: Debug menu "NAT" section shows STUN result, punch status per peer, relay fallback indicator.

### D5: Settings / Graphics / QoL — 📋 PLANNED
FOV slider, resolution, fullscreen, VSync, 4-layer audio (Master/Music/Gameplay/UI), rebindable controls. Full plan in [d5-settings-plan.md](d5-settings-plan.md).

### D6: Persistent Stats — 🔶 PARTIAL (S49)
- `port/src/playerstats.c` — string-keyed hash table of counters, JSON persistence to `$S/playerstats.json`. CODED, needs build test.
- Stats are incremented per gameplay event (kill, death, shot, mode played, weapon used, etc.)
- Wired into: mpstats.c + mplayer.c (planned but not yet done)
- Achievements are a future query layer on top of stats (D6 Phase 2)

### D9: Dedicated Server — 🔶 LARGELY DONE
Server process with CLI args, signal handling, 4-panel ImGui GUI (now tabbed: Server + Hub), lobby/leader election, CLC_LOBBY_START protocol, sentence-based connect code system.

**Connect codes**: `port/src/connectcode.c` — 4-word sentence encoding of IPv4. Public IP via UPnP (async) with HTTP fallback (`curl`→`api.ipify.org`). Code displayed in lobby + clipboard copy. No raw IP in any UI. See [join-flow-plan.md](join-flow-plan.md).

**SPF-1 additions (S47d)**: Hub lifecycle, room system (4-room pool, 5-state machine), player identity (`pd-identity.dat`), phonetic IP encoding. See SPF section below.

**Remaining**:
- SVC_ROOM_LIST: broadcast room state from server to clients — J-3
- Combat Sim stage selection (currently hardcoded to Complex)
- SVC_LOBBY_LEADER broadcast on leader change
- "Quick Play" button (auto-launch server + connect to localhost)

**Done** (see [join-flow-plan.md](join-flow-plan.md)):
- J-1: End-to-end playtest verified (S81)
- J-2: Connect code display in server_gui.cpp — IP waterfall UPnP→STUN (S84)
- J-4: Recent server history UI with relative timestamps (S80/S84)
- J-5: Lobby handoff polish (S81)

### D13: Update System — ⏳ CODE WRITTEN
All source files written (S8–S11). Semantic versioning, GitHub API, SHA-256, self-replace, save migration, ImGui UI, dual-tag releases, two channels.
**Blocker**: Mike must install libcurl (`pacman -S mingw-w64-x86_64-curl`) and compile.
Full design in [update-system.md](update-system.md).

### D-MEM: Memory Modernization — 🔶 M0–M1 DONE, MEM-1/2/3 DONE, M2–M6 REMAIN
- **M0**: Diagnostic log cleanup → LOG_VERBOSE. ✅
- **M1**: `memsizes.h` created, 30+ named constants, 8 files converted. ✅ (~100 ALIGN16 remaining)
- **MEM-1**: `asset_load_state_t` + 4 fields added to `asset_entry_t`. ✅ **DONE**
- **MEM-2**: `assetCatalogLoad()` / `assetCatalogUnload()` — allocate/free `loaded_data`. ✅ **DONE**
- **MEM-3**: `ref_count` acquire/release + eviction policy. ✅ **DONE**
- **M2**: Stack→heap promotion (pak.c 16KB, texdecompress.c 12KB, menuitem.c 24KB). Not started.
- **M3**: IS4MB ternary collapse (107 dead branches). Not started.
- **M4**: ALIGN16 strip (119 wrappers, 39 files). Not started.
- **M5**: Separate pool regions (architectural — eliminates overlap risk). Not started.
- **M6**: Thread safety (mutex on mempAlloc/mempFree). Not started.

Full plan in [memory-modernization.md](memory-modernization.md).

### D-STAGE: Stage Decoupling — ✅ ALL PHASES DONE (S47c)
- **Phase 1 Safety Net**: ✅ Done (S23). Bounds checks at all known access points.
- **Phase 2 Dynamic Table**: ✅ Done (S47c). Heap-allocated `g_Stages`, `g_NumStages`, `stageTableInit()`, `stageGetEntry()`, `stageTableAppend()`.
- **Phase 3 Domain Separation**: ✅ Done (S47c). `soloStageGetIndex()` lookup, bounds guards in `endscreen.c` + `mainmenu.c`.

See [constraints.md](constraints.md) — Index Domain Warning section.

### B-12: Dynamic Participant System — 🔶 PHASE 1–2 DONE (S47b)
- **Phase 1 Parallel Pool**: ✅ Done (S26). `participant.h/c`, heap-allocated pool (capacity MAX_MPCHRS=40), parallel sync hooks.
- **Phase 2 Callsite Migration**: ✅ Done (S47b). 7 files, ~25 mplayer.c sites + setup.c + challenge.c + filemgr.c + matchsetup.c. `mpAddParticipantAt()` API. Build pass.
- **Phase 3 Remove chrslots**: NEXT. Delete u64 chrslots field, legacy shims, BOT_SLOT_OFFSET. Protocol bump to v28 (next after v27).

### SPF: Server Platform Foundation — 🔶 SPF-1–3 IN PROGRESS, R-series PLANNED (S51)
New track building the community platform layer on top of the dedicated server.

- **SPF-1 Hub/Room/Identity/Phonetic**: ✅ BUILDS (S47d coded, S49 confirmed via SPF-3 build).
  - `hub.h/c` — Hub singleton, owns rooms + identity, `hubTick()` drives room 0 from `g_Lobby.inGame`. **Note**: `hubGetMaxSlots/SetMaxSlots/GetUsedSlots/GetFreeSlots` declared in hub.h but NOT implemented in hub.c — R-1 implements them.
  - `room.h/c` — Room struct, 5-state lifecycle (LOBBY→LOADING→MATCH→POSTGAME→CLOSED), pool of 4 (`HUB_MAX_ROOMS`), client array capped at 8 (`HUB_MAX_CLIENTS` — stale, must expand to 32); `roomGenerateName()` for auto-naming (adjective+noun)
  - `identity.h/c` — `pd-identity.dat` persistence, 16-byte UUID, up to 4 profiles
  - `phonetic.h/c` — CV syllable IP:port encoding (still available, coexists with sentence codes)
  - `connectcode.h/c` — **Primary join mechanism**: 4-word sentence codes (adjective+noun+action+place = IPv4). Code-only joining enforced. No raw IP in UI.
  - `server_main.c` — hubInit/Tick/Shutdown wired in
  - `server_gui.cpp` — Tabbed layout (Server + Hub tabs), color-coded room states. **B-29**: raw IP shown in status bar (line 695) — remove in R-1.
  - `CMakeLists.txt` — 4 new files added to SRC_SERVER (S47e fix)
  - **QC PENDING** — needs end-to-end server playtest
- **SPF-2a Menu Manager**: ✅ BUILD PASS (S48 coded, S49 extern C fix). menumgr.c/h, 100ms cooldown. Pause + modding hub + join screen wired.
- **SPF-3 Lobby + Join-by-Code**: ✅ CODED (S49, commit `3b588c1`). Awaiting playtest.
  - Lobby: shows hub state + room list with color-coded states and player counts
  - Join screen: menu view 4, phonetic or direct IP input, MENU_JOIN push/pop
- **SPF Asset Catalog Audit Phase 1**: ✅ DONE (S49). Failure logging at all critical load points.
- **SPF Player Stats**: ✅ CODED (S49). `playerstats.h/c`, `statIncrement()`, JSON persistence. Needs wiring at gameplay sites.
- **SPF Connect Codes**: ✅ REWRITTEN (S49). Sentence-based codes replace phonetic syllables as primary connect method. 256-word vocabulary × 4 slots = 32-bit IPv4.
- **Join Flow**: See [join-flow-plan.md](join-flow-plan.md). Gaps: room state not yet synced to clients (SVC_ROOM_LIST needed), server GUI missing connect code display.
- **R-series Room Architecture**: PLANNED (S51). See [room-architecture-plan.md](room-architecture-plan.md).
  - **R-1** Foundation: hub slot pool, g_NetLocalClient=NULL for dedicated, IP scrub (B-28/29/30). No protocol change.
  - **R-2** Room lifecycle: demand-driven rooms, leader_client_id, room_id on netclient, HUB_MAX_ROOMS=16/HUB_MAX_CLIENTS=32.
  - **R-3** Room sync protocol: SVC_ROOM_LIST/UPDATE/ASSIGN (0x75-0x77), CLC_ROOM_JOIN/LEAVE (0x0A-0x0B).
  - **R-4** Match start: CLC_ROOM_SETTINGS/KICK/TRANSFER/START (0x0C-0x0F), room-scoped stage start.
  - **R-5** Server GUI redesign: Players + Rooms panels, operator actions (move/kick/set-leader/close).
- **SPF-3+**: Social hub, content sharing, whitelists, mesh networking (milestones v0.3–v0.4).

Architecture doc: [server-architecture.md](server-architecture.md)

---

## Dependency Graph

```
D1 (N64 Strip) ─── DONE
  │
  ├── D2 (Jump/Bots) ─── PARTIAL
  │     ├── D2a (Char Select) ─── DONE
  │     ├── D2b (Capsule) ─── testing
  │     ├── D2c (Bot Jump AI) ─── needs D2b
  │     └── D2d (Custom Sims) ─── feeds D11
  │
  ├── D3 (Mod Manager legacy) ─── REDESIGNED → D3R
  │     └── D3a-d ─── DONE (replaced)
  ├── D3R (Component Mod Architecture) ─── ✅ CORE COMPLETE
  │     ├── D3R-1–11 ─── ALL ✅ DONE
  │     ├── S46a (Catalog expansion) ─── ✅ DONE
  │     └── S46b (Full enumeration) ─── ✅ DONE (S80)
  │
  ├── D-STAGE (Stage Decoupling) ─── ✅ ALL PHASES DONE
  │
  ├── B-12 (Participant System) ─── Phase 1–2 DONE, Phase 3 NEXT
  │
  ├── D9 (Server) ─── LARGELY DONE
  │     └── SPF (Server Platform) ─── SPF-1 coded, SPF-2+ planned
  │           ├── J-1 (verify join) ─── DONE (S81)
  │           ├── J-2 (server GUI code) ─── DONE (S84)
  │           ├── J-3 (SVC_ROOM_LIST) ─── NEXT
  │           ├── SPF → D16 (Master Server) ─── after content tools
  │           └── SPF → D10 (Spectator)
  │
  ├── D6 (Stats) ─── playerstats.c CODED (needs wire-in)
  │
  ├── D13 (Updater) ─── code written, needs build
  │
  ├── D-MEM (Memory) ─── M0-M1 done, MEM-1/2/3 DONE, M2-M6 (stack→heap) remain
  │
  └── Priority build order:
        D5 (Settings) → D14a (Counter-Op) → D15 (Editor/Creator/Skins)
        → D16 (Master Server) → D6 (Stats) → D7 (Discord)
        → D8 (NAT) → D10 (Spectator) → D11 (Sim Creator)
        → D12 (Co-op) → D14b (Mod Distribution)
```
