# ImGui Menu State Transition Audit

> **Generated**: 2026-04-09, Session S187
> **Scope**: Every ImGui menu file audited for legacy state transitions that the ImGui path might bypass
> **Triggered by**: B-??? — Main menu close handler missing playerUnpause() + g_PlayersWithControl restore because legacy menutick bg-transition (func0f0fa6ac) never fires when ImGui hotswap bypasses the legacy renderer

---

## Background: What the Legacy Path Does

When the legacy menu system opens/closes, it performs these state changes:

**On menu open** (`menuPushRootDialog`, menu.c:3640):
- `g_PlayersWithControl[playernum] = false` — disables player movement
- `inputAutoLockMouse(false)` — releases mouse capture
- `lvSetPaused(true)` + `pausemode = PAUSEMODE_PAUSED` — pauses game simulation

**On menu close** (`menuClose`, menu.c:3531):
- `inputAutoLockMouse(true)` — re-captures mouse
- `g_PlayersWithControl[playernum] = true` (MPPAUSE root only)

**On background transition complete** (`func0f0fa6ac`, menu.c:4680):
- `playerUnpause()` — sets PAUSEMODE_UNPAUSED, calls `lvSetPaused(false)` + `musicEndMenu()`
- `g_PlayersWithControl[0] = true`
- Only fires for MENUROOT_MAINMENU, MPSETUP, FILEMGR, 4MBMAINMENU, TRAINING

**Key insight**: `func0f0fa6ac` is called from the menutick background transition animation. When ImGui hotswap renders the menu instead of the legacy renderer, the background transition never runs, so `func0f0fa6ac` never fires. Any state changes in that function must be replicated in the ImGui close handler.

**Safety net**: Stage transitions (`mainChangeToStage`, main.c:1110-1120) call `inputCtxShutdown()` + `inputCtxInit()` + `inputCtxPush(&g_CtxGameplay)`, which resets the entire input context stack. This prevents stale contexts from surviving stage loads. However, it does NOT reset `g_PlayersWithControl`, `pausemode`, `lvPaused`, or `musicEndMenu` — those must be handled explicitly.

---

## Audit Results

### GAP-1: pdgui_menu_endscreen.cpp — No input context pop on close

**Severity: HIGH**

**Problem**: The endscreen pushes `inputCtxPush(&g_CtxImGuiMenu)` on first appear (solo line 398, MP line 726) but never explicitly pops it. All exit paths call `pdguiEndscreenExitToMainMenu()` (pdgui_bridge.c:652) which does pop `g_CtxImGuiMenu` if active — but only AFTER calling `configSave()`. If `configSave` throws an exception or the function is bypassed, the context leaks.

**Mitigating factor**: `pdguiEndscreenExitToMainMenu()` at bridge.c:655 does check `inputCtxIsActive(&g_CtxImGuiMenu)` and pops. Also, all exit paths eventually call `mainChangeToStage()` which resets the context stack entirely. So this is unlikely to cause problems in practice.

**Risk**: If a new exit path is added that doesn't go through `pdguiEndscreenExitToMainMenu()`, the context will leak. Fragile design.

**Suggested fix**: Add a cleanup function called from every exit path, or pop the context in the endscreen's own close/dismiss logic before delegating to bridge functions.

---

### GAP-2: pdgui_menu_endscreen.cpp — No playerUnpause / g_PlayersWithControl restore

**Severity: MEDIUM**

**Problem**: The solo endscreen enters a paused state (the game sets `lvSetPaused(true)` + `PAUSEMODE_PAUSED` in endscreen.c:1725/1770/1875 from the legacy side). The ImGui endscreen's exit paths all trigger stage transitions (`pdguiEndscreenStartMission` → `menuhandlerAcceptMission` which loads a new stage, `pdguiEndscreenExitToMainMenu` → `func0f0f8120` which pops all dialogs and goes to title). Since stage transitions reset everything, `playerUnpause()` and `g_PlayersWithControl` restore happen implicitly through stage init.

**Risk**: If a "Return to CI Hub" exit path were added (staying on the same stage without a full stage transition), the pause state and player control would not be restored.

**Suggested fix**: No immediate fix needed — all current exit paths trigger stage transitions. Document this dependency.

---

### GAP-3: pdgui_menu_pausemenu.cpp (game over screen) — Context push without explicit pop

**Severity: MEDIUM**

**Problem**: The game-over screen pushes `inputCtxPush(&g_CtxImGuiMenu)` at line 1139 when it first appears. The exit buttons (Return to Lobby line 1266, Quit to Menu line 1286) each call `mainChangeToStage()` which resets the context stack. But there's no explicit pop before the stage transition.

**Current safety**: The stage transition at main.c:1118 calls `inputCtxShutdown()` + `inputCtxInit()` + `inputCtxPush(&g_CtxGameplay)`, which nukes everything. So the leaked context is cleaned up.

**Risk**: Between the `mainChangeToStage()` call and the actual stage load (which happens on next frame in the main loop), the stale ImGuiMenu context is still active. During this window, gameplay input could be incorrectly consumed by the menu context.

**Suggested fix**: Pop `g_CtxImGuiMenu` before calling `mainChangeToStage()` in both exit paths.

---

### GAP-4: pdgui_menu_pausemenu.cpp — mpSetPaused only in NETMODE_NONE

**Severity: LOW**

**Problem**: `pdguiPauseMenuOpen()` (line 245) calls `mpSetPaused(MPPAUSEMODE_PAUSED)` and `pdguiPauseMenuClose()` (line 265) calls `mpSetPaused(MPPAUSEMODE_UNPAUSED)`, but both are gated by `g_NetMode == NETMODE_NONE`. This is intentional — in networked games, the server controls pause state.

**Legacy equivalent**: The legacy `mpOpenPauseMenu()` at menu.c:3737 also sets `PAUSEMODE_PAUSED` unconditionally via `g_Vars.currentplayer->pausemode`. The ImGui pause menu does NOT set `pausemode` at all — it uses the separate `mpSetPaused()` system.

**Risk**: In offline multiplayer, the legacy pause menu set both `pausemode` (per-player) AND `g_MpSetup.paused` (global). The ImGui pause menu only sets `g_MpSetup.paused`. If any game logic checks `pausemode` instead of `g_MpSetup.paused` during MP pause, it won't see the pause.

**Suggested fix**: Audit all `pausemode` checks in multiplayer paths. Consider setting `g_Vars.currentplayer->pausemode = PAUSEMODE_PAUSED` in `pdguiPauseMenuOpen()` and `PAUSEMODE_UNPAUSED` in `pdguiPauseMenuClose()` for consistency.

---

### GAP-5: pdgui_menu_pausemenu.cpp — No lvSetPaused in MP pause

**Severity: LOW**

**Problem**: The legacy `mpOpenPauseMenu()` at menu.c:3736 calls `lvSetPaused(true)`. The ImGui pause menu does NOT call `lvSetPaused()`. It relies on `mpSetPaused(MPPAUSEMODE_PAUSED)` which sets `g_MpSetup.paused` but does NOT call `lvSetPaused()`.

**Legacy path**: `lvSetPaused(true)` freezes the game simulation tick. In the legacy system, `mpSetPaused` + `lvSetPaused` were both called. The ImGui path only calls `mpSetPaused`.

**Risk**: Without `lvSetPaused(true)`, the simulation continues running while the pause menu is displayed in offline MP. Game logic that checks `mpIsPaused()` (via `g_MpSetup.paused`) will correctly see the pause, but the tick loop at lv.c continues processing. This could lead to subtle simulation drift during pause.

**Mitigating factor**: In NETMODE_NONE offline play, `mpIsPaused()` is checked early in the game tick and gates most game logic. So even without `lvSetPaused`, the game is effectively paused. But physics, timers, and ambient updates may still tick.

**Suggested fix**: Add `lvSetPaused(true)` in `pdguiPauseMenuOpen()` and `lvSetPaused(false)` in `pdguiPauseMenuClose()` for NETMODE_NONE.

---

### GAP-6: pdgui_menu_mainmenu.cpp — No musicEndMenu on close

**Severity: LOW**

**Problem**: The legacy `playerUnpause()` (player.c:2672-2673) calls both `lvSetPaused(false)` AND `musicEndMenu()`. The ImGui main menu close at line 2306 calls `playerUnpause()` which does handle both. **This gap is already fixed.**

**Status**: NOT A GAP — `playerUnpause()` is called and handles music correctly.

---

### GAP-7: pdgui_menu_mainmenu.cpp — No inputAutoLockMouse on close

**Severity: LOW (mitigated)**

**Problem**: The legacy `menuClose()` at menu.c:3541 calls `inputAutoLockMouse(true)` to re-capture the mouse. The ImGui main menu close handler at line 2296 calls `inputCtxPopDeferred(&g_CtxImGuiMenu)` which triggers the context's `on_pop` callback. The `on_pop` for `g_CtxImGuiMenu` (imguiMenuOnPop in inputctx.c) deactivates the menu IMC but does NOT directly call `inputAutoLockMouse()`. Instead, `inputCtxSyncMouseMode()` runs at frame end and sets `SDL_SetRelativeMouseMode(TRUE)` when the top context is gameplay.

**Status**: NOT A GAP — the input context system's `syncMouseMode` handles this. The legacy `inputAutoLockMouse` path is bypassed but the replacement (context-based sync) achieves the same result.

---

### GAP-8: pdgui_menu_room.cpp — inputCtxPush tracking via s_RoomPushedCtx

**Severity: LOW**

**Problem**: The room screen tracks whether it pushed the context via `s_RoomPushedCtx`. If the room is entered from the main menu (which already pushed `g_CtxImGuiMenu`), `s_RoomPushedCtx` stays false and the room doesn't pop on close. If entered directly (solo room via `pdguiSoloRoomOpen`), it pushes and tracks.

**Risk**: If a new entry path is added that doesn't push the context, and `s_RoomPushedCtx` is not set, the context won't be popped. Also, if the room pushes but a match starts (stage transition), `s_RoomPushedCtx` is not cleared — the stage transition resets the context stack, but `s_RoomPushedCtx` retains its stale true value. On next room open, it would try to pop a context that was never pushed.

**Mitigating factor**: `inputCtxIsActive()` is likely checked before popping, preventing invalid pops.

**Suggested fix**: Clear `s_RoomPushedCtx` in the room's reset/init path and check `inputCtxIsActive()` before popping.

---

### GAP-9: pdgui_menu_solomission.cpp — Three separate inputCtxPopDeferred calls

**Severity: LOW**

**Problem**: Mission accept has three code paths that each independently call `inputCtxPopDeferred(&g_CtxImGuiMenu)`:
1. Mission list direct-start (line 1089)
2. Accept dialog "Accept" (line 1627)
3. Pre-mission briefing "Accept" (line 1705)

Each is guarded by `inputCtxIsActive(&g_CtxImGuiMenu)`, so double-pops are prevented.

**Risk**: Code duplication. If a fourth accept path is added without the guard, the context could leak or double-pop.

**Suggested fix**: Extract a common `pdguiSoloMissionAccept()` function that handles the context pop.

---

### GAP-10: pdgui_menu_challenges.cpp — matchStartFromChallenge triggers stage load with no context cleanup

**Severity: LOW**

**Problem**: "Accept Challenge" calls `matchStartFromChallenge()` which eventually calls `mainChangeToStage()`. No `inputCtxPopDeferred()` is called before the stage transition.

**Mitigating factor**: The stage transition at main.c:1118 calls `inputCtxShutdown()` which cleans everything up. And the challenges screen is within the main menu, which already has the `g_CtxImGuiMenu` context pushed — the stage transition will nuke it.

**Status**: Not actively harmful, but should pop context before stage transition for hygiene.

---

### GAP-11: pdgui_menu_modmgr.cpp — modmgrApplyChanges triggers stage load

**Severity: LOW**

**Problem**: "Apply Changes" in the mod manager calls `modmgrApplyChanges()` which calls `mainChangeToStage(STAGE_TITLE)`. No input context cleanup before this.

**Mitigating factor**: Same as GAP-10 — stage transition resets context stack. The mod manager doesn't push its own context (it's a standalone visibility-flag overlay).

**Status**: Not actively harmful.

---

### GAP-12: No ImGui menu calls activemenu MENUOP_CLOSE

**Severity: MEDIUM**

**Problem**: The legacy active menu system (activemenu.c) manages `g_PlayersWithControl` through MENUOP_OPEN/TICK/CLOSE callbacks:
- MENUOP_OPEN: `g_PlayersWithControl[currentplayernum] = false` (line 195, 739)
- MENUOP_CLOSE: `g_PlayersWithControl[currentplayernum] = true` (line 201, 760)

ImGui menus that replace active menus (like the pick-target dialog) must ensure these callbacks still fire. If an ImGui menu bypasses the active menu system entirely, `g_PlayersWithControl` won't be restored.

**Current state**: The active menu system (activemenu.c) is a separate subsystem from the main menu (menu.c). Active menus are in-game overlays (weapon selection, pick-target for guided weapons). None of the current ImGui pdgui_menu_* files replace active menus — they replace main menus.

**Risk**: If an ImGui menu is ever written to replace the active menu system (e.g., an ImGui weapon wheel or target picker), it must replicate the `g_PlayersWithControl` toggle.

**Suggested fix**: No immediate fix needed. Add a comment in activemenu.c noting this dependency for future ImGui ports.

---

### GAP-13: pdgui_menu_pausemenu.cpp — g_PlayersWithControl not explicitly managed

**Severity: MEDIUM**

**Problem**: The legacy `mpOpenPauseMenu()` path goes through `menuPushRootDialog()` (menu.c:3648) which sets `g_PlayersWithControl[playernum] = false`. The legacy close through `menuClose()` (menu.c:3544) restores `g_PlayersWithControl[playernum] = true` for MPPAUSE root.

The ImGui pause menu (`pdguiPauseMenuOpen`/`pdguiPauseMenuClose`) does NOT manage `g_PlayersWithControl` at all. It relies on the input context system to suppress input.

**Risk**: If any game logic checks `g_PlayersWithControl[]` directly (rather than the input context system) to decide whether to process player input, the player could still receive input during MP pause.

**Audit of consumers**: `g_PlayersWithControl` is read in:
- `bondmove.c:2127` — gates the entire movement tick. If `g_PlayersWithControl[playernum]` is true AND `pausemode == PAUSEMODE_UNPAUSED`, movement processes.
- Since the ImGui pause menu doesn't set `pausemode` either (GAP-4), the combination means bondmove sees UNPAUSED + has-control = player can move during MP pause.

**HOWEVER**: The bondmove check at line 2127 reads `pausemode` which is set to UNPAUSED. But `mpIsPaused()` is checked earlier in the MP tick and gates most logic. So in practice, movement is probably blocked by the MP pause check, not by `g_PlayersWithControl` or `pausemode`.

**Suggested fix**: Set `g_PlayersWithControl[0] = false` in `pdguiPauseMenuOpen()` and `g_PlayersWithControl[0] = true` in `pdguiPauseMenuClose()` for defense-in-depth.

---

## Summary Table

| ID | File | Gap | Severity | Fix Needed? |
|----|------|-----|----------|-------------|
| GAP-1 | endscreen | Context push without explicit pop (relies on stage transition) | HIGH | Yes — add explicit pop in exit logic |
| GAP-2 | endscreen | No playerUnpause (relies on stage transition) | MEDIUM | No — current paths all do stage transitions |
| GAP-3 | pausemenu (game over) | Context push without explicit pop | MEDIUM | Yes — pop before mainChangeToStage |
| GAP-4 | pausemenu | pausemode not set (only mpSetPaused) | LOW | Investigate — check all pausemode consumers in MP |
| GAP-5 | pausemenu | No lvSetPaused in MP pause open/close | LOW | Yes — add lvSetPaused for NETMODE_NONE |
| GAP-6 | mainmenu | musicEndMenu on close | NOT A GAP | N/A — playerUnpause handles it |
| GAP-7 | mainmenu | inputAutoLockMouse on close | NOT A GAP | N/A — context sync handles it |
| GAP-8 | room | s_RoomPushedCtx stale after stage transition | LOW | Yes — clear flag in reset path |
| GAP-9 | solomission | Three separate context pop paths (duplication) | LOW | Refactor — extract common function |
| GAP-10 | challenges | No context pop before stage transition | LOW | Hygiene — pop before stage load |
| GAP-11 | modmgr | No context cleanup before stage transition | LOW | Hygiene — not harmful |
| GAP-12 | (future risk) | Active menu system not replicated in ImGui | MEDIUM | Document — no current ImGui replaces active menus |
| GAP-13 | pausemenu | g_PlayersWithControl not managed in MP pause | MEDIUM | Yes — add toggle for defense-in-depth |

---

## Priority Fix Order

1. **GAP-1 + GAP-3** (HIGH/MEDIUM): Explicit context pops in endscreen and game-over exit paths
2. **GAP-13** (MEDIUM): g_PlayersWithControl in MP pause open/close
3. **GAP-5** (LOW): lvSetPaused in MP pause
4. **GAP-4** (LOW): pausemode in MP pause (investigate consumers first)
5. **GAP-8** (LOW): s_RoomPushedCtx staleness
6. **GAP-9** (LOW): Solo mission accept code dedup
7. **GAP-10, GAP-11** (LOW): Hygiene context pops before stage transitions

---

## Systemic Pattern: "Stage Transition as Cleanup"

Multiple ImGui menus rely on `mainChangeToStage()` to clean up the input context stack (via the `inputCtxShutdown` + `inputCtxInit` + `inputCtxPush(&g_CtxGameplay)` at main.c:1118). This is a valid safety net but creates fragility:

1. **The window between `mainChangeToStage()` and actual stage load**: The stage transition is deferred — it sets `g_MainChangeToStageNum` and the actual load happens on the next frame in `mainProc`. During this window, stale contexts remain active.

2. **Not all state resets in stage transition**: `inputCtxShutdown` resets the context stack, but NOT:
   - `g_PlayersWithControl[]` — reset in `playerreset.c:152` during player init, which happens during stage load
   - `pausemode` — reset in `playermgrReset()` during stage load
   - `g_MpSetup.paused` — NOT reset during stage load (persists across transitions)
   - `lvPaused` — reset in `lv.c:619` during `lvInit`
   - Music state — NOT explicitly reset; depends on the new stage's music init

3. **Recommendation**: Each ImGui menu should clean up its own state on close, not depend on stage transitions. The stage transition reset should be a backstop, not the primary cleanup mechanism.

---

## Files Audited (Complete List)

| File | Has State Mgmt | Gaps Found |
|------|---------------|------------|
| pdgui_menu_mainmenu.cpp | Yes (ctx push/pop, playerUnpause, g_PlayersWithControl) | None — already fixed |
| pdgui_menu_pausemenu.cpp | Yes (ctx push/pop, mpSetPaused) | GAP-3, GAP-4, GAP-5, GAP-13 |
| pdgui_menu_lobby.cpp | No (stateless renderer) | None |
| pdgui_menu_room.cpp | Yes (ctx push/pop tracked) | GAP-8 |
| pdgui_menu_network.cpp | No (menu stack only) | None |
| pdgui_menu_mpsettings.cpp | No (menu stack only) | None |
| pdgui_menu_solomission.cpp | Yes (ctx pop on mission accept) | GAP-9 |
| pdgui_menu_training.cpp | No (menu stack only) | None |
| pdgui_menu_agentcreate.cpp | No (menu stack only) | None |
| pdgui_menu_agentselect.cpp | Yes (ctx push/pop tracked) | None |
| pdgui_menu_challenges.cpp | No (menu stack only, stage transition) | GAP-10 |
| pdgui_menu_endscreen.cpp | Yes (ctx push, no explicit pop) | GAP-1, GAP-2 |
| pdgui_menu_mpingame.cpp | No (passive overlay) | None |
| pdgui_menu_teamsetup.cpp | No (menu stack only) | None |
| pdgui_menu_stats.cpp | No (visibility flag only) | None |
| pdgui_menu_moddinghub.cpp | No (visibility flag only) | None |
| pdgui_menu_modmgr.cpp | No (visibility flag, stage transition) | GAP-11 |
| pdgui_menu_logviewer.cpp | No (debug panel) | None |
| pdgui_menu_theme_editor.cpp | No (palette save/restore within frame) | None |
| pdgui_menu_update.cpp | No (notification flags) | None |
| pdgui_menu_warning.cpp | No (palette save/restore within frame) | None |
