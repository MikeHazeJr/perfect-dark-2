# Sprint Report - c115 Connectivity Pillar Smoke (listen_host_init)

- Date: 2026-05-14 (UTC)
- Branch: dev
- Worktree: none (main checkout, worktrees disabled)
- Commit: `605faf3f` Tests - c115: listen_host_init_smoke (connectivity pillar)
- Build under test: `Build\PerfectDark.exe` from dev `189e67be`

## Summary

Authored a NEW in-client smoke test that proves the connectivity stack
(netInit, p2pInit, presenceInit) initialises end-to-end on a clean
install without the firewall focus-steal class crashing the harness or
producing connectivity-error log channels. Every prior smoke
(boot_smoke, mod_load_smoke, mp_room_flow, mission_intro_flow,
full_sdl_pipeline_smoke) bakes `--no-net` to close the firewall prompt
window; this test deliberately omits it. The path-pinned firewall
allow rule seeded by `Install-Harness.ps1::Add-SmokeFirewallAllowRule`
(keyed on `<install>/PerfectDark.exe`) covers the new binds.

PASS 19/19 assertions in 12.5s, shared-install mode, no C/C++ source
touched.

## Deliverable

File: `tools/smoke-verify/tests/listen_host_init_smoke.json` (55 lines).
Tags: `[connectivity, net, p2p, presence, smoke, pillar:connectivity, boot]`.

Boot args:
`--no-update-check --no-sound --skip-intro`

Input sequence (pure waits + scripted exit):
- 0 ms wait (boot path: netInit + p2pInit + presenceInit all fire
  before `mainProc`; observed at t+20ms per the latest run log)
- 12000 ms scripted exit (9s headroom over the ~3s settle floor)

Assertions:
- 7 required lines (SMOKE scenario, PRESENCE x2, P2P.NAT, P2P.LAN,
  Asset Catalog, SMOKE result)
- 10 forbidden patterns (the two `BOOT: --no-net set; ...` skip lines,
  NET enet-init failure, NET enet_host_create failure, P2P.LAN bind
  failure, P2P.LAN socket failure, EXCEPTION, FATAL, timeout, LOUDFAIL)
- 2 required-count pins (1..1 each for PRESENCE bound + P2P.LAN bound,
  guarding against duplicate sockets)

## Validation

```
PASS 19/19 assertions
  Total: 1 pass, 0 fail
  log: .claude\smoke-verify-install\pd-client.log
  exit code: 0
  elapsed: 12.5s
```

All 4 connectivity init markers fire at t+20ms in the recorded log:
```
[00:00.02] P2P.LAN: listener bound on UDP 27101
[00:00.02] P2P.NAT: layer initialised (max pairs=64)
[00:00.02] PRESENCE: socket bound on UDP 27105
[00:00.02] PRESENCE: initialised state=online scheduled=1
```

Zero NET/STUN warnings or failures emitted. No firewall prompt fired
during the run (the path-pinned allow rule absorbed the new binds).

## Scope discipline

`--host` alone latches `g_NetHostLatch` but does NOT auto-call
`netStartServer` -- the actual listen-host bind requires menu nav via
`pdgui_menu_network.cpp::networkGraphStartServer`. Worse, `--host`
re-routes the log path to `pd-host.log` (per `port/src/system.c:283`),
which the smoke runner does not read (`Get-SmokeLogPath` is hardcoded
to `pd-client.log`). Two structural blockers, both deferred to a
future menu-driven smoke.

This is why the test asserts on the connectivity-stack INIT contract
(ENet init succeeded + P2P LAN tier socket bound + PRESENCE socket
bound + initialised state) and not on `NET: created server on port N`
(which would require menu input the harness does not drive on this
boot path). Naming reflects this: `listen_host_init`, not
`listen_host_full`. The deeper "host actually listening" coverage is
left for a future smoke once `mp_room_flow`'s post-Combat-Simulator
crash class is fixed and menu nav can reach the Host dialog reliably.

## Firewall check

Verified path-pinned, not port-pinned:
- `Install-Harness.ps1::Add-SmokeFirewallAllowRule` (line 293) creates
  the rule via `New-NetFirewallRule -Direction Inbound -Action Allow
  -Program $absProgram -Profile Any -Enabled True`. The `-Program`
  argument is the canonical path
  `.claude/smoke-verify-install/PerfectDark.exe`.
- Same exe path is used by every existing smoke (and reused across
  runs by the shared-install harness), so the rule covers all binds
  the test triggers (UDP 27101 LAN broadcast + UDP 27105 presence)
  without a new port-pinned rule.
- During this test run, `Add-SmokeFirewallAllowRule` emitted a
  warning ("Access is denied" - PowerShell unelevated) but the rule
  already exists from prior elevated runs, so the test proceeded
  cleanly. No mid-run firewall prompt observed.

## What this unlocks

Before this slice the connectivity pillar had zero in-client smoke
coverage. The pillar is now wired into the regression net:
- ENet stack init regressions (e.g., enet_initialize() failure on a
  future ENet rebuild) get caught.
- LAN-tier UDP 27101 bind regressions (the firewall-prompt class
  itself) get caught.
- Presence UDP 27105 bind / Ed25519 v2 init regressions get caught.
- The two `BOOT: --no-net set` skip lines are now in forbidden_patterns
  on a real test, so any silent re-introduction of `--no-net` to the
  boot path of this test fails it loudly.

## Hard-rules compliance

- No C / C++ source edits (JSON only).
- No kanban / session-log edits.
- No sub-agents.
- Worktrees disabled (main checkout, dev branch).
- Build path covered by the existing firewall allow rule; no new rules
  required.

## Files touched

- `tools/smoke-verify/tests/listen_host_init_smoke.json` (NEW, 55 lines)

## References

- `context/pillars/connectivity.md` - pillar contract.
- `port/src/main.c:289-293` - `--no-net` gate on `netInit()`.
- `port/src/main.c:974-978` - `--no-net` gate on `p2pInit()`.
- `port/src/main.c:979` - unconditional `presenceInit()`.
- `port/src/net/net.c:790` - `netInit()` definition.
- `port/src/net/p2p.c:163` - `p2pInit()` definition.
- `port/src/net/p2p_lan.c:229` - `P2P.LAN: listener bound` log line.
- `port/src/presence.c:191` - `PRESENCE: socket bound` log line.
- `port/src/presence.c:342` - `PRESENCE: initialised state=` log line.
- `tools/smoke-verify/lib/Install-Harness.ps1:293-361` -
  `Add-SmokeFirewallAllowRule` (path-pinned, idempotent).
