# Infrastructure Phase Tracker

> Execution status for all modernization phases. For the long-term vision and priority ordering, see [roadmap.md](roadmap.md).
> Back to [index](README.md)

---

## Phase Status Summary

| Phase | Name | Status | Last Touched |
|-------|------|--------|-------------|
| D1 | N64 Strip | ✅ **DONE** | S1 |
| D2 | Jump / Bot AI / Char Select | 🔶 Partial | S15 |
| D3 | Mod Manager (legacy) | ♻️ Redesigned → D3R | S24 |
| D3R | Component Mod Architecture | 🔶 D3R-1–4 DONE, D3R-5 next | S31 |
| D4 | Menu Migration | ♻️ Superseded (ongoing, no longer blocks) | S22 |
| D5 | Settings / Graphics / QoL | 📋 Planned | — |
| D6 | Persistent Stats | 📋 Planned | — |
| D7 | Discord Rich Presence | 📋 Planned | — |
| D8 | NAT Traversal / LAN | 📋 Planned | — |
| D9 | Dedicated Server | 🔶 Largely done | S7 |
| D10 | Spectator Mode | 📋 Planned | — |
| D11 | Simulant Creator | 📋 Planned | — |
| D12 | Co-op Polish | 📋 Planned | — |
| D13 | Update System | ⏳ Code written, needs build | S11 |
| D14a | Counter-Op Mode | 📋 Planned | — |
| D14b | Mod Distribution | 📋 Planned | — |
| D15 | Map Editor / Char Creator / Skins | 📋 Planned | — |
| D16 | Master Server | 📋 Planned | — |
| D-MEM | Memory Modernization | 🔶 M0–M1 done, M2–M6 remain | S15 |
| D-STAGE | Stage Decoupling | 🔶 Phase 1 done, 2–3 remain | S23 |
| D-PART | Dynamic Participant System (B-12) | 🔶 Phase 1 coded, awaiting build test. 2–3 remain | S26 |

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
- **D3a–D3d**: ✅ Done. Core modmgr, shadow arrays, fs.c refactor, ImGui foundation. (Legacy — will be replaced by D3R)
- **D3e–D3g**: Superseded by D3R. See below.
- **S23 fix**: Mod manager path resolution (CWD → exe dir → base dir). Stage range check widened.
- **S24 fix**: Bundled mod ID mismatch corrected. `g_NotLoadMod` init fix.

### D3R: Component Mod Architecture — 🔶 FOUNDATION COMPLETE (Sessions 27–31)
Full design in [component-mod-architecture.md](component-mod-architecture.md). Replaces monolithic D3 with component-based system.
- **D3R-1 Decompose mods**: ✅ DONE (S29) — 56 maps, 42 chars, 5 tex packs in `post-batch-addin/mods/mod_*/_components/`
- **D3R-2 Asset Catalog**: ✅ DONE (S28) — `assetcatalog.h/c`, FNV-1a + CRC32, open addressing, 20-function API
- **D3R-3 Base game cataloging**: ✅ DONE (S30, build pass S31) — `assetcatalog_base.c`, 87 stages + 63 bodies + 75 heads
- **D3R-4 Scanner + loader**: ✅ DONE (S30, build pass S31) — `assetcatalog_scanner.c/h`, INI parser, category scan
- **D3R-5 Callsite migration**: ← NEXT — Wire catalog init into startup, replace numeric lookups with `catalogResolve()`. See briefing in [tasks-current.md](tasks-current.md).
- **D3R-6 Mod Manager UI**: Browse by category/group, toggle, validate, apply
- **D3R-7 INI Manager tool**: In-game schema-driven editor for component `.ini` files
- **D3R-8 Bot Customizer**: Trait editor → saves as `bot_variants/` component
- **D3R-9 Network distribution**: Delta packs, session-only downloads, lobby combat log
- **D3R-10 Mod Pack export**: `.pdpack` creation and extraction
- **D3R-11 Legacy cleanup**: Remove `g_ModNum`, `modconfig.txt`, static arrays

### D4: Menu Migration — ♻️ SUPERSEDED
Original F11 storyboard plan superseded by direct ImGui hotswap. Component library evolves organically. Menus built as needed alongside other phases. See [menu-storyboard.md](menu-storyboard.md) for reference inventory.

**Built so far**: Agent Create, Agent Select, Match Setup, Pause Menu, Scorecard Overlay, Network/Multiplayer, Lobby, Server GUI, Update UI, Debug Menu, Typed Dialogs (Danger + Success).

### D5: Settings / Graphics / QoL — 📋 PLANNED
FOV slider, resolution, fullscreen, VSync, 4-layer audio (Master/Music/Gameplay/UI), rebindable controls. Full plan in [d5-settings-plan.md](d5-settings-plan.md).

### D9: Dedicated Server — 🔶 LARGELY DONE
Server process with CLI args, signal handling, 4-panel ImGui GUI, lobby/leader election, CLC_LOBBY_START protocol, connect code system.

**Remaining**:
- End-to-end playtest (Connect → Lobby → Start → Play → Endscreen)
- Combat Sim stage selection (currently hardcoded to Complex)
- SVC_LOBBY_LEADER broadcast on leader change
- "Quick Play" button (auto-launch server + connect to localhost)

### D13: Update System — ⏳ CODE WRITTEN
All source files written (S8–S11). Semantic versioning, GitHub API, SHA-256, self-replace, save migration, ImGui UI, dual-tag releases, two channels.
**Blocker**: Mike must install libcurl (`pacman -S mingw-w64-x86_64-curl`) and compile.
Full design in [update-system.md](update-system.md).

### D-MEM: Memory Modernization — 🔶 M0–M1 DONE
- **M0**: Diagnostic log cleanup → LOG_VERBOSE. ✅
- **M1**: `memsizes.h` created, 30+ named constants, 8 files converted. ✅ (~100 ALIGN16 remaining)
- **M2**: Stack→heap promotion (pak.c 16KB, texdecompress.c 12KB, menuitem.c 24KB). Not started.
- **M3**: IS4MB ternary collapse (107 dead branches). Not started.
- **M4**: ALIGN16 strip (119 wrappers, 39 files). Not started.
- **M5**: Separate pool regions (architectural — eliminates overlap risk). Not started.
- **M6**: Thread safety (mutex on mempAlloc/mempFree). Not started.

Full plan in [memory-modernization.md](memory-modernization.md).

### D-STAGE: Stage Decoupling — 🔶 PHASE 1 DONE
- **Phase 1 Safety Net**: ✅ Done (S23). Bounds checks at all known access points. Needs build test.
- **Phase 2 Dynamic Table**: Designed. Heap-allocated `g_Stages`, `g_NumStages`, pristine base copy.
- **Phase 3 Domain Separation**: Designed. `soloStageGetIndex()` lookup, stagenum-keyed besttimes.

See [constraints.md](constraints.md) — Index Domain Warning section.

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
  │     └── D3a-d ─── DONE (to be replaced)
  ├── D3R (Component Mod Architecture) ─── FOUNDATION DONE
  │     ├── D3R-1 (Decompose mods) ─── ✅ DONE
  │     ├── D3R-2 (Asset Catalog) ─── ✅ DONE
  │     ├── D3R-3 (Base game catalog) ─── ✅ DONE
  │     ├── D3R-4 (Scanner) ─── ✅ DONE
  │     ├── D3R-5 (Callsite migration) ─── NEXT
  │     ├── D3R-6–8 (UI tools) ─── planned
  │     ├── D3R-9–10 (Network + packs) ─── planned
  │     └── D3R-11 (Legacy cleanup) ─── planned
  │
  ├── D9 (Server) ─── LARGELY DONE
  │     ├── D16 (Master Server) ─── after content tools
  │     └── D10 (Spectator)
  │
  ├── D13 (Updater) ─── code written, needs build
  │
  ├── D-MEM (Memory) ─── M0-M1 done, M2-M6 remain
  │
  ├── D-STAGE (Stage Decoupling) ─── Phase 1 done, 2-3 remain
  │
  └── Priority build order:
        D5 (Settings) → D14a (Counter-Op) → D15 (Editor/Creator/Skins)
        → D16 (Master Server) → D6 (Stats) → D7 (Discord)
        → D8 (NAT) → D10 (Spectator) → D11 (Sim Creator)
        → D12 (Co-op) → D14b (Mod Distribution)
```
