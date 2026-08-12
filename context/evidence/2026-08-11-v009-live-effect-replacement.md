# V-009 Live Effect Replacement and Rollback

Date: 2026-08-11

## Scope

This receipt closes the same-ID live `.pdeffect` replacement and failed-candidate
rollback gate for V-009. It exercises the installed-client network distribution
path, catalog replacement invalidation, typed dependency ownership, public
effect activation, weapon-selected effect resolution, presentation rendering,
and rollback to the last accepted replacement in one ordinary client process.

It does not close V-009's remaining readable pixel-level visual comparison,
second overlapping active-owner reacquisition/release, or real-peer transport
gates.

## Production result

The retained scenario is
`tools/smoke-verify/tests/needler_effect_replacement_rollback_smoke.json`.
Its result is
`.claude/smoke-verify-runs/results-20260811T143756Z.json`.

The client passed 29/29 assertions, emitted 11/11 requested input/receive
events, exited 0, and shut down cleanly. The same process proved:

- baseline Needler presentation reached the renderer twice as authored pink
  `1.000,0.400,0.800,1.000`;
- a valid same-ID received replacement reached the renderer twice as authored
  cyan `0.000,1.000,1.000,1.000`;
- a later invalid same-ID candidate was rejected by the typed scanner with
  result `-1`;
- the published PDCA transaction rolled the filesystem back, left no active
  transaction, and deferred root reload until the accepted bytes were restored;
- the restored cyan source reactivated and reached the renderer twice after
  rollback;
- the rejected `mod_needler:missing_replacement_sfx` dependency was never
  resolved or published.

## Bugs closed

- B-1027: replacement invalidation excluded the replaced root and could leave
  stale runtime state across rejection. Replacement now retires the selected
  root and its reverse dependents, restores the retired snapshot on failure,
  and reloads only after filesystem rollback.
- B-1028: received manifest components were incorrectly looked up as asset
  catalog IDs. Component identity now resolves through the mod registry.
- B-1029: the smoke wrapper could discard a valid scenario result when helper
  output preceded it. It now selects exactly one structured result object.

## Automated receipt

The final frozen fingerprint was
`3e57c9c1bb3b14d5faa9b11e00223074e4c928026c98e9032a95454f0e44a7b6`.

- client, updater, and test targets: PASS;
- `[b1027]`: 77 assertions / 6 cases PASS;
- `[b1029]`: 6 assertions / 1 case PASS;
- `[modding][pdxxx]`: 18,614 assertions / 241 cases PASS;
- complete `pd-tests`: 54,972 assertions / 996 cases PASS;
- native public-source guard: PASS;
- conformance selftest: 16 parity cases, recursion, and 9 source contracts PASS;
- all-family conformance: 28 roots / 52 recursive archives / all 27 families PASS;
- `git diff --check`: PASS.

The complete test log is
`.claude/session-builds/v009replace/b1027-full-final.log`.

## Remaining V-009 gates

- readable pixel-level pink-versus-cyan visual comparison rather than the
  washed-out retained frames;
- a second overlapping active owner acquired and released in the same process;
- real-peer network distribution and rollback proof.
