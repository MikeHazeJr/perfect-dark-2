# PD hosted CI dependency portability

2026-10-03 15:07 UTC checkpoint. T-TOOLING-010; source and verification gpt-6.

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
