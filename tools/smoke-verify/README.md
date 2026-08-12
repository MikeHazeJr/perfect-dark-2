# Smoke verify

Scripted boot / menu / input scenarios that exercise the client end-to-end and
parse stdout for required + forbidden patterns. The harness lives in
`port/src/smoke_harness.c`; the runner is `tools/smoke-verify/run.ps1`; the
tests are JSON files in `tools/smoke-verify/tests/`.

Most tests launch one client process, drive it through a scripted input
sequence at scheduled millisecond offsets, and exit when the scenario hits
its end-of-script marker or the timeout fires. A test can also declare a
`processes` array for coordinated multi-process coverage, such as the
listen-host/client loopback smoke. The runner then re-reads one log or the
concatenated process logs and checks the test's `assertions` block.

## Lifecycle

1. `run.ps1` launches `PerfectDark.exe --smoke <test.json>` with the test's
   `boot_args` appended.
2. `smokeHarnessInit()` parses the JSON, applies the requested
   `log_channel_mask` + `verbose` flag, and records the start tick.
3. `smokeHarnessTick()` is called once per frame from `mainTick`. It
   dispatches any events whose `at_ms` has come due, then runs the
   pending-release sweep for tap events.
4. On `exit` event OR timeout: the harness writes a `SMOKE: result=...` line
   to the log, flushes, and `exit()`s with code 0 (clean exit) or 1 (timeout).
5. `run.ps1` reads the log, runs the `required_lines` + `forbidden_patterns`
   + `required_counts` assertions, and reports pass / fail.

## Running a single test

```powershell
.\tools\smoke-verify\run.ps1 -Test boot_smoke
```

The runner copies the binary into a sandboxed `.claude/smoke-verify-runs/<test>/`
directory, drops a minimal install harness alongside (data/ROM placeholder),
and runs the smoke from there so the live `Build/dist/` install is not
disturbed.

## Test JSON schema

Top-level keys consumed by the harness:

| Key                  | Required | Meaning |
|----------------------|----------|---------|
| `scenario_name`      | yes      | Logged on init, used by assertions |
| `description`        | no       | Free-form note |
| `tags`               | no       | Runner-side filter (`-Tag boot`) |
| `paths_of_interest`  | no       | Reference for human reviewer |
| `log_channel_mask`   | no       | `"all"`, `"none"`, numeric mask, or named channels such as `"game,catalog,render"` |
| `verbose`            | no       | 0 / 1 |
| `timeout_seconds`    | no       | Hard ceiling; default 90 s |
| `install_state`      | no       | Runner-side hint (`"clean"`, `"upgraded"`) |
| `boot_args`          | no       | Extra argv appended to the launch command |
| `processes`          | no       | Multi-process launch definitions; each entry can set `name`, `log_file`, `boot_args`, `wait_for`, and `wait_timeout_seconds` |
| `input_sequence`     | yes      | Ordered list of events (see below) |
| `assertions`         | no       | Runner-side; not consumed by the harness |

When `processes` is present, the runner seeds one install directory, launches
each process in order, waits for any `wait_for` barrier before starting the
next process, and evaluates assertions against the concatenated logs. This is
the path used by `listen_host_peer_smoke` to prove a listen host can bind and a
client can complete the ENet auth handshake on loopback.

### Event types

All events have `at_ms` (when, milliseconds since harness init) and `type`.

#### `wait`

Inert sequencing marker. Use for comments or to drop a no-op at a specific
offset.

```json
{ "at_ms": 0, "type": "wait", "comment": "boot to title" }
```

#### `exit`

Scripted clean exit. Produces `SMOKE: result=scripted_exit code=0`.

```json
{ "at_ms": 90000, "type": "exit" }
```

#### `key`

SDL_KEYDOWN / KEYUP for a named key or numeric scancode. `action` defaults to
`tap` (one-frame auto-release).

```json
{ "at_ms": 12000, "type": "key", "key": "Return", "action": "tap" }
{ "at_ms": 12500, "type": "key", "key": "Down",   "action": "press" }
{ "at_ms": 13000, "type": "key", "key": "Down",   "action": "release" }
```

Named keys are listed in `s_KeyTable[]` in `smoke_harness.c` (Return, Escape,
Tab, Space, Backspace, arrows, shifts, ctrls, alts, F1-F12, plus single
letters A-Z and digits 0-9). For anything else, pass a numeric `scancode`
field instead of `key`.

#### `action` (c115, 2026-05-14)

Inject an action-map press/release edge directly via
`actionmapInjectStateForSmoke()`. Bypasses SDL events and ImGui's focus gate.
Deterministic and focus-independent -- the right tool when the SDL window
cannot reliably hold focus (firewall prompt, OS modal, alt-tab to a different
process).

```json
{ "at_ms": 50000, "type": "action", "name": "ACTION_MENU_ACCEPT", "action": "tap" }
{ "at_ms": 51000, "type": "action", "name": "ACTION_MENU_DOWN",   "action": "press" }
{ "at_ms": 51500, "type": "action", "name": "ACTION_MENU_DOWN",   "action": "release" }
```

`name` accepts the full `ACTION_*` enum identifier (preferred for clarity) or
the CamelCase short form used in `pd.ini` keys (`"Use"`, `"MenuUp"`).
Backward-compat aliases resolve to their canonical targets:
`ACTION_INTERACT` / `ACTION_MENU_ACCEPT` -> `ACTION_USE`,
`ACTION_MENU_CANCEL` -> `ACTION_CANCEL_USE`,
`ACTION_MENU_CONTEXT` -> `ACTION_MENU_SECONDARY`,
`ACTION_MENU_SOCIAL` -> `ACTION_MENU_TERTIARY`.

Always targets player 0. Inject-state is gated to harness-active only; calls
made outside `--smoke` runs are inert.

#### `mouse` (c115, 2026-05-14)

Synthesise an `SDL_MOUSEBUTTONDOWN` / `UP` pair at `{x, y}` for the named
button. Needed for genuinely-mouse-only UIs (Settings -> Debug -> Test
Scenarios radios, modding hub file pickers, skin editor canvas).

```json
{ "at_ms": 60000, "type": "mouse", "x": 640, "y": 400, "button": "left",   "action": "click" }
{ "at_ms": 61000, "type": "mouse", "x": 640, "y": 400, "button": "left",   "action": "press" }
{ "at_ms": 61500, "type": "mouse", "x": 640, "y": 400, "button": "left",   "action": "release" }
```

`button` accepts `"left"` / `"right"` / `"middle"` (plus `"x1"` / `"x2"` for
side buttons; aliases `lmb` / `rmb` / `mmb` also recognised). `action`
accepts `tap` / `click` / `press` / `release`; `tap` and `click` are aliases
and produce an auto-release one frame after the press.

Events use `SDL_PushEvent` so they flow through the same path as a real user
click -- this matters because ImGui's `IsItemHovered` / `IsItemActive` only
fire when the press / release sequence is correct.

#### `mouse_move` and `mouse_wheel` (V-004, 2026-08-08)

Move the ordinary SDL mouse cursor to an absolute client coordinate, then send
a wheel delta to the currently hovered target:

```json
{ "at_ms": 60000, "type": "mouse_move", "x": 640, "y": 400 }
{ "at_ms": 61000, "type": "mouse_wheel", "wheel_x": 0, "wheel_y": -3 }
```

Both events use `SDL_PushEvent` and the live SDL window ID. `mouse_move`
also warps the real cursor to the same client coordinate before queueing the
event, because ImGui's SDL backend polls the OS cursor during `NewFrame` and
would otherwise overwrite a synthetic hover position before the click lands.
`mouse_move` requires both `x` and `y`; `mouse_wheel` requires at least one supplied,
non-zero delta. Use an explicit move before wheel input so the hover target is
established through the same backend path as ordinary mouse use.

#### `receive_pdca_list` (V-009/B-1027, 2026-08-11)

```json
{ "at_ms": 90000, "type": "receive_pdca_list", "path": "social/test/receive-list.txt" }
```

This smoke-only event delivers each raw PDCA entry through the production
network `BEGIN`/`CHUNK`/`END` receive handlers at a deterministic live point.
Use it for lifecycle/rollback validation after an ordinary runtime owner is
already active; it is not a substitute for a real-peer validation receipt.

### Action modes

For `key`, `action`, and `mouse` events the `action` field controls edge
behaviour:

| Value      | Behaviour |
|------------|-----------|
| `tap`      | press now, auto-release one frame later (default) |
| `click`    | alias for `tap` (mouse events only) |
| `press`    | press edge, no auto-release |
| `release`  | release edge |

## Adding a new test

1. Drop a new `tests/<name>.json` file. Start with the boot_smoke schema as
   a baseline.
2. List paths your test exercises in `paths_of_interest` (runner-side hint;
   helps reviewers correlate test failures with code changes).
3. Author the `input_sequence` -- prefer `action` over `key` whenever the
   action-map binding is stable; `action` is focus-independent and harder to
   accidentally desync. Reach for `key` only when you need to drive a raw
   SDL handler (chord keys, raw text input).
4. Write the `assertions` block. `required_lines` are regex patterns that
   MUST appear in the log; `forbidden_patterns` MUST NOT appear;
   `required_counts` constrain how often a pattern occurs.

The runner streams the log file in real time and validates after exit; there
is no in-engine assertion path. Keep assertions log-pattern based.

## Install modes (c115, 2026-05-14)

The runner supports two install layouts. `shared` is the default.

### `-SharedInstall` (default ON)

Re-seeds a single canonical install directory at
`.claude/smoke-verify-install/` before each test. Binary, ROM, and (when
`install_state` is `prefilled`) `data/<romid>/` are refreshed in place;
`pd-client.log` is wiped so each test sees a clean log.

Why: Windows Defender Firewall keys its inbound allow rules by absolute
program path. The pre-c115 per-test layout copied `PerfectDark.exe` to a
fresh `<utc>-<test>` directory every run, which made Defender treat each
launch as a new executable and surface a "Windows Security Alert" dialog
that stole focus from the SDL window. Shared mode eliminates that class
of failure because the same path is launched every time.

On the first run the harness adds an idempotent
`New-NetFirewallRule -DisplayName "PD2 Smoke Verify" -Direction Inbound
-Action Allow -Program <canonical-path>` entry. The rule survives
reboots; first-run elevation is the only UAC prompt the user ever sees.
Subsequent runs verify the rule still points at the canonical path and
re-apply it if drift occurred. If elevation fails (non-admin shell) the
harness logs a warning and proceeds -- worker alpha's `--no-net` boot
arg closes the prompt class for offline smokes.

Per-test artefacts (results JSON, retained install dir on failure) still
land in `.claude/smoke-verify-runs/<utc>-<test>/` so debugging trails
remain isolated per run.

### `-PerTestInstall` (legacy)

Forces the pre-c115 layout: a fresh
`.claude/smoke-verify-runs/<utc>-<test>/PerfectDark.exe` per test.
Useful only when you genuinely need two concurrent runs against
different binaries, since every fresh path retrigger the firewall
prompt. Pair with `--no-net` (added by worker alpha in c115) to keep the
network stack from initialising.

### `-Install <dir>`

Use an existing install dir as-is. Implies `-Keep` and disables both
shared and per-test modes. The runner will not refresh the binary or
ROM; the dir is expected to be ready-to-launch.

## Firewall allow rule

Created automatically by `Add-SmokeFirewallAllowRule` in
`lib/Install-Harness.ps1` whenever shared mode is active. To inspect or
remove the rule manually:

```powershell
# Show
Get-NetFirewallRule -DisplayName "PD2 Smoke Verify"
Get-NetFirewallRule -DisplayName "PD2 Smoke Verify" | Get-NetFirewallApplicationFilter

# Remove (e.g. for a smoke install path that no longer exists)
Remove-NetFirewallRule -DisplayName "PD2 Smoke Verify"
```

The next shared-mode run will recreate the rule against the current
canonical path.
