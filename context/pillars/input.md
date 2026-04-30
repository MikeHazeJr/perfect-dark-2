# Input System

> Unified action map. Pushdown input context stack. Typed layer stack. Single suppression predicate. The action map is the only legitimate way to read input outside the SDL event entry point.

---

## What it is

The input system has three principal subsystems sitting on top of SDL2:

1. **Action map** ([port/include/actionmap.h](../../port/include/actionmap.h), [port/src/actionmap.cpp](../../port/src/actionmap.cpp), 2912 lines). 103 named `InputAction` values, 12 `InputMappingContext` (IMC) singletons stacked by priority, deterministic dispatch. Every game-side input read goes through `actionPressed/Held/Value`.
2. **Input context stack** ([port/include/inputctx.h](../../port/include/inputctx.h), [port/src/inputctx.c](../../port/src/inputctx.c), 974 lines). 16-slot pushdown stack of input contexts (`g_CtxGameplay`, `g_CtxImGuiMenu`, `g_CtxPauseMenu`, `g_CtxDebugOverlay`). Owns SDL mouse mode and the `gameplayInputSuppressed()` single-truth-source predicate.
3. **Layer stack** ([port/include/inputlayer.h](../../port/include/inputlayer.h), [port/src/inputlayer.c](../../port/src/inputlayer.c), 421 lines). 7 typed layers with handle-based push/pop, generation counters, scene-event integration.

Single SDL event entry point: [port/fast3d/pdgui_backend.cpp:1272](../../port/fast3d/pdgui_backend.cpp:1272) `pdguiProcessEvent`. Every event flows ImGui -> WantCaptureKeyboard gate -> `actionmapDispatch` -> `inputCtxDispatch`.

---

## Action map

### Action enum

103 actions defined at [port/include/actionmap.h:54-248](../../port/include/actionmap.h:54). Categories:

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

---

## Glyph system

`port/include/pdgui_glyphs.h` + `port/fast3d/pdgui_glyphs.cpp` resolve any `InputAction` to its current primary VK through the active IMC stack, produce short labels ("E", "Space", "LMB", "A", "LB", "D-Up"), and render `[KEY] Label` pills on the foreground drawlist. Auto-detects KBM vs gamepad via `actionmapGetLastDevice()` with 500ms debounce. Used by interact prompts and Glyph-aware UI surfaces.

---

## Active invariants

Per [constraints.md](../constraints.md):

- **Input context stack owns mouse capture.** No menu may call `SDL_SetRelativeMouseMode` or `SDL_ShowCursor` directly.
- **Action map is the only input read mechanism in game-side code.** Raw SDL polling is reserved for the SDL event entry point and a small set of audited helpers.
- **C-button actions retained but hidden in controller tab.** Settings -> Controls -> Controller hides the C-button group in the bind table; do not remove `ACTION_CBUTTON_*` from the actionmap or default IMC without an explicit product decision.
- **Layer transition flushes must include declared shared actions** (S489, B-266). Cutscene entry is the reference implementation; any new layer or transition that owns shared actions must declare its action set and call `actionmapFlushActionSet`.

---

## What is done (per [audits/infrastructure-pillars-status-2026-04-27.md](../audits/infrastructure-pillars-status-2026-04-27.md) Section 2)

- 103-action enum with full coverage across movement, combat, weapons, vehicle, menu nav, system, forge, observer, voice PTT, skin editor, cutscene skip.
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

- **Input universality Branch 2 / Cohorts 5-8.** Per [designs/input/input-universality-and-transitions.md](../designs/input/input-universality-and-transitions.md), Cohort 1 (layer types + scene events), Cohort 2 (layer push/pop), Cohort 3 (IMC ownership migration), Cohort 4 (per-player cutscene state) shipped. Cohorts 5-8 cover full controller support, menu graph completion, and remaining transitional shim retirement. Post-context-rebuild priority lane after Catalog Gate 3.
- **Input mapping menu rebuild.** Phase 1 design at [designs/input/input-mapping-menu-rebuild.md](../designs/input/input-mapping-menu-rebuild.md); Phase 2 implementation gated on Priority L menu pass.

---

## Known gaps

- **F-key system actions bypass the actionmap.** [pdgui_backend.cpp:1288-1379](../../port/fast3d/pdgui_backend.cpp:1288) contains 10 raw `if (ev->key.keysym.sym == SDLK_Fxx)` blocks (F6-F12, Shift+F1, F2, Shift+F2, RS-click hotswap toggle). They consume events before `actionmapDispatch` is reached at line 1439. Not rebindable, not gateable through `gameplayInputSuppressed()`. Migrate to `ACTION_HOTSWAP_TOGGLE`, `ACTION_DIAG_TOGGLE`, `ACTION_MESH_DEBUG_TOGGLE` on `g_ImcDebugOverlay` or a new system IMC.
- **`gfx_sdl2.cpp` has 3 raw hotkeys.** Lines 335-345: Alt+Enter (fullscreen), F10 (mesh debug), backquote (console toggle). Backquote duplicates `ACTION_CONSOLE_TOGGLE`. Migrate or delete the duplicates.
- **`input.c` retains direct hardware polling.** Lines 1075, 1092, 1099 call `SDL_GameControllerGetAxis` and `SDL_GameControllerGetButton` for trigger and stick-direction VKs. Used by legacy `inputKeyJustPressed` (line 1115, marked DEPRECATED). After full controller migration, narrow to mouse buttons only per the DEPRECATED comment.
- **Layer stack IMC pointers deferred.** [port/src/inputlayer.c:210, 237](../../port/src/inputlayer.c:210) have `.imc = NULL, /* Cohort 3: &g_ImcGameplay */` and `/* Cohort 3: &g_ImcMenu */`. Layer stack only owns IMC lifecycle for vehicle and cutscene; gameplay and menu IMCs are still in `inputctx.c` on_push/on_pop callbacks. Wire as part of Cohort 5+.
- **`pdgui_spectator.cpp:162` uses the wrong gate.** Checks `io.WantCaptureKeyboard` rather than `gameplayInputSuppressed()` or top-of-stack ctx. WantCaptureKeyboard is ImGui-internal state, not the input authority.
- **Right-stick scroll spec is decoupled from runtime.** [tests/test_right_stick_scroll.cpp:14-18](../../tests/test_right_stick_scroll.cpp:14) is the spec; runtime is `pdguiDriveImGuiNav`. If runtime drifts from deadzone 0.15 / max 1200 px/s / exponent 1.7, no test catches it. Add a static test linking the runtime constants to the spec.
- **Observer layer asymmetry.** [port/src/inputlayer.c:157-183](../../port/src/inputlayer.c:157) `onObserverPush` activates `g_ImcObserver` only when source == SPECTATOR; `onObserverPop` deactivates unconditionally. Likely safe but defect.

---

## Tests

Coverage at `tests/`: `test_input_authority`, `test_input_layer_stack`, `test_actionmap_flush`, `test_right_stick_scroll`, `test_social_toggle_imc`, `test_editor_tool_hotkeys`. Pure-C mirrors `actionmap_pure.c`, `inputctx_pure.c`, `inputlayer_pure.c` keep the test binary globals-free; each carries a manual @SYNC header for drift audit (see [pillars/tests.md](tests.md)).

---

## Active design references

- [designs/input/input-universality-and-transitions.md](../designs/input/input-universality-and-transitions.md) - Phase 1 design, K.1-K.9 decided 2026-04-27. Cohorts 5-8 in flight.
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
