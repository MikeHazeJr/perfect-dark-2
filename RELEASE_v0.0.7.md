# Perfect Dark 2 — v0.0.7 Release Notes

**Release date**: 2026-03-27
**Build type**: Alpha
**Artifacts**: `pd.x86_64.exe` (client), `pd-server.x86_64.exe` (dedicated server)

---

## What's New Since v0.0.3a

### Multiplayer Fixes (Cross-Machine Playtest Results)

- **B-27**: Fixed dedicated server crash on first client connect — 9 root causes across 6 files:
  - `g_RomName` type mismatch in server_stubs.c (`char[64]` vs `const char *`)
  - ROM/mod check now skipped on dedicated server (`!g_NetDedicated` gate)
  - `SVC_AUTH` no longer rejects client slot 0 (now available to real players on dedicated)
  - `netmsgClcAuthRead` hardcoded slot 0 assumption removed
  - NULL guard on `g_NetLocalClient` in auth handler
  - NULL guard on `ev.packet` in receive handler
  - `LOBBY_MAX_PLAYERS` updated from 8 to 32
  - Stale `#define NET_MAX_CLIENTS 8` in server_gui.cpp removed
  - GUI ping/kick actions now use correct `cl->id` instead of loop index

- **B-24**: Fixed connect code byte-order reversal — client was decoding IP octets in wrong order (MSB→LSB instead of LSB→MSB), connecting to wrong server

- **B-25**: Server now correctly supports up to 32 clients (`NET_MAX_CLIENTS 32`, decoupled from `MAX_PLAYERS`)

- **B-26**: Player name now reads from identity profile — no more "Player 1" for clients with no legacy N64 save file

---

### Dedicated Server

- Standalone `pd-server.x86_64.exe` — fully decoupled from game client
- CLI arguments: `--port`, `--maxclients`, `--gamemode`, `--headless`, `--dedicated`, `--bind`
- ImGui control panel: tabbed layout (Server tab + Hub tab), color-coded room states
- Headless mode for unattended operation
- Signal handling (SIGINT/SIGTERM) for graceful shutdown
- Separate save directory from client
- Server is not a player — no slot consumed by the server process

### Multiplayer Lobby

- Leader election: first client in lobby becomes leader
- Leader controls game mode, stage, difficulty; non-leaders see read-only view
- `CLC_LOBBY_START` protocol for match start
- `SVC_LOBBY_LEADER` / `SVC_LOBBY_STATE` broadcast messages
- Re-election on leader disconnect
- Room system: hub lifecycle with 4-room pool, 5-state machine (LOBBY→LOADING→MATCH→POSTGAME→CLOSED)

### Connect Code System

- **4-word sentence codes** ("wicked spider sliding under a savanna") — primary join mechanism
- 256-word vocabulary × 4 word slots = full 32-bit IPv4 encoding
- Code-only joining enforced — no raw IP in any UI element
- Public IP fetched via UPnP (async, no startup hang) with HTTP fallback (`curl` → `api.ipify.org`)
- Phonetic syllable codes preserved as secondary display format

### Join by Code Screen

- New menu view in main menu multiplayer section
- Phonetic/sentence code input → decode → connect
- Falls back to direct IP input for advanced use

### Lobby Screen

- Shows hub state and room list with color-coded states and player counts
- Wired through menu state manager (stack-based, no double-press issues)

### Player Identity

- `pd-identity.dat` — 16-byte UUID, up to 4 profiles per machine
- Identity profile is the canonical name source on PC

### Component Mod Architecture (D3R — Sessions 27–46)

- Each asset (map, character, skin, texture pack) is an independent component folder with `.ini` manifest
- No more monolithic mod directories or `modconfig.txt` parsing
- Asset Catalog with 11 asset types and string-keyed lookups (FNV-1a + CRC32)
- Name-based asset resolution — no numeric ROM addresses, table indices, or slot offsets
- 56 maps, 42 characters, 5 texture packs decomposed into components
- D3R-8 Bot Customizer: trait editor, `botvariant.c/h`, save-as-preset, hot-register
- D3R-9 Network distribution: PDCA archives, zlib chunks, crash recovery, download prompt UI
- D3R-10 Mod Pack export/import: PDPK format with zlib compression, 4th tab in Modding Hub
- D3R-11 Legacy cleanup: `g_ModNum` removed, `modconfig.txt` removed, shadow arrays removed
- Modding Hub: Mod Manager, INI Editor, Model Scale Tool

### Update System

- Semantic versioning (`MAJOR.MINOR.PATCH` with optional label)
- Version boxes in Dev Window are single source of truth for all builds (version baking always correct)
- All builds are clean builds — stale CMake CACHE eliminated as a bug class
- Auto-commit message uses version from UI boxes, not CMakeLists.txt defaults
- GitHub Releases API for version discovery, two channels (Stable / Dev)
- SHA-256 download verification
- Rename-on-restart self-replacement
- `.update.ver` sidecar — "Switch" button persists across restarts without re-downloading
- Per-row Download/Rollback/Switch buttons with `CalcTextSize`-based widths

### Participant System (B-12)

- Dynamic participant pool replacing fixed `chrslots` bitmask
- `participant.h/c` — heap-allocated pool (capacity 40), parallel to chrslots in Phase 1–2
- 31-bot support (raised from 8 on N64; `MAX_BOTS=32`, `chrslots u64`)
- Full 63+ character roster available in network play

### Stage Decoupling

- Heap-allocated `g_Stages` table with `stageTableInit()` / `stageTableAppend()` / `stageGetEntry()`
- `soloStageGetIndex()` lookup prevents solo index domain confusion
- Bounds guards in `endscreen.c` + `mainmenu.c` prevent OOB on mod stages

### Player Stats

- `playerstats.c` — string-keyed counter system, JSON persistence to `playerstats.json`
- Tracks kills, deaths, shots, matches, weapons used

### Build System

- CMake auto-discovery (`file(GLOB_RECURSE)`)
- Dev Window GUI: version boxes, Build Client / Build Server buttons
- Unified release: client + server ship together in one package
- Clean build enforced — no incremental builds

---

## Bug Fixes

- B-17: Mod stages loading wrong maps (catalog smart redirect)
- B-14: START button opening and immediately closing pause menu
- B-16: B button not working in pause menu
- B-20: Mission 1 crash on objective completion (NULL modeldef guard in modelmgr)
- B-22: Version boxes not baking into exe (CMake CACHE bypass fix)
- B-23: Quit Game button clipped on right edge (CalcTextSize + margin fix)
- B-24: Connect code byte-order reversal
- B-25: Server max clients hardcoded to 8
- B-26: Player name showing "Player 1" on fresh install
- B-27: Dedicated server crash on first connect (9-bug fix)
- CI corruption at boot and after MP return
- Fixed data directory search order (exe dir first)
- Fixed post-build data copy not running (was blocked by server target guard)

---

## Technical

- Protocol version 21 (chrslots u64 in SVC_STAGE_START)
- `g_ModNum` fully removed — Asset Catalog is sole mod authority
- `modconfig.txt` parsing removed — mods require `mod.json`
- Shadow asset arrays (`g_ModBodies[]`, `g_ModHeads[]`, `g_ModArenas[]`) removed
- `fileSlots` 2D array flattened to single dimension
- EXE icon embedded via `dist/windows/icon.rc`
- Logging always on — no `--log` flag needed
- F8 hotswap badge removed from main menu (toggle still works)

---

## Known Issues

- B-18: Pink sky on Skedar Ruins (missing texture or clear color issue)
- B-19: Bot spawn stacking on Skedar Ruins (mod stages lack INTROCMD_SPAWN entries)
- B-21: Menu double-press / hierarchy issues in some edge cases
- End-to-end multiplayer playtest (Connect → Lobby → Start → Play → Endscreen) pending
- SDL2 / zlib still distributed as DLLs (static linking deferred)
- Update system (D13): code written, needs first compile + test with libcurl

---

## Build Instructions

### Prerequisites

```bash
# MSYS2 MinGW terminal:
pacman -S mingw-w64-x86_64-curl   # required for update system
```

### Compile

```
# Using Dev Window:
Double-click "devtools/Dev Window.bat" → set version → Build Client / Build Server

# Or headless:
.\devtools\build-headless.ps1
```

### Run

```
# Client:
build\pd.x86_64.exe

# Dedicated Server:
build\pd-server.x86_64.exe --port 27100 --dedicated
```

---

## File Inventory (Release Assets)

| File | Description |
|------|-------------|
| `pd.x86_64.exe` | Game client |
| `pd.x86_64.exe.sha256` | Client SHA-256 hash |
| `pd-server.x86_64.exe` | Dedicated server |
| `pd-server.x86_64.exe.sha256` | Server SHA-256 hash |
| `SDL2.dll` | SDL2 runtime |
| `zlib1.dll` | zlib runtime |
| `data/` | Game data directory (ROM assets) |
| `mods/` | Mod directories |
