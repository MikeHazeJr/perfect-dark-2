# T-NETWORKING-009 temporary distributed-asset crash recovery

Date: 2026-08-12

Status: implemented and source-frozen verification passed.

## Production contract

- Temporary receive state and receipts use the canonical Mod Manager root.
- Startup detects a dirty verified tree and presents a production recovery
  modal with explicit MenuPool and input-context ownership.
- Keep revalidates receipts and admits the parent plus complete nested typed
  closure before publishing it for this process.
- Keep Disabled retires catalog, FileProvider, runtime, reverse-index, and
  dependency ownership before preserving an inert restart quarantine.
- Discard performs the same retirement, atomically renames the verified tree
  out of the active root, then removes the quarantine when possible.
- Nested weapon scanning preflights UI, audio, animation, model, entity,
  projectile, and effect members in dependency order before any publication.
- Sequential smoke processes remove the previous ordinary log before waiting
  for a new marker, preventing stale-process false passes.

## Automated and installed-client evidence

- Client, updater, and `pd-tests` build: PASS.
- `[t-networking-009]`: 89 assertions in 3 cases, PASS.
- `[smoke][tooling]`: 53 assertions in 3 cases, PASS.
- Complete `pd-tests`: 56,456 assertions in 1,022 cases, PASS.
- Asset native-source guard: PASS.
- Conformance selftest: 16 parity cases, recursion, and 9 structured source
  contracts, PASS.
- Public examples: 28 roots, 52 recursive archives, all 27 families, PASS.
- Eight-process installed-client recovery matrix: 37/37, exit 0, 603 seconds.
  It includes three deliberate unclean exits, all three real mouse-driven
  modal actions, disabled restart isolation, a replacement temporary session,
  discard, and a final empty restart.

The Keep process proved `example:tri_weapon` active with exact provider/runtime
identity and four direct nested dependencies: reticle UI, model, entity, and
projectile. Disable, disabled restart, Discard, and final restart each proved
the weapon and projectile absent from catalog, provider, runtime, and typed
dependency state where required.

Durable result: `context/evidence/2026-08-12-b1044-smoke-result.json`.

Preserved detailed logs:

- `.claude/session-builds/b1044/pd-aggregated-temporary_asset_crash_recovery_smoke.log`
- `.claude/session-builds/b1044/b1044-final-full-pd-tests.log`
- `.claude/session-builds/b1044/b1044-native-source-guard.log`
- `.claude/session-builds/b1044/b1044-conformance-selftest.log`
- `.claude/session-builds/b1044/b1044-conformance-all-families.log`

Final source/test/tool fingerprint:
`4c9cc2c4cb139b3bb4b3b354927e87b5b81d2dca2fdb49df89dba1461f202af4`.
