# Active Tasks — Current Punch List

> Razor-thin: only what needs doing. For completed work, see [tasks-archive.md](tasks-archive.md).
> For phase status, see [infrastructure.md](infrastructure.md). For bugs, see [bugs.md](bugs.md).
> Back to [index](README.md)

---

## Build Test Results (S26)

**Passed:**
- CI corruption fix (S24) — CI clean at boot and after MP return
- Stage decoupling Phase 1 (S23) — Kakariko loads, spawning works (pad-0 fallback functional)
- Pause menu via Tab (S22) — Opens, Rankings/Settings/End Game tabs work
- Match end (both paths) — No ACCESS_VIOLATION. B-10 likely resolved by ImGui path.

**Still needs testing:**
- Look inversion (S22) — Not yet tested
- Updater diagnostics (S22) — Not yet tested
- Verbose logging persistence — May not survive restart (not confirmed this build)

---

## Coded This Session — Awaiting Build Test

| Item | What Was Done | Status |
|------|--------------|--------|
| **B-13: Prop Scale Fix** | Changed `model->scale` → `modelGetEffectiveScale(model)` at lines 857–858, 883–884 in model.c. | **CODED — needs build test** |
| **B-12 Phase 1: Dynamic Participant System** | New `participant.h` / `participant.c` with heap-allocated pool (default 32, expandable). Parallel sync hooks in 6 locations in mplayer.c. Runs alongside legacy chrslots. | **CODED — needs build test** |
| **Build Tool: Commit Button** (build-gui.ps1 v3.2) | GIT section in sidebar. Dynamic "Commit XX changes" button. Dialog with message field + push checkbox. Race condition fix, `--set-upstream` push, double-v prefix fix. | **TESTED — PASS** |
| **B-14: START Double-Fire Fix** | Frame guard `s_PauseJustOpened` in pdgui_menu_pausemenu.cpp. Added `pauseActive` to pdguiProcessEvent input consumption. | **TESTED — PASS** |
| **B-16: B Button Navigation** | Added `ImGuiKey_GamepadFaceRight` handling in pause menu render. B cancels End Game confirm, or closes pause. | **TESTED — PASS** |
| **Build Tool: Commit Details** (build-gui.ps1 v3.3) | Commit dialog now shows categorized change summary (modified/added/deleted, grouped by area: Game, Port, Context, etc.). | **TESTED — PASS** |
| **Map Cycle Test** (S33/34) | Attempted automated arena cycling — crashed on 5th transition due to MEMPOOL_STAGE lifecycle conflict. Feature removed; maps tested manually. | **REMOVED** |

## Bugs Still Open

| Bug | Severity | Root Cause | Status |
|-----|----------|-----------|--------|
| [B-17](bugs.md) **Mod stages load wrong maps** | HIGH | Legacy `modConfigParseStage()` patches `g_Stages[]` with wrong file IDs. | **STRUCTURALLY FIXED** (S32) — catalog smart redirect bypasses `g_Stages[]` patching. Paradox confirmed correct. Needs broader testing across all mod maps. |

## Pause Menu UX Fixes (S26 feedback)

| Issue | What Mike Wants |
|-------|----------------|
| End Game confirm/cancel too small | Separate overlay dialog. B cancels → returns to pause menu. |
| Settings B-button exits to main menu | Should back out one level only |
| OG 'Paused' text behind ImGui menu (B-15) | Suppress legacy pause rendering when ImGui active. Low priority — will be stripped. |
| Scroll-hidden buttons | Prefer docked/always-visible buttons, minimize scrolling everywhere |

---

## New Feature Requests (S26)

| Feature | Details |
|---------|---------|
| **Starting Weapon Option** | Toggleable option: everyone spawns with a weapon. Sub-options: pick a specific weapon OR random from a configurable pool. Goes in match setup. |
| **Spawn Scatter** | Instead of circle spawn, scatter players across the map facing away from nearest wall. |
| **Update Tab: Set Version** | Currently can browse versions but can't apply one. Need "use this version" action. Policy: don't force latest; match highest version in connection. |

---

## Next Up

| Priority | Task | Depends On | Details |
|----------|------|-----------|---------|
| 1 | **B-12 Phase 2: Migrate chrslots callsites** | Phase 1 build test pass | 100+ locations across mplayer.c, setup.c, menu.c, challenge.c, filemgr.c. Replace chrslots reads/writes with participant API calls. |
| 2 | **B-12 Phase 3: Remove chrslots** | Phase 2 complete | Delete u32 chrslots field, legacy shims, BOT_SLOT_OFFSET. Protocol bump to v20. |
| 3 | **Pause Menu Fixes** | — | B-14 START double-fire, B-16 back button, End Game overlay, Settings back-out, suppress OG Paused text. |
| 4 | **Stage Decoupling Phase 2** — Dynamic stage table | Phase 1 verified | Heap-allocated `g_Stages`, `g_NumStages` counter. See [S23 session log](session-log.md). |
| 5 | **Stage Decoupling Phase 3** — Index domain separation | Phase 2 | `soloStageGetIndex()` lookup, stagenum-keyed besttimes. |
| 6 | **Starting Weapon Option** | Match setup UI | Toggle + weapon picker / random pool. New match setup field. |
| 7 | **Spawn Scatter** | — | Distribute across map pads, face away from nearest wall. |
| 8 | **Bot Customizer** | Build stable | Advanced options popup in match settings. Save as new bot type. Save/load match settings. |
| 9 | **BotController Architecture** | Build stable | Wrapper around chr/aibot. Extension points for physics, combat telemetry, lifecycle. |
| 10 | **Custom Post-Game Menu** | BotController | ImGui-based endscreen. Also fully resolves B-10. |
| 11 | **D5: Settings/Graphics/QoL** | — | FOV slider, resolution, audio volumes (4-layer). See [d5-settings-plan.md](d5-settings-plan.md). |
| 12 | **Memory Modernization M2+** | — | Stack→heap promotion. See [memory-modernization.md](memory-modernization.md). |

---

## D3-Revised: Component Mod Architecture (Session 27 Design)

> Full design: [component-mod-architecture.md](component-mod-architecture.md)
> This replaces the original D3 monolithic mod plan. Implementation sequence below.

| Phase | Task | Depends On | Details |
|-------|------|-----------|---------|
| D3R-1 | ~~**Decompose existing mods**~~ | — | **DONE (S29)** ✓ BUILD PASS: 56 maps, 42 chars, 5 tex packs in `post-batch-addin/mods/mod_*/_components/`. |
| D3R-2 | ~~**Asset Catalog core**~~ | — | **DONE (S28)** ✓ BUILD PASS: `assetcatalog.h/c` — FNV-1a + CRC32, open addressing, dynamic growth, 20-function API. |
| D3R-3 | ~~**Base game cataloging**~~ | D3R-2 | **DONE (S30)** ✓ BUILD PASS (S31): `assetcatalog_base.c` — 87 stages, 63 bodies, 75 heads with `"base:"` prefix IDs. Arenas deferred to D3R-5. |
| D3R-4 | ~~**Category scanner + loader**~~ | D3R-1, D3R-2 | **DONE (S30)** ✓ BUILD PASS (S31): `assetcatalog_scanner.c` — INI parser, category→type mapping, component registration. Block comment `*/` bug fixed (S31). |
| D3R-5 | **Callsite migration** ← IN PROGRESS | D3R-3, D3R-4 | Steps 1-4 ✓ (Step 4 coded S35, awaiting build test). B-17 fixed. **Next**: Build test Step 4, then Tier 1 easy callsites (~15 read-only display sites). See briefing below. |
| D3R-6 | **Mod Manager UI** | D3R-4 | Browse by category or mod group, toggle, validate, apply (hot-toggle). |
| D3R-7 | **INI Manager tool** | D3R-6 | In-game editor: browse/edit/create/validate. Schema-driven forms. |
| D3R-8 | **Bot Customizer** | D3R-7 | Trait editor in match setup → saves as `bot_variants/` component. |
| D3R-9 | **Network distribution** | D3R-4 | Delta packs, session-only downloads, lobby spectator combat log. |
| D3R-10 | **Mod Pack export/import** | D3R-9 | `.pdpack` creation, extraction, sharing. |
| D3R-11 | **Legacy cleanup** | D3R-5 | Remove `g_ModNum`, `modconfig.txt` parsing, static array patching. |

### D3R-5 Briefing: Callsite Migration

**Goal**: Replace numeric array index lookups with `catalogResolve()` calls so the runtime actually *uses* the Asset Catalog instead of just populating it.

**Completed steps**:
- Step 1 ✓: Catalog bootstrap — wired init into startup sequence
- Step 2 ✓: Standalone filesystem resolution — `assetcatalog_resolve.c` intercepts at `fsFullPath()`
- Step 3 ✓: Catalog-as-truth smart redirect — B-17 structurally fixed, Paradox confirmed correct

**Step 4: Arena Registration — CODED (S35, awaiting build test)**

All 4 files modified:
- `assetcatalog.h` — `ASSET_ARENA` enum, `ext.arena` struct, `assetCatalogRegisterArena()` decl
- `assetcatalog.c` — wrapper implementation
- `assetcatalog_base.c` — 75 base arenas registered via `s_ArenaGroupMap[]` + loop reading from `g_MpArenas[]`
- `pdgui_menu_matchsetup.cpp` — `#include "assetcatalog.h"`, removed `arenaGroupDef`/`s_ArenaGroups[]`, added `s_ArenaGroupCache[]` + `rebuildArenaCache()` + callback, dropdown reads from catalog entries

**Migration tiers** (from S34 callsite survey):
- **Tier 1 (easy, ~15 sites)**: Read-only display — arena names, stage lookups. NEXT after build test.
- ~~**Tier 2 (medium, big win)**: ImGui arena dropdown~~ — **DONE (S35)**
- **Tier 3 (deferred D3R-9)**: Network sync — body/head u8 indices over wire

**Constraints**:
- `bool` is `s32`, never `<stdbool.h>`
- Name-based resolution only — no new numeric lookups
- C11 game code, C++ port code
- `modmgr.c` must keep working until D3R-11

**Not in scope**: Weapons registration, Mod Manager UI (D3R-6), legacy removal (D3R-11), network sync (D3R-9)

### Key Architectural Decisions (S27)
- **No numeric lookups** — project constraint. Everything through Asset Catalog.
- **Components, not monoliths** — each asset is an independent folder + `.ini`.
- **Category = grouping label** — `category` field in `.ini` enables mod manager group toggles.
- **Soft dependencies** — `depends_on` field, graceful fallback to base assets.
- **Skins as soft references** — `target` field references a character ID, resolved lazily.
- **Dynamic memory only** — N64 shared pools removed, each component uses `malloc`.
- **Temp downloads** — `mods/.temp/` with crash recovery (keep/disable/discard).
- **Combat log for lobby** — server sends pre-resolved display names, not asset IDs.

---

## Backlog (lower priority, do when relevant)

- ~~D3e: Mod Menu~~ → superseded by D3R-6
- ~~D3f: Network mod manifest~~ → superseded by D3R-9
- ~~D3g: Cleanup~~ → superseded by D3R-11
- Systemic bug audit: [SP-1](systemic-bugs.md) remaining files (activemenu.c, player.c, endscreen.c, menu.c)
- rendering-trace.md header update (stale — claims no ImGui menus)
- menu-storyboard.md review (partially superseded)
- TODO-1: SDL2/zlib still DLL (low priority)
- TODO-5: Dr. Carroll sentinel redesign (before D3R-11)
- TODO-6: g_ModNum stragglers (D3R-11)
- Update tab UX: version selection + version policy design
