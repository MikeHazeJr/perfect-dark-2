# Input System

> Unified action map. Pushdown input context stack. Typed layer stack. Single suppression predicate. The action map is the only legitimate way to read input outside the SDL event entry point.

---

## What it is

The input system has three principal subsystems sitting on top of SDL2:

1. **Action map** ([port/include/actionmap.h](../../port/include/actionmap.h), [port/src/actionmap.cpp](../../port/src/actionmap.cpp)). 121 named `InputAction` values, 12 `InputMappingContext` (IMC) singletons stacked by priority, deterministic dispatch. Every game-side input read goes through `actionPressed/Held/Value`.
2. **Input context stack** ([port/include/inputctx.h](../../port/include/inputctx.h), [port/src/inputctx.c](../../port/src/inputctx.c), 974 lines). 16-slot pushdown stack of input contexts (`g_CtxGameplay`, `g_CtxImGuiMenu`, `g_CtxPauseMenu`, `g_CtxDebugOverlay`). Owns SDL mouse mode and the `gameplayInputSuppressed()` single-truth-source predicate.
3. **Layer stack** ([port/include/inputlayer.h](../../port/include/inputlayer.h), [port/src/inputlayer.c](../../port/src/inputlayer.c), 421 lines). 7 typed layers with handle-based push/pop, generation counters, scene-event integration.

Single SDL event entry point: [port/fast3d/pdgui_backend.cpp:1272](../../port/fast3d/pdgui_backend.cpp:1272) `pdguiProcessEvent`. Every event flows ImGui -> WantCaptureKeyboard gate -> `actionmapDispatch` -> `inputCtxDispatch`.

---

## Action map

### Action enum

121 actions defined at [port/include/actionmap.h](../../port/include/actionmap.h). Categories:

- **Movement**: forward/back/strafe/jump/crouch/sprint
- **Analog aim**: 4 axes (move_x/y, aim_x/y) + look invert/sensitivity
- **Combat**: fire/secondary/reload/zoom/melee/grenade
- **Weapons**: weapon prev/next, weapon 1-9, weapon-radial open
- **Vehicle**: throttle/brake/turn/handbrake
- **N64 C-buttons**: 4 actions (kept for keyboard / mod bindings, hidden in controller tab)
- **D-pad**: 4 actions
- **Menu nav**: accept/cancel/up/down/left/right/tab_prev/tab_next/secondary/tertiary/delete
- **System**: pause/console/screenshot/voice_ptt
- **Forge**: toggle/ascend/descend/boost/precision + bot commands + placement
- **Observer**: subset/member nav, camera toggle, freefly, ascend/descend, stop
- **Skin editor**: brush/tool/grid/UV/undo/redo/save
- **Cutscene**: skip

`ACTION_MENU_ACCEPT == ACTION_USE` aliasing at line 252.

### IMC priority stack

12 IMCs declared at [port/include/actionmap.h:553-561](../../port/include/actionmap.h:553):

| IMC | Priority | Activated by |
|-----|----------|--------------|
| `g_ImcGameplay` | 0 | LAYER_GAMEPLAY (always-on baseline) |
| `g_ImcMission` | 1 | Solo / co-op / anti scenario |
| `g_ImcCombatSim` | 1 | Combat Sim / MP + scorecard-hold |
| `g_ImcCutscene` | 4 | LAYER_CUTSCENE (Cohort 4, K.2) |
| `g_ImcVehicle` | 5 | LAYER_VEHICLE_DRIVER |
| `g_ImcForgeSession` | 6 | Forge session active |
| `g_ImcForge` | 7 | Forge editor freefly |
| `g_ImcObserver` | 8 | LAYER_OBSERVER (spectator) |
| `g_ImcMenu` | 10 | LAYER_MENU |
| `g_ImcPauseMenu` | 11 | Pause |
| `g_ImcDebugOverlay` | 20 | Debug overlay (F-keys) |
| `g_ImcTextInput` | 30 | Text capture |

Higher priority wins for the same VK. Deterministic dispatch at [port/src/actionmap.cpp:566-594](../../port/src/actionmap.cpp:566): walk `s_Active[]` highest priority first, lower trigger-slot index first, then lower action id.

---

## Input context stack

`inputctx_t` at [port/include/inputctx.h](../../port/include/inputctx.h:1) declares the four contexts. The stack is 16-slot pushdown; depth >= 5 logs DEEP STACK warning, depth >= 15 triggers an emergency reset and re-seeds gameplay (watchdog at [port/src/inputctx.c:448-472](../../port/src/inputctx.c:448)).

**Mouse capture** is owned by `inputCtxSyncMouseMode()` at frame end. Each context declares its SDL mouse mode (gameplay = relative/captured, menus = absolute/visible). No menu may call `SDL_SetRelativeMouseMode` or `SDL_ShowCursor` directly; this is enforced by the constraint ledger and by code review.

**Suppression predicate** [port/src/inputctx.c:716-741](../../port/src/inputctx.c:716) `gameplayInputSuppressed()` returns true if:
- Top of context stack is non-gameplay, OR
- Window has lost focus, OR
- Within 50ms focus-settle window

This is the single truth-source. Every action-map query gates on `actionLayerAllows()` at [port/src/actionmap.cpp:1392-1411](../../port/src/actionmap.cpp:1392) which checks the predicate plus declared layer action-set membership.

**Deferred-pop correctness**: `PopDeferred` marks-without-removing; `inputCtxGetTop` skips marked entries; `EndFrame` compacts at frame boundary. Verified at [tests/test_input_authority.cpp:145-163](../../tests/test_input_authority.cpp:145).

---

## Layer stack

7 layer types at [port/include/inputlayer.h:43-49](../../port/include/inputlayer.h:43):

```
LAYER_BOOT              pre-stage-load, splash, copyright
LAYER_GAMEPLAY          normal play (bondmove + bondgun)
LAYER_CUTSCENE          cutscene tick (per-player in Cohort 4)
LAYER_MENU              any menu / overlay / dialog
LAYER_VEHICLE_DRIVER    mounted vehicle, driver seat
LAYER_VEHICLE_TURRET    future, gunner seat
LAYER_OBSERVER          Dr Carroll free-fly + Forge freefly + spectator
```

Each layer carries `imc` pointer, push/pop/abort callbacks, declared `action_set[]`, and identity. Handle-based push/pop with generation counters at [port/src/inputlayer.c:21-26](../../port/src/inputlayer.c:21); pop zeros generation; stale-handle pop returns -1 rather than corrupting (verified [tests/test_input_layer_stack.cpp:140-152](../../tests/test_input_layer_stack.cpp:140)).

Scene-event integration at [port/src/scene.c:128](../../port/src/scene.c:128) routes 14 `SceneEvent`s into layer transitions (`SCENE_EVENT_CUTSCENE_START/END`, `SCENE_EVENT_VEHICLE_BOARD/DISMOUNT`, `SCENE_EVENT_STAGE_READY/TEARDOWN`, etc.).

---

## Flush discipline

`actionmapFlushGameplayState()` synthesizes release edges for held gameplay actions while preserving shared actions (USE, PAUSE). Called on:
- Non-gameplay context push ([port/src/inputctx.c:263](../../port/src/inputctx.c:263))
- Focus loss / gain ([port/src/inputctx.c:614, 625](../../port/src/inputctx.c:614))
- Cutscene push ([port/src/inputlayer.c:117](../../port/src/inputlayer.c:117))

`actionmapFlushActionSet(actions, count)` flushes a declared action set without broadening the gameplay flush. Cutscene layer's action set includes ACTION_USE / ACTION_MENU_ACCEPT, ACTION_CANCEL_USE, ACTION_PAUSE, ACTION_SKIP_CUTSCENE, and legacy skip actions so held Continue/Use cannot survive the menu-accept to stage-change to cutscene-entry path (S489, B-266 fix per [constraints.md](../constraints.md)).

---

## Hold / tap as first-class

Hold and tap timing are part of the action API surface: `actionHeldForMs`, `actionWasTap`, `actionConsumeHold`, `actionHoldProgress` declared at [port/include/actionmap.h:424-440](../../port/include/actionmap.h:424). Implementation at [port/src/actionmap.cpp:1539-1712](../../port/src/actionmap.cpp:1539). Gated through `actionLayerAllows`.

### Tap / hold dispatcher pattern (Mike directive 2026-05-18)

Single buttons can carry both a tap action and a hold action. The canonical pattern lives in [src/game/bondmove.c](../../src/game/bondmove.c) for player-side gameplay inputs:

- **Tap**: `actionWasTap(player, action, BOND_TAP_HOLD_THRESH_MS)` fires once on the release frame if the button was released before `BOND_TAP_HOLD_THRESH_MS` (= 250) and the gesture wasn't consumed by a hold handler.
- **Hold**: `actionHeldForMs(player, action, threshold) && !actionHoldConsumed(player, action)` fires once when the threshold is crossed; pair with `actionConsumeHold(player, action)` so the tap path stays silent on the eventual release.
- **Double-tap**: stash the frame index of the last tap-release per player; on the next tap-release, check whether the gap is within `BOND_DOUBLE_TAP_TICKS` (= 15, ~250 ms at 60 Hz). Reset the stamp so the tap pair does not cascade into another reload. It does not interact.

Live bindings using this pattern:

- `ACTION_USE`: tap = X_BUTTON (reload); hold = A_BUTTON (interact) + consume. Tap and double-tap must not synthesize A_BUTTON or call `bmoveHandleActivate()`.
- `ACTION_WEAPON_NEXT`: tap = Y_BUTTON / Q / mouse-wheel-down / RB cycle; hold = Y_BUTTON or Q opens BUTTON_RADIAL. Mouse wheel remains momentary and cannot hold.
- `ACTION_WEAPON_PREV`: mouse-wheel-up / LB cycle backward through the same PC gameplay consumer.
- `ACTION_CROUCH`: drives the existing `CROUCHPOS_{STAND, DUCK, SQUAT}` state machine directly (STAND tap → DUCK, DUCK tap → STAND, any hold → SQUAT, SQUAT tap → DUCK, ACTION_JUMP press → STAND).

Natural-stop cases stay consumer-owned. The input primitive does not need a separate ammo-empty, door-open, full-charge, beam, overheat, or cooldown primitive; those consumers use the same threshold/consume APIs and decide when their own interaction or weapon behavior has naturally stopped. Historical `c3814-s16` / `s17` / `s19` references are migration provenance only; current work must be represented in the repo-local Workbench.

The hold-ring visual (`pdguiDrawHoldProgressRingAroundBox`) decays the smoothed value toward 0 on every `pdguiInteractPromptRender` early-return (menu open, CI intro, prop label NULL) so a re-entry doesn't flash from a stale value.

Crouch-jump: `ACTION_JUMP` latches `g_BondCrouchJumpActive[pi]`; a fresh `ACTION_CROUCH` press while `bdeltapos.y > 0` (mid-jump) adds +1.5 to vertical velocity, consumes the latch (single-shot per jump). Players clear surfaces slightly above their regular jump apex. Bots get the same boost but only on `BOTDIFF_HARD+` AND when the target Y delta is in the just-barely 30-60 unit zone.

---

## Glyph system

`port/include/pdgui_glyphs.h` + `port/fast3d/pdgui_glyphs.cpp` resolve any `InputAction` to its current primary VK through the active IMC stack, produce short labels ("E", "Space", "LMB", "A", "LB", "D-Up"), and render `[KEY] Label` pills on the foreground drawlist. Auto-detects KBM vs gamepad via `actionmapGetLastDevice()` with 500ms debounce. For non-standard controller classes, glyphs use generic `BtnN` / `AxisN+/-` labels instead of pretending a HOTAS, homemade HID, or accessibility device is an Xbox-style controller. Used by interact prompts and Glyph-aware UI surfaces.

## Custom / accessibility controller foundation

Historical item `c3816` added the first-class foundation for non-standard controllers without exposing raw hardware identity; current status and follow-ups belong in the Workbench:

- `ACTIONMAP_INPUT_CLASS_*` categories distinguish MKB, Controller, Custom, Accessibility, HOTAS, HOSAS, and Mixed as privacy-safe UI/presence metadata.
- SDL raw joystick devices that are not `SDL_GameController` are opened beside normal controllers.
- Bind capture accepts raw joystick buttons and axes 0-5, mapping them into the existing JOY virtual-key range so remapping, action dispatch, and hold/tap semantics remain one system.
- Actionmap dispatch routes raw joystick buttons/axes through player-1 virtual keys, while standard controllers continue through the SDL_GameController path to avoid duplicate events.
- Social presence and friend rows show only coarse input class labels, never GUID/vendor/name.

The next accessibility layer originated in historical item `c3819`; any
remaining work is tracked in the Workbench rather than the retired Kanban.

`c3819` adds the first Settings-side profile layer on top of that foundation:

- Settings now exposes an `Input` tab instead of the old `Controls` tab.
- The page is organized as Profiles, Devices, Bindings, and Tuning rather than nested IMC/device tabs.
- Standard SDL_GameController devices and raw joystick/custom devices are listed together.
- Device nicknames and profile-slot assignments persist through `Input.ProfileNames` and `Input.DeviceProfiles`.
- Profile slots save/load real player-0 actionmap binding snapshots under `$S/input-profiles/profileN.ini`.
- The binding table still writes through `actionmapBind`, `actionmapSaveBinds`, and the existing capture path.

Remaining deeper accessibility work: optional custom glyph texture packs, calibration/deadzones/axis shaping, explicit multi-controller composition, automatic per-device profile activation policy, and richer labels for devices beyond the first six axes.

---

## Active invariants

Per [constraints.md](../constraints.md):

- **Input context stack owns mouse capture.** No menu may call `SDL_SetRelativeMouseMode` or `SDL_ShowCursor` directly.
- **Action map is the only input read mechanism in game-side code.** Raw SDL polling is reserved for the SDL event entry point and a small set of audited helpers.
- **C-button actions retained for rebinding/custom hardware.** Settings -> Input retains `ACTION_CBUTTON_*` in the actionmap binding surface so keyboard, mod, and unusual controller profiles can bind them; do not remove them from the actionmap or default IMC without an explicit product decision.
- **Layer transition flushes must include declared shared actions** (S489, B-266). Cutscene entry is the reference implementation; any new layer or transition that owns shared actions must declare its action set and call `actionmapFlushActionSet`.

---

## What is done (per [audits/infrastructure-pillars-status-2026-04-27.md](../audits/infrastructure-pillars-status-2026-04-27.md) Section 2)

- 121-action enum across movement, combat, weapons, vehicle, menu nav, system,
  forge, observer, voice PTT, skin editor, cutscene skip, and developer/test
  controls.
- B-967 classifies all 121 IDs: 117 user-bindable actions are visible in
  Settings -> Input, while four derived continuous axis channels are
  represented by the visible stick-layout, deadzone, inversion, and
  sensitivity controls. Vehicle direct-use now has a production consumer;
  Forge E/Q are scoped to the Forge IMC.
- Deterministic dispatch (priority + slot + id ordering).
- Single suppression predicate gating every query API call.
- Flush discipline at every transition boundary.
- Hold/tap first-class; gated through layer aperture.
- Deferred-pop correctness with frame-boundary compaction.
- Generation-numbered layer handles; stale-handle pop fails clean.
- IMC lifecycle tied to context push/pop.
- Watchdog with self-recovery (depth >= 15 emergency reset).
- Static-analysis tests pin invariants (e.g. every `actionmap.cpp` query call goes through `actionLayerAllows`; menu files have zero raw `ImGui::IsKeyPressed`).
- Layer-aware aperture extended to `fireVk` dispatch writes (S568+ era), to analog axis polling, to hold bookkeeping, and to legacy `OSContPad` axis bridge through `inputReadController`.
- Voice PTT migrated from raw V-key polling to `ACTION_VOICE_PTT` with shared-action treatment so text fields do not start transmission.
- Synthetic chord VKs for Ctrl+Tab, Ctrl+Z, Ctrl+S etc. with paired keydown/keyup release tracking.
- Backend `ACTION_MENU_TAB_PREV/NEXT` raw injection retired in favor of action-map authority.

---

## What is in flight

- **Input universality Branch 2 / Cohorts 5-8.** Complete 2026-05-19 via c036. Controller is first-class across actionmap-owned system actions, fullscreen/console/debug chords, gameplay suppression gates, observer lifecycle, right-stick menu scroll, tap/hold gameplay affordances, vehicle look/handbrake/use, and the active ImGui menu graph surface.
- **Combat Sim Room per-element binding sweep.** Code/build verified 2026-05-21 under c086, pending Mike playtest. Room-specific controller parity now includes X/right-click context parity for player rows and Add Bot, Start-to-Start-Match from right-panel rows, focused-panel LT/RT skipping, left-panel section walking, and Y undefined on Combat Sim per Q4. MKB Ctrl/Shift-click multi-select remains.
- **Y-Social on allowed menu roots.** Code/build verified 2026-05-21 under c087/c088, pending Mike playtest. Main Menu and Pause Menu poll `pdguiMenuTertiaryPressed()` at the screen level, open Social through the existing menu/social ownership path, render the `ACTION_MENU_SOCIAL` glyph, and block parent B/Escape close while the Social shell owns input. Combat Sim Room remains explicitly undefined for Y per Q4.
- **Main Menu CI camera input gate.** Code/build verified 2026-05-21, pending Mike playtest. Main Menu/File Select auto-open keeps `g_PlayersWithControl[0] = false` while the Carrington Institute camera cutscene is active, then opens the queued menu only after the camera animation is no longer in progress and any stale finished cutscene latch is cleared. This covers cold boot file select, mission/end returns, and Combat Simulator room returns.
- **Cutscene hold-to-skip prompt.** Code/build verified 2026-05-21, pending Mike playtest. Cutscenes now skip only after a deliberate hold against `ACTION_SKIP_CUTSCENE_HOLD_THRESHOLD_MS`, display a contextual Hold [glyph] Skip prompt with the existing radial fill/unfill ring while a skip-related button is held, and suppress gameplay interact prompts during all active cutscenes.
- **Custom/accessibility controller foundation.** Code/build verified 2026-05-21 under c3816, pending manual custom-device retest. Raw non-SDL_GameController devices now bridge into rebind capture and actionmap dispatch through existing JOY virtual keys, glyphs fall back to generic labels for custom-class devices, and Social displays privacy-safe input class.
- **Settings Input tab rebuild.** Code/build verified 2026-05-21 under c3819, pending Mike UI/hardware playtest. The old Settings -> Controls tab is gone from the active UI and Settings -> Input now handles profile naming, connected-device nicknames/profile assignments, simplified Scheme/Input binding selection, and global tuning. Static coverage pins the tab rename, simplified renderer, profile metadata persistence, actionmap profile save/load, and continued actionmap capture path.
- **All-action Settings coverage and live glyph audit.** B-967/B-968 are
  implemented in the current tree. Focused/build results and ordinary-client
  proof are tracked under Workbench `T-INPUT-004` and `V-004`; do not call
  this validated until profile save/reload plus MKB/controller/device-switch
  evidence exists.

---

## Known gaps

- **c036/c086/c087/c088 are code-verified; remaining controller risk is live playtest breadth.** Static/build coverage pins the actionmap migrations, right-stick constants, observer lifecycle, active ImGui menu graph surface, Combat Sim Room per-element binding sweep, and Y-Social root-menu restriction. Mike playtest should still sanity-check common controller flows: main menu, Social from Main/Pause, Combat Sim setup/start/return, mission start/cancel, training flows, multiplayer setup/options, cheats/modal confirms, tap/hold interact/reload, crouch/squat/crouch-jump, and vehicle look/dismount.
- **Legacy runtime menu stack calls still exist outside the active ImGui menu surface.** The c036 closure removed direct stack calls from `port/fast3d/pdgui_menu_*.cpp`; older C menu/runtime plumbing remains out of scope unless a future card targets full legacy stack retirement.

---

## Tests

Coverage at `tests/`: `test_input_authority`, `test_input_layer_stack`, `test_actionmap_flush`, `test_right_stick_scroll`, `test_social_toggle_imc`, `test_editor_tool_hotkeys`. Pure-C mirrors `actionmap_pure.c`, `inputctx_pure.c`, `inputlayer_pure.c` keep the test binary globals-free; each carries a manual @SYNC header for drift audit (see [pillars/tests.md](tests.md)).

---

## Active design references

- [designs/input/input-universality-and-transitions.md](../designs/input/input-universality-and-transitions.md) - Phase 1 design, K.1-K.9 decided 2026-04-27. Cohorts 5-8 complete via c036.
- [designs/input/input-mapping-menu-rebuild.md](../designs/input/input-mapping-menu-rebuild.md) - Phase 1 design. Phase 2 implementation pending.
- [designs/input/contextual-input-schemes.md](../designs/input/contextual-input-schemes.md) - IMC architecture formalization. J-1 / J-2 / J-3 landed.
- [designs/input/input-authority-methodology.md](../designs/input/input-authority-methodology.md) - Policy preventing two-stack drift.
- [designs/input/menu-controller-input-constraints.md](../designs/input/menu-controller-input-constraints.md) - UX contract for ImGui menus.
- [designs/menus/input-authority-and-menu-pool.md](../designs/menus/input-authority-and-menu-pool.md) - ADR for input authority + menu pool. Phase 1+2 implemented.

---

## Where to look

- For menu-side input (acceptance, navigation, modal focus): [pillars/menus.md](menus.md).
- For invariants on context ownership / mouse capture: [constraints.md](../constraints.md) entries on input context, layer transition flushes, observer.
- For cutscene per-player state: [pillars/menus.md](menus.md) (scene event wiring) and the cutscene action set entries in [constraints.md](../constraints.md).
