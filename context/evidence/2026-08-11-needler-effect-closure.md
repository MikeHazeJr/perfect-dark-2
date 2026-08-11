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
edited-value A/B visual gate, alongside replacement/rollback, repeated-owner,
and real-peer verification.

## Preserved-install restart receipt

The accepted cold run left the packed archive and enablement state in the
shared install. A separate restart scenario intentionally declares no
`remove_paths`, `fixtures`, or `packed_fixtures`, and was launched with
`-Install .claude/smoke-verify-install` so the runner could not reseed the
binary, archive, or configuration.

`.claude/smoke-verify-runs/results-20260811T112514Z.json` passes 37/37 and
exits 0 in the second client process. It re-registers the exact recursive
weapon/effect/SFX chain, activates the three public owners once each, selects
and collides the normal secondary projectile, and commits the same authored
gameplay, audio, presentation, and renderer work. Before and after the run:

- `needler.pdmod` SHA-256 remained
  `8F256E0675AFCBDB11EA2E822A84615AED6A2579AACF4E195E391AEBD77A18F0`
  with timestamp `2026-08-11T11:03:38.3566331Z`;
- `mods-enabled.json` SHA-256 remained
  `A905BAB375CBB9F806A371C5938F05E88C141DB26D1C7C594F3F7C5A270B5050`
  with timestamp `2026-06-17T18:14:43.4619575Z`.

This closes process-restart persistence. It does not substitute for live
replacement/rollback, overlapping owners inside one process, a real network
peer, or the still-open edited-value A/B visual comparison.

## Same-process production Mod Manager lifecycle

The tracked `needler_mod_manager_lifecycle_smoke` starts from a clean 1280x720
portable install, packs and enables the editable Needler source, and performs
one real `catalogLoadTypedAsset(ASSET_WEAPON, "mod_needler:needler")` before
opening the existing Mod Manager after the ordinary hotswap menu is active.
All state mutation then goes through real SDL mouse motion/button events and
the production checkbox, Apply, and completion modal paths.

The first retained receipt,
`.claude/smoke-verify-runs/results-20260811T120524Z.json`, is rejected overall
because re-enable hit a false Large Mod modal. It nevertheless proves the
first lifecycle half: the active weapon, nested effect, and nested SFX each
unloaded at ref `1->0`. That modal exposed B-1026: comparing signed Windows
`off_t` to `(off_t)0xFFFFFFFFu` converted the limit to `-1`, so the real
12,851-byte archive displayed as 4096.0 MiB. The fixed code widens `st_size` to
`u64` before clamping to the public `u32` field.

Final `.claude/smoke-verify-runs/results-20260811T121111Z.json` passes 22/22
and exits 0. It proves:

- active Needler weapon/effect/SFX teardown at ref `1->0`;
- a zero-enabled-mod catalog rebuild;
- same-process checkbox re-enable without the false size warning;
- archive remount, recursive public-source rescan, successful Apply, and final
  `mods-enabled.json` containing `needler`;
- six in-game captures, including visually confirmed disabled-pending and
  enabled-pending checkbox states.

Automated verification passes client/updater/tests build, focused B-1026 and
mouse tooling at 22 assertions/3 cases, the launcher/source-gate contract at
391/1, the complete 54,852-assertion/986-case suite, native-source guard, and
diff check. Durable build/result artifacts are
`2026-08-11-v009-mod-manager-{client,updater,build}.log` and
`v009-mod-manager-lifecycle-result-20260811T121111Z.json` in this evidence
directory. Final production/test/scenario fingerprint:
`1f7a25ad07e4483e50d23c0f0c92f99f2855c25a6d009346eb04598ccb14411b`.

This closes active disable and same-process re-enable/remount/rescan. It does
not claim a second active-owner reacquisition/release after re-enable, live
replacement rollback, real-peer distribution, or edited-value A/B visuals.
