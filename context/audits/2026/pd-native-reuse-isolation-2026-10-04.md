# Native retained-profile reuse isolation

T-TOOLING-011, 2026-10-04 04:20 UTC published verification. Implementation attribution:
`gpt-6.1-sol` (boot/cache/UI adapters and PowerShell adapter), `gpt-6`
(native write policy, integration, source guard and verification).

Mike's current direction is to **commit and push our scoped project work**.
The night assignment permits this repair without another full-game copy or
cleanup. Existing controller/app admission gates remain required.

## Implementation

The native opt-in `--reuse-write-root` mode activates before updater/filesystem
startup and requires explicit base, profile, save/home/capture paths plus smoke,
no-network and no-update flags. Ordinary launches retain their existing paths.
Generated extraction SHA/stamp and compiler products use the new profile;
authored public assets remain native catalog/provider inputs in the verified
base. Missing/corrupt extraction inputs refuse repair rather than rewrite it.
Automatic migration/update paths are disabled in reuse. Font/theme/chrome
discovery reads the verified base mods directory.

The common Windows policy guards both direct and imported-pointer routes.
It validates canonical paths, parent handles, reparse status, final handle paths
and single-link files before disk mutation; base/profile roots remain locked
against replacement. Mapping and shell/process escape paths refuse in reuse.
Win32 and CRT inherited outputs must be console/pipe or private profile files;
refusal before activation is silent, and failed output admission keeps guards
active until exit. Debug home may name the declared root to route child logs;
the root itself remains forbidden to mutation.
It is a linked native policy, not an OS sandbox against other programs.
Storage admission/completion separately verifies base bytes, metadata, ownership
and prior receipts; ambiguity/mutation retains the lease and fails closed.

Recognized HID and pipe opens use only narrow OPEN_EXISTING access and require
actual CHAR/PIPE handles. Policy denial or unknown type latches a refusal before
fixture admission, on smoke ticks/controller transitions and before scripted
success. Ordinary original OS failures, including absent Discord IPC endpoints,
preserve their normal failure behavior. No hardware device was probed.

The PowerShell adapter uses a separately supplied consumer executable and SHA,
holds byte/path identity locks, and queries the actual linked capability before
acquiring a profile. The completed base executable/hash and failed verdict stay
unchanged. An exact canonical unchanged-input menu fixture is supported;
settings keyboard and mutating/orchestrated fixtures refuse before acquisition.
Profile, logs and captures share the fresh retained writable root. Actor absence
and complete console drainage precede release. The canonical fixture remains
locked; readiness, event timing, assertions and timeout caps are preserved.
Each run writes a fresh `profile/result.json` before completion, and retains
input-sequence captures in its result and compressed completion receipt.

## Evidence so far

Private evidence root:
`.claude/pd-initial-integration/20261004-night/native-write-isolation/`.

- Queued native client build passed. The `all` wrapper built client/updater;
  the separately invoked `tests` target passed. Do not interpret `all` as
  evidence of a tests rebuild.
- Actual hidden capability query PID 29672 exited 0 with supported true.
  Focused device-policy suite passed **21 cases / 286 assertions**. This is
  preliminary proof before the subsequently discovered `rmdir` alias repair.
- Final queued client and **explicit tests** rebuilds passed after that repair
  and boot/output review. **22 cases / 321 assertions** pass, including separate
  inherited Win32 and CRT outputs, base-byte preservation and guarded refusal.
  `native-reuse-final.log` and `final-tests-build.log` retain the evidence.
- Production locked-consumer query passed on **PS 5.1.26100.9549 and PS 7.6.5**;
  each actual native process exited 0 and was verified absent. The query starts
  no SDL/game. `policy-query-final-ps51.json` and `policy-query-final-ps76.json`
  bind client SHA256 `26aefa01b2b5d1a3723f6433c5cacd82d27a2855e195bc6b732567d738a96660`.
- Production retained-profile helpers passed **19 storage checks plus adapter
  refusal/identity checks** on PS 5.1.26100.9549 and PS 7.6.5. These tests did
  not execute a consumer binary or game; tiny fixtures remain retained.
- Known write/API source guard passed **6 cases**, scanned **789 configured
  client translation units / 1,477 repository files**, and inventoried **64
  guarded symbols**. It caught actual plain `rmdir` undefined references in map
  import/distribution objects; that additional route and an empty-source
  directory refusal test now pass in the rebuilt client/tests. Reviewed inactive
  branches/dynamic random/timer calls are bound to call-context hashes.
- The source guard is a known-API inventory, not a C/C++ verifier or complete
  third-party dependency proof. Link witness, behavioral tests and dependency
  audit remain separate evidence. CI runs the inventory after configuration.
- Final public-native-source guard and adapter parse checks pass.
- `source-freeze-4.json` binds 26 source paths. Earlier failed verification
  receipts are retained, including the corrected path comparison assertion.

## Publication and hosted verification

Source commit [c6b3e2ec](https://github.com/MikeHazeJr/perfect-dark-2/commit/c6b3e2ece8b3e2855ed820534b03925ef799a7b9)
is published on `origin/dev`; fresh remote equality, empty index and all owned/
peer working snapshot bytes were verified. Its 30 paths contain 26 source files,
this audit and only root-owned additions in three mixed context documents.

Exact [CI 37176234687](https://github.com/MikeHazeJr/perfect-dark-2/actions/runs/37176234687)
completed **success**, recorded at 04:18:51 UTC: 4 dependency/6 source-guard/
20 storage/17 reuse Python cases, 19 storage checks plus adapter checks on both
hosted PS versions, configured inventory **790 client units / 1,478 files / 64
routes**, clean client/tests build, actual linked capability query, full **1,835
cases / 334,167 assertions**, and client artifact upload. Hosted configured
counts differ from the local 789/1,477 configuration; both inventories pass.
Compact `ci-c6b3-success.json`, `ci-c6b3-compact.log` and `ci-c6b3-summary.log`
retain exact source, step verdicts and counts. Sol6.1's final read-only review
found both boot/output corrections sound within that limited scope.

## Remaining gates

Root requested the new shared window, used the latest parent availability
with fresh storage/process admission and normal FIFO, and released it at
04:03 UTC after the bounded headless checks. Free space was 270,119,858,176 bytes,
floor 214,748,364,800, pending build 4 GiB; archive stayed 881,170,023 bytes.
Scoped source publication and exact-source CI are complete. T-TOOLING-011 stays
partial until ordinary retained-profile game integration is actually exercised.

No real game, full-game copy, deletion, cap/pin change or controller takeover
occurred. Initial asset/menu acceptance remains partial: the last ordinary run
passed 11/29 assertions, fired 1/17 events and produced zero captures because
the Xbox controller occupied player 0. Actual free slot, app access, fresh
storage/process admission and a bounded runtime lease remain required.
