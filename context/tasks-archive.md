# Task Archive — Completed Work

> Historical record of completed tasks with file manifests and architecture notes.
> For active tasks, see [tasks-current.md](tasks-current.md).
> Back to [index](README.md)

---

## Combat Sim ImGui Pause Menu + Scorecard (S22) — CODE WRITTEN

**A. Pause Menu (START button) — `pdgui_menu_pausemenu.cpp` (~650 LOC)**
- `mpPushPauseDialog()` in ingame.c redirects to `pdguiPauseMenuOpen()` for combat sim
- Co-op/counter-op keep legacy pause menus
- Tab layout: Rankings (sorted scoreboard) | Settings (read-only match info) | End Game (two-step confirm)
- Pauses game in local mode, toggles on re-press

**B. Hold-to-Show Scorecard (Tab / Back button)**
- Polls SDL `SDL_SCANCODE_TAB` + `ImGuiKey_GamepadBack` each frame
- Semi-transparent overlay (75% alpha, top-center, 40% width)
- Hidden when pause open or during GAMEOVER

**Files created**: `port/include/pdgui_pausemenu.h`, `port/fast3d/pdgui_menu_pausemenu.cpp`
**Files modified**: pdgui_bridge.c (10 bridge functions), pdgui_backend.cpp, ingame.c

---

## Stage Decoupling Phase 1 — Safety Net (S23) — CODE WRITTEN

| File | Change |
|------|--------|
| setup.c | Validate `g_StageSetup.intro` (proximity + cmd type range) |
| playerreset.c | Bounds-check `g_SpawnPoints[24]`, rooms[] sentinel init, pad-0 fallback |
| player.c | NULL check before intro loop, spawn selection divide-by-zero guard |
| scenarios.c | Iteration limit on intro loop |
| endscreen.c | `stageindex < NUM_SOLOSTAGES` before besttimes writes |
| training.c | `stageindex` bounds-check in `ciIsStageComplete()` |
| mainmenu.c | Bounds-check in `soloMenuTextBestTime()` |
| modmgr.c | Path resolution: CWD → exe dir → base dir |
| mod.c | Stagenum range check widened to 0xFF |

---

## Build & Combat Stabilization (S12–S21) — ✅ COMPLETE

**Model loading crash chain (S12–S13)**: `catalogValidateAll()` moved after `mempSetHeap()`. `fileLoadToNew` pre-check for non-existent ROM files. Lazy validation architecture.

**Pool expansion for 32 bots (S17)**: NUMTYPE1=70, NUMTYPE2=50, NUMTYPE3=48, NUMSPARE=80. Object pools doubled. Bot counting added to chrmgrConfigure + modelmgrAllocateSlots.

**Combat bugs (S18–S21)**:
- B-01 Camera crash: POS_DESYNC diagnostic removed from collision path
- B-02 Shots through bots: Scale clamp removal restored valid model geometry
- B-03 Player instant death: Handicap force `0x80` at match start

**8-step test sequence**: 7/8 PASS, #6 (instant death) → FIXED. Clean 3-min combat session.

---

## Log Channel Filter System (S11) — CODE WRITTEN

- Always-on logging (no --log gate), mode-dependent filenames
- 8 channels with ~30 prefix mappings, zero changes to 470+ call sites
- LOG_VERBOSE level with --verbose CLI flag
- ImGui debug menu section with All/None presets
- Config persistence (Debug.VerboseLogging, Debug.LogChannelMask)

**Files**: system.h, system.c, pdgui_debugmenu.cpp

---

## D13 Update System (S8–S11) — CODE WRITTEN

Full self-updating system: semantic versioning, GitHub Releases API, SHA-256 verification, rename-on-restart self-replacement, save migration framework, ImGui notification + version picker, dual-tag release (client-v/server-v), two channels (Stable/Dev).

**Prereq**: `pacman -S mingw-w64-x86_64-curl` in MSYS2

**New files**: updateversion.h, sha256.c/h, updater.c/h, savemigrate.c/h, pdgui_menu_update.cpp
**Modified**: versioninfo.h.in, CMakeLists.txt, main.c, server_main.c, pdgui_backend.cpp

---

## Menu System Phase 2 (S1–S8) — MOSTLY COMPLETE

**2a Bug Fixes**: Dropdown focus steal, controller nav, jump height wiring, movement inhibit, PD-authentic window rendering — all DONE.

**2b Menu Restructure**: 3-button layout (Play/Settings/Quit), LB/RB tab switching, 5 Settings tabs (Video/Audio/Controls/Game/Updates) — DONE.

**2c Agent Create**: Name input, body/head carousels, 3D FBO preview, create/cancel — DONE. TODOs: head display names, camera tuning.

**2e Agent Select Enhancements**: Contextual actions, delete confirmation, duplicate, character preview, hover selection, slot count warning — DONE.

**2f Typed Dialog System**: DANGER (red) + SUCCESS (green) palette renderers, dynamic title/items — DONE.

**2g Network Audit & Lobby**: Protocol documented, agent auto-wiring verified, lobby sidebar, menu fixes — DONE.

---

## Phase 3: Dedicated Server (S3–S7) — DONE

Host menu removal, new Multiplayer menu (server browser + direct IP), lobby rewrite (leader election), lobby screen (game mode selection), server GUI (4-panel), headless mode, CLC_LOBBY_START protocol, connect code system, ROM missing dialog.

**Remaining**: End-to-end playtest, stage selection (hardcoded Complex), leader broadcast, Quick Play button.

---

## Memory Modernization M0–M1 (S14–S15) — DONE

**M0**: 24 diagnostic logs → LOG_VERBOSE, orphaned include cleanup.
**M1**: `memsizes.h` with 30+ named constants. 8 high-priority files converted. ~100 ALIGN16 replacements remaining.

---

## BotController Architecture Design (S19) — DESIGNED, NOT CODED

```c
struct BotController {
    struct chrdata *chr;
    struct prop *prop;
    struct aibot *ai;          // existing AI brain (reused as-is)
    struct BotPhysics physics;
    struct BotCombat combat;   // hit tracking, damage stats for post-game
    u32 flags;                 // BOTCTRL_CAN_JUMP, BOTCTRL_MESH_COLLISION, etc.
    void (*onTick)(struct BotController *self);
    void (*onDamage)(struct BotController *self, f32 amount, struct prop *attacker);
    void (*onDeath)(struct BotController *self, struct prop *killer);
};
```

Wraps existing chr/aibot without rewriting AI. Extension points for physics, combat telemetry, lifecycle.
**Depends on**: Build stabilization (done), BotController coding (not started).
