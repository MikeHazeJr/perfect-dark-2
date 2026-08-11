# V-009 Needler edited-effect A/B receipt — 2026-08-11

## Scope and truth boundary

This receipt proves that changing an editable color value in the public nested
Needler `.pdeffect` changes the production value consumed by the real world
renderer. It does not claim that the retained screenshots visually distinguish
the two colors: all six frames are washed out. V-009 therefore remains partial
for a readable visual comparison, live replacement/rollback, a second active
owner acquired and released in one process, and real-peer transport.

No validation-only effect trigger was added. Both variants run the existing
`needler_graph_runtime_visual_smoke` flow: pack a real `.pdmod`, enter Combat
Simulator, select the authored secondary function, fire the normal projectile,
hit world geometry, commit gameplay/audio/presentation, render, and exit.

## Editable variants

`tools/build_needler_mod.py` keeps its checked-in default output byte-identical
and now accepts temporary output/tint arguments. Preflight regenerated the
default and matched the tracked `needler.pdweapon` SHA-256 exactly. Strict
recursive conformance passed both temporary closures as 1 root / 9 archives
across `.pdweapon`, `.pdprojectile`, `.pdmesh`, `.pdtexture`, `.pdeffect`, and
`.pdsfx`.

- baseline explosion tint: `[1.0, 0.4, 0.8, 1.0]`
- cyan explosion tint: `[0.0, 1.0, 1.0, 1.0]`

The opt-in audit in the production GBI renderer reports source provenance,
prepared command color, and the final material-color input immediately before
drawing. It observes the real path and does not mutate the effect.

## Authoritative installed-client results

Baseline result:
`context/evidence/v009-needler-ab-baseline-result-20260811T124400Z.json`

- PASS 46/46, exit 0, 115.004 s
- gameplay: explosion 1, spark 1, catalog audio 1
- presentation: two consumer lanes, four committed snapshots
- renderer: authored, effective, and render RGBA all
  `1.000,0.400,0.800,1.000`

Cyan result:
`context/evidence/v009-needler-ab-cyan-result-20260811T124601Z.json`

- PASS 46/46, exit 0, 115.002 s
- the same gameplay/audio/presentation path and counts
- renderer: authored, effective, and render RGBA all
  `0.000,1.000,1.000,1.000`

The baseline and cyan clients ran as separate smoke-runner invocations so the
shared install was fully released before reseeding. An earlier combined-run
attempt is rejected: baseline passed 46/46, but Windows still held the exited
binary when the runner immediately tried to copy the cyan seed. The still
earlier attempt is also rejected because BOM-bearing generated JSON was refused
before gameplay. Neither rejected attempt contributes to the final claim.

## Automated receipt

- isolated `v009ab` client, updater, and `pd-tests` builds: PASS
- `[needler_ab]`: 24 assertions / 1 case PASS
- complete `pd-tests`: 54,876 assertions / 987 cases PASS
- `asset_native_source_guard.py`: PASS
- conformance selftest: 16 parity + recursion + 9 structured contracts PASS
- checked-in Needler recursive conformance: 1 root / 9 archives PASS
- `git diff --check`: PASS
- exact final source/test fingerprint:
  `94a0167de847d7e43b43cbde907acd27e489b8829eabc41b3951d677fd109a2e`

Durable focused/full/guard/conformance logs are adjacent to this document.
The retained frames are under:

- `.claude/smoke-verify-runs/screenshots/20260811T084202-baseline/`
- `.claude/smoke-verify-runs/screenshots/20260811T084402-cyan/`

Visual inspection found the live HUD/source-model audit in both sets, but the
world view is overexposed and the transient effect color is not readable.
