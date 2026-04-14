# Input System Repair Plan

**Date**: 2026-04-09  
**Status**: REVIEW — do not implement without Mike's approval  
**Scope**: Compare original (working) input system with current (broken) action map migration

---

## Executive Summary

The M0.2 action map migration introduced an abstraction layer (`actionmap.cpp`) between SDL
events and game logic. The migration is *architecturally correct* but has **6 critical bugs**
and **3 moderate issues** caused by incomplete bridging between the old `OSContPad`-based
data flow and the new `ActionState`-based system.

The most impactful bugs are:
1. **Double mouse input** — mouse aim applied via BOTH freelook AND action axis
2. **Keyboard movement at 62.5% speed** — range 80 vs original 128
3. **Controller analog sticks not written to OSContPad** — breaks any un-migrated code
4. **Right stick routing changed** — original: right stick = strafe/walk; current: right stick = turn/pitch

---

## Architecture Comparison

### Original Data Flow
```
SDL events → inputEventFilter() [event watch — lastKey, device hotplug]
Per-frame:  inputUpdate() → SDL_GameControllerUpdate() + mouse state
Per-tick:   joyReadData() → osContGetReadData() → inputReadController():
              - CK_* binds → inputBindPressed() → inputKeyPressed() → SDL state
              - SDL_GameControllerGetAxis() → deadzone → npad->stick_x/y, rstick_x/y
              - Writes npad->button bitmask
            → stored in g_JoyData sample ring buffer
Game code:  joyGetStickX/Y(contpad) → samples[].pads[].stick_x
            joyGetButtons(contpad, mask) → samples[].pads[].button
            inputMouseGetScaledDelta() → freelookdx/dy (separate path)
```

### Current Data Flow
```
SDL events → pdguiProcessEvent() → actionmapDispatch() → s_State[player][action]
           → inputEventFilter() [event watch — lastKey, device hotplug]
Per-frame:  actionmapPollFrame() → controller sticks, mouse delta, WASD synthesis
            inputUpdate() → SDL_GameControllerUpdate() + mouse state
Per-tick:   joyReadData() → osContGetReadData() → inputReadController():
              - actionHeld() → npad->button bitmask ✓
              - WASD digital only → npad->stick_x/y (±128, no controller analog) ✗
              - rstick_x/y NEVER written ✗
            → stored in g_JoyData sample ring buffer
Game code:  bondmove reads actionValue() for sticks ✓
            bondmove reads actionHeld/Pressed() for buttons ✓
            bondmove STILL reads inputMouseGetScaledDelta() → freelookdx/dy ✓
            bondmove ALSO reads actionValue(ACTION_AXIS_AIM_X) * 80 → c2stickx ✗ DOUBLE
            menu.c reads actionPressed() for buttons ✓
            joyGetButtons() STUBBED → returns 0 always ✗ (no active callers remain)
```

---

## CRITICAL ISSUES

### C-1: Double Mouse Input (bondmove.c)

**Severity**: CRITICAL — mouse aiming is applied twice, making aim way too fast

**Original behavior** (bondmove.c:757-761 original):
```c
c1stickx = allowc1x ? joyGetStickX(contpad1) : 0;        // left stick X
c2stickx = allowc1x ? (s8) joyGetRStickX(contpad1) : 0;  // right stick X
// Mouse goes ONLY through:
inputMouseGetScaledDelta(&movedata.freelookdx, &movedata.freelookdy);
```
Mouse input → `freelookdx/dy` → applied in `bwalkApplyMoveData()` at lines 2371, 2410, 2524-2525.
Right stick → `c2stickx/c2sticky` → `movedata.analogstrafe/analogwalk` (movement, not aim).
These are SEPARATE paths. Mouse never touches c2stickx.

**Current behavior** (bondmove.c:940-945 current):
```c
c1stickx = (s8)(actionValue((s32)contpad1, ACTION_AXIS_MOVE_X) * 80.0f);
c2stickx = (s8)(actionValue((s32)contpad1, ACTION_AXIS_AIM_X) * 80.0f);
// Mouse STILL goes through (bondmove.c:1033):
inputMouseGetScaledDelta(&movedata.freelookdx, &movedata.freelookdy);
```
Now mouse delta writes to BOTH:
1. `ACTION_AXIS_AIM_X/Y` (via actionmapPollFrame line 809) → `c2stickx` → `movedata.analogturn`
2. `freelookdx/dy` (via inputMouseGetScaledDelta line 1033) → applied in bwalkApplyMoveData

**Fix**: In `actionmapPollFrame()`, do NOT write mouse delta to `ACTION_AXIS_AIM_X/Y`.
The mouse aim path must remain exclusively through `inputMouseGetScaledDelta()` → `freelookdx/dy`,
exactly as the original worked. The `ACTION_AXIS_AIM_X/Y` values should only come from
controller right stick.

**File**: `port/src/actionmap.cpp` lines 798-813  
**Change**: Remove the mouse delta → AIM axis block entirely. Mouse aim is handled by
the existing `inputMouseGetScaledDelta()` path in bondmove.c:1033.

---

### C-2: Keyboard Movement Speed at 62.5% (bondmove.c)

**Severity**: CRITICAL — WASD movement feels sluggish

**Original** (bondmove.c:757, input.c:814-817):
```c
c1stickx = joyGetStickX(contpad1);  // reads npad->stick_x
// Where inputReadController set:
npad->stick_x = xdiff < 0 ? -0x80 : (xdiff > 0 ? 0x7F : 0);  // ±128/127
```

**Current** (bondmove.c:940):
```c
c1stickx = (s8)(actionValue((s32)contpad1, ACTION_AXIS_MOVE_X) * 80.0f);  // ±80
```

WASD → `actionValue = ±1.0` → `× 80` = ±80. Original gave ±128/127.

**Fix**: Change multiplier from `80.0f` to `127.0f` to match original range.

**File**: `src/game/bondmove.c` lines 940-941  
**Change**:
```c
c1stickx = allowc1x ? (s8)(actionValue((s32)contpad1, ACTION_AXIS_MOVE_X) * 127.0f) : 0;
c1sticky = allowc1y ? (s8)(actionValue((s32)contpad1, ACTION_AXIS_MOVE_Y) * 127.0f) : 0;
```

Also check menu.c:4844-4847 which uses the same `* 80.0f` pattern.

---

### C-3: Controller Analog Sticks Not in OSContPad (input.c)

**Severity**: HIGH — breaks any code path still reading joyGetStickX/Y or joyGetRStickX/Y

**Original** (input.c:835-871):
```c
s32 leftX = SDL_GameControllerGetAxis(pads[idx], cfg->axisMap[0][0]);
// ... deadzone, sensitivity ...
if (!npad->stick_x && leftX) {
    npad->stick_x = leftX / 0x100;
}
// ... same for leftY, rightX, rightY → rstick_x/rstick_y
```

**Current** (input.c:687-698):
```c
if (!pads[idx]) { return 0; }
/* Stick values left at 0 — actionmapPollFrame() is the single source of truth. */
return 0;
```

The controller sticks are polled in `actionmapPollFrame()` but never written to OSContPad.
`npad->stick_x/y` only gets WASD digital values. `npad->rstick_x/y` stays at 0.

**Impact**: Currently all known callers of `joyGetStickX/Y` in game code have been migrated
to actionValue. If this is true, the sticks in OSContPad are unused and this is a latent bug
rather than an active one. However, it makes the OSContPad data inconsistent.

**Fix**: Restore controller stick population in `inputReadController()` after the WASD section,
so OSContPad reflects complete input state:

```c
if (pads[idx]) {
    const struct controllercfg *cfg = &padsCfg[idx];
    s32 leftX = SDL_GameControllerGetAxis(pads[idx], cfg->axisMap[0][0]);
    s32 leftY = SDL_GameControllerGetAxis(pads[idx], cfg->axisMap[0][1]);
    s32 rightX = SDL_GameControllerGetAxis(pads[idx], cfg->axisMap[1][0]);
    s32 rightY = SDL_GameControllerGetAxis(pads[idx], cfg->axisMap[1][1]);

    leftX = inputAxisScale(leftX, cfg->deadzone[cfg->axisMap[0][0]], cfg->sens[cfg->axisMap[0][0]]);
    leftY = inputAxisScale(leftY, cfg->deadzone[cfg->axisMap[0][1]], cfg->sens[cfg->axisMap[0][1]]);
    rightX = inputAxisScale(rightX, cfg->deadzone[cfg->axisMap[1][0]], cfg->sens[cfg->axisMap[1][0]]);
    rightY = inputAxisScale(rightY, cfg->deadzone[cfg->axisMap[1][1]], cfg->sens[cfg->axisMap[1][1]]);

    if (!npad->stick_x && leftX) npad->stick_x = leftX / 0x100;
    s32 stickY = -leftY / 0x100;
    if (!npad->stick_y && stickY) npad->stick_y = (stickY == 128) ? 127 : stickY;

    if (rightX) npad->rstick_x = rightX / 0x100;
    s32 rStickY = -rightY / 0x100;
    if (rStickY) npad->rstick_y = (rStickY == 128) ? 127 : rStickY;
}
```

**File**: `port/src/input.c` lines 687-698

---

### C-4: Right Stick Routing Changed — Strafe vs Aim (bondmove.c)

**Severity**: HIGH — right stick does completely different thing

**Original** (bondmove.c:1258-1266):
```c
if (controlmode == CONTROLMODE_PC) {
    if (!g_Vars.currentplayer->insightaimmode) {
        movedata.analogstrafe = c2stickx;   // right stick → strafe
        movedata.analogwalk = c2sticky;     // right stick → walk
        movedata.unk14 = (c2stickx || c2sticky);
    } else {
        movedata.analogstrafe = 0.f;
        movedata.analogwalk = 0.f;
    }
}
```

**Current** (bondmove.c:1521-1528):
```c
if (controlmode == CONTROLMODE_PC) {
    movedata.analogturn = c2stickx;    // right stick → TURN (was strafe)
    movedata.analogpitch = c2sticky;   // right stick → PITCH (was walk)
    movedata.unk14 = (c2stickx || c2sticky);
}
```

The original's CONTROLMODE_PC used right stick for MOVEMENT (strafe/walk), with mouse
handling all aiming via freelook. The current changed it to TURNING/PITCHING, which is
modern twin-stick FPS behavior.

**Question for Mike**: Was this intentional? Modern twin-stick (left=move, right=aim) is
more conventional, but it changed the original behavior. If intentional, keep current.
If not, restore original:

```c
if (controlmode == CONTROLMODE_PC) {
    if (!g_Vars.currentplayer->insightaimmode) {
        movedata.analogstrafe = c2stickx;
        movedata.analogwalk = c2sticky;
        movedata.unk14 = (c2stickx || c2sticky);
    } else {
        movedata.analogstrafe = 0.f;
        movedata.analogwalk = 0.f;
    }
}
```

**File**: `src/game/bondmove.c` lines 1521-1528

**NOTE**: If we keep twin-stick, we need to fix C-1 (double mouse) but route controller
right stick to aiming. If we restore original, right stick → strafe/walk and mouse → aim
via freelook only, no action map aim axis needed at all.

---

### C-5: contpad1 Used as Player Index (bondmove.c)

**Severity**: MEDIUM-HIGH — works in single-player, may break in multiplayer

**Original** (bondmove.c:757):
```c
contpad1 = optionsGetContpadNum1(g_Vars.currentplayerstats->mpindex);
c1stickx = joyGetStickX(contpad1);  // contpad1 = hardware controller index
```
`joyGetStickX(contpadnum)` indexes into `samples[].pads[contpadnum]` — the hardware pad array.

**Current** (bondmove.c:940):
```c
contpad1 = optionsGetContpadNum1(g_Vars.currentplayerstats->mpindex);
c1stickx = (s8)(actionValue((s32)contpad1, ACTION_AXIS_MOVE_X) * 80.0f);
```
`actionValue(player, ...)` treats first arg as **player index** (0-3), not hardware pad index.
`optionsGetContpadNum1()` returns the hardware contpad number, not the player index.

For single-player, contpad1 is typically 0, matching player 0. For splitscreen multiplayer,
contpad1 could be 1, 2, or 3 while the player's action state might be at a different index.

**Fix**: Use `g_Vars.currentplayerstats->mpindex` (the player index) instead of `contpad1`
when calling actionValue/actionHeld/actionPressed:

```c
s32 actionPlayer = g_Vars.currentplayerstats->mpindex;
c1stickx = allowc1x ? (s8)(actionValue(actionPlayer, ACTION_AXIS_MOVE_X) * 127.0f) : 0;
```

**File**: `src/game/bondmove.c` lines 940-990 — all actionValue/actionHeld/actionPressed calls

---

### C-6: Aim Axis Value Clamping Loses Mouse Precision (actionmap.cpp)

**Severity**: MEDIUM — fast mouse movements get clipped

**Current** (actionmap.cpp:806-807):
```c
f32 ax = clampf((f32)mdx * MOUSE_AIM_SCALE, -1.0f, 1.0f);  // MOUSE_AIM_SCALE = 0.003
```

A mouse movement of >333 pixels/frame (very achievable at 60fps) gets clamped to 1.0.
The original had no such clamping — `inputMouseGetScaledDelta()` returned raw scaled values.

**Fix**: This becomes moot if C-1 is fixed (removing mouse from ACTION_AXIS_AIM entirely).

---

## MODERATE ISSUES

### M-1: actionmapEndFrame Timing

**Current flow**:
```
gfx_sdl_handle_events() → [poll SDL events] → actionmapEndFrame()
  ... later ...
pdsched → actionmapPollFrame() → joyReadData() → game logic reads actions
```

`actionmapEndFrame()` clears pressed/released edges at the END of `gfx_sdl_handle_events()`.
Then `actionmapPollFrame()` runs during pdsched. If multiple game ticks occur between render
frames, the pressed edge only exists for the first tick.

**Impact**: Sub-frame button press detection could miss edges on the second+ game tick within
a render frame. The original had sample-based edge detection that survived multiple reads.

**Fix**: Move `actionmapEndFrame()` to the end of pdsched's tick loop (after game logic),
not the end of the event handler. Or: keep the current position but acknowledge that
sub-frame precision is reduced.

**File**: `port/fast3d/gfx_sdl2.cpp` line 354

---

### M-2: Menu Navigation with Controller (menu.c)

**Current** (menu.c:4849):
```c
s32 player = g_MpPlayerNum;
if (actionPressed(player, ACTION_USE)) { inputs.select = 1; }
```

`ACTION_USE` is bound to `JOY_BTN(0, JBTN_A)` in `g_ImcGameplay`. But when a PD native
menu is open, is `g_ImcMenu` active? If so, `ACTION_MENU_ACCEPT` (bound to `JOY_BTN(0, JBTN_A)`)
would fire instead, and `ACTION_USE` wouldn't fire because the higher-priority `g_ImcMenu`
consumed the VK first.

The menu code checks `ACTION_USE` (gameplay action), not `ACTION_MENU_ACCEPT` (menu action).
If `g_ImcMenu` is active at higher priority, `ACTION_USE` never fires for the A button because
`g_ImcMenu` maps `JOY_BTN(0, JBTN_A)` → `ACTION_MENU_ACCEPT` first.

**Fix**: Menu code should check BOTH `ACTION_USE` and `ACTION_MENU_ACCEPT`, OR the menu
should only use `ACTION_MENU_ACCEPT`:

```c
if (actionPressed(player, ACTION_USE) || actionPressed(player, ACTION_MENU_ACCEPT)) {
    inputs.select = 1;
}
```

**File**: `src/game/menu.c` lines 4849-4860

---

### M-3: Stick Value Range in Menu Code (menu.c)

**Current** (menu.c:4844):
```c
s8 thisstickx = (s8)(actionValue(player, ACTION_AXIS_MOVE_X) * 80.0f);
```

Same 80 vs 127 issue as C-2 but for menu navigation stick sensitivity.

**File**: `src/game/menu.c` lines 4844-4847

---

## REPAIR PRIORITY ORDER

1. **C-1** (Double mouse) — Fix first, biggest visible impact
2. **C-2** (Movement speed) — Fix second, changes feel dramatically
3. **C-4** (Right stick routing) — Needs Mike's decision: twin-stick or original?
4. **C-5** (contpad vs player index) — Fix for multiplayer correctness
5. **M-2** (Menu A button) — Fix for controller menu navigation
6. **C-3** (OSContPad sticks) — Restore for correctness/safety
7. **C-6** (Mouse clamp) — Moot after C-1 fix
8. **M-1** (EndFrame timing) — Lower priority, edge case
9. **M-3** (Menu stick range) — Minor

---

## DECISION REQUIRED: Twin-Stick vs Original

The biggest architectural question is whether CONTROLMODE_PC should use:

**Option A: Original behavior (right stick = strafe/walk, mouse = aim)**
- Matches the original port exactly
- Mouse aim through `freelookdx/dy` only
- `ACTION_AXIS_AIM_X/Y` only from controller right stick, goes to `analogstrafe/walk`
- `actionmapPollFrame()` does NOT write mouse to aim axes

**Option B: Modern twin-stick (left stick = move, right stick = aim, mouse = aim)**
- More conventional modern FPS
- Requires fixing C-1 (remove mouse from action aim axes)
- Right stick and mouse both control aiming but through different paths
- `movedata.analogturn/analogpitch` from controller right stick
- `movedata.freelookdx/dy` from mouse

If Option B, the mouse must NOT go through `ACTION_AXIS_AIM_X/Y` — it must ONLY use
the freelook path. The right stick would go through `ACTION_AXIS_AIM_X/Y` → `c2stickx` →
`movedata.analogturn/pitch`. These are separate rotation axes that won't double-apply.

**Recommendation**: Option B (modern twin-stick) because:
- It's what every modern FPS player expects
- The original "right stick = strafe" only made sense because N64 had one stick
- But we MUST fix the double-apply bug (C-1)

---

## FILES TO MODIFY

| File | Issues | Changes |
|------|--------|---------|
| `port/src/actionmap.cpp` | C-1, C-6 | Remove mouse → AIM axis; or restructure |
| `src/game/bondmove.c` | C-2, C-4, C-5 | Fix multiplier, fix player index, decide routing |
| `port/src/input.c` | C-3 | Restore controller sticks in inputReadController |
| `src/game/menu.c` | M-2, M-3 | Fix ACTION_USE vs MENU_ACCEPT, fix range |
| `port/fast3d/gfx_sdl2.cpp` | M-1 | Consider moving actionmapEndFrame |
