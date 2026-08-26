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
| `processes`          | no       | Multi-process launch definitions; each entry can set `name`, `log_file`, `smoke_path`, `snapshot_log_file`, `snapshot_exit_timeout_seconds`, `expected_exit_code`, `boot_args`, `wait_for`, and `wait_timeout_seconds` |
| `window_focus_transition` | no | Focus-sensitive multi-process gate with `from_process`, `to_process`, `source_wait_for`, `wait_for`, `timeout_seconds`, and `settle_ms` |
| `retain_artifacts`   | no       | Required install-relative files copied to the durable run artifacts directory before cleanup; entries use `src` and optional leaf `name` |
| `input_sequence`     | yes      | Ordered list of events (see below) |
| `assertions`         | no       | Runner-side; not consumed by the harness |

When `processes` is present, the runner seeds one install directory, launches
each process in order, waits for any `wait_for` barrier before starting the
next process, and evaluates assertions against the concatenated logs. This is
the path used by `listen_host_peer_smoke` to prove a listen host can bind and a
client can complete the ENet auth handshake on loopback. `smoke_path` selects a
different input script for one process while preserving the parent test's shared
install and aggregate assertions. `snapshot_log_file` waits for that process to
exit and copies its log before the next sequential restart overwrites the normal
client log. The runner removes the ordinary log after each snapshot and before
the next launch, so a restart cannot satisfy its barrier with a stale marker.
Its post-marker clean-exit wait defaults to 30 seconds and can be
raised per process with `snapshot_exit_timeout_seconds`.

`window_focus_transition` is fail-closed. The runner enumerates visible,
unowned top-level windows by exact process ID and requires exactly one candidate
for each named process. It then uses checked Win32 foreground/active/focus calls,
requires a newly appended source `source_wait_for` witness (normally
`INPUTCTX: focus GAINED`), focuses the target, and requires a newly appended
source `wait_for` witness (normally `INPUTCTX: focus LOST`). The target must
still own GUI-thread active keyboard focus at the lost witness. Operational
failures are stored separately in the result JSON with their phase and native
focus diagnostics. Focus-dependent `required_sequences` declare
`"anchor": "window_focus_transition"`; the runner supplies the exact fresh
loss-witness line only to those sequences. Untagged startup/gameplay sequences
still inspect the complete log. A missing named anchor or an operational focus
failure rejects the tagged sequence, so old log lines are never credited as
causal evidence without discarding valid pre-transition evidence.

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

#### `unclean_exit`

Flushes `SMOKE: result=unclean_exit` and terminates with `_Exit(0)`, deliberately
skipping the normal `atexit` cleanup path. Use only for restart recovery tests
that must leave durable dirty-session state behind.

```json
{ "at_ms": 70000, "type": "unclean_exit" }
```

#### `catalog_recovery_probe`

Loads a catalog ID through the production typed lifecycle and logs whether its
catalog row, temporary flag, FileProvider source, runtime binding, and typed
dependency edges are present. Missing IDs log zeroes for every layer.

```json
{ "at_ms": 62000, "type": "catalog_recovery_probe", "path": "example:tri_weapon" }
```

#### `wait_until` (B-1076/B-1085/B-1088/B-1095)

Pause only the current process's virtual script timeline until an exact typed
production predicate is satisfied. The real harness watchdog continues. Every
wait requires `timeout_ms`, and the fixture timeout must strictly exceed the
latest virtual event plus the sum of all wait timeouts.

```json
{ "at_ms": 0, "type": "wait_until", "condition": "network_listen_ready", "timeout_ms": 120000 }
{ "at_ms": 0, "type": "wait_until", "condition": "network_stage_live", "timeout_ms": 220000 }
{ "at_ms": 0, "type": "wait_until", "condition": "gameplay_ready", "timeout_ms": 60000, "stable_ms": 3000 }
```

Exact conditions are `network_listen_ready`, `network_stage_live`,
`network_reconnect_available`, `cutscene_skip_ready`, `gameplay_ready`,
`offline_gameplay_ready`, and `endscreen_visible`.
Use `network_listen_ready` before a host-side peer-dependent wait when the
runner withholds dependent processes until the listen socket exists; this keeps
cold startup outside the peer's causal deadline. `stable_ms` defaults to zero;
when non-zero, the condition must remain continuously true for that real-time
window and any false sample resets it. Expiration wins if readiness and the
deadline occur on the same tick.

`offline_gameplay_ready` mirrors the complete usable-gameplay predicate without
requiring a network endpoint: the current offline stage epoch, multiplayer
session, local player allocation and spawn, gameplay layer, normal updating
tick, player control, unpaused/live state, and walking mode must all agree.
`endscreen_visible` requires the production endscreen state, the authoritative
`MENU_TYPE_ENDSCREEN_MP` pool owner, and the live menu input context together.
This makes an endscreen wait an input-usable UI boundary rather than a flag-only
signal.

An assisted wait may issue one bounded production action while its target is
false:

```json
{
  "at_ms": 0,
  "type": "wait_until",
  "condition": "gameplay_ready",
  "timeout_ms": 60000,
  "stable_ms": 3000,
  "assist_action": "ACTION_SKIP_CUTSCENE",
  "assist_condition": "cutscene_skip_ready",
  "assist_hold_ms": 900
}
```

The four assist/stability fields are one complete tuple: assisted waits require
`stable_ms > 0`, a resolvable action, an exact assist condition, and a positive
hold shorter than the wait timeout. The assist presses at most once through
`actionmapInjectStateForSmoke()`, uses real time while the script is paused,
and releases on hold expiry and every success, failure, or timeout path.
Standalone `action` events are deliberately unconditional; pair explicit
press/release events, or use this owned assisted-wait form.

Successful wait receipts include `stable_elapsed_ms`. Runtime assertions for a
stable wait must bound that value at or above `stable_ms`; the configured value
alone is not transition proof. Explicit key/action/mouse presses cannot cross a
`wait_until`, because pausing virtual time would silently extend the hold in
real time. The fixture loader also validates the complete JSON document before
parsing and rejects truncated syntax, duplicate or unknown event fields, and
known fields used on the wrong event type.

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

#### Agent session actions (D-005)

The agent_activate and agent_delete events invoke the same Agent Session
boundary used by ordinary Agent Select while the smoke client remains live.
Each event requires a name and logs its result plus the active identity before
and after. Use these events for replacement rollback and active-delete policy
proof, not as a substitute for the separate ordinary Agent Select input smoke.

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
