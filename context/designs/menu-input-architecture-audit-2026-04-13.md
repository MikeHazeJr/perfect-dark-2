# Menu & Input Architecture Audit -- 2026-04-13

> **Session**: S226 | **Author**: Claude Opus 4.6 (1M context) | **Scope**: Design doc only, zero code changes
> **Predecessor docs**: [d5-full-menu-overhaul.md](d5-full-menu-overhaul.md), [d5-ui-polish-plan.md](d5-ui-polish-plan.md), [menu-inventory.md](menu-inventory.md), [hud-layer-order.md](hud-layer-order.md)
> **Related bugs**: B-117, B-121, B-122, B-124(a-d), B-130, B-131, B-132

---

## Table of Contents

1. [Intended Architecture](#1-intended-architecture)
2. [Gap Matrix -- All Menu & Input Consumers](#2-gap-matrix)
3. [Root-Cause Taxonomy](#3-root-cause-taxonomy)
4. [Reinit Audit: Room --> Match --> Room](#4-reinit-audit)
5. [Case Study: Campaign Mission Select Strings](#5-campaign-strings)
6. [Full Campaign Menu System Audit](#6-campaign-menu-system)
7. [Case Study: Arena List](#7-arena-list)
8. [Recommended Rework Plan](#8-rework-plan)

---

## 1. Intended Architecture

### 1.1 Menu Stack Semantics

The codebase has **two co-existing menu stacks**, one legacy and one modern:

**Legacy Stack** (`menu.c`):
- `g_Menus[MAX_PLAYERS]` -- per-player menu state, selected by mutable global `g_MpPlayerNum`
- 6-depth layer stack (`layers[6]`), each layer holds up to 5 sibling dialogs (tabs)
- 10-dialog allocation pool per player
- `menuPushDialog(dialogdef)` / `menuPopDialog()` / `menuCloseDialog()` with handler veto
- No duplicate rejection -- same dialogdef can be pushed twice
- `menuRenderDialog()` now delegates 100% to hotswap (`pdguiHotswapCheck()`)
- Zero native rendering paths remain (P10 D5.7 complete)

**Modern Stack** (input context system, `inputctx.c`):
- 16-slot pushdown automaton (`s_Stack[16]`)
- 4 built-in contexts: `g_CtxGameplay` (base), `g_CtxImGuiMenu`, `g_CtxPauseMenu`, `g_CtxDebugOverlay`
- Push with duplicate rejection + resurrection (deferred-pop in same frame)
- `inputCtxPopDeferred()` marks for removal; actual `on_pop` callback fires in `inputCtxEndFrame()`
- `inputCtxPopImmediate()` removes top only (positional, not by-reference)
- Key suppression: 100ms grace after push prevents trigger key re-fire (B-124 fix)
- Mouse mode derived from top context (`inputCtxSyncMouseMode()`)

**Bridge Layer** (`pdgui_hotswap.cpp`):
- 256-slot registration table linking `menudialogdef*` to ImGui render callbacks
- Two-phase pipeline: GBI phase queues dialogs (`pdguiHotswapCheck`), overlay phase renders them (`pdguiHotswapRenderQueued`)
- Type-based fallback for unregistered dialogs (DANGER/SUCCESS types)
- `s_HotswapMenuWasActive` bridges GBI-phase queuing to next-frame input ownership

### 1.2 Push/Pop Rules (Intended)

| Rule | Mechanism | Enforcement |
|------|-----------|-------------|
| No duplicate contexts on stack | `inputCtxPush` scans + rejects/resurrects | Enforced in inputctx.c |
| No duplicate dialogs on legacy stack | None | **NOT ENFORCED** -- menuPushDialog has no dedup |
| Pop is deferred to frame end | `marked_for_removal` flag | Enforced in inputctx.c |
| Handler can veto close | `MENUOP_CLOSE` sets `preventclose` | Enforced in menu.c |
| Context owns mouse mode | `on_push`/`on_pop` + `inputCtxSyncMouseMode` | Enforced in inputctx.c |
| No direct SDL mouse calls | Convention | **NOT ENFORCED** -- 3 violations found |

### 1.3 Input Context Stack -- Priority Rules

Events walk top-to-bottom. First `can_consume()` == true claims the event. `g_CtxGameplay` always returns true (catch-all at bottom).

```
Top:    g_CtxDebugOverlay  (F12 toggle)     -- eats everything
        g_CtxPauseMenu     (Esc in gameplay) -- eats KB/mouse/gamepad
        g_CtxImGuiMenu     (menu screens)    -- eats KB/mouse/gamepad
Bottom: g_CtxGameplay      (always present)  -- eats everything
```

Each context activates its own action mapping context (IMC) on push and deactivates on pop. This is symmetric by design.

### 1.4 Focus/Capture Ownership

**Policy**: The input context stack is the sole authority for SDL mouse mode. No other code may call `SDL_SetRelativeMouseMode` or `SDL_ShowCursor` directly.

**Mechanism**: `inputCtxSyncMouseMode()` runs at push, pop (both immediate and deferred), and frame-end. Binary rule: gameplay on top = relative+hidden; anything else = absolute+visible.

**Known violations** (see Gap Matrix):
1. `pdgui_backend.cpp:499-500` -- B-92 deferred flush (intentional bypass, documented)
2. `pdgui_menu_pausemenu.cpp:230-234` -- `SDL_WarpMouseInWindow` on pause open
3. `inputctx.c` on_push callbacks -- redundant SDL calls (harmless, belt-and-suspenders)

### 1.5 Theme/Tint Separation

**Policy**: Theme = persistent visual identity (user-chosen palette, draws first). Tint = transient overlay (composites on top, cleared on pop).

**Mechanism**: 7 built-in palettes derived from N64 `menucolourpalette`. `pdguiSetPaletteOverride()` for per-dialog tint (DANGER=red, SUCCESS=green). Shimmer (20s cycle) and scanline overlays are independent of palette.

**Implementation**: Consistent across all menu files. No theme/tint leaks found -- palette override is scoped to render callbacks and clears automatically.

### 1.6 Return-to-Room Reinit Contract (Intended)

When exiting a match back to menu/room, ALL of the following must happen:
1. **Input contexts**: All gameplay/pause/menu contexts popped, stack reset to `[g_CtxGameplay]`
2. **Legacy menu stack**: All dialog depth popped to 0
3. **Music**: Match music stopped, menu/CI music restarted
4. **Manifest**: Match manifest cleared, menu manifest rebuilt (all bodies/heads for preview)
5. **Network**: Either disconnect (full cleanup) or stay connected (room state preserved)
6. **ImGui state**: Endscreen windows stop rendering (`g_MainIsEndscreen = false`)
7. **Room screen state**: Reset or preserve depending on path (solo return vs. networked return)

---

## 2. Gap Matrix

### Classification Key

| Level | Definition |
|-------|-----------|
| **Conformant** | Follows B-124 pattern: push tracked via bool, pop on all exit paths, symmetric init/teardown |
| **Partial** | Has push/pop but with gaps (missing pop on some paths, or push without pop) |
| **Ad-hoc** | No formal context management, relies on parent or external cleanup |
| **Broken** | Active bugs: context leaks, stale state, invisible strings |

### 2.1 Menu Files -- Context Conformance

| File | Lines | Level | Push | Pop | Notes |
|------|-------|-------|------|-----|-------|
| `pdgui_menu_mainmenu.cpp` | 3534 | **Conformant** | L2470, L3090, L3291 | L2564, L3137, L3394, L3423 | 3 independent push/pop pairs (main, settings, cinema). Tracked via `s_MainMenuPushedCtx`. |
| `pdgui_menu_room.cpp` | 2871 | **Conformant** | L2086 | L2345 | Tracked via `s_RoomPushedCtx`. `pdguiRoomScreenReset()` clears all static state. |
| `pdgui_menu_cheats.cpp` | 920 | **Conformant** | L454 | L469, L637 | Tracked via `s_PushedCtx`. Clean B-124 pattern. |
| `pdgui_menu_agentselect.cpp` | 612 | **Conformant** | L210 | L333, L343, L389 | Tracked via `s_AgentSelectPushedCtx`. All 3 exit paths pop. |
| `pdgui_menu_mpsetup.cpp` | 1265 | **Conformant** | L420 | L434 | Simple pair. |
| `pdgui_menu_botsetup.cpp` | 1074 | **Conformant** | L490 | L504 | Simple pair. |
| `pdgui_menu_mpadvanced.cpp` | 1059 | **Conformant** | L381 | L395 | Simple pair. |
| `pdgui_menu_mppause.cpp` | 1210 | **Conformant** | L487 | L501 | Simple pair. |
| `pdgui_menu_playerconfig.cpp` | 1294 | **Conformant** | L422 | L436 | Simple pair. |
| `pdgui_menu_mpsettings.cpp` | 1054 | **Conformant** | L324 | L338 | Simple pair. |
| `pdgui_menu_solomission.cpp` | 3351 | **Partial** | None | L1153, L2340, L2418 | **Pops without pushing.** Relies on parent (mainmenu) having pushed. |
| `pdgui_menu_training.cpp` | 2143 | **Broken** | L1195 | None | **Pushes g_CtxImGuiMenu but NEVER pops.** Potential context leak on FR Weapon List close. |
| `pdgui_menu_endscreen.cpp` | 1161 | **Partial** | L402, L755 | None (in file) | Pushes at solo completed/failed. Pop handled externally by `pdguiEndscreenExitToMainMenu()` and stage transition `inputCtxShutdown()`. |
| `pdgui_menu_pausemenu.cpp` | 1434 | **Partial** | L228 (Pause), L1258, L1408 (GameOver) | L259 (Pause) | Pause push/pop is symmetric. **Game-over panels push g_CtxImGuiMenu at L1258 and L1408 with no visible pop.** |
| `pdgui_menu_agentcreate.cpp` | 720 | **Ad-hoc** | None | None | Pure render, no context management. |
| `pdgui_menu_network.cpp` | 336 | **Ad-hoc** | None | None | Relies on parent context. |
| `pdgui_menu_lobby.cpp` | 396 | **Ad-hoc** | None | None | Standalone render function. |
| `pdgui_menu_challenges.cpp` | 388 | **Ad-hoc** | None | None | Pure render, uses menuPopDialog only. |
| `pdgui_menu_teamsetup.cpp` | 421 | **Ad-hoc** | None | None | Pure render. |
| `pdgui_menu_warning.cpp` | 1083 | **Ad-hoc** | None | None | Type-based fallback renderer. |
| `pdgui_menu_controldiagram.cpp` | 575 | **Ad-hoc** | None | None | Stateless. |
| `pdgui_menu_stats.cpp` | 434 | **Ad-hoc** | None | None | Standalone overlay. |
| `pdgui_menu_theme_editor.cpp` | 510 | **Ad-hoc** | None | None | Standalone overlay (B-130 fixed). |
| `pdgui_menu_moddinghub.cpp` | 1258 | **Ad-hoc** | None | None | Standalone overlay. |
| `pdgui_menu_modmgr.cpp` | 1304 | **Ad-hoc** | None | None | Embedded in modding hub. |
| `pdgui_menu_mpingame.cpp` | 356 | **Ad-hoc** | None | None | Kill ticker overlay. |
| `pdgui_menu_audiomod.cpp` | 919 | **Ad-hoc** | None | None | Embedded in modding hub. |
| `pdgui_menu_update.cpp` | 738 | **Ad-hoc** | None | None | Standalone notification. |
| `pdgui_menu_logviewer.cpp` | 373 | **Ad-hoc** | None | None | Debug tool. |
| `pdgui_charpreview.c` | 649 | N/A | None | None | FBO renderer, not a menu. |

### 2.2 Direct SDL Mouse Violations

| File | Line | Call | Classification |
|------|------|------|----------------|
| `pdgui_backend.cpp` | 499-500 | `SDL_ShowCursor(SDL_DISABLE)` + `SDL_SetRelativeMouseMode(SDL_TRUE)` | **Intentional bypass** (B-92 deferred flush for solo mission transition). Documented. |
| `pdgui_menu_pausemenu.cpp` | 230-234 | `SDL_WarpMouseInWindow` + `SDL_GetMouseFocus` + `SDL_GetWindowSize` | **Violation** -- warps mouse to center on pause open. Should use context system. |
| `inputctx.c` | on_push callbacks | `SDL_SetRelativeMouseMode` + `SDL_ShowCursor` | **Redundant** -- same state is set by `inputCtxSyncMouseMode()`. Harmless but architecturally noisy. |

### 2.3 Legacy Stack Issues

| Issue | Location | Severity |
|-------|----------|----------|
| No duplicate rejection in `menuPushDialog` | `menu.c:1520` | MED -- input context stack's dedup is the only protection |
| Sibling loop infinite-loop bug | `menu.c:1541-1543` | LOW -- documented `@bug`, never triggered because no dialogdef has >10 siblings |
| `g_MpPlayerNum` global mutable coupling | Everywhere in menu.c | MED -- save/restore pattern is fragile, no locking |
| Depth underflow on excess pops | `menu.c:menuCloseDialog` | LOW -- depth check exists but no logging |

---

## 3. Root-Cause Taxonomy

### Pattern A: "Context push without matching pop"

**Description**: Menu pushes `g_CtxImGuiMenu` on open (usually in `IsWindowAppearing` block) but has no pop call in any exit path. Relies on external cleanup (`inputCtxShutdown()` during stage transition) to eventually clear the leak.

**Affected files**:
- `pdgui_menu_training.cpp:1195` -- FR Weapon List pushes, never pops
- `pdgui_menu_pausemenu.cpp:1258,1408` -- game-over panels push, never pop

**Root cause**: These menus were written before the B-124 push/pop pattern was established. The "rely on stage transition cleanup" approach works but is fragile -- if the menu is closed without a stage transition (e.g., user navigates back within the same stage), the context is orphaned.

**Impact**: Orphaned `g_CtxImGuiMenu` context prevents gameplay input until stage transition. Mouse stays in absolute mode. Can only be cleared by `inputCtxShutdown()` in the main loop.

### Pattern B: "Pop without push (child relies on parent)"

**Description**: Child menu pops `g_CtxImGuiMenu` on close, but never pushes it. Assumes parent menu already pushed it. If child is ever reached without the parent's push, the pop is a no-op (logged warning) but the menu renders without input context protection.

**Affected files**:
- `pdgui_menu_solomission.cpp:1153,2340,2418` -- pops context pushed by mainmenu

**Root cause**: Intentional delegation pattern. The parent (mainmenu) owns the context lifetime. The child pops as a courtesy on certain exit paths.

**Impact**: Low in practice because the parent always pushes before navigating to child. But violates the principle of each menu owning its own lifecycle.

### Pattern C: "Direct SDL calls bypassing context system"

**Description**: Menu code calls SDL mouse functions directly instead of going through the input context system. This can desynchronize the context system's understanding of mouse state.

**Affected files**:
- `pdgui_menu_pausemenu.cpp:230-234` -- `SDL_WarpMouseInWindow`
- `pdgui_backend.cpp:499-500` -- deferred flush (documented exception)

**Root cause**: The pause menu warp was added before `inputCtxSyncMouseMode()` existed. The B-92 deferred flush was a deliberate workaround for a specific race condition.

### Pattern D: "Static state persists across menu opens"

**Description**: File-scoped `static` variables retain values between menu open/close cycles. If the menu's init path doesn't reset them, stale state from a previous open can affect the new open.

**Affected files**:
- Most menu files have static state, but only some have explicit reset functions
- `pdgui_menu_room.cpp` has `pdguiRoomScreenReset()` (gold standard)
- `pdgui_menu_agentcreate.cpp` uses `IsWindowAppearing` to reset focus but not form data
- `pdgui_menu_solomission.cpp` has no reset function

**Root cause**: Organic development pattern. Some menus were written with reset-on-appear logic, others weren't.

### Pattern E: "Two-stack desynchronization"

**Description**: The legacy menu stack (depth, dialogs) and the modern input context stack are managed independently. If one is pushed/popped without the other, they diverge.

**Mechanism**: ImGui menus typically push both (legacy dialog via `menuPushDialog` + context via `inputCtxPush`). Pop paths may only pop one. The bridge layer (`pdgui_bridge.c`) guards with `inputCtxIsActive()` checks.

**Impact**: When stacks diverge, either: (a) legacy stack thinks menus are open but input context doesn't, causing render without input capture; or (b) input context thinks menus are open but legacy stack doesn't, causing input capture without visible menu.

### Pattern F: "Shadow header #define mismatch"

**Description**: C++ files cannot include `types.h` (bool redefinition). They shadow game header constants with local `#define` values. When the shadow values are wrong, the code compiles and links fine but produces incorrect runtime behavior.

**Affected files**:
- `pdgui_menu_solomission.cpp:282-332` -- 30 language text ID defines are ALL wrong (missing bank prefix)

**Root cause**: The language ID encoding packs a 7-bit bank index in the upper bits and a 9-bit string offset in the lower bits. The shadow defines only used the string offset, producing bank=0 lookups. Bank 0 is always NULL, so `langGet()` returns NULL and `langSafe()` returns `""`.

**Impact**: CRITICAL -- ~15 visible strings in the campaign menu are blank/invisible. See Section 5.

---

## 4. Reinit Audit: Room --> Match --> Room

### 4.1 The Complete Transition Path

```
[Player presses "Return to Room" in endscreen]
    |
    v
pdguiEndscreenExitToMainMenu()    [pdgui_bridge.c:777]
    |-- configSave("pd.ini")
    |-- inputCtxPopDeferred(&g_CtxImGuiMenu) if active
    |-- func0f0f8120()  ->  pops all legacy dialog depth to 0
    |
    v
[menutick.c processes MENUROOT_MPENDSCREEN]
    |-- mpSetPaused(MPPAUSEMODE_UNPAUSED)
    |-- g_Vars.mplayerisrunning = false
    |-- g_Vars.normmplayerisrunning = false   <-- affects music path
    |-- titleSetNextStage(STAGE_CITRAINING)
    |-- setNumPlayers(1)
    |-- mainChangeToStage(STAGE_CITRAINING)
    |
    v
[mainChangeToStage in pdmain.c:744]
    |-- manifestClear + manifestMenuTransition  (if !STAGE_IS_GAMEPLAY)
    |   NOTE: STAGE_CITRAINING IS gameplay, so this path is NOT taken.
    |         The stale match manifest persists. See GAP #1.
    |-- g_MainChangeToStageNum = STAGE_CITRAINING
    |
    v
[Main loop detects g_MainChangeToStageNum >= 0]
    |
    v  TEARDOWN (main.c:1101-1120)
    |-- lvStop()
    |   |-- musicStop()         <-- match music stopped
    |   |-- menuStop()          <-- legacy menu cleanup
    |   `-- (all game subsystems torn down)
    |-- inputCtxShutdown()      <-- ALL contexts popped with on_pop callbacks
    |-- inputCtxInit()          <-- stack zeroed
    |-- inputCtxPush(&g_CtxGameplay)  <-- baseline restored
    |
    v  REINIT (main.c:910-1054)
    |-- g_MainIsEndscreen = false
    |-- joyReset(), dhudReset(), zbufReset()
    |-- lvReset(STAGE_CITRAINING)
    |   |-- musicReset()        <-- clears event queue
    |   |-- modMusicStop()      <-- stops mod PCM
    |   |-- IF normmplayerisrunning: musicSetStageAndStartMusic()
    |   |   ELSE: musicSetStage() only (no playback)
    |   |   NOTE: normmplayerisrunning was set false BEFORE this point.
    |   |         Music does NOT auto-start here. See GAP #2.
    |   `-- (all game subsystems reinitialized)
    |-- viReset()
```

### 4.2 Identified Gaps

#### GAP #1: Stale Match Manifest (Moderate Risk)

**Location**: `pdmain.c:764-773`

`mainChangeToStage(STAGE_CITRAINING)` checks `STAGE_IS_GAMEPLAY(stagenum)`. STAGE_CITRAINING is a gameplay stage (the CI environment with the player walking around). So the `!STAGE_IS_GAMEPLAY` branch that calls `manifestClear(&g_ClientManifest)` + `manifestMenuTransition()` is NOT taken.

The match manifest persists with match-specific entries. If the room screen's character preview needs assets not in the old match manifest (e.g., a body/head that wasn't in the match), those assets won't be loaded via the manifest pipeline. The `manifestEnsureLoaded()` safety net in `bodyAllocateChr()`/`bodyAllocateModel()` catches most cases, but it's O(n) per call.

**Recommendation**: Call `manifestClear(&g_ClientManifest)` explicitly in `pdguiEndscreenExitToMainMenu()` before `func0f0f8120()`. Or add STAGE_CITRAINING to the manifest-clear path in `mainChangeToStage()`.

#### GAP #2: Music Restart Timing (Low Risk)

**Location**: `lv.c:456-460`, `menutick.c:628-654`

`normmplayerisrunning` is set false by `menutick.c:686` BEFORE `mainChangeToStage()`. So `lvReset()` takes the `musicSetStage()` path (no playback). Music eventually restarts when the menu system calls `musicStartMenu()` from `menuPushRootDialog()`. But this depends on the legacy menu root transition path running correctly.

During the black-screen transition, there is a brief silence window. This is cosmetically harmless but architecturally fragile -- if the menu root path changes, music restart could break silently.

**Recommendation**: Make `lvReset()` for STAGE_CITRAINING always call `musicSetStageAndStartMusic()` regardless of `normmplayerisrunning`. The CI training stage should always have ambient music.

#### GAP #3: Double mainChangeToStage on Disconnect (Harmless)

**Location**: `netDisconnect()` and `menutick.c:697`

Both call `mainChangeToStage(STAGE_CITRAINING)`. The second overwrites the first. No functional impact since both target the same stage.

#### GAP #4: Room Screen Stale State (Low Risk)

**Location**: `pdgui_lobby.cpp:176-180`

On networked "Return to Room", `pdguiSetInRoom(1)` is called without `pdguiRoomScreenReset()`. If the room screen had an open modal (bot edit, save scenario), stale state from the pre-match configuration persists. The `s_RoomPushedCtx` flag may also be stale.

**Recommendation**: Call `pdguiRoomScreenReset()` on "Return to Room" unless the intent is to preserve the exact pre-match configuration.

#### GAP #5: menuStop vs. pdguiEndscreenExitToMainMenu Timing (Low Risk)

`pdguiEndscreenExitToMainMenu()` pops the dialog stack but does NOT call `menuStop()`. `menuStop()` is called later by `lvStop()`. Between these two calls, the legacy menu system is in a partially-torn-down state (dialogs popped but system still "active"). If any menu tick occurs in this window, it could process stale state.

---

## 5. Campaign Mission Select Strings (Case Study)

### 5.1 Root Cause: Shadow #define Values Are ALL Wrong

**File**: `pdgui_menu_solomission.cpp:282-332`

The C++ file cannot `#include "types.h"` due to `#define bool s32` breaking C++. It declares shadow `#define` values for language text IDs. **Every single define has the wrong value.**

The language system encodes text IDs as 16-bit values: 7-bit bank index in upper bits, 9-bit string offset in lower bits.

- Real values: `L_OPTIONS_N = 0x5600 + N` (LANGBANK_OPTIONS = 0x2b, shifted left 9 = 0x5600)
- Shadow values: `L_OPTIONS_N = 0x0000 + N` (bank = 0)

When `langSafe(0x007a)` is called, `langGet()` extracts bank=0 from the upper 7 bits. `g_LangBanks[0]` is NULL. `langGet()` returns NULL. `langSafe()` returns `""`.

**30 defines affected**, lines 282-332.

### 5.2 Strings That Are VISIBLY BLANK

These render as empty/invisible because there is no fallback:

| Line | Define | Missing Text | Symptom |
|------|--------|-------------|---------|
| 1028 | `L_OPTIONS_251..253` | "Agent"/"Special Agent"/"Perfect Agent" | Difficulty overlay in detail panel invisible |
| 1206, 1509 | `L_OPTIONS_248` | "Select Difficulty" | Dialog title blank |
| 2115 | `L_OPTIONS_247` | "Briefing" | Briefing dialog title blank |
| 2198 | `L_OPTIONS_178` | "Inventory" | Inventory dialog title blank |
| 2296-2297 | `L_OPTIONS_273` | "Overview" | Accept mission shows "StageName: " (trailing colon) |
| 2415 | `L_OPTIONS_274` | "Accept" | Accept button invisible/unlabeled |
| 2425 | `L_OPTIONS_275` | "Decline" | Decline button invisible/unlabeled |
| 2487-2488 | `L_OPTIONS_172` | "Status" | Pause title shows "StageName: " (no "Status") |
| 2608 | `L_OPTIONS_178` | "Inventory" | Inventory button in pause menu blank |
| 2610 | `L_OPTIONS_173` | "Abort!" | Abort button in pause menu blank |
| 2806 | `L_OPTIONS_174` | "Warning" | Abort confirmation title blank |
| 2813 | `L_OPTIONS_175` | "Do you want to abort the mission?" | Abort confirmation body invisible |
| 2849 | `L_OPTIONS_176` | "Cancel" | Cancel button in abort dialog blank |
| 2869 | `L_OPTIONS_177` | "Abort" | Abort button in abort dialog blank |

### 5.3 Strings That Are MASKED by Fallbacks

These have hardcoded English fallback paths, so the user sees text but it's the wrong source:

| Lines | Defines | Fallback |
|-------|---------|----------|
| 716-724 | `L_OPTIONS_123..131` | `"Mission N"` format |
| 819-825 | `L_OPTIONS_132` | `"Special Assignments"` |
| 1286-1289 | `L_OPTIONS_251..253` | `k_DiffFallbackNames[]` |
| 1383-1384 | `L_MPWEAPONS_221` | `"PD Mode"` |
| 1424, 1638 | `L_OPTIONS_254` | `"Cancel"` |
| 1573-1574 | `L_OPTIONS_251..253` | `k_DiffFallbackNames[]` |
| 1867-1868 | `L_OPTIONS_256/267` | `"Radar On"` |
| 1908-1909 | `L_OPTIONS_257` | `"Friendly Fire"` |
| 1960-1962 | `L_OPTIONS_258` | `"Perfect Buddy"` |
| 2011-2014 | `L_OPTIONS_259/260/269/270` | `"Continue"`/`"Cancel"` |

### 5.4 Additional String Issue: L_MPWEAPONS_221 OOB

`L_MPWEAPONS_221` is defined as `0x80dd`. `0x80dd >> 9 = 0x40 = 64`. This exceeds the `g_LangBanks[69]` array bounds (bank 0x40 = 64 is within bounds but the value 0x80dd decodes to bank 64 which is likely uninitialized). This is a potential OOB read in `langGet()`.

### 5.5 Static Init Order Issue

**File**: `pdgui_menu_solomission.cpp:2604-2611`

The pause menu's `k_Btns` array captures `langSafe()` return values at static-local initialization time. If the language bank is not loaded when `renderSoloPauseMenu()` is first called, these values become permanently empty strings for the lifetime of the process.

### 5.6 Fix

The correct shadow values follow the formula:
- `L_OPTIONS_N` = `0x5600 + N`
- `L_MPWEAPONS_N` = `0x5400 + N`

Better long-term fix: Create a shared C header (`port/include/langids.h`) that provides the text ID constants without including `types.h`. C++ files include this header instead of shadowing.

---

## 6. Full Campaign Menu System Audit

### 6.1 Campaign Flow State Machine

```
                          MAIN MENU
                              |
                    [Solo Missions / Campaign]
                              |
                    MISSION SELECT (two-panel)
                    left: mission list
                    right: detail + difficulty + objectives
                              |
                    [Select mission -> detail panel]
                              |
                    DIFFICULTY SELECT (inline picker)
                              |
                    BRIEFING (expandable text)
                              |
                    ACCEPT MISSION
                    [Accept] -> stage load
                    [Decline] -> back to select
                              |
                    =========GAMEPLAY==========
                              |
                    [Esc]  SOLO PAUSE MENU
                           Resume | Objectives | Inventory
                           Options | Abort | Restart
                              |
                    =======MISSION END=========
                              |
                    SOLO ENDSCREEN (completed/failed)
                    [Retry] -> reload same stage
                    [Next Mission] -> next stage
                    [Exit] -> main menu
```

### 6.2 Menu Pages in Campaign Flow

| Menu | File | Dialogdef | State Machine Entry | State Machine Exit |
|------|------|-----------|---------------------|-------------------|
| Mission Select | solomission.cpp | `g_SelectMissionMenuDialog` | `menuPushDialog` from mainmenu | Back: `menuPopDialog` |
| Difficulty Select | solomission.cpp | `g_DifficultySelectMenuDialog` | Inline in right panel | Back: reset focus |
| Pre-Briefing | solomission.cpp | `g_SoloPrebriefingMenuDialog` | `menuPushDialog` from select | Back: `menuPopDialog` |
| Briefing | solomission.cpp | `g_SoloBriefingMenuDialog` | Pushed from pre-briefing | Back: `menuPopDialog` |
| Accept Mission | solomission.cpp | `g_AcceptMissionMenuDialog` | Pushed from briefing | Accept: `menuhandlerAcceptMission` / Decline: `menuPopDialog` |
| Solo Pause | solomission.cpp | `g_SoloPauseMenuDialog` | Esc during gameplay | Resume: pop / Options: push settings |
| Pause Options | solomission.cpp | `g_SoloPauseOptionsMenuDialog` | Push from pause | Back: `menuPopDialog` |
| Restart Confirm | solomission.cpp | Inline in pause render | s_RestartConfirm flag | Cancel: reset flag |
| Abort Confirm | solomission.cpp | `g_SoloAbortMissionMenuDialog` | Push from pause | Cancel: `menuPopDialog` / Abort: stage transition |
| Solo Completed | endscreen.cpp | `g_SoloMissionCompletedMenuDialog` | End of stage (success) | Retry/Next/Exit buttons |
| Solo Failed | endscreen.cpp | `g_SoloMissionFailedMenuDialog` | End of stage (failure) | Retry/Exit buttons |
| Co-op Options | solomission.cpp | `g_CoopOptionsMenuDialog` | From room screen co-op tab | Back: `menuPopDialog` |
| Counter-Op Options | solomission.cpp | `g_CounterOpOptionsMenuDialog` | From room screen counter-op tab | Back: `menuPopDialog` |

### 6.3 Issues Found

#### CRITICAL: All language string IDs wrong (Pattern F)
See Section 5. This is the primary cause of "strings not rendered / invisible."

#### MODERATE: No reset function
`pdgui_menu_solomission.cpp` has no `pdguiSoloMissionReset()` function. Static state (`s_MissionSelectIdx`, `s_DetailDiffIdx`, etc.) persists across opens. If the player completes a mission, exits, and re-enters mission select, the cursor position and difficulty selection from the previous visit persist.

#### MODERATE: Context pop without push
Lines 1153, 2340, 2418 pop `g_CtxImGuiMenu` without ever pushing it. This is the Pattern B issue -- the parent mainmenu pushes, and solomission pops as a courtesy. This works but violates the "each menu owns its lifecycle" principle.

#### LOW: Difficulty overlay rendering path
Lines 1020-1040 render difficulty names as `dl->AddText()` overlay on the detail panel. These use `langSafe()` with the broken shadow defines. Even with correct defines, the overlay text is drawn with `pdguiPalImU32(PDPAL_TITLE_TEXT)` which may have low alpha depending on palette.

#### LOW: Co-op/Counter-Op modes share state
`s_CoopAntiDiffSelectIdx` and `s_CoopAntiOptSelectIdx` are shared between co-op and counter-op. If the player configures co-op, switches to counter-op, the difficulty selection persists.

### 6.4 Reads From / Writes To

| Menu | Reads | Writes |
|------|-------|--------|
| Mission Select | `g_SoloStages[21]`, `besttimes[]`, `isStageDifficultyUnlocked()`, `catalogIdByRuntime()`, lang bank | `s_MissionSelectIdx`, `s_DetailDiffIdx` |
| Accept | `g_Briefing.*`, `sm_missionconfig.*` | `sm_missionconfig.stage_id`, `sm_missionconfig.stagenum`, `sm_missionconfig.diff` |
| Solo Pause | `g_MissionConfig.*`, `objectiveIsComplete()` | `s_PauseSelectIdx` |
| Abort | (reads nothing) | Calls `titleSetNextStage(STAGE_CITRAINING)` + `mainChangeToStage()` |
| Endscreen | `g_MissionConfig.*`, `besttimes[]`, `cheatsGet()` | Calls `saveSaveAgent()`, `pdguiEndscreenStartMission/NextMission/ExitToMainMenu` |

### 6.5 Smelly Interactions with MP/Combat Flow

1. **Shared endscreen bridge functions**: `pdguiEndscreenExitToMainMenu()` is used by both solo and MP endscreens. The function pops `g_CtxImGuiMenu` and calls `func0f0f8120()`. Both flows pass through the same code, but the legacy menu root handler (`menutick.c`) branches on `g_MenuData.root` to determine the stage transition target.

2. **Co-op/Counter-Op as hybrid**: These modes use solo mission data structures (`sm_missionconfig`, `g_Briefing`) but are launched from the room screen (MP flow). The room screen's co-op tab writes to `s_CampaignMission` and `s_CampaignDiff`, then pushes the co-op options dialog from `pdgui_menu_solomission.cpp`. This creates a cross-file dependency where the room screen and the solo mission menu share state through static variables.

---

## 7. Arena List (Case Study)

### 7.1 Current State

**Data model**: Flat `arena_entry { name[64], id[64], stagenum }`. No category, no source type, no sort key.

**Population**: `buildArenaListFromCatalog()` calls `assetCatalogIterateByType(ASSET_ARENA, catalogArenaCollect, NULL)`. The callback copies name, id, stagenum from each catalog entry. **Category and bundled flag are discarded.**

**Sort order**: None. Entries appear in hash-table insertion order (base game groups first in `s_ArenaGroupMap` order, then mods in `mod.json` array order).

**UI**: Flat `ImGui::BeginCombo` / `ImGui::Selectable` loop. No grouping, no separators, no tree nodes.

### 7.2 Catalog Data Available But Unused

Each `asset_entry_t` in the catalog carries:
- `category` -- `"Dark"`, `"Solo Missions"`, `"Classic"`, `"Bonus"`, `"Random"` for base game; mod ID string for mods
- `bundled` -- `1` for base game, `0` for mods
- `ext.arena.stagenum`, `ext.arena.requirefeature`, `ext.arena.name_langid`

This data is sufficient to classify every arena into the three desired sections:
- **Base Game Multiplayer**: `bundled == 1 && category != "Solo Missions"`
- **Base Game Campaign**: `bundled == 1 && category == "Solo Missions"`
- **Mod Maps**: `bundled == 0`

### 7.3 Proposed Data Model

```c
// Extended arena entry
struct arena_entry {
    char name[64];
    char id[64];
    s32  stagenum;
    char category[32];    // NEW: "Dark", "Solo Missions", "gf64", etc.
    s32  bundled;         // NEW: 1 = base game, 0 = mod
    s32  section;         // NEW: derived: 0=MP_BASE, 1=CAMPAIGN, 2=MOD
};

enum {
    ARENA_SEC_MP_BASE = 0,   // Dark, Classic, Bonus, Random
    ARENA_SEC_CAMPAIGN,      // Solo Missions
    ARENA_SEC_MOD,           // bundled == 0
    ARENA_SEC_COUNT
};
```

### 7.4 Proposed Sort/Section Logic

After `buildArenaListFromCatalog()`:
1. Derive `section` from `bundled` + `category`
2. `qsort(s_Arenas, s_NumArenas, sizeof(arena_entry), arenaCompare)`
   - Primary: section (MP_BASE < CAMPAIGN < MOD)
   - Secondary: alphabetical by name (`strcasecmp`)
3. Compute section boundaries: `s_SectionStart[ARENA_SEC_COUNT]` and `s_SectionCount[ARENA_SEC_COUNT]`

### 7.5 Proposed UI Sketch

```
[Arena Picker Combo]
  |- [v] Multiplayer Arenas (13)         <- CollapsingHeader, default open
  |     Area 52
  |     Car Park
  |     Complex
  |     ...
  |- [v] Campaign Maps (14)              <- CollapsingHeader, default open
  |     Area 51: Infiltration
  |     Chicago: Stealth
  |     ...
  |- [v] Mod Maps (N)                    <- CollapsingHeader, default open
  |     Dark Noon: Main Street
  |     GoldenEye: Facility
  |     ...
```

The existing `ImGui::CollapsingHeader` pattern is already used in `pdgui_menu_room.cpp:1565` ("Simulant Profiles") and `pdgui_menu_modmgr.cpp:622-641` (category sections with `TreeNodeEx`).

### 7.6 Catalog Query

No new query API needed. The existing `assetCatalogIterateByType(ASSET_ARENA, ...)` callback just needs to copy the additional fields (`category`, `bundled`) during collection.

---

## 8. Recommended Rework Plan

### 8.1 Assessment: Targeted Fix, Not Top-Down Rework

The core architecture (input context stack, hotswap bridge, two-phase render) is **sound**. The problems are:

1. **Inconsistent application** of the B-124 push/pop pattern (3 files broken/partial)
2. **Shadow header bug** causing campaign menu strings to be invisible (1 file)
3. **Missing data** in the arena list collection (1 file, data model change)
4. **Minor reinit gaps** (manifest clear, music restart)

A top-down rework is **not warranted**. The 10 conformant menu files prove the pattern works when applied correctly. The fix is to audit and repair the 3-6 non-conformant files.

### 8.2 Layered Fix Strategy

Following the foundation --> identity --> interface --> experience layering:

**Layer 0 -- Foundation (do first, everything depends on this)**
1. Fix shadow `#define` values in `pdgui_menu_solomission.cpp` (or create `langids.h` shared header)
2. Add missing `inputCtxPopDeferred` calls to training.cpp and pausemenu.cpp game-over panels
3. Add `manifestClear(&g_ClientManifest)` to the return-to-room path

**Layer 1 -- Consistency (apply B-124 pattern uniformly)**
4. Audit each Partial/Ad-hoc menu for whether it needs its own context push/pop (most ad-hoc menus correctly delegate to parent -- verify)
5. Remove direct `SDL_WarpMouseInWindow` from pausemenu.cpp (use context system instead)
6. Add `pdguiSoloMissionReset()` function to clear static state on open

**Layer 2 -- Features (arena list, campaign polish)**
7. Extend `arena_entry` struct with category/bundled/section
8. Add qsort + CollapsingHeader sections to arena picker
9. Fix static init order for pause menu `k_Btns` array

**Layer 3 -- Robustness**
10. Add duplicate rejection to `menuPushDialog` (or accept that input context dedup is sufficient)
11. Remove redundant SDL calls from inputctx.c on_push callbacks (optional cleanup)
12. Add logging to menuPopDialog underflow path

---

## Appendix A: File Reference

| File | Path | Lines | Role |
|------|------|-------|------|
| menu.c | src/game/menu.c | 6571 | Legacy menu stack |
| menutick.c | src/game/menutick.c | ~700+ | Menu tick + root transitions |
| inputctx.c | port/src/inputctx.c | 564 | Input context stack |
| inputctx.h | port/include/inputctx.h | 116 | Context struct definition |
| pdgui_bridge.c | port/fast3d/pdgui_bridge.c | 1524 | C/C++ bridge layer |
| pdgui_backend.cpp | port/fast3d/pdgui_backend.cpp | 772 | ImGui backend + event pipeline |
| pdgui_hotswap.cpp | port/fast3d/pdgui_hotswap.cpp | 375 | Dialog hotswap engine |
| pdgui_menu_solomission.cpp | port/fast3d/pdgui_menu_solomission.cpp | 3351 | Campaign menu system |
| pdgui_menu_room.cpp | port/fast3d/pdgui_menu_room.cpp | 2871 | Room/lobby screen |
| pdgui_menu_training.cpp | port/fast3d/pdgui_menu_training.cpp | 2143 | Training mode menus |
| pdgui_menu_pausemenu.cpp | port/fast3d/pdgui_menu_pausemenu.cpp | 1434 | MP pause + game-over |
| pdgui_menu_endscreen.cpp | port/fast3d/pdgui_menu_endscreen.cpp | 1161 | Solo/MP endscreens |
| pdgui_menu_mainmenu.cpp | port/fast3d/pdgui_menu_mainmenu.cpp | 3534 | Main menu + CI options |
| assetcatalog_base.c | port/src/assetcatalog_base.c | ~1500+ | Base asset registration |
| netmanifest.c | port/src/net/netmanifest.c | ~1500+ | Asset manifest system |
| music.c | src/game/music.c | ~2000+ | Music system |
| lv.c | src/game/lv.c | ~2700+ | Level reset/stop |
| pdmain.c | port/src/pdmain.c | ~800+ | PC main + stage transitions |

## Appendix B: Bug Cross-References

| Bug ID | Relevance to This Audit |
|--------|------------------------|
| B-117 | Input context leak on match exit -- FIXED, established the inputCtxShutdown pattern |
| B-121 | Endscreen menu not interactive -- FIXED, established the "push g_CtxImGuiMenu on appear" pattern |
| B-122 | Deferred flush desync -- FIXED, established pdguiIsActive() guard and inputCtxSyncMouseMode |
| B-124(a-d) | Input system suite -- FIXED, established key suppression, nav mapping, device gate removal |
| B-130 | Theme editor close lifecycle -- FIXED, moved to BeginPopupModal |
| B-131 | Double-menu reopen race -- FIXED, established 150ms close grace + key edge clear |
| B-132 | Saved bindings cross-contamination -- FIXED, established first-match-wins in loadBinds |
