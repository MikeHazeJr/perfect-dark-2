# Asset Roadmap Wave 3 Verification - 2026-08-08

Scope: Workbench `T-ASSETS-014`, `T-ASSETS-023`, `T-ASSETS-026`,
`T-ASSETS-031`, and `T-MODDING-007` on commit parent `71825417`.

## Source freeze and coordinated builds

- Lina voice creator source frozen at `2026-08-08T15:32:33.029-04:00`.
- Luna reticle and Ultra theme source were frozen before the accepted root
  receipts. The earlier Luna client compile was rejected after a post-start
  include fix.
- Isolated session: `.claude/session-builds/assetwave3`.
- Final client and updater build: PASS.
- Final `pd-tests` build: PASS.

## Automated results

- `[T-ASSETS-026]`: 3 cases / 19 assertions PASS.
- `[creator]`: 3 cases / 41 assertions PASS.
- `[unicode]`: 1 case / 5 assertions PASS.
- `[reticle]`: 2 cases / 36 assertions PASS.
- `[modding][pdxxx]`: 159 cases / 15,847 assertions PASS.
- Archive scanner selftest: 16 parity cases, recursion, and 8 structured
  contracts PASS.
- Strict recursive conformance: 28 root archives, 53 total archives, all 27
  typed families PASS.
- `tools/asset_native_source_guard.py`: PASS.
- `git diff --check`: PASS.

## Regression findings repaired during verification

1. The Voice creator static test included the following generic SFX/Music
   function comment in its Voice-only slice. The boundary now ends at the
   generic function documentation marker.
2. The regenerated weapon example gained a valid declared reticle, so its old
   direct runtime test had to provide the same catalog/image seam that
   production nested registration provides.
3. That seam exposed B-985: UI source paths were limited to 128 bytes and
   silently truncated a normal absolute nested archive chain. UI texture,
   layout, and nine-slice paths now use `FS_MAXPATH`.
4. Mission source-contract coverage still expected an obsolete trailing
   boolean conjunction after removal of `briefing_file`; the assertion now
   pins the three-field graph/scenario/objectives requirement.

## Truth boundary

These receipts establish production connection and automated regression
coverage. They do not replace ordinary-client edited-source, restart,
listen-host/network, MKB/controller, device-switch glyph, or rendered-output
captures. Those gates remain open in the Workbench validation program.
