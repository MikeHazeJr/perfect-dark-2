# Input System Repair Plan

## Executive Summary

The action map architecture is conceptually sound and mostly wired correctly, but has several
concrete implementation gaps and at least two structural bugs. The most serious issues are:
(1) `inputReadController` sets `npad->stick_x/y` from digital ACTION_MOVE_* but never from the
    analog ACTION_AXIS_MOVE_X/Y value, leaving analog stick movement dead in the OSContPad path;
(2) the `actionmapSaveBinds` / `actionmapLoadBinds` functions only operate on `g_ImcGameplay`
    binds — they silently drop changes made to other IMCs (Vehicle, PauseMenu, etc.);
(3) `pdguiDriveImGuiNav` injects held-state nav events even when no ImGui window is focused,
    causing phantom arrow-key events when the game is running normally.

---

## Architecture Overview

### Original (CK_* / input.c-centric)
- `input.c` owned a flat `binds[]` array mapping N64 controller keys (`CK_*`) to virtual keys (`VK_*`).
- `inputReadController()` iterated the bind table each frame, building an `OSContPad` bitmask.
- Analog sticks were read directly from `SDL_GameControllerGetAxis()` inside `inputReadController`,
  scaled, and written into `npad->stick_x / stick_y / rstick_x / rstick_y`.
- Mouse delta was consumed only inside `bondmove.c` via `inputMouseGetScaledDelta()`.
- There was no context separation — all input went everywhere all the time.

### Current (action map architecture)
- `actionmap.cpp` is the sole input authority. It owns `InputMappingContext` (IMC) structs,
  each holding a set of `InputAction → VK` bindings.
- `actionmapDispatch(ev)` converts raw SDL events into per-player `ActionState` (held/pressed/released).
  Called from `pdguiProcessEvent()` inside `gfx_sdl2.cpp`'s SDL event loop.
- `actionmapPollFrame()` samples analog sticks (SDL_GameControllerGetAxis) and mouse delta each
  frame; writes into `s_State[p][ACTION_AXIS_*]`. Called from `pdsched.c` before game logic.
- `actionmapEndFrame()` clears edge (pressed/released) signals. Called at end of event loop in `gfx_sdl2.cpp`.
- `bondmove.c` reads `actionHeld/actionPressed/actionValue` directly. It also still calls
  `inputMouseGetScaledDelta()` for the freelook / mouse-aim path.
- `inputReadController()` bridges to the legacy `OSContPad` struct used by N64 game code that has
  not yet been migrated to direct actionmap queries.
- `inputctx.c` runs a context stack (gameplay / ImGui menu / pause / debug). The stack top
  determines whether analog axes are zeroed and how events are routed.

---

## Input Path Analysis

---

### 1. Keyboard Movement (WASD)

**Original behavior:**
WASD was bound to `CK_C_D/U/R/L` (C-button positions). `inputReadController` checked those binds
each frame and set `CONT_* button bits` plus `npad->stick_x/y` at maximum deflection (±0x7F/0x80).

**Current behavior:**
`actionmapDispatch` fires `ACTION_MOVE_FORWARD/BACKWARD/LEFT/RIGHT` on SDL_KEYDOWN/UP.
`actionmapPollFrame` synthesizes `ACTION_AXIS_MOVE_X/Y` values from those actions:
```
if (mx != 0.0f || my != 0.0f) { s_State[0][ACTION_AXIS_MOVE_X].value = mx; ... }
```
`bondmove.c` reads `actionValue(contpad1, ACTION_AXIS_MOVE_X/Y)` and scales to `c1stickx/c1sticky`
(±80 range). These are then passed into `movedata.analogstrafe/analogwalk`.

**Broken because:**
`inputReadController` also writes `npad->stick_x/y` from the digital ACTION_MOVE_* states only
(lines 670-673). This path yields only ±0x7F or 0 — no analog scaling. That is correct behavior
for the digital-only path. However it is the *only* stick population in `inputReadController`.
The `joyGetStickX/Y` functions read from `g_JoyDataPtr->samples[curlast].pads[n].stick_x` which
is populated by `osContGetReadData` → `inputReadController`. So any game-code path that uses
`joyGetStickX/Y` instead of direct `actionValue()` calls will only get digital ±128/0 from WASD
and will never get the analog value from a gamepad left stick.

The deeper issue: `bondmove.c` reads axes via `actionValue()` directly (correct), but
`joyCountButtonsOnSpecificSamples` and legacy callers read from the joy sample buffer, which is
filled by `inputReadController`, which never writes the analog stick values from actionmap.

**Fix:**
In `inputReadController` (port/src/input.c), after the digital stick lines, add a fallback: if
stick_x is still 0 and no digital key is held, read `actionValue(idx, ACTION_AXIS_MOVE_X/Y)` and
scale to the s8 range. Similarly populate `rstick_x/rstick_y` from `ACTION_AXIS_AIM_X/Y` if
zero and the gamepad aim axis has a value. This ensures the joy sample buffer reflects analog
deflection for any code paths still reading from it.

Specific location: `port/src/input.c`, `inputReadController()`, after line 673 and before the
`cancelCButtons` block.

---

### 2. Controller Analog Sticks (Look/Move Axes)

**Original behavior:**
`inputReadController` called `SDL_GameControllerGetAxis` directly with the `axisMap[][]` from
`padsCfg`, applied deadzone/sensitivity, and wrote `npad->stick_x/y` and `npad->rstick_x/y`.

**Current behavior:**
`actionmapPollFrame` reads `SDL_GameControllerGetAxis` directly (using SDL's own player-index
lookup, not the `padsCfg.axisMap[][]`), applies `s_StickDeadzone` and `s_StickSensitivity`,
negates Y-axis, writes `s_State[p][ACTION_AXIS_MOVE_X/Y]` and `s_State[p][ACTION_AXIS_AIM_X/Y]`.
`bondmove.c` reads these via `actionValue()`. So the primary game path works.

**Broken because:**
Two divergent sensitivity/deadzone systems now coexist:
1. `padsCfg[].sens[]` and `padsCfg[].deadzone[]` — used by `inputAxisScale()` in the old path,
   now orphaned (no callers update the actionmap after these change).
2. `s_StickSensitivity` and `s_StickDeadzone` in actionmap — used by `actionmapPollFrame`.

When the user changes sensitivity in the Options menu, it writes to `padsCfg[].sens[]` (the
original system). `actionmapPollFrame` reads `s_StickSensitivity` which is a separate variable.
The settings do not cross-talk. The user will see no effect from sensitivity changes.

Additionally, `actionmapPollFrame` uses `SDL_GameControllerFromPlayerIndex(p)` to find the
controller, but `padsCfg[].axisMap[][]` contains the per-player axis swap configuration from
`inputControllerSetSticksSwapped`. `actionmapPollFrame` uses its own `s_SwapSticks` flag which is
a global single toggle, not per-player. For players 2-4 this will be wrong if their controllers
are configured differently.

**Fix:**
1. In the Options menu sensitivity/deadzone change handlers, call `actionmapSetStickSensitivity()`
   and `actionmapSetStickDeadzone()` in addition to (or instead of) writing `padsCfg[].sens[]`.
   Alternatively, make `actionmapPollFrame` read sensitivity from `padsCfg` instead of the
   separate `s_StickSensitivity` variable.
2. In `actionmapPollFrame`, replace `SDL_GameControllerFromPlayerIndex(p)` with
   `pads[p]` (the controller pointer from `padsCfg`) and use `padsCfg[p].axisMap[][]` to
   determine which SDL axis corresponds to move/aim for that player. This requires either
   exposing `pads[]` from input.c or adding a function `inputGetController(p)`.

---

### 3. Controller Buttons (Fire, Aim, Crouch, etc.)

**Original behavior:**
`inputReadController` iterated the `binds[]` array (CK_* → VK_*), checked `inputKeyPressed()`,
and set `CONT_* bits` in `npad->button`.

**Current behavior:**
`inputReadController` contains a `s_ContToAction[]` table that maps `CONT_*` bits to
`InputAction` values, and calls `actionHeld(idx, action)` for each. The bitmask is also built
redundantly in `bondmove.c` (lines 952-989) directly from `actionHeld/actionPressed`. These two
paths will produce identical results in the common case.

**Broken because:**
`s_ContToAction[]` is missing several actions that the `bondmove.c` path includes:
- `ACTION_SPRINT` → not in `s_ContToAction[]` at all (no CONT_* bit mapped)
- `ACTION_JUMP` → mapped to `CONT_4000` in bondmove but `CONT_4000` entry in `s_ContToAction`
  maps to `ACTION_CROUCH`. These overlap; `CONT_4000` is defined as `BUTTON_HALF_CROUCH`.
- `ACTION_FIRE_PRIMARY` → `CONT_G` (Z_TRIG) in `s_ContToAction` — this is correct.
- The comment at line 691-694 claims stickCButtons path was removed, but the old
  `stickCButtons` path in the original code read the right stick and translated to C-button
  presses. That path is now handled by `actionmapDispatch`'s `handleAxisDigital` (correct
  conceptually), but those synthetic VKs (`JOFS_RSTICK_*`) must be bound in the IMC to
  `ACTION_CBUTTON_*`. They are bound in `setupGameplayDefaults` — this looks correct.

The bigger concern: any game code that calls `joyGetButtons()` / `joyGetButtonsPressedThisFrame()`
returns 0 (they are stubs). `joyCountButtonsOnSpecificSamples` reads from the joy sample buffer
which *is* populated by `osContGetReadData` → `inputReadController`. So that path works for buttons.
However it reads from `npad->button` built by `s_ContToAction`, not the fuller bondmove table.

**Fix:**
1. Audit `s_ContToAction[]` against the bondmove table for completeness. Specifically, clarify
   what CONT_4000 means — if it is `BUTTON_HALF_CROUCH` (partial crouch), ensure the mapping is
   to `ACTION_CROUCH`. If `BUTTON_JUMP = CONT_4000` is also needed, it must get a different bit
   or `ACTION_JUMP` must be added to `s_ContToAction` with a distinct CONT bit.
2. Document which game subsystems still rely on `joyCountButtonsOnSpecificSamples` vs. direct
   actionmap queries. Any subsystem using the joy sample path is limited to what `inputReadController`
   wrote into `npad->button`.

---

### 4. Pause / Escape Handling

**Original behavior:**
Escape was bound to `CK_START` (start button). The old input.c path called `inputKeyJustPressed(VK_ESCAPE)`
and synthesized `START_BUTTON` in the pad. The game checked `c1buttonsthisframe & START_BUTTON`.

**Current behavior:**
`ACTION_PAUSE` is bound to `VK_ESCAPE` and `JOY_BTN(0, JBTN_START)` in `g_ImcGameplay`.
`bondmove.c` line 979 sets `c1buttonsthisframe |= START_BUTTON` when `actionPressed(pi, ACTION_PAUSE)`.
The old parallel path (`inputKeyJustPressed(VK_ESCAPE)`) was explicitly removed at line 1040-1043.

`pdguiProcessEvent` also uses F12 / raw key checks for debug overlay toggling before
`actionmapDispatch` is called (lines 682-702). These consume the event so actionmap never sees it.

**Broken because:**
The context stack's key-suppression grace window (INPUTCTX_PUSH_GRACE_MS) suppresses key-down
events shortly after a context is pushed. If Escape is the key that triggers the menu context
push, the menu IMC will not see the same Escape keydown because it is suppressed. The sequence is:
1. Player presses Escape (gameplay context active).
2. `pdguiProcessEvent` calls `actionmapDispatch(ev)` with the keydown — ACTION_PAUSE fires.
3. bondmove reads `actionPressed(ACTION_PAUSE)`, calls `playerPause()`, which pushes the pause
   menu context.
4. `inputCtxShouldSuppressKey` now returns 1 for the SAME event — but we have already past the
   suppress check (which is called before actionmapDispatch).

This appears fine for the open path. The close path: player presses Escape again.
`imcActivate(&g_ImcPauseMenu)` fires `ACTION_MENU_CANCEL` and `ACTION_PAUSE` simultaneously
because both are bound to `VK_ESCAPE` in the pause menu IMC (lines 1344-1344). The game may
interpret both: cancel closes the menu AND pause fires the pause logic again.

**Fix:**
In `g_ImcPauseMenu`, do NOT bind VK_ESCAPE to both `ACTION_PAUSE` and `ACTION_MENU_CANCEL`.
Use only `ACTION_MENU_CANCEL` for Escape in the pause context. The game's pause logic should
check `actionPressed(ACTION_MENU_CANCEL)` to close the pause menu, not `ACTION_PAUSE`. Or,
restructure so that `ACTION_PAUSE` is the open/close toggle and `ACTION_MENU_CANCEL` is
"navigate back one level within the pause menu". These must not overlap on the same VK.

---

### 5. Menu Navigation (Up/Down/Select in Menus)

**Original behavior:**
Menus read `npad->button` from `osContGetReadData`, looking for `U_JPAD / D_JPAD / A_BUTTON /
B_BUTTON`. The keyboard path bound arrow keys to `CK_DPAD_*`.

**Current behavior:**
`pdguiDriveImGuiNav()` translates actionmap states into ImGui key events each frame:
- `ACTION_MENU_ACCEPT` → `ImGuiKey_Enter` (edge press/release)
- `ACTION_MENU_CANCEL` → `ImGuiKey_Escape`
- `ACTION_MENU_UP/DOWN/LEFT/RIGHT` → ImGui arrow keys (held state)

Both keyboard (arrow keys) and gamepad (D-pad + left stick) are bound to the menu actions in
`g_ImcMenu` (lines 1309-1330).

**Broken because:**
Three specific problems:

(a) `pdguiDriveImGuiNav` is called every frame from `pdguiNewFrame()`, which is called even when
no menu is active (it returns early if `!g_PdguiActive` but only after already calling
`pdguiDriveImGuiNav` at line 333). When the gameplay context is the only active context, arrow
key presses still fire `ACTION_MENU_UP` etc. in `g_ImcGameplay` (which has `ACTION_AIM_*` on
arrow keys, not menu actions — so they will NOT fire menu actions in gameplay). However, if any
ImGui window is inadvertently open (e.g., the live console), `io.AddKeyEvent(ImGuiKey_UpArrow)`
will navigate that window even if the game is running. The guard should check `pdguiWantsInput()`
before injecting nav events.

(b) The `g_ImcMenu` IMC is always active — it is activated in `actionmapInit()` alongside
`g_ImcGameplay`. Menu actions (ACTION_MENU_UP etc.) will fire during gameplay if the player
presses arrow keys or D-pad. For keyboard players this is benign (arrow keys are not otherwise
bound in gameplay), but for gamepad players, D-pad is bound to `ACTION_CBUTTON_*` in gameplay
AND to `ACTION_MENU_DOWN/UP` in the menu IMC. Because `g_ImcMenu` has higher priority (10) than
`g_ImcGameplay` (0), D-pad will resolve to `ACTION_MENU_*` instead of `ACTION_CBUTTON_*` during
gameplay. This means D-pad does NOT work as C-buttons in-game; it drives menu navigation instead.

(c) The menu IMC binds left-stick synthetic VKs (`JOFS_LSTICK_*`) to `ACTION_MENU_UP/DOWN/LEFT/RIGHT`
for all four players (lines 1320-1323). In gameplay, the left stick generates the same synthetic
VKs and fires them through `handleAxisDigital`. Because `g_ImcMenu` (priority 10) is consulted
before `g_ImcGameplay` (priority 0) in `fireVk`, the left stick will resolve to `ACTION_MENU_*`
not `ACTION_MOVE_*`. Left-stick movement during normal gameplay will only work because
`actionmapPollFrame` bypasses `fireVk` entirely for the analog axis values — but the *digital
threshold crossing* from the stick (which drives `ACTION_MOVE_FORWARD` etc.) will be suppressed
by the menu IMC claiming those VKs first.

**Fix:**
(b+c) Do NOT activate `g_ImcMenu` unconditionally in `actionmapInit`. Instead, activate it only
when a menu is actually open. During gameplay, only `g_ImcGameplay` (and optionally the debug
overlay IMC) should be active. The menu IMC should be activated when the pause menu / main menu /
any overlay is opened, and deactivated when it closes. This requires wiring IMC activation/deactivation
to the same calls that push/pop the input context stack.

(a) In `pdguiDriveImGuiNav`, guard the nav injection with `if (pdguiWantsInput())` to avoid
spurious nav events when no ImGui menu is focused.

---

### 6. Binding Save / Load

**Original behavior:**
`inputSaveBinds()` serialized the `binds[]` array to pd.ini. `inputLoadBinds()` parsed it back.

**Current behavior:**
`actionmapInit()` registers all bind strings with `configRegisterString()` (key: `ActionMap.P%d.%s`).
`actionmapLoadBinds()` (called after `configLoad`) parses the strings and populates `g_ImcGameplay`.
`actionmapSaveBinds()` builds the bind strings from the active IMCs and stores into `s_BindStr[][]`.
It is called at shutdown (`main.c:121`) and from various UI paths.

**Broken because:**
`buildBindStr()` (called by `actionmapSaveBinds`) walks `s_Active[]` — the list of currently
active IMCs. It takes the first active IMC that has a mapping for the action. During normal
gameplay, only `g_ImcGameplay` and `g_ImcMenu` are active (per `actionmapInit`). Vehicle, pause
menu, and debug overlay IMCs are never activated, so binds for those IMCs are never serialized.
If a user customizes Vehicle controls, those binds exist in `g_ImcVehicle.mappings[]` but will
never be written to `s_BindStr` by `actionmapSaveBinds`.

`actionmapLoadBinds()` only loads into `g_ImcGameplay` (line 1105: `parseBindStr(&g_ImcGameplay, ...)`).
Custom bindings for Vehicle, Debug, etc. cannot be loaded even if they were saved.

Additionally, the `buildBindStr` function only searches active IMCs. If the Vehicle IMC is
temporarily activated (player enters a vehicle), then `actionmapSaveBinds` is called, it would
see Vehicle binds — but this depends on save timing, making bind persistence non-deterministic.

**Fix:**
`actionmapSaveBinds` should iterate ALL known IMCs regardless of active state:
`g_ImcGameplay`, `g_ImcVehicle`, `g_ImcMenu`, `g_ImcPauseMenu`, `g_ImcDebugOverlay`,
`g_ImcTextInput`. For each IMC, build bind strings from its `mappings[]` array.
`actionmapLoadBinds` should similarly load into each IMC, not just `g_ImcGameplay`. Use the
IMC name as part of the pd.ini key prefix (e.g., `ActionMap.gameplay.P0.MoveForward`) to avoid
key collisions, or maintain a per-IMC namespace.

This is a significant refactor. Short-term workaround: save `g_ImcVehicle` alongside
`g_ImcGameplay` in `buildBindStr` by iterating both unconditionally instead of walking
`s_Active`.

---

### 7. ImGui Nav / Dev Window Input Bleed

**Original behavior:**
No ImGui overlay existed in the original port.

**Current behavior:**
`pdguiIsActive()` returns true when the input context stack top is not `g_CtxGameplay`. When
true, `inputReadController` returns zeroed stick and button data (lines 645-652). `inputUpdateMouse`
also zeroes mouse state when active. So the game sees no input while ImGui is open.

`g_CtxDebugOverlay` only consumes keyboard and mouse events — controller axes and buttons pass
through to actionmap. This means gamepad buttons will still fire game actions even when the debug
overlay is open.

**Broken because:**
(a) `g_CtxDebugOverlay.can_consume` (lines 476-493) does NOT include `SDL_CONTROLLERBUTTONDOWN`,
`SDL_CONTROLLERBUTTONUP`, or `SDL_CONTROLLERAXISMOTION`. These events are NOT consumed by the
debug overlay context, so they flow through to `actionmapDispatch` which fires game actions.
When the developer opens F12 while in a mission, pressing A on the gamepad will fire
`ACTION_USE` (interact / open door) behind the debug menu.

(b) `actionmapPollFrame` checks `inputCtxGetTop() != &g_CtxGameplay` to zero gameplay axes. But
`g_CtxDebugOverlay` is the top context when F12 is open, so axes ARE zeroed correctly for the
analog polling path. The problem is only with the event-driven digital path (a) above.

(c) `pdguiDriveImGuiNav` calls `io.AddKeyEvent` unconditionally, which will inject arrow-key
events into ImGui whenever those keys are held — even before the guard check returns. As noted
in Path 5(a), this should only inject when an ImGui menu actually wants input.

**Fix:**
(a) Add `SDL_CONTROLLERBUTTONDOWN`, `SDL_CONTROLLERBUTTONUP`, `SDL_CONTROLLERAXISMOTION`,
`SDL_JOYBUTTONDOWN`, `SDL_JOYBUTTONUP`, `SDL_JOYAXISMOTION` to `debugOverlayCanConsume()` in
`port/src/inputctx.c`. The debug overlay should consume all input just like the ImGui menu
context does.

(b) Already handled by axis zeroing in `actionmapPollFrame` — no change needed.

(c) Guard `pdguiDriveImGuiNav` with `if (!pdguiWantsInput()) return;` as the first line of the
function in `port/fast3d/pdgui_backend.cpp`.

---

## Fix Priority Order

1. **[CRITICAL] Menu IMC always-active causes D-pad / left stick to be stolen from gameplay.**
   `g_ImcMenu` at priority 10 is activated at init and never deactivated. It claims D-pad VKs
   and left-stick synthetic VKs before `g_ImcGameplay` can process them. D-pad does not work as
   C-buttons in-game; left-stick digital threshold crossings do not fire movement actions.
   **Fix location:** `actionmapInit()` — remove `imcActivate(&g_ImcMenu)`. Activate/deactivate
   `g_ImcMenu` (and `g_ImcPauseMenu`) in sync with the input context stack push/pop callbacks
   for `g_CtxImGuiMenu` and `g_CtxPauseMenu`.

2. **[HIGH] `actionmapPollFrame` ignores per-player axis map; uses wrong per-player sensitivity.**
   The action map reads a hardcoded global `s_SwapSticks` and `s_StickSensitivity` instead of
   using `padsCfg[p].axisMap[][]` and `padsCfg[p].sens[]`. Options menu sensitivity changes
   do not affect actual gameplay.
   **Fix location:** `actionmapPollFrame()` in `port/src/actionmap.cpp`. Either expose
   `inputGetPad(p)` and `inputGetPadCfg(p)` functions from `input.c`, or move the axis
   sensitivity path so that per-player `padsCfg` drives the values.

3. **[HIGH] `inputReadController` does not populate analog stick values.**
   `npad->stick_x/y` and `npad->rstick_x/y` are always digital (0/±128) or zero. Game code
   paths that read via `joyGetStickX/Y/joyGetRStickX/Y` get no analog data.
   **Fix location:** `inputReadController()` in `port/src/input.c`. After setting the digital
   stick values, if both digital states are 0, read `actionValue(idx, ACTION_AXIS_MOVE_X/Y)`,
   scale by 0x7F, and clamp to s8 range. Do the same for rstick using `ACTION_AXIS_AIM_X/Y`.

4. **[HIGH] `actionmapSaveBinds` / `actionmapLoadBinds` only touch `g_ImcGameplay`.**
   Vehicle and other IMC bindings are never persisted. `actionmapLoadBinds` does not restore them.
   **Fix location:** `buildBindStr()` and `actionmapLoadBinds()` in `port/src/actionmap.cpp`.
   Iterate all six known IMCs, not just the active-context list.

5. **[MEDIUM] Debug overlay (`g_CtxDebugOverlay`) leaks gamepad button events to the game.**
   Controller button events bypass the debug overlay context filter, allowing game actions while
   the dev window is open.
   **Fix location:** `debugOverlayCanConsume()` in `port/src/inputctx.c`. Add controller/joystick
   event types to the consumed set.

6. **[MEDIUM] Escape bound to both `ACTION_PAUSE` and `ACTION_MENU_CANCEL` in `g_ImcPauseMenu`.**
   Double-binding on Escape causes the pause menu to receive both a "cancel" and a "pause toggle"
   on the same keypress, potentially double-triggering.
   **Fix location:** `setupPauseMenuDefaults()` in `port/src/actionmap.cpp`. Remove the
   `ACTION_PAUSE` Escape bind from `g_ImcPauseMenu`; keep only `ACTION_MENU_CANCEL`.

7. **[LOW] `pdguiDriveImGuiNav` injects nav events unconditionally.**
   Arrow keys inject into ImGui even when gameplay is the top context, potentially affecting any
   unfocused ImGui window (e.g., the live console).
   **Fix location:** `pdguiDriveImGuiNav()` in `port/fast3d/pdgui_backend.cpp`. Add an early
   return if `pdguiWantsInput()` returns 0.

---

## Open Questions

1. **Is there existing code that reads from `joyGetRStickX/Y`?**
   The current `bondmove.c` does not call `joyGetRStickX/Y` — it uses `actionValue()` directly.
   But other subsystems (camera.c, bondwalk.c, etc.) may still use it. Need a grep to confirm
   whether `npad->rstick_x/y` being zero causes any gameplay regressions outside bondmove.c.

2. **Should `g_ImcMenu` be one context or split by menu type?**
   Currently a single menu IMC covers all menu navigation. The fix (priority #1) requires
   activating/deactivating it in sync with context stack changes. If different menus need
   different bindings, multiple IMCs may be needed. Mike should confirm whether the current
   single-IMC design is intentional or if a per-menu split is desired.

3. **Sensitivity synchronization: which system wins?**
   Two sensitivity systems exist: `padsCfg[].sens[]` (old input.c) and `actionmap`'s
   `s_StickSensitivity`. For priority #2, either the Options menu must be updated to write the
   actionmap value, or `actionmapPollFrame` must be updated to read from `padsCfg`. The simpler
   approach is to read from `padsCfg` in `actionmapPollFrame` and deprecate `s_StickSensitivity`.
   Mike should confirm which value the Options menu currently exposes to the user.

4. **Is CONTROLMODE_PC the only mode used, or do N64 control modes need to work?**
   Several fixes above assume CONTROLMODE_PC is the primary path. If N64 emulation modes (1.1,
   1.2, etc.) must function, the `joyCountButtonsOnSpecificSamples` path in bondmove.c becomes
   critical and all fixes to the joy sample buffer (priority #3) must be verified for those modes
   too.

5. **`CONT_4000` / `BUTTON_JUMP` collision in `s_ContToAction`.**
   In `inputReadController`, `CONT_4000` maps to `ACTION_CROUCH`. In `bondmove.c`, `BUTTON_JUMP
   = CONT_4000`. These use the same bit. Clarify whether jump and crouch share a bit by design
   (requiring separate handling) or whether they need different `CONT_*` bit assignments.
