# T-CATALOG-002 stage-owner lifecycle evidence — 2026-08-08

## Production correction

Stage-category ownership is a separate 0/1 ledger on each non-bundled catalog
row. `catalogLoadStageAsset` and `catalogReleaseStageAsset` compose that ledger
over the normal typed lifecycle, while `catalogComputeStageDiff` enumerates
only rows actually owned by the stage path. Aggregate loaded state is no
longer treated as proof that a stage owns an explicit theme, menu, editor,
screen, manifest, network, or other subsystem reference.

All audited lazy stage-lifetime typed consumers were moved to the stage API.
The propagation pass also corrected weapon dependency release: because every
parent load/retain increments its dependency closure, every parent decrement
now decrements that closure rather than waiting for the parent to reach zero.

B-1001 connected `pdguiThemeLoaderShutdown` to the real backend shutdown path
before theme/backend teardown. The active transaction can therefore release
its parent plus UI/font/SFX/music dependencies exactly once on normal exit.

## Source-frozen automated evidence

- Isolated client and updater build: PASS.
- Isolated test build: PASS.
- `[T-CATALOG-002]`: PASS, 5 cases / 74 assertions.
- Full `pd-tests`: PASS, 901 cases / 48,165 assertions.
- `asset_native_source_guard.py`: PASS.
- Source fingerprint: `.claude/session-builds/tcat002/tcat002-source-freeze.sha256`.
- Build/test logs: `.claude/session-builds/tcat002/`.

## Live receipt

The prior source-frozen theme smoke proves repeated same-ID apply is balanced:
the parent plus four dependencies each log `ref=2->1 (retained)`. It did not
perform a post-activation stage transition, and it exposed the missing
production shutdown call fixed as B-1001.

`tools/smoke-verify/tests/theme_stage_ownership_smoke.json` activates
`example:tri_theme`, queues the real
`mainChangeToStage` path from CI to credits, and requires exactly five
post-transition rows at `ref=1 stage_ref=0`. Production shutdown emits exactly
five shutdown-specific `ref=1 stage_ref=0` pre-release markers and normal
scripted exit logs the five named `ref=1->0 (freed)` releases. The final
source-frozen receipt passes 33/33 at
`.claude/smoke-verify-runs/results-20260808T223427Z.json`; the exact trace is in
`.claude/smoke-verify-install/logs/game client/pd-client.log` lines 7595-7599
and 7627-7636. T-CATALOG-002 is therefore `implemented`. Broader typed-family
replacement, editor, and network-owner stress remains before `validated`.

The final result and client trace are tracked durably under
`context/evidence/2026-08-08-catalog-stage-ownership/`.
