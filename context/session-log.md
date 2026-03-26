# Session Log (Active)

> Recent sessions only. Archives: [1-6](sessions-01-06.md) . [7-13](sessions-07-13.md) . [14-21](sessions-14-21.md) . [22-46](sessions-22-46.md)
> Back to [index](README.md)

## Session 48 -- 2026-03-25

**Focus**: Dev Window overhaul, project cleanup, infrastructure hardening

### What Was Done

**Dev Window (dev-window.ps1)**:
- Fixed UI thread hang: git status moved to background runspace, then to Activated event
- NotesSaveTimer race condition fixed (no more dispose-in-tick)
- Font caching in paint handlers (no per-frame allocation)
- Tab background white strip eliminated (dark panel wrapper)
- Auth label: clickable button, opens `gh auth login` if unauthenticated
- GitHub + Folder buttons moved to main UI (bottom of Build tab)
- Two font size settings (Button + Detail) with live refresh
- Stable/Dev toggle checkbox for releases
- Documentation tab (split pane: file list + content reader, 30/70 ratio)
- Clean Build toggle button (beneath BUILD, wipes build dirs before configure)
- Post-build copy list configurable via settings
- Client/server status labels show exe existence on startup
- Latest release label shows tag + dev/stable + color
- Background runspaces now pass PATH for gh CLI access

**Release pipeline (release.ps1)**:
- All 7 PS7-only syntax violations fixed for PS5 compatibility
- All em dashes replaced with ASCII
- Unified release: single tag (v0.0.1) with both client + server attached
- Auto-overwrite existing releases (delete + recreate with sound notification)
- GIT_TERMINAL_PROMPT=0 in subprocess environment

**Project cleanup**:
- Deleted: 6 runbuild scripts, fix_endscreen, phase3 docs, context-recovery.skill, mods folder info, PROMPT.md, context.md (106KB monolithic), ROADMAP.md, pd-port-director-SKILL.md, CHANGES.md, old devtools (build-gui, playtest-dashboard, doc-reader + .bat launchers)
- Deleted: 4.3GB of abandoned Claude Code worktrees
- Created: UNRELEASED.md (player-facing changelog), dist/windows/icon.ico + icon.rc
- Session log archived (S22-46 to sessions-22-46.md, active trimmed to 229 lines)
- tasks-current.md cleaned (completed items removed)
- COWORK_START.md rewritten as lean bootstrap pointer

**Code fixes**:
- fs.c: data directory search priority fixed (exe dir first, then cwd, then AppData)
- romdata.c: creates data/ dir + README.txt when ROM missing, then opens correct folder
- .build-settings.json: ROM path updated to new project location

**Skill + context**:
- game-port-director skill updated with Sections 8-9 (design principles, tool patterns)
- Skill packaged as .skill for reinstallation
- Context canonical location documented in CLAUDE.md
- 6 memories saved (profile, event-driven, clean structure, no worktrees, ACK messages, no ambiguous intent)

### Decisions Made
- Event-driven over polling (standing principle)
- Unified release tag (v0.0.1) replaces split client/server tags
- context/ is canonical location, parent copies are convenience mirrors
- No worktrees: all code changes in working copy

### Bugs Noted
- B-18: Pink sky on Skedar Ruins (possible texture/clear color issue)

### Next Steps
- Collision system rewrite (mesh-based, capsule for movement, original for damage)
- S46b: Full asset catalog enumeration
- Multiplayer playtest target: host server, friend connects, 30-bot match

---

## Session 47d — 2026-03-24

**Focus**: SPF-1 — Server Platform Foundation (hub lifecycle, room system, identity, phonetic encoding)

### What Was Done

Implemented the server platform foundation layer on top of the existing ENet dedicated server.
Four new module pairs + wiring into server_main.c + server_gui.cpp tab bar.

**New files (8):**

1. **`port/include/phonetic.h`** / **`port/src/phonetic.c`** — CV syllable IP:port encoding.
   16 consonants × 4 vowels = 6 bits/syllable × 8 syllables = 48 bits (IPv4 + port).
   Format: `"BALE-GIFE-NOME-RIVA"` — shorter than word-based connect codes. Both coexist.
2. **`port/include/identity.h`** / **`port/src/identity.c`** — `pd-identity.dat` persistence.
   Magic `PDID`, version byte, 16-byte UUID (xorshift128 seeded from SDL perf counter + time),
   up to 4 profiles (name/head/body/flags). Validates on load, rebuilds default on corruption.
3. **`port/include/room.h`** / **`port/src/room.c`** — Room struct + 5-state lifecycle.
   Pool of 4 rooms. Room 0 permanently wraps the existing match lifecycle (never truly closes).
   States: LOBBY→LOADING→MATCH→POSTGAME→CLOSED. Transitions logged via `sysLogPrintf`.
4. **`port/include/hub.h`** / **`port/src/hub.c`** — Hub singleton owning rooms + identity.
   `hubTick()` reads `g_Lobby.inGame` each frame → drives room 0 state machine.
   One-frame POSTGAME bridge on match end. Derives hub state from aggregate room states.

**Modified files (3):**

5. **`port/src/server_main.c`** — Added `hubInit()` / `hubTick()` / `hubShutdown()` calls.
6. **`port/fast3d/server_gui.cpp`** — Middle panel converted to tabbed layout.
   "Server" tab: existing player list + match controls. "Hub" tab: hub state + room table
   with color-coded states. Log panel: HUB: prefix highlighted purple.
7. **`context/server-architecture.md`** — SPF-1 section added (hub/room diagram, phonetic,
   GUI changes, new file table).

**Commit**: `fb5450b feat(SPF-1): hub lifecycle, room system, player identity, phonetic encoding`

### Decisions Made

- **Backward compatibility**: Room 0 driven by `g_Lobby.inGame` observation — zero changes
  to `net.c` or `netlobby.c`. Existing single-match path unchanged.
- **Protocol**: v21 unchanged. No new ENet messages. Both phonetic and word connect codes
  remain available.
- **`HUB_MAX_CLIENTS`**: Defined directly in `room.h` (= 8) rather than including `net/net.h`
  to keep hub modules standalone and avoid the full game header chain.
- **Boolean fields**: Used `int` not `_Bool`/`bool` in new C modules (port/ files, but
  matching the project convention of `s32` for boolean-like values).
- **Room 0 persistence**: `roomDestroy()` on room 0 resets to LOBBY instead of CLOSED —
  room 0 is the permanent lounge for the existing server lifecycle.

### Dev Build Status

**UNVERIFIED** — Build environment broken in session (GCC TEMP path issue in sandbox).
`build-headless.ps1` TEMP/TMP fix committed. User to verify build from local environment.

### Session 47e Follow-up — 2026-03-24

**Focus**: Fix server build — SPF-1 symbols undefined in pd-server

**Root cause**: SRC_SERVER in CMakeLists.txt is a manually curated list; the 4 new SPF-1
files (hub.c, room.c, identity.c, phonetic.c) were not added when coded in S47d.
Client uses GLOB_RECURSE so it picked them up automatically; server did not.

**Fix**: Added 4 entries to SRC_SERVER block in CMakeLists.txt (lines 478–482).
Commit `c788486`. Pushed to dev.

**Build status**: Cannot verify in sandbox (GCC DLL loading issue — cc1.exe needs
libmpfr-6.dll via Windows PATH, not POSIX PATH). Run `.\devtools\build-headless.ps1 -Target server`
from PowerShell to confirm.

### Next Steps

- Run `.\devtools\build-headless.ps1 -Target server` from PowerShell to confirm fix
- Build and QC test SPF-1 modules (see qc-tests.md)
- SPF-2: Room federation / multi-room support
- D5: Settings persistence for server configuration

---

## Session 47b — 2026-03-24

**Focus**: B-12 Phase 2 — Migrate chrslots callsites to participant API

### What Was Done

Completed the Phase 2 migration of all chrslots bitmask read/write sites across 5 files.
Phase 1 bulk-sync calls (`mpParticipantsFromLegacyChrslots`) replaced with targeted
`mpAddParticipantAt`/`mpRemoveParticipant` at each write site.

**Key design established:**
- Pool capacity is `MAX_MPCHRS` (40), not the Phase 1 default 32
- Pool slot `i` == chrslots bit `i` (players 0–7, bots 8–39)
- `mpIsParticipantActive(i)` is a direct drop-in for `chrslots & (1ull << i)`
- New `mpAddParticipantAt(slot, type, ...)` API for exact-slot placement

**Files changed (7):**

1. **`src/include/game/mplayer/participant.h`** — Added `mpAddParticipantAt()` declaration
2. **`src/game/mplayer/participant.c`** — Added `mpAddParticipantAt()` impl; rewrote
   `mpParticipantsToLegacyChrslots` (slot index IS bit index) and
   `mpParticipantsFromLegacyChrslots` (use `mpAddParticipantAt` for exact placement)
3. **`src/game/mplayer/mplayer.c`** — ~25 sites: mpInit, match lifecycle, bot create/copy/
   remove, score, team assignment, name generation, save/load config and WAD
4. **`src/game/mplayer/setup.c`** — 10 sites: handicap CHECKHIDDEN, team loop ×3,
   bot slot UI, simulant name display, player file availability
5. **`src/game/challenge.c`** — Read check + fix `1u`→`1ull` write bug + add participant
   calls alongside chrslots writes in `challengePerformSanityChecks`
6. **`src/game/filemgr.c`** — 2 player-file presence checks
7. **`port/src/net/matchsetup.c`** — `mpClearAllParticipants()` + `mpAddParticipantAt`
   at each player/bot write site

**Commit**: `94a2b1e feat(B-12-P2): migrate chrslots callsites to participant API`

### Dev Build Status

**PASS** — `cmake --build --target pd` clean (exit 0). All 7 files compiled without errors.

### Decisions Made

- `challengeIsAvailableToAnyPlayer` reads `chrslots & 0x000F` as a bitmask for challenge
  availability computation — left as-is (no clean participant API equivalent, chrslots
  still dual-written in Phase 2)
- `mp0f18dec4` VERSION guard retained (PC builds are >= JPN_FINAL, always included)
- `setup.c` fixes applied via line-by-line PowerShell replace (Edit tool had CRLF mismatch)

### Next Steps

- B-12 Phase 3: Remove `chrslots` field + legacy shims + BOT_SLOT_OFFSET
- Protocol version bump to v21 (SVC_STAGE_START uses participant list)
- QC: in-game bot add/remove, match start/end, save/load bot config

---

## Session 47c — 2026-03-24

**Focus**: Stage Decoupling Phase 2 (Dynamic stage table) + Phase 3 (Index domain separation)

### What Was Done

**Phase 2 — Dynamic stage table** (7 files):

1. **`src/game/stagetable.c`** — Renamed static array to `s_StagesInit[]`, added heap pointer `g_Stages` + `g_NumStages`. `stageTableInit()` mallocs+memcpys. `stageGetEntry(index)` bounds-checked accessor. `stageTableAppend(entry)` realloc-based. Both `stageGetCurrent()` and `stageGetIndex()` rewritten to use `g_NumStages`. `soloStageGetIndex(stagenum)` iterates `g_SoloStages[0..NUM_SOLOSTAGES-1]`.
2. **`src/include/data.h`** — `extern struct stagetableentry *g_Stages` + `extern s32 g_NumStages` (was array).
3. **`src/include/game/stagetable.h`** — Full declaration set for all Phase 2 + 3 functions.
4. **`src/game/bg.c`** — `ARRAYCOUNT(g_Stages)` replaced with `g_NumStages` (2 occurrences).
5. **`port/src/assetcatalog_base.c`** — Removed local `extern struct stagetableentry g_Stages[]` (conflicted with pointer decl). Bounds check `idx >= 87` → `idx >= g_NumStages`.
6. **`port/src/main.c`** — Added `stageTableInit()` call before `assetCatalogRegisterBaseGame()`.

**Phase 3 — Index domain guards** (2 files):

7. **`src/game/endscreen.c`** — 9 guard sites: `endscreenMenuTitleRetryMission`, `endscreenMenuTitleNextMission`, `endscreenMenuTitleStageCompleted`, `endscreenMenuTextCurrentStageName3`, `endscreenMenuTitleStageFailed`, `endscreenHandleReplayPreviousMission` (underflow), `endscreenAdvance()` (overflow), `endscreenHandleReplayLastLevel`, `endscreenContinue` DEEPSEA (2 paths, both guarded).
8. **`src/game/mainmenu.c`** — 4 guard sites: `menuTextCurrentStageName`, `soloMenuTitleStageOverview`, `soloMenuTitlePauseStatus`, `isStageDifficultyUnlocked` (top guard returns true for out-of-range — mod stages treated as unlocked).

**Bonus fix**: Restored `src/game/mplayer/setup.c` and `src/game/setup.c` from commit `4704eab` after auto-commit `0a36981` corrupted them (all tabs replaced with literal `\t`). Pre-existing bug revealed by full rebuild.

### Decisions Made

- `soloStageGetIndex()` lives in `stagetable.c` (iterates `g_SoloStages[]`). It is the Phase 3 domain translation function.
- `isStageDifficultyUnlocked(stageindex < 0 || >= NUM_SOLOSTAGES)` returns `true` — mod stages are "unlocked" by definition (no solo-stage-based unlock system applies to them).
- `ARRAYCOUNT(g_Stages)` was eliminated. Any future code must use `g_NumStages`.

### Dev Build Status

**PASS** — `build-headless.ps1 -Target client` clean (exit 0). All modified files compiled without errors. Warnings in bg.c are pre-existing.

### Next Steps

- MEM-2: `assetCatalogLoad()` / `assetCatalogUnload()`
- MEM-1 build test: full cmake pass confirms `assetcatalog.h` struct changes are stable
- S46b: Full asset catalog enumeration (animations, SFX, textures)

---

## Session 47a — 2026-03-24

**Focus**: MEM-1 — Asset Catalog load state tracking fields

### What Was Done

Added lifecycle state tracking to `asset_entry_t` as the foundation for Phase D-MEM
memory management. This is purely additive — no existing behavior changes.

**Files changed (4 files):**

1. **`port/include/assetcatalog.h`** — Added `asset_load_state_t` enum
   (`REGISTERED`/`ENABLED`/`LOADED`/`ACTIVE`). Added `#define ASSET_REF_BUNDLED 0x7FFFFFFF`.
   Added 4 fields to `asset_entry_t`: `load_state`, `loaded_data`, `data_size_bytes`,
   `ref_count`. Added `assetCatalogGetLoadState()` and `assetCatalogSetLoadState()`
   declarations in new "Load State API (MEM-1)" section.

2. **`port/src/assetcatalog.c`** — `assetCatalogRegister()` initializes new fields:
   `ASSET_STATE_REGISTERED`, `loaded_data=NULL`, `data_size_bytes=0`, `ref_count=0`.
   `assetCatalogSetEnabled()` now advances `REGISTERED→ENABLED` on first enable.
   Added `assetCatalogGetLoadState()` and `assetCatalogSetLoadState()` implementations.

3. **`port/src/assetcatalog_base.c`** — All 4 bundled registration sites (stages, bodies,
   heads, arenas) now set `load_state=ASSET_STATE_LOADED` and `ref_count=ASSET_REF_BUNDLED`.

4. **`port/src/assetcatalog_base_extended.c`** — All 7 bundled registration sites (weapons,
   animations, textures, props, gamemodes, audio, HUD) now set `ASSET_STATE_LOADED` and
   `ref_count=ASSET_REF_BUNDLED`.

### Decisions Made

- `ASSET_REF_BUNDLED = 0x7FFFFFFF` (S32_MAX) as documented in MEM-1 spec.
- `REGISTERED→ENABLED` transition happens in `setEnabled(id, 1)`. If load_state is already
  LOADED or ACTIVE (bundled assets), setEnabled does not downgrade state.
- `assetCatalogSetLoadState()` is a raw setter — callers own the validity of transitions.
  Future eviction logic will use `ref_count` to guard bundled assets.
- `loaded_data` / `data_size_bytes` fields left at NULL/0 for all existing entries —
  wired for the future loader, not populated yet.

### Dev Build Status

- Syntax-check (MinGW gcc -fsyntax-only): **PASS** on all 3 modified .c files
- Full cmake build: needs Mike's `build-headless.ps1` run (cmake env not available in session)

### Next Steps

- MEM-2: Implement `assetCatalogLoad()` / `assetCatalogUnload()` (allocate/free loaded_data)
- MEM-3: ref_count acquire/release + eviction policy (skip if `ref_count == ASSET_REF_BUNDLED`)
- Wire load state into mod manager UI (show loaded/active indicators)

---
