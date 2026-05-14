# Sprint Report - c115: Smoke harness extensions (fixtures + debug-spawn-at)

**Date**: 2026-05-14
**Worker**: child / harness-extensions
**Scope**: Two small extensions to the Phase 1 smoke verify harness, in
preparation for Wave 3 test authoring (`mod_load_smoke`,
`save_roundtrip_smoke`, `wall_jump_capsule_smoke`).
**Branch**: dev (main checkout, worktrees disabled per c115 rules).

---

## Extension 1: `fixtures` array in test JSON + runner staging

### Schema delta

Test JSON definitions now accept an optional `fixtures` array. Each
entry has two fields:

```jsonc
"fixtures": [
  { "src": "tools/smoke-verify/fixtures/test_skin_redmund.pdmod",
    "dst": "mods/test_skin_redmund.pdmod" }
]
```

* `src` is repo-relative (resolved against `$ProjectRoot`).
* `dst` is install-relative (resolved against `$InstallDir`, which is
  either `.claude/smoke-verify-install/` for shared mode or
  `.claude/smoke-verify-runs/<utc>-<test>/` for per-test mode).

### Runner integration

Added `Copy-SmokeFixtures` helper to
`tools/smoke-verify/lib/Install-Harness.ps1`. Called from
`Invoke-SmokeTest` in `run.ps1` after `New-SmokeInstall` /
`New-SmokeSharedInstall` materialises the base install (binary + ROM +
optional prefilled data/) but **before** the binary launches.

Per-fixture flow:
1. Validate src/dst both present in JSON entry (skip with warning if not).
2. Resolve absolute paths.
3. If src missing on disk: warn and continue (binary launch decides
   whether the missing fixture is fatal).
4. Create destination parent directory chain (`New-Item -ItemType
   Directory -Force`).
5. `Copy-Item -LiteralPath $absSrc -Destination $absDst -Force`.
6. Log staged count once at the end.

### Conventions

Fixtures live under `tools/smoke-verify/fixtures/`. Seeded with a
`.gitkeep` documenting the convention; no fixture binaries staged yet
(those land with the test authoring tasks).

### Use cases unblocked

* **mod_load_smoke**: pre-stage a `.pdmod` under `mods/<id>.pdmod` so
  `modmgrScanDirectory` picks it up on next boot.
* **save_roundtrip_smoke**: pre-stage a v1 agent save under
  `data/<romid>/saves/agent_001.sav` so the save migrator runs and the
  test can assert post-migration shape.
* Future tests that need any pre-positioned file under the install
  root.

---

## Extension 2: `--debug-spawn-at x,y,z,room` CLI flag

### CLI parsing

Added in `port/src/main.c::bootApplyCliFastPaths`. Pattern mirrors the
existing `--debug-mount-bike` deferred latch.

* `sysArgGetString("--debug-spawn-at")` captures the comma-separated
  token string at boot.
* `bootApplyDebugSpawnAt` parses 4 comma-separated tokens (x,y,z,room).
  x/y/z are `f32` (parsed via `strtod`); room is `s32` widened to fit
  `RoomNum` (s16) at the chrMoveToPos call site.
* Malformed input (wrong token count, unparseable numbers) leaves the
  latch off and emits a `LOG_WARNING` line.
* On success: latches state in four file-static globals
  (`g_BootSpawnAtX/Y/Z/Room`) and sets `g_BootSpawnAtPending = 1`.
  Emits `BOOT: --debug-spawn-at armed: pos=(...) room=...`.

### Deferred tick site

Added `bootDebugSpawnAtTick` in `port/src/main.c` (alongside
`bootDebugMountBikeTick`). Wired into `port/src/pdmain.c::mainTick`
right after the existing mount-bike tick.

Gates (mirror the mount-bike pattern):

* `g_BootSpawnAtPending` must be set.
* `g_Vars.lvframenum >= 4` (load black frame out of the way, player
  prop spawned by `setupCreateProps`).
* `g_Vars.players[0]->prop->chr` must exist.

On fire:

1. Build `struct coord target = {x, y, z}` from the latched floats.
2. Build `RoomNum rooms[2] = { room, -1 }` (canonical single-room
   form, matches `chraicommands.c:5215` AI-script teleport).
3. `setCurrentPlayerNum(0)` to mirror the bike hook's invariant.
4. `chrMoveToPos(chr, &target, rooms, 0.0f, /*force=*/true)`. The
   force flag is on so `chrAdjustPosForSpawn` does not refuse the move
   because of bg-collision near the target.
5. Emit `BOOT: --debug-spawn-at consumed: prop=<addr> stagenum=<hex>
   result=<OK|FAILED> pos=<x,y,z> room=<id>`.
6. Clear `g_BootSpawnAtPending` regardless of `chrMoveToPos`'s return
   value (one-shot per boot).

### Helper choice

`chrMoveToPos` (`src/include/game/chraction.h:208`) is the canonical
teleport entry. The AI-script command at `chraicommands.c:5215`
already calls it with the exact same `[room, -1]` shape; the bot
respawn path at `bot.c:414` uses the same signature. No new
`chrTeleportSimple` helper needed.

Header strategy: declared `chrMoveToPos` as an `extern` shim in
`port/src/main.c` rather than `#include "game/chraction.h"`, to keep
this PC-port file outside the `game/chr*.h` dependency surface (which
transitively pulls `PR/gbi.h` and N64 micro-code defines).
`struct chrdata`, `struct coord`, and `RoomNum` typedef are already
visible via the existing `bss.h` + `data.h` → `types.h` chain.

### Use cases unblocked

* **wall_jump_capsule_smoke**: teleport player 0 to a known ledge so
  the test can fire jump + capsule-sweep assertions without depending
  on AI-script triggers.
* Physics-collision regression coverage that needs deterministic
  positioning.

---

## Files edited

```
port/src/main.c
port/src/pdmain.c
tools/smoke-verify/run.ps1
tools/smoke-verify/lib/Install-Harness.ps1
tools/smoke-verify/fixtures/.gitkeep                (new)
context/designs/engine/smoke-verify-gate.md
```

---

## Build verify

```
powershell -NoProfile -ExecutionPolicy Bypass -File devtools/build-headless.ps1
```

Result:

```
[PASS  ]  CLIENT       8s   -> Build\PerfectDark.exe   (55.5 MB)
[PASS  ]  UPDATER      1s   -> Build\Updater.exe       (12.3 MB)
Total time: 11s
Result: SUCCESS
```

`-Target all` builds Client + Updater (per `devtools/build-headless.ps1:889`,
"all" maps to `@("client", "updater")`). Server + tests targets are
not built by default; the task spec confirmed they do not need to
build for this change. No new warnings introduced.

---

## Deviations from spec

* Spec mentioned an optional smoke-test invocation of the new flag
  (`PerfectDark.exe --skip-intro --debug-spawn-at 0,0,0,0 --smoke-quiet-exit 5000`).
  Skipped: the spec marked it "optional, only if obvious", launching
  the binary at 0,0,0 with no boot stage drops into the title screen
  where there is no `players[0]->prop->chr`, so the tick gate would
  not fire. The log strings are present in the binary (grep verified);
  end-to-end firing of the flag will be exercised when
  `wall_jump_capsule_smoke` authors a fixture stage that actually
  positions the player.
* Spec described `bootApplyCliFastPaths` as the parse site; the
  current main.c parses `sysArgGetString` calls inside `main()` and
  has `bootApplyCliFastPaths` invoke per-flag `bootApply*` helpers.
  Followed the existing pattern: parsing happens inside
  `bootApplyDebugSpawnAt` (which calls `sysArgGetString` itself),
  invoked from `bootApplyCliFastPaths`. This matches how
  `bootApplyDebugMountBike` works.
* Spec mentioned "module-static state in port/src/main.c (mirror
  s_DebugMountBikePending etc.)" with `s_` prefix. The existing
  bike-mount latches use the `g_Boot*` prefix; followed that
  convention for consistency rather than the `s_` form the spec
  cited.

---

## Commit

Title: `Tests - c115: smoke harness extensions (fixtures + debug-spawn-at)`
Refs: c115
Co-Authored-By: standard trailer.
