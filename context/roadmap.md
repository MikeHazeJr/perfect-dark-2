# Modernization Roadmap

## Status: PLANNING
Phases D1 and D2a complete. D2 partially complete (capsule collision done, bot jump AI pending). Remaining phases are planned but not started.

## Completed

### Phase D1: PC-Only Cleanup — Remove N64 Guards (DONE)
672 platform guards removed across 114+ files. Three categories executed:
- Dead code removal (~186 `#ifdef PLATFORM_N64` blocks)
- Unconditional promotion (~485 `#ifndef PLATFORM_N64` blocks)
- Careful review (~170 in critical files — verified PC branch completeness)

Post-fix: Cleaned orphaned preprocessor directives (12 orphaned `#endif`, 3 orphaned `#else` with dead code, 14 orphaned `#else` across 11 files, bg.c function corruption, menu.c crash path). Zero `PLATFORM_N64` references remain.

### Phase D2a: Character Select Screen Redesign (DONE)
- Scrollable `MENUITEMTYPE_LIST` for body selection (120px wide, 136px tall)
- Live 3D model preview on cursor movement (MENUOP_LISTITEMFOCUS)
- Head carousel kept for optional head-only changes
- Integrated-head body detection (unk00_01=1) — locks carousel
- Null model protection in menu.c and player.c
- Files: src/game/mplayer/setup.c, src/game/menu.c, src/game/player.c

## In Progress

### Phase D2: Jump Polish & Bot Jump AI
**Player jump**: Capsule collision system implemented and integrated (see collision.md). Stationary jumping confirmed working. Full movement testing pending.

**Bot jump AI** (not started):
- Reactive jumping: Forward raycast at knee height for obstacles
- Evasive jumping: Random chance under fire (0%/15%/30% by difficulty)
- Pathfinding: Jump-required nav node tags
- Files: bot.c, botact.c, bondwalk.c

**Custom simulant types** (not started):
- JSON-defined simulant personality types with custom stats and character models
- Boss simulants with scaled hitboxes and custom behaviors
- Files: bot.c, botact.c, setup.c, modmgr.c

## Planned

### Phase D3: Mod Manager Framework
**Goal**: Dynamic mod loading with runtime-extensible asset tables.

**Current state**: Hardcoded mod system — `MOD_NORMAL/GEX/KAKARIKO/DARKNOON/GOLDFINGER_64` constants, static `g_MpBodies[63]`/`g_MpHeads[75]`/`g_MpArenas[71]` arrays, per-mod CLI args.

**New system**:
- `mod.json` manifest format (id, name, version, author, content declarations)
- `port/src/modmgr.c` — scans `mods/` directory, reads manifests, registers assets
- Dynamic asset tables: realloc-based arrays with stable string IDs
- Network compatibility: mod manifest exchange on connect, mismatch rejection
- Legacy support: fallback from mod.json to modconfig.txt
- Migration: Convert all 5 existing mods to new format, remove hardcoded constants

**Key files**: modmgr.c (new), modmgr.h (new), mod.c (refactor), fs.c (refactor), constants.h (remove MOD_* defines), mplayer.c/setup.c/stagetable.c (dynamic tables)

### Phase D4: NAT Traversal & Direct Connect
**Goal**: Connect without Hamachi or port forwarding.

- STUN + UDP hole punching via public STUN servers
- Signaling options: manual exchange, lightweight relay server, Discord Rich Presence
- LAN discovery: UDP broadcast on port 27101 every 2 seconds
- Fallback: Port forwarding instructions on symmetric NAT failure

**Key files**: natstun.c (new), netlan.c (new), netmenu.c (LAN browser)

### Phase D5: Dedicated Server
**Goal**: Headless server mode for persistent hosting.

- `--dedicated` CLI flag, skip video/audio/input init
- stdin command console: map, kick, status, rotate, say, quit
- Config: server.cfg or CLI args (map, mode, max players, rotation)
- Stub systems: no-op render, no local player, no input polling
- Future: GUI server manager (web interface or standalone app)

**Key files**: main.c, video.c (stub), audio.c (stub), netdedicated.c (new)

### Phase D6: Update System
**Goal**: Seamless check-and-apply updates.

- Version tracking: `BUILD_VERSION` in version.h, displayed in menu
- Update check: GitHub releases API on launch
- Delta updates: download zip, compare hashes, replace changed files
- Preserve: saves/, config.cfg, user mods
- Save data versioning with migration functions

**Key files**: version.h (new), update.c (new), gamefile.c (save versioning)

### Phase D7: Mod Distribution & Counter-Op
**Goal**: Server sends missing mods to clients. Counter-operative mode.

**Mod transfer**:
- Dedicated ENet channel 3 for file transfer
- LZ4 compression, 16KB chunks, SHA-256 verification
- Progress bar in joining UI, 50MB per mod limit

**Counter-operative mode**:
- Player 2 possesses NPCs instead of controlling co-op buddy
- NPC authority transfer message
- Respawn into new NPC on death
- Scoring: counter-op player vs bond

**Key files**: netmod.c (new), net.c (counter-op authority), chraction.c (NPC possession)

### Phase D8: Map Editor & Custom Characters
**Goal**: Standalone tools for content creation.

- Map editor: Import geometry, visual prop placement, AI path editor, export as mod pack
- Character creator: Import 3D models (OBJ/FBX), texture assignment, export as mod body/head
- Content pipeline: modpacker.py (validate/package), modconvert.py (format conversion)

**Key files**: tools/mapeditor/ (new), tools/modpacker.py (new), tools/modconvert.py (new)

## Collision & Physics Modernization (Cross-cutting)

These upgrades span multiple phases and represent the long-term direction:

1. **Horizontal capsule sweep** (extends D2): Wall sliding, fast-move clipping prevention
2. **Spatial partitioning** (new): Replace room-based brute-force geo iteration with BVH/octree
3. **Per-triangle collision** (new): Full capsule-vs-triangle intersection for precise contact normals
4. **Animation-driven movement** (new): Replace fixed velocity constants with animation-synchronized speeds

## Dependencies Between Phases

```
D1 (N64 Strip) ──── DONE
  │
  ├── D2 (Jump/Bots) ──── IN PROGRESS
  │     └── D2a (Char Select) ──── DONE
  │
  ├── D3 (Mod Manager) ──── planned
  │     │
  │     ├── D6 (Update System) ──── planned
  │     │
  │     └── D7 (Mod Distribution) ──── planned (requires D3)
  │           └── D8 (Map Editor) ──── planned (requires D3+D7)
  │
  ├── D4 (NAT Traversal) ──── planned (independent)
  │
  └── D5 (Dedicated Server) ──── planned (independent)
```

## Outstanding TODOs
- TODO-1: Investigate SDL2/zlib static linking (lower priority)
- TODO-2: Comprehensive N64 strip netmode audit (72/77 patterns confirmed, 5 fixed)
- TODO-4: Verify model files for mod characters (BODY_TESTCHR, BODY_PRESIDENT_CLONE)

## Session Runtime Fixes (Post-D1)
Twelve fixes applied after Phase D1 compilation (FIX-1 through FIX-12). See build.md and tasks.md for details. Most need testing confirmation.
