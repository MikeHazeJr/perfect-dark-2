# V-006 extractor fail-closed receipt

Date: 2026-08-12

Scope: B-957, B-958, and the extraction slice of Workbench V-006.

## Method

An isolated `v006` client/updater/tests build was produced from current source.
The first `PerfectDark.exe --extract-assets-only --portable --no-update-check
--no-sound --no-net` run started with no extracted `data` tree and completed a
cold extraction. A second warm run used `Start-Process -PassThru -Wait` to
capture the authoritative process exit code.

For the negative receipt, only the disposable install was changed. The valid
`base_theme_blue.pdtheme` was moved to a sibling backup, a directory was placed
at its exact output pathname, and only `themes/.pdextract-cache` was moved to a
sibling backup. This forced the real `.pdtheme` emitter and walker to process a
write failure without altering canonical source, ROM, or extracted data.

## Results

Clean production run: PASS, exit code 0.

- raw extraction and verification: `failed=0`;
- every typed family, including `.pdtheme`: `failed=0`;
- `BOOT: --extract-assets-only complete` reached normally.

Induced production run: expected rejection, exit code 2.

- `LOUDFAIL.EXTRACT.PDTHEME: modArchiveFinish failed` names the blocked archive;
- `romextract pdtheme: written=6 skipped=0 failed=1`;
- `LOUDFAIL.ASSETCHAIN: phase 'pdtheme' reported ... (-1)`;
- the universal walker reports one theme envelope failure;
- `LOUDFAIL.ASSETCHAIN: runtime cache build skipped because 2 ... failures`;
- `LOUDFAIL.ASSETCHAIN: boot stopped` is the terminal result.

Preserved raw logs:

- `.claude/session-builds/v006/v006-extractor-clean.log`;
- `.claude/session-builds/v006/v006-extractor-induced-failure.log`.

The focused source contracts remain `[b957]` and `[b958]`. This receipt closes
only the extraction aggregate and child-emitter failure requirements of V-006;
selected-source, save, dependency invalidation, and receive-rollback negative
receipts remain separate matrix rows.
