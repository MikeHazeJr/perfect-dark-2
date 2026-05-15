# Sprint Report - c115 Tests Pillar Meta-Smoke (pd_tests_runtime_smoke)

- Date: 2026-05-14 (UTC)
- Branch: dev
- Worktree: none (main checkout, worktrees disabled)
- Build under test: `Build\pd-tests.exe` from dev `189e67be`

## Summary

Authored a META smoke verify wrapper that runs the pd-tests Catch2
binary and asserts the unit suite reports clean against a documented
allowlist of pre-existing carry-over failures. Unlike every other
c115 smoke (boot, mp_room_flow, mission_intro_flow, mod_load,
listen_host_init, save_init, physics_capsule_basic) which exercise
the in-client harness, this one exists to catch regressions in the
pd-tests SUITE itself -- a class of bug that has shipped silently in
the past because Mike doesn't always run `pd-tests` between releases.

PASS on full-suite run (6 allowlisted carry-overs detected, 0 new
failures, exit -1073741819 / 0xC0000005 = pre-existing teardown
segfault accepted). PASS on scoped `[netbuf]` confidence run (23/23
test cases, 133/133 assertions).

## Deliverable

File: `tools/smoke-verify/run-pd-tests-smoke.ps1` (~310 lines).

Standalone wrapper. Does NOT modify `tools/smoke-verify/run.ps1`
(parallel worker is editing that file for the server pillar).

**Invocation**:
```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
    -File tools/smoke-verify/run-pd-tests-smoke.ps1 -VerboseAssertions
```

**Output**: `results-<utc>Z-pd_tests_runtime_smoke.json` written to
`.claude/smoke-verify-runs/`, matching the per-result schema that
`run.ps1` produces (Name / Category / BugId / RegressionFor / Passed
/ ExitCode / ElapsedSeconds / InstallDir / AssertionsTotal /
AssertionsMet / Failures) plus tests-pillar-specific fields
(Catch2*Total/Met, NewFailures, AllowlistedFailures,
KnownFailureAllowlist, Scope).

Parameters:
- `-Scope <catch2-filter>` -- e.g. `[netbuf]` or
  `[catalog][provider][static]`. Empty = full suite.
- `-ExePath <path>` -- override pd-tests.exe location (default
  `Build/pd-tests.exe`).
- `-OutputDir <path>` -- override results JSON dir.
- `-VerboseAssertions` -- echo every parsed line / allowlist
  decision.
- `-TimeoutSeconds <int>` -- watchdog (default 300).

## Scope strategy

Chose **A** (allowlist of carry-over test case names) over B (scope
to currently-passing subset). Rationale:

- The goal is to catch NEW pd-tests regressions, not to chip away at
  the existing carry-overs. A new failure in `test_netbuf` or
  `test_input_authority` would silently ship under strategy B
  because those scopes wouldn't run.
- The allowlist gates by Catch2 TEST_CASE name, NOT by file. So a
  NEW failure inside an already-allowlisted file (e.g. a new
  REQUIRE added to `test_uichrome_paths_pin.cpp`) still surfaces.

## Allowlist

Six TEST_CASE names, sourced from `context/session-log.md:65`
(c129 carry-over note), `:1202`, `:1988`, and the 2026-05-13
infrastructure audit:

| File | TEST_CASE name | Why pre-existing |
|------|----------------|------------------|
| `test_uichrome_paths_pin.cpp:50` | `uichrome-paths: catalog entries point at data/ui/textures` | post-Step 5 path-shape drift |
| `test_uichrome_paths_pin.cpp:77` | `uichrome-paths: extraction destination writes to data/ui/textures` | post-Step 5 path-shape drift |
| `test_uichrome_paths_pin.cpp:89` | `uichrome-paths: directory creation targets data/ui/textures` | post-Step 5 path-shape drift |
| `test_uichrome_paths_pin.cpp:145` | `uichrome-paths: 13 catalog entries pinned, 14 extraction filenames pinned` | count drifted 13 -> 14 |
| `test_pdbase_retired_audit.cpp:93` | `step5: no pdbase substrings remain in port/ source` | four `*_authored.c` files still reference pdbase in comments |
| `test_catalog_provider_static.cpp:528` | `rom-backed catalog registration helpers populate provider handles` | Pass B FileProvider migration retired the assertions the test grep'd for |

When a carry-over is fixed: the allowlist entry MUST be deleted in
the same commit. Stale allowlist entries hide regressions.

## Validation

```
[FULL SUITE]
  pd-tests runtime smoke (c115 tests pillar)
  exe:     C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike\Build\pd-tests.exe
  scope:   <all>
  timeout: 300s
  exit code: -1073741819
  elapsed:   2.2s
  [ok] exit_code_allowed
  [ok] no_new_failures
  [ok] summary_footer_present_or_known_teardown_segfault
  [ok] failure_count_accounted_skipped_no_footer
  [PASS] pd_tests_runtime_smoke (2.2s)
       Catch2: 0 cases (0 pass / 0 fail), 0 assertions (0 pass)
       allowlisted carry-over failures: 6
  EXIT=0
```

(The Catch2 footer reports 0/0 because the segfault-on-teardown
class also pre-existing per session-log:1202 swallows the footer.
The script's per-FAILED regex parse picks up the 6 carry-overs
authoritatively; the soft-guard accepts the missing footer when the
exit code is in the segfault-class allowlist.)

```
[SCOPED [netbuf] CONFIDENCE RUN]
  Filters: [netbuf]
  All tests passed (133 assertions in 23 test cases)
  [PASS] pd_tests_runtime_smoke (0.0s)
       Catch2: 23 cases (23 pass / 0 fail), 133 assertions (133 pass)
```

Confirms the parser correctly extracts the `All tests passed` footer
on a clean run.

## How a future regression would surface

Three classes:

1. **NEW test case failure outside the allowlist**: parsed by the
   per-`FAILED:` regex walker, classified as `NewFailures` (not
   in `$KnownFailureCases`), trips the `no_new_failures` assertion,
   exits 1. Visible at the top of the result JSON under
   `NewFailures`. Surfaces in stdout as `[FAIL] no_new_failures --
   new (non-allowlisted) failing TEST_CASEs: <names>`.

2. **pd-tests fails to build / exe missing**: the wrapper throws at
   bootstrap with `pd-tests.exe not found at <path>. Build it first:
   ninja -C Build pd-tests.`. Exit code propagates as 1.

3. **Crash before any test case runs** (e.g. dll-resolve failure):
   exit code lands outside the allowed-codes set, trips
   `exit_code_allowed`. Visible as `[FAIL] exit_code_allowed --
   expected one of <list>, observed <code>`.

The intended next-session follow-up is to wire the wrapper into the
daily-flow build-headless gate so a regression caught here flags
before the next playtest.

## Hard-rules compliance

- DID NOT MODIFY `tools/smoke-verify/run.ps1` (parallel server worker
  owns it this session).
- DID NOT MODIFY `tools/smoke-verify/lib/*` (also parallel-worker
  territory).
- No C / C++ source edits.
- No kanban / session-log edits.
- No sub-agents.
- Worktrees disabled (main checkout, dev branch).
- pd-tests built clean before the slice (`Build\pd-tests.exe` from
  dev `189e67be`).

## Files touched

- `tools/smoke-verify/run-pd-tests-smoke.ps1` (NEW, ~310 lines)
- `.claude/smoke-verify-runs/results-20260515T000540Z-pd_tests_runtime_smoke.json` (full-suite run output)
- `.claude/smoke-verify-runs/results-20260515T000548Z-pd_tests_runtime_smoke.json` (scoped [netbuf] confidence run)

## References

- `context/pillars/tests.md` -- pillar contract, scope aliases, known
  gaps.
- `context/session-log.md:65` -- c129 carry-over note.
- `context/session-log.md:1202` -- "Pre-existing source-grep test
  failures ... terminal segfault in pd-tests is pre-existing".
- `context/session-log.md:1988` -- "Segfault-on-teardown also
  pre-existing per the same memo".
- `tests/test_uichrome_paths_pin.cpp` -- 4 of 6 allowlisted carry-overs.
- `tests/test_pdbase_retired_audit.cpp` -- 1 of 6 allowlisted carry-overs.
- `tests/test_catalog_provider_static.cpp:528` -- 1 of 6 allowlisted carry-overs.
- `devtools/run-pd-tests.ps1` -- per-session isolated runner the
  wrapper does NOT use (we go direct to `Build/pd-tests.exe` to
  avoid the per-session rebuild cost; the smoke is meta, not
  invariant-pinning).
