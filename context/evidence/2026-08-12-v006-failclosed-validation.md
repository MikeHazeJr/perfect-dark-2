# V-006 fail-closed validation

Date: 2026-08-12

V-006 now has production-linked negative proof for all three parts of its
acceptance contract: extraction, selected public-source loading, and persisted
setup load/save failure.

## Production receipts

- Extraction aggregation: a clean cold extraction exits `0`; a forced
  `.pdtheme` emitter failure exits `2`, reports the failed family and envelope,
  skips the runtime cache build, and stops boot. Detail and raw-log pointers are
  in `context/evidence/2026-08-12-v006-extractor-failclosed.md`.
- Selected public source: the exact isolated client rejects corrupt public
  texture, vector-font, animation, SFX, WAV voice, MP3 voice, and music source
  through their real consumers with no ROM, native-bank, loose-file, or cached
  fallback. The seven-process result passes 47/47 at
  `.claude/smoke-verify-runs/results-20260812T103913Z.json`.
- Persistence: the exact isolated client invokes the production JSON and binary
  MP setup APIs. It rejects malformed JSON, a semantic missing-weapon JSON
  document, truncated binary data, and a future binary version without changing
  live setup state. It also injects failure after complete candidate writes but
  before replacement for both JSON and binary saves; the prior destination
  bytes remain exact. The six runtime cases and scenario assertions pass 17/17
  with exit `0` at
  `context/evidence/2026-08-12-v006-save-failclosed-result.json`.

## Automated receipt

- isolated client/updater build: PASS;
- isolated `pd-tests` build: PASS;
- focused V-006/B-1046, B-964, B-965, and smoke-tooling bands: 7 cases / 84
  assertions PASS;
- complete suite: 1,026 cases / 56,544 assertions PASS;
- native public-source guard: PASS;
- archive scanner selftest: 16 parity cases, recursion, and 9 structured
  contracts PASS;
- generated examples: 28 roots / 52 recursive archives / all 27 families PASS;
- scoped diff check: PASS.

The production/test/tool fingerprint before the broad receipt and after all
tests was identical:
`c09c897808af837dd9db7d1c77c6d1a196e54340178a1146546ac9581fc8a385`.

## Root corrections

- B-1045 validates an exact catalog font before adding it to the ImGui atlas,
  so malformed vector-font bytes cannot become a live UI face.
- B-1046 gives every PC JSON save writer and binary MP setup writer a shared
  sibling-candidate, checked durable flush, atomic replace, and cleanup
  transaction. MP setup loads validate into restorable or temporary state and
  publish only after the complete document succeeds, including restoration of
  the local player configuration mutated by match initialization.

This validates negative-path behavior. It does not replace positive creator
round-trip, visual, audible, controller, or real-peer validation owned by their
separate Workbench items.
