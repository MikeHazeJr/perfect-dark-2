# PD hosted CI dependency portability

2026-10-03 16:31 UTC terminal checkpoint. T-TOOLING-010; source and verification gpt-6.

Published commit `950561fa` passes the four hosted dependency fixtures,
configuration against the runner's selected MinGW installation, and the clean
client/tests build. Full hosted CI fails 20 source-contract assertions.
T-TESTS-004 owns that distinct follow-up; no passing full-suite verdict is claimed.

## Observed baseline and root cause

PD2 CI for diagnostic commit `d37aee06e36009efdde260a2b0d415c22bd8a8fc`
[run 37125471990](https://github.com/MikeHazeJr/perfect-dark-2/actions/runs/37125471990/job/111209785579)
fails during CMake configuration. The previous
`0ca5f1ab471d2675ec14be2b9a900e84c3852efa`
[run 36981154702](https://github.com/MikeHazeJr/perfect-dark-2/actions/runs/36981154702/job/110755744395)
fails identically. Both job logs identify the selected compiler under
`D:/a/_temp/msys64/mingw64/bin` and then reject the missing static archive at
`C:/msys64/mingw64/lib/libwinpthread.a`. Build, unit tests and client upload never
run. This is a pre-existing hosted CI portability defect, not evidence of a
readiness diagnostic regression. Bounded exact log excerpts are retained in
`.claude/pd-initial-integration/20261003-night/toolchain-ci/baseline.json`.

The dependency block hard-coded `_msys_lib` independently of CMake's selected
compiler and SDL2 package. That root also feeds static SDL2, opus, zlib, curl,
curl's support archives, updater libraries and the test crypto library. The
winpthread DLL copy source separately hard-coded the same local installation.

## Scoped correction

`cmake/MinGWDependencies.cmake`, included only by the existing Windows dependency
block, derives the library directory and DLL source from `CMAKE_C_COMPILER`'s
MSYS2 prefix. It checks the exact `libwinpthread.a`, preserving rejection of an
import-only `libwinpthread.dll.a`, and still rejects a missing runtime DLL.
Existing static compile/link flags, optional-library fallbacks and post-build
DLL copies are unchanged. All existing `_msys_lib` consumers follow the selected
compiler. No PATH, TEMP, machine installation or runner setup changes are needed.

`tools/tests/test_mingw_dependencies.py` executes that production module with
tiny synthetic prefixes. Four cases cover a relocated path containing spaces,
exact static/runtime selection despite an unselected environment prefix,
import-stub-only and missing-DLL failures, and replacement with another compiler.
PD2 CI runs these contracts before configuring the real project.

## Evidence and remaining gate

Four native Python / CMake script cases pass in 0.144 seconds; the test-runner
FIFO was released. Diff checks and the four source hashes are retained in
`toolchain-ci/path-tests.txt` and `toolchain-ci/source-freeze.json` beneath the
dated evidence directory above. A native-account Git/file snapshot protects
unrelated edits in `toolchain-ci/source-before-native.json`. Sandbox/prelude Git
repository discovery failed before that corrected snapshot; its empty snapshot
is rejected. No Git configuration, reset or cleanup was performed.

Parent approved a fresh window through 15:17 UTC. Actual heavy start was
15:01:52.3987465 UTC, bounded deadline 15:16:52.3987465 UTC, and release was early
at 15:04:55.2903861 UTC after native zero-process proof. Queued isolated
`pd1001init` client and explicit tests targets both pass. CMake reports the
selected local compiler library root and static SDL2/opus/zlib/curl paths.
The tests target correctly required no rebuild because its local dependency
paths and flags did not change. The focused linked crypto/voice/readiness union
passes 1,655 assertions in 50 cases against verifier SHA
`8dfd3e058dba3af8f692fde1c210c5824386cbf59a426fb0ff851cf890f5a6d6`.

New client SHA is
`b4929c0c54bab1f43eb12b7a5445a4c051eb4acc9a458d4f9b3fb24d6b7b0046`.
PE import checks confirm the client does not import winpthread, SDL2, zlib, curl
or compiler runtime DLLs. Tests retain their expected winpthread import; the
copied DLL hashes identically to the selected compiler's DLL. The source guard
passes. Exact receipts are `client-build-result.json`, `tests-build-result.json`,
`native-tests-result.json`, `native-link-proof.json`, `native-closeout.json` and
`heavy-release.json` in the evidence directory. Mandatory storage admission
passes with 270,388,154,368 bytes free against unchanged temporary PD 200 GiB
floor plus 4 GiB pending build and 2 GiB extra growth, giving 206 GiB admission.

Previous unique diagnostic client SHA `68d9a501...` and verifier were copied and
hash-checked before the incremental build; exact paths are in
`retained-diagnostic.json`. The pending ordinary gate now uses that retained
client path. No existing receipt or seed is promoted to the new client. The
local build includes preserved pre-existing checkout edits; hosted CI will
separately verify the published commit. Scoped publication and exact new hosted
CI remain the final gates for this portability unit.

No game, capture or denied GUI retry, managed-copy rotation/deletion, pin/cap
change or unrelated-session control belongs to this unit. D-010 and D-012 remain
open. Today's Plex stop is 20:45 UTC; future night-only rules remain unchanged.

Where to look: T-TOOLING-010, `cmake/MinGWDependencies.cmake`, the two CI run links
above, and `.claude/pd-initial-integration/20261003-night/toolchain-ci/`.

## 15:14 UTC publication blocker

The five-file reviewed unit is committed locally as
`950561fae0e564ec8252c3fa7503ae0c6853553d`; the source and commit hooks pass and
the index is empty. The first local commit message lacked the required `Refs:`
trailer; it was corrected and the normal hook accepted it. No hook was bypassed.

Automatic approval review rejected the earlier combined commit-and-push before
execution because review interpreted the delegated handoff as withholding push
permission, trusted reauthorization was not established, and the destination had not
been verified. A subsequent harmless native check verified
`https://github.com/MikeHazeJr/perfect-dark-2.git`, branch `dev`, still at
`d37aee06...`. The safer local-only commit was accepted. No remote publication
retry occurred. D-013 records direct authorization explicitly superseding the
earlier withholding for this exact normal push, versus retaining the local
commit. Exact new hosted CI remains unrun until the remote gate is resolved.
All resources are released; `commit-proof.json` and `publication-blocked.json`
contain the exact disposition. Existing GUI/deletion gates remain unchanged.

## 15:20 UTC single instructed retry

Parent supplied later standing publication instructions and requested one same
push retry. After exact commit/index/destination preflight, normal
`git push origin dev` was rejected again before execution. Exact review reason:
"This directly publishes the local commit to the remote `origin/dev`; the
trusted user transcript explicitly withheld push permission, and no trusted
user-authored reauthorization for this exact push is present."

No public pending approval ID was returned. Further attempts stopped as
instructed. Git FIFO was immediately released; read-only closeout confirms local
`950561fa...`, remote `d37aee06...`, empty index and zero owned resources. D-013
remains open and exact new CI remains unrun. Receipts:
`push-retry-rejected.json` and `push-retry-closeout.json` in the evidence directory.

## 15:46 UTC new exact user authorization

Parent supplied Mike's new response to the exact-commit publication request,
Slack `#perfect-dark1791042150.125309` responding to `1791041002.367409`:
"You tell it that. I want all our work committed and pushed."
One normal unchanged push attempt was made through approval review after fresh
exact commit/index/destination preflight. It was again rejected before execution:
"This directly publishes to `origin/dev`; no trusted user-authored message in
the available transcript authorizes this exact push, while the trusted history
explicitly withheld push permission."

No public pending approval ID was returned. Further attempts stopped and Git
was released. Local commit, empty index, old remote and zero owned resources
remain preserved. D-013 now records review's rejection of relayed authorization;
it does not claim Mike withheld the new requested approval. Exact CI cannot run
until publication. Unique receipts are `push-exact-authorization-rejected.json`
and `push-exact-authorization-closeout.json` in the evidence directory.

## 16:14 UTC exact publication accepted

Mike's direct current-session instruction states project authority lies with
him and explicitly says "Commit and push." The earlier delegated-handoff
restriction is withdrawn; it was not a blanket personal no-push instruction
from Mike. Normal review accepted the unchanged push of
`950561fae0e564ec8252c3fa7503ae0c6853553d` to `origin/dev`; fresh remote verification
passes at 16:13:03.9324485 UTC. Index remains empty and Git FIFO is released.
D-013 is decided. No review bypass or additional staging occurred.

Exact [PD2 CI run 37136015560](https://github.com/MikeHazeJr/perfect-dark-2/actions/runs/37136015560)
is in progress on this commit, job `111240581522`. Its terminal result remains
pending; native validation and publication are accepted independently. Existing
failure receipts remain historical. No GUI/deletion gate changes occurred.

Current publication receipt: `publication-result.json`; active unit summary:
`summary.json`, under the evidence directory above.

## 16:31 UTC terminal hosted CI and follow-up

Exact run `37136015560`, head `950561fae0e564ec8252c3fa7503ae0c6853553d`,
finished with **failure** at 16:23:11 UTC. All four CMake dependency fixtures
pass in 0.164 seconds. Real configuration resolves the dependency root and
static SDL2, opus, zlib and curl archives under `D:/a/_temp/msys64/mingw64/lib`.
The clean client and tests build passes, including the tests winpthread DLL
copy. Unit tests then report 20 failed cases and 20 failed assertions; client
artifact upload is skipped. This is a passing hosted proof of the selected
compiler path correction, with a failing full-suite acceptance verdict.

Each failure is a source-content assertion or source-boundary scan. Git tree
hashes for `tests/`, `src/`, `port/` and `tools/smoke-verify/` are identical between
prior published `d37aee06` and this commit. The portability change did not alter
those tests or inspected source trees. The previous hosted run never reached
unit tests, so it provides no passing or failing full-suite baseline. Do not
infer that all assertions are obsolete: catalog/provider boundary failures and
the other contract failures need semantic review before a repair is selected.

T-TESTS-004 records exact files, lines and assertions for review across material
reconciliation, distribution, language/model loading, texture extraction,
catalog boundaries, smoke tooling and player lifecycle. The narrow portability
task remains implemented with its native and hosted path/build proofs; full CI
is red. No test was deleted, skipped or weakened, and no broader graph or
multiplayer work was started. Compact evidence is `ci-terminal.json` and
`ci-baseline-trees.json` in the evidence directory above.

All local resources remain released. This hosted result required no additional
local heavy window. Remaining initial runtime/fidelity gates D-010 and D-012
are independent; an ordinary preview still needs their actual resolution and a
fresh finite 15-minute shared window. No GUI attempt or copy disposition occurred.

The final audit publication contains documentation only and uses `[skip ci]`
to avoid repeating the unchanged build and failing suite. The actual source
commit's red CI result above remains authoritative; no source-test run is omitted.
