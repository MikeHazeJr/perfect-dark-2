# Sprint: CPU swarm 256 -> 512 cycle crash (B-330 fix)

- **Date**: 2026-05-16
- **Card**: c029 (Benchmarking)
- **Branch**: dev (no worktree per task rules)
- **Status**: FIXED-PENDING-PLAYTEST. Smoke `swarm_cpu_smoke` PASS 22/22 after the fix.

## Task framing (from caller triage)

Mike's playtest log `Build/pd-client.1.log` captured a silent process exit ~6s after the cycle 256->512 fired:

```
[02:23.61] TESTSCEN.SWARM: cycle NEXT prev_idx=7 -> idx=8 target=512 (alive_was=256)
[02:23.67] TESTSCEN.SWARM: despawn_all freed 254 chrs (was count=256)
<<< process killed; no FATAL line, no further log >>>
```

Hypothesis offered: `despawn_all` force-freeing chrs mid-death-animation (`ACT_DIE` / `ACT_DEAD`) left dangling pointers in the engine's death-state machine. Default fix shape: drain death-state before `chrRemove` + `propFree`.

## What I actually found

Reproduced the silent exit in `tools/smoke-verify/run.ps1 -Test swarm_cpu_smoke` after extending the test to cycle past 256. The harness exited with `code -1073740791` = `0xC0000409` = `STATUS_STACK_BUFFER_OVERRUN` (Windows `__fastfail`, raised by the MSVC `/GS` stack-cookie check at function epilogue). Identical signature to the prior B-307 description in `swarm_cpu_smoke.json` ("the binary force-exits (code 0xC0000409 fail-fast) within ~1s of the 256-bot match running"). B-307's prior fix (`715424c4`, CHRVTX pool bump 120/80 -> 4096) closed a *different* symptom -- the GBI 0xbb0000ff FATAL on the GPU path -- but never addressed the stack-cookie crash class.

Walking the call graph at the `0xC0000409` signature pointed at `roomGetProps()` (`src/game/prop.c:3602`). The function signature declares a `len` parameter for the caller-supplied propnums buffer, but the implementation IGNORES it:

```c
void roomGetProps(RoomNum *rooms, s16 *propnums, s32 len)
{
    s16 *writeptr = propnums;
    // ... walks every chunk in every iterated room ...
    if (ptr == writeptr) {
        writeptr++;
        writeptr[-1] = propnum;   // <-- no bound check against `len`
    }
}
```

24 call sites across `src/game/` and `src/lib/` all pass a 256-element stack-allocated `s16 propnums[256]` plus `len=256`. With 256+ swarm Skedars sharing the player's room at the 128->256 and 256->512 cycles, the writes spill past the caller's stack buffer. The MSVC `/GS` cookie at the caller's function epilogue catches the corruption and raises `__fastfail` -- a kernel-level abort with no SEH unwind, no breadcrumb, no FATAL line. Silent exit, exactly what Mike's playtest log showed.

This is the actual B-307 root cause; the CHRVTX bump in `715424c4` was an unrelated valid fix that happened to be in the same crash neighborhood.

## The fix (B-330)

### `src/game/prop.c::roomGetProps` (primary)
Now honors `len`. Writelimit = `propnums + (len - 1)` (one slot reserved for the `-1` terminator). Once full, additional propnums are dropped; de-dup state stays valid via the existing in-buffer prefix walk; a single `LOG_WARNING` fires per game frame (gated by `g_Vars.lvframe60`) to avoid log saturation when 256+ chr-tick callers all hit the cap on the same frame. Defensive bail on NULL buffer / non-positive len.

Truncation is correctness-safe for the swarm scenario because swarm chrs run with `CHRHFLAG_PERIMDISABLED` (collision broadphase skips them) and the bot AI targets the player prop directly -- truncated rear-of-buffer chrs do not participate in any per-frame correctness path. For non-swarm scenes a room legitimately holding >255 props is not known to exist; the long-term scaling path is to bump caller buffers to `>= MAX_PROPS`, deferred.

### `port/src/swarm_test.c::swarm_drain_death_state` (defensive cleanup, not root cause)
Per caller's directive ("Default Shape A"). New static helper drains transient `ACT_DIE` / `ACT_DEAD` pointers before `chrRemove`:
- `chrBeginDead(chr)` if `ACT_DIE` -- canonical engine transition handles `chrStopFiring` + cover release + sleep clear.
- Force `chr->aibot = NULL`, `chr->target = -1`, `chr->cover = -1` (belt-and-braces; chrRemove already handles these).
- Clear `chr->act_die.notifychrindex` / `chr->act_dead.notifychrindex` (stale alert-fanout cursors).
- Force `chr->fadealpha = -1` (idle value).

Called from both `despawn_all` and `despawn_slot` immediately before `chrRemove`. Kept because it's a real hardening of the swarm despawn path even though the actual crash root cause turned out to live in `roomGetProps`.

`port/src/swarm_test.c` also gains `#include "game/chraction.h"` for `chrBeginDead` / `chrStopFiring` (both already in public header; no new symbol exposure).

## Build verify

```
$ source devtools/build-env.sh
$ ninja -C Build pd pd-server pd-updater pd-tests
[2/4] Building C object CMakeFiles/pd.dir/port/src/swarm_test.c.obj
[2/4] Building C object CMakeFiles/pd.dir/src/game/prop.c.obj
[3/4] Linking CXX executable PerfectDark.exe
```

Exit 0. PerfectDark.exe rebuilt 2026-05-16 11:36 (58.2 MB). pd-server / pd-tests / pd-updater unchanged because prop.c is not in their src lists (verified via Build/build.ninja `PerfectDarkServer.exe:` link line).

## Smoke verify (recursive per caller directive)

Extended `tools/smoke-verify/tests/swarm_cpu_smoke.json` to push past the 128 ceiling:
- 8 cycle taps: 4 -> 8 -> 16 -> 32 -> 48 -> 64 -> 128 -> 256 -> 512.
- 30s dwell at 256 before the 512 cycle (matches Mike's playtest timing exactly).
- Scripted exit 40s after the 512 cycle.
- Asserts `target=256`, `target=512`, `respawn_ring target=512`, `BENCHMARK.SWARM.CPU: SUMMARY count=512`.

### Pre-fix (CHRVTX bump only, before B-330 fix)
```
exit code: -1073740791    (0xC0000409 STATUS_STACK_BUFFER_OVERRUN)
log tail: ...
[01:50.42] TESTSCEN.SWARM: cycle NEXT prev_idx=6 -> idx=7 target=256
[01:50.45] TESTSCEN.SWARM: despawn_all freed 128 chrs
[01:50.45] TESTSCEN.SWARM: respawn_ring target=256 spawned=256 ok=256 failed=0
[01:50.45] BENCHMARK.SWARM.CPU: SUMMARY count=256 kills=2 frame_avg_ms=16.666
[01:50.45] LOG.WPN.DIAG: playerRemoveChrBody <<< process killed >>>
```

### Post-fix
```
elapsed: 181.3s, exit code: 0
PASS 22/22 assertions
[02:22.39] cycle NEXT prev_idx=7 -> idx=8 target=512 (alive_was=256)
[02:22.53] respawn_ring target=512 spawned=512 ok=512 failed=0
[02:22.53] post-cycle target=512 actual=512 alive=510 kills=2
[02:22.53] BENCHMARK.SWARM.CPU: SUMMARY count=512 kills=2 frame_avg_ms=16.667
[03:03.70] SMOKE: scripted exit
[03:03.70] MEMPC [shutdown]: all 5 allocations intact
```

`boot_smoke` regression: PASS 14/14 (no boot-path side effects from the prop.c change).

## Files changed

- `src/game/prop.c` (roomGetProps bounds check + per-frame warning gate)
- `port/src/swarm_test.c` (swarm_drain_death_state helper + use in despawn_all/despawn_slot)
- `context/bugs.md` (B-330 entry)
- `tools/smoke-verify/tests/swarm_cpu_smoke.json` (extended to 256/512 ladder)
- `.claude/sprint-reports/sprint-2026-05-16T154349-cpu-swarm-despawn-race.md` (this file)

## Open items / follow-up

1. The roomGetProps warning fires ~30 times per second during a stable 256/512 run (once per frame, frame-gated). This is informational; if Mike wants the log quieter, bump caller buffers (24 sites) to `s16[MAX_PROPS]` and drop the warning. Deferred per "tight fix" directive.
2. The death-state drain in `swarm_drain_death_state` may now be unnecessary given the actual root cause was elsewhere. Kept because it's defensive hardening that won't regress; can be removed in a future cleanup pass if Mike prefers minimal diff.
3. B-307 in bugs.md describes the OLD CHRVTX hypothesis. With this sprint surfacing the real root cause class, a future pass could fold B-307 / B-330 into a single entry noting the two distinct fixes (CHRVTX bump in `715424c4`, roomGetProps bound check here). Not done in this sprint per scope.
