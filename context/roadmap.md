# Modernization Roadmap

## Status: D1 DONE, D2 PARTIAL, D3 PARTIAL, D8 DONE, D9 LARGELY DONE, D13 IN PROGRESS, **D5 NEXT**
Last updated: 2026-04-03 (S131)

## Engine Modernization Vision

> The ROM should be used exclusively as a legacy asset provider. All gameplay, rendering, and engine systems will be rebuilt/replaced over time. The catalog is the foundation for this evolution.

| Stage | Description | Status |
|-------|-------------|--------|
| **Option A** (Current) | Catalog ID strings at all boundaries. ROM is sole asset provider. Legacy engine internals use integer indices. | **CODE COMPLETE (S130)** — protocol v27, all net_hash removed, SAVE-COMPAT stripped. Five systemic sweeps done (S131). Playtest pending. |
| **Option A+** (Next) | Catalog-backed data structures replace legacy arrays internally. `g_HeadsAndBodies[]` becomes catalog lookup. Integer index stops existing as a concept. | PLANNED |
| **Option B** (Long-term) | Catalog becomes provider-agnostic asset bus. ROM is one provider (legacy). Modern asset pipeline is another. Each catalog entry declares which provider. PBR materials, modern meshes, advanced physics — all new provider types. Mods ship modern assets that bypass the GBI path entirely. | VISION |

The catalog-as-single-source-of-truth principle means migration is incremental — upgrade assets one at a time, game runs with mixed legacy/modern content. fast3d stays for anything not yet upgraded.

## ⚡ PRIMARY WORKSTREAM: Catalog Universality Migration

> **Status**: PHASES A–G CODE COMPLETE (S130). Wire protocol v27. All net_hash removed. SAVE-COMPAT stripped. Playtest verification pending.
> **Governing spec**: `PD2_Catalog_Universality_Spec_v1.0.docx`
> **S130 result**: Full wire protocol migration to catalog ID strings. 4 critical/high bug fixes from comprehensive audit. 15 remaining findings (MEDIUM/LOW) tracked in tasks-current.md.

Phases A–G complete (code). Dependency order:

```
Phase A (Audit) ✓
  └── Phase B (API Hardening + Human-Readable IDs) ✓
        └── Phase C (Systematic Conversion) ✓
              └── Phase D (Server Manifest Model) ✓
Phase E (Menu Stack Architecture) ✓
Phase F (Spawn + Input Mode Hardening) ✓
  └── Phase G (Full Verification Pass) — code ✓, wire protocol v27 ✓, bug fixes ✓, playtest PENDING
```

**Phase G playtest success criteria**: zero CATALOG-ASSERT in logs, zero type=16, all MP game modes run to completion with bots, menu transitions clean (no tint bleed, no duplicate instances), spawn variety, bot unstick, spawn weapons present.

## Completed

### Phase D1: PC-Only Cleanup — Remove N64 Guards (DONE)
672 platform guards removed across 114+ files. Zero `PLATFORM_N64` references remain.

### Phase D2a: Character Select Screen Redesign (DONE)
Scrollable `MENUITEMTYPE_LIST` for body selection, live 3D model preview, integrated-head detection, null model protection.
Files: src/game/mplayer/setup.c, src/game/menu.c, src/game/player.c

### Phase D3a–D3d: Mod Manager Foundation (DONE)
- D3a — Core: modmgr.c/h, scan `mods/`, read mod.json, registry, enable/disable, config persistence.
- D3b — Dynamic Tables: Shadow arrays g_ModBodies[64], g_ModHeads[64], g_ModArenas[64]. All accessors converted.
- D3c — fs.c Refactor: fsFullPath uses modmgrResolvePath, first-match-wins iteration.
- D3d — ImGui Foundation: Dear ImGui v1.91.8 vendored, pdgui_backend, pdgui_style (shimmer, palette system), pdgui_debugmenu. See context/imgui.md.

### Phase D9: Dedicated Server (LARGELY DONE via Menu Phase 3)
Dedicated server process with CLI args (`--port`, `--maxclients`, `--gamemode`, `--headless`), signal handling, 4-panel ImGui server GUI, lobby state management, leader election, CLC_LOBBY_START protocol, server/client build separation. See context/server-architecture.md.

**Remaining D9 items:**
- ~~End-to-end playtest~~ **DONE (S81)** — J-1 verified: connect code → CLSTATE_LOBBY → match loads → runs → ends
- Protocol v27 (S130): all net_hash removed from wire, catalog ID strings everywhere
- ~~Combat Sim stage selection (currently hardcoded to Complex)~~ **FIXED (S128)** — stage_id in matchconfig, bridge API string-native
- Authoritative leader broadcast (SVC_LOBBY_LEADER on leader change — handlers written, not yet called from lobbyUpdate)
- "Quick Play" button (auto-launch server subprocess + connect to localhost)
- ~~B-51/B-52/B-53: bot visibility, weapon pickup, door interaction~~ **ALL FIXED (S90)** — bot configs via SVC_STAGE_START + scenarioInitProps on client; protocol v25

### Phase D4: Menu Storyboard (SUPERSEDED)
The F11 storyboard catalog (D4a/D4b) was the original plan, but menus are being built directly through the ImGui hotswap system instead. The component library (D4c) has evolved organically. The remaining useful sub-phases (priority menus, complete coverage, migration workflow, cleanup) continue as ongoing work alongside other phases rather than as a blocking dependency.

**Still relevant (absorbed into ongoing work):**
- D4c — Component Library: PdDialog, PdMenuItem, PdCheckbox, PdSlider, PdDropdown, PdListGroup. Palette-driven, no hardcoded colors.
- D4d — Priority Menus: endscreen, pause, game setup, extended options.
- D4e — Complete Coverage: remaining ~100 menus, built as needed.
- D4f/D4g — Migration & Cleanup: `g_PdUseImGuiMenus` toggle, gradual rollout, remove old paths.

## In Progress

### Phase D2: Jump Polish & Bot Jump AI
**Player jump**: Capsule collision system implemented and integrated (see collision.md). Stationary jumping confirmed working. Full movement testing pending.
**Bot jump AI** (not started): Reactive jump, evasive jump, pathfinding.
**Custom simulant types** (not started): JSON-defined personality types, boss simulants.
Files: bot.c, botact.c, bondwalk.c, setup.c, modmgr.c

### Phase D3e–D3g: Mod Manager Remaining
- D3e — Mod Menu (NEXT): Replace demo window with real mod manager screen.
- D3f — Network Manifest: Mod list serialization, mismatch rejection on connect.
- D3g — Cleanup: Remove remaining g_ModNum references, fix Dr. Carroll sentinel.

## Planned — Priority Order

The following reflects Mike's priority for implementation. Phases are listed in intended build order. Dependencies have been loosened since D4 is no longer a hard blocker — ImGui menus are built directly as needed.

### 1. Phase D5: Settings, Graphics & QoL ⟵ **NEXT UP**
FOV slider (60-110°), graphics settings (resolution, fullscreen, VSync), controls/bindings (rebindable). Audio: three independent volume layers — Music, Gameplay (SFX/weapons/footsteps/environment), UI (menu sounds/notifications) — each with its own slider, plus a master volume. Requires audio bus routing in the mixer.
**Why first**: High bang-for-buck. Makes it feel like a real PC game. Infrastructure already exists.
**Plan file**: [d5-settings-plan.md](d5-settings-plan.md)

### 2. Phase D13: Update System — IN PROGRESS (code written, needs build test)
Semantic versioning (MAJOR.MINOR.PATCH + dev channel), GitHub Releases API via libcurl, SHA-256 verified download, rename-on-restart self-replacement, save migration framework, ImGui update notification + version picker, separate client/server versioning, release channels (Stable/Dev). See context/update-system.md.
**Status**: All code written (2026-03-20). Needs: libcurl MSYS2 install, compile test, first GitHub release for end-to-end verification.
**Why early**: Essential for distributing builds. Once players have the game, you need a way to push fixes.

### 3. Phase D14a: Counter-Operative Mode
NPC possession mechanic — second player takes control of NPCs to oppose the solo player in co-op missions. Net protocol extensions for possession handoff.
**Why before mod distribution**: Counter-Op is a marquee gameplay feature. Mod distribution (D14b) can wait since all current mods ship in the base build.

### 4. Phase D15: Map Editor, Character Creator & Skin System
modpacker.py, modconvert.py. Props list / level inspector (ships early as dev tool). Full map editor. Character creator with 3D model import. **Texture/Skin system**: export a model's texture, import alternate textures as skins, switch between skins at the character select screen. Skins stored per-model, selectable in the character screen UI.
**Why high priority**: This is the differentiator. Community-created content (maps, characters, skins) is what gives the project long-term life.

### 5. Phase D16: Master Server / Server Pool
Lightweight master server process maintaining a live registry of dedicated game servers. Servers register + heartbeat via simple UDP. Clients query for a browsable server list, connect directly via existing ENet protocol. ~750 LOC total. Purely additive. Full plan in context/master-server-plan.md.
**Why after content tools**: Players need something worth playing before matchmaking infrastructure matters.

### 6. Phase D6: Persistent Stats & Post-Game Scorecard
stats.c (JSON/SQLite), hook into mpstatsRecordDeath/mpEndMatch. Post-game scorecard (ImGui). Lifetime stats viewer.

### 7. Phase D7: Discord Rich Presence
discord-rpc or GameSDK Activity API. States: menu, match (map/mode/score), co-op (mission/difficulty). Join button for easy matchmaking.

### 8. Phase D8: NAT Traversal & LAN Discovery — **DONE (S83)**
STUN client (`netstun.c`), query advertising via SVC_ADDR_QUERY/CLC_ADDR_REPORT, UDP hole punch handshake (`netholepunch.c`), relay fallback, NAT diagnostics in debug menu. Protocol v23. Connect code extended with 6-word port encoding. LAN discovery: deferred (UDP broadcast not yet implemented).

### 9. Phase D10: Spectator Mode
Spectator client flag (receives all state, sends no input). Free-camera and follow-cam modes. ImGui HUD overlay.

### 10. Phase D11: Simulant Creator
simcreator.c (ImGui): sliders/dropdowns for reaction time, accuracy, aggression, etc. Body/head selector with 3D preview. JSON presets. Boss simulants.

### 11. Phase D12: Co-op Polish
Cutscene camera sync (anim_id in SVC_CUTSCENE). SVC_INVENTORY_SYNC on reconnect. Dynamic NPC spawning (SVC_NPC_SPAWN).

### 12. Phase D14b: Mod Distribution
ENet channel 3 file transfer (LZ4, 16KB chunks, SHA-256, 50MB cap). Auto-sync mods to connecting clients.

## Dependency Graph

```
D1 (N64 Strip) ─── DONE
  │
  ├── D2 (Jump/Bots) ─── IN PROGRESS
  │     ├── D2a (Char Select) ─── DONE
  │     ├── D2b (Capsule Collision) ─── testing
  │     ├── D2c (Bot Jump AI) ─── needs D2b
  │     └── D2d (Custom Simulants) ─── feeds D11
  │
  ├── D3 (Mod Manager) ─── PARTIAL
  │     ├── D3a-D3d ─── DONE
  │     ├── D3e (Mod Menu) ─── NEXT
  │     ├── D3f (Network Manifest) ─── planned
  │     └── D3g (Cleanup) ─── planned
  │
  ├── D4 (Menu Migration) ─── SUPERSEDED (ongoing, no longer blocks)
  │
  ├── D9 (Dedicated Server) ─── LARGELY DONE
  │     ├── D16 (Master Server) ─── after content tools
  │     └── D10 (Spectator)
  │
  ├── D13 (Update System) ─── IN PROGRESS (code written, build test needed)
  │
  └── Priority build order (loosely sequential):
        D5 (Settings/QoL)
        → D14a (Counter-Op)
        → D15 (Map Editor / Char Creator / Skins)
        → D16 (Master Server)
        → D6 (Stats) → D7 (Discord RP)
        → D8 (NAT) ─── DONE (S83) → D10 (Spectator)
        → D11 (Sim Creator) → D12 (Co-op Polish)
        → D14b (Mod Distribution)
```

Flexible slots: D7 (Discord RP), D12 (Co-op Polish) — can be inserted anywhere.

## Outstanding TODOs

| ID | Issue | File | Priority |
|----|-------|------|----------|
| TODO-1 | SDL2 and zlib still DLL (not static) | CMakeLists.txt | Low |
| TODO-2 | N64 strip netmode audit (72/77 confirmed, 5 fixed) | various | Low |
| TODO-4 | Verify model files: BODY_TESTCHR, BODY_PRESIDENT_CLONE | mod assets | Medium |
| TODO-5 | Dr. Carroll sentinel needs redesign before public mod release | mplayer.c | High (before D3g) |
| TODO-6 | g_ModNum still in romdata.c, mplayer.c, propobj.c, menutick.c | various | Medium (D3g) |
