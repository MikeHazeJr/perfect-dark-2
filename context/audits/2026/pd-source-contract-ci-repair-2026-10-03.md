# PD source-contract CI acceptance repair

T-TESTS-004, 2026-10-03. Source and verification: gpt-6.

## Baseline and categories

Published source commit `950561fa` passes dependency fixtures, configuration and
the hosted clean client/tests build. Exact [CI run 37136015560](https://github.com/MikeHazeJr/perfect-dark-2/actions/runs/37136015560)
then fails 20 test cases and 20 assertions. These are source-inspection checks;
the count does not represent 20 separate gameplay bugs. All inspected source
and test Git trees match preceding `d37aee06`, whose CI stopped at configuration.

Review identifies 16 obsolete implementation expectations, one function-selection
error in a test helper call and three API boundary violations. No missing gameplay
behavior is confirmed. A full local run also exposed checkout line-ending drift.
The exact case-by-case mapping is retained in
`.claude/pd-initial-integration/20261003-night/contract-ci/classification.json`,
alongside the original failure lines in `toolchain-ci/ci-terminal.json`.

| Category | Cases | Correction |
| --- | ---: | --- |
| Changed signatures, helpers, transactions and source selection | 13 | Follow the current production path while retaining identity, source-only loading, failure, ordering and rollback requirements. |
| Managed smoke storage and cleanup | 3 | Check exact owned process cleanup, immutable hashed input seeding and once-per-install completion. |
| Test function selection | 1 | Inspect the exact `playerReset(void)` definition rather than its similarly prefixed abort helper. |
| Catalog/provider API boundaries | 3 | Use existing typed model/body/head resolvers and the catalog source-file handle API; keep the source-wide bans unchanged. |

## Scoped changes and proof contract

The queued weapon check still validates the exact selected catalog ID, its
native source number, catalog generation and provider handle. It now obtains
the selected entry and handle together through `catalogResolveModel`.
Relocation witnesses use typed body/head/model getters; body/head source
witnesses obtain file handles through `catalogHandleForSourceFile`. No provider
exception or new authored numeric reference is introduced.

Updated guards follow shared distribution queue, scanner audio admission,
model generations, validated language publication, locale ranking, nested mesh
binding, public texture properties and source selection, strict missing-headspot
rejection, native body index selection and shared body-only identity validation.
Source-only rejection, capacity/backpressure, exact identity and transactional
rollback contracts remain. Existing behavioral suites accompany the source pins.

A new tiny isolated storage regression changes binary bytes without changing
their length, assigns an older timestamp, and requires the shared install to
receive the new bytes while an immutable prior seed still restores the original
binary. Hosted CI runs the complete small storage suite before CMake.

The first finite batch passes queued client and tests builds, all 20 tiny storage
tests and the public-source guard. The full native suite retains five failures:
three LF-only source comparisons against CRLF checkout files, the ambiguous
`playerReset` selector and a language-cache token hidden behind the first failed
locale assertion. Those logs and the first source freeze remain preserved.
Heavy resources were released immediately after native executor absence was
verified, before the follow-up edits.

The source-contract reader now normalizes only CRLF pairs, preserving all other
bytes. The reset guard names the full signature; the language cache guard checks
the current regional source/hash stamp and source-hash reuse gate. The fresh
tests-only rebuild passes; the full native rerun passes all 1,817 cases and
335,458 assertions. The refreshed public-source guard passes. Previously accepted
client and 20-case storage checks remain unchanged. All 14 frozen code/workflow
file hashes match. The corrected verifier SHA-256 is
`45c37aa610a671fdf0554a5c1729cf806f4b4ca7f11e68f5d8e41746aa0af98b`;
the client is `43a60276400fe2be1db40279195b4fb7fc6168fb2dff527d092610c75b1ea145`.
The passing log hash is
`c72188ab2a8fba5f70977796267f42c8f93723919b96a0d9ad3e76d378789d65`.
Receipts are `contract-ci/followup-full-native-tests-result.json`,
`followup-source-guard-result.json` and `followup-closeout.json` under the
evidence directory above. The follow-up heavy window ran from 17:09:45 UTC
to its early release at 17:16 UTC after verifying zero owned native processes.

Scoped normal commit, push and a passing exact hosted CI remain the final gates.
The local build includes preserved peer changes, so exact published-source CI
must independently pass. Preserve peer edits and unique binary witnesses. No GUI, real
smoke install, retained-copy rotation/deletion, asset extraction or broader graph/
multiplayer implementation is part of this unit.

## Published-source follow-up

Scoped commit `5ab68aab81c2e4b151b67f003268d85316ea3e0b` was normally pushed
and freshly verified on `origin/dev` at 17:20:22 UTC. The index is empty;
all 33 preserved peer files and the 14 source-freeze hashes match.
[Exact CI 37140142730](https://github.com/MikeHazeJr/perfect-dark-2/actions/runs/37140142730)
passes dependency fixtures, then fails one of the 20 storage cases before
configuration. MinGW Python emits forward-slash `D:/a/...` paths; `cmd` reads
their path components as switches in the junction fixture. The other 19 cases,
including the new byte-change regression, pass. The terminal receipt is
`contract-ci/ci-first-terminal.json`.

The fixture now explicitly converts both paths to native Windows separators
before `mklink`, using forward-slash inputs under every Windows Python so the
same conversion is exercised locally. Actual junction creation and both
traversal-rejection assertions remain required. No skip or production policy
change is introduced. The native 20-case small suite passes in 3.266 seconds,
including actual junction creation from the normalized slash inputs. The source
guard passes. Receipts are `contract-ci/junction-storage-tests-result.json`
and `junction-closeout.json`. The seconds-long headless window is released after
verifying test executor absence. Exact follow-up publication and CI are pending;
client/C++ source and the prior full native pass are unchanged.

The two-path follow-up `e0f957ac70d508291dac641fdb236c69a42fc3e9` was normally
pushed and freshly verified at 17:27:51 UTC. Its [exact CI 37140595483](https://github.com/MikeHazeJr/perfect-dark-2/actions/runs/37140595483)
still fails the same junction setup, with 19 other storage cases passing.
`contract-ci/ci-second-terminal.json` preserves the failure.
The [MinGW Python 3.14.8 ntpath implementation](https://raw.githubusercontent.com/msys2-contrib/cpython-mingw/mingw-v3.14.8/Lib/ntpath.py)
changes its preferred separator in an active MSYS2 environment, so its
`normpath` does not guarantee backslashes. This matches the
[MSYS2 Python portability documentation](https://www.msys2.org/docs/python/).

The fixture now converts separators explicitly. The CI storage step uses
PowerShell and requires standard native Windows CPython, matching the existing
Storage-Harness interpreter boundary; its interpreter path/platform is logged
and an unsuitable interpreter fails before tests. The [exact runner image](https://raw.githubusercontent.com/actions/runner-images/win25-vs2026/20260925.250/images/windows/Windows2025-VS2026-Readme.md)
already supplies Python 3.12.10. No interpreter installation, local PATH mutation
or test skip is needed. The build and dependency fixtures retain their existing
MSYS2 environment. The final native-interpreter guard and all 20 tiny storage
cases pass locally in 3.297 seconds; the source guard passes. Receipts are
`contract-ci/native-interpreter-storage-tests-result.json` and
`native-interpreter-closeout.json`. The seconds-long headless window is released
early at 17:34 UTC after executor absence. Exact three-path follow-up publication
and CI remain pending; the prior C++ full-suite/source proof is unchanged.
