# V-009 overlapping owner lifecycle

Date: 2026-08-11
Status: production proof passed; V-009 remains partial for readable visual and
real-peer transport proof.

## Production path

The smoke harness adds two validation-only events that invoke the real typed
weapon lifecycle:

- `catalog_weapon_acquire` calls
  `catalogLoadTypedAsset(ASSET_WEAPON, catalog_id)`.
- `catalog_weapon_release` calls
  `catalogReleaseTypedAsset(ASSET_WEAPON, catalog_id)`.

The scenario does not replace owner accounting. It opens the real Mod Manager,
acquires and releases a second Needler owner, then sends real SDL mouse input to
disable the original Mod Manager owner and apply the change.

## Installed-client receipt

`.claude/smoke-verify-runs/results-20260812T022302Z.json` passes 40/40 and exits
0. The final client log proves:

- explicit acquire: `mod_needler:needler` reaches `ref=2`;
- explicit release: weapon, `pink_burst_effect`, and `pink_burst_sfx` each
  retain `ref=2->1`;
- real Mod Manager Apply: all three unload `ref=1->0 (freed)`;
- no crash, native/RomProvider fallback, `[imgui-error]`, extra
  `PopStyleColor()`, or missing `PopStyleColor()` diagnostic occurs.

Rejected receipts remain evidence rather than being hidden:

- `results-20260812T015805Z.json`: events ran before catalog readiness;
- `results-20260812T020357Z.json`: an invalid generic release stole the live
  match owner and crashed the held weapon;
- `results-20260812T021256Z.json`: menu input preceded the cold catalog-ready
  point;
- `results-20260812T021626Z.json`: owner accounting passed 37/37 but exposed
  B-1030's ImGui style-stack imbalance.

## B-1030

`renderModManagerBody` previously re-read mutable `s_ApplyFlowState` when
deciding whether to pop a style color. The `2->3` transition popped without a
push, while the `3->0` transition pushed without a pop. It now snapshots
`pushedApplySuccessBg` before rendering and uses that exact value for both ends
of the scope. A production propagation search found no sibling mutable-condition
style scope.

## Automated verification

- isolated client/updater/tests builds: PASS;
- `[v009][owner]`: 15 assertions / 1 case PASS;
- `[b1030]`: 5 assertions / 1 case PASS;
- complete `pd-tests`: 54,992 assertions / 998 cases PASS;
- native-source guard: PASS;
- archive scanner selftest: 16 parity cases + recursion + 9 structured source
  contracts PASS;
- checked-in examples: 28 root / 52 recursive archives / all 27 families PASS;
- `git diff --check`: PASS.

Source/test/scenario/bug fingerprint:
`95ac4bc2348b887bedffd750eb796b5df808b899aade47c4f26adf24d2975c00`.

The remaining V-009 gates are a readable pixel-level visual comparison and a
real-peer network-distribution receipt. Neither is inferred from this owner
lifecycle result.
