# Multiplayer movement ordering — September 27

Owner: codex-385f1c85-675768a6; T-NETWORKING-011 remains partial.

## Paused checkpoint — 04:50 ET, user request

Goal paused. Both queued runtime reservations were cancelled before start.
No owned process remains running. Source changes are uncommitted; do not resume
builds, tests, runtime or implementation until Mike explicitly resumes.

Integrated asset0923lang client/tests builds pass. Focused multiplayer gates
pass 172 cases / 14,417 assertions, guard exit 0, with 3,432 source files unchanged.
Native receipt: .claude/mp0927-movement/native-result.json; JUnit: focused.xml;
source-before.json and source-after-native.json preserve the exact snapshot.
Client SHA256: 5e7ab9ab57c2491efeeabd9716084ff7a2921229f10cf732774ff68159e3bbc7.
Build receipt: .claude/session-builds/asset0923lang/model0927-build-exits.json.
These are component checks, not new runtime, visual, audio, controller or WAN proof.

On explicit resume, first recheck peer ownership/current source. Next queued
runtime set was network_coop_stage_transaction_smoke,
network_counterop_stage_transaction_smoke, and mp_content_decline. None started.

## Applied transaction repair

CLC_MOVE previously changed outmoveack before rejecting a stale input. The
SVC_PLAYER_MOVE receiver lacked that rejection entirely, allowing a stale
snapshot to replace history and reapply a local force-position snapshot.
Both receive paths now use the same production commit operation after complete
decode and their state/policy checks. Duplicate, older and ambiguous half-range
serials leave the entire client record unchanged; teleport and reconnect-fire
side effects occur only after admission. Accepted records preserve the newest
acknowledgement using serial arithmetic. Pending teleport and outbound-send
checks use the same acknowledgement predicate. Explicit record/ACK validity
separates initial unknown state from a tick that wraps to zero. Client reset
already clears the entire record. The v61 wire bytes are unchanged.

Native cases exercise the actual production commit against real netclient
storage: reordered/duplicate records carrying teleport acknowledgement and
replacement state, monotonic ACKs, serial wrap and zero initialization. Existing
decoder cases still cover truncation and non-finite values. Static ingress
checks cover policy-before-commit and commit-before-teleport/fire ordering.
Builds and focused tests pass as recorded above; ordinary-client runtime for
this unit remains unrun.

## Authority work still required

This does not close F05 or establish loss/reconciliation acceptance. The current
wire carries movement speeds after local acceleration and crouch/aim modifiers;
bwalkUpdateRemote on the host still follows client-reported position. Reusing
those processed speeds as raw input would apply modifiers twice. The raw-input
capture boundary is before mode-specific application in bmoveApplyMoveData;
walking, grabbing and the separate vehicle-intent path need distinct handling.

The next authority unit must transport bounded input intent, simulate host
movement without trusting client position, acknowledge consumed simulation
inputs, and retain sufficient prediction history for reconciliation. Current
two-sample interpolation is not replay history. Published-tick bounds for hostile
future ACKs, one-shot input consumption, input expiry, mode transitions and
render smoothing need explicit contracts and production evidence. Serial-safe
ingress alone does not make the remaining simulation tick consumers wrap-safe.

Previous admission runtime receipts remain red as recorded in
multiplayer-admission-2026-09-27.md: missing Needler impact-effect witnesses and
obsolete Campaign spawn assertions, with concurrent source drift. Corrected
fixtures still require a fresh integrated run after peers' source windows.
