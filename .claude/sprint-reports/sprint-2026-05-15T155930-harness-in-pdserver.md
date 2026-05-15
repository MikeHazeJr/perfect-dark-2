# Sprint: compile smoke_harness into pd-server target

- Card: c115 (server-pillar coverage / harness reach)
- Branch: dev
- Date: 2026-05-15
- Scope: enable `runtime_strategy: "harness"` for pd-server smoke tests

## Goal

Compile `port/src/smoke_harness.c` into the `pd-server` target so the
server-pillar smoke test (`tools/smoke-verify/tests/dedicated_server_boot_smoke.json`)
can opt out of the `timeout-kill` workflow and use the scripted-exit
harness path instead -- giving us a deterministic exit-code-0 outcome,
SMOKE assertions, and parity with all other smoke tests.

## Investigation

Dependencies pulled in by `port/src/smoke_harness.c`:

| Symbol | Source | In pd-server? |
|---|---|---|
| `<SDL.h>` (SDL_Event, SDL_PushEvent, SDL_GetTicks, ...) | SDL2 | yes (server already links SDL2 + opens window) |
| `system.h` (sysArgGetString, sysMemAlloc/Free, sysLogPrintf, sysLogSetChannelMask, sysLogSetVerbose) | port/src/system.c | yes (already in SRC_SERVER) |
| `actionmapResolveByName` | port/src/actionmap.cpp | **no** (client-only; pulls in input.h / inputctx.h / inputlayer.h) |
| `actionmapInjectStateForSmoke` | port/src/actionmap.cpp | **no** (same TU) |
| `gfxGetSdlWindow` | port/fast3d/gfx_sdl2.cpp | **no** (server uses its own server_gui.cpp window path) |

Three missing symbols. Compiling `actionmap.cpp` / `gfx_sdl2.cpp` into
the server target was rejected -- they pull in big subtrees (input
contexts, controller state, fast3d) the server has no business linking.

**Chosen option: A (stub the 3 client-only symbols in `server_stubs.c`).**

Rationale:
1. Surface is tiny (3 functions, all C-linkage).
2. The server has no input pipeline -- key/action/mouse injection has
   no consumer on pd-server anyway, so the no-op stubs accurately model
   what would happen even if we had real implementations.
3. The harness's `wait` / `exit` / timeout paths -- which is what
   server-pillar smokes actually need -- are pure SDL_GetTicks + JSON
   parsing + log emission. Those work unchanged.
4. `gfxGetSdlWindow` returning NULL is harmless: `smokeResolveWindowId`
   already has a fallback chain (SDL_GetKeyboardFocus / SDL_GetMouseFocus /
   windowID=0), and nothing on the server consumes the synthesised
   events anyway.

## Changes

### `CMakeLists.txt`
Added `port/src/smoke_harness.c` to `SRC_SERVER` with a comment block
explaining what's stubbed and why.

### `port/src/server_stubs.c`
- Removed the pre-c115 stub `int smokeHarnessIsActive(void) { return 0; }`.
  Now provided by `smoke_harness.c` itself (real implementation, returns
  `s_State.active`).
- Added three new stubs:
  - `struct SDL_Window *gfxGetSdlWindow(void) { return NULL; }`
  - `s32 actionmapResolveByName(const char *name) { (void)name; return -1; }`
  - `s32 actionmapInjectStateForSmoke(s32 player, s32 action, s32 down) { ... return 0; }`
- Comment block documents the design (action/key/mouse no-op on server,
  wait/exit/timeout fully functional).

### `port/src/server_main.c`
- Added `#include "smoke_harness.h"` next to the other header includes.
- Added `smokeHarnessInit()` call after `sysLogPrintf("SERVER: Entering main loop")`
  and before the `while (running && !s_ShutdownRequested)` loop. No-op
  when `--smoke` is not passed.
- Added `smokeHarnessTick()` inside the main loop body, after the
  `SDL_PollEvent` GUI dispatch and before the network tick. Cheap no-op
  when harness inactive; when active dispatches scheduled events,
  fires the timeout exit, and writes the SMOKE result line on scripted
  exit.

### `tools/smoke-verify/tests/dedicated_server_boot_smoke.json`
- `runtime_strategy`: `"timeout-kill"` -> `"harness"`
- `timeout_seconds`: 12 -> 15 (gives 5s scripted-exit + 10s of cushion)
- Updated `description` to reflect harness wiring.
- Added `port/src/smoke_harness.c` to `paths_of_interest`.
- `input_sequence`: replaced the advisory-only `exit` at 11000 with
  a real scripted `exit` at 5000.
- `assertions.required_lines`: appended four SMOKE: required matches
  (`harness init`, `scenario=`, `scripted exit at_ms=5000`,
  `result=scripted_exit ... code=0`).
- `assertions.forbidden_patterns`: appended `SMOKE: result=timeout` --
  the harness should never time out on this scenario; if it does, the
  scripted exit got starved out by something blocking the main loop.
- `assertions.required_counts`: added `SMOKE: result=scripted_exit` 1..1.

## Build verify

All 4 targets built cleanly via `build-headless.ps1`:

| Target | Result | Time | Binary |
|---|---|---|---|
| pd        | PASS | 16s | PerfectDark.exe (55.5 MB) |
| pd-updater | PASS |  8s | Updater.exe (12.3 MB) |
| pd-server | PASS |  3s | PerfectDarkServer.exe (22.4 MB) |
| pd-tests  | PASS |  0s | pd-tests.exe (24.9 MB, no-op recompile) |

Smoke_harness.c compiled into pd-server with no warnings beyond the
pre-existing block-comment warnings shared with the client build. Link
clean -- the three client-only symbols resolved against the new stubs in
server_stubs.c on first try (Option A picked correctly; no iteration
needed).

## Smoke verify

`tools/smoke-verify/run.ps1 -Test dedicated_server_boot_smoke -VerboseAssertions`:

```
PASS 26/26 assertions
exit code: 0
elapsed: 7.4s
```

Log evidence of harness wiring:

```
SERVER: Entering main loop
SMOKE: harness init, test=...\dedicated_server_boot_smoke.json
SMOKE: scenario=dedicated_server_boot_smoke timeout_ms=15000 events=2 channel_mask=0xFFFF verbose=0
SMOKE: wait marker at_ms=0
SMOKE: scripted exit at_ms=5000
SMOKE: result=scripted_exit scenario=dedicated_server_boot_smoke elapsed_ms=5014 events_fired=2/2 code=0
```

All 12 required lines, 11 forbidden patterns clean, 3 required counts
in range. PASS.

## Before/after

| Aspect | Before (timeout-kill) | After (harness) |
|---|---|---|
| Process termination | runner Kill() after 12s | server self-exit at 5s |
| Exit code | non-zero (kill -1 / -2) | 0 (clean exit) |
| Assertion count | 21 | 26 (4 new SMOKE: required, 1 new count) |
| Elapsed wall time | ~12s | ~7.4s |
| Harness reach | client only | client + server |
| Determinism | log-only (process kill is async) | scripted exit + SMOKE result sentinel |

## Constraint check

- No N64/Switch guards added. PC-only target. No platform guards.
- No removed-constraint regressions (no IS4MB, no compile-time gates).
- Server pillar bring-up sequence unchanged -- harness init/tick are
  pure additions, no existing call sites moved.
- Hard rules: no kanban edits, no session-log edits, no sub-agents,
  no worktrees. Done in main checkout on `dev`.

## Files touched

- `CMakeLists.txt` (+11 lines, SRC_SERVER addition)
- `port/src/server_stubs.c` (replaced 1 stub with 3, ~30 net lines)
- `port/src/server_main.c` (+12 lines: 1 include, 1 init call, 1 tick call, comments)
- `tools/smoke-verify/tests/dedicated_server_boot_smoke.json` (rewritten)
