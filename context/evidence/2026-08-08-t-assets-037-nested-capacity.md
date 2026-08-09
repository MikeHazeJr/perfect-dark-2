# T-ASSETS-037 growable nested-weapon ingress receipt

Date: 2026-08-08
Owner: `luna-nested-capacity-20260808`
Bug: B-1010

## Production boundary

This receipt covers `.pdweapon` nested UI/audio/animation/effect discovery,
complete preflight, catalog-row publication, direct dependency edges,
effect-owned dependency edges, and transactional rollback. It does not claim
that more than 64 custom SFX have private runtime playback slots; that existing
B-992/T-ASSETS-022 limit remains open.

## Implementation

- `port/src/assetcatalog_scanner.c` replaces both fixed 64-row discovery and
  preflight tables with checked growable PC storage, reserves the complete
  direct plus effect-edge closure before publication, and reverse-rolls back
  every exact row and edge created by the transaction.
- `port/src/weapon_nested_runtime_harness.c` uses growable observation storage,
  totals the children of every direct effect row, and verifies graph-edge
  baselines after both acceptance and late-corruption rejection.
- `devtools/generate-weapon-nested-capacity-fixture.py` authors 70 editable
  INI/WAV SFX archives and two individually valid JSON effects with 35 disjoint
  typed audio dependencies each. The accepted weapon therefore has 72 direct
  nested archives and 70 aggregate effect-owned edges.
- `tools/asset_archive_conformance.py` recognizes only the production-owned
  `dependencies/assets/effects/*.pdeffect` slot and recursively validates each
  typed identity.

## Automated evidence

- Isolated client/updater build: PASS,
  `.claude/session-builds/t037fix/_build-session.out.log`.
- Isolated tests build: PASS, same log.
- Focused `[t-assets-037]`: PASS, 18 assertions / 2 cases.
- `python tools/asset_native_source_guard.py`: PASS.
- Recursive accepted-source conformance: PASS, 1 root / 73 checked archives /
  `.pdweapon`, `.pdeffect`, and `.pdsfx` families.
- Production-client smoke: PASS 7/7, harness 2/2, exact 72 registration log
  rows, clean exit, and no harness failure or timeout:
  `.claude/smoke-verify-runs/results-20260809T011935Z.json`.
- Retained production log:
  `.claude/smoke-verify-runs/20260809T011851Z-weapon_nested_capacity/logs/game client/pd-client.log`.
- Root's final combined Wave 9 fingerprint also passes client/updater/tests
  builds, `[t-assets-037]` 18/2, the complete 52,721/943 suite, conformance
  self-test, 28-root/52-recursive all-27-family conformance, native-source
  guard, and diff check. Raw logs are under
  `.claude/session-builds/wave9final/`.

The initial one-effect fixture attempt is retained at
`.claude/smoke-verify-runs/results-20260809T010700Z.json`; it truthfully failed
before scanner exercise because one 70-node graph exceeded the separate
per-graph compiler contract. A second run proved the corrected production
harness but exposed a stale 71-row smoke assertion; the final receipt above
corrected only that assertion and passed.

## Residual handoff

The final log registers all 70 SFX catalog rows and all 70 effect-owned edges,
but SFX 64 through 69 also log `CATALOG.SOUND.CUSTOM_SLOT_FAIL` because the
private custom-sound runtime pool remains fixed at 64. This is existing
B-992/T-ASSETS-022 work. T-ASSETS-037 therefore proves omission-free
discovery/preflight/publication/rollback, not more-than-64 custom-SFX playback.
