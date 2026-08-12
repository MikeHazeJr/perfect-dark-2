# T-ASSETS-030 real-peer theme distribution evidence

Date: 2026-08-12

## Outcome

The source-frozen two-peer scenario passed 67/67 and exited cleanly. The host
started from a clean install containing the editable typed example package;
the client started from a separate clean install containing only its saved
`example:tri_theme` selection. The host distributed the exact package, the
client verified its SHA-256 digest and admitted it, and identical nested
UI/font/SFX/music content reused the canonical catalog rows without weakening
type or content collision checks.

After hot registration, the client retried the preserved selection and applied
the theme from the received package path. The vector font and every nested role
were active before READY and match entry. At clean shutdown the client retained
one complete theme closure, released its parent and four children exactly once,
and only then retired the temporary package. No missing-row or double-release
diagnostic appeared.

Machine-readable result:
`context/evidence/2026-08-12-t-assets-030-real-peer-theme-result.json`.
Original smoke result:
`.claude/smoke-verify-runs/results-20260812T131648Z.json`.

## Bugs closed by the receipt

- B-1050: a saved theme absent during initial boot did not retry after a peer
  package hot-registered it.
- B-1051: folder admission could report success after typed scanning rejected
  the package.
- B-1052: identical nested content from separate qualified paths was treated as
  a collision instead of sharing the canonical row.
- B-1053: shutdown retired temporary packages before releasing the active theme
  closure.

## Automated verification

- Isolated client, updater, and test builds passed from the final fingerprint.
- `[b1050]` passed 7 assertions / 1 case.
- `[b1051]` passed 9 assertions / 1 case.
- `[b1052]` passed 9 assertions / 1 case.
- `[b1053]` passed 7 assertions / 1 case.
- `[T-ASSETS-030]` passed 241 assertions / 9 cases.
- `[pdtheme]` passed 368 assertions / 18 cases.
- `[t-networking-009]` passed 89 assertions / 3 cases.
- `[modding][pdxxx]` passed 18,659 assertions / 244 cases.
- The complete suite passed 56,586 assertions / 1,030 cases.
- Conformance selftest passed 16 parity, recursion, and 9 structured contracts.
- Strict examples conformance passed 28 roots / 52 recursive archives across
  all 27 public families.
- `asset_native_source_guard.py`, scoped diff check, and post-smoke source and
  binary fingerprint checks passed.

## Remaining T-ASSETS-030 boundary

The theme archive, restart, MKB, lifecycle, and real-peer package paths are now
proved. T-ASSETS-030 remains partial because a real physical controller has not
yet produced navigation/button transitions, and a live keyboard-to-controller
device switch with corresponding glyph transition still requires user action.
