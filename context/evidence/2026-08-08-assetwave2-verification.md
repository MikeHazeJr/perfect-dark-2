# Asset roadmap wave 2 verification receipt

Date: 2026-08-08

Scope: Workbench `T-ASSETS-013`, `T-ASSETS-011`, `T-ASSETS-021`, and the
partial `T-ASSETS-022` implementation milestone.

## Source-freeze rule

The first combined build receipt was rejected after shared scanner/test files
changed during the run. The lanes then froze source and the isolated
`assetwave2` client, updater, and test builds were rerun. No lane source edits
landed after the accepted rerun began. This receipt records only that accepted
source-frozen run.

## Passing results

- Isolated game client build: PASS (34 seconds).
- Isolated updater build: PASS (2 seconds).
- Final isolated `pd-tests` compilation: PASS (29 seconds).
- `[prop_graph]`: 2 cases, 16 assertions, PASS.
- `[voice]`: 6 cases, 53 assertions, PASS.
- `[T-ASSETS-022]`: 1 case, 10 assertions, PASS.
- Full `[modding][pdxxx]`: 150 cases, 15,736 assertions, PASS.
- Archive conformance selftest: 16 parity cases, recursive checks, and 8
  structured source contracts, PASS.
- Typed examples: 28 root archives, 52 recursive archives, all 27 families,
  PASS.
- Audio source verification: 3 archives and 4 localized WAV members, PASS.
- `tools/asset_native_source_guard.py`: PASS.
- Python source compile and `git diff --check`: PASS.

## Truth boundary

These results support `implemented` for production-connected prop and voice
paths, not `validated`; ordinary-client edited-source, restart/network, and
negative-path receipts remain. `T-ASSETS-022` remains `partial` because the
current test binary does not link a complete native catalog/scanner/loader
lifecycle harness and no ordinary-client nested-media receipt exists yet.

Transient raw logs were produced under `.claude/session-builds/assetwave2` and
`.claude/session-builds/voice-localization-20260808`; this tracked receipt is
the durable summary after applying the source-overlap rejection rule.
