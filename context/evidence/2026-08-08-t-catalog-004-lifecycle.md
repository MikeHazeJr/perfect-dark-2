# T-CATALOG-004 all-family lifecycle receipt

Date: 2026-08-08
Session: `luna-catalog-lifecycle-20260808`
Source base: `5adcffbe`
Status: implemented, not live-validated

## Production connection

- `assetCatalogSetEnabled` snapshots identity/type/enabled state, preflights
  the complete typed dependency closure before publishing disable, retires
  outside the catalog mutex, and restores the enabled state on an unexpected
  teardown failure.
- `assetCatalogClearMods` snapshots every non-bundled registered family,
  preflights every root before the first mutation, builds a growable
  dependency-aware parent-first order, retires each payload/runtime adapter,
  then removes mod edges and identities. Bundled rows, edges, and adapters are
  preserved.
- `assetCatalogClear` and catalog reinitialization use the same transaction for
  bundled and non-bundled rows, then clear the asset-runtime, weapon-runtime,
  effect-runtime, dependency, and private-slot state before row memory is
  cleared or reused.
- Typed unload covers provider bytes, generated modeldefs, collision meshes,
  compiled animation clips, language/runtime metadata, weapon/effect owners,
  and the generic public-source runtime adapter. Interrupted legacy activation
  also detaches adapters even when its payload marker/refcount is absent.
- Mod selection rebuild now reconstructs runtime-to-catalog-ID and body/head
  caches after re-registration. This removes pointers to deleted/reused rows.
- Activation now snapshots prior residence and force-rolls back a newly
  activated bundled payload if a later dependency fails. Existing bundled and
  overlapping non-bundled owners remain balanced.

## Behavioral and build evidence

- Isolated session: `.claude/session-builds/tcat004/`
- Client compile: pass.
- Updater compile: pass.
- Test runner compile: pass.
- Focused `[t-catalog-004]`: 198 assertions in 3 cases, pass.
- Full suite: 52,718 assertions in 943 cases, pass.
- All-family reset planner covers every `asset_type_e`, a 130-row chain,
  shared-child/diamond ordering, unique rows, and cycle rejection before any
  output order is published.
- Native public-source guard: pass.
- Diff check: pass.
- Pre/post source fingerprint: exact match after excluding the fingerprint
  receipt file itself.

Root's final combined Wave 9 rerun after all sibling lanes froze also passes:
client/updater/tests builds, `[t-catalog-004]` 198 assertions / 3 cases, the
complete 52,721-assertion / 943-case suite, all-27-family conformance,
native-source guard, and diff check. Raw logs are under
`.claude/session-builds/wave9final/`.

## Truth boundary and residual validation

No ordinary installed-client mod-toggle/restart fixture was run. The receipt
does not prove physical runtime teardown for one live fixture of each of the 31
registered types, nor concurrent editor/network/screen/stage owner overlap in a
shipping game process. Those are validation gates, not substituted by the
source-frozen compile, pure reset-plan behavior, or static production-path
assertions. T-ASSETS-033 still owns reverse invalidation/reload when disabling
a dependency child must immediately retire an already-active parent.
