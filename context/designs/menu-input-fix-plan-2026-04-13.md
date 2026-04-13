# Menu & Input Fix Plan -- 2026-04-13

> **Companion to**: [menu-input-architecture-audit-2026-04-13.md](menu-input-architecture-audit-2026-04-13.md)
> **Session**: S226 | **Author**: Claude Opus 4.6 (1M context) | **Scope**: Actionable punch list

---

## Fix Layers

All items organized by dependency. Each layer must be completed before the next begins. Items within a layer are independent and can be done in parallel.

---

## Layer 0 -- Foundation (Critical, Everything Depends on This)

### F-0.1: Fix Campaign Menu Language ID Shadows

**File**: `port/fast3d/pdgui_menu_solomission.cpp`
**Lines**: 282-332
**Symptom**: ~15 campaign menu strings are invisible (blank labels on buttons, titles, confirmation dialogs). ~15 more strings use hardcoded English fallbacks instead of localized text.
**Root cause**: Shadow `#define` values for `L_OPTIONS_*` and `L_MPWEAPONS_*` use only the string offset (e.g., `0x007a`), missing the bank prefix. Real values encode `(bank << 9) | offset`. Bank 0 is NULL in `g_LangBanks[]`, so `langGet()` returns NULL.
**Proposed change**: Replace all 30 shadow defines with correct values:
- `L_OPTIONS_N` = `0x5600 + N` (LANGBANK_OPTIONS = 0x2b)
- `L_MPWEAPONS_N` = `0x5400 + N` (LANGBANK_MPWEAPONS = 0x2a)

Alternative (preferred long-term): Create `port/include/langids.h` that exports the needed text ID constants without including `types.h`. All C++ menu files include this instead of shadowing.

**Risk**: Low -- purely fixing constant values. No logic change.
**Dependencies**: None.
**Build verify**: Full link. Then launch solo missions -- all button labels, dialog titles, and confirmation text should appear.

### F-0.2: Fix Context Leak in Training Menu

**File**: `port/fast3d/pdgui_menu_training.cpp`
**Line**: 1195 (push site)
**Symptom**: After opening FR Weapon List in training mode and navigating back, `g_CtxImGuiMenu` remains on the input context stack. Gameplay input blocked until stage transition.
**Root cause**: `inputCtxPush(&g_CtxImGuiMenu)` at line 1195 has no matching `inputCtxPopDeferred(&g_CtxImGuiMenu)` on any exit path.
**Proposed change**: Add `inputCtxPopDeferred(&g_CtxImGuiMenu)` to the back/Escape handler for the FR Weapon List dialog. Follow the B-124 pattern: track push via `s_FrWeaponPushedCtx` bool, pop when `!s_FrWeaponPushedCtx` would mismatch.
**Risk**: Low -- adding a missing pop call.
**Dependencies**: None.

### F-0.3: Fix Context Leak in Game-Over Panels

**File**: `port/fast3d/pdgui_menu_pausemenu.cpp`
**Lines**: 1258, 1408 (push sites)
**Symptom**: After MP game-over panels render, `g_CtxImGuiMenu` contexts accumulate on the stack. Cleaned only by stage transition `inputCtxShutdown()`.
**Root cause**: Two `inputCtxPush(&g_CtxImGuiMenu)` calls for game-over result/challenge panels with no matching pops.
**Proposed change**: Add `inputCtxPopDeferred(&g_CtxImGuiMenu)` on the exit paths from these panels. Track with `s_GameOverPushedCtx` bool. Pop when panel is dismissed or match exits.
**Risk**: Low -- adding missing pop calls.
**Dependencies**: None.

### F-0.4: Fix Stale Manifest on Return-to-Room

**File**: `port/src/pdmain.c` or `port/fast3d/pdgui_bridge.c`
**Symptom**: After MP match exit, `g_ClientManifest` retains match-specific entries because `mainChangeToStage(STAGE_CITRAINING)` takes the gameplay path (STAGE_CITRAINING is gameplay), which does not call `manifestClear()`.
**Root cause**: The `!STAGE_IS_GAMEPLAY()` gate in `mainChangeToStage()` excludes STAGE_CITRAINING from the manifest-clear path.
**Proposed change (option A -- surgical)**: Add `manifestClear(&g_ClientManifest)` to `pdguiEndscreenExitToMainMenu()` before `func0f0f8120()`.
**Proposed change (option B -- systemic)**: In `mainChangeToStage()`, add STAGE_CITRAINING to the manifest-clear condition: `if (!STAGE_IS_GAMEPLAY(stagenum) || stagenum == STAGE_CITRAINING)`.
**Risk**: Low -- manifestMenuTransition() rebuilds the menu manifest immediately. The safety net `manifestEnsureLoaded()` catches any gaps.
**Dependencies**: None.

---

## Layer 1 -- Consistency (Apply B-124 Pattern Uniformly)

### F-1.1: Remove Direct SDL_WarpMouseInWindow from Pause Menu

**File**: `port/fast3d/pdgui_menu_pausemenu.cpp`
**Lines**: 230-234
**Symptom**: Mouse is warped to screen center when pause menu opens via direct SDL call, bypassing the input context system.
**Root cause**: Added before `inputCtxSyncMouseMode()` existed.
**Proposed change**: Remove `SDL_GetMouseFocus()`, `SDL_GetWindowSize()`, `SDL_WarpMouseInWindow()` calls. The input context system's `on_push` callback for `g_CtxPauseMenu` already handles mouse mode transition. If cursor centering is desired, add it to the context's `on_push` callback.
**Risk**: Low -- the context system already handles the transition.
**Dependencies**: F-0.3 (to avoid double-fix in same file).

### F-1.2: Add pdguiSoloMissionReset() Function

**File**: `port/fast3d/pdgui_menu_solomission.cpp`
**Symptom**: Static state persists across menu opens (cursor position, difficulty selection, confirm flags). Stale focus if returning after different game flow.
**Root cause**: No reset function exists. All 17 static variables persist indefinitely.
**Proposed change**: Add `void pdguiSoloMissionReset(void)` that zeroes `s_MissionSelectIdx`, `s_DetailDiffIdx`, `s_DetailFocusIdx`, `s_DetailPanelFocus`, `s_PrevBriefingStage`, `s_ShowLockedMissions`, `s_DiffSelectIdx`, `s_AcceptSelectIdx`, `s_PauseSelectIdx`, `s_RestartConfirm`, `s_RestartSelectIdx`, `s_AbortSelectIdx`, `s_OptionsSelectIdx`, `s_OptionsTabIdx`, `s_CoopAntiDiffSelectIdx`, `s_CoopAntiOptSelectIdx`. Call from `pdguiEndscreenExitToMainMenu()` and on Solo Missions entry from main menu.
**Risk**: Low -- additive change.
**Dependencies**: None.

### F-1.3: Verify Ad-Hoc Menus Don't Need Context

Review each "Ad-hoc" menu in the gap matrix and confirm it correctly delegates context management to its parent or host:

| File | Verdict |
|------|---------|
| `agentcreate.cpp` | OK -- opened within agent select flow (parent pushes) |
| `network.cpp` | OK -- opened from mainmenu (parent pushes) |
| `lobby.cpp` | OK -- standalone render, no dialog stack participation |
| `challenges.cpp` | OK -- opened from mainmenu (parent pushes) |
| `teamsetup.cpp` | OK -- opened from room screen (parent pushes) |
| `warning.cpp` | OK -- type-based fallback, rendered within existing context |
| `controldiagram.cpp` | OK -- opened from settings flow (parent pushes) |
| `stats.cpp` | REVIEW -- standalone overlay, should it push its own context? Currently no input capture issues reported. |
| `theme_editor.cpp` | OK -- rewritten with BeginPopupModal (B-130 fix) |
| `moddinghub.cpp` | REVIEW -- standalone overlay with tabs and text input. Does not push context. If text input is active, ImGui captures keyboard via io.WantCaptureKeyboard, but the context stack doesn't know about it. |
| `modmgr.cpp` | OK -- embedded in modding hub |
| `mpingame.cpp` | OK -- overlay, no input capture needed |
| `audiomod.cpp` | OK -- embedded in modding hub |
| `update.cpp` | OK -- notification banner, minimal interaction |
| `logviewer.cpp` | OK -- debug tool, F12 overlay pushes debug context |

**Candidates for future context push**: `stats.cpp` (if it gets interactive features), `moddinghub.cpp` (for text input protection -- currently works because ImGui's WantCaptureKeyboard gates the game's input handler, but this is implicit rather than explicit).

### F-1.4: Clean Up Redundant SDL Calls in inputctx.c

**File**: `port/src/inputctx.c`
**Lines**: `on_push` callbacks for gameplay, imguiMenu, pauseMenu
**Symptom**: Each `on_push` callback calls `SDL_SetRelativeMouseMode` + `SDL_ShowCursor`, then `inputCtxSyncMouseMode()` also calls them.
**Proposed change**: Remove the redundant SDL calls from `on_push` callbacks. Let `inputCtxSyncMouseMode()` be the sole SDL mouse authority.
**Risk**: Very low -- removing redundant calls. `inputCtxSyncMouseMode()` is called in every push/pop path.
**Dependencies**: None. Optional cleanup.

---

## Layer 2 -- Features

### F-2.1: Arena List -- Alphabetized with Collapsible Sections

**File**: `port/fast3d/pdgui_menu_room.cpp`
**Lines**: 297-348 (data model), 1610-1631 (picker UI)

**Step 1 -- Extend data model**:
```c
struct arena_entry {
    char name[64];
    char id[64];
    s32  stagenum;
    char category[32];    // NEW
    s32  bundled;         // NEW
    s32  section;         // NEW: 0=MP_BASE, 1=CAMPAIGN, 2=MOD
};
```

**Step 2 -- Collect additional fields**:
In `catalogArenaCollect()` callback (line 304), copy `e->category` and `e->bundled` from the catalog entry. Derive `section`:
```c
if (!e->bundled) {
    entry->section = ARENA_SEC_MOD;
} else if (strcmp(e->category, "Solo Missions") == 0) {
    entry->section = ARENA_SEC_CAMPAIGN;
} else {
    entry->section = ARENA_SEC_MP_BASE;
}
```

**Step 3 -- Sort after collection**:
```c
qsort(s_Arenas, s_NumArenas, sizeof(arena_entry), arenaCompare);
// arenaCompare: primary by section, secondary by strcasecmp(name)
```

**Step 4 -- Section boundary tracking**:
```c
static s32 s_SectionStart[ARENA_SEC_COUNT];
static s32 s_SectionCount[ARENA_SEC_COUNT];
// Computed after sort by scanning s_Arenas[i].section transitions
```

**Step 5 -- UI with CollapsingHeader**:
Replace the flat `Selectable` loop with:
```cpp
static const char *k_SectionNames[] = {
    "Multiplayer Arenas", "Campaign Maps", "Mod Maps"
};

if (ImGui::BeginCombo("##arena", arenaLabel)) {
    for (int sec = 0; sec < ARENA_SEC_COUNT; sec++) {
        if (s_SectionCount[sec] == 0) continue;
        char hdr[128];
        snprintf(hdr, sizeof(hdr), "%s (%d)", k_SectionNames[sec], s_SectionCount[sec]);
        if (ImGui::TreeNodeEx(hdr, ImGuiTreeNodeFlags_DefaultOpen)) {
            for (int ai = s_SectionStart[sec]; ai < s_SectionStart[sec] + s_SectionCount[sec]; ai++) {
                bool sel = (ai == s_SelectedArena);
                if (ImGui::Selectable(s_Arenas[ai].name, sel)) {
                    s_SelectedArena = ai;
                    syncArenaToConfig(ai);
                }
                if (sel) ImGui::SetItemDefaultFocus();
            }
            ImGui::TreePop();
        }
    }
    ImGui::EndCombo();
}
```

**Risk**: Medium -- touches the arena data pipeline. Must verify `syncArenaFromConfig()` (ID-based search) still works after sort.
**Dependencies**: None.
**Build verify**: Full link + open Combat Sim room, verify arena picker shows three sections, alphabetical within each.

### F-2.2: Fix Static Init Order in Solo Pause k_Btns

**File**: `port/fast3d/pdgui_menu_solomission.cpp`
**Lines**: 2604-2611
**Symptom**: `k_Btns` array captures `langSafe()` values at first-call time. If lang bank not loaded on first call, values are permanently empty.
**Root cause**: Static local array initialization in C++ evaluates once on first entry.
**Proposed change**: Move `langSafe()` calls out of the static initializer. Either:
- (A) Make `k_Btns` non-static and populate every frame (negligible cost), or
- (B) Use a `static bool inited` guard and refresh on first successful lang resolution
**Risk**: Low.
**Dependencies**: F-0.1 (shadow defines must be fixed first or this fix is moot).

### F-2.3: Music Restart Robustness

**File**: `src/game/lv.c`
**Lines**: 456-460
**Symptom**: On return-to-room, `normmplayerisrunning == false` causes `lvReset()` to call `musicSetStage()` without starting playback. Music eventually restarts via menu system, but there's a silent gap.
**Proposed change**: For STAGE_CITRAINING, always call `musicSetStageAndStartMusic()` regardless of `normmplayerisrunning`. The CI environment should always have ambient music.
**Risk**: Low -- CI training ambient music is expected behavior.
**Dependencies**: None.

---

## Layer 3 -- Robustness (Optional, Low Priority)

### F-3.1: Add Duplicate Rejection to menuPushDialog

**File**: `src/game/menu.c`
**Lines**: ~1520
**Symptom**: Same dialogdef can be pushed twice to the legacy stack, creating duplicate dialog instances.
**Proposed change**: Before allocating, scan `layers[0..depth]` for matching `definition` pointer. If found, log warning and return without pushing.
**Risk**: Low -- additive guard. Must verify no legitimate case pushes the same def twice (tabs/siblings use `nextsibling` linked list, not double push).
**Dependencies**: None.

### F-3.2: Add Logging to menuPopDialog Underflow

**File**: `src/game/menu.c`
**Symptom**: If menuPopDialog is called when depth == 0, it silently does nothing.
**Proposed change**: Add `sysLogPrintf(LOG_WARNING, "MENU: menuPopDialog called at depth 0")`.
**Risk**: None.
**Dependencies**: None.

### F-3.3: B-92 Deferred Flush Review

**File**: `port/fast3d/pdgui_backend.cpp`
**Lines**: 499-500
**Symptom**: Direct SDL calls for solo mission mouse capture bypass the context system.
**Proposed change**: Evaluate whether the B-92 deferred flush is still needed after the input context system matured. If `inputCtxSyncMouseMode()` now handles the transition correctly, remove the deferred flush.
**Risk**: Medium -- this was a specific race condition fix. Removing requires careful testing of the solo mission start path.
**Dependencies**: None, but requires solo mission playtest verification.

---

## Summary Table

| ID | Layer | File | Severity | Effort | Description |
|----|-------|------|----------|--------|-------------|
| F-0.1 | 0 | solomission.cpp | **CRITICAL** | Small | Fix 30 shadow language ID defines |
| F-0.2 | 0 | training.cpp | HIGH | Small | Add missing context pop |
| F-0.3 | 0 | pausemenu.cpp | HIGH | Small | Add missing context pop for game-over |
| F-0.4 | 0 | pdmain.c/bridge.c | MED | Small | Clear stale manifest on room return |
| F-1.1 | 1 | pausemenu.cpp | LOW | Small | Remove direct SDL_WarpMouseInWindow |
| F-1.2 | 1 | solomission.cpp | MED | Small | Add pdguiSoloMissionReset() |
| F-1.3 | 1 | (multiple) | LOW | Small | Verify ad-hoc menus don't need context |
| F-1.4 | 1 | inputctx.c | LOW | Tiny | Remove redundant SDL calls from on_push |
| F-2.1 | 2 | room.cpp | MED | Medium | Arena list alphabetized + collapsible sections |
| F-2.2 | 2 | solomission.cpp | MED | Small | Fix static init order for pause k_Btns |
| F-2.3 | 2 | lv.c | LOW | Tiny | Music restart robustness for CITRAINING |
| F-3.1 | 3 | menu.c | LOW | Small | Duplicate rejection in menuPushDialog |
| F-3.2 | 3 | menu.c | LOW | Tiny | Logging on menuPopDialog underflow |
| F-3.3 | 3 | pdgui_backend.cpp | LOW | Medium | Review B-92 deferred flush necessity |
