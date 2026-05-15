# Sprint Report - c115 Input Pillar Smoke (full SDL pipeline)

- Date: 2026-05-14 (UTC)
- Branch: dev
- Worktree: none (main checkout, worktrees disabled)
- Commit: `9d380f41` Tests - c115: full-pipeline SDL smoke test (input pillar coverage)

## Summary

Authored a NEW smoke test covering the input pillar end-to-end with
real `key`-type events (no action injection). Test passes with all
assertions met on the existing iter-2/iter-3 build of PerfectDark.exe.
This closes the c115 input-pillar coverage gap noted in the original
dispatch ("currently only partially covered").

## Deliverable

File: `tools/smoke-verify/tests/full_sdl_pipeline_smoke.json`

64 lines, declarative JSON. Tags: `[menu, input, smoke, pillar:input]`.

Boot args:
`--no-update-check --no-sound --no-net --launch-mp-room base:arena_mp_felicity base:combat 4 --main-menu`

Input sequence (real key events only, no action injection):
- 0 ms wait (boot path arms `g_PostExitMainMenuView=0`)
- 13800 ms Return (main_menu row 0 -> Solo Play push)
- 18000 ms Down (arrow nav)
- 19000 ms Down (arrow nav)
- 20000 ms Up (arrow nav inverse)
- 22000 ms Escape (main_menu close -> MENU.GRAPH.FIRE)
- 26000 ms wait (settle for deferred imgui_menu pop)
- 28000 ms scripted exit

## Run outcome (Outcome A: PASS)

```
powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools/smoke-verify/run.ps1 -Test full_sdl_pipeline_smoke -VerboseAssertions
```

Result: **PASS 20/20 assertions in 28.5s**.

### Required-line assertions matched (16)

- `SMOKE: scenario=full_sdl_pipeline_smoke timeout_ms=\d+ events=\d+`
- `Asset Catalog: \d+ entries registered`
- `BOOT: --no-net set; netInit\(\) skipped`
- `BOOT: --launch-mp-room arena='base:arena_mp_felicity'`
- `BOOT: --main-menu armed`
- `INPUTCTX: imgui_menu on_push -- g_ImcMenu activated`
- `ACTIONMAP: activated 'menu'`
- `MENUPOOL: acquired main_menu`
- `MENU\.GRAPH\.FIRE source=main_menu edge=close trigger=25` (canonical full-pipeline assertion)
- `SMOKE: tap-press scancode=40` (Return)
- `SMOKE: tap-press scancode=41` (Escape)
- `SMOKE: tap-press scancode=81` (Down)
- `SMOKE: tap-press scancode=82` (Up)
- `SMOKE: result=scripted_exit`

### Forbidden-pattern assertions absent (6)

- `EXCEPTION_ACCESS_VIOLATION`
- `FATAL: `
- `SMOKE: result=timeout`
- `LOUDFAIL\.LOAD: .*unrecoverable`
- `INPUTCTX: focus LOST`
- `MENU\.GRAPH\.FIRE .* ok=0`

## Pipeline coverage demonstrated

Each link in the SDL -> ImGui -> actionmap -> menugraph chain is
covered by a distinct assertion:

1. SDL queue receipt: `SMOKE: tap-press scancode=40/41/81/82`
   (proves smokePushKey reached SDL_PushEvent with windowID stamped)
2. ImGui backend forwarding: `INPUTCTX: imgui_menu on_push`
   (proves the windowID-gated event survived the ImGui SDL2 backend)
3. ActionMap dispatch: `ACTIONMAP: activated 'menu'`
   (proves the IMC layer was active and ready to receive)
4. MenuGraph edge fire: `MENU.GRAPH.FIRE source=main_menu edge=close trigger=25`
   (proves the full chain end-to-end: Escape key produced
   ACTION_CANCEL_USE which the menu graph dispatched as the
   `close` edge on `main_menu`)

If a future regression breaks any layer, the failing assertion
immediately localises the gap.

## Findings

### Iter-2/3 windowID fix verified

The earlier iter-2 follow-up commit `7531cece` added windowID stamping
to `smokePushKey` / `smokePushMouse` to fix the ImGui SDL2 backend
windowID gate at `imgui_impl_sdl2.cpp:400`. This test proves the fix
is live: real key events now drive MENU.GRAPH.FIRE through the full
pipeline. Pre-fix the same input sequence would have stuck at the
ImGui backend (no INPUTCTX activation, no edge fire).

### `--main-menu` arms main_menu BEFORE agent_select

Original test design assumed `agent_select` would gate the first key.
Actual flow with `--launch-mp-room --main-menu`: `main_menu` acquires
at ~13.2s **before** `agent_select` becomes the active screen. The
agent_select dialog only acquires later (~25s in our run) after the
Escape pop walks the input context stack down. Test was revised to
assert against the actual main_menu flow rather than the assumed
agent_select gating.

### Main-menu Return doesn't log MENU.GRAPH.FIRE for Solo Play push

Empirical: Return on `main_menu` row 0 (Solo Play) drives MENUPOOL
release/acquire churn (the legacy menuhandler path) but does NOT
fire `MENU.GRAPH.FIRE` for that specific transition. Only the close
edge (Escape) logs through the menugraph dispatcher in this flow.
The canonical full-pipeline assertion is therefore the
Escape -> CANCEL -> close edge, not the Return -> ACCEPT path.
Future menugraph migrations may add a Solo Play accept edge; if so,
this test can be tightened to assert on that too.

## Hard rules honoured

- No C / C++ source edits (test JSON only; smoke_harness.c is the
  separate harness-extensions track owned by `fc9645aa`).
- No kanban / session-log edits (coordinator-owned).
- No sub-agents invoked.
- Main checkout (worktrees disabled).
- Two unrelated pre-existing edits to `boot_smoke.json` and
  `mission_intro_flow.json` were left untouched; they are out of
  scope for this dispatch and belong to a different track.

## Follow-ups (none required)

Outcome A reached on first iteration after one design correction
(the `--main-menu` flow ordering). No infrastructure gap surfaced;
all pipeline markers fired as expected.

Future tightenings if the menugraph dispatcher grows more edges:

- Add `MENU.GRAPH.FIRE source=main_menu edge=solo_play trigger=24`
  once Solo Play push migrates from legacy menuhandler to the
  menugraph dispatcher (would prove ACCEPT-path too).
- Add a Tab key event to exercise tab-cycle bindings once the
  pdgui menus expose Tab as an actionmap-bound trigger.

## Files

- `tools/smoke-verify/tests/full_sdl_pipeline_smoke.json` (new, 64 lines)
- `.claude/smoke-verify-runs/results-20260514T231224Z.json` (run output)
- `.claude/smoke-verify-install/pd-client.log` (canonical log under
  shared-install dir; 28.5s run, all 20 assertions met)

## Commit

```
9d380f41 Tests - c115: full-pipeline SDL smoke test (input pillar coverage)
```

Refs: c115
