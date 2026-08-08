# T-ASSETS-018 executable pdeffect program receipt

## Production boundary

Public `.pdeffect` activation now registers one owned executable program per
catalog ID. Legacy graph source retains compiled nodes, edges, authored
parameters, shared contexts, subgraphs, exports, and a stable topological
execution order. Optional `timeline.json` retains authored key order and
linearly interpolates numeric properties. Validated v2 profile libraries decode
all stored rows, permanent IDs, smoke links, colors, and explicit catalog-audio
string/null state. Graph-only, timeline-only, and combined archives use the same
transactional replacement boundary.

Runtime effect records, custom spark rows, and embedded effect discovery use
growable PC registries. The retired limits were 64 records, 16 spark rows, and
eight embedded effects.

This receipt does not claim T-ASSETS-019 renderer/audio/gameplay consumers or
T-ASSETS-020 selected-source fallback semantics.

## Source-frozen verification

- Client and updater compile: PASS in isolated session `lunaeffect18`.
- Test binary compile: PASS in isolated session `lunaeffect18`.
- `[t-assets-018]`: PASS, 5 cases and 251 assertions.
- `[effect_graph]`: PASS, 13 cases and 517 assertions, including legacy paths.
- Native-source guard: PASS.
- Archive scanner selftest: PASS, 16 parity cases, recursion, and eight
  structured-source contracts.
- Strict example conformance: PASS, 28 root and 52 recursively checked archives
  across all 27 families.
- Complete suite: one unrelated in-flight T-CATALOG-003 assertion failed at
  `tests/test_asset_path_contract.cpp:124`; no T-ASSETS-018 case failed. The
  path lane owns the combined rerun, so this item is implemented, not validated.

## Behavioral coverage

- Complete graph topology, context, subgraph, export, arbitrary policy and
  dependency parameter retention.
- Stable executable order and visitor dispatch.
- Timeline authored order, clamp behavior, and interpolation.
- Graph-only, timeline-only, and combined archive activation.
- Exact validated v2 explosion-profile row retention.
- Ninety-six simultaneously registered effects.
- Forty-eight simultaneously registered custom spark rows.
- Twelve effects embedded inside one nested projectile archive.

## Remaining work

- T-ASSETS-019 connects program events/profile rows to production gameplay,
  renderer, audio, material, texture, light, camera, decal, beam, and screen
  consumers.
- T-ASSETS-020 makes selected and nested source failures fail closed without
  native fallback and completes negative/capacity receipts.
