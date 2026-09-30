# Held public model stage lifetime

T-RUNTIME-001; implementation and review by Codex (GPT-6). Canonical checkout,
September 29, 2026. The broader asset/graph goal remains partial.

The ordinary held loader previously published compiled model/texture pointers
whose last owner could be a replaceable catalog row. Native gun, hand and
casing models and their command lists still borrowed those pointers. The loader
now pins the exact source generation before publication, failing closed if it
cannot retain it. Repeated pins add only one stage reference. Player reset
invalidates player roots before releasing stage pins; repeated release is safe.

Retained validation from September 27:

- Client and tests builds exit 0: asset0923lang/model0927-build-exits.json.
- Explicit graph/c3842 cohort: 109 cases, 4,040 assertions, zero failures or
  errors; native-source guard exit 0: model0927-test-result.json.
- Installed weapon_mesh_ingress_smoke: results-20260927T085020Z.json passes
  37/37, exit 0, including MODEL.GENERATION.HARNESS stage_retirement PASS
  and 25/25 nested internal cases. The harness edits standard image source,
  observes old/new pixels, rejects malformed source, retains old storage after
  caller retirement and checks final/repeated stage release.
- September 29 fresh fingerprint comparison: all 2,011 paths in
  model0927-source-before.json match; model0929-drift-result.json has no changes.
  Validation artifacts live under .claude/session-builds/asset0923lang/ and
  .claude/smoke-verify-runs/. No new runtime run was needed for unchanged source.

The source-ordering assertion covers the ordinary loader's pin-before-publish
and player reset's root-before-release boundaries. The installed generation
harness is supporting source/lifetime proof, not played executable-graph proof.
Stage pins intentionally retain encountered generations until stage reset.

Still required: immutable graph equipment/model slot identity, ordinary v2
catalog activation, explicit idle policy, per-hand native dispatch, deferred
hit ownership, all-family edited-source production proof and the complete
September 23 closure plan. Peer menu/MP edits are outside this checkpoint.

Where to continue: context/designs/modding/held-graph-production-seams-2026-09-27.md.
