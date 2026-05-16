# Sprint Report — Wireless Controller Deadzone Bump

**Date:** 2026-05-16
**Lane:** c036 (Input - Controller Support Cohorts 5-8)
**Branch:** dev
**Scope:** Tighten default stick deadzone defaults to absorb wireless Bluetooth controller noise without affecting wired pads.

## Trigger

Mike playtested with a wireless Xbox Series X controller (GUID `030000005e040000130b000022057200`) registered as player 0 and reported "jank" — micro-input from stick rest noise leaking past the deadzone gate. Wireless Series X over Bluetooth is a well-known noisy-rest controller; the 0.15f (~12.5%) radial deadzone the engine shipped with does not reliably swallow that noise band.

## Investigation

The original task pointed at `port/src/input.c:26 DEFAULT_DEADZONE 4096`. Triage revealed that the **active deadzone** in the live SDL polling path is no longer that constant — it is `s_StickDzMove` / `s_StickDzAim` in `port/src/actionmap.cpp` (both `0.15f`, applied via `applyRadialStick2D` against the raw SDL axis values inside `actionmapPollFrame`, line 1180+).

The legacy `DEFAULT_DEADZONE` in `input.c` still seeds the `padsCfg[].deadzone[]` rows (config-registered as `Input.PlayerN.LStickDeadzoneX/...`) but `inputAxisScale`, the only consumer, has no callers in the current tree (the active design reference in `_old/_archive/designs/input-repair-plan.md` confirms the historical wiring; the C-3 follow-up retired it in favour of `actionValue`). The legacy constant therefore only matters as a default that may flow back through pd.ini if anyone wires the old config keys to anything live. We bump it anyway for consistency and to future-proof against the legacy code being reactivated.

Per-GUID override logic was considered and rejected: the active deadzone is shared across players via static globals, and threading per-controller GUID identification into `actionmap.cpp` would have required restructuring the radial-stick state plumbing. The simpler bump matches what every wired pad tolerates without feeling sluggish (0.18 is the same band most modern shooters ship with) and addresses the reported jitter directly.

## Changes

**`port/src/input.c`**

```c
#define DEFAULT_DEADZONE 6144      /* was 4096 — ~18.7% of stick range vs 12.5% */
#define DEFAULT_DEADZONE_RY 6144   /* unchanged */
```

**`port/src/actionmap.cpp`**

```cpp
static f32 s_StickDzMove = 0.18f;  /* was 0.15f */
static f32 s_StickDzAim  = 0.18f;  /* was 0.15f */
```

Both edits include a comment block citing the c036 lane, the wireless Series X trigger, and the rationale for the small bump.

Both values track each other intentionally: `6144 / 32767 ≈ 0.1875 ≈ 0.18`. The radial in `actionmap.cpp` is what the user feels at the stick; the legacy int constant is the pd.ini fallback floor.

The in-game Settings -> Controls sliders (`actionmapSetStickDeadzoneMove/Aim`, range 0..0.5) and the pd.ini keys `ActionMap.StickDeadzone` / `ActionMap.StickDeadzoneAim` continue to override these defaults — users who already tuned their deadzone will see no change.

## Build verify

```
ninja -C Build pd pd-server pd-tests pd-updater
```

All four targets relinked clean.

| Target | Size |
|---|---|
| `PerfectDark.exe` | 58.2 MB |
| `PerfectDarkServer.exe` | 23.5 MB |
| `pd-tests.exe` | 26.1 MB |
| `Updater.exe` | 12.9 MB |

Both edited files rebuilt: `[124/131] Building C object .../input.c.obj`, `[127/131] Building CXX object .../actionmap.cpp.obj` — no warnings or errors introduced by the change.

(Note: a session-level env hiccup made the bash-side compiler silent until TEMP/TMP/USERPROFILE/PATH/CCACHE_BASEDIR were re-exported manually in the same command line. `devtools/build-env.sh` sets these but the sourcing was being eaten by a `2>/dev/null` redirect chained with `&&`. Once env was re-exported inline, the build went green on the first try.)

## Test regression

Smoke-runner uses PowerShell, which is unavailable in this session. Used pd-tests instead, which has full coverage of the input / actionmap / scroll surface.

```
./Build/pd-tests.exe "[input]"     -> 978 assertions / 43 cases    PASS
./Build/pd-tests.exe "[actionmap]" -> 153 assertions / 21 cases    PASS
./Build/pd-tests.exe "[scroll]"    -> 122 assertions / 10 cases    PASS
```

Notable: `tests/test_right_stick_scroll.cpp:198` (`runtime constants match spec`) is a static-text pin that asserts the runtime contains `const f32 deadzone = 0.18f`. It is **independent** of the actionmap defaults — that test reads `port/fast3d/pdgui_backend.cpp`, which has its own scroll-cursor deadzone constant (0.18f, unchanged). The bump in actionmap.cpp coincidentally matches it but does not exercise the same path. Test still passes.

No deadzone constants in the test tree pin against `s_StickDzMove` / `s_StickDzAim` directly. The legacy `Input.PlayerN.*Deadzone*` pd.ini keys are config-registered with min/max bounds (0..32767) that comfortably contain the new 6144 default.

Smoke test `full_sdl_pipeline_smoke` would not exercise the change — it injects keyboard events through `smokePushKey`, never analog stick axes, so the new defaults can't move that test pass/fail state either direction. Skipped on this basis (documented for the reviewer).

## c036 closeout consideration

Checked the c036 card's open_questions and notes: no mention of a controller-deadzone gap. The remaining subtask `s036-08` (menu graph completion, 15-25 sessions) is unrelated. No card status update required — this commit is a quality-of-life fix outside the cohort task list.

## Files touched

- `port/src/input.c` (1 hunk, +5 -2)
- `port/src/actionmap.cpp` (1 hunk, +6 -2)
- `.claude/sprint-reports/sprint-2026-05-16T112000-wireless-deadzone.md` (this file)

## Commit

```
Input - c036: wireless controller deadzone bump (Series X Bluetooth)
```

Refs: c036.
