# Infrastructure Phase Tracker

> Execution status for all modernization phases. For the long-term vision and priority ordering, see [roadmap.md](roadmap.md).
> For milestone targets, see [milestones.md](milestones.md).
> Back to [index](README.md)

> **Last updated**: 2026-03-24, Session 47e

---

## Phase Status Summary

| Phase | Name | Status | Last Touched |
|-------|------|--------|-------------|
| D1 | N64 Strip | ✅ **DONE** | S1 |
| D2 | Jump / Bot AI / Char Select | 🔶 Partial | S15 |
| D3 | Mod Manager (legacy) | ♻️ Redesigned → D3R | S24 |
| D3R | Component Mod Architecture | ✅ **D3R-1–11 ALL DONE**, S46a done, S46b TODO | S46 |
| D4 | Menu Migration | ♻️ Superseded (ongoing, no longer blocks) | S22 |
| D5 | Settings / Graphics / QoL | 📋 Planned | — |
| D6 | Persistent Stats | 📋 Planned | — |
| D7 | Discord Rich Presence | 📋 Planned | — |
| D8 | NAT Traversal / LAN | 📋 Planned | — |
| D9 | Dedicated Server | 🔶 Largely done | S47d |
| D10 | Spectator Mode | 📋 Planned | — |
| D11 | Simulant Creator | 📋 Planned | — |
| D12 | Co-op Polish | 📋 Planned | — |
| D13 | Update System | ⏳ Code written, needs build | S11 |
| D14a | Counter-Op Mode | 📋 Planned | — |
| D14b | Mod Distribution | 📋 Planned | — |
| D15 | Map Editor / Char Creator / Skins | 📋 Planned | — |
| D16 | Master Server | 📋 Planned | — |
| D-MEM | Memory Modernization | 🔶 M0–M1 done, MEM-1 coded, M2–M6 remain | S47a |
| D-STAGE | Stage Decoupling | ✅ **ALL 3 PHASES DONE** | S47c |
| B-12 | Dynamic Participant System | 🔶 Phase 1–2 done, Phase 3 (remove chrslots) next | S47b |
| SPF | Server Platform Foundation | 🔶 SPF-1 coded, needs build test | S47d |

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
- **S46b Full enumeration**: TODO — Full animation table (~1000), SFX table (1545), texture table from ROM metadata.

### D4: Menu Migration — ♻️ SUPERSEDED
Original F11 storyboard plan superseded by direct ImGui hotswap. Component library evolves organically.

**Built so far**: Agent Create, Agent Select, Match Setup, Pause Menu, Scorecard Overlay, Network/Multiplayer, Lobby, Server GUI, Update UI, Debug Menu, Typed Dialogs (Danger + Success), Mod Manager, Modding Hub, Bot Customizer, Mod Pack tab.

### D5: Settings / Graphics / QoL — 📋 PLANNED
FOV slider, resolution, fullscreen, VSync, 4-layer audio (Master/Music/Gameplay/UI), rebindable controls. Full plan in [d5-settings-plan.md](d5-settings-plan.md).

### D9: Dedicated Server — 🔶 LARGELY DONE
Server process with CLI args, signal handling, 4-panel ImGui GUI (now tabbed: Server + Hub), lobby/leader election, CLC_LOBBY_START protocol, connect code system.

**SPF-1 additions (S47d)**: Hub lifecycle, room system (4-room pool, 5-state machine), player identity (`pd-identity.dat`), phonetic IP encoding. See SPF section below.

**Remaining**:
- End-to-end playtest (Connect → Lobby → Start → Play → Endscreen)
- Combat Sim stage selection (currently hardcoded to Complex)
- SVC_LOBBY_LEADER broadcast on leader change
- "Quick Play" button (auto-launch server + connect to localhost)

### D13: Update System — ⏳ CODE WRITTEN
All source files written (S8–S11). Semantic versioning, GitHub API, SHA-256, self-replace, save migration, ImGui UI, dual-tag releases, two channels.
**Blocker**: Mike must install libcurl (`pacman -S mingw-w64-x86_64-curl`) and compile.
Full design in [update-system.md](update-system.md).

### D-MEM: Memory Modernization — 🔶 M0–M1 DONE, MEM-1 CODED
- **M0**: Diagnostic log cleanup → LOG_VERBOSE. ✅
- **M1**: `memsizes.h` created, 30+ named constants, 8 files converted. ✅ (~100 ALIGN16 remaining)
- **MEM-1**: `asset_load_state_t` + 4 fields added to `asset_entry_t`. **CODED (S47a)**, needs build test.
- **MEM-2**: `assetCatalogLoad()` / `assetCatalogUnload()` — allocate/free `loaded_data`. PENDING.
- **MEM-3**: `ref_count` acquire/release + eviction policy. PENDING.
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
- **Phase 3 Remove chrslots**: NEXT. Delete u64 chrslots field, legacy shims, BOT_SLOT_OFFSET. Protocol bump to v22.

### SPF: Server Platform Foundation — 🔶 SPF-1 CODED (S47d)
New track building the community platform layer on top of the dedicated server.

- **SPF-1 Hub/Room/Identity/Phonetic**: ✅ CODED (S47d), needs build test. 8 new files + 3 modified.
  - `hub.h/c` — Hub singleton, owns rooms + identity, `hubTick()` drives room 0 from `g_Lobby.inGame`
  - `room.h/c` — Room struct, 5-state lifecycle (LOBBY→LOADING→MATCH→POSTGAME→CLOSED), pool of 4
  - `identity.h/c` — `pd-identity.dat` persistence, 16-byte UUID, up to 4 profiles
  - `phonetic.h/c` — CV syllable IP:port encoding (48-bit, "BALE-GIFE-NOME-RIVA" format)
  - `server_main.c` — hubInit/Tick/Shutdown wired in
  - `server_gui.cpp` — Tabbed layout (Server + Hub tabs), color-coded room states
  - `CMakeLists.txt` — 4 new files added to SRC_SERVER (S47e fix)
- **SPF-2 Room Federation**: PLANNED. Multi-room support, concurrent independent sessions.
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
  │     └── S46b (Full enumeration) ─── TODO
  │
  ├── D-STAGE (Stage Decoupling) ─── ✅ ALL PHASES DONE
  │
  ├── B-12 (Participant System) ─── Phase 1–2 DONE, Phase 3 NEXT
  │
  ├── D9 (Server) ─── LARGELY DONE
  │     └── SPF (Server Platform) ─── SPF-1 coded, SPF-2+ planned
  │           ├── SPF → D16 (Master Server) ─── after content tools
  │           └── SPF → D10 (Spectator)
  │
  ├── D13 (Updater) ─── code written, needs build
  │
  ├── D-MEM (Memory) ─── M0-M1 done, MEM-1 coded, M2-M6 remain
  │
  └── Priority build order:
        D5 (Settings) → D14a (Counter-Op) → D15 (Editor/Creator/Skins)
        → D16 (Master Server) → D6 (Stats) → D7 (Discord)
        → D8 (NAT) → D10 (Spectator) → D11 (Sim Creator)
        → D12 (Co-op) → D14b (Mod Distribution)
```
