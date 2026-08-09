# T-CATALOG-003 PDCA receive transaction

Date: 2026-08-08

Status: implementation is source-frozen and the combined Wave8 receipt passes.
T-CATALOG-003 remains partial because real installed-client end-to-end path
fixtures are still open.

## B-1006 production correction

Received PDCA envelopes no longer write directly into the live component
directory after preflight. `pdcaExtractArchiveTransactional` derives a unique
sibling stage, validates the complete envelope and both final/stage paths,
writes and file-syncs every member, and publishes the complete tree by rename.
An existing destination is moved to a unique backup only after the stage is
complete. Open, write, or publish failure restores the prior directory.

Windows destination identity is normalized separately from authored output:
member case is preserved and backslashes become separators, while duplicate
checks compare a lowercase slash-normalized key. Traversal, ADS/colon, `::`,
empty/dot components, wildcards, trailing dot/space components, and Windows
device names are rejected before staging. Matching abandoned stage trees are
removed; other destination hashes are untouched. A crash backup beside no
destination is restored. A backup beside a destination is deliberately left
in place and blocks replacement because the correct owner cannot be inferred
safely.

## Frozen lane surfaces

- `port/include/pdca_extract_transaction.h`
- `port/src/pdca_extract_transaction.c`
- `port/src/net/netdistrib.c` extraction wrapper/callsite only
- `tests/test_pdca_extract_transaction.cpp`
- `tests/test_asset_path_contract.cpp` production pin update
- `CMakeLists.txt` two test-source entries

The final source-frozen Wave8 receipt passes client, updater, and `pd-tests`
builds; `[T-CATALOG-003]` passes 3,561 assertions across 12 cases; and the
complete suite passes 52,416 assertions across 932 cases. The conformance
self-test, 28-root/52-recursive all-27-family scan, native-source guard, and
diff check also pass. The focused summary is preserved in
`context/evidence/2026-08-08-catalog-pdca-transaction-focused.log`; raw logs
remain under `.claude/session-builds/wave8final/`.

## Explicit residual

This closes deterministic PDCA filesystem rollback, not T-CATALOG-003. The
existing 34-field matrix still snapshots catalog/provider/runtime stages.
Real installed-client fixtures must exercise standalone typed archives,
nested `.pdmod`, and actual received-network components through scanner,
catalog row, FileProvider handle, and runtime consumer at 127, 128, 1023, and
over-capacity lengths. Rejection must observe zero catalog row, provider
binding, dependency edge, runtime registration, or installed component tree.
