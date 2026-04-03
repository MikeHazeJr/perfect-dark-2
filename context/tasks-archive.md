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

---

## Catalog Activation (C-series) — ALL DONE (S74–S80)

| Step | Session |
|------|---------|
| C-0: Wire assetCatalogInit + RegisterBaseGame + ScanComponents | S74 |
| C-2-ext: source_filenum/texnum/animnum/soundnum in asset_entry_t | S74 |
| catalogLoadInit: Reverse-index arrays + query functions | S74 |
| C-4: catalogGetFileOverride intercept in romdataFileLoad() | S74 |
| C-5: catalogGetTextureOverride intercept in texLoad() | S92 |
| C-6: catalogGetAnimOverride intercept in animLoadFrame/Header() | S92 |
| C-7: catalogGetSoundOverride in sndStart() | S80 |
| C-8: Re-wire catalogLoadInit() on mod enable/disable | S80 |
| C-9: Stage diff (catalogComputeStageDiff) | S80 |

---

## Mod System (T-series) — ALL DONE (S77–S80)

Base table expansion (anim 1207, tex 3503, audio 1545), size_bytes walker, thumbnail queue, sound intercept, mod.json content, stage reset, texture flush. T-1 through T-10 all complete.

---

## Memory Modernization (D-MEM) — ALL DONE

| Task | Session |
|------|---------|
| MEM-1: Load state fields | Pre-S90 |
| MEM-2: assetCatalogLoad/Unload | Pre-S90 |
| MEM-3: ref_count + eviction | Pre-S90 |

---

## Session Catalog + Modular API (SA-series) — ALL DONE (S91–S97)

| Phase | Details | Session |
|-------|---------|---------|
| SA-1 | Session catalog infrastructure — sessioncatalog.h/c, SVC_SESSION_CATALOG (0x67) | S91 |
| SA-2 | Modular catalog API — catalogResolveBody/Head/Stage/Weapon + wire helper structs | S91–S92 |
| SA-3 | Network session catalog wire migration — raw N64 indices replaced with u16 session IDs across ~180 call sites | S91–S92 |
| SA-4 | Persistence migration — session IDs in savefile, identity, scenario save | S91–S92 |
| SA-5 | Load path migration + deprecation pass — all g_HeadsAndBodies/g_Stages load-path accesses migrated | S92–S93 |
| SA-6 | SP load manifest (diff-based lifecycle) — counter-op body/head, manifestEnsureLoaded() safety net | S94/S96 |
| SA-7 | Consolidation cleanup — modelcatalog.c kept, dead accessors removed, port/CLAUDE.md updated | S95 |

---

## Manifest Lifecycle Sprint — ALL DONE (S110–S116)

| Phase | Task | Session |
|-------|------|---------|
| Phase 0 | Remove numeric alias entries; manifest uses human-readable catalog IDs | S110 |
| Phase 1 | Manifest-diff transitions — load/unload delta via catalogLoadAsset/catalogUnloadAsset | S110 |
| Phase 2 | Dependency graph — character → body + head + anims + textures | S111 |
| Phase 3 | Language bank manifesting — 68 base lang banks as ASSET_LANG, langManifestEnsureId() | S112 |
| Phase 4 | Pre-validation pass — manifestValidate() verifies all to_load entries before apply | S113 |
| Phase 5 | Proper ref-counted unloads — load-before-unload ordering, cascade dep unloads | S114 |
| Phase 6 | Menu/UI asset manifesting — screenmfst.h/c, per-frame enter/leave detection | S115 |
| Post-sprint | langSafe() at 9 unsafe sites, manifest hash dedup fix (FNV-1a→CRC32), synthetic ID cleanup | S116 |

---

## Match Startup Pipeline — ALL DONE (S84–S88)

| Phase | Task | Session |
|-------|------|---------|
| A | Protocol messages — SVC_MATCH_MANIFEST, CLC_MANIFEST_STATUS, SVC_MATCH_COUNTDOWN | S84 |
| B | Server-side manifest build — manifestBuild(), manifestComputeHash() | S85 |
| C | Client manifest processing — manifestCheck(), g_ClientManifest | S86 |
| C.5 | SP bodies/heads from g_HeadsAndBodies[152] registered | S87 |
| D | Mod transfer — netmsgClcManifestStatusRead resolves missing hashes, queues via netDistribServerHandleDiff | S88 |
| E | Ready gate — s_ReadyGate bitmask, CLSTATE_PREPARING transitions, 30s timeout | S88 |
| F | Sync countdown — SVC_MATCH_COUNTDOWN, 3-2-1-GO overlay | S104 |

---

## Catalog Universality Migration (Phases A–G) — CODE COMPLETE (S119–S130)

Governing spec: PD2_Catalog_Universality_Spec_v1.0.docx

| Phase | Task | Session |
|-------|------|---------|
| A | Full codebase audit — 47 raw-index sites mapped | S120 |
| B | Catalog API hardening + human-readable IDs — FIX-24/7/13/11/12/14/10/15, new typed API | S121 |
| C | Systematic conversion — bot alloc, SVC_STAGE_START, weapon spawn, arena selection, stage loading | S122 |
| D | Server manifest model — manifestBuildForHost, SHA-256 in modinfo_t, CLC_LOBBY_START embeds host manifest, protocol v26 | S123 |
| E | Menu stack architecture — full-stack duplicate rejection, endscreen mouse restore, palette save/restore | S124 |
| F | Spawn system hardening — anti-repeat (F.1), bot stuck detection (F.5), spawn weapon fix (F.6/B-70), mouse capture (B-66) | S125 |
| G (code) | Catalog-ID-native data model (S127), stage_id + bridge API (S128), UI picker conversion (S129) | S127–S129 |
| G (wire) | Wire protocol v27 — all net_hash u32 wire fields replaced with catalog ID strings; SAVE-COMPAT stripped | S130 |

**Phase G playtest verification still pending** — success criteria in tasks-current.md.

---

## Stale Playtest Backlog (Pre-S68) — ARCHIVED

The "Awaiting Build Test / Playtest" items from sessions S40–S67 (SPF-1/3, SP-6 null guards, B-36 skyReset, 2-player Combat Sim, D3R-7 Modding Hub, B-12 Phase 1, B-13, Update tab staged version, Player Stats) predate the current architecture (catalog universality, session catalog, manifest lifecycle, match startup pipeline). Most of the underlying systems have been completely rewritten. These items are archived rather than carried forward — any actual remaining work was re-captured in the current tasks-current.md.

