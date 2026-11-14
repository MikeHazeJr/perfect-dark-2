# Historical smoke storage cleanup - 2026-09-30

T-TOOLING-005, owner codex-0e8900ca-467b81e8, model GPT-6.

Mike approved the 20.9 GiB lower-risk pool and the 73.6 GiB conditional pool,
then explicitly stopped exhaustive verification and authorized permanent removal,
accepting possible repair. This supersedes the earlier manifest/confirmation wait
for these pools only. All deletion targets remain under canonical `.claude/`.
The active action receipt is
`.claude/cleanup-manifests/20260930T164808Z-approved-cleanup/`.

Mike's later clarification: the increased C: free space came from his own Steam
uninstalls of Baldur's Gate 3 and Satisfactory, separately from this cleanup.
The 16:05 UTC snapshot measured 292,030,832,640 free bytes (272.0 GiB). He prefers
at least a few hundred GB free where practical. Future build/extraction/package
preflight should reserve peak copy growth and expose a configurable free-space
floor (proposed 300 GiB), without deleting other files or uninstalling anything.

The read-only audit measured 124.7 GiB in smoke runs, 28.7 GiB in session builds,
2.3 GiB in current menu snapshots, 1.5 GiB in the shared install and 0.21 GiB in
the extraction baseline. Smoke runs include 4.67 million files. Results are only
58 MiB; active context/Workbench cites 154 distinct receipts. Codex still uses
the `.claude` paths. The old automatic cleaner was able to erase whole evidence
trees based on age; it now returns without mutation on automatic invocation.

Generators/references: `tools/smoke-verify/lib/Install-Harness.ps1` copies the
binary, staged ROM and data into isolated installs (around line 173), including
prefilled extraction assets (around line 213). `tools/smoke-verify/run.ps1`
removes successful non-shared, non-`-Keep` installs (around lines 1934/2440), but
failures and `-Keep` output accumulated without a global budget. The native
`port/src/modasset_compiler.c` creates source-hashed private `mod-cache` products
(around lines 4569/4582). `devtools/build-session.ps1` still uses `.claude` for
Codex builds. The target output pools are Git-ignored and contain no tracked
deletion targets. Current source can recreate fresh installs/compiler products;
Git alone cannot recover ignored historical outputs or guarantee an old failing
binary/data combination. Mike explicitly acknowledged that loss and reconfirmed
proceeding in #perfect-dark message `1790785834.792399` after the parent notice
`1790785814.207779`. Root receipts, failure logs, mods/social fixture trees,
screenshots and retained source/binary witnesses are therefore kept.

`devtools/smoke-storage-manifest.py` is a non-destructive serial audit. It
restricts its pool to May-August dated installs plus compiler products from
builds with no file writes since September 1. Every proposed duplicate is
fully SHA-256 verified against a retained baseline or retained unique historical
copy. Every proposed compiler product is also hashed. Unique source, binary and
runtime-cache content remains in place, with logs/captures/config/mods/results
excluded. Targets are never directories. Canonical root and reparse checks plus
Windows opened-handle final-path verification prevent path alias traversal.
Process snapshots cannot prove absence of every open handle; action-time usage
and content checks remain mandatory.

Both exhaustive hash jobs were stopped by this lane, with their partial records
retained under `20260930T160400Z` and `20260930T161611Z`. Recovered complete
retained-witness records identify 29,577 protected paths, including 12,659 unique
historical source assets, five unique historical binaries and four unique cache
files; these stay. The full hash proposal is intentionally incomplete and is not
the assurance basis of the approved action. Deletion now uses the exact previously
scoped 293 May-August runs and 70 pre-September builds, not freshly discovered
pools. Only run `data`/`mod-cache` files, root `pd.ntsc-final.z64`/`PerfectDark.exe`
and build `CMakeFiles` `.obj`/`.gch` files are eligible. Evidence/config/save file
types, identified witnesses, declared fixture destinations and newer writes are
excluded. No directories, permissions, or sessions are modified by deletion.
Exclusive Windows handles reject currently open files; opened-handle final paths
and pinned plain directories reject aliases/reparse traversal. Locked or uncertain
targets are logged and left. Source and retained-evidence snapshots are compared
afterward. No tests, builds, game runs, Git staging/commits/push are performed.

Proposed recurrence prevention, not implemented: content-addressed immutable
baselines with private copies or true copy-on-write for mutation tests; compact
retained records including a source/binary/asset reconstruction map; a global
full-install budget; disk-budget preflight and manifest-only cleanup proposals.
Do not implement shared mutable test assets or retroactive evidence eviction.

Concrete next harness plan, before restructuring: one immutable versioned asset
baseline and private copies or genuine copy-on-write for every mutating test;
compact durable receipts with binary/source/asset/test-definition fingerprints,
dirty-source reconstruction, failure logs and selected captures; at most three
new full installs globally (latest validated and two unresolved failures), within
an 8 GiB full-install budget; explicit pins for unique unresolved evidence;
preflight reserves configurable 300 GiB free on Shadowbane plus peak pending
build/extraction/package/copy growth. Refuse a new large copy when evidence cannot
yet be compacted or a budget is exceeded; never silently evict old evidence.
Implement and validate isolation before changing copy behavior. The live smoke
harness is unchanged; this is a proposed scoped follow-up, not a retention sweep.

Completed 16:48:08-17:19:12 UTC. No permission/approval-review stall occurred in
this worker; elapsed time was individual-file NTFS work, at BelowNormal priority.

| Deleted file group (exact roots/names in receipt) | Files | Logical bytes | GiB |
| --- | ---: | ---: | ---: |
| Historical run `data` | 3,247,742 | 57,031,015,165 | 53.114 |
| Historical root `PerfectDark.exe` | 288 | 21,395,659,775 | 19.926 |
| Historical root `pd.ntsc-final.z64` | 293 | 9,831,448,576 | 9.156 |
| Historical `mod-cache` selected files | 35,964 | 25,893,963 | 0.024 |
| Old build `CMakeFiles` `.obj`/`.gch` | 49,406 | 11,641,322,668 | 10.842 |
| **Total** | **3,333,693** | **99,925,340,147** | **93.063** |

Preserved candidate files: 48,633 / 1,519,833,670 bytes (1.415 GiB): 12,668
identified unique witnesses / 627,715,336 bytes, plus 35,965 conservative
JSON/evidence/config records / 892,118,334 bytes. This includes historical cache
JSON retained conservatively; no claim is made that every remaining JSON record
is irreplaceable evidence. Locked/in-use/uncertain failures: zero. Directory
skips/removals: zero. No active game/build process was seen before or afterward.
Global handle/command-line enumeration was inaccessible; exclusive file handles
and periodic ordinary process snapshots were used instead, without controlling
other sessions. Metadata/file identities of 71,691 retained files were checked
with zero drift; 3294 tracked source/script files were SHA-256 compared before/
after deletion with zero drift and no inaccessible source files. Authorized
procedure/context/Workbench edits are separate from the deletion operation.

Free C: at action start 291,755,159,552 bytes (271.718 GiB), after 392,267,542,528
bytes (365.328 GiB); net +93.609 GiB. Logical deletion size is the exact sum of
successful individual operations; net free-space changes can also reflect
filesystem allocation and concurrent work. Steam uninstalls preceded this
baseline and are not credited to cleanup.

Receipt: `.claude/cleanup-manifests/20260930T164808Z-approved-cleanup/summary.json`.
Exact full-path ledgers: `deleted-files.jsonl.gz`, `skipped-files.jsonl.gz`,
`attempted-files.jsonl.gz`. `protected-witnesses.jsonl.gz` is a fully closed union
of 29,577 retained records recovered from the two interrupted hash attempts.
`source-before.json.gz` / `source-after.json.gz` contain the source proof, and
`evidence-before.json.gz` records the initial retained-evidence inventory.
Deletion-ledger SHA-256:
`a3fda47590be254bdb7a188edaf3db669b1d6b19f3f18a36f7736eab3a00a71d`.
Receipt records the other ledger/scope hashes and explicit confirmation IDs.

No cleanup blocker remains. Broader harness isolation/budget integration is a
proposal awaiting the normal exact-file ownership and validation lane, while
Astral engineering is active. No unrelated source changes or publication made.
