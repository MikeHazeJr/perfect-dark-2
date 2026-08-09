# T-ASSETS-033 reverse typed dependency invalidation

Date: 2026-08-08

## Production result

- Every successful typed root load or retain records exact root identity,
  type, reference count, bundled state, and stage ownership in growable PC
  storage. Release, disable, mod reset, and full reset update the same ledger.
- Child disable or replacement preflights every active root's complete typed
  closure before the first mutation. A missing row, wrong type, or cycle leaves
  runtime truth unchanged.
- Affected overlapping and diamond roots retire by exact root reference count.
  Effect executor/runtime adapters are detached before the child becomes
  unreachable. No ownership is inferred from aggregate child refcounts.
- Invalid roots remain pending and unreachable. Re-enable and completed source
  replacement use the ordinary dependency-first activation transaction; a
  failed reload rolls back its committed prefix and remains pending.
- Successful `.pdeffect` replacement prunes typed owner edges to the exact new
  public dependency set. A rejected replacement restores the prior catalog row
  and edges, then reloads only parents that pass the restored closure.
- Mod reset removes mod roots while preserving bundled pending owners for a
  later rescan. Full reset clears active and pending ledger truth.

## Ingress audit

- Base metadata walking registers bundled typed effect edges before base effect
  activation.
- Loose and local typed archives use the strict scanner transaction.
- Nested `.pdmod` and `.pdweapon` effects use the same scanner plus hoisted
  typed parent edges.
- Received network archives publish transactionally and hot-scan through the
  same strict typed descriptor path. No separate network dependency parser or
  runtime representation was introduced.

## Automated evidence

- Isolated session `.claude/session-builds/tassets033/`:
  - client compile: pass
  - updater compile: pass
  - tests compile: pass
- Focused selector `[t-assets-033]`: 42 assertions in 4 cases, pass.
- `python tools/asset_native_source_guard.py`: pass.
- `git diff --check`: pass at handoff.

The first combined installed-client ingress run
`.claude/smoke-verify-runs/results-20260809T020251Z.json` exposed a real
startup regression before its intended probes: the stock huge explosion uses
a configured native sound token whose public sample is MP3/voice-backed.
Complete T033 closure activation correctly loaded that dependency, but the
effect executor rejected every `AUDIO_CAT_VOICE` row without distinguishing a
packed `hasconfig` playback token from an ordinary voice line. The correction
accepts only that configured native token; unconfigured voice and all music
remain rejected. Isolated session `.claude/session-builds/tassets033fix/`
passes client, updater, and tests compilation, and focused selectors
`[t-assets-033],[effect_executor]` pass 123 assertions in 6 cases. The
native-source guard and focused diff check pass after the correction.

The source-frozen authoritative installed-client rerun then passes 24/24 at
`.claude/smoke-verify-runs/results-20260809T021712Z.json`. The same combined
receipt passes focused T-CATALOG-003 at 3,576 assertions/13 cases, the complete
suite at 53,264 assertions/949 cases, all-family archive conformance, and the
native-source guard. This closes the configured-alias startup regression and
combined automated-suite gate; it does not simulate a live child
disable/re-enable or replacement transaction.

The focused cases cover repeated and overlapping roots, a shared diamond,
direct child ownership exclusion, stage-owner restoration, cycle/type
preflight with zero retirement, failed replacement reload remaining pending,
mod/full ledger clearing, and exact stale-edge pruning.

## Truthful residual gates

- No ordinary installed-client fixture disabled/re-enabled or replaced a live
  audio/material/texture/effect child while gameplay held its parent.
- No real network peer replacement was run. Network ingress shares the scanner
  path, but runtime replacement parity still needs an installed-peer receipt.
- The failed combined receipt remains preserved beside the successful 24/24
  rerun so the regression and correction are both auditable.
- Concurrent editor, stage, manifest, and network owners still require live
  stress before validation.

Therefore T-ASSETS-033 is implemented, not validated.

## Root combined Wave 10 receipt

The final frozen aggregate run compiled the client, updater, and tests, then
passed focused T033 42/4, the complete 53,264-assertion/949-case suite,
28-root/52-recursive all-27-family conformance, the native-source guard, and
diff check. Logs are retained under `.claude/session-builds/wave10final/`.
