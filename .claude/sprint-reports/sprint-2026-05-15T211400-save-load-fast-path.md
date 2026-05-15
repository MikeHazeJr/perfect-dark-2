# Sprint c118 — save_load_smoke + --launch-load-agent fast-path

**Date**: 2026-05-15
**Branch**: dev
**Card**: c118 (engine-tooling daily-flow follow-up)
**Type**: Tests + CLI fast-path (combined unit)
**Status**: SHIPPED (26/26 smoke assertions)

---

## Summary

Shipped a combined unit that delivers deep save-pillar coverage of the
`saveLoadAgent` wire-format path without scripted menu nav:

1. New CLI fast-path **`--launch-load-agent <name>`** that latches a name at
   boot, defers to mainTick, and calls `saveLoadAgent(name)` on the first
   stage frame past `lvframenum >= 4`.
2. New smoke test **`save_load_smoke`** that pre-stages the v2 agent fixture,
   boots with the new flag, and asserts the full chain: arm → consume → SAVE
   path emits `loaded from ...`.

This bypasses the structural blocker hit in the prior worker's iter-1 attempt
(Agent Select UI routes through `gamefileLoad`, not `saveLoadAgent` — see
`port/PHASE_D5_PLAN.md:89` for the planned-but-unwired hook). Future
production wiring of `saveLoadAgent` into the menu path doesn't change the
coverage this test gives; the fast-path remains valuable for regression.

---

## CLI parse (port/src/main.c)

Module-static state added next to the other CLI fast-path latches:

```c
static s32         g_BootLoadAgentArmed = 0;
static char        g_BootLoadAgentName[64] = {0};
```

Parser helper (mirrors `bootApplyDebugSpawnAt`):

```c
static void bootApplyLaunchLoadAgent(const char *arg)
{
    if (!arg || !arg[0]) {
        return;
    }
    const size_t maxLen = sizeof(g_BootLoadAgentName) - 1;
    if (strlen(arg) > maxLen) {
        sysLogPrintf(LOG_WARNING,
            "BOOT: --launch-load-agent name too long (max %zu chars); got: '%s'",
            maxLen, arg);
        return;
    }
    strncpy(g_BootLoadAgentName, arg, maxLen);
    g_BootLoadAgentName[maxLen] = '\0';
    g_BootLoadAgentArmed = 1;
    sysLogPrintf(LOG_NOTE,
        "BOOT: --launch-load-agent armed: name='%s'", g_BootLoadAgentName);
}
```

Dispatched from `bootApplyCliFastPaths` alongside `bootApplyDebugSpawnAt`:

```c
bootApplyLaunchLoadAgent(sysArgGetString("--launch-load-agent"));
```

## Deferred tick (port/src/main.c + port/src/pdmain.c)

```c
s32 bootLaunchLoadAgentTick(void)
{
    if (!g_BootLoadAgentArmed) return 0;
    if (g_Vars.lvframenum < 4) return 0;

    s32 result = saveLoadAgent(g_BootLoadAgentName);
    sysLogPrintf(LOG_NOTE,
        "BOOT: --launch-load-agent consumed: name='%s' result=%s",
        g_BootLoadAgentName, result == 0 ? "OK" : "FAILED");
    g_BootLoadAgentArmed = 0;
    return 1;
}
```

Wired in `pdmain.c::mainTick` next to `bootDebugSpawnAtTick`:

```c
{
    extern s32 bootLaunchLoadAgentTick(void);
    (void)bootLaunchLoadAgentTick();
}
```

`saveInit()` runs synchronously at `main.c:957` long before any deferred
tick fires, so the save dir is always wired when `saveLoadAgent` is called.
The `lvframenum >= 4` gate keeps the assertion ordering deterministic
relative to the boot integrity log so the smoke harness sees a stable
log order.

## Build verify

Build ran via `ninja -C Build pd pd-server` (wrapped in a `.bat` for env
delivery — bash env didn't propagate USERPROFILE/LOCALAPPDATA into ccache,
which masked compiler error output until traced via `script.exe` PTY).

- `port/src/main.c`: compiled clean (only pre-existing `'/*' within comment`
  warnings from comment formatting in unrelated blocks)
- `port/src/pdmain.c`: compiled clean
- Linked `PerfectDark.exe` (58.2 MB)
- `PerfectDarkServer.exe` unchanged (no source dependency on our diffs)

No new warnings. No new errors.

## Smoke test (tools/smoke-verify/tests/save_load_smoke.json)

Boot args: `--no-update-check --no-sound --no-net --skip-intro --portable --launch-load-agent smoke`

Fixture: `tools/smoke-verify/fixtures/agent_smoke.json` → `<install>/agent_smoke.json`

15s scripted-exit run.

Required patterns (9):

- `SMOKE: scenario=save_load_smoke`
- `BOOT: --no-net set; netInit\(\) skipped`
- `BOOT: --launch-load-agent armed: name='smoke'`
- `SAVEMIGRATE: Initialized \(0 migrations registered, current save version: 2\)`
- `SAVE: initialized — save dir: `
- `Asset Catalog: \d+ entries registered`
- `BOOT: --launch-load-agent consumed: name='smoke' result=OK`
- `SAVE: agent 'smoke' loaded from `
- `SMOKE: result=scripted_exit`

Forbidden patterns (12): standard crash set + save error patterns + the
new `result=FAILED` / `name too long` patterns.

Required counts: 5 patterns must match exactly once (saveLoadAgent is
one-shot, so a re-fire would indicate a latch-clear bug).

## Test results

```
save_load_smoke (16.2s) assertions=26/26 — PASS
save_init_smoke (15.5s) assertions=18/18 — PASS (no regression)
```

The save_load_smoke run confirms:
- CLI arg arms latch at boot (1× armed log line)
- Deferred tick fires once past `lvframenum >= 4` (1× consumed log line, result=OK)
- saveLoadAgent reads the fixture, validates v2, parses fields, emits its
  own success log line (1× `SAVE: agent 'smoke' loaded from`)
- One-shot semantics enforced (counts all max=1)

## Files touched

- `port/src/main.c` — CLI doc comment, static state, parser helper, deferred tick, dispatcher hook
- `port/src/pdmain.c` — mainTick wiring
- `tools/smoke-verify/tests/save_load_smoke.json` — new test
- `.claude/sprint-reports/sprint-2026-05-15T211400-save-load-fast-path.md` — this file

Single coherent commit: c118.
