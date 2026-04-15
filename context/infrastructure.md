# Infrastructure Phase Tracker

> Execution status for all modernization phases. For the long-term vision and
> priority ordering, see [roadmap.md](roadmap.md). Release milestones
> (v0.1.0 → v1.0.0) live in roadmap.md under "Release Milestones".
> Back to [index](README.md)

> **Last updated**: 2026-04-14, S255 Dev Window v2 Pull/Push + DPI. Wire protocol at
> **v35** (v34 seamless audio-mod sync + v35 L5 match_seed + S253 additive
> `SVC_ROOM_SETTINGS` / `SVC_ROOM_PLAYLIST` / CLC counterparts). D13 Update
> System FIXED (S245 FIX-F). R-1 / R-2 / R-3 DONE; R-4 protocol-side DONE
> (S143 + S253 additives); L-1 through L-4 DONE; R-5 server GUI redesign,
> L-5 Campaign / Counter-Op setup, L-6 drop-in, D14a Counter-Op, D16 master
> server remain planned. Master Orchestration Plan L0–L7 complete (FIX-B.1
> sole open supporting item). See `session-log.md` S231–S253 for layer-by-layer
> detail; `constraints.md` for current invariants;
> [network-architecture.md](network-architecture.md) for the consolidated
> networking roadmap.

---

## Branch Strategy (updated S84)

| Branch | Purpose |
|--------|---------|
| `dev` | Default branch, active development. All PRs merge here. |
| `stable` | Releases only. Created from `dev` S84. |

`main` was deleted S84 (local + GitHub remote). GitHub default branch is
`dev`. Stale remote worktree branches also deleted S84.

**WorktreeCreate hook** (`.claude/settings.local.json`): blocks Claude from
creating new worktrees (exit code 2). All Claude work happens directly in the
main working copy OR in session-created worktrees under `.claude/worktrees/`.

## Build Tooling

| Tool | Location | Notes |
|------|----------|-------|
| `devtools/build-headless.ps1` | PowerShell headless build (AI-facing). Self-configures env via `_build-env-prelude.ps1`. | S247 self-heal |
| `devtools/build-env.sh` | Bash prelude: `source devtools/build-env.sh && ninja -C Build pd pd-server`. Sets TEMP/TMP + prepends MinGW to PATH. | S247 |
| `devtools/dev-window-v2/dev-window-v2.ps1` | WPF dev window v2 — build / run / version / status / **git Pull·Push** (S255) + DPI-aware font scaling (`SetProcessDPIAware`, layout rounding, Display text mode). Font polish baseline S248. | S255 |
| `devtools/release.ps1` | Versioned release build. | — |

**Build environment invariants** (see project `CLAUDE.md`):
`TEMP=C:\Users\mikeh\AppData\Local\Temp`, `/c/msys64/mingw64/bin` prepended
to PATH, `CCACHE_SLOPPINESS=pch_defines,time_macros`. Do not rediscover.

---

## Phase Status Summary

| Phase | Name | Status | Last touched |
|-------|------|--------|--------------|
| D1 | N64 Strip (672 guards, 114+ files) | ✅ **DONE** | S1 |
| D2 | Jump / Bot AI / Char Select | 🔶 Partial | S15 / S187 |
| D3 | Mod Manager (legacy) | ♻️ Redesigned → D3R | S24 |
| D3R | Component Mod Architecture | ✅ **ALL DONE** (D3R-1–11, S46a, S46b) | S80 |
| D4 | Menu Migration | ♻️ Superseded by ImGui hotswap | S22 |
| D5 | Settings / Graphics / QoL | 🔶 Phase 3 DONE; Phase 4 (themes) partial; Phase 5 (lobby scene) planned | S221 |
| D6 | Persistent Stats | 🔶 Partial — `playerstats.c` coded (S49), gameplay-site wire-in partial | S49 |
| D7 | Discord Rich Presence | 📋 Planned | — |
| D8 | NAT Traversal / LAN | ✅ **DONE** | S83 |
| D9 | Dedicated Server | ✅ **MOSTLY DONE** — R-1 / R-2 / R-3 / R-4 shipped; R-5 server GUI redesign planned | S253 |
| D10 | Spectator Mode | 📋 Planned | — |
| D11 | Simulant Creator | 📋 Planned | — |
| D12 | Co-op Polish | 📋 Planned | — |
| D13 | Update System | ✅ **DONE** (S245 FIX-F) | S245 |
| D14a | Counter-Op Mode | 📋 Planned | — |
| D14b | Mod Distribution | ✅ **DONE** — D3R-9 network distrib + D3R-10 mod-pack export/import + A-7 mod-audio network sync + S-9 skin network sync | S-series 2026-04-12 |
| D15 | Map Editor / Char Creator / Skins | 🔶 Partial — Skin Editor DONE (S-1 → S-9, 2026-04-12); Map Import Pipeline DONE (L3, S240+S244); Level Editor (Forge) planned (v1.0.0) | 2026-04-12 / 2026-04-13 |
| D16 | Master Server | 📋 Planned (see [network-architecture.md](network-architecture.md) §7 for full design) | — |
| MSP | Match Startup Pipeline (A–F + SA-1–7 + Manifest Lifecycle 0–6 + L5) | ✅ **ALL DONE** | S115 / S241 |
| D-MEM | Memory Modernization | 🔶 M0–M1 + MEM-1/2/3 DONE; M2–M6 stack→heap remain | S47a |
| D-STAGE | Stage Decoupling | ✅ **ALL 3 PHASES DONE** | S47c |
| B-12 | Dynamic Participant System | 🔶 Phase 1–2 DONE; Phase 3 (remove chrslots) next | S47b |
| SPF | Server Platform Foundation | ✅ **ALL SHIPPED** — SPF-1 Hub/Room/Identity/Phonetic + SPF-2a Menu Mgr + SPF-3 Lobby + SPF-3 Connect Codes + R-1 through R-4 | S51 / S143 / S253 |

### Audio / Skin / Map-Import feature lines

| Line | Scope | Status | Session |
|------|-------|--------|---------|
| A-1 → A-7 | Audio Mod Menu (catalog extension, mod music stream, UI, soundtrack extension, pack creation, multi-format import, network sync) | ✅ **ALL DONE** | S210 – S222 (2026-04-12) |
| S-1 → S-9 | Skin Editor (canvas + 2D editor, live 3D preview, save-as-mod, image import, quantize/dither, blend modes, UV wireframe, network sync) | ✅ **ALL DONE** | S215 – S217 (2026-04-12) |
| L3 / M-5.x / M-6.x / M-7.x | Mod Map Import Pipeline (PD-native importer + UI + retroactive validation + smoke sweep) | ✅ **ALL DONE** | S240 / S244 / S246 |
| L2 spawn pool | `spawnpool.c/h` L1-L4 chain + match_seed determinism + B-134 capsule-radius threshold | ✅ **ALL DONE** | S239 / S242 / S249 |
| L5 match lifecycle | Co-op manifest pipeline + CLC_STAGE_READY + protocol v35 + match_seed in SVC_STAGE_START | ✅ **DONE** | S241 |
| L6 rendering | FIX-C.1 canonical GBI state reset + FIX-C.3 menu opacity + sky tearing systemic defense | ✅ **DONE** | S243 |
| L7 | FIX-F updater robustness + FIX-G mission category headers with completion counters | ✅ **DONE** | S245 |

---

## Detailed Status

### D1: N64 Strip — ✅ DONE

672 platform guards removed across 114+ files. Zero `PLATFORM_N64` references
remain. Historical N64 assembly, ultra/os, ultra/libc also removed; only
ultra/audio, ultra/gu, and 4 ultra/io VI-mode files remain.

### D2: Jump / Bot AI / Char Select — 🔶 PARTIAL

- **D2a Char Select Redesign**: ✅ DONE. Scrollable body list, live 3D preview,
  head detection.
- **D2b Capsule Collision**: In use — capsule sweep system in `capsule.c`.
  Stationary jumping + stair-step work. Replaces legacy `cdTestVolume` /
  `cdFindGroundInfoAtCyl` hacks. See [collision.md](collision.md) and
  constraint "N64 collision workarounds" (Removed 2026-03-12).
- **D2c Bot Jump AI**: Not started. Depends on D2b stability.
- **D2d Custom Simulants**: Feeds D11 (planned).
- **Input rework**: S187–S190 full unification under action-map IMCs
  (M0.2). P0-only binding setup (single-local-player constraint).
  `setupGameplayDefaults`, CrouchMode (hold / analog / toggle / toggle+analog),
  FarSight strafe fixes, LSTICK=Sprint binding.

### D3R: Component Mod Architecture — ✅ CORE COMPLETE

Full design in [component-mod-architecture.md](component-mod-architecture.md).
Replaces monolithic D3 with a component-based system.

- **D3R-1 Decompose mods**: ✅ DONE (S29) — 56 maps, 42 chars, 5 tex packs.
- **D3R-2 Asset Catalog**: ✅ DONE (S28) — FNV-1a + CRC32, open addressing,
  20-function API. String-keyed, namespace / readable-name format.
- **D3R-3 Base game cataloging**: ✅ DONE (S30 / S31) — 87 stages + 63 bodies +
  75 heads.
- **D3R-4 Scanner + loader**: ✅ DONE (S30 / S31) — INI parser, category scan.
- **D3R-5 Callsite migration**: ✅ DONE (S38 / S39) — 6 modmgr accessors
  catalog-backed, 62 callsites, zero caller changes.
- **D3R-6 Mod Manager UI**: ✅ DONE (S39 / S40) — Browse / toggle /
  validation / `.modstate` persistence, embedded in Modding Hub.
- **D3R-7 Modding Hub**: ✅ DONE — Hub with Mod Manager + INI Editor + Model
  Scale Tool. S248 fixes: B-135 / B-136 / B-138 (mod.json id field, persistence,
  dirty-state modal).
- **D3R-8 Bot Customizer**: ✅ DONE (S43) — Trait editor, `botvariant.c/h`,
  save-as-preset, hot-register.
- **D3R-9 Network distribution**: ✅ DONE (S44) — PDCA archives, zlib chunks,
  crash recovery, download-prompt UI, `expected_chunk` ordering guard (B-79
  S185), 256 MB cap on decompression buffer (B-80 S185).
- **D3R-10 Mod Pack export/import**: ✅ DONE (S45a) — `modpack.h/c`, PDPK
  format, zlib, 4th tab in Modding Hub.
- **D3R-11 Legacy cleanup**: ✅ DONE (S45b) — `g_ModNum` removed, modconfig.txt
  parsing removed, shadow arrays removed, catalog-only accessors.
- **S46a Asset Catalog expansion**: ✅ DONE — ASSET_ANIMATION / TEXTURE /
  GAMEMODE / AUDIO / HUD + rich ext structs.
- **S46b Full enumeration**: ✅ DONE (S80) — 1207 animations, 3503 textures,
  1545 audio entries in `assetcatalog_base_extended.c`.
- **A-1 → A-7 / S-1 → S-9**: complete Audio Mod Menu + Skin Editor lines
  landed 2026-04-12 — see feature-line table above.

### D4: Menu Migration — ♻️ SUPERSEDED

F11 storyboard plan superseded by direct ImGui hotswap. ImGui menus
(`pdgui_menu_*.cpp`) are the sole menu system (P10 D5.7 complete S184). All
254 dialogs ported across Batches 0 / 1 / 2 / 3 / 4 / 5 / 6 / 6-polish / 7 / 8 /
10 / 11 / 12 (S192 – S209, 2026-04-11). Legacy `menuPush`/`menuPop` dialog
stack retained only as plumbing.

### D5: Settings / Graphics / QoL — 🔶 PHASE 3 DONE

- **Phase 0–2**: DONE pre-S191. UI Scaling (S97), controller bindings, radial
  menu.
- **Phase 3 (menu replacement)**: ✅ **COMPLETE 2026-04-11**. All 254 dialogs
  ported to ImGui — 13 batches S192 – S209.
- **Phase 4 (themes)**: 🔶 Partial. Theme loader + base-game template mod
  (S196). Theme editor shipped (B-130 rewrite S208 — native `BeginPopupModal`).
  Mod themes auto-rescan on mod apply (Issue 2/8 S253). Nineslice pipeline
  infrastructure shipped.
- **Phase 5 (lobby scene)**: 📋 Planned — player portraits, connected player
  avatars, character preview.
- **D5.1 Input ownership boundary**: ✅ DONE (S136).
- **D5.3 Pause menu**: ✅ DONE — ImGui pause + scorecard + Return to Lobby +
  Quit to Menu (S139 / S144).
- **D5.4 MP post-match scoreboard**: ✅ DONE (S139) — accuracy column, team
  section headers, dual exit buttons.
- **D5.7 OG menu removal**: ✅ DONE (S184).
- **D5.8 HUD score panel dock-below-minimap**: ✅ DONE (S221) — see archived
  `_archive/designs/hud-score-panel.md`.

### D6: Persistent Stats — 🔶 PARTIAL

- `port/src/playerstats.c` — string-keyed hash-table counters, JSON
  persistence to `$S/playerstats.json` (CODED S49).
- Integrated with `fsFullPath("$S/...")`; works even when `saveInit()` is
  delayed.
- `statIncrement()` accessor exported. Gameplay-site wire-in (`mpstats.c`,
  `mplayer.c`) partial — several events already recorded, others planned.
- Achievements = future query layer on top.

### D7: Discord Rich Presence — 📋 PLANNED

Activity API + join button.

### D8: NAT Traversal / LAN — ✅ DONE (S83)

See [network-architecture.md](network-architecture.md) §2.4 and
[designs/nat-traversal-architecture.md](designs/nat-traversal-architecture.md).
STUN client, SVC_ADDR_QUERY / CLC_ADDR_REPORT, symmetric hole-punch, relay
fallback, NAT diagnostics in debug menu.

### D9: Dedicated Server — ✅ MOSTLY DONE

Consolidated architecture in
[network-architecture.md](network-architecture.md) §3. Summary:

- **SPF-1 Hub / Room / Identity / Phonetic**: DONE (S47d).
- **SPF-2a Menu Mgr**: DONE (S48 / S49).
- **SPF-3 Lobby + Join-by-Code**: DONE (S49) — sentence connect codes, no raw
  IP in UI.
- **R-1 Foundation**: DONE — `hubGetMaxSlots()` + dedicated-server slot guard +
  IP scrub (B-28 / B-29 / B-30).
- **R-2 Room lifecycle**: DONE — demand-driven rooms, `leader_client_id`,
  `HUB_MAX_ROOMS = 16`, `HUB_MAX_CLIENTS = 32`.
- **R-3 Room sync protocol**: DONE (S143) — `SVC_ROOM_LIST 0x75` +
  `SVC_ROOM_UPDATE 0x76` + `SVC_ROOM_ASSIGN 0x77` + `CLC_ROOM_JOIN 0x0A` +
  `CLC_ROOM_LEAVE 0x0B`.
- **R-4 Match start**: DONE (S143 base + S253 additives) — `CLC_ROOM_START 0x0F`
  room-scoped; S253 added `SVC_ROOM_SETTINGS 0x78` + `SVC_ROOM_PLAYLIST 0x79` +
  `CLC_ROOM_SETTINGS_UPDATE 0x13` + `CLC_ROOM_PLAYLIST_UPDATE 0x14` for
  leader-side mutations. CLC_ROOM_KICK / TRANSFER operator ops still planned.
- **R-5 Server GUI redesign**: 📋 Planned — Players + Rooms panels, operator
  actions (Move / Kick / Set Leader / Close Room).
- **J-1 / J-2 / J-3 / J-4 / J-5**: ALL DONE.
- **Remaining**: Combat Sim stage selection (currently hardcoded in several
  places), Quick Play auto-launch button, SVC_LOBBY_LEADER broadcast on leader
  change, R-5 server GUI redesign, L-5 dedicated Campaign / Counter-Op setup
  screen, L-6 drop-in prompt on client side.

### D13: Update System — ✅ DONE (S245 FIX-F)

All source files written (S8 – S11). Semantic versioning, GitHub API, SHA-256,
self-replace, save migration, ImGui UI, dual-tag releases, two channels. S245
FIX-F closed the rough edges: (1) `curlGet()` returns HTTP code; 403 classified
as rate-limit with user-visible message; non-JSON classified as wrong-endpoint.
(2) `fsFullPath("$E/")` fallback when `detectExePath()` yields empty
`installDir`. (3) Extracted `PerfectDark.exe` checked for existence AND
minimum 1 MB size before self-replace. B-99 closed. Full design in
[update-system.md](update-system.md).

### D14a: Counter-Op Mode — 📋 PLANNED (v0.6.0)

NPC possession mechanic. Room-type support (CAMPAIGN with role-assign) already
in [network-architecture.md](network-architecture.md) §4. Per-session
role-assign UI (L-5 Counter-Op Setup Screen) planned but not built.

### D14b: Mod Distribution — ✅ DONE

Network distribution protocol (D3R-9), mod-pack export/import (D3R-10),
A-7 mod-audio network sync, S-9 skin network sync all shipped 2026-04-12.
Protocol bumped v20 → v21 → … → v34 over the distribution pipeline evolution.

### D15: Map Editor / Char Creator / Skins — 🔶 PARTIAL

- **Skin Editor (S-1 → S-9)**: ✅ DONE 2026-04-12 — canvas, live 3D preview,
  tools, save-as-mod, image import, quantize/dither, blend modes, UV wireframe,
  network sync. Detail: `daily-logs/2026-04-12.md`.
- **Mod Map Import Pipeline**: ✅ DONE 2026-04-13 — PD-native importer
  (`mapimport.c/h` 6-stage pipeline), Modding Hub UI tab, smoke sweep (M-7.x).
  Detail: session-log S240 / S244 / S246.
- **Level Editor (Forge)**: 📋 Planned v1.0.0. Separate main-menu entry.
- **Character Creator / Bot Customizer**: ✅ DONE (D3R-8 bot customizer; also
  Skin Editor covers skins).

### D16: Master Server — 📋 PLANNED

Full design in [network-architecture.md](network-architecture.md) §7. 4-phase
plan (D16a – D16d, ~750 LOC total). Nothing in current code needs changes to
prepare.

### MSP: Match Startup Pipeline — ✅ ALL DONE

- Phases A – F: DONE (S84 – S90). 8-phase match-startup sequence (Gather →
  Sync).
- SA-1 – SA-7: DONE (S91 – S97). Session Catalog + Modular API.
- Manifest Lifecycle Sprint Phases 0 – 6: DONE (S110 – S115).
- L5 Match Lifecycle Major: DONE (S241) — co-op manifest pipeline +
  CLC_STAGE_READY + protocol v35 + match_seed in SVC_STAGE_START.
- MSP contributors: [designs/match-startup-pipeline.md](designs/match-startup-pipeline.md),
  [designs/manifest-architecture.md](designs/manifest-architecture.md),
  [designs/session-catalog-and-modular-api.md](designs/session-catalog-and-modular-api.md).

### D-MEM: Memory Modernization — 🔶 M0–M1 + MEM-1/2/3 DONE

- **M0**: Diagnostic log cleanup → LOG_VERBOSE. ✅
- **M1**: `memsizes.h` created, 30+ named constants, 8 files converted. ✅
  (~100 ALIGN16 wrappers remain as M4.)
- **MEM-1**: `asset_load_state_t` + 4 fields added to `asset_entry_t`. ✅
- **MEM-2**: `assetCatalogLoad()` / `assetCatalogUnload()` — allocate / free
  `loaded_data`. ✅
- **MEM-3**: `ref_count` acquire / release + eviction policy. ✅
- **M2**: Stack→heap promotion (`pak.c` 16 KB, `texdecompress.c` 12 KB,
  `menuitem.c` 24 KB). 📋 Not started.
- **M3**: IS4MB ternary collapse (107 dead branches). 📋 Not started.
- **M4**: ALIGN16 strip (119 wrappers, 39 files). 📋 Not started.
- **M5**: Separate pool regions (architectural — eliminates overlap risk).
  📋 Not started.
- **M6**: Thread safety (mutex on `mempAlloc` / `mempFree`). 📋 Not started.

Full plan in [memory-modernization.md](memory-modernization.md).

### D-STAGE: Stage Decoupling — ✅ ALL 3 PHASES DONE (S47c)

- **Phase 1 Safety Net**: ✅ DONE (S23). Bounds checks at all known access
  points.
- **Phase 2 Dynamic Table**: ✅ DONE (S47c). Heap-allocated `g_Stages`,
  `g_NumStages`, `stageTableInit()`, `stageGetEntry()`, `stageTableAppend()`.
- **Phase 3 Domain Separation**: ✅ DONE (S47c). `soloStageGetIndex()` lookup,
  bounds guards in `endscreen.c` + `mainmenu.c`.

See [constraints.md](constraints.md) → Index Domain Warning.

### B-12: Dynamic Participant System — 🔶 PHASE 1–2 DONE (S47b)

- **Phase 1 Parallel Pool**: ✅ DONE (S26). `participant.h/c`, heap-allocated
  pool (capacity `MAX_MPCHRS = 40`), parallel sync hooks.
- **Phase 2 Callsite Migration**: ✅ DONE (S47b). 7 files, ~25 mplayer.c sites
  + setup.c + challenge.c + filemgr.c + matchsetup.c. `mpAddParticipantAt()`
  API.
- **Phase 3 Remove chrslots**: 📋 NEXT. Delete `u64 chrslots` field, legacy
  shims, `BOT_SLOT_OFFSET`. Protocol bump next v36. Target: v0.2.0 release.

### Master Orchestration Plan (2026-04-13) — ✅ L0–L7 COMPLETE

Full plan (now archived): `_archive/designs/2026-04-13/master-orchestration-plan-2026-04-13.md`.

Layer-by-layer status (all DONE unless noted):

- **L0**: L0-BUILD (ccache warm-build), L0-LINK (server link verify)
- **F-0.1 / 0.2 / 0.3 / 0.4**: campaign language-bank shadow / FR weapon
  context / game-over dead code / stale-manifest on return-to-room
- **FIX-A**: chr tick isolation + lifetime hardening (A.1 – A.4; B-112 / B-126
  MITIGATED)
- **L1-1**: clear `g_ClientManifest` on match end
- **FIX-B.2**: graceful fallback for missing models
- **FIX-B.1**: deep manifest scanner (cinematics + AI scripts) — 📋 **OPEN**
- **L1-2 / 3 / 4 / 5**: periodic score broadcast / HUD gate during endscreen /
  clear co-op netclient / NET_RESYNC_FLAG_SCORES in initial resync
- **F-1.1 / 2 / 3 / 4**: pause SDL warp removed / `pdguiSoloMissionReset()` /
  ad-hoc menus context audit / redundant SDL calls cleanup
- **F-2.1 / 2**: arena collapsible sections / solo pause k_Btns resolved
- **F-3.1 / 2 / 3**: duplicate-push rejection / pop-underflow logging / B-92
  deferred flush review
- **FIX-C.1 / 2 / 3**: GBI state reset (B-128 systemic defense) / Skedar Ruins
  investigation / menu opacity stacking
- **FIX-G**: mission completion counters / SeparatorText headers
- **FIX-F**: updater robustness (D13 closeout)

---

## Dependency Graph

```
DONE ── D1 (N64 strip) ── D3R (component mods) ── D3R-1..11
          │
          ├── D-STAGE (all 3 phases DONE)
          │
          ├── B-12 Phase 1–2 DONE ─→ Phase 3 (remove chrslots, v0.2.0)
          │
          ├── MSP all phases DONE (A–F + SA-1..7 + ML 0–6 + L5)
          │
          ├── D8 NAT traversal DONE
          │
          ├── D9 dedicated server:
          │     ├── SPF-1/2a/3 DONE
          │     ├── R-1/2/3 DONE
          │     ├── R-4 protocol DONE (S143 + S253)
          │     ├── R-5 server GUI redesign — PLANNED
          │     └── → D16 master server (post content tools)
          │
          ├── D5 Phase 3 DONE; Phase 4 partial; Phase 5 planned
          │
          ├── D13 update system DONE (S245 FIX-F)
          │
          ├── D14b mod distribution DONE (D3R-9/10 + A-7 + S-9)
          │
          ├── D15:
          │     ├── Skin Editor DONE (S-1..S-9)
          │     ├── Map Import Pipeline DONE (L3)
          │     └── Level Editor — PLANNED (v0.5.0 / v1.0.0)
          │
          └── D-MEM: M0/M1 + MEM-1/2/3 DONE; M2–M6 not started

PLANNED (by release target):
  v0.1.0 "Foundation"  ── D5 Phase 4 / 5 finish, B-141 audio root cause
  v0.2.0 "Connected"   ── B-12 Phase 3, R-5 server GUI, stats wire-in
  v0.3.0 "Community"   ── L-5 Campaign/Counter-Op setup, L-6 drop-in
  v0.4.0 "Federation"  ── D16 master server, mesh networking
  v0.5.0 "Studio"      ── D15 level editor (Forge)
  v0.6.0 "Spectacle"   ── D14a Counter-Op mode, D10 spectator, D12 co-op polish
  v1.0.0 "Release"     ── D6 stats, D7 Discord, accessibility, audio polish
```

See [roadmap.md](roadmap.md) for release milestones.
