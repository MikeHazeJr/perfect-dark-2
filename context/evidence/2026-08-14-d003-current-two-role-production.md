# D-003 current-product two-role production receipt

Date: 2026-08-14

Verdict: accepted for D-003 Option A and the B-1082/B-1083/B-1084/B-1085/
B-1087/B-1088/B-1089/B-1090 friend-play regression cluster. T-ENGINE-004
remains partial for its separate Campaign/player-init, transition, reconnect,
broader rollback, strengthened listen-host, and visual-release gates.

## Frozen artifacts

- Product client SHA-256:
  `D1C9158335DA8E5ACD1C39A6ED4643E44FA97D4E6563EDE89AC3F7810EDC33D0`.
- ROM SHA-256:
  `B4173EE90BA6E3514B17D7F340C6C248E2A4C429FDC3C671DD82BBF5AFCBB152`.
- Focused-test binary SHA-256:
  `853499495859173730C54DF4D27948C37266848E912ABDEC83CAE06622478FC8`.
- Product freeze `cc8791c1...` previously passed the complete suite at 60,824
  assertions in 1,152 cases and passed `tools/asset_native_source_guard.py`.
- The final product/test/tool scope fingerprint was unchanged before and after
  the inverse-role smoke:
  `2784d121bdbdb3ee095ac7cc6b6bdba8797c8b913104401e580ecd50635966a1`.
  Receipts:
  `.claude/session-builds/v1m1engine004/b1085-scoped-anchor-source-fingerprint-pre-smoke.txt`
  and `b1085-scoped-anchor-source-fingerprint-post-smoke.txt`.

The accepted initiator role predates the final verifier-only named-anchor
change, but uses the exact same product binary. That change affected only the
inverse fixture's focus-dependent assertion scope; it did not alter product
code or the initiator fixture.

## Consolidated focused boundary

- PowerShell parsing passed for `run.ps1`, `Test-Assertions.ps1`, and the
  assertion self-test.
- The inverse fixture parsed as JSON.
- The ordered-sequence self-test passed named-anchor success, exact-line
  mismatch, missing-anchor fail-closed, unreachable-sentinel fail-closed, and
  independent whole-log behavior.
- The embedded exact-PID Win32 focus helper compiled successfully.
- `[b1085]` passed 402 assertions in 13 cases.
- Durable preflight:
  `.claude/session-builds/v1m1engine004/b1085-scoped-anchor-preflight.log`.

## Ordinary-client receipts

1. Initiator authority:
   `.claude/smoke-verify-runs/results-20260814T020223Z.json`, first result,
   accepted 214/214, exit 0, exact product client above.
2. Invitee authority:
   `.claude/smoke-verify-runs/results-20260814T030245Z.json`, accepted 218/218,
   exit 0, zero assertion failures, zero operational failures, exact product
   client above. Definition SHA-256:
   `C61C9AFDACE4E9B015F7B19603A14262EB6223922DFB26C77B472289A9F14FDD`.

The inverse authority log proves one deterministic election, one pre-connect
latch, one server-start attempt, and one ENet listen bind. Presence v5
published the separately typed signed match-server route. The non-authority log
proves one idempotent `route_kind=match-server` join attempt. Neither log
contains the retired group handoff, a probe endpoint passed to `netStartClient`,
or a relay descriptor passed to `netStartClient`.

The same run proves fresh SDL source `focus GAINED` then `focus LOST`, target
GUI-thread keyboard-focus retention, post-loss smoke owner acquisition,
`fire_held=1`, source-backed gameplay and presentation effects, and one scripted
shutdown per client. No `PerfectDark`, `PerfectDarkServer`, or `WerFault`
process remained after teardown.

Retained startup/publication/join rollback receipts remain:
`.claude/smoke-verify-runs/results-20260812T174511Z.json` and
`.claude/smoke-verify-runs/results-20260812T174751Z.json`.

## Visual boundary

This unit adds no new screenshot claim. V-009's accepted visual proof remains
the regression gate for the previously fixed white first-person obstruction,
Needler transform/scale, GLTF material-colour projection, and effect blending.
Future gameplay capture gates must keep that unobstructed-frame check. The
separate Carrington Institute white-door report remains B-1086/V-010 and is not
closed by these log receipts.
