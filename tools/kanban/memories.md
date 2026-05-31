# Task Group: PD2 Asset Pipeline Card Decisions and c3838 Handoff
scope: Kanban Card Decisions workspace behavior, archived asset-format decision retention, and the read order between archived decisions and the active implementation card.
applies_to: cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike; reuse_rule=reuse for PD2 `tools/kanban` decision-workspace and asset-pipeline card-routing questions in this checkout; verify live card ids if the board has changed again.

## Task 1: Convert Asset Decisions into a selected-card Card Decisions workspace, success

### rollout_summary_files

- rollout_summaries/2026-05-23T17-11-59-Njc1-card_decisions_workspace_c3839.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\sessions\2026\05\23\rollout-2026-05-23T13-12-04-019e55d2-7fe3-72e1-9031-89e7b1ba963d.jsonl, updated_at=2026-05-24T23:09:30+00:00, thread_id=019e55d2-7fe3-72e1-9031-89e7b1ba963d, per-card decision workspace replaced the global Asset Decisions tab)

### keywords

- Card Decisions, Asset Decisions, x_card_task_context, card_id, c3839, tools/kanban/state.json, selected-card workspace, active_card_id, HTTPServer

## Task 2: Preserve c3824 decision history and seed c3838 implementation context, success

### rollout_summary_files

- rollout_summaries/2026-05-23T17-11-59-Njc1-card_decisions_workspace_c3839.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\sessions\2026\05\23\rollout-2026-05-23T13-12-04-019e55d2-7fe3-72e1-9031-89e7b1ba963d.jsonl, updated_at=2026-05-24T23:09:30+00:00, thread_id=019e55d2-7fe3-72e1-9031-89e7b1ba963d, archived decisions preserved under c3824 while c3838 became the implementation handoff)

### keywords

- c3824, c3838, x_asset_archive_decisions, x_card_task_context.cards.c3824.items, ready_to_act, approved decisions, implementation handoff, card history

## User preferences

- when the user asked whether decisions were still tracked "somewhere for it to utilize in our improvements" after the card changed -> preserve durable decision history that survives card renames/revisions and keep it queryable from the live board state [Task 1][Task 2]
- when a decision surface is tied to current work, prefer card-scoped context over a one-off global list so the active card's notes, decisions, and next actions stay reusable [Task 1]
- when archive history and current implementation context diverge, make the split explicit instead of treating the newest active card as the whole source of truth [Task 2]

## Reusable knowledge

- `x_card_task_context.cards[card_id]` is the durable store for card-specific progress/context/memory/decision rows; inactive card workspaces should be stashed there instead of deleted [Task 1]
- The old `x_asset_archive_decisions` object was intentionally preserved, not deleted, so legacy asset-format decisions still exist alongside the card-scoped workspace [Task 1][Task 2]
- The correct read order for the next asset-pipeline implementation session is: `c3838` for current implementation guidance, `c3824` / `x_card_task_context.cards.c3824` for approved asset-format decisions, then the finalized docs/audit [Task 2]
- `tools/kanban/index.html` and `tools/kanban/state.json` are the primary surfaces for this lane; the tab label is now `Card Decisions`, not `Asset Decisions` [Task 1]
- Related skill: skills/pd2-card-closeout/SKILL.md [Task 2]

## Failures and how to do differently

- If a state-seeding or JSON-editing step is being done in this Windows PowerShell environment, avoid Bash-style heredocs; PowerShell-native piping to `node` worked reliably here [Task 1]
- If standalone Kanban server smoke is flaky because of local process/logging friction, use an in-process Python `HTTPServer` smoke as the reliable fallback [Task 1]
- Do not answer a card-history question by assuming the newest active card still contains the archived decisions; inspect both the preserved legacy container and the per-card workspace map first [Task 2]

# Task Group: PD2 Asset Pipeline Shared Archive Writer Foundation
scope: The first verified `c3838-s1` implementation slice for typed archive extraction, centered on the shared archive writer and `.pdlang` migration.
applies_to: cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike; reuse_rule=reuse for PD2 asset-pipeline extraction work in this checkout when `c3838` or typed `*.pdxxx` emitter migration is in scope; keep it card-slice-specific rather than treating it as full card completion.

## Task 1: Land `c3838-s1` shared archive writer foundation and `.pdlang` wiring, partial overall card / successful slice

### rollout_summary_files

- rollout_summaries/2026-05-24T23-10-45-3mJR-c3838_s1_shared_archive_writer_pdlang.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\sessions\2026\05\24\rollout-2026-05-24T19-10-50-019e5c41-51ea-7f21-941d-86893cd19aab.jsonl, updated_at=2026-05-24T23:27:11+00:00, thread_id=019e5c41-51ea-7f21-941d-86893cd19aab, first verified shared archive-writer slice for `c3838`)

### keywords

- c3838-s1, asset_archive_writer, romextract_pdlang, modarchive, _meta/manifest.json, _meta/inventory.json, _meta/hashes.tsv, _meta/provenance.json, _meta/validation.json, _meta/source-handles.json, sha256, CMakeLists.txt, [modding][pdxxx][writer][c3838]

## User preferences

- when the user said "You see our active card. Begin implementation." on `c3838` -> start the active implementation slice immediately instead of reopening settled archive-format decisions [Task 1]
- when the card workspace framed `c3838-s1` as the shared-writer foundation first -> follow the card's foundation-first slice order before touching other emitter families [Task 1]
- when finishing PD2 implementation work, keep project state current with short release-note-ready bullets in `UNRELEASED.md`, not just session-log detail [Task 1] [ad-hoc note]

## Reusable knowledge

- `asset_archive_writer` is the shared seam over `modarchive` for typed emitters; it centralizes root descriptor output plus `_meta/manifest.json`, `_meta/inventory.json`, `_meta/hashes.tsv`, `_meta/provenance.json`, `_meta/validation.json`, `_meta/source-handles.json`, and public-file `*.sha256` sidecars [Task 1]
- `.pdlang` is the verified first migration and now uses `assetArchiveWriterInit`, `assetArchiveWriterAddDescriptor`, `assetArchiveWriterAddPublicMem`, and `assetArchiveWriterFinishMetadata` [Task 1]
- `CMakeLists.txt` needed an explicit add for `port/src/asset_archive_writer.c` so the test target builds the helper [Task 1]
- Verified coverage for this slice was the focused selector `[modding][pdxxx][writer][c3838]`, with adjacent confidence from `[modding][pdxxx][base][static][c3812]` [Task 1]
- In this repo, `run-pd-tests.ps1` / `build-session.ps1` with an isolated session id is the proven workflow for focused verification, and the temporary session build directory should be removed after use [Task 1]
- Related skill: skills/pd2-card-closeout/SKILL.md [Task 1]

## Failures and how to do differently

- If `git diff --check` times out against the full dirty tree, rerun scoped checks or allow a longer timeout before treating it as a content problem; the repo can be large enough for default hygiene checks to stall [Task 1]
- Do not treat `c3838` as finished just because the shared writer landed; this rollout only verifies the first slice, and remaining typed emitters still need migration to `asset_archive_writer` [Task 1]
