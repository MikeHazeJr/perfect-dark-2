# Settings metadata persistence publication

Workbench: T-MENUS-007, T-TOOLING-008. Publication owner:
`01a0f5c1-d031-77b0-a372-3391765df0d5`, model gpt-6. Inherited implementation
attribution remains gpt-6.1-sol. Mike's October 2 night grant authorizes scoped
commits and pushes to the verified existing `origin/dev` branch.

The Settings Input editor supports six named profiles and sixteen device rules.
All maximum-length rules need 3,103 payload bytes plus the terminator. Previously,
the UI and input subsystem clipped them to 1,024 bytes, the config reader split
lines at 2,048 bytes, and startup's delayed-registration buffer retained only
256 bytes. Later rules could disappear or restart with an incomplete device key.

The shared metadata contract now derives both complete string capacities.
The UI serializes both complete fields before publishing them or requesting a
save. Rejected serialization restores the accepted state. The config reader
grows its complete logical line buffer with checked allocation, retains owned
pending values until registration, and preserves boundary whitespace and quotes
through the established config format. Public config keys and delimiters remain
compatible. The isolated build/test wrappers expose `settings-tests` for the
existing native Settings save executable.

Retained passing receipts from October 1 are in
`.claude/pd-initial-integration/20261001T041916343Z-naAive/`:

- Focused profile/preview cohort: 10 cases, 82 assertions.
- Broader overlapping cohort: 246 cases, 10,033 assertions.
- Native Settings save executable: 14 cases, 236 assertions.

These overlapping counts must not be added into a unique coverage total.
The Settings tests exercise full sixteen-rule pending registration and reload,
LF/CRLF and final lines without a newline, replacement of longer pending values,
literal quotes/backslashes, rejected atomic saves, and retained following keys.
The initial missing engine-header prerequisite was corrected by using the lean
metadata header; the successful native retry is the acceptance receipt.

October 2 publication preparation reviewed the complete owned diff and retained
source identities in
`.claude/pd-initial-integration/20261002-night/settings-source-integrity.json`.
That file compares the earlier pre-build snapshot; the lean-header correction
postdates that snapshot and is not mislabeled as unchanged. No build or game was
launched for this publication preparation. The checked-in pre-commit source
guard remains enabled.

Ordinary menu traversal, UI edits followed by restart, mouse and physical-device
routing remain separate open gates. Native config persistence does not establish
those interactive outcomes. The initial integration remains partial under
T-MENUS-007; the current resource request is a bounded audio/source run followed
by separately authorized native Computer Use preview verification.
