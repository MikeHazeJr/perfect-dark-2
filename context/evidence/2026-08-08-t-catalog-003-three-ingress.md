# T-CATALOG-003 real three-ingress boundary proof

Date: 2026-08-08

Status: B-1012 implementation, source-frozen automation, and the authoritative
installed-client three-ingress fixture pass. T-CATALOG-003 remains partial for
the explicit all-family and mixed-descriptor atomicity residuals below.

## B-1012 production correction

Received PDCA extraction previously committed its candidate tree and removed
the prior destination backup before typed scanner/catalog admission. A later
source-path or descriptor rejection could therefore leave rejected files
installed, destroy the prior valid install, and still increment the network
`received_count`.

`pdcaExtractArchiveBegin` now publishes a candidate while retaining the exact
prior sibling backup. The production receive handler commits the filesystem
transaction only after the scanner reports catalog admission. Rejection removes
the candidate, restores the prior destination, and does not count the transfer.
The existing one-call API remains as a begin-plus-commit compatibility wrapper.

## Real fixture route

`catalog_three_ingress_boundaries_smoke` dynamically builds editable
`.pdgamemode` public archives whose declared runtime member produces exact
FileProvider source lengths of 127, 128, 1023, and 1024 bytes through each of:

- an enabled loose standalone mod folder;
- an enabled nested `.pdmod` archive;
- raw PDCA bytes delivered through the production BEGIN/CHUNK/END receive
  handlers into `mods/received`.

The client probe loads each admitted identity through the real catalog and
runtime binding, then records exact catalog-path-to-runtime-path equality and
the FileProvider path length. Disabled loose and nested 1024-byte candidates
are injected after startup through their real scanners. The received 1024-byte
candidate reaches staged filesystem publication and must roll back after
catalog rejection. All three rejected IDs are then required to remain absent.

## Supporting verification

- Isolated client, updater, and `pd-tests` builds pass in
  `.claude/session-builds/catalog-ingress/`.
- Focused `[T-CATALOG-003]` automation passes 3,576 assertions across 13 cases,
  including open/write/publish rollback, prior-install restoration, rejected
  archive preflight, exact 127/128/1023 destination boundaries, and the new
  post-publication catalog-admission rollback transaction.
- Fixture structure inspection generated 12 typed archives and confirmed exact
  127/128/1023/1024 catalog source identities for loose and nested sources plus
  four received PDCA rows.
- The complete current suite passes 53,264 assertions across 949 cases. The
  native-source guard, strict 28-root/52-recursive all-27-family example
  conformance, JSON/PowerShell syntax, and scoped diff check pass.
- The exact source/test/smoke fingerprint remained
  `516CC61D8D63FC814295E7BE56E1EEDB1FC6509D515F081474E71CB0C74A55AA`
  across the authoritative build, smoke, focused/full suite, guard, and
  conformance interval.

## Authoritative installed-client result

`.claude/smoke-verify-runs/results-20260809T021712Z.json` passes 24/24 with a
clean scripted exit. Loose, nested `.pdmod`, and received-network assets each
record exact FileProvider/catalog/runtime equality at 127, 128, and 1023 bytes:
three matches at each length and nine total. All four received archives reach
transactional staging. The three valid candidates commit and refresh the mod
registry; the 1024-byte candidate rejects after staging and restores its prior
destination. Loose and nested 1024-byte candidates also reject. All three
rejected IDs have no catalog row, provider, runtime binding, or dependency
edge, and no valid candidate falls back or reports a mismatched runtime path.

## Preserved blocked receipt

The corrected installed-client fixture run at
`.claude/smoke-verify-runs/results-20260809T020251Z.json` never reached an
ingress probe. Startup stopped because the concurrent T-ASSETS-033 source
classified built-in `base:effect_explosion_profiles` dependency
`base:sfx_unlabeled_avrr` as unresolved/non-playable. The catalog summary
reported one registration failure and deliberately failed closed. The fixture
had only its valid loose/nested packages enabled, so this is an independent
startup regression, not a boundary rejection. T-ASSETS-033 corrected and
refroze that independent configured-audio predicate before the authoritative
passing receipt above.

## Truth boundary and residuals

This representative `.pdgamemode` fixture proves the real three-ingress path
contract. It does not by itself replace the existing 34-field
static/behavioral capacity matrix with a live all-family matrix. A received
archive containing multiple typed descriptors is also not
yet one catalog transaction: the current folder scanner may publish valid
descriptors and return a positive count even if a sibling descriptor rejects,
causing the network filesystem transaction to commit. Keep T-CATALOG-003
partial until that multi-descriptor atomic-admission residual and comprehensive
all-family production coverage are either closed or assigned to permanent
follow-up items.

## Root combined Wave 10 receipt

The final frozen aggregate run compiled the client, updater, and tests, then
passed focused T-CATALOG-003 3,576/13, the complete 53,264-assertion/949-case
suite, 28-root/52-recursive all-27-family conformance, the native-source guard,
and diff check. Logs are retained under `.claude/session-builds/wave10final/`.
