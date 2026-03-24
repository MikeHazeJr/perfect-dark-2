# Perfect Dark 2 — v0.0.3a Release Notes

**Release date**: 2026-03-20
**Build type**: Alpha (first versioned release with update system support)
**Artifacts**: `pd.x86_64.exe` (client), `pd-server.x86_64.exe` (dedicated server)

---

## What's New Since v0.0.2

### Dedicated Server (NEW)
- Standalone `pd-server.x86_64.exe` — fully decoupled from the game client
- CLI arguments: `--port`, `--maxclients`, `--gamemode`, `--headless`, `--dedicated`, `--bind`
- ImGui control panel: 4-panel layout with status bar, player list (with ping/kick), server controls, and live log
- Headless mode for unattended operation
- Signal handling (SIGINT/SIGTERM) for graceful shutdown
- Separate save directory from client

### Network Lobby System
- Leader election: first client in lobby becomes leader (not host)
- Leader controls game mode selection: Combat Simulator, Co-op Campaign, Counter-Operative
- Non-leaders see disabled controls, wait for leader to start
- `CLC_LOBBY_START` protocol: leader sends match start request with gamemode/stage/difficulty
- `SVC_LOBBY_LEADER` / `SVC_LOBBY_STATE` broadcast messages
- Re-election on leader disconnect
- Agent names flow through lobby via `cl->settings.name`

### Multiplayer Menu (ImGui)
- Server Browser with clickable rows (address, status, player count)
- Direct IP connect with text input
- Lobby screen: two-column layout (player list + game controls)
- "Multiplayer" renamed from "Network Game" throughout UI
- Disconnect button with B/Escape support

### Update System Infrastructure (D13)
- Semantic versioning: `MAJOR.MINOR.PATCH` with optional label suffix
- GitHub Releases API integration for version discovery
- SHA-256 download verification
- Rename-on-restart self-replacement (Windows-safe)
- Save migration framework (version-aware, backup-before-migrate)
- Release channels: Stable and Dev
- ImGui update notification banner and version picker
- Server: `--check-update` CLI flag for scripted management

### Match Setup & Infrastructure
- Match setup system for networked games
- Input modes system (menu vs gameplay context switching)
- Model catalog for character/body management
- Title screen guard system
- Automatic UPnP port forwarding (async, no startup hang)

---

## What's Working

- **Single-player**: Solo missions, Combat Simulator (local), training — fully functional
- **ImGui menu system**: F8 hot-swap between new and original menus
  - Agent Select (with preview, actions, delete/copy)
  - Agent Create (name, head/body carousel, 3D preview)
  - Main Menu (Play / Settings / Quit)
  - Warning/Success typed dialog fallbacks (48 dialogs)
- **Dedicated server**: Builds, runs, accepts connections, ImGui control panel works
- **Network connection**: Client → Server connect flow works
- **Lobby**: Player list, leader election, game mode selection functional
- **Build system**: CMake + MinGW, Build Tool GUI with separate client/server buttons
- **PD-authentic styling**: Shimmer, palettes, gradients, 7 color themes
- **Controller support**: Full gamepad navigation in ImGui menus, R3 = F8 toggle
- **Character system**: Skedar/Dr. Carroll mesh loading fixed, 3D preview via FBO

## What's In Progress

- **Update system**: Code written, needs first compile + test with libcurl
- **End-to-end multiplayer playtest**: Connect → Lobby → Start Match → Play → Endscreen not yet verified as complete flow
- **Combat Sim stage selection**: Currently hardcoded to Complex for quick-start
- **Capsule collision**: Stationary jumping works, full movement testing pending
- **157 DEFAULT-type menu dialogs**: Still rendered via original PD menu system

## Known Issues

- **Endscreen font/menu corruption**: After Combat Sim match, "Game Over" dialog initially renders correctly then transitions to blocky/corrupted version. GBI state issue. (HIGH PRIORITY)
- **Bots not moving/respawning**: Multiple root causes identified, fixes applied but untested
- **Character preview framing**: Camera/zoom needs tuning
- **Head display names**: Shows "Head XX" instead of descriptive names
- **Blue geometry at Paradox stage**: Visual bug at center pit (LOW PRIORITY)
- **SDL2/zlib**: Still distributed as DLLs (static linking deferred)

---

## Build Instructions

### Prerequisites
```bash
# MSYS2 MinGW terminal:
pacman -S mingw-w64-x86_64-curl   # NEW for v0.0.3a (update system)
```

### Compile
```
# Using Build Tool GUI:
Double-click "Build Tool.bat" → Build Client / Build Server

# Or manually:
build.bat all
```

### Run
```
# Client:
build\pd.x86_64.exe

# Dedicated Server:
build\pd-server.x86_64.exe --port 27100 --maxclients 8
```

---

## File Inventory (Release Assets)

| File | Description |
|------|-------------|
| `pd.x86_64.exe` | Game client |
| `pd.x86_64.exe.sha256` | Client SHA-256 hash (for update verification) |
| `pd-server.x86_64.exe` | Dedicated server |
| `pd-server.x86_64.exe.sha256` | Server SHA-256 hash |
| `SDL2.dll` | SDL2 runtime (required) |
| `zlib1.dll` | zlib runtime (required) |
| `data/` | Game data directory (ROM assets) |
| `mods/` | Mod directories |
