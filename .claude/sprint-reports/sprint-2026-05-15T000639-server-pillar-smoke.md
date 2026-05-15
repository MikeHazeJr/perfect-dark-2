# Sprint Report - c115 Server Pillar Smoke (dedicated_server_boot_smoke + runner target-swap)

- Date: 2026-05-15 (UTC) (dispatch tag 2026-05-14)
- Branch: dev (worktrees disabled)
- Build under test: `Build\PerfectDarkServer.exe` + `Build\PerfectDark.exe` from dev `189e67be`

## Summary

First server-pillar smoke fixture. Extends the smoke verify gate's
`run.ps1` + `lib/Install-Harness.ps1` to launch either `PerfectDark.exe`
(default, unchanged) or `PerfectDarkServer.exe` (new) per the new
optional `target` field in test JSON, and adds a parallel
`runtime_strategy` field that selects between the existing harness-driven
scripted-exit workflow ("harness") and a timeout-kill workflow
("timeout-kill") needed for long-running daemons that do not link
`smoke_harness.c`.

Authored `tools/smoke-verify/tests/dedicated_server_boot_smoke.json`
exercising a vanilla `--headless --port 27200 --maxclients 4
--no-update-check` boot of `PerfectDarkServer.exe`. PASS 20/20
assertions in 12.0s. Regression-checked against `mission_intro_flow`
(client target, harness strategy): PASS 18/18 assertions, exit code 0,
no regression of the default code path.

## Case decision: Case B (timeout-kill)

`CMakeLists.txt` `SRC_SERVER` (lines 629-742) does NOT include
`port/src/smoke_harness.c`. The dispatch's Case A (harness-driven
`--smoke <test.json>` injection) is therefore unavailable on this
target -- the binary would parse `--smoke` as an unknown CLI arg and
boot normally, but no scripted-exit sentinel would land. Case B is
required: launch with vanilla boot_args, wait `timeout_seconds`, then
forcibly kill. Assertions are log-only; the non-zero exit code from
the kill is acceptable for this strategy.

`runtime_strategy` defaults to:
- `"harness"` when `target == "pd"` (back-compat, the historical default)
- `"timeout-kill"` when `target == "pd-server"` (the only target lacking
  smoke_harness.c today)

A test JSON may set `runtime_strategy` explicitly to override the
default -- e.g. if a future commit links `smoke_harness.c` into
`SRC_SERVER`, the server tests can opt into the harness workflow
without code changes by setting `"runtime_strategy": "harness"`.

## Files touched

| File | Edit |
|------|------|
| `tools/smoke-verify/run.ps1` | parse new `target` + `runtime_strategy` fields; thread `-Target` to install harness; conditional `--smoke <path>` injection; conditional sentinel parse; conditional exit-code gate; new "timeout-kill" wait-and-Kill path |
| `tools/smoke-verify/lib/Install-Harness.ps1` | `Find-SourceBinary` -Target switch (pd vs pd-server exe); `New-SmokeInstall` + `New-SmokeSharedInstall` -Target param; ROM seed skipped for pd-server (no ROM-load path); `Get-SmokeLogPath` -Target switch (pd-server.log vs pd-client.log); firewall rule display name split per exe ("PD2 Smoke Verify" vs "PD2 Smoke Verify (Server)") |
| `tools/smoke-verify/tests/dedicated_server_boot_smoke.json` | new test, 60 lines |

## Test scenario

Boot args: `--headless --port 27200 --maxclients 4 --no-update-check`.
Timeout: 12s (server boot lands `SERVER: Entering main loop` at ~t+150ms
on this machine; 12s covers cold-start variance + STUN/UPnP background
discovery noise).

Required-line markers (8):
- `NET: using protocol version \d+`
- `NET: created server on port 27200`
- `HUB: initialised, state=LOUNGE`
- `BANS: no ban file at .* \(0 bans loaded\)` (clean install)
- `ADMIN: no admin token configured`
- `SERVER: Running in headless mode`
- `SERVER: Listening on port 27200 \(max 4 clients\)`
- `SERVER: Entering main loop`

Forbidden patterns (10):
- `EXCEPTION_ACCESS_VIOLATION`, `FATAL: `, `LOUDFAIL\.LOAD: .*unrecoverable`
- Server bring-up failures: `SERVER: Failed to start`,
  `SERVER: SDL_Init failed`, `SERVER: SDL_CreateWindow failed`,
  `SERVER: SDL_GL_CreateContext failed`
- Net bring-up failures: `NET: could not init ENet, disabling networking`,
  `NET: could not create ENet host on port \d+`
- `CLC_AUTH: ROM hash check fired` -- positive-assertion of the
  invariant from `context/pillars/server.md`: dedicated servers MUST
  skip the ROM hash check at `CLC_AUTH` because no ROM is loaded.

Count pins (2):
- `SERVER: Entering main loop` x1 (no re-entry)
- `NET: created server on port 27200` x1 (no rebind churn)

## Marker discovery notes

- `NET: dedicated server mode enabled` (suggested in the dispatch
  outline) is in `port/src/net/net.c:828` but is gated on
  `sysArgCheck("--dedicated")` -- it fires for the IN-CLIENT path
  (PerfectDark.exe launched with `--dedicated`), not for the standalone
  PerfectDarkServer.exe (which sets `g_NetDedicated` directly in
  `server_main.c` BEFORE `netInit` runs). Asserting on this marker
  would silently fail every dedicated server smoke. The test description
  documents this nuance to head off "obvious" follow-up edits.
- Early `printf()` calls in `server_main.c` (e.g. "PD2 Dedicated Server
  v... starting...") go to stdout, NOT to `pd-server.log`. The runner
  only inspects the log file, so these strings are NOT viable
  assertions. Only `sysLogPrintf`-emitted markers end up in the file.

## Validation

```
=== dedicated_server_boot_smoke ===
  target: pd-server (PerfectDarkServer.exe)
  strategy: timeout-kill
  log: .claude\smoke-verify-install\pd-server.log
  exit code: -1
  elapsed: 12.0s
  PASS 20/20 assertions
  Total: 1 pass, 0 fail
```

Back-compat regression check:

```
=== mission_intro_flow ===
  target: pd (PerfectDark.exe)
  strategy: harness
  log: .claude\smoke-verify-install\pd-client.log
  exit code: 0
  elapsed: 61.3s
  PASS 18/18 assertions
```

The default path (`target: "pd"`, `runtime_strategy: "harness"`) still
injects `--smoke <path>`, parses the harness sentinel, and gates on
exit code 0 -- no observable change for the 13 existing client tests.

## How a future regression surfaces

Three live regression classes are now covered:

1. **Server bring-up regression** -- a code change that breaks any of
   the 8 required-line markers (e.g. a regression in `netStartServer`,
   `hubInit`, `serverBansInit`, `serverAdminInit`, or the
   `--headless`/`--no-update-check` arg parsing) lights up this test
   inside ~12s.
2. **The dedicated ROM-skip invariant** -- if a future commit accidentally
   re-enables the ROM hash check at `CLC_AUTH` for dedicated servers
   (or somebody fat-fingers the `!g_NetDedicated` gate), the
   `CLC_AUTH: ROM hash check fired` line lands and the test fails
   immediately. Today this is a quiet code path because no ROM is
   loaded; without this test, a regression would only surface during
   a live client-to-server connect attempt with a wrong ROM.
3. **The smoke runner target-swap mechanism itself** -- if `run.ps1`
   loses the `target` parameter wiring or `Install-Harness.ps1`
   regresses on the pd-server exe seed path, this test fails before
   the binary even launches (no pd-server.log) and the assertion
   engine's `log_missing` failure mode trips.

## Out of scope (deliberately)

- Wrapper `tools/smoke-verify/run-pd-tests-smoke.ps1` (owned by parallel
  worker per the dispatch).
- Multi-test pd-server coverage (room creation, ban list parse, admin
  RCON token init -- can layer on later using the same `target` /
  `runtime_strategy` plumbing).
- pd-server pillar fixtures depending on prefilled state (no
  `install_state: prefilled` exercised today; ROM-less server doesn't
  consume `data/<romid>/` anyway).
- Linking `smoke_harness.c` into `pd-server` (a follow-up CMake change
  could opt the server build into harness-driven scripted exits, after
  which existing pd-server smoke JSON files can flip
  `runtime_strategy: harness` -- no runner changes needed).

## Refs

- c115 (smoke verify gate work front)
- Pillar contract: `context/pillars/server.md`
- Listen-vs-dedicated semantics:
  `context/designs/connectivity/hosting-modes-listen-vs-dedicated.md`
- Smoke runner iter-3 baseline: commits `a803d534`, `7531cece`,
  `a18f7569`, `189e67be`
