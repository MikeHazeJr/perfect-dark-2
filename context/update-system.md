# Update System (D13)

## Overview
Self-updating system for both game client and dedicated server. Uses GitHub Releases API
for version discovery, SHA-256 for download verification, and rename-on-restart for safe
binary replacement on Windows.

**GitHub repo**: `https://github.com/MikeHazeJr/perfect-dark-2.git`
(AI has filesystem access to the repo — not via `gh` CLI tool.)

## Versioning Scheme

### Semantic Version
`MAJOR.MINOR.PATCH` — e.g., `1.0.0`, `1.2.3`

- **MAJOR**: Breaking changes (save format incompatibility, protocol breaks)
- **MINOR**: New features, backward-compatible
- **PATCH**: Bug fixes only

### Client vs Server
Client and server have **independent** version numbers:
- `BUILD_VERSION_CLIENT` — injected at build time
- `BUILD_VERSION_SERVER` — injected at build time
- `NET_PROTOCOL_VER` (currently 27) — stays separate, only bumped on wire-format changes

### Release Channels
- **Stable**: GitHub releases NOT marked as prerelease. Tags: `client-v1.0.0`, `server-v1.0.0`
- **Dev/Test**: GitHub releases marked as prerelease. Tags: `client-v1.1.0-dev.3`, `server-v0.9.1-dev.1`
- Player selects channel in Settings (default: Stable)
- Persisted in `system.json` → `updateChannel` field

### GitHub Release Tag Format
```
client-v{MAJOR}.{MINOR}.{PATCH}          — stable client
client-v{MAJOR}.{MINOR}.{PATCH}-dev.{N}  — dev client
server-v{MAJOR}.{MINOR}.{PATCH}          — stable server
server-v{MAJOR}.{MINOR}.{PATCH}-dev.{N}  — dev server
```

### Release Assets (per release)
```
pd.x86_64.exe                — game client binary
pd.x86_64.exe.sha256         — SHA-256 hash of client binary
pd-server.x86_64.exe         — dedicated server binary (server releases only)
pd-server.x86_64.exe.sha256  — SHA-256 hash of server binary
CHANGELOG.md                 — human-readable changelog
manifest.json                — file list with hashes for multi-file updates (future)
```

## Architecture

### Files

| File | Language | Purpose |
|------|----------|---------|
| `port/include/updateversion.h` | C | Version struct, comparison, channel enum |
| `port/include/sha256.h` | C | SHA-256 public API |
| `port/src/sha256.c` | C | SHA-256 implementation (~200 LOC, no deps) |
| `port/include/updater.h` | C | Update checker/downloader API |
| `port/src/updater.c` | C | GitHub API client, download, self-replace logic |
| `port/include/savemigrate.h` | C | Save migration chain API |
| `port/src/savemigrate.c` | C | Version-aware migration functions |
| `port/fast3d/pdgui_menu_update.cpp` | C++ | ImGui update notification + version picker |
| `port/include/versioninfo.h.in` | Template | Extended with semantic version fields |

### Dependencies
All libraries are **statically linked** on Windows — no DLLs needed at runtime.
- **SDL2** (static) — `pacman -S mingw-w64-x86_64-SDL2`
- **zlib** (static) — `pacman -S mingw-w64-x86_64-zlib`
- **libcurl** (static, HTTPS-only) — `pacman -S mingw-w64-x86_64-curl`
- CMake auto-detects static `.a` libraries on Windows (see CMakeLists.txt)
- **NOTE**: After switching to static linking, delete `build/CMakeCache.txt` to force re-detection

### Threading Model
- Update check runs on a **background SDL_Thread** at game launch
- Main thread polls `updaterGetStatus()` each frame (non-blocking)
- Download also runs on background thread with progress callback
- All shared state protected by SDL_mutex

### Update Flow (Client)

```
Game Launch
    │
    ├── updaterApplyPending()     ← check for .update file, rename-on-restart
    │
    ├── updaterCheckAsync()       ← background thread: GET /releases, parse JSON
    │
    ├── [main loop polls updaterGetStatus()]
    │
    ├── User sees notification → opens version picker
    │
    ├── User selects version → updaterDownloadAsync(tag)
    │     └── background thread: download asset, verify SHA-256, write .update
    │
    └── User restarts → updaterApplyPending() on next launch
```

### Self-Replacement (Rename-on-Restart)

Windows allows renaming (but not deleting/overwriting) a running executable.

1. Download completes → `pd.x86_64.exe.update` written + verified
   - Version sidecar `pd.x86_64.exe.update.ver` written (plain text version string)
2. Player closes game (or game prompts restart)
3. On next launch, `updaterApplyPending()` runs before anything else:
   - Rename `pd.x86_64.exe` → `pd.x86_64.exe.old`
   - Rename `pd.x86_64.exe.update` → `pd.x86_64.exe`
   - Delete `pd.x86_64.exe.old`
   - Delete `pd.x86_64.exe.update.ver` (sidecar no longer needed)
   - If any step fails: attempt rollback, log error
4. Game continues launching with new binary

**Cross-session staged version**: On `updaterInit()`, if `.update` exists, reads `.update.ver`
to restore the staged version in memory. `updaterGetStagedVersion()` returns it. The UI
uses this to show the "Switch to this version" amber button even after a game restart.

### Save Migration

The save system already has `SAVE_VERSION` and per-struct `s32 version` fields.

**Migration chain pattern:**
```c
// Each version bump registers a migration:
saveMigrateRegister(1, 2, saveMigrate_agent_1to2);
saveMigrateRegister(2, 3, saveMigrate_agent_2to3);

// On load, if save.version < SAVE_VERSION:
//   Run chain: 1→2→3→...→current
//   Backup original file first
```

**Downgrade protection:**
- If `save.version > SAVE_VERSION` (running older game): load read-only, show warning
- Never modify saves from a newer version

**Backup policy:**
- Before any migration: copy `agent_foo.json` → `agent_foo.json.v1.bak`
- Backups preserved indefinitely (user can manually clean up)

### Dedicated Server Update Path

Same infrastructure, different version constant and tag prefix:
- Checks `server-v*` tags instead of `client-v*`
- `--check-update` CLI flag: prints available version to stdout and exits (scripted ops)
- `updaterTick()` called each frame in `server_main.c` main loop
- When check completes, logs result via `sysLogPrintf` (both headless and GUI)
- **Server GUI (server_gui.cpp)**:
  - Status bar (column 4) shows "Update: vX.X.X" badge when available
  - "Updates (*)" tab in middle panel with version list + per-row Download/Switch buttons
  - Restart & Update button downloads and re-launches server binary
- **Headless**: logs download URL; operator restarts manually or uses `--check-update` flag

### Data Preservation

Updates NEVER touch:
- `saves/` — agent, player, setup, system JSON files
- `config/` — user configuration
- `mods/` — user-installed mods
- Keybinds, window position, any user preferences

Updates ONLY replace:
- `pd.x86_64.exe` (or `pd-server.x86_64.exe`)
- Future: shared data files listed in `manifest.json`

## MSYS2 Setup

```bash
# Install libcurl for MinGW (static + shared)
pacman -S mingw-w64-x86_64-curl
```

### Client Update UI (pdgui_menu_update.cpp)

- **Notification banner**: Full-width green bar at top when update available
  - Buttons right-aligned via `SameLine(GetContentRegionMax().x - totalW)` pattern
  - "Update Now" → immediately downloads latest, tracks `s_DownloadingIndex`
  - "Details" → opens floating version picker dialog
  - "Dismiss" → hides for this session
- **Settings > Updates tab**: per-row action buttons in version table (5 columns)
  - "Download" button per non-current version with assetUrl
  - "Switch" button on staged version (downloaded, pending restart) → triggers restart prompt
  - Progress % shown in action column during active download
  - Rollback tooltip for older versions
  - Download failure shown below table
- `s_DownloadingIndex` / `s_StagedReleaseIndex` track which release is in each state (session)
- Cross-session staged detection: `updaterGetStagedVersion()` + `.update.ver` sidecar file

## Status
- Created: 2026-03-20
- Status: Implemented + cross-session staged version persistence added (S49/S50) — needs build test
