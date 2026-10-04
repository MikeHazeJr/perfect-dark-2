# Existing-base smoke reuse — October 3, 2026

At 19:46 Eastern / 23:46 UTC, the storage-only lease/profile foundation is
implemented and fixture-validated (model gpt-6). **Current client runtime reuse
is refused**: separate save/home/log/capture paths cannot prevent extraction
sidecars and asset-directory `.pdextract-cache` writes. No immutable-base native
contract exists. A caller promise or a post-run hash comparison cannot replace
write prevention. No new game, native build, full-game copy or deletion occurred.

`storage.py` verifies the registered completed unpinned base against its complete
recipe, every live byte and file identity, canonical seed hash, archive blobs,
exact tested executable hash and owner marker. It rejects in-use/alias/hardlinked
files, exhausted budgets and ambiguous ownership. Failed prior verdicts remain
failed. Acquiring a lease never resets, moves or evicts a base and never relaxes
the full-copy count, byte cap or pins.

The registered exclusive lease belongs to a live Windows PID plus creation
identity. It blocks cooperating storage reset/retention. Fresh `profile`, `logs`
and `captures` directories live under `profiles/<reuse-id>/`, outside the base;
they reserve bytes under the existing policy without consuming another full-copy
slot. Closed profile bytes remain budgeted. Admission snapshots are compressed
and source-hashed rather than repeated in the mutable registry.

Closing verifies the original base, its directory identities and every prior
receipt, then retains the new profile and a separate completion receipt. Changed
base/receipt evidence keeps an integrity-failed lease held for review. Wrong
owners, escaped paths and changed markers refuse close. No stale-owner stealing,
automatic cleanup, eviction, reset or permanent-delete operation was added.
These are cooperative storage safeguards and mutation detection, not an OS
sandbox or a guarantee that an arbitrary consumer cannot write the base.

`run.ps1 -ReuseInstallId ... -ReuseSeed ... -ReuseBinarySha256 ... -ReusePlan`
validates an exact base without a profile or game. Omitting `-ReusePlan` reaches
the mandatory runtime refusal before native helpers, build or storage setup.
Read-only profile APIs are available in `lib/Storage-Harness.ps1`; they do not
launch a consumer. Existing install/copy modes are separate legacy paths.

Final verification: **16 native Windows Python cases**, **14 wrapper checks each
on PowerShell 5.1 and 7.6.5**, parser/diff checks and the native-source guard pass.
Tests use synthetic tiny retained fixtures (Python set 70,998 bytes), not copied
game assets. They cover concurrent acquisition, live file locks, wrong hashes,
seed/blob/marker tampering, file/directory/receipt mutation, owner PID reuse,
path scope, pin/cap refusals and retained profile accounting/close behavior.
CI includes the same focused reuse checks.

Durable receipts:

- `.claude/smoke-storage-validation/reuse-72f33dc82d8742a494418c544e03fadb/results.json`
- `.claude/smoke-storage-selftests/reuse-7a089c98a49a4a578a09fe79788ef540/PowerShell path with spaces/wrapper-results.json`
- `.claude/smoke-storage-selftests/reuse-9c7f02fb808842a5b5e7a665b7b0e850/PowerShell path with spaces/wrapper-results.json`
- `.claude/pd-initial-integration/20261003-night/reuse-profile/`

Initial asset/menu acceptance remains partial. The last actual fixture remains
11/29 assertions, 1/17 events, zero captures and native-consumer proof absent.
The immediate ownership watcher remains prepared and unexecuted. A real reuse
run requires supported native immutable-base write isolation and appropriate
verification first, plus confirmed free Xbox player slot, actual app approval
and a fresh parent runtime window. No runtime window is requested for this
storage-only unit. Mike's current commit/push authority remains D-013.

## UTF-8 transport repair — October 3, 20:08 Eastern / October 4, 00:08 UTC

Published foundation `d6b59982` passed local16Python and14checks eachPS5/7, but
hosted CI37163228953 failed after4dependency/20storage/16reusePython passes.
PS5 reuse-plan stopped at JSON `Expecting value: line 1 column 1 (char 0)`;
PS7 and the native build were not reached. The original failure is preserved.

An actual native CLI fixture reproduced that exact signature with a UTF-8 BOM
and separately reproduced Unicode path corruption under locale decoding. The
backend now decodes stdin bytes as UTF-8 with an optional preamble. A Unicode
wrapper fixture then proved PS5's native pipeline ignored the function-local
`$OutputEncoding` assignment and sent a question mark in the path. The wrapper
now sends explicit UTF-8 bytes through a native Process stdin stream, uses
child-local Python UTF-8 mode and drains stdout/stderr concurrently. It does not
change console, PATH, TEMP or caller encoding defaults.

The corrected gate passes **17 Python cases**, including CLI ASCII/Unicode paths
with/without BOM, and **19 checks each on PS5.1/7**, including Unicode directories,
ASCII/BOM-bearing caller encodings, restored caller encoding and all read-only
storage actions. These remain tiny retained fixtures, with no game/full copy or
deletion. Runtime immutable-base refusal is unchanged. Only storage.py matched
the locale-dependent stdin JSON pattern in the scoped tools/devtools search.
Exact new hosted CI must complete before a hosted-green claim.

Wire receipts are under
`.claude/pd-initial-integration/20261003-night/reuse-profile/utf8-wire/`;
the exact pre-fix reproduction is `../ci-wire-reproduction-exact.json` and the
old hosted failure is `../ci-failure.log`. Python gate receipt:
`.claude/smoke-storage-validation/reuse-1652e18c08a344768a7790299796eaea/results.json`.
