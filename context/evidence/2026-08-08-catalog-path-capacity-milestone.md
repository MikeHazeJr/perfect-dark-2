# T-CATALOG-003 path-capacity milestone

Status: partial. This receipt proves a safe production milestone, not the full
Wave C all-family ingress matrix.

Implemented:

- All 34 audited path-bearing `asset_entry_t` fields use `FS_MAXPATH`; IDs,
  names, shader IDs, archetypes, and voice context retain semantic capacities.
- Checked path copy/join APIs clear output and reject overflow instead of
  returning a nonempty prefix.
- Loose and typed archive qualification, base walkers, weapon descriptor
  mirrors, network hot-registration, runtime joins, FileProvider admission,
  and generated model metadata paths use the checked contract.
- Network hot-registration preflights the canonical shared path-key inventory.
  Failure restores an existing row or unregisters a new row before dependency
  edges are committed.

Verification:

- Isolated `tcat003` client/updater build: PASS.
- Isolated `tcat003` test build: PASS.
- `[T-CATALOG-003]`: 399 assertions / 4 cases PASS.
- Complete `pd-tests` suite: 48,531 assertions / 905 cases PASS. Durable log:
  `context/evidence/2026-08-08-catalog-path-capacity-full-suite.log`.
- Native-source guard: PASS.
- Conformance selftest: 16 parity cases plus recursive/structured contracts PASS.
- All-family example conformance: 28 root / 52 recursive archives across all
  27 typed families PASS.

Residual Wave C proof:

- Build real standalone, nested `.pdmod`, and received-network fixtures for
  every affected family at 127, 128, 1023, and over-capacity lengths.
- Prove exact catalog to FileProvider to runtime path equality for accepted
  fixtures, and zero row/provider/dependency/runtime mutation for rejected
  fixtures. Current tests behaviorally prove the shared boundary and all 34
  capacities, then statically pin each production ingress and network rollback.
- Sweep remaining scanner/network filesystem traversal joins that select
  candidate descriptors but do not themselves store public payload fields.
