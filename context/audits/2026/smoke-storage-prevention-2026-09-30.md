# Prospective smoke storage — T-TOOLING-006

Mike explicitly authorized implementation after the completed cleanup report.
Model: GPT-6. Ownership: smoke install/storage harness, runner storage lifecycle,
small storage tests and their retention documentation. Instruction-document
auditing belongs to the separate worker. No staging, commit or push performed.

## Implemented behavior

- `tools/smoke-verify/storage.py`: prospective registry, serialized storage lock,
  immutable SHA-256 compressed content store, reusable seed maps, exact changed/
  deleted reconstruction recipes, native Windows verified exclusive-handle
  retention, restore, read-only inventory and configurable preflight.
- `lib/Storage-Harness.ps1` and `lib/Install-Harness.ps1`: source DLL/binary/ROM/
  data inputs remain read-only; all runtime files are ordinary private copies.
  Stable shared work has an exclusive active owner; completed bytes/identities
  are verified and reset before reuse. Modified/new files and empty directories
  cannot leak across tests. Failures and pins retain distinct full copies.
  Fixture operations reject escaping targets, links and junction traversal.
- `run.ps1`: both install modes, explicit read-only `-Install` templates and
  single/multi-process finalization use managed storage. Whole-install success
  deletion is replaced with retained complete receipts. Logs, definitions,
  verdict/assertion failures, exact binary/assets/cache/saves, screenshots and
  requested artifacts remain. Source HEAD/diff is diagnostic provenance;
  exact binary bytes are retained without claiming a reproducible source build.
- `storage-policy.json`: three full installs / 8 GiB total, including active,
  pinned and restored copies; compact archive 8 GiB; free-space floor 300 GiB,
  extra reserve 2 GiB, pending run 2 GiB / smoke build 4 GiB. Explicit JSON
  overrides are validated. Native screenshot preflight reserves 32 MiB per shot.
  Older successes, then oldest failures, yield full-copy slots, with the newest
  success preferred. Full copies yield only after complete reconstruction blobs
  and every file hash/identity/containment/exclusive handle have been checked.
- Active, pinned, edited, locked, aliased, corrupt or incomplete evidence refuses
  new copies. Receipts and content blobs have no automatic deletion policy.
  Restore creates a new pinned, budgeted directory and never overwrites.

No historical directory is imported or pruned by the new defaults. The September
30 cleanup was not repeated. The age-based cleaner remains a no-op. Historical
cache/shared install and explicit templates are read-only seed inputs.

## Measured isolated validation

Final Python receipt:
`.claude/smoke-storage-validation/4acd62227aa14b6b9b9561bc8a9b7e18/results.json`.
All **14 cases passed in 2.687 seconds**, zero errors/failures:
mutation isolation and exact same-size/timestamp-modified fixture restoration,
shared reset/reuse, retained failed/pinned shared evidence, bounded retention and
compacted restoration, active/pinned blocking, archive/disk/byte budgets, changed
completed fixtures, Windows exclusive locks, hardlinks/corruption/path escape,
policy validation, junction traversal, canonical dot-segment containment, and
non-mutating dry-run.

- Seed: **10,880 bytes**. Shared reset copied **7,168 bytes** of modified source
  and reused **3,712 bytes** of unchanged binary/ROM.
- Five synthetic runs retained **three full installs**. Archive: **5,333 bytes**,
  five distinct content blobs (binary, ROM, editable asset, definition, source
  patch). Modified exact reconstruction receipt: **927 bytes**.
- Entire Python test fixture footprint: **342,278 bytes**; its checked disposable
  GUID fixture tree was removed. No historical tree scan or large fixture copy.

PowerShell integration receipt:
`.claude/smoke-storage-validation/ps-5b8f659435b6453dbbbccf6e11426fef/results.json`.
**11 checks passed in 1.586 seconds**: plain-tree validation, target containment,
binary/DLL/ROM and prefilled seeding, completion receipts, shared mutation reset/
reuse, private template cloning, read-only inventory, and build preflight acceptance/
expected disk-floor refusal. No build child was started. Shared reset copied
**15 bytes**, reused **12 bytes**. Tiny synthetic integration fixtures remain at
`.claude/smoke-storage-selftests/ps-5b8f659435b6453dbbbccf6e11426fef/` for inspection.

PowerShell parsing passed for runner, both libraries, build-session and wrapper test.
`python -B tools/asset_native_source_guard.py` passed (`asset-native-source guard ok`).
The coordination test FIFO was finished. No game, native build, firewall change,
large extraction, permission change, unrelated source edit or other session
control occurred. Prior successful receipts remain as additional small evidence.

## Practical limits / handoff

Budgets are preflight and prospective retention controls, not an OS filesystem
quota. Unexpected native growth is preserved, can exceed an estimate, and blocks
later copies. Interrupted work remains active/protected and needs deliberate
review; the implementation does not reap another session. Multi-process tests
with separate installs need a slot for the parent plus each process.

The production smoke runner and direct `devtools/build-session.ps1` starts are
connected. The build wrapper checks after its existing queue/lock acquisition and
before starting build copies, accepts `-StoragePolicy`, and preserves maintenance,
queue and watchdog behavior. Other package/extraction tools retain their lifecycle;
generic preflight is available for adoption. Reuse session build IDs and
retain build evidence before deliberate compiler-product cleanup. Gameplay and
performance with full real assets have not been exercised by this small storage
gate. The new stable executable path may need the existing firewall allow helper
at the next authorized network smoke. No current tool/approval blocker remains.

Mike's Steam uninstalls are separate from the completed 93.063 GiB historical
cleanup. This implementation does not claim any additional historical reclaimed
space.
