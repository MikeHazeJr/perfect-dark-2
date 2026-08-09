# Wave 8 `.pdeffect` fail-closed receipt

Date: 2026-08-08

Scope: Workbench `T-ASSETS-019`, `T-ASSETS-020`, and the effect-owned slice of
`T-ASSETS-033`. The receipt covers production compilation, selected/nested
failure behavior, executable profile consumers, recursive typed dependency
planning, catalog lifecycle regression coverage, archive conformance, and the
public-source guard. It is automated evidence, not an ordinary-client visual or
physical-device receipt.

## Authoritative results

- Client and updater production builds: PASS.
- `T-ASSETS-019` focused: 129 assertions / 5 cases PASS.
- `T-ASSETS-020` focused: 221 assertions / 8 cases PASS.
- Effect graph band: 571 assertions / 15 cases PASS.
- Catalog band: 3,561 assertions / 12 cases PASS.
- Complete `pd-tests`: 52,416 assertions / 932 cases PASS.
- Archive conformance self-test: PASS.
- Checked-in examples: 28 roots / 52 recursively checked archives / all 27
  public families PASS.
- `tools/asset_native_source_guard.py`: PASS.
- `git diff --check`: PASS.

Raw logs are under `.claude/session-builds/wave8final/`, including
`wave8-t019-authoritative.log`, `wave8-t020-authoritative.log`,
`wave8-effect-graph-authoritative.log`, `wave8-catalog-authoritative.log`,
`wave8-full-authoritative.log`, `wave8-conformance-selftest.log`,
`wave8-conformance-full.log`, and `wave8-native-source-guard.log`.

## Truth boundary

`T-ASSETS-020` is implemented, not validated: the selected and nested
fail-closed production path is connected and automated proof passes, while the
ordinary-client negative-path gate `V-006` remains open. Reverse invalidation
when an already-active SFX/material/texture child is disabled belongs to
`T-ASSETS-033`. Family-generic disable/re-enable and mod/full reset teardown
belongs to `T-CATALOG-004`. Generic v1 effect presentation consumers and the
remaining nested-media ceiling are separate Workbench items; this receipt does
not close them.
