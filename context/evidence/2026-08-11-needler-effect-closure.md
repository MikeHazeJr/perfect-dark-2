# Needler recursive effect closure verification — 2026-08-11

## Outcome

B-1025 is fixed and verified. Public effect dependencies embedded beneath a
weapon projectile or entity are catalog-visible before the weapon activates:

`needler.pdmod::needler.pdweapon::secondary.pdprojectile::pink_burst.pdeffect::pink_burst.pdsfx`

The registration boundary is transactional. It validates the public archive,
descriptor type, catalog namespace, graph-declared typed dependency, content
identity, and complete path before publishing any row. Direct and recursive
effect rows, effect-to-child edges, and weapon-to-effect/child edges roll back
in reverse on any later failure.

The first recursive run rejected an effect-local `pink_spark.pdtexture` that
the graph never referenced. The editable generator was corrected to remove
that duplicate while keeping the projectile-owned texture and effect-owned
sound; the runtime validator was not relaxed.

## Automated receipt

- Client and updater compile: PASS in
  `.claude/session-builds/v009effect/_build-session.out.log`.
- Tests compile: PASS in
  `.claude/session-builds/v009effect2/_build-session.out.log`.
- `[b1025],[v009]`: 28 assertions / 2 cases PASS.
- Complete `pd-tests`: 54,834 assertions / 985 cases PASS.
- Archive scanner selftest: 16 parity cases, recursion, and 9 structured
  source contracts PASS.
- All-family conformance: 28 roots / 52 recursive / all 27 families PASS.
- Needler conformance: 1 root / 9 recursive archives PASS.
- `asset_native_source_guard.py`: PASS.
- Frozen smoke-start working-set fingerprint:
  `d3500b6bdc21f0930071ce9e8c0045d3fc83de449bc801acf293a4994d71906e`.
- Final 21-file production/test/tool fingerprint after the truthful fixture and
  smoke-regex corrections:
  `ede68704d400af6e2d694c0cc3ba89c2b3d2c26b600c5b0bf63fb2bce9b172ad`.

## Ordinary-client receipt

`.claude/smoke-verify-runs/results-20260811T110536Z.json` passes 45/45 and
exits 0. The client log proves:

- recursive `.pdsfx` row publication and exact archive-qualified source;
- effect, audio, and weapon lifecycle activation;
- normal custom secondary-function selection and world collision;
- authored explosion, spark, catalog audio, and 1.5-second instance lifetime;
- presentation snapshot commit and `needler_pink_burst` renderer consumption;
- clean scripted shutdown without source fallback.

All three frames under
`.claude/smoke-verify-runs/screenshots/20260811T070338-needler_graph_runtime_visual_smoke/`
were inspected. They show the live HUD and source-model proof overlay, but the
640x480 scene is washed out; they are not claimed as an unambiguous visual
shape/color proof for the effect. That remains part of the open V-009
edited-value A/B visual gate, alongside restart, replacement/rollback,
repeated-owner, and real-peer verification.
