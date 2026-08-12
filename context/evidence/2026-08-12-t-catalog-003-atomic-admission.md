# T-CATALOG-003 B-1043 atomic admission evidence

Date: 2026-08-12

Workbench item: `T-CATALOG-003`

Bug: `B-1043`

Status: fixed and installed-client verified. T-CATALOG-003 remains partial for
the broader comprehensive live all-family three-ingress matrix.

## Contract

A received external-layout package is one catalog admission transaction. An
earlier valid sibling cannot mask a later recognized rejection. Before the
network layer commits the staged filesystem candidate, rejection must restore:

- every catalog row changed or added by the scan;
- exact typed dependency edges;
- FileProvider interned source paths;
- private weapon, body, head, SFX, texture, animation, and stage allocation;
- `g_Stages` and its count;
- loader animation, body, and head pools and their counters/defaults;
- body and head manager mirrors;
- the prior published filesystem and pending invalidated root state.

Public editable `.pdxxx` source remains the game-facing source. The transaction
does not add an authored runtime mirror, raw cache, numeric identity, or fallback.

## Implementation

`port/src/assetcatalog_scanner.c` owns `external_scan_transaction_t`. It creates
deep snapshots before any recognized descriptor is admitted. Loose scanners
return negative for recognized rejection instead of conflating failure with
absence. Recursive scanning retains the first rejection while still counting
earlier accepts for diagnostics. Any rejection restores all snapshots and
returns `-(accepted + 1)`.

Snapshot APIs are implemented at the mutation owners:

- catalog rows and rollback-only unregister: `assetcatalog.c`;
- typed edges: `assetcatalog_deps.c`;
- provider append pool: `assetprovider_file.c`;
- private family allocators: `assetcatalog_*_slots.c`;
- stage table: `stagetable.c`;
- loader pools: `loader_pool.c`;
- body/head manager mirrors: `catalog_mgr_bodies.c` and
  `catalog_mgr_heads.c`.

`port/src/net/netdistrib.c` now rejects every non-positive scanner result,
rolls back the PDCA filesystem transaction, restores invalidated roots, and
rebuilds catalog indexes. The smoke fixture models production's hex-encoded
receive storage segments when constructing exact path boundaries.

The frozen 42-file source/test/tool manifest hash is
`63eb6fa07b47b5a47464b150f753bb93ff4d48028989c999d8fae15eb3b04e0a`.

## Automated verification

- Client, updater, and tests compile PASS. Preserved build wrapper output:
  `context/evidence/2026-08-12-b1043-build.log`.
- `[b1043]`: 36 assertions / 6 cases PASS:
  `context/evidence/2026-08-12-b1043-focused.log`.
- `[T-CATALOG-003]`: 3,636 assertions / 19 cases PASS:
  `context/evidence/2026-08-12-t-catalog-003-focused.log`.
- Full suite: 56,345 assertions / 1,019 cases PASS:
  `context/evidence/2026-08-12-b1043-full-suite.log`.
- Native-source guard PASS:
  `context/evidence/2026-08-12-b1043-native-source-guard.log`.
- Conformance selftest PASS with 16 parity cases, recursion, and 9 structured
  source contracts:
  `context/evidence/2026-08-12-b1043-conformance-selftest.log`.
- Strict conformance PASS for 28 root archives, 52 recursively checked archives,
  and all 27 families:
  `context/evidence/2026-08-12-b1043-conformance-all-families.log`.

## Installed-client receipt

The authoritative result is
`context/evidence/2026-08-12-b1043-smoke-result.json`: 39/39 assertions PASS,
exit code 0, scripted clean shutdown.

The production log proves:

- `net_127`, `net_128`, and `net_1023` each admit one descriptor;
- the three network FileProvider/runtime paths are exactly 127, 128, and 1023
  bytes, byte-identical to their catalog source;
- `net_over` rejects at 1024 with no catalog/provider/runtime/dependency state;
- `net_mixed` admits nine earlier descriptors, logs
  `CATALOG.SCAN.TRANSACTION.ROLLBACK ... accepted=9`, then returns scanner
  result `-10`;
- weapon, projectile, entity, texture, head, body, arena, game mode, SFX, and
  weapon-animation identities are absent after rollback;
- the rejected animation has no loader-pool entry;
- the prior catalog remains healthy and the process exits cleanly.

The rejected first receipt remains at
`.claude/smoke-verify-runs/results-20260812T071722Z.json`. It is not completion
evidence: its boundary fixture used the retired readable receive directory and
made network-qualified paths 15 bytes too long. It was retained because its
accepted=9 rollback markers helped isolate fixture drift from production truth.

## Remaining boundary

B-1043 and mixed-validity catalog atomicity are closed. T-CATALOG-003 remains
partial until a comprehensive installed-client matrix proves every supported
asset family's relevant path fields across standalone, nested `.pdmod`, and
received-network ingress at the exact accepted and rejected boundaries.
