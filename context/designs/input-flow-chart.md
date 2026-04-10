# Input System Flow Chart — Grounded in Code

Generated 2026-04-09. Traces every input type from SDL event to game action.

---

## Frame Timing (CRITICAL)

```
schedStartFrame (pdsched.c:243)
  └─ videoStartFrame (video.c:117)
       └─ gfx_start_frame (gfx_pc.cpp:2620)
            └─ gfx_sdl_handle_events (gfx_sdl2.cpp:307)
                 ├─ SDL_PollEvent loop:
                 │    └─ pdguiProcessEvent (pdgui_backend.cpp:671)
                 │         ├─ Global hotkeys (F8, F12) → return 1
                 │         ├─ inputCtxShouldSuppressKey → grace period check
                 │         ├─ actionmapDispatch(ev) ← UPDATES s_State
                 │         ├─ ImGui_ImplSDL2_ProcessEvent(ev)
                 │         └─ inputCtxDispatch(ev) → context stack consume check
                 │
                 ├─ inputCtxEndFrame (inputctx.c:177) ← deferred pops, syncMouseMode
                 └─ actionmapEndFrame (actionmap.cpp:883) ← CLEARS pressed/released
                      *** BUG: This clears BEFORE game logic reads! ***

[Game logic runs here — bondmove.c, player.c, etc.]
  └─ Reads actionPressed() → ALWAYS 0 (already cleared!)
  └─ Reads actionHeld() → works (held not cleared by endFrame)
  └─ Reads actionValue() → works (value not cleared by endFrame)

schedEndFrame (pdsched.c:282)
  ├─ inputUpdate (input.c:778) ← updates mouseDX/DY
  ├─ actionPressed(0, ACTION_DEBUG_TOGGLE) ← ALWAYS 0 (bug)
  ├─ actionmapPollFrame (actionmap.cpp:746) ← samples analog sticks
  ├─ joyStartReadData / joyReadData ← inputReadController populates OSContPad
  └─ ...
```

**ROOT BUG**: `actionmapEndFrame()` at gfx_sdl2.cpp:354 runs at the END of
`gfx_sdl_handle_events()`, which is called from `schedStartFrame()`. Game logic
runs AFTER `schedStartFrame` returns. So `pressed`/`released` edge signals are
cleared before anyone reads them. `actionPressed()` always returns 0.

**SECOND BUG**: `actionmapInit()` is called TWICE — first in main.c:161 (correct),
then again inside `pdguiInit()` at pdgui_backend.cpp:289. The second call happens
AFTER `configInit()` + `actionmapLoadBinds()`, wiping all pd.ini bind customizations.

---

## Path 1: Keyboard Key Press → Game Action

```
SDL_KEYDOWN event (scancode = SDL scancode, e.g. 26 for W)
  │
  ├─ inputEventFilter (input.c:328, event watch)
  │    └─ Sets lastKey for rebind capture. Does NOT consume event.
  │
  └─ SDL_PollEvent → pdguiProcessEvent (pdgui_backend.cpp:671)
       │
       ├─ inputCtxShouldSuppressKey: suppresses KEY_DOWN for 100ms after context push
       │
       ├─ actionmapDispatch (actionmap.cpp:609)
       │    └─ case SDL_KEYDOWN (line 618):
       │         ├─ Skip if ev->key.repeat
       │         ├─ updateDevice(ACTIONMAP_DEVICE_KBM)
       │         └─ fireVk(ev->key.keysym.scancode, 1)
       │              │
       │              └─ Walk s_Active[] IMCs (highest priority first):
       │                   For each action in IMC with has_mapping[a]:
       │                     For each trigger in mapping:
       │                       If trigger.vk == scancode → MATCH:
       │                         s_State[0][action].held = 1
       │                         s_State[0][action].pressed = 1
       │                         s_State[0][action].value = 1.0f
       │                         goto done (first match wins)
       │
       ├─ ImGui_ImplSDL2_ProcessEvent (line 721)
       │
       └─ inputCtxDispatch (line 727)
            └─ If g_CtxGameplay is top: can_consume=1, on_event returns 0
               → pdguiProcessEvent returns 0 → event falls through to
               gfx_sdl_handle_events switch (Alt+Enter, F9, backtick)

Game reads: actionHeld(0, ACTION_MOVE_FORWARD) ← returns s_State[0][0].held
            actionValue(0, ACTION_AXIS_MOVE_X)  ← synthesized in actionmapPollFrame

WASD → AXIS synthesis (actionmapPollFrame, actionmap.cpp:854-870):
  If ACTION_MOVE_RIGHT.held → mx += 1.0
  If ACTION_MOVE_LEFT.held  → mx -= 1.0
  If ACTION_MOVE_FORWARD.held → my += 1.0
  If ACTION_MOVE_BACKWARD.held → my -= 1.0
  Normalize diagonal, write to s_State[0][ACTION_AXIS_MOVE_X/Y].value

bondmove.c reads: actionValue(0, ACTION_AXIS_MOVE_X) * 127.0f → c1stickx
```

**STATUS**: Keyboard MOVEMENT works (uses actionHeld which persists).
Keyboard EDGE actions (Esc→pause, weapon switch) are BROKEN (use actionPressed which is always 0).

---

## Path 2: Mouse Movement → Freelook/Aim

```
SDL_MOUSEMOTION event
  │
  └─ pdguiProcessEvent → actionmapDispatch:
       case SDL_MOUSEMOTION (line 656):
         updateDevice(ACTIONMAP_DEVICE_KBM)  ← only device tracking, no fireVk

Mouse deltas are NOT routed through ACTION_AXIS_AIM (see comment at line 843).
Instead, mouse aim uses a completely separate path:

inputUpdate (input.c:778, called from schedEndFrame:304)
  └─ inputUpdateMouse (input.c:727):
       SDL_GetRelativeMouseState(&mdx, &mdy) → mouseDX, mouseDY
       (only if mouseLocked is true)

bondmove.c (line 1039):
  inputMouseGetScaledDelta(&movedata.freelookdx, &movedata.freelookdy)
    └─ input.c:1148: inputMouseGetScaledDelta
         If mouseLocked:
           mdx = mouseSensX * (mouseDX / 3.5f) * 0.022f
           mdy = mouseSensY * (mouseDY / 3.5f) * 0.022f
         Else: 0, 0

movedata.freelookdx/dy → applied to camera rotation in bmoveApplyMoveData
```

**STATUS**: Works when mouseLocked=true. Breaks when mouse capture doesn't
restore after leaving menus (see Mouse Capture section below).

---

## Path 3: Mouse Button → Game Action

```
SDL_MOUSEBUTTONDOWN event (button = 1..5)
  │
  └─ pdguiProcessEvent → actionmapDispatch:
       case SDL_MOUSEBUTTONDOWN (line 631):
         fireVk(VK_MOUSE_BEGIN + (button - 1), 1)
           VK_MOUSE_BEGIN = 512
           VK_MOUSE_LEFT  = 512 (button 1)
           VK_MOUSE_RIGHT = 514 (button 3)
         → fireVk finds binding in g_ImcGameplay:
           ACTION_FIRE_PRIMARY bound to VK_MOUSE_LEFT (512)
           ACTION_FIRE_SECONDARY bound to VK_MOUSE_RIGHT (514)
         → Sets s_State[0][ACTION_FIRE_PRIMARY].held/pressed/value

bondmove.c reads: actionHeld(0, ACTION_FIRE_PRIMARY) → c1buttons |= Z_TRIG
```

**STATUS**: Mouse buttons HELD work (actionHeld). Mouse button EDGES broken
(actionPressed cleared too early).

---

## Path 4: Controller Button → Game Action

```
SDL_CONTROLLERBUTTONDOWN event (button = SDL_CONTROLLER_BUTTON_*)
  │
  ├─ inputEventFilter (input.c:377, event watch):
  │    Sets lastKey = VK_JOY1_BEGIN + button + idx*32 (for rebind capture)
  │
  └─ pdguiProcessEvent → actionmapDispatch:
       case SDL_CONTROLLERBUTTONDOWN (line 661):
         ctrl = SDL_GameControllerFromInstanceID(ev->cbutton.which)
         player = SDL_GameControllerGetPlayerIndex(ctrl)  ← set to cidx in inputInitController
         if player < 0 || player >= 4 → player = 0
         vk = JOY_BTN(player, ev->cbutton.button)
            = VK_JOY1_BEGIN + player*32 + button
            = 519 + 0*32 + button (for player 0)
         fireVk(vk, 1)
           → Walk s_Active[], find g_ImcGameplay binding:
             e.g. JOY_BTN(0, JBTN_A=0) = 519 → ACTION_USE
                  JOY_BTN(0, JBTN_B=1) = 520 → ACTION_CANCEL_USE
                  JOY_BTN(0, JBTN_X=2) = 521 → ACTION_RELOAD
                  JOY_BTN(0, JBTN_Y=3) = 522 → ACTION_JUMP
                  JOY_BTN(0, JBTN_START=6) = 525 → ACTION_PAUSE
                  JOY_BTN(0, JBTN_LB=9)   = 528 → ACTION_WEAPON_PREV
                  JOY_BTN(0, JBTN_RB=10)  = 529 → ACTION_WEAPON_NEXT
           → Sets s_State[0][action].held/pressed/value

bondmove.c reads: actionHeld(0, ACTION_USE) → c1buttons |= A_BUTTON  (held ✓)
                  actionPressed(0, ACTION_USE) → c1buttonsthisframe    (BROKEN)
```

**STATUS**: Controller buttons HELD work (actionHeld persists through endFrame
clearing). Controller button EDGES broken (actionPressed always 0). This means:
- Can't pause (needs c1buttonsthisframe & START_BUTTON)
- Can't interact with doors/items (needs edge detection)
- Can't switch weapons (needs edge detection)
- Fire DOES work (uses c1buttons / actionHeld for Z_TRIG)

---

## Path 5: Controller Stick → Movement/Aim

### Analog Values (actionmapPollFrame)

```
actionmapPollFrame (actionmap.cpp:746, called from schedEndFrame:313)
  │
  ├─ Check menuActive: topCtx = inputCtxGetTop()
  │    If topCtx != g_CtxGameplay → menuActive=1 → zero all axes, return
  │
  └─ For player p=0..3:
       ctrl = SDL_GameControllerFromPlayerIndex(p)
       If ctrl && !menuActive:
         Read raw axes (honour swap sticks):
           lx = SDL_GameControllerGetAxis(ctrl, LEFTX or RIGHTX if swapped)
           ly = SDL_GameControllerGetAxis(ctrl, LEFTY or RIGHTY if swapped)
           rx = SDL_GameControllerGetAxis(ctrl, RIGHTX or LEFTX if swapped)
           ry = SDL_GameControllerGetAxis(ctrl, RIGHTY or LEFTY if swapped)

         Apply deadzone + sensitivity:
           flx = applyDeadzone(lx / 32767.0f, s_StickDeadzone) * s_StickSensitivity
           (same for fly, frx, fry)

         Negate Y (SDL Y+ = down, game Y+ = forward):
           fly = -fly; fry = -fry

         Optional Y-invert for aim:
           if s_StickInvertY: fry = -fry

         Write to s_State:
           s_State[p][ACTION_AXIS_MOVE_X].value = clampf(flx, -1, 1)
           s_State[p][ACTION_AXIS_MOVE_Y].value = clampf(fly, -1, 1)
           s_State[p][ACTION_AXIS_AIM_X].value  = clampf(frx, -1, 1)
           s_State[p][ACTION_AXIS_AIM_Y].value  = clampf(fry, -1, 1)

bondmove.c (line 946-951):
  c1stickx = actionValue(0, ACTION_AXIS_MOVE_X) * 127.0f  ← left stick X
  c1sticky = actionValue(0, ACTION_AXIS_MOVE_Y) * 127.0f  ← left stick Y
  c2stickx = actionValue(0, ACTION_AXIS_AIM_X) * 127.0f   ← right stick X
  c2sticky = actionValue(0, ACTION_AXIS_AIM_Y) * 127.0f   ← right stick Y
```

**STATUS**: Controller sticks SHOULD work — actionValue reads .value which is
NOT cleared by actionmapEndFrame. However, actionmapPollFrame runs in
schedEndFrame (AFTER bondmove). So bondmove reads LAST FRAME'S stick values.
This is a 1-frame lag, not a total failure.

### Digital Threshold Crossing (actionmapDispatch)

```
SDL_CONTROLLERAXISMOTION event
  │
  └─ actionmapDispatch → case SDL_CONTROLLERAXISMOTION (line 677):
       handleAxisDigital(player, val, slot_neg, slot_pos, vk_neg, vk_pos)
         If val crosses STICK_PRESS_THRESHOLD (12000):
           fireVk(JOY_BTN(player, JOFS_LSTICK_LEFT/RIGHT/UP/DOWN), 1)
         If val falls below STICK_RELEASE_THRESHOLD (8000):
           fireVk(JOY_BTN(player, JOFS_LSTICK_LEFT/RIGHT/UP/DOWN), 0)

These synthetic VKs are bound to:
  JOFS_LSTICK_UP   → ACTION_MOVE_FORWARD (gameplay), ACTION_MENU_UP (menu)
  JOFS_LSTICK_DOWN → ACTION_MOVE_BACKWARD (gameplay), ACTION_MENU_DOWN (menu)
  JOFS_RSTICK_*    → ACTION_AIM_UP/DOWN/LEFT/RIGHT
```

**STATUS**: Works for held state (actionHeld). Edge detection broken (same bug).

### Legacy Path (inputReadController → OSContPad)

```
joyReadData (called from schedEndFrame:315)
  └─ inputReadController (input.c:637):
       Build npad->button from actionHeld queries (s_ContToAction table)
       Build npad->stick_x/y from actionHeld (WASD digital)
       If pads[idx] exists:
         Read SDL_GameControllerGetAxis directly → npad->stick_x/y, rstick_x/y
         (C-3 fix: populates OSContPad for joyGetStick* legacy path)

joy.c: joyGetStickX(contpadnum) → g_JoyDataPtr samples[last].pads[n].stick_x
       (populated by inputReadController via joyReadData)
```

**STATUS**: This legacy path works for analog sticks because it reads SDL directly.
Button bitmask uses actionHeld (works for held, not edges).

---

## Mouse Capture Flow

```
Context push: inputCtxPush(&g_CtxPauseMenu)
  └─ pauseMenuOnPush (inputctx.c:422):
       SDL_SetRelativeMouseMode(FALSE)  ← absolute mode
       SDL_ShowCursor(ENABLE)           ← cursor visible
       imcActivate(&g_ImcMenu)
       imcActivate(&g_ImcPauseMenu)

Context pop: inputCtxPopDeferred(&g_CtxPauseMenu)
  └─ Marked for removal. Actual pop in inputCtxEndFrame (next frame).
     inputCtxEndFrame (inputctx.c:177):
       └─ pauseMenuOnPop: deactivates IMCs
       └─ inputCtxSyncMouseMode (inputctx.c:279):
            top = inputCtxGetTop()
            If top == g_CtxGameplay:
              SDL_SetRelativeMouseMode(TRUE)   ← relative mode
              SDL_ShowCursor(DISABLE)          ← cursor hidden
```

**STATUS**: The sync mechanism looks correct, but depends on the context stack
being properly balanced. If a push happens without a matching pop (or a pop
fails), mouse capture stays in absolute mode during gameplay.

Key variables:
- `mouseLocked` (input.c:88) — tracks desired lock state
- `inputLockMouse(1)` called in gameplayOnPush — sets mouseLocked AND SDL mode
- `inputMouseGetScaledDelta` only returns values when mouseLocked==true

Note: pauseMenuOnPush calls `SDL_SetRelativeMouseMode(FALSE)` directly without
changing `mouseLocked`. When inputCtxSyncMouseMode restores relative mode, it
also doesn't touch `mouseLocked`. So `mouseLocked` should stay at 1 (set at
game start in gameplayOnPush). BUT if something else sets mouseLocked=0...

---

## VK Ranges (input.h)

```
VK_KEYBOARD_BEGIN = 0          (SDL scancodes: A=4, W=26, etc.)
VK_MOUSE_BEGIN    = 512        (LEFT=512, MIDDLE=513, RIGHT=514, ...)
VK_MOUSE_WHEEL_UP = 517
VK_MOUSE_WHEEL_DN = 518
VK_JOY_BEGIN      = 519
VK_JOY1_BEGIN     = 519        (A=519, B=520, ..., START=525, ..., LSTICK_LEFT=541, ...)
VK_JOY2_BEGIN     = 551        (519 + 32)
VK_JOY3_BEGIN     = 583
VK_JOY4_BEGIN     = 615
VK_TOTAL_COUNT    = 647        (519 + 4*32)
```

---

## Active IMC Stack During Gameplay

```
Normal gameplay:
  s_Active[0] = &g_ImcGameplay (priority 0) ← only active IMC

Pause menu open:
  s_Active[0] = &g_ImcPauseMenu (priority 11)
  s_Active[1] = &g_ImcMenu      (priority 10)
  s_Active[2] = &g_ImcGameplay  (priority 0)
  (Menu IMC wins for shared VKs like D-pad, A/B buttons)

Debug overlay open:
  s_Active[0] = &g_ImcDebugOverlay (priority 20)
  s_Active[1] = &g_ImcMenu         (priority 10)
  s_Active[2] = &g_ImcGameplay     (priority 0)
```

---

## Summary of Bugs

| # | Bug | Impact | Fix |
|---|-----|--------|-----|
| 1 | `actionmapEndFrame` in `gfx_sdl_handle_events` (gfx_sdl2.cpp:354) clears pressed/released BEFORE game logic reads them | `actionPressed()` always returns 0 → no edge-triggered actions (pause, interact, weapon switch, reload) | Move to end of `schedEndFrame` in pdsched.c |
| 2 | `actionmapInit()` called twice — main.c:161 then pdguiInit pdgui_backend.cpp:289 | Wipes pd.ini bind customizations loaded by actionmapLoadBinds at main.c:164 | Remove the call in pdguiInit |
