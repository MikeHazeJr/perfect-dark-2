# Task Group: Shanties n Such Unity Repo Orientation
scope: Identifying the active `Shanties n Such` Unity workspace and reconstructing the current project and feature inventory from live repo files.
applies_to: cwd=D:\Game Dev\Shanties n Such\Shanties n Such; reuse_rule=reuse for repo-orientation, "what project is this?", or feature-inventory requests in this checkout; re-check the live workspace and `README.md`/`docs/` before assuming the project root or feature set is unchanged.

## Task 1: Identify the active Unity project directory and describe the project/features, success

### rollout_summary_files

- rollout_summaries/2026-06-17T03-55-25-VOEt-shanties_n_such_project_directory_and_feature_overview.md (cwd=D:\Game Dev\Shanties n Such\Shanties n Such, rollout_path=C:\Users\mikeh\.codex\sessions\2026\06\16\rollout-2026-06-16T23-55-30-019ed3b8-33cd-7771-b02f-20c9d97ba12d.jsonl, updated_at=2026-06-17T03:56:55+00:00, thread_id=019ed3b8-33cd-7771-b02f-20c9d97ba12d, verified the live workspace and reconstructed the feature set from current repo files)

### keywords

- Shanties n Such, Unity, README.md, ProjectSettings/ProjectVersion.txt, Packages/manifest.json, docs, HDRP, Netcode for GameObjects, Assets/Main.unity, procedural generation

## User preferences

- when the user asks "What is our project directory and what is our project? What features does it have?" -> give the directory first, then a concise but concrete feature inventory built from live workspace evidence rather than memory alone [Task 1]
- when the user wants repo orientation rather than implementation help -> default to a concise workspace overview, not proposed edits [Task 1]

## Reusable knowledge

- The project root for this rollout was `D:\Game Dev\Shanties n Such\Shanties n Such`, and the project is `Shanties n Such` [Task 1]
- `README.md` is the canonical high-level feature overview; `docs/*.md` are the detailed subsystem map to open next [Task 1]
- `ProjectSettings/ProjectVersion.txt` shows Unity `6000.4.11f1`, and `Packages/manifest.json` confirms HDRP plus Netcode for GameObjects [Task 1]
- The current project description from repo files is an open-world pirate sailing, survival, and crew-multiplayer adventure with a procedurally generated world, ships/sailing, quests, crafting/survival, settlements/world simulation, building, modding, and project tooling/docs [Task 1]
- `Assets/Main.unity` is the main scene called out in the README [Task 1]

## Failures and how to do differently

- Do not answer repo-orientation questions from memory alone when the live workspace is available; verify the actual working directory, Unity project markers, and current `README.md`/`docs/` first [Task 1]
- A related prior path under `C:\Users\mikeh\OneDrive\Documents\Shanties n Such` exists, so future orientation answers should verify the active workspace root before assuming which checkout the user means [Task 1]

# Task Group: Codex Mobile Windows Remote Control Pairing
scope: Diagnosing and repairing Codex Mobile remote-control pairing on Windows when the phone can connect but the PC host is not recognized as connected.
applies_to: cwd=C:\Users\mikeh\Documents\Codex\2026-06-06\i-connected-my-phone-for-use; reuse_rule=reuse for local Codex desktop/mobile pairing issues on this Windows machine; verify current app version and auth state before reapplying because the fix is account-state sensitive.

## Task 1: Diagnose why the PC was not showing as connected to Codex Mobile, success

### rollout_summary_files

- rollout_summaries/2026-06-06T15-41-33-HOCH-codex_mobile_windows_remote_control_auth_metadata_fix.md (cwd=C:\Users\mikeh\Documents\Codex\2026-06-06\i-connected-my-phone-for-use, rollout_path=C:\Users\mikeh\.codex\sessions\2026\06\06\rollout-2026-06-06T11-41-33-019e9d98-bda0-7791-9a6b-22cd2f4d331f.jsonl, updated_at=2026-06-06T17:21:10+00:00, thread_id=019e9d98-bda0-7791-9a6b-22cd2f4d331f, traced the issue to local auth metadata and verified the packaged desktop app-server came online)

### keywords

- Codex Mobile, remote control, Windows, auth.json, tokens.account_id, state_5.sqlite, remote_control_enrollments, codex doctor, app-server, 409 Conflict, Shadowbane

## Task 2: Confirm durability and regression triggers, partial

### rollout_summary_files

- rollout_summaries/2026-06-06T15-41-33-HOCH-codex_mobile_windows_remote_control_auth_metadata_fix.md (cwd=C:\Users\mikeh\Documents\Codex\2026-06-06\i-connected-my-phone-for-use, rollout_path=C:\Users\mikeh\.codex\sessions\2026\06\06\rollout-2026-06-06T11-41-33-019e9d98-bda0-7791-9a6b-22cd2f4d331f.jsonl, updated_at=2026-06-06T17:21:10+00:00, thread_id=019e9d98-bda0-7791-9a6b-22cd2f4d331f, fix held after the helper exited, but future auth resets can reintroduce it)

### keywords

- will this continue to work, auth reset, account_id null, last_seen_at, packaged app-server, backend environment list, sign-out, reinstall, update

## User preferences

- when the user says "Please fix it." after listing suspected causes -> prefer direct remediation over generic advice if the local state can be inspected and repaired [Task 1]
- when the user follows with "Will this continue to work, going forward?" -> include a concrete durability assessment and the likely regression triggers, not just the one-time fix [Task 2]

## Reusable knowledge

- For Codex Mobile remote control on Windows, the host is the Codex desktop app-server, not a Windows service [Task 1]
- `codex doctor` can show whether the app-server is running, but it may not reveal a remote-control metadata mismatch by itself [Task 1]
- `C:\Users\mikeh\.codex\auth.json` contains `tokens.account_id`; if that field is `null`, remote-control enrollment can fail even when login otherwise looks valid [Task 1]
- The enrollment cache lived in `C:\Users\mikeh\.codex\state_5.sqlite` table `remote_control_enrollments` [Task 1]
- The likely regression trigger is a future sign-out/in, reinstall, or update that rewrites `auth.json` and clears `tokens.account_id` again [Task 2]

## Failures and how to do differently

- `codex remote-control start` is not the Windows fix path here; it failed because app-server daemon lifecycle support is Unix-only in that tool path [Task 1]
- Restoring only the enrollment row was not enough; the real blocker was `tokens.account_id = null`, so check auth metadata before assuming the backend is broken [Task 1]
- If a temporary helper app-server returns `409 Conflict` with "Remote app server already online", treat that as confirmation the packaged desktop host has taken over and stop the helper [Task 1]

# Task Group: PD2 c3844 Final Proof and Readiness Closeout
scope: Verified late-stage `c3844` proof work for Public Mods runtime delivery, B-801 source-only readiness, and the final regression sweep in the PD2 repo.
applies_to: cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike; reuse_rule=reuse for PD2 parity/closeout work in this checkout when the user wants live-context-first proof, exact counts, scoped logging, and cleanup; re-check `tools/kanban/state.json` and repo-local `context/` before assuming the lane is still in the same state.

## Task 1: Prove received Public Mods `.pdmod` delivery/runtime through the strict shared archive contract, success

### rollout_summary_files

- rollout_summaries/2026-06-12T01-29-52-8SKt-c3844_public_mods_pdmod_runtime_delivery_proof.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\sessions\2026\06\11\rollout-2026-06-11T21-30-01-019eb973-2773-7143-8afe-4458430ff299.jsonl, updated_at=2026-06-12T02:14:55+00:00, thread_id=019eb973-2773-7143-8afe-4458430ff299, inbox install, enable/load parity, negative pre-install proof, and closeout hygiene were verified)

### keywords

- c3844, Public Mods, pdmod, modmgrValidateArchiveFile, modmgrInstallArchiveFile, public_mods_pdmod_install_smoke, scoped logging, mods/installed, WerFault

## Task 2: Close B-801 source-only fallback readiness while leaving c3849 Wave 7 explicitly gated, success

### rollout_summary_files

- rollout_summaries/2026-06-12T01-31-10-qXjf-c3844_b801_source_only_readiness_closeout.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\sessions\2026\06\11\rollout-2026-06-11T21-31-15-019eb974-5947-7572-a9af-825b5cdc4655.jsonl, updated_at=2026-06-12T03:04:47+00:00, thread_id=019eb974-5947-7572-a9af-825b5cdc4655, fixed narrow readiness gaps, re-ran grouped smokes, and preserved Wave 7 as an explicit future gate)

### keywords

- c3844, B-801, c3849, Wave 7, all_family_source_gate_smoke, audio_live_playback_source_smoke, base_animation_source_probe_smoke, results-20260612T025658Z.json, fatal cutover, toggle retirement

## Task 3: Run the final all-family regression sweep and record the green proof set, success

### rollout_summary_files

- rollout_summaries/2026-06-12T03-06-34-0ISs-c3844_final_all_family_regression_sweep.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\sessions\2026\06\11\rollout-2026-06-11T23-06-44-019eb9cb-b0cd-7023-911c-f784548fa7e0.jsonl, updated_at=2026-06-12T03:57:43+00:00, thread_id=019eb9cb-b0cd-7023-911c-f784548fa7e0, full validator/test/smoke/build matrix was re-run and the blockers were fixed)

### keywords

- c3844, final regression sweep, results-20260612T035120Z.json, asset_native_source_guard.py, verify_pdxxx_modder_workflow.py, asset_archive_conformance.py, verify_audio_sources.py, verify_pdmesh_sources.py, verify_pdanim_sources.py, 426/426

## User preferences

- when the user says "Read mandatory context first" for PD2 closeout work -> start from the live repo-local `context/` tree and `tools/kanban/state.json`, not from older summaries [Task 1][Task 2][Task 3]
- when the user requires scoped logging and "prevent lingering PerfectDark/PerfectDarkServer/WerFault processes" -> treat log masks and explicit process cleanup as first-class proof criteria, not afterthoughts [Task 1][Task 3]
- when the user asks for exact pass/fail counts -> preserve the per-gate and per-smoke totals instead of collapsing the report to "green" [Task 3]
- when the user says "do not implement new feature work unless a regression blocks completion" -> stay in verification/fix mode during final sweeps and only patch true blockers [Task 3]
- when the user requires updates to context, Kanban, and release notes -> treat those edits as part of the deliverable, not separate admin [Task 1][Task 2][Task 3]
- when the user wants remaining integer-only runtime limits treated as private migration debt -> preserve that distinction in closeout artifacts and do not describe it as public archive behavior [Task 2]

## Reusable knowledge

- Received Public Mods `.pdmod` delivery only matches local install behavior when the inbox path is validated by `modmgrValidateArchiveFile()` and then installed through `modmgrInstallArchiveFile()`; keep that shared contract intact for both Hub import and received-mod install [Task 1]
- The strongest runtime proof for this lane covered inbox delivery, enable prompt, install into `mods/installed`, catalog scan, and actual runtime loads from installed content, plus negative proof for public `.bin`, public `.tsv`, and invalid nested typed archives [Task 1]
- The B-801 readiness closeout fixed narrow timing/state gaps without flipping the larger c3849 Wave 7 policy. Wave 7 remains explicitly gated on default flip criteria, per-family fatal cutover, MP mismatch refusal/protocol pins, and toggle retirement [Task 2]
- The fast authoritative gate set for this lane was `python tools\asset_native_source_guard.py`, `python tools\verify_pdxxx_modder_workflow.py`, archive conformance selftest, checked-in example conformance, retained `Build\data\ntsc-final` conformance, audio/mesh/animation validators, focused tests, the smoke result JSON, and the isolated `-Target all` build [Task 2][Task 3]
- The final smoke evidence artifact was `.claude/smoke-verify-runs/results-20260612T035120Z.json`, which summarized the matrix cleanly as 9/9 tests and 426/426 assertions [Task 3]
- Repo-local `context/` files are canonical first. Sync parent mirrors only when those files actually exist there and only after the repo-local copies are updated [Task 2][Task 3]
- Keep a running simplified list of release-note bullets in `UNRELEASED.md` for completed PD2 tasks, using concise release-ready wording instead of session-log detail [ad-hoc note]
- When Mike says the log is "in build", check `C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike\Build\logs\game client` first; when he says the release folder or install directory, check `C:\Users\mikeh\Downloads\Perfect Dark 2.0\logs\game client` first [ad-hoc note]
- Related skill: skills/pd2-card-closeout/SKILL.md [Task 3]

## Failures and how to do differently

- If a Public Mods smoke exits too early, leave enough time for cold install/extraction/catalog activation before sentinel shutdown; the first run failed because the proof path had not finished [Task 1]
- If grouped source-gate smokes stall, fix the timing/state boundary instead of forcing retries: wait for the right boot/source signals and shorten path-length-sensitive generated cache names where needed [Task 2]
- If a final static guard fails after runtime work already looks correct, check for stale test-contract drift before changing runtime behavior; the `.pdarena` / `.pdhead` source-ownership failure was a stale expectation, not a new runtime regression [Task 3]
- If a smoke mixes installed `.pdmod` proof with a later loose-folder proof, clear stale installed fixture state first so the loose-folder path is actually exercised [Task 3]
- Broad `git status` and repo-wide hygiene checks can be noisy or slow in this workspace. Use narrower tracked-file checks, and do not pass parent paths outside the repo root to `git diff --check` [Task 3]

# Task Group: PD2 c3844 Workflow Hardening and Remaining-Work Planning
scope: Pre-closeout `c3844` work that hardened the `.pdmod` transport path, corrected overly broad audits, and turned the remaining goal work into parallelizable, validated prompts.
applies_to: cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike; reuse_rule=reuse for PD2 asset-pipeline parity work when the user wants narrow active-lane scope, explicit status accounting, or parallel worker prompt decomposition; re-check live board/context if the active lane changes.

## Task 1: Replace the active pack/import path with strict `.pdmod` validation and a shared install gate, success

### rollout_summary_files

- rollout_summaries/2026-06-06T14-32-59-8ANQ-c3844_s110_pdmod_import_install_and_goal_prompt_planning.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\sessions\2026\06\06\rollout-2026-06-06T10-33-04-019e9d59-f6c5-70d3-9436-810147763d72.jsonl, updated_at=2026-06-12T00:01:32+00:00, thread_id=019e9d59-f6c5-70d3-9436-810147763d72, hardened the active Hub/Public Mods transport path around strict `.pdmod` handling)

### keywords

- c3844, s110, pdmod, modmgrValidateArchiveFile, modmgrInstallArchiveFile, pdpack, file_transfer.c, test_public_mods_static.cpp, asset_native_source_guard.py

## Task 2: Quantify the remaining goal work and generate parallel `/goal` prompts, success

### rollout_summary_files

- rollout_summaries/2026-06-06T14-32-59-8ANQ-c3844_s110_pdmod_import_install_and_goal_prompt_planning.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\sessions\2026\06\06\rollout-2026-06-06T10-33-04-019e9d59-f6c5-70d3-9436-810147763d72.jsonl, updated_at=2026-06-12T00:01:32+00:00, thread_id=019e9d59-f6c5-70d3-9436-810147763d72, turned the remaining work into six packages with four parallel worker prompts and two serial follow-ups)
- rollout_summaries/2026-06-05T15-18-44-B8eY-pd2_asset_pipeline_status_parallel_split.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\sessions\2026\06\05\rollout-2026-06-05T11-18-49-019e985d-7d2d-7e52-8733-b46fbec45718.jsonl, updated_at=2026-06-05T15:23:29+00:00, thread_id=019e985d-7d2d-7e52-8733-b46fbec45718, read-only status review that split remaining work by asset family plus runtime surface)

### keywords

- c3844 Completion Gates, /goal prompts, remaining-work quantification, parallelization, serial closeout, visual audit, custom-created asset workflow, source-only fallback, final regression sweep, asset family

## Task 3: Correct an overly broad source-gap audit back to the requested asset family, uncertain

### rollout_summary_files

- rollout_summaries/2026-06-05T15-24-24-AFOg-c3844_source_audit_scope_correction.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\sessions\2026\06\05\rollout-2026-06-05T11-24-31-019e9862-af4d-7d81-a16d-73eed68a55e0.jsonl, updated_at=2026-06-06T03:31:42+00:00, thread_id=019e9862-af4d-7d81-a16d-73eed68a55e0, mostly orientation work that the user narrowed back to meshes before any fix landed)

### keywords

- meshes not messages, source-gap audit, c3844, asset_native_source_guard.py, pdsong, context/tasks.md, smoke-verify-install, scope correction

## User preferences

- when the user keeps the request on `c3844/s110` rather than broad architecture -> stay constrained to the active lane unless the user explicitly broadens scope [Task 1]
- when code changes land in this PD2 lane -> update the durable project surfaces in the same slice instead of treating board/context/release-note work as optional follow-up [Task 1]
- when the user says "Quantify EVERYTHING that needs done to get our goal complete" -> answer with explicit remaining-work packages and checklists, not a vague status summary [Task 2]
- when the user asks for "`/goal` prompts" -> default to copy-pasteable session starters that already encode scope, verification, cleanup, and closeout rules [Task 2]
- when the user says they prefer "parallel sessions" and to break work "up by asset type" -> split remaining work into narrow asset-family or runtime-surface owners rather than one monolithic session [Task 2]
- when the user corrects a sweep with "meshes*, not messages" -> narrow immediately to the named asset family and stop spending tools on adjacent surfaces [Task 3]

## Reusable knowledge

- The active Hub workflow became `.pdmod` only; the visible legacy `.pdpack` import surface was removed from the active path [Task 1]
- `modmgrValidateArchiveFile()` and `modmgrInstallArchiveFile()` are the shared strict gates for `.pdmod` import/install and Public Mods delivery, including rejection of public `.bin`, public `.tsv`, and invalid nested typed archives after root `mod.json` validation [Task 1]
- The remaining c3844 completion path was narrower than the older archive-migration notes suggested. The useful decomposition was six packages: visual audit, custom-created asset workflow proof, Public Mods/network delivery proof, source-only fallback/fatal-cutover readiness, final regression sweep, and final closeout [Task 2]
- The practical execution split was four parallel worker sessions plus two serial sessions, with the regression sweep gated on green parallel work and final closeout gated on a green sweep [Task 2]
- The important remainder was runtime trust and parity closure, not a broad archive-format rewrite; the live task ledger was a better completion source than older narrative notes [Task 2][Task 3]

## Failures and how to do differently

- If strict archive validation still behaves like generic `.zip` handling, split the user-facing extension helper so install/import paths enforce `.pdmod` only [Task 1]
- PowerShell does not accept bash-style `python - <<'PY'`; use a PowerShell here-string piped to python instead [Task 1]
- When quantifying remaining work in a lane with lots of historical green notes, separate "already closed" from "still open" aggressively so the user does not get a bloated list [Task 2]
- Parallelization should be organized around asset family plus runtime surface, not arbitrary work chunking or collision-heavy shared files [Task 2]
- Do not treat the scope-correction rollout as a fix; no source edit, test run, or build verification completed there [Task 3]

# Task Group: PD2 Asset Pipeline Planning and Decision Flow
scope: Kanban ordering, archive-format sequencing, archived decision retention, and read-only architecture guidance for the PD2 asset-pipeline migration.
applies_to: cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike; reuse_rule=reuse for PD2 asset-pipeline planning, card-order updates, or decision-surface work in this checkout; treat it as board/context guidance, not proof that a runtime feature is implemented.

## Task 1: Define clean `.pdweapon` archive format and make the freeze sequence explicit, success

### rollout_summary_files

- rollout_summaries/2026-05-22T12-51-20-xKwG-pd2_clean_weapon_format_and_format_freeze_priority.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\archived_sessions\rollout-2026-05-22T08-51-25-019e4fbd-849e-7e92-8a0d-1042167f762a.jsonl, updated_at=2026-05-22T20:25:26+00:00, thread_id=019e4fbd-849e-7e92-8a0d-1042167f762a, documented the self-contained `.pdweapon` contract and made the process order explicit)

### keywords

- pdweapon, c3824, c3832, behavior.graph.json, _meta, manifest.json, nested_payloads.json, self-contained archive, format freeze, kanban reprioritization

## Task 2: Add the shared gameplay node-editor foundation card behind the asset-pipeline lane, success

### rollout_summary_files

- rollout_summaries/2026-05-22T03-51-04-lwD5-kanban_shared_gameplay_node_editor_foundation.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\archived_sessions\rollout-2026-05-21T23-51-04-019e4dce-e3c1-7183-b352-d4056d84700f.jsonl, updated_at=2026-05-23T16:50:57+00:00, thread_id=019e4dce-e3c1-7183-b352-d4056d84700f, inserted `c3835` into the active order and mirrored the change into context docs)

### keywords

- kanban, state.json, c3835, asset pipeline, pdweapon, node editor, imgui-node-editor, priority order, context sync

## Task 3: Preserve archived asset-format decisions while converting the UI to card-specific decision workspaces, success

### rollout_summary_files

- rollout_summaries/2026-05-23T17-11-59-Njc1-card_decisions_workspace_c3839.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\archived_sessions\rollout-2026-05-23T13-12-04-019e55d2-7fe3-72e1-9031-89e7b1ba963d.jsonl, updated_at=2026-05-24T23:09:30+00:00, thread_id=019e55d2-7fe3-72e1-9031-89e7b1ba963d, preserved `c3824` decision history while seeding `c3838` implementation context)

### keywords

- Card Decisions, Asset Decisions, x_card_task_context, x_asset_archive_decisions, c3824, c3838, c3839, decision history, active_card_id

## Task 4: Review streamlining opportunities without weakening the strict archive/source contract, uncertain

### rollout_summary_files

- rollout_summaries/2026-06-04T17-22-44-MKdw-pd2_asset_archive_streamlining_review.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\sessions\2026\06\04\rollout-2026-06-04T13-22-49-019e93a8-a98e-7b22-8352-8c188a166e3e.jsonl, updated_at=2026-06-04T17:27:24+00:00, thread_id=019e93a8-a98e-7b22-8352-8c188a166e3e, read-only architecture review that highlighted consolidation seams but did not implement them)

### keywords

- read only, streamline, asset_archive_writer, asset_archive_policy, asset_mod_utility_contract, source-hashed cache, no ROM fallback, family-contract registry

## User preferences

- when the user asks "Does the card reflect the ongoing process... defining the format for each other asset type before implementing the full sweep?" -> make the process sequence explicit in the tracking card, not just the end state [Task 1]
- when the user says "everything we need for the weapon is fully self-contained" -> document the `.pdweapon` contract as a self-contained archive rule [Task 1]
- when the user-established priority chain keeps asset pipeline first, then `.pdweapon`, then mod utilities -> preserve explicit Kanban ordering rather than re-sorting by topic [Task 2]
- when the user asks whether decisions are still tracked "somewhere for it to utilize in our improvements" after a card changed -> preserve durable decision history and distinguish archived decisions from current implementation context [Task 3]
- when the user marks a review as "[Read only]" and wants the system "as simple and powerful as possible, without it being convoluted" -> keep the work advisory and bias toward consolidation, not more rule surfaces [Task 4]

## Reusable knowledge

- The clean target `.pdweapon` archive layout is `weapon.ini` plus purpose folders and `_meta/` for manifest, inventory, provenance, validation, and hashes [Task 1]
- `c3824` is the Asset Pipeline migration-plan card; `c3832` is the first concrete `.pdweapon` format card beneath it; `c3814` remains the runtime/parity card below that migration layer [Task 1]
- `tools/kanban/state.json` is the source of truth for manual priority order; when inserting a card in the middle of the queue, shift later `order` values and verify the parsed JSON order afterward [Task 2]
- `x_card_task_context.cards[card_id]` is the durable store for card-specific progress, context, memory, and decision rows; `x_asset_archive_decisions` was intentionally preserved as historical data [Task 3]
- The correct read order for the next implementation session after that migration was: `c3838` for current implementation guidance, `c3824` for approved decisions, then the finalized docs/audit [Task 3]
- The current strict contract is intentional: typed `.pdxxx` archives are editable source, `.pdmod` is transport, public asset refs are catalog IDs, generated runtime products are private source-hashed cache, and ROM fallback after extraction is a failure [Task 4]

## Failures and how to do differently

- If a card draft only describes the end state, the user may reject it as incomplete process tracking; add explicit freeze-order and sweep-order subtasks [Task 1]
- The only real risk in the node-editor priority change was corrupting active board order; verify the parsed JSON order after shifting later cards [Task 2]
- Do not treat `c3838` as the archive of the original asset decisions; `c3824` is the archived decision tracker and `c3838` is the implementation handoff [Task 3]
- Do not infer that streamlining suggestions were adopted just because the architecture review identified them; that rollout was explicitly read-only [Task 4]

# Task Group: PD2 Asset Pipeline Implementation Slices and Runtime Source Fixes
scope: Implementation slices under `c3838` plus the later runtime source/loading fix that made source-built meshes render correctly by fixing adjacent public texture loading.
applies_to: cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike; reuse_rule=reuse for PD2 asset-pipeline implementation work in this checkout when the user wants active-card execution, shared writer migration, or render-path proof; verify current card state before assuming which slice is active.

## Task 1: Land the first verified shared archive writer slice for `c3838-s1`, success

### rollout_summary_files

- rollout_summaries/2026-05-24T23-10-45-3mJR-c3838_s1_shared_archive_writer_pdlang.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\archived_sessions\rollout-2026-05-24T19-10-50-019e5c41-51ea-7f21-941d-86893cd19aab.jsonl, updated_at=2026-05-24T23:27:11+00:00, thread_id=019e5c41-51ea-7f21-941d-86893cd19aab, added the shared typed-archive writer and rewired `.pdlang` through the new contract)

### keywords

- c3838-s1, asset_archive_writer, romextract_pdlang.c, _meta/manifest.json, _meta/inventory.json, hashes.tsv, provenance.json, validation.json, source-handles.json, sha256

## Task 2: Recover `c3838` state and choose the next safe non-scenario emitter slice, mixed

### rollout_summary_files

- rollout_summaries/2026-05-24T23-41-59-LL3h-pd2_asset_pipeline_c3838_shared_writer_recovery.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\sessions\2026\05\24\rollout-2026-05-24T19-42-06-019e5c5d-ea7a-7aa0-a6a1-68be6fa6c5ef.jsonl, updated_at=2026-06-05T15:18:33+00:00, thread_id=019e5c5d-ea7a-7aa0-a6a1-68be6fa6c5ef, recovered card state and mapped remaining direct writers, but did not complete the next migration slice)

### keywords

- Continue c-3838, track your progress actively, scenario stuff for last, next action, direct modArchiveAddFileMem, romextract_pdsfx.c, romextract_pdmesh.c, explicit file search

## Task 3: Fix public texture source loading so source-built meshes render correctly, success

### rollout_summary_files

- rollout_summaries/2026-06-06T03-32-05-mPg1-pdmesh_texture_source_loading_fix.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\sessions\2026\06\05\rollout-2026-06-05T23-32-10-019e9afc-e549-7cd2-bfbd-f53eaf97576d.jsonl, updated_at=2026-06-06T06:12:13+00:00, thread_id=019e9afc-e549-7cd2-bfbd-f53eaf97576d, solved the real runtime blocker by decoding public texture images instead of feeding them into the legacy compressed-texture path)

### keywords

- meshes do not render properly in-game, pdmesh, pdtexture, mod_texture_source.c, texdecompress.c, source_texnum, RGBA32, texture.png, weapon_match_source_gate_smoke, all_family_source_gate_smoke

## User preferences

- when the user says "Begin implementation" on the active card -> start with the active slice immediately rather than reopening settled decisions [Task 1]
- when the user says "Continue c-3838. Track your progress actively, before starting a task, how you will implement it, and when you complete" -> recover current state first, show the slice plan, and keep progress visible while working [Task 2]
- when the user says "Save the remaining scenario stuff for last" -> prefer non-scenario asset-family work first unless scenario blocks the others [Task 2]
- when the user reports "the meshes don’t seem to render properly in-game" -> trace the actual runtime load/render path, not just the archive extraction path [Task 3]

## Reusable knowledge

- The shared writer seam is `port/include/asset_archive_writer.h` plus `port/src/asset_archive_writer.c`; it centralizes root descriptor writes, `_meta/*` metadata, and public-file SHA sidecars [Task 1]
- `romextract_pdlang.c` is the first base extractor already wired through the shared writer, and the test contract in `tests/test_mod_external_archive_static.cpp` already pins that metadata shape [Task 1][Task 2]
- `tools/kanban/state.json` holds the live per-card next action and should be checked when resuming long-running implementation cards like `c3838` [Task 2]
- Source-loaded mesh/render debugging in this repo can require checking adjacent asset families; the runtime render failure here was actually a texture-family source-loading bug [Task 3]
- Public `.pdtexture` rows now need `source_texnum`, and runtime texture loading should decode public `.pdtexture::texture.png` style sources into RGBA32 data before any legacy compressed-texture fallback [Task 3]

## Failures and how to do differently

- Broad repo-wide hygiene checks like full-tree `git diff --check` can time out in this workspace; scoped checks with longer timeout are safer [Task 1]
- PowerShell treated `port\src\romextract*.c` globbing literally during search; use `rg --files` plus a second filter, or explicit file lists, instead of that glob form [Task 2]
- Do not mark `c3838` complete from the first shared-writer slice; only `.pdlang` was migrated in that rollout [Task 1][Task 2]
- In large files like `texdecompress.c`, split edits into smaller exact patches instead of assuming a combined multi-anchor patch will apply cleanly [Task 3]

# Task Group: PD2 Kanban Mobile Board and Card-Scoped Session UX
scope: Phone-first Kanban access and the response-only Codex session UI layered onto the PD2 board tooling.
applies_to: cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike; reuse_rule=reuse for the local Kanban server/UI workflow when the user wants phone access, per-card headless sessions, or cleaner session transcripts; this is tooling/UI memory, not gameplay/runtime behavior.

## Task 1: Add mobile Kanban access, card editing, and card-scoped/headless Codex sessions, success

### rollout_summary_files

- rollout_summaries/2026-05-24T00-16-23-OIhl-kanban_mobile_board_and_codex_session_response_only_cleanup.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\archived_sessions\rollout-2026-05-23T20-16-28-019e5757-0f82-7be3-81d5-3d9999169023.jsonl, updated_at=2026-05-24T16:25:28+00:00, thread_id=019e5757-0f82-7be3-81d5-3d9999169023, added phone-friendly board access plus card-scoped/ad-hoc headless sessions with durable queueing)

### keywords

- kanban, mobile UI, session overlay, queued_messages, steer, headless Codex session, phone access, tools/kanban/server.py, tools/kanban/index.html

## Task 2: Simplify the visible session transcript to response-only output, success

### rollout_summary_files

- rollout_summaries/2026-05-24T00-16-23-OIhl-kanban_mobile_board_and_codex_session_response_only_cleanup.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\archived_sessions\rollout-2026-05-23T20-16-28-019e5757-0f82-7be3-81d5-3d9999169023.jsonl, updated_at=2026-05-24T16:25:28+00:00, thread_id=019e5757-0f82-7be3-81d5-3d9999169023, removed internal event noise from the visible transcript while keeping queue state separate)

### keywords

- response-only transcript, weird extra log details, last_message, queue panel, thread markers, command execution entries, stdout, stderr

## User preferences

- when the user asks to "interface with the kanban board on my phone" for viewing, rearranging, modifying, and creating cards -> treat phone Kanban as a first-class workflow, not a desktop-only convenience [Task 1]
- when the user asks to "dispatch a headless Codex session for a given card" and see "the responses etc for the card’s specific session" -> keep per-card session launch and history visible in the UI by default [Task 1]
- when the user says sessions should know "what other cards are currently being worked on simultaneously" -> include active-card and other-running-session context at launch [Task 1]
- when the user says "i only need to see the responses in there, not the weird extra log details" -> default the main transcript to response-only output, not a diagnostic event stream [Task 2]

## Reusable knowledge

- `tools/kanban/server.py` is the source of truth for session creation, resumption, queueing, and steering; the UI should reuse that server model rather than inventing a parallel state layer [Task 1]
- Card-scoped session prompts should include the target card, other Active cards, and other running sessions for context [Task 1]
- The Kanban board already had a full-screen session overlay, so new session features should extend that surface rather than inventing a separate page [Task 1]
- The visible transcript contract is now response-only: response text in the main pane, queue state in the queue panel, and raw logs left on disk for diagnostics [Task 2]

## Failures and how to do differently

- Direct follow-up messages sent while a Codex turn is running must be queued and drained later; do not overwrite or error the active turn [Task 1]
- The first cleanup pass still rendered internal event types; if a new Codex event type appears later, it should be opt-in for the visible transcript rather than rendered by default [Task 2]

# Task Group: PD2 Dev Window `.pdxxx` Asset Browser and Extractor
scope: Developer-only archive browsing/extraction tooling for `.pdxxx` assets in Dev Window v2, plus the follow-up WPF binding and freeze fix.
applies_to: cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike; reuse_rule=reuse for PD2 dev-tooling work around archive inspection, selective extraction, or Dev Window v2 UX; do not reuse for shipping/runtime features because this tooling was explicitly developer-only.

## Task 1: Add a developer-only `.pdxxx` asset browser/extractor in Dev Window v2, success

### rollout_summary_files

- rollout_summaries/2026-06-11T14-31-29-p6s0-pdxxx_assets_browser_extractor_devwindow_fix.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\sessions\2026\06\11\rollout-2026-06-11T10-31-34-019eb718-623b-7932-be3c-18398d465518.jsonl, updated_at=2026-06-11T17:03:21+00:00, thread_id=019eb718-623b-7932-be3c-18398d465518, added the crawler/extractor, CLI wrapper, and Dev Window v2 Assets tab)

### keywords

- pdxxx, Dev Window v2, ASSETS tab, selective extraction, Blender, pdmesh, .pdxxx-dev-extracts, devtools/pdxxx-asset-tool.psm1, Build\data\ntsc-final

## Task 2: Fix blank list, `System.Object[]`, and UI freeze issues in the Assets tab, success

### rollout_summary_files

- rollout_summaries/2026-06-11T14-31-29-p6s0-pdxxx_assets_browser_extractor_devwindow_fix.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\sessions\2026\06\11\rollout-2026-06-11T10-31-34-019eb718-623b-7932-be3c-18398d465518.jsonl, updated_at=2026-06-11T17:03:21+00:00, thread_id=019eb718-623b-7932-be3c-18398d465518, follow-up fix restored correct rows and moved crawling off the UI thread)

### keywords

- System.Object[], WPF, Start-AsyncPoolAction, background runspace, blank list, dropdown freeze, Get-PdxxxAssetList, 733 pdmesh rows, base:model_a51board

## User preferences

- when the user asks for archive tooling "for me as a dev, not pushed with releases or committed to GH" -> keep the output developer-only, non-shipping, and easy to clean up [Task 1]
- when the user says the tool can be added "as a tab in the Dev Window v2" -> extend the existing Dev Window before inventing a separate app [Task 1]
- when the user says "extract the assets as they are stored, do not translate them" -> preserve stored bytes and avoid conversion/regeneration unless the task is explicitly about asset creation from ROM content [Task 1]
- when the user reports a blank list, `System Object []`, or a frozen dropdown -> treat that as a correctness bug to fix immediately, not just a presentation issue [Task 2]

## Reusable knowledge

- `Build\data\ntsc-final` is the default data root when no root is specified [Task 1]
- The tool preserves nested typed archives as stored `.pdxxx` files and also expands adjacent `*.pdxxx extracted/` folders for inspection [Task 1]
- The self-test can build a temporary `.pdbody` with a nested `.pdmesh` to validate extraction without launching the game [Task 1]
- After the fix, `Build\data\ntsc-final\meshes` returned 733 `pdmesh` rows, and the first verified row was `base:model_a51board` with `base_model_a51board.pdmesh` [Task 2]
- The Assets tab should refresh on explicit action and run archive crawling on the background runspace pool while Refresh/Extract are disabled during the scan [Task 2]

## Failures and how to do differently

- Returning the whole `ArrayList` as one item leads to `System.Object[]` in WPF; validate list helpers as actual row objects before wiring them into a grid [Task 2]
- Do not run archive crawling synchronously on WPF selection-change handlers; move the crawl to the background pool and keep filter callbacks lightweight [Task 1][Task 2]

# Task Group: PD2 Gameplay and Audio Polish
scope: Narrow gameplay-facing polish changes in the PD2 repo, currently centered on Tiny Mode voice pitch behavior.
applies_to: cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike; reuse_rule=reuse for scoped gameplay or audio polish requests in this checkout when the user names a specific visible behavior to change; do not generalize this into a broad audio-system refactor.

## Task 1: Make Tiny Mode raise voice line pitch slightly, success

### rollout_summary_files

- rollout_summaries/2026-05-23T20-44-50-ot87-tiny_mode_voice_pitch.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\archived_sessions\rollout-2026-05-23T16-44-55-019e5695-5e5f-7232-9526-7e8946ce81ab.jsonl, updated_at=2026-05-23T21:02:18+00:00, thread_id=019e5695-5e5f-7232-9526-7e8946ce81ab, implemented and verified the Tiny Mode voice pitch bump)

### keywords

- CHEAT_SMALLJO, sndStart, propsnd.c, sndApplyTinyVoicePitch, PSTYPE_CHRTALK, audio pitch, build-session.ps1, run-pd-tests.ps1, watchdog timeout

## User preferences

- when the user asks for a narrow gameplay/audio polish change like "make all voice lines play at a slightly higher pitch" -> treat it as a scoped implementation, not a broad audio-system rewrite [Task 1]
- when the fix is clear from code search and repo context -> default to implement-first instead of waiting for extra approval [Task 1]
- when this repo expects live context/release-note hygiene -> update session-log/tasks/release-note state alongside code [Task 1]

## Reusable knowledge

- `CHEAT_SMALLJO` is the Tiny Mode cheat flag in `src/game/cheats.c` [Task 1]
- `src/lib/snd.c` is the central place to bias pitch for voice-config sounds; `src/game/propsnd.c` is also needed because some bark lines come through `PSTYPE_CHRTALK` [Task 1]
- The implementation used a shared helper, `sndApplyTinyVoicePitch(...)`, and a `1.12f` scale constant so the Tiny Mode voice bias stays centralized [Task 1]
- The isolated build/test wrapper workflow is the right verification path here: `build-session.ps1` for all-target build and `run-pd-tests.ps1` for focused selector tests [Task 1]

## Failures and how to do differently

- Clean builds in this repo can hit the wrapper’s 60-second active-build watchdog while linking; rerun or start with a larger `-BuildTimeoutSeconds` when needed [Task 1]
- Pre-existing dirty files can exist in context/Kanban surfaces; do not overwrite unrelated edits while syncing task-local changes [Task 1]

# Task Group: PD2 OG-ROM Backport Feasibility Framing
scope: Read-only analysis of which current PD2 PC-port features can be translated back into an OG-ROM/N64 target and which remain PC-only authoring/runtime features.
applies_to: cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike; reuse_rule=reuse for backport-feasibility questions in this checkout when the user is asking about translating current project changes into an original-hardware build; do not reuse it as an implementation plan.

## Task 1: Assess OG-ROM backport feasibility and split the features into buckets, success

### rollout_summary_files

- rollout_summaries/2026-06-04T16-00-58-7yJG-og_rom_backport_feasibility_features_vs_pc_runtime.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\sessions\2026\06\04\rollout-2026-06-04T12-01-03-019e935d-cdea-7eb1-8713-bab0b3c72b20.jsonl, updated_at=2026-06-04T16:04:21+00:00, thread_id=019e935d-cdea-7eb1-8713-bab0b3c72b20, reframed the answer around OG-ROM translation rather than PC-port dual-targeting)

### keywords

- translating our changes, OG game, ROM backport, N64, Expansion Pak, jump, Tiny Mode, dynamic spawn points, level editor, authored saves, PC-only runtime

## User preferences

- when the user clarifies "translating our changes we made on our version here as an update for the OG game" -> answer as an OG-ROM backport-feasibility question, not as a request to dual-target the PC port [Task 1]
- when the user asks about many examples at once like jump, Tiny Mode, bots, dynamic spawn points, modding, and a level editor -> give a concrete per-feature feasibility split instead of a vague yes/no [Task 1]

## Reusable knowledge

- The right mental model for OG/N64 backport analysis is "PC tools author content; ROM runs compact baked data," not "carry over the PC runtime stack" [Task 1]
- Good backport candidates are compact gameplay/data changes like jump/crouch-jump/collision correction, Tiny Mode, simple dynamic spawn rules, and modest AI/path metadata [Task 1]
- Backportable only as authored-to-ROM compilation: custom maps/levels, mod-like authored save selections, and lightweight in-game additive editing rather than full PC authoring [Task 1]
- Not realistically backportable as-is: `.pdmod` archives, zip/VFS runtime loading, ImGui menus, node editors, OpenGL/SDL, ENet/social/network distribution, JSON-heavy PC save/mod workflows, and large dynamic archives [Task 1]

## Failures and how to do differently

- If the wording is ambiguous, explicitly distinguish "ROM backport feasibility" from "PC-port implementation plan" before going deep; the first framing here was too PC-port-centric [Task 1]
- Do not let PC authoring conveniences bleed into the ROM answer; hard memory and CPU caps still dominate even with Expansion Pak assumptions [Task 1]

# Task Group: Memory Registry Export Formatting
scope: Exporting stored memories and learned context from the local Codex memory registry with strict formatting boundaries and near-verbatim preservation.
applies_to: cwd=memory-registry workflow across C:\Users\mikeh\.codex\memories; reuse_rule=reuse when the user asks for a stored-memory export or registry readout from the local memory folder; do not treat it as a raw-transcript dump unless the user explicitly broadens scope.

## Task 1: Export stored memories in the requested category order and code-block format, success

### rollout_summary_files

- rollout_summaries/2026-06-09T14-59-22-MbP0-export_stored_memories_and_context.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\sessions\2026\06\09\rollout-2026-06-09T10-59-27-019eace5-3355-7712-bac9-8d41dbfd0796.jsonl, updated_at=2026-06-09T15:03:31+00:00, thread_id=019eace5-3355-7712-bac9-8d41dbfd0796, exported registry-backed memories with preserved wording and a completeness note)

### keywords

- MEMORY.md, rollout_summaries, memory export, verbatim preservation, single code block, dated lines, Instructions, Identity, Career, Projects, Preferences

## User preferences

- when the user says "Preserve my words verbatim where possible, especially for instructions and preferences" -> keep near-verbatim phrasing for reusable instructions and preference lines in memory exports [Task 1]
- when the user asks for the fixed order "Instructions", "Identity", "Career", "Projects", "Preferences" -> preserve that category order unless the user changes it [Task 1]
- when the user says "Wrap the entire export in a single code block" and wants dated one-line entries -> default to a copy-ready single code block with dated lines and a brief completeness note afterward [Task 1]
- when the user says "Only include rules from stored memories, not from conversations" -> separate curated registry content from fresh chat/transcript material [Task 1]

## Reusable knowledge

- The primary stored-memory registry is `C:\Users\mikeh\.codex\memories\MEMORY.md` [Task 1]
- Linked evidence lives in `C:\Users\mikeh\.codex\memories\rollout_summaries`, which is where dated preference wording and task-specific project facts were pulled from [Task 1]
- The durable stored-memory surface at export time was dominated by PD2 workflow, Kanban/session UX, asset pipeline rules, OG-ROM backport framing, and direct remediation/debugging style [Task 1]
- The export boundary that worked was: curated stored memories in the code block, then a completeness statement noting that raw transcript material exists outside the export [Task 1]

## Failures and how to do differently

- Do not silently expand a stored-memory export into a raw transcript dump; keep the boundary explicit unless the user asks for transcript material too [Task 1]
