# Graph ownership and complete mode publication — September 27

T-MODDING-002, D-006A. Implemented and validated by Codex in coordination
session codex-f9571c12-6af53c63. Supporting API evidence only; full goal partial.

## Change

Deferred gset copies retained only action bundles, which did not keep equipped
ammo, reload commands or model leases alive. They now retain the complete exact
catalog entry, with generation-specific source lookup. Self-copy retains before
release; base overwrite, scope closure and owner retirement release everything.

Complete captured-archive preparation prepares primary and optional secondary
before transferring the caller's model lease. Both equipment generations share
that lease; a failed secondary frees partial candidates and leaves caller
ownership intact. Complete publication preflights both modes, changes both slots
before retirement callbacks, and prevents old secondary candidates resurrecting
a mode removed by a newer complete replacement.

## Verification

- Isolated `asset0923lang` client/updater build0; separate test-target build0.
- Explicit selector `[graph-v2-draft],[graph-v2-equipped],[modding][pdxxx][c3842]`:
  **108 cases, 4,018 assertions, zero failures/errors**.
- All five added regression cases are present in JUnit. They include multiple
  generated scenarios for removal and complete resource retirement.
- `python tools/asset_native_source_guard.py`:0.
- `git diff --check`:0. No changed/added files across2,010 C/C++ sources, headers,
  tests and root CMake file captured before build and checked after validation.
- Initial wildcard tag selector covered only58 cases/3,188 assertions; it is
  superseded by the explicit combined selector above, not counted cumulatively.

Local artifacts: `.claude/session-builds/asset0923lang/graph0927-source-before.json`,
`graph0927-combined-junit.xml`, `graph0927-combined-result.json`,
`graph0927-native-guard.log`, `graph0927-validation.json`. Build step logs retain
the043423 client,043510 updater and043538 tests invocations on September27.

Client SHA256: `4CCA7B09F8EA9838582827488E4EBC7452DA80552414085AE7BB0CA892E60AF0`.
Tests SHA256: `C3FFEE1CBAB59F28C5FED6F9430E062AF99A9692FC039CB0F0D949A0522344A6`.
Base HEAD was `a49b537b400784d7b8388500c9345e8c24d0888d` with owned graph
changes and peer's five-file menu candidate present. Peer changes are excluded
from this commit; these build hashes identify the integrated validation tree.

## Still required

Connect these APIs to ordinary catalog activation, define explicit idle policy,
pin native model/weapon slots, bind physical hand owners, dispatch native
actions once, and preserve selection through delayed hit delivery. Exercise
public-source edits through normal played gameplay, including replacement,
reload/switch/death/stage reset. The full asset/mod-pack closure plan remains
unchanged; this is not all-family or gameplay completion.
