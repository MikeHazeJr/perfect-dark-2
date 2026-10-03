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
