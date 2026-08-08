# Theme Archive Wave Verification - 2026-08-08

Scope: Workbench `T-ASSETS-027`, `T-ASSETS-028`, and `T-ASSETS-029`, with
aggregate regression coverage for `T-ASSETS-024` on commit parent `0bdf8caa`.

## Frozen build receipts

- Isolated build session: `.claude/session-builds/theme-consumers`.
- Client build: PASS (`_build-headless-20260808-161911-compile-client.out.log`).
- Updater build: PASS (`_build-headless-20260808-161913-compile-updater.out.log`).
- Final `pd-tests` build after parser/conformance parity fixes: PASS
  (`_build-headless-20260808-162825-compile-tests.out.log`).

## Automated results

- `[T-ASSETS-027]`: 2 cases / 30 assertions PASS.
- `[T-ASSETS-028]`: 1 case / 16 assertions PASS.
- `[T-ASSETS-029]`: 2 cases / 29 assertions PASS.
- `[pdtheme]`: 8 cases / 103 assertions PASS.
- `[modding][pdxxx]`: 165 cases / 15,938 assertions PASS.
- Archive scanner selftest: 16 parity cases, recursion, and 8 structured
  source contracts PASS.
- Strict recursive conformance: 28 root archives, 52 checked archives, all 27
  typed families PASS.
- `tools/asset_native_source_guard.py`: PASS.
- `git diff --check`: PASS.

## Verification findings repaired

1. Nested vector `.pdfont` activation initially inspected the container
   provider path instead of the public face member. It now reads qualified
   archive-member bytes and rebuilds the live ImGui atlas.
2. Custom sequence `.pdsong` rows intentionally have no legacy numeric track.
   Theme music now obtains the catalog-owned private sequence track.
3. Strict effect `elementId` validation used a semantic ID even though the
   compositor requires it to equal the catalog-ID `menuStyle`; both parser and
   conformance contract now accept the same catalog identity.
4. The original menu/effect registries could theoretically refuse capacity
   after visible active state had already changed. Read-only capacity preflight
   now makes the active-theme replacement fail before mutation.

## Truth boundary

These receipts prove production connection and automated regression coverage.
They do not substitute for an ordinary-client edited theme swap, restart,
listen-host/network distribution, rendered comparison, physical MKB/controller,
or device-switch glyph capture. Those remain open under `T-ASSETS-030` and the
aggregate validation gates.
