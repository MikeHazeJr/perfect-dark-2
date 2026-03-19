# Modernization Roadmap

## Status: D1 DONE, D2 PARTIAL, D3 IN PROGRESS
Last updated: 2026-03-18

## Completed

### Phase D1: PC-Only Cleanup — Remove N64 Guards (DONE)
672 platform guards removed across 114+ files. Zero `PLATFORM_N64` references remain.

### Phase D2a: Character Select Screen Redesign (DONE)
Scrollable `MENUITEMTYPE_LIST` for body selection, live 3D model preview, integrated-head detection, null model protection.
Files: src/game/mplayer/setup.c, src/game/menu.c, src/game/player.c

### Phase D3d: ImGui Menu System Foundation (DONE — rendering verified 2026-03-17)
- Dear ImGui v1.91.8 vendored in `port/fast3d/imgui/`
- pdgui_backend.cpp: init, frame lifecycle, event handling, F12 toggle, input isolation, mouse grab
- pdgui_style.cpp: pixel-accurate shimmer math ported from menugfx.c, palette system (7 themes), PD-authentic style derivation
- pdgui_debugmenu.cpp: performance, network state, memory diagnostics, theme switcher, game controls
- See context/imgui.md for full details

## In Progress

### Phase D2: Jump Polish & Bot Jump AI
**Player jump**: Capsule collision system implemented and integrated (see collision.md). Stationary jumping confirmed working. Full movement testing pending.

**Bot jump AI** (not started): Reactive jump (knee-height raycast), evasive jump (difficulty-scaled), pathfinding (jump-required nav nodes).
Files: bot.c, botact.c, bondwalk.c

**Custom simulant types** (not started): JSON-defined personality types, boss simulants.
Files: bot.c, botact.c, setup.c, modmgr.c

### Phase D3: Mod Manager Framework
**D3a — Core** (DONE): modmgr.c/h, scan `mods/`, read mod.json, registry, enable/disable, config persistence.
**D3b — Dynamic Tables** (DONE): Shadow arrays g_ModBodies[64], g_ModHeads[64], g_ModArenas[64]. All accessors converted.
**D3c — fs.c Refactor** (DONE): fsFullPath uses modmgrResolvePath, first-match-wins iteration.
**D3d — ImGui Foundation** (DONE): See above.
**D3e — Mod Menu** (NEXT): Replace demo window with real mod manager screen. Requires dynmenu.c fluent builder API + modmenu.c.
**D3f — Network Manifest** (PLANNED): Mod list serialization, mismatch rejection on connect.
**D3g — Cleanup** (PLANNED): Remove remaining g_ModNum references, fix Dr. Carroll sentinel.

## Planned

### Phase D4: Menu Storyboard & ImGui Migration (ADR-001)
Controller-navigable storyboard (F11) for building, previewing, and rating all 113 game menus. Catalog on left, preview on right. X toggles OLD/NEW render. Y cycles quality rating (Good/Fine/Incomplete/Redo). LB/RB cycles themes. Full spec in context/menu-storyboard.md.

**D4a — Storyboard Shell**: pdgui_storyboard.cpp/h, F11 toggle, catalog panel, controller nav, rating JSON persistence.
**D4b — OLD Mode**: Capture original PD dialog to offscreen FBO, composite into ImGui preview area. Stubbed handlers.
**D4c — Component Library**: pdgui_menubuilder.cpp/h. PdDialog (shimmer), PdMenuItem, PdCheckbox, PdSlider, PdDropdown, PdListGroup. All palette-driven, no hardcoded colors. Dialog tint blending for type-awareness on custom themes.
**D4d — Priority Menus**: Build NEW ImGui versions of 10-15 highest-visibility menus (endscreen, pause, game setup, extended options).
**D4e — Complete Coverage**: Remaining ~100 menus, category by category, guided by quality ratings.
**D4f — Migration Workflow**: Wire NEW menus into game. `g_PdUseImGuiMenus` toggle. Gradual rollout: Good-rated first.
**D4g — Cleanup**: Remove OLD rendering paths for fully migrated menus. `#ifdef PD_DEV_PREVIEW` for storyboard itself.

### Phase D5: Settings, Graphics & QoL
FOV slider (60-110°), graphics settings (resolution, fullscreen, VSync), controls/bindings (rebindable), audio settings.
Depends on: D4 (dynmenu.c, pdgui_style.c)

### Phase D6: Persistent Stats & Post-Game Scorecard
stats.c (JSON/SQLite), hook into mpstatsRecordDeath/mpEndMatch. Post-game scorecard (ImGui). Lifetime stats viewer.
Depends on: D4

### Phase D7: Discord Rich Presence
discord-rpc or GameSDK Activity API. States: menu, match (map/mode/score), co-op (mission/difficulty). Join button hooks D8.
Depends on: nothing (can do after D6)

### Phase D8: NAT Traversal & LAN Discovery
netlan.c (UDP broadcast, LAN browser), natstun.c (STUN binding), UDP hole punching, relay fallback.
Depends on: D4

### Phase D9: Dedicated Server
`--dedicated` CLI, skip video/audio/input init. netdedicated.c stdin console. server.cfg. Optional ImGui server manager GUI.
Depends on: D8

### Phase D10: Spectator Mode
Spectator client flag (receives all state, sends no input). Free-camera and follow-cam modes. ImGui HUD overlay.
Depends on: D9

### Phase D11: Simulant Creator
simcreator.c (ImGui): sliders/dropdowns for reaction time, accuracy, aggression, etc. Body/head selector with 3D preview. JSON presets. Boss simulants.
Depends on: D2d + D4

### Phase D12: Co-op Polish
Cutscene camera sync (anim_id in SVC_CUTSCENE). SVC_INVENTORY_SYNC on reconnect. Dynamic NPC spawning (SVC_NPC_SPAWN).
Depends on: nothing (anytime after networking stable)

### Phase D13: Update System
version.h BUILD_VERSION, GitHub releases API check, SHA-256 verified download, preserve saves/config/mods. ImGui update screen.
Depends on: D9 + D4

### Phase D14: Mod Distribution & Counter-Op
ENet channel 3 file transfer (LZ4, 16KB chunks, SHA-256, 50MB cap). Counter-operative mode (NPC possession).
Depends on: D3f + D4

### Phase D15: Map Editor & Character Creator
modpacker.py, modconvert.py. Props list / level inspector (ships early as dev tool). Full map editor. Character creator with 3D import.
Depends on: D14 + D11

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
  └── D3 (Mod Manager) ─── IN PROGRESS
        ├── D3a-D3d ─── DONE
        ├── D3e (Mod Menu) ─── NEXT
        ├── D3f (Network Manifest) ─── planned
        └── D3g (Cleanup) ─── planned
              │
              └── D4 (Preview System) ─── needs D3e
                    │
                    ├── D5 (Settings/FOV)
                    │     └── D6 (Stats/Scorecard)
                    │           └── D7 (Discord RP)
                    │
                    ├── D8 (NAT Traversal)
                    │     └── D9 (Dedicated Server)
                    │           └── D10 (Spectator)
                    │
                    ├── D11 (Simulant Creator) ─── needs D2d + D4
                    ├── D12 (Co-op Polish) ─── independent
                    ├── D13 (Update System) ─── needs D9 + D4
                    └── D14 (Mod Distribution) ─── needs D3f + D4
                          └── D15 (Map Editor) ─── needs D14 + D11
```

Flexible slots: D7 (Discord RP), D12 (Co-op Polish), Props inspector from D15.

## Outstanding TODOs

| ID | Issue | File | Priority |
|----|-------|------|----------|
| TODO-1 | SDL2 and zlib still DLL (not static) | CMakeLists.txt | Low |
| TODO-2 | N64 strip netmode audit (72/77 confirmed, 5 fixed) | various | Low |
| TODO-4 | Verify model files: BODY_TESTCHR, BODY_PRESIDENT_CLONE | mod assets | Medium |
| TODO-5 | Dr. Carroll sentinel needs redesign before public mod release | mplayer.c | High (before D3g) |
| TODO-6 | g_ModNum still in romdata.c, mplayer.c, propobj.c, menutick.c | various | Medium (D3g) |
