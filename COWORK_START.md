# Perfect Dark Mike — CoWork Session Primer
*Drop this file into a new CoWork session to get up to speed immediately.*

---

## ⚡ Session Startup Protocol

**Follow these steps in order at the start of every session. Do not skip steps.**

1. **Read `CLAUDE.md`** (project root) — stack, critical rules, architecture overview
2. **Read `context/tasks.md`** — what was in progress, what's blocked, what needs testing
3. **Read the system file** for the active task only (see File Index below)
4. **If planning or roadmapping**: read `context/roadmap.md`
5. **Confirm understanding** — tell Mike what task you've picked up and ask what he wants to do

Do NOT re-read every file every session. Only load what the active task requires.
The monolithic `context.md` in the project root is historical reference — prefer the modular files in `context/`.

---

## 🧠 Context & Compaction Rules

These rules exist to reduce how often CoWork compacts (which causes context loss) and ensure files survive compaction cleanly.

### Preventing Compaction
- **Scope work to one system per session.** One feature, one bug, one file cluster. Don't try to do networking + collision + menus in the same session.
- **Break large tasks into steps.** Update `context/tasks.md` at every step boundary so a new session can resume precisely.
- **Prefer `/clear` over `/compact`** when switching to a new task. Compaction is lossy. A fresh session with the right files loaded is better than a compacted session with degraded context.
- **Watch the context meter.** Green = safe. Yellow = wrap up. Red = stop and start fresh.

### What Survives Compaction
- Everything in `CLAUDE.md` and `context/` — these are re-read from disk on every session start
- Files explicitly referenced with `@path` imports are re-loaded, not summarized
- Anything stated only in conversation is **lost on compaction** — always write it to a file

### What to Write Down
- Any decision made during a session → append to the relevant system file in `context/`
- Any new bug discovered → add to the Known Issues table in `context/tasks.md`
- Any completed step → mark DONE in `context/tasks.md` before ending the session
- New files created → add to the File Index section in `context/README.md`

---

## 📁 File Index

All context files live in `context/`. Load only what you need.

| File | When to Read | What It Covers |
|------|-------------|----------------|
| `context/tasks.md` | **Every session, first** | Current task, step progress, blocked items, FIX backlog |
| `context/README.md` | First session or orientation | Project overview, architectural facts, file index |
| `context/roadmap.md` | Planning sessions | Full D1–D15 phase plan, dependencies, TODOs |
| `context/imgui.md` | ImGui / styling / debug menu | Integration points, shimmer math, palette system, mouse grab, font, known bugs |
| `context/networking.md` | Net bugs / new net features | All phases 1–10 + C1–C12, message types, damage authority |
| `context/collision.md` | Collision / physics work | Capsule system, legacy cdTestVolume, integration points |
| `context/movement.md` | Jump / movement work | Jump physics, vertical pipeline, ground/ceiling detection |
| `context/build.md` | Build errors / CMake issues | Toolchain, static linking, Build Tool GUI, mod loading |

**Docs folder** — design plans for specific systems:

| File | What It Covers |
|------|----------------|
| `docs/MOD_LOADER_PLAN.md` | Full D3 mod manager implementation plan (D3a–D3g) |
| `docs/netplay.md` | Netplay quick-reference |
| `docs/ailists.md` | AI list format reference |
| `docs/chrs.md` | Character data format |
| `docs/piracychecks.md` | Piracy check removal notes |
| `docs/challenge7bug.md` | Challenge 7 bug analysis |

**Key source directories:**

```
src/game/          — Game logic (player.c, bot.c, botact.c, bondwalk.c, menu.c, menutick.c)
src/game/mplayer/  — Multiplayer (mplayer.c, setup.c, challenge.c)
src/lib/           — Engine libs (collision.c, capsule.c)
port/src/          — PC port (main.c, fs.c, mod.c, modmgr.c, config.c, video.c)
port/src/net/      — Networking (net.c, netmsg.c, netmenu.c, netbuf.c)
port/fast3d/       — Renderer (GBI translator, ImGui backend)
port/fast3d/imgui/ — Dear ImGui v1.91.8 (vendored)
port/include/      — Port headers (pdgui.h, modmgr.h, net/)
```

---

## 🗺️ Project Overview

**perfect_dark-mike** is a PC-only port of Perfect Dark (N64, Rare 2000) merging two forks:
- **perfect_dark-netplay** — ENet-based online multiplayer (server-authoritative)
- **perfect_dark-AllInOneMods** — Community mod content (GEX, Kakariko, Goldfinger 64, Dark Noon, extra characters/stages)

**Language**: C11 (game code) + C++ (port layer, renderer, ImGui)
**Build**: CMake + MSYS2/MinGW on Windows → `build/pd.x86_64.exe`
**Rendering**: fast3d GBI translator (N64 display lists → OpenGL) + Dear ImGui overlay
**Networking**: ENet (UDP), server-authoritative, 60 Hz tick rate
**Platform**: Windows x86_64 only. Zero `PLATFORM_N64` references remain (Phase D1 complete).

**Mike is the sole developer.** He builds on Windows. CoWork writes code, Mike compiles and tests.
CoWork cannot compile this project — the build environment is Windows/MSYS2 only.

### IMPORTANT: No N64 Constraints
This runs on modern x86_64 hardware. N64 computational limits do not apply.
- Per-triangle mesh collision, BVH, runtime raycasts — all trivially cheap
- Prefer correctness over micro-optimization
- Legacy workarounds exist because of N64 hardware limits, not good design

---

## ⚙️ Architecture Quick-Reference

```
g_NetMode:      NETMODE_NONE(0)  NETMODE_SERVER(1)  NETMODE_CLIENT(2)
g_NetGameMode:  NETGAMEMODE_MP(0)  NETGAMEMODE_COOP(1)  NETGAMEMODE_ANTI(2)

Protocol version: 18  |  Tick rate: 60 Hz
Channels: unreliable (position updates) / reliable (state/events)

Bots:  PROPTYPE_CHR with chr->aibot != NULL
NPCs:  PROPTYPE_CHR with chr->aibot == NULL and not player-linked
Props identified by syncid (offset 0x48, PC-only)

Player capsule: radius ~30 units, height = vv_headheight
Geometry types: geotilei (s16 BG), geotilef (float lifts), geoblock, geocyl
Room-based geo: g_TileFileData + g_TileRooms[roomnum] offsets
Memory: mempAlloc(size, MEMPOOL_STAGE) for stage-lifetime allocations
```

### Damage Authority (server-only — all paths guarded)
| Entry Point | File | Guard |
|-------------|------|-------|
| func0f0341dc | chraction.c | Returns on NETMODE_CLIENT |
| chrDamage | chraction.c | Via SVC_CHR_DAMAGE only on client |
| objDamage | propobj.c | Returns on NETMODE_CLIENT |
| autogunTick | propobj.c | Returns on NETMODE_CLIENT |
| botTickUnpaused | bot.c | Returns on NETMODE_CLIENT |

### ImGui Integration Points
- Init: `pdguiInit(videoGetWindowHandle())` in `main.c` after `videoInit()`
- Render: inside `gfx_run()` after `rapi->end_frame()`, before `wapi->swap_buffers_begin()`
- Events: `pdguiProcessEvent()` in `gfx_sdl2.cpp` event loop, before PD's handler
- Toggle: F12 (consumed by pdguiProcessEvent — PD never sees it)
- GL state reset: `gfx_opengl_reset_for_overlay()` between PD scene and ImGui
- Style: PD-authentic styling from menugfx.c — shimmer, gradients, 7-palette system (Grey/Blue/Red/Green/White/Silver/Black&Gold)
- Font: Handel Gothic OTF embedded (24pt), separate from game's native CI4 bitmap fonts

---

## 🔧 Current Status

### What's Running
- Build compiles and produces `pd.x86_64.exe`
- Multiplayer (deathmatch), co-op campaign, and counter-operative netplay functional
- ImGui overlay renders correctly over game content at 60fps (F12 toggle)
- PD-authentic styling system: pixel-accurate shimmer from menugfx.c, 7-palette theme switcher, full style derivation
- F12 debug menu: perf metrics, network state, memory diagnostics, theme switcher, game controls
- Mouse capture fix: save/restore SDL relative mode around overlay, guards in input.c and gfx_sdl2.cpp
- Build Tool GUI (build-gui.ps1): colored progress bar (blue/green/red), gated run buttons, process monitoring, -k flag
- Mod manager core (D3a–D3d) complete — dynamic asset tables, fs.c refactored

### Active Task
See `context/tasks.md` — **Font Rendering Corruption in Endscreen Menu** (investigating — game's native bitmap font renders correctly then becomes blocky during menu transition)

### Active Bug: Font Corruption
After Combat Simulator match ends, endscreen menu text renders correctly initially then transitions to corrupted blocky rectangles. This is in the PD native rendering pipeline (CI4 bitmap font + TLUT palette through GBI→OpenGL translator). No ImGui game menu replacements exist yet — the ImGui system is only used for the F12 debug overlay. Font integrity checking (textVerifyFontIntegrity with CRC32 checksum) has been added but log output not yet reviewed.

### FIX Backlog (needs testing)
FIX-2 through FIX-13 applied but not confirmed. Critical ones:
- **Bots not moving/respawning** — FIX-2 (bot weapon table) + FIX-8/9 (mpEndMatch/mpStartMatch guards)
- **End-game crash** — FIX-7 + FIX-2 + FIX-9
- **Compile errors** — FIX-13 (#include "system.h" in 3 files)
See full table in `context/tasks.md`.

---

## 🗓️ Full Modernization Roadmap

### Legend
- ✅ DONE  |  🔄 IN PROGRESS  |  ⏳ PLANNED  |  🆕 NEW

---

### D1 — PC-Only Cleanup ✅
672 platform guards removed across 114+ files. Zero `PLATFORM_N64` references remain.

---

### D2 — Jump Polish & Bot Jump AI 🔄

#### D2a — Character Select Screen Redesign ✅
Scrollable MENUITEMTYPE_LIST for body selection, live 3D model preview, integrated-head detection, null model protection.

#### D2b — Capsule Collision System 🔄
Implemented: `capsuleSweep()`, `capsuleFindFloor()`, `capsuleFindCeiling()`. Integrated into `bondwalk.c`.
**Status**: Stationary jump confirmed working. Movement/prop/ceiling tests WAITING (Mike at PC).

#### D2c — Bot Jump AI ⏳
- Reactive jump: Forward knee-height raycast for obstacles (`bot.c`, `bondwalk.c`)
- Evasive jump: Random chance under fire — 0%/15%/30% by difficulty (`botact.c`)
- Pathfinding: Jump-required nav node tags
*Depends on: D2b confirmed*

#### D2d — Custom Simulant Types ⏳
JSON-defined simulant personality types with custom stats and character models.
Boss simulants with scaled hitboxes and custom behaviors.
Files: `bot.c`, `botact.c`, `setup.c`, `modmgr.c`
*Feeds: D11 Simulant Creator UI*

---

### D3 — Mod Manager Framework 🔄

#### D3a — Mod Manager Core ✅
`modmgr.c/h`: scan `mods/`, read `mod.json`, registry, enable/disable, config persistence.
Stable `"modid:assetid"` CRC32 net hashes. Legacy `modconfig.txt` fallback.

#### D3b — Dynamic Asset Tables ✅
Shadow arrays `g_ModBodies[64]`, `g_ModHeads[64]`, `g_ModArenas[64]`.
All accessor functions in `mplayer.c`, `setup.c`, `challenge.c` converted.
**Known issue**: Dr. Carroll sentinel (`ARRAYCOUNT(g_MpBodies) + 1`) needs proper sentinel design before user-facing mod release.

#### D3c — fs.c Refactor ✅
`fsFullPath()` uses `modmgrResolvePath()` — iterates all enabled mods in load order (first match wins). Per-mod CLI args and statics removed. `g_ModNum` preserved for now (still used in `romdata.c`, `mplayer.c`, `propobj.c`, `menutick.c` — clean up in D3g).

#### D3d — ImGui Menu System Foundation ✅ (complete 2026-03-18)
- Dear ImGui v1.91.8 vendored in `port/fast3d/imgui/`
- `pdgui_backend.cpp`: init, frame lifecycle, event handling, F12 toggle, input isolation, mouse grab save/restore
- `pdgui_style.cpp`: pixel-accurate shimmer port from menugfx.c, 7-palette system mirroring menucolourpalette, full ImGui style derivation from active palette
- `pdgui_debugmenu.cpp`: perf metrics, network state, memory diagnostics, 7-theme switcher, game controls (Host/Start/End/Disconnect)
- `pdgui.h` / `pdgui_style.h`: public C APIs
- Mouse capture fix: guards in input.c and gfx_sdl2.cpp prevent game re-grabbing while overlay active
- Font: Handel Gothic OTF embedded at 24pt base size
- See `context/imgui.md` for full details on shimmer math, palette fields, style derivation, and known bugs

#### D3e — Mod Manager Menu (modmenu.c) 🔄 ← **IMMEDIATE NEXT STEP**
Replace demo window with real mod manager screen.
- `pdgui_style.c` — Extract PD theme constants (colors, rounding, padding, fonts) as single source of truth
- `dynmenu.c` — Fluent builder API: buttons, sliders, toggles, lists, section headers as first-class primitives. Goal: define a menu screen in ~20 lines without touching ImGui internals.
- `modmenu.c` — Mod list with checkboxes, enable/disable, Apply button → `modmgrApplyChanges()`
- Wire into Options as reachable entry point

#### D3f — Network Mod Manifest ⏳
Serialize/deserialize enabled mod lists with content hashes. Mismatch rejection on connect.
Party leader's enabled mod list becomes the session's required mod list.

#### D3g — Cleanup & g_ModNum Removal ⏳
Remove remaining `g_ModNum` references in `romdata.c`, `mplayer.c`, `propobj.c`, `menutick.c`.
Replace with `modmgrGetMod()`/`modmgrFindMod()` lookups.
Fix Dr. Carroll sentinel — replace index arithmetic with a proper sentinel entry in the dynamic table.

---

### D4 — ImGui Menu Preview System 🆕 ⏳
*The debug environment for building and comparing all menu screens.*

**Concept**: A sandboxed preview mode (think Storybook) where every menu screen can be opened in isolation and toggled between old PD renderer and new ImGui version for direct comparison.

#### D4a — Preview Registry & Infrastructure
New file: `port/src/pdgui_preview.c`

```c
typedef struct {
    const char  *name;
    const char  *category;        // "Main", "Campaign", "Multiplayer", "Settings", "In-Game", "Post-Game", "Dev"
    void       (*openOldMenu)(void);   // triggers PD native menu (NULL if no old equivalent)
    void       (*renderNewMenu)(void); // renders ImGui shell
    bool        newComplete;           // shows checkmark in browser list
} MenuPreviewEntry;
```

Registry is a static array. Adding a new menu is one line.
Guard with `#ifdef PD_DEV_PREVIEW` — zero cost in release builds.
Launch flag: `--dev` enables preview mode at runtime.

#### D4b — Toggle Mechanism
- **F11**: Toggle old/new renderer for current menu
- "OLD" / "NEW" badge in corner — always visible, never ambiguous
- Old mode: push PD menu into active state via `g_Vars.currentmenumode`
- New mode: kill PD menu state, let ImGui render
- **Mock guard**: Any old-menu action that would leave preview context (e.g., start a match, load a stage) is intercepted — logs a dev message instead of executing. Prevents accidental game launches from preview.

#### D4c — Browse Mode
- Categorized list on left panel (Main, Campaign, Multiplayer, Settings, In-Game, Post-Game, Dev Tools)
- Select entry → opens in right panel with toggle + completion badge
- Entries with no old equivalent show "N/A" on old side
- Day-to-day working mode for menu development

#### D4d — Carousel Mode
- Cycles through all entries on configurable timer (default 3s, slider to adjust)
- Sequence: old → new → next old → next new
- Use for final consistency pass (spacing, typography, color) across all screens

#### D4e — Diff Mode (optional, post-carousel)
- Side-by-side or split-screen simultaneous render of old and new
- Catches alignment/spacing differences that toggle obscures (memory vs. simultaneous)

#### D4f — Menu Inventory (full register list)
All entries to register in the preview system:

| Menu | Old Equivalent | Notes |
|------|---------------|-------|
| Title / main menu | ✅ | |
| Solo mission select | ✅ | |
| Mission briefing | ✅ | |
| Difficulty select | ✅ | |
| Character select | ✅ | D2a already rebuilt — first "complete" entry |
| Simulant setup / bot config | ✅ | |
| Multiplayer lobby (host MP) | ✅ | |
| Co-op lobby (host co-op) | ✅ | |
| Join / server browser | ✅ | |
| Recent servers | ✅ | |
| Arena / stage select | ✅ | |
| Match settings | ✅ | |
| In-game pause menu | ✅ | |
| Controls / bindings | ✅ | |
| Graphics settings | ✅ (partial) | |
| Audio settings | ✅ (partial) | |
| Mod manager | ✅ → ImGui | D3e output — second "complete" entry |
| Post-game scorecard | ❌ new | No old equivalent |
| Lifetime stats viewer | ❌ new | No old equivalent |
| Simulant creator | ❌ new | No old equivalent |
| LAN / server browser | ❌ new | No old equivalent |
| Dev tools / props inspector | ❌ new | No old equivalent |
| Update screen | ❌ new | No old equivalent |

#### D4g — Migration Workflow
Once preview system exists, the loop for each menu is:
1. Open preview → navigate to menu
2. Toggle OLD → study layout, spacing, element order, font weights
3. Implement `renderNewMenu` shell (no functionality yet)
4. Toggle back and forth until visually satisfactory
5. Mark `newComplete = true` — checkmark appears in list
6. Carousel pass when all entries checked — fix cross-menu inconsistencies
7. **Second pass**: wire functionality into each completed shell, deprecate old PD menu path one by one

---

### D5 — Settings, Graphics & QoL ⏳
*First new ImGui screens that affect gameplay-visible state.*

- **FOV slider** — `float g_Fov`, passed into projection matrix. Range 60–110°, saved to `pd.ini`. High-impact, low-effort. One of the most-requested features for ports of this era.
- **Graphics settings screen** — Resolution, fullscreen, framerate cap, VSync, FOV. Migrate from `pd.ini`-only into live ImGui UI.
- **Controls/bindings screen** — Rebindable keyboard/mouse/controller. SDL2 event capture for live rebinding. Full modern controller support (Xbox, DualSense) beyond original 1.2 bindings.
- **Audio settings screen** — Volume sliders for music/SFX/voice with real-time preview.

*Depends on: D4 (dynmenu.c, pdgui_style.c)*

---

### D6 — Persistent Stats & Post-Game Scorecard ⏳

- **`stats.c`** — Lightweight local stats store (JSON or SQLite). Accumulates: kills, deaths, accuracy (shots fired/landed), weapon usage frequency, win/loss record, time played, favourite map.
- Hook into `mpstatsRecordDeath()` and `mpEndMatch()` to write records.
- **Post-game scorecard** (ImGui) — Rich end-of-match screen: per-player stats, MVP callout, weapon breakdown, team scores. Replaces existing sparse end screen.
- **Lifetime stats viewer** (ImGui) — Career totals, per-weapon accuracy, win rates by map. Accessible from main menu.

*Depends on: D4 (dynmenu.c). Feeds: D11 Simulant Creator (per-weapon usage data).*

---

### D7 — Discord Rich Presence ⏳
*Low effort, high visibility. Free word-of-mouth every session.*

- Vendor `discord-rpc` or GameSDK Activity API (~200 lines total)
- Three presence states:
  - In menu: `"Perfect Dark — Main Menu"`
  - In match: `"Skedar Ruins · 3v3 Simulants · 12 kills"`
  - In co-op: `"Co-op · Carrington Institute · Special Agent"`
- Join button in presence when hosting — hooks into D8 NAT traversal once available
- Data sources: `g_NetGameMode`, stage name, player count, scores — all already available

*Depends on: nothing. Can be done after D6 to include richer state.*

---

### D8 — NAT Traversal & LAN Discovery ⏳
*Highest single lever for multiplayer growth. Direct connect without Hamachi.*

- **`netlan.c`** — UDP broadcast on port 27101 every 2s. LAN server browser in join menu.
- **`natstun.c`** — STUN binding request to public STUN server, read reflexive address/port.
- **UDP hole punching** — Exchange reflexive endpoints via lightweight relay or manual copy-paste. Attempt punch simultaneously from both sides.
- **Relay fallback** — For symmetric NAT (hole punching can't pierce). Discord Rich Presence join button hooks here.
- Migrate join/host menus to ImGui with LAN browser integrated.

*Depends on: D4 (menu infrastructure). Feeds: D7 join button, D9 dedicated server.*

---

### D9 — Dedicated Server ⏳

- `--dedicated` CLI flag — skip `videoInit()`, `audioInit()`, `inputInit()`. Stub with no-ops.
- `netdedicated.c` — stdin command loop: `map`, `kick`, `status`, `rotate`, `say`, `quit`, `listmods`
- `server.cfg` — startup config for map rotation, max players, mode, allowed mods
- Headless mode: no local player, no `g_Vars.bond` allocation, server-only tick loop
- Optional: lightweight ImGui server manager GUI (separate process or embedded)

*Depends on: D8 (NAT traversal makes dedicated servers far more useful)*

---

### D10 — Spectator Mode ⏳
*Architecturally simple given server-authoritative model.*

- Spectator client flag: server assigns slot that receives all entity state, sends no player input, spawns no character
- Free-camera spectator view: fly cam with no-clip, toggle between players
- Follow-cam mode: lock to specific player first/third person
- **Spectator ImGui overlay** — HUD with current player stats, score ticker, kill feed. Designed for streaming/observation.

*Depends on: D9 (dedicated server + spectator slots is the natural deployment)*

---

### D11 — Simulant Creator ⏳
*Where mod system and bot AI work pay off in a highly visible, fun-affecting feature.*

- `simcreator.c` (ImGui screen) — Build simulant personality from sliders/dropdowns: reaction time, accuracy, aggression, evasion chance, weapon preference, difficulty multipliers
- Body/head selector with 3D model preview (FBO → ImGui image widget, planned in D3d design)
- Save/load simulant presets as JSON in `mods/simulants/` — treated as lightweight mods
- Network sync: custom simulant definitions sent in mod manifest exchange on connect (D3f)
- **Boss simulants** — scaled hitbox, stat overrides, special behaviors. Distinct from normal custom sims.

*Depends on: D2d (custom simulant types), D4 (dynmenu.c + 3D preview infrastructure)*

---

### D12 — Co-op Polish ⏳
*Deferred features from networking phases C1–C12. Can slot between any two phases.*

- **Cutscene camera sync** — Add `anim_id` to `SVC_CUTSCENE (0x53)`. Client plays animation locally. Eliminates "freeze and miss the cutscene" co-op experience.
- **SVC_INVENTORY_SYNC** — On reconnect: serialize full inventory (weapons, ammo, gadgets) into reliable message. Restore on client after `SVC_STAGE_START`.
- **Dynamic NPC spawning** — `SVC_NPC_SPAWN` for AI-scripted spawn events (reinforcements, ambushes mid-mission).

*Depends on: nothing new. Can be done anytime after networking is stable.*

---

### D13 — Update System ⏳

- `version.h` — `BUILD_VERSION` string, displayed in main menu corner
- `update.c` — Check GitHub releases API on launch (non-blocking async). Compare version strings.
- Download + verify — Pull zip, SHA-256 check, extract to temp directory
- Apply — Replace changed files, preserve `saves/`, `config.cfg`, user mods. Prompt to restart.
- **ImGui update screen** — Changelog display with "Update Now" / "Later" buttons

*Depends on: D9 (stable versioned builds with dedicated server), D4 (ImGui screen)*

---

### D14 — Mod Distribution & Counter-Op ⏳

**Mod transfer (server → client)**
- ENet channel 3 for file transfer — LZ4 compressed, 16KB chunks, SHA-256 verification, 50MB cap per mod
- **Progress bar** in join screen during download (ImGui)
- Files: `netmod.c` (new)

**Counter-operative mode**
- Player 2 possesses NPCs instead of controlling co-op buddy
- NPC authority transfer message — server assigns NPC control to counter-op client
- Respawn into new NPC on death
- Scoring: counter-op player vs Bond
- Counter-op lobby screen (ImGui) — mode selection, NPC faction assignment, difficulty
- Files: `net.c`, `chraction.c`

*Depends on: D3f (network mod manifest), D4 (ImGui screens)*

---

### D15 — Map Editor & Character Creator ⏳
*Long-horizon phase. All prior phases build the foundation it requires.*

- **`tools/modpacker.py`** — Validate and package mod directories into distributable archives
- **`tools/modconvert.py`** — OBJ/FBX import → PD body/head format conversion
- **Props list / level inspector** (ImGui, in-game) — Browse all props in current stage, inspect/adjust parameters. Ships as a useful dev tool before the full editor exists. Seed of the map editor.
- **Map editor** — Standalone tool: geometry import, visual prop placement, AI path editor, export as mod pack. Reuses props list screen.
- **Character creator** — 3D model import, texture assignment, export as mod body/head. Builds on 3D preview FBO work from D11.

*Depends on: D14 (mod distribution for map/character publishing), D11 (3D preview infrastructure)*

---

## 📊 Phase Dependency Graph

```
D1 (N64 Strip) ─── ✅ DONE
  │
  ├── D2 (Jump/Bots) ─── 🔄 IN PROGRESS
  │     ├── D2a (Char Select) ─── ✅ DONE
  │     ├── D2b (Capsule Collision) ─── 🔄 testing
  │     ├── D2c (Bot Jump AI) ─── ⏳ needs D2b
  │     └── D2d (Custom Simulants) ─── ⏳ feeds D11
  │
  └── D3 (Mod Manager) ─── 🔄 IN PROGRESS
        ├── D3a (Core) ─── ✅ DONE
        ├── D3b (Dynamic Tables) ─── ✅ DONE
        ├── D3c (fs.c Refactor) ─── ✅ DONE
        ├── D3d (ImGui Foundation) ─── ✅ DONE
        ├── D3e (Mod Menu) ─── 🔄 NEXT
        ├── D3f (Network Manifest) ─── ⏳
        └── D3g (Cleanup) ─── ⏳
              │
              └── D4 (ImGui Preview System) ─── ⏳ needs D3e (dynmenu.c)
                    │
                    ├── D5 (Settings/FOV) ─── ⏳ needs D4
                    │     └── D6 (Stats/Scorecard) ─── ⏳ needs D4
                    │           └── D7 (Discord RP) ─── ⏳ feeds D8 join button
                    │
                    ├── D8 (NAT Traversal) ─── ⏳ feeds D7 join button
                    │     └── D9 (Dedicated Server) ─── ⏳ needs D8
                    │           └── D10 (Spectator) ─── ⏳ needs D9
                    │
                    ├── D11 (Simulant Creator) ─── ⏳ needs D2d + D4
                    │
                    ├── D12 (Co-op Polish) ─── ⏳ independent, anytime
                    │
                    ├── D13 (Update System) ─── ⏳ needs D9 + D4
                    │
                    └── D14 (Mod Distribution + Counter-Op) ─── ⏳ needs D3f + D4
                          └── D15 (Map Editor) ─── ⏳ needs D14 + D11
```

**Flexible slots** (loose dependencies — can pull forward):
- D7 (Discord RP) — can do after D6 or even earlier
- D12 (Co-op Polish) — fully independent, anytime
- Props list / inspector from D15 — can ship as a dev tool early, before full D15

---

## 📐 Outstanding TODOs

| ID | Issue | File | Priority |
|----|-------|------|----------|
| TODO-1 | SDL2 and zlib still DLL (not static) | CMakeLists.txt | Low |
| TODO-2 | N64 strip netmode audit (72/77 confirmed, 5 fixed) | various | Low |
| TODO-4 | Verify model files: BODY_TESTCHR, BODY_PRESIDENT_CLONE | mod assets | Medium |
| TODO-5 | Dr. Carroll sentinel needs redesign before public mod release | mplayer.c | High (before D3g ships) |
| TODO-6 | g_ModNum still in romdata.c, mplayer.c, propobj.c, menutick.c | various | Medium (D3g) |

---

## 📏 Rules of Engagement

1. **Read `context/tasks.md` first** — always, every session. It tells you where we left off.
2. **Load system files on demand** — only the file(s) relevant to the active task.
3. **Write decisions down** — anything discovered or decided goes into the relevant `context/` file before the session ends.
4. **Update `context/tasks.md`** at every step boundary — start, complete, block.
5. **Don't build in the sandbox** — Mike builds on Windows. Write code, explain what to test.
6. **Prefer `/clear` over `/compact`** — a fresh session with the right files is better than a degraded compacted session.
7. **Scope to one system** — one feature, one bug cluster per session. Cross-system work causes context bloat and compaction.
8. **Compare against references** when debugging N64 strip damage or mod integration issues:
   ```bash
   # Against netplay reference:
   diff src/game/FILE.c ../perfect_dark-netplay/perfect_dark-port-net/src/game/FILE.c
   # Against AllInOneMods reference:
   diff src/game/FILE.c ../perfect_dark-AllInOneMods/perfect_dark-allinone-latest/src/game/FILE.c
   ```
9. **Be thorough** — missing functionality causes runtime crashes. When fixing one thing, check nearby for related issues.
10. **Collaborate** — Mike thinks in systems and phases. He values depth and iterative progress. If you see something to improve, say so. This is a partnership.

---

*Last updated: 2026-03-18 — D3d complete (PD-authentic styling, shimmer, palettes, debug menu, mouse grab). Font corruption bug active. Build tool upgraded. Context files restructured with imgui.md added. Phases D1–D15, preview system, FIX backlog through FIX-13, compaction guidance.*
