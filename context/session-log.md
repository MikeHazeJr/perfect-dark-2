# Session Log

Reverse-chronological. Each entry is a self-contained summary of what happened.

---

## Session 6 — 2026-03-21

**Focus**: strncpy audit, 32-slot bot expansion, async bot ticking

### What Was Done
- Re-applied netmsg.c fixes lost during compaction + full propagation sweep (27 sites, 9 files)
- Created ADR-001 architecture audit document and session-log.md
- **32-character slot expansion**: MAX_BOTS 8→24, chrslots u16→u32, all masks updated
- Removed all MPFEATURE_8BOTS gates — all bot slots available on PC
- Fixed signed shift UB across 20 files (~60 call sites: `1 <<` → `1u <<`)
- Updated network protocol (SVC_STAGE_START chrslots u32, bumped to v19)
- Updated preprocess/misc.c N64 save format conversion for u32 chrslots
- Added async bot tick scheduler: distributes AI across frame groups for 9+ bots
- Updated matchsetup.c MATCH_MAX_SLOTS to MAX_MPCHRS, save format to use constants

### Commits
- `841ae31`: Static linking fix, build tool features, release overwrite
- `e0a8853`: strncpy null-termination across codebase (27 locations)
- `8c6e47a`: 32-character slot expansion (20 files)

### Decisions
- Unified slot model: 8 player bits + 24 bot bits in u32 chrslots
- Async AI: ≤8 bots = 60Hz, 9-16 = 30Hz, 17-24 = 20Hz per bot
- Protocol version bumped to 19 (breaking change, chrslots wire format)

### Previous Focus

### What Was Done
- Re-applied netmsg.c fixes lost during context compaction (3 strncpy + 1 strcpy→snprintf)
- **Propagation check**: Scanned entire port/ codebase for the same strncpy class bug
  - Found 17 additional instances across 8 files (net.c, updater.c, modmgr.c, fs.c, libultra.c, config.c, input.c, optionsmenu.c)
  - Fixed all 17 with consistent `buf[SIZE - 1] = '\0'` pattern
- Created ADR-001 documenting the architecture audit findings
- Previous session's build tool changes committed: static linking fix, headless server, auto-stash, version increment buttons, release overwrite

### Files Modified
- `port/src/net/netmsg.c` — 3 strncpy + 1 strcpy fix
- `port/src/net/net.c` — 4 strncpy fixes
- `port/src/updater.c` — 2 strncpy fixes
- `port/src/modmgr.c` — 4 strncpy fixes
- `port/src/fs.c` — 5 strncpy fixes
- `port/src/libultra.c` — 3 strncpy fixes
- `port/src/config.c` — 3 strncpy fixes
- `port/src/input.c` — 1 strncpy fix
- `port/src/optionsmenu.c` — 2 strncpy fixes
- `context/ADR-001-lobby-multiplayer-architecture-audit.md` — NEW

### Decisions
- strncpy null-termination is now a project-wide standard; future code should prefer snprintf

---

## Sessions 4-5 — 2026-03-20

**Focus**: Architecture audit, build tool improvements, release tooling

### What Was Done
- Complete architecture audit of lobby/multiplayer systems
- Fixed CLC_MAP_VOTE_START ID collision (was 0x09, same as CLC_LOBBY_MODE → shifted to 0x0A)
- CMakeLists.txt: Fixed static linking path resolution (SDL2, zlib, libcurl)
- build-gui.ps1: Server headless launch, auto-stash branch switching, version increment buttons
- release.ps1: Release overwrite support, DLL warning cleanup
- Identified and documented 3 verified false positives in the audit

### Decisions
- Server launches headless by default (avoids OpenGL context contention)
- Auto-stash on branch switch with tagged restore
- Release overwrite: delete existing GitHub release + tags before recreating

---

## Session 3 — 2026-03-19

**Focus**: Phase 3 dedicated server, lobby system

### What Was Done
- Completed Phase 3: Dedicated-server-only multiplayer model
- New multiplayer menu (server browser, direct IP connect)
- Lobby system rewrite with leader election
- Lobby screen with game mode selection
- Server GUI (4-panel layout) and headless mode
- CLC_LOBBY_START protocol for match launching
- Cleanup: renamed "Network Game" to "Multiplayer", removed stale host menus

---

## Session 2 — 2026-03-18

**Focus**: ImGui debug menu, styling, build system

### What Was Done
- PD-authentic styling with pixel-accurate shimmer from menugfx.c
- 7 built-in palettes including Black & Gold
- F12 debug menu with mouse capture/release
- Build tool: colored progress bar, gated run buttons, process monitoring
- Font size 16pt → 24pt

---

## Session 1 — 2026-03-17

**Focus**: Menu system Phase 2 (agent create, delete, typed dialogs, network audit)

### What Was Done
- Agent Create screen with 3D character preview (FBO)
- Agent Select enhancements (contextual actions, delete confirmation)
- Typed dialog system (DANGER + SUCCESS)
- Network audit began (ENet protocol, message catalog)
- Skedar/DrCaroll mesh fix (duplicate array indices in g_MpBodies)
