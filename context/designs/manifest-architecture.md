# Manifest Architecture — Asset Lifecycle for All Stage Types

> **Created**: 2026-04-13
> **Status**: Implemented
> **Files**: `port/src/net/netmanifest.c`, `port/include/net/netmanifest.h`, `port/src/pdmain.c`, `src/game/lv.c`

---

## Overview

The manifest system tracks which assets are loaded for the current stage. It enables diff-based asset lifecycle: when transitioning between stages, only the delta is loaded/unloaded. This matters for mod assets (loaded from disk, ref-counted) — bundled base-game assets are always ROM-resident.

---

## Three Manifest Paths

### 1. MP Match — `manifestBuild()` / `manifestBuildForHost()`

**Trigger**: `mainChangeToStage()` when `g_ClientManifest.num_entries > 0`

**Coverage**: Stage map, all weapons, all connected players' body/head, all bots' body/head, enabled mods, audio playlist tracks.

**Flow**: Host builds via `manifestBuildForHost()`, embeds in CLC_LOBBY_START. Server receives, supplements with other players, broadcasts SVC_MATCH_MANIFEST. Client stores in `g_ClientManifest`. On stage load, `manifestMPTransition()` diffs against current and applies.

### 2. SP Mission — `manifestBuildMission()` (two-phase)

**Trigger**: `mainChangeToStage()` when `g_ClientManifest.num_entries == 0` and `STAGE_IS_GAMEPLAY(stagenum)`

**Phase 1 (pre-load)**: Called from `mainChangeToStage()` via `manifestSPTransition()`. Captures: stage map, Joanna body/head, counter-op body/head (if active). `g_StageSetup.props` is NULL at this point — the setup-props scan finds nothing.

**Phase 2 (post-load)**: Called from `lvInit()` via `manifestSPRescanSetup()` after `setupLoadFiles()`. Re-runs `manifestBuildMission()` — now `g_StageSetup.props` is populated, so the CHR/prop scan captures all NPCs, enemies, and prop models. Diffs against the Phase 1 baseline and applies only newly discovered entries.

**Why two phases**: Phase 1 enables early diff-based unloading of the prior stage's mod assets. Phase 2 captures setup-file characters. Without Phase 2, all setup characters are caught reactively by `manifestEnsureLoaded()` at spawn time — a safety net, not a plan.

### 3. Menu/System Stage — `manifestBuildForMenu()` / `manifestMenuTransition()`

**Trigger**: `mainChangeToStage()` when `STAGE_IS_SYSTEM(stagenum)`

**Coverage**: ALL registered ASSET_BODY and ASSET_HEAD entries from the catalog, plus their dependency chains (ASSET_ANIMATION, ASSET_TEXTURE).

**Rationale**: The title screen hosts the Skin Editor, Agent Select, Bot Setup, Modding Hub, and other menus that preview arbitrary characters. Without a manifest, mod character assets loaded during a previous match would be unloaded and unavailable for preview.

---

## Safety Net: `manifestEnsureLoaded()`

Called from `bodyAllocateChr()`, `bodyAllocateModel()`, and `setuputils.c` at spawn time. Adds late-discovered assets to `g_CurrentLoadedManifest` and advances them to `ASSET_STATE_LOADED`. No-op in MP mode or when no manifest is active. O(n) dedup — safe to call on every spawn.

This is a **backup**, not the primary mechanism. The pre-scan phases should capture all assets statically.

---

## Manifest Inclusion Policy

| Asset Type | Where Registered |
|---|---|
| Stage map | All three paths — by catalog ID from stagenum |
| Player body/head (MP) | `manifestBuild()` — from `g_NetClients[].settings.body_id/head_id` |
| Bot body/head (MP) | `manifestBuild()` — from `g_MatchConfig.slots[].body_id/head_id` |
| Weapons (MP) | `manifestBuild()` — from `g_MpSetup.weapons[]` via catalog |
| Joanna body/head (SP) | `manifestBuildMission()` — hardcoded `"base:dark_combat"` / `"base:head_dark_combat"` |
| Setup CHR body/head (SP) | `manifestBuildMission()` Phase 2 — from `g_StageSetup.props` CHR entries |
| Setup prop models (SP) | `manifestBuildMission()` Phase 2 — from `g_StageSetup.props` OBJTYPE entries |
| Counter-op body/head (SP) | `manifestBuildMission()` — from `g_Vars.antibodynum/antiheadnum` (Phase 2 only, since g_Vars not set pre-load) |
| All bodies/heads (Menu) | `manifestBuildForMenu()` — full catalog iteration |
| Mod components | `manifestBuild()` / `manifestBuildForHost()` — from `modmgrGetCount/GetMod` |
| Audio tracks | `manifestBuild()` / `manifestBuildForHost()` — from audio playlist |
| Dependency chains | All body/head entries — `s_manifestExpandDeps()` adds ANIM/TEXTURE deps |

---

## Stage Type Coverage

| Stage Type | Macro | Manifest Path | Notes |
|---|---|---|---|
| MP arena | `STAGE_IS_GAMEPLAY` | MP (if client manifest) or SP (if local) | Full coverage |
| SP mission | `STAGE_IS_GAMEPLAY` | SP two-phase | Phase 2 captures setup characters |
| CI Training | `STAGE_IS_GAMEPLAY` | SP two-phase | Fixed B-118 (56 characters) |
| Title/Menu | `STAGE_IS_SYSTEM` | Menu (all characters) | Fixed Skin Editor |
| Credits | `STAGE_IS_SYSTEM` | Menu | Overkill but harmless |
| Boot pak menu | `STAGE_IS_SYSTEM` | Menu | Overkill but harmless |

---

## Key Files

- `port/src/net/netmanifest.c` — All manifest builders, diff, apply, rescan
- `port/include/net/netmanifest.h` — Public API declarations
- `port/src/pdmain.c:mainChangeToStage()` — Manifest path selection
- `src/game/lv.c:lvInit()` — Post-setup manifest rescan callsite
- `src/game/body.c` — `manifestEnsureLoaded()` safety net callsites
- `src/game/setuputils.c` — `manifestEnsureLoaded()` for prop models
