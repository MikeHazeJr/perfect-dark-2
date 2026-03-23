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
| **Build Tool: Commit Button** (build-gui.ps1 v3.2) | GIT section in sidebar. Dynamic "Commit XX changes" button. Dialog with message field + push checkbox. | **CODED — needs build test** |

## Bugs Still Open

| Bug | Severity | Root Cause | Status |
|-----|----------|-----------|--------|
| [B-14](bugs.md) **START opens/closes pause** | MED | Input passthrough — both legacy + ImGui consume START in same frame | NEEDS DIAGNOSIS |
| [B-16](bugs.md) **Back on controller noop** | MED | ImGui not mapping gamepad B to nav back | NEEDS DIAGNOSIS |

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

## Backlog (lower priority, do when relevant)

- D3e: Mod Menu (replace demo window with real manager)
- D3f: Network mod manifest (mismatch rejection)
- D3g: Cleanup (remove g_ModNum refs, Dr. Carroll sentinel)
- Systemic bug audit: [SP-1](systemic-bugs.md) remaining files (activemenu.c, player.c, endscreen.c, menu.c)
- rendering-trace.md header update (stale — claims no ImGui menus)
- menu-storyboard.md review (partially superseded)
- TODO-1: SDL2/zlib still DLL (low priority)
- TODO-5: Dr. Carroll sentinel redesign (before D3g)
- TODO-6: g_ModNum stragglers (D3g)
- Update tab UX: version selection + version policy design
