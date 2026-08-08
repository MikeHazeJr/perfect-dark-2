# T-CATALOG-003 Wave C — path identity and PDCA fail-close

Date: 2026-08-08

Baseline: `8aaa14a10662b82b1ad01aa0784d4efb91418029`

Status: partial. The production fixes and automated boundary/regression bands
pass. Real installed-client/FileProvider transport fixtures remain required
before this item can become implemented or validated.

## Defects closed in B-1002

- Loose scanning left relative subdirectory values unrooted.
- Received-network preflight checked a joined path but populated the original
  relative INI value, allowing catalog and runtime path identity to diverge.
- The shared path-key inventory omitted the supported `skin_file` alias.
- Scanner, network, and recovery traversal joins could truncate a candidate.
- PDCA extraction wrote while parsing and could accept a partial archive after
  rejecting a later member.
- PDCA construction could silently omit hidden, unreadable, oversized,
  allocation-failed, or short-read members.

The shared path contract now roots safe relative paths, rejects parent
traversal and overflow atomically, and preserves already-qualified archive VFS
paths. Network ingress qualifies the mutable candidate before catalog row
population. PDCA receive preflights the complete envelope and every resolved
destination before writing; construction fails the whole candidate if any
member cannot be represented faithfully.

## Frozen source fingerprint

- `port/include/asset_path_contract.h`:
  `91EE698FD0B445CDD6668B2F4AE9C6A778D127058D32990DB02CA69802E63B41`
- `port/src/assetcatalog_scanner.c`:
  `BFCDE21006B546D14ACCB44D5856A0FF1006E82EA23765C2E41D579215FB35C2`
- `port/src/net/netdistrib.c`:
  `8FD7F6464FF09B2BF902B42A4B0263D93AC6B494C602CEF9E6CAE5FEF19F316A`
- `tests/test_asset_path_contract.cpp`:
  `60AE084AAB8D6AABA94A78493A338D12965EB1B5DAEA8025489D7C9856187538`

## Automated receipts

- Isolated `tcat003c` client, updater, and `pd-tests` builds: PASS.
  Preserved build transcript:
  `context/evidence/2026-08-08-catalog-path-capacity-wavec-build.log`.
- Focused `[T-CATALOG-003]`: PASS, 3,472 assertions in 5 test cases.
  Log: `context/evidence/2026-08-08-catalog-path-capacity-wavec-focused.log`.
- Complete `pd-tests`: PASS, 51,902 assertions in 913 test cases.
  Log: `context/evidence/2026-08-08-catalog-path-capacity-wavec-full-suite.log`.
- Archive conformance selftest: PASS, 16 parity cases, recursion, and 8
  structured source contracts.
- Strict example conformance: PASS, 28 root archives, 52 checked archives, all
  27 public families.
- Native-source guard: PASS.
- `git diff --check`: PASS.

The focused matrix covers the 34 migrated catalog path fields across
standalone filesystem, nested `.pdmod` VFS, and received-network qualification
at total lengths 127, 128, 1023, and 1024/over-capacity. Accepted helper-stage
paths remain byte-exact; rejected qualification clears its output and leaves
downstream sentinel state untouched.

## Explicit residual acceptance work

The focused matrix exercises the production path contract but uses local
snapshots for the catalog/provider/runtime stages. It is not an installed game
or real network transfer fixture. Remaining work must create real standalone,
nested `.pdmod`, and received-network packages for affected family/key groups,
load them through the production catalog and FileProvider, observe the runtime
consumer value exactly, and prove rejected packages produce no catalog row,
provider binding, dependency edge, runtime registration, or partial installed
filesystem state. I/O-failure atomicity also still needs a staged extraction
transaction rather than relying only on preflight.
