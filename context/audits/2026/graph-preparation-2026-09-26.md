# Executable graph preparation checkpoint, September 26

T-MODDING-002 remains partial. Model attribution: GPT-6. Mike requested finish
the current validation and pause. No further goal work until explicit resume.

## Applied source

- Prepare an unpublished equipped candidate from one captured public weapon ZIP:
  descriptor ID, selected primary/secondary graph, settings and canonical hash.
- Keep caller-owned model/dependency leases on failure and transfer on success.
  Reject unsupported bindings rather than silently dropping their meaning.
- Preserve the original ammo object shorthand and support exactly two slots,
  including absent slots. Validate every action and gate reference before use.

This is a source-preparation API. It has no ordinary gameplay caller yet.

## Validation

Five owned compiled/test files remained unchanged from the pre-build SHA-256
receipt. The coordinated `mpfinish0923` client and tests compilation passed.
Initial `asset0923lang` all-target attempt compiled the client but failed during
updater CMake regeneration while a peer's newly listed test file was absent.
The later successful client/tests build supersedes that affected-target failure;
no successful all-target/updater result is claimed.

- Graph cohort: 59 reported cases, 1,057 assertions, zero failures/errors.
- Required `[modding][pdxxx][c3842]`: 52 reported cases, 2,825 assertions,
  three failures, zero errors. This gate remains failing.
- `python tools/asset_native_source_guard.py`: exit 0.

Local evidence under `.claude/session-builds/asset0923lang/`:
`graph0926-source-before.json`, `graph0926-junit.xml`,
`graph0926-c3842-junit.xml`, `graph0926-native-guard.log`, and
`graph0926-validation.json`. Shared build evidence:
`.claude/session-builds/mpfinish0923/mp0926-validation.json`.

Remaining static failures, preserved without relaxing assertions:

1. `test_mod_external_archive_static.cpp`: source slice depends on exact comment
   and LF matching; the scanner uses a different newline representation.
2. `test_voice_locale_source.cpp`: expects duplicated locale field literals in
   each registration path, while current code delegates public audio parsing.
3. `test_asset_native_source_contract.cpp`: expects retired WAV metadata error
   wording; conformance now derives metadata from selected WAV/MP3/Ogg bytes.

These observations identify stale checks, not complete production acceptance.
On resume, trace and test each invariant through its current shared helper.

## Resume boundary

Repair those validation contracts as one batch. Then connect ordinary catalog
publication, equipped model/file-slot lifetime, per-hand press/idle admission,
native shot/debit/presentation, selected-action gset copies and retirement.
First production proof must cover a base single-shot weapon and an edited
branching mod graph. Extraction fidelity, all-family edited-source witnesses,
standard-format creator flows, the requested mod pack and installed/network/
visual/audio/controller acceptance remain in the full September 23 closure plan.
