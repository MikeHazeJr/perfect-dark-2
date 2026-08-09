# Wave8 `.pdeffect` consumer and lifecycle receipt — 2026-08-08

Authoritative isolated build/test session: `.claude/session-builds/wave8final/`.
The source and tests were frozen for the final receipt; no overlapping edits
were accepted during the coordinated build and test runs.

## Passing evidence

- Client, updater, and `pd-tests` builds: PASS.
- `[t-assets-019]`: 129 assertions across 5 cases PASS.
- `[t-assets-020]`: 221 assertions across 8 cases PASS.
- `[effect_graph]`: 571 assertions across 15 cases PASS.
- `[T-CATALOG-003]`: 3,561 assertions across 12 cases PASS.
- Full suite: 52,416 assertions across 932 cases PASS.
- Archive conformance self-test: PASS.
- Archive conformance: 28 root archives, 52 recursively checked archives,
  all 27 public `.pdxxx` families PASS.
- `tools/asset_native_source_guard.py`: PASS.
- `git diff --check`: PASS.

## Durable artifacts

- `.claude/session-builds/wave8final/wave8-t019-authoritative.log`
- `.claude/session-builds/wave8final/wave8-t020-authoritative.log`
- `.claude/session-builds/wave8final/wave8-effect-graph-authoritative.log`
- `.claude/session-builds/wave8final/wave8-catalog-authoritative.log`
- `.claude/session-builds/wave8final/wave8-full-authoritative.log`
- `.claude/session-builds/wave8final/wave8-conformance-selftest.log`
- `.claude/session-builds/wave8final/wave8-conformance-full.log`
- `.claude/session-builds/wave8final/wave8-native-source-guard.log`
- `.claude/session-builds/wave8final/_build-session.out.log`

## Truth boundary

This receipt proves the v2 public profile adapters, strict selected-source
failure, typed dependency collection/activation/rollback, and catalog
transport bands. `T-ASSETS-019` remains `partial`: generic v1 graph scheduling,
gameplay/audio dispatch, renderer/presentation dispatch, timeline/context/
target/lifetime semantics, the 64-entry nested-media ingress ceiling, and
edited-source ordinary-game evidence remain in `T-ASSETS-032`,
`T-ASSETS-034`, `T-ASSETS-035`, `T-ASSETS-036`, `T-ASSETS-037`, and `V-009`.
