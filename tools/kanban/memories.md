# Task Group: PD2 Mesh and Texture Source Rendering Debugging
scope: Runtime load/render debugging for source-built meshes and adjacent texture-family loading, plus failure shields for keeping mesh-family audits tightly scoped.
applies_to: cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike; reuse_rule=reuse for PD2 `.pdmesh` / `.pdtexture` runtime-debugging or asset-family source-gap audits in this checkout; verify live context/tasks if a prompt appears to reopen already-closed source-gap work.

## Task 1: Fix public texture source loading for source-built meshes, success

### rollout_summary_files

- rollout_summaries/2026-06-06T03-32-05-mPg1-pdmesh_texture_source_loading_fix.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\sessions\2026\06\05\rollout-2026-06-05T23-32-10-019e9afc-e549-7cd2-bfbd-f53eaf97576d.jsonl, updated_at=2026-06-06T06:12:13+00:00, thread_id=019e9afc-e549-7cd2-bfbd-f53eaf97576d, public `.pdtexture` source images now decode into RGBA32 runtime texture data instead of flowing into the legacy compressed texture loader)

### keywords

- pdmesh, pdtexture, texLoad, texdecompress, source_texnum, RGBA32, FileProvider, assetcatalog, catalog-provider-static, weapon_match_source_gate_smoke, all_family_source_gate_smoke, asset_native_source_guard

## Task 2: Narrow a mesh-family source-gap audit before it drifts into unrelated surfaces, uncertain

### rollout_summary_files

- rollout_summaries/2026-06-05T15-24-24-AFOg-c3844_source_audit_scope_correction.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\sessions\2026\06\05\rollout-2026-06-05T11-24-31-019e9862-af4d-7d81-a16d-73eed68a55e0.jsonl, updated_at=2026-06-06T03:31:42+00:00, thread_id=019e9862-af4d-7d81-a16d-73eed68a55e0, user scope correction for a mesh-family search that drifted into messages/text surfaces)

### keywords

- c3844, meshes*, not messages, asset_native_source_guard, pdlang, sequence.mid, sequence.tsv, modSequenceLoad, catalogResolveMusicSequence, hudmsg, asset family narrowing, smoke-verify-install

## User preferences

- when the user reports a symptom like "assets as extracted into our archive formats are actually loaded / rendered properly" and "meshes don’t seem to render properly in-game" -> trace the actual runtime load/render path, not just extraction output [Task 1]
- when the user corrects a sweep with "meshes*, not messages" -> immediately re-anchor to the exact asset family they named and stop expanding into unrelated message/text/UI surfaces [Task 2]
- when a search starts drifting or getting too broad and the user interrupts -> stop and narrow before spending more tools [Task 2]

## Reusable knowledge

- A `.pdmesh` render symptom can be caused by adjacent texture-family loading, not only by model or OBJ/MTL extraction; in this repo, mesh/render debugging should trace the real runtime path through catalog resolution, model loading, and texture load/decode boundaries [Task 1]
- Public `.pdtexture` rows need `source_texnum`, and `src/game/texdecompress.c::texLoad()` should try the public image source path before any legacy compressed-texture fallback [Task 1]
- Public image source files such as `.png`, `.tga`, `.jpg`, `.jpeg`, and `.bmp` must not be handed to the legacy compressed texture reader; the source path lives in `port/src/mod_texture_source.c` and the catalog plumbing in `port/src/assetcatalog.c` plus `port/src/assetcatalog_scanner.c` [Task 1]
- The authoritative fast checks for this lane are `python tools\asset_native_source_guard.py`, focused catalog-provider/static tests, and the relevant smoke proofs such as `weapon_match_source_gate_smoke` and `all_family_source_gate_smoke` [Task 1]
- If a future prompt appears to reopen `c3844`, first verify the live `context/tasks.md`; this rollout found project memory already recording the c3844 runtime-source closeout as complete, with remaining open work shifted toward Scenario-last parity/nav proof surfaces [Task 2]
- For PD2 runtime diagnostics, when Mike says the log is "in build", use `C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike\Build\logs\game client` first; when he says the release/install directory, use `C:\Users\mikeh\Downloads\Perfect Dark 2.0\logs\game client` first [ad-hoc note]

## Failures and how to do differently

- If a mesh-family audit starts surfacing `pdlang`, `hudmsg`, or other message/text/HUD paths without a direct runtime link to the requested asset family, the search has drifted; narrow back to the requested family immediately [Task 2]
- No edit, build, or test in the scope-correction rollout validated a fix, so do not promote that audit into solved-state memory; use it only as a scoping/failure-shield reference [Task 2]
- In large files like `texdecompress.c`, split multi-file edits into smaller exact patches and verify anchors before assuming a combined patch will apply cleanly [Task 1]
- When the working tree is already heavily dirty, apply the requested fix around existing branch work without reverting unrelated changes just to make the tree look cleaner [Task 1]

# Task Group: PD2 Asset Pipeline Remaining Runtime Closure and Parallel Split
scope: Read-only completeness checks for the remaining PD2 asset-pipeline work, especially `c3844` runtime-parity/fallback closure and parallel-session decomposition by asset family plus runtime surface.
applies_to: cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike; reuse_rule=reuse for read-only asset-pipeline status reviews, `c3844` closure planning, and parallel worker decomposition in this checkout; verify live Kanban counts/files if the board has changed.

## Task 1: Assess migration completeness and provide a 100%-goal prompt, success

### rollout_summary_files

- rollout_summaries/2026-06-05T15-18-44-B8eY-pd2_asset_pipeline_status_parallel_split.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\sessions\2026\06\05\rollout-2026-06-05T11-18-49-019e985d-7d2d-7e52-8733-b46fbec45718.jsonl, updated_at=2026-06-05T15:23:29+00:00, thread_id=019e985d-7d2d-7e52-8733-b46fbec45718, read-only status review showing broad archive migration mostly done and `c3844` still open)

### keywords

- asset pipeline, c3844, c3842, c3843, c3841, pdsong, scenario, runtime fallback, native-source guard, tools/kanban/state.json, asset_native_source_guard.py, read only first, source-only mode

## Task 2: Recommend a parallel-session split for the remaining work, success

### rollout_summary_files

- rollout_summaries/2026-06-05T15-18-44-B8eY-pd2_asset_pipeline_status_parallel_split.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\sessions\2026\06\05\rollout-2026-06-05T11-18-49-019e985d-7d2d-7e52-8733-b46fbec45718.jsonl, updated_at=2026-06-05T15:23:29+00:00, thread_id=019e985d-7d2d-7e52-8733-b46fbec45718, parallelization guidance split by asset family plus runtime surface)

### keywords

- parallel sessions, asset type, runtime surface, pdsong, scenario, scenario_source_runtime.c, run-scenario-source-matrix.ps1, asset_native_source_guard.py, fallback audit, collision-heavy files

## User preferences

- when the user asked for a "Read only, first" assessment before any edits -> do inspection-grounded status checks before proposing implementation or changing files [Task 1]
- when the user asked for a percentage and a simple prompt to reach 100% -> frame remaining work concisely around the actual closure surface instead of giving a broad rewrite plan [Task 1]
- when the user said "I will prefer to use parallel sessions, so should perhaps break them (and their goal, validated) up by asset type." -> default broad remaining PD2 work to parallelizable slices with a clear validation boundary per worker [Task 1][Task 2]
- when remaining work spans several surfaces -> split by asset family plus runtime surface, not by arbitrary equal-sized chunks [Task 2]

## Reusable knowledge

- The broad archive-format migration is largely done; the live remainder is narrower runtime trust/parity closure under `c3844`, especially `.pdsong` native playback, Scenario-last behavior, Scenario live-route proof, and residual ROM/RomProvider fallback audit [Task 1][Task 2]
- `tools/kanban/state.json` was the authoritative live status source in this review: `c3844` active with `104` subtasks, `101` done, `3` remaining, while `c3842`, `c3843`, `c3841`, `c3838`, `c3834`, and `c3835` were already done [Task 1]
- `python tools\asset_native_source_guard.py` is a fast sanity check for this lane and returned `asset-native-source guard ok` during the review [Task 1]
- `context/pillars/modding.md` and `context/designs/modding/asset-archive-clean-formats.md` are the right context docs when reviewing source-first runtime behavior: public `.pdsong` track audio should stream before legacy sequence fallback, `ASSET_AUDIO` source-only mode should fatal on missing source, and ROM fallback after extraction is an asset-chain failure [Task 1]
- A good worker goal is: keep public editable source as the native runtime source, treat generated products as source-hashed cache, and make any ROM/RomProvider fallback after extraction fail loudly [Task 2]
- Parallel worker validation should stay focused: the source guard, the relevant `c3842`/`c3844` tests, and the smoke/matrix for that worker's asset family/runtime surface [Task 2]

## Failures and how to do differently

- Do not describe the whole migration as broadly incomplete just because `c3844` remains open; the remaining work is runtime closure, not a restart of archive extraction/migration [Task 1]
- If an old global skill/context path lookup disagrees with the repo, prefer the project-local `context/` tree and live Kanban files as the source of truth [Task 1]
- Do not split parallel sessions by arbitrary chunk size; split by asset family plus runtime surface so each worker owns a clean validation boundary [Task 2]
- Do not let multiple sessions concurrently edit `tools/asset_native_source_guard.py`, `tools/smoke-verify/run-scenario-source-matrix.ps1`, or `port/src/scenario_source_runtime.c` without a coordinator and sequential merge plan [Task 2]

# Task Group: PD2 Asset Pipeline Shared Archive Writer Foundation
scope: The first verified `c3838-s1` implementation slice for typed archive extraction, plus the proven recovery/read-order for resuming that card and choosing the next emitter-family migrations.
applies_to: cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike; reuse_rule=reuse for PD2 asset-pipeline extraction work in this checkout when `c3838` or typed `*.pdxxx` emitter migration is in scope; keep it card-slice-specific rather than treating it as full card completion.

## Task 1: Land `c3838-s1` shared archive writer foundation and `.pdlang` wiring, partial overall card / successful slice

### rollout_summary_files

- rollout_summaries/2026-05-24T23-10-45-3mJR-c3838_s1_shared_archive_writer_pdlang.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\archived_sessions\rollout-2026-05-24T19-10-50-019e5c41-51ea-7f21-941d-86893cd19aab.jsonl, updated_at=2026-05-24T23:27:11+00:00, thread_id=019e5c41-51ea-7f21-941d-86893cd19aab, first verified shared archive-writer slice for `c3838`)

### keywords

- c3838-s1, asset_archive_writer, romextract_pdlang, modarchive, _meta/manifest.json, _meta/inventory.json, _meta/hashes.tsv, _meta/provenance.json, _meta/validation.json, _meta/source-handles.json, sha256, CMakeLists.txt, [modding][pdxxx][writer][c3838]

## Task 2: Recover `c3838` state and choose the next safe slice, success

### rollout_summary_files

- rollout_summaries/2026-05-24T23-41-59-LL3h-pd2_asset_pipeline_c3838_shared_writer_recovery.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\sessions\2026\05\24\rollout-2026-05-24T19-42-06-019e5c5d-ea7a-7aa0-a6a1-68be6fa6c5ef.jsonl, updated_at=2026-06-05T15:18:33+00:00, thread_id=019e5c5d-ea7a-7aa0-a6a1-68be6fa6c5ef, live-context recovery for active `c3838` and next-slice selection)

### keywords

- c3838, c3838-s1, c3838-s2, context/tasks.md, tools/kanban/state.json, asset_archive_writer.h, asset_archive_writer.c, continue c-3838, scenario last, live context files

## Task 3: Inspect remaining emitter families against the shared writer contract, uncertain

### rollout_summary_files

- rollout_summaries/2026-05-24T23-41-59-LL3h-pd2_asset_pipeline_c3838_shared_writer_recovery.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\sessions\2026\05\24\rollout-2026-05-24T19-42-06-019e5c5d-ea7a-7aa0-a6a1-68be6fa6c5ef.jsonl, updated_at=2026-06-05T15:18:33+00:00, thread_id=019e5c5d-ea7a-7aa0-a6a1-68be6fa6c5ef, inventory of direct-writer emitters still outside the shared writer)

### keywords

- romextract_pdsfx, romextract_pdsong, romextract_pdfont, romextract_pdui, romextract_pdmesh, romextract_pdanim, romextract_pdcharacter, romextract_pdbody, romextract_pdarena, romextract_pdhead, tests/test_mod_external_archive_static.cpp, asset family coverage

## User preferences

- when the user said "You see our active card. Begin implementation." on `c3838` -> start the active implementation slice immediately instead of reopening settled archive-format decisions [Task 1]
- when the user said "Continue c-3838. Track your progress actively, before starting a task, how you will implement it, and when you complete" -> explicitly recover current state, show the implementation plan, and keep progress visible during long-running PD2 card work [Task 2]
- when the card workspace framed `c3838-s1` as the shared-writer foundation first -> follow the card's foundation-first slice order before touching other emitter families [Task 1]
- when the user said "Save the remaining scenario stuff for last, if it won’t interfere with implementing the others" -> keep non-scenario asset-family work first and defer scenario-related slices unless they block broader coverage [Task 2][Task 3]
- when the user emphasized resolving issues "for all asset types" -> choose the next slice by asset-family coverage, not just by whichever emitter is easiest [Task 3]
- when finishing PD2 implementation work, keep project state current with short release-note-ready bullets in `UNRELEASED.md`, not just session-log detail [Task 1] [ad-hoc note]

## Reusable knowledge

- `asset_archive_writer` is the shared seam over `modarchive` for typed emitters; it centralizes root descriptor output plus `_meta/manifest.json`, `_meta/inventory.json`, `_meta/hashes.tsv`, `_meta/provenance.json`, `_meta/validation.json`, `_meta/source-handles.json`, and public-file `*.sha256` sidecars [Task 1]
- `.pdlang` is the verified first migration and now uses `assetArchiveWriterInit`, `assetArchiveWriterAddDescriptor`, `assetArchiveWriterAddPublicMem`, and `assetArchiveWriterFinishMetadata` [Task 1]
- `context/README.md`, `context/working-preferences.md`, `context/constraints.md`, `context/procedures.md`, `context/tasks.md`, recent `context/session-log.md`, and `tools/kanban/state.json` are the best first reads when resuming `c3838` implementation work [Task 2]
- `tools/kanban/state.json` held the live `c3838` next action and confirmed the staged shape of the card: shared writer slice first (`c3838-s1`), then catalog/type mapping work (`c3838-s2`), then broader family coverage [Task 2]
- `CMakeLists.txt` needed an explicit add for `port/src/asset_archive_writer.c` so the test target builds the helper [Task 1]
- `tests/test_mod_external_archive_static.cpp` already contains the contract test `shared typed archive writer emits c3838 metadata contract`, so future emitter migrations should keep that metadata behavior intact instead of duplicating ad hoc archive-writing logic [Task 3]
- Remaining direct-writer emitters after `.pdlang` included `romextract_pdsfx.c`, `romextract_pdsong.c`, `romextract_pdfont.c`, `romextract_pdui.c`, `romextract_pdmesh.c`, `romextract_pdanim*.c`, `romextract_pdcharacter.c`, `romextract_pdbody.c`, `romextract_pdarena.c`, and `romextract_pdhead.c`; these are the likely next `c3838-s1` targets before `c3838-s2` [Task 3]
- Verified coverage for the first slice was the focused selector `[modding][pdxxx][writer][c3838]`, with adjacent confidence from `[modding][pdxxx][base][static][c3812]` [Task 1]
- In this repo, `run-pd-tests.ps1` / `build-session.ps1` with an isolated session id is the proven workflow for focused verification, and the temporary session build directory should be removed after use [Task 1]
- Related skill: skills/pd2-card-closeout/SKILL.md [Task 1]

## Failures and how to do differently

- If `git diff --check` times out against the full dirty tree, rerun scoped checks or allow a longer timeout before treating it as a content problem; the repo can be large enough for default hygiene checks to stall [Task 1]
- A Windows PowerShell glob like `port\src\romextract*.c` failed with `rg`; use `rg --files port/src` plus a second filter or explicit file lists instead of that glob form [Task 2][Task 3]
- Broad repo-wide `rg` scans became noisy and timed out; keep searches targeted to `port/src` and the specific emitter families in scope [Task 2][Task 3]
- Do not treat `c3838` as finished just because the shared writer landed; this only verifies the first slice, and remaining typed emitters still need migration to `asset_archive_writer` [Task 1][Task 3]

# Task Group: PD2 Asset Archive Streamlining Review
scope: Read-only architecture guidance for simplifying the PD2 asset archive/modding flow without weakening the strict typed-archive, source-only, and catalog-bound rules.
applies_to: cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike; reuse_rule=reuse for PD2 asset-pipeline review, c3842/c3844 source-rule discussions, and future simplification proposals in this checkout; keep it advisory until the repo adopts any of the suggestions.

## Task 1: Review the asset archive/modding pipeline for streamlining opportunities, read-only guidance only

### rollout_summary_files

- rollout_summaries/2026-06-04T17-22-44-MKdw-pd2_asset_archive_streamlining_review.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\sessions\2026\06\04\rollout-2026-06-04T13-22-49-019e93a8-a98e-7b22-8352-8c188a166e3e.jsonl, updated_at=2026-06-04T17:27:24+00:00, thread_id=019e93a8-a98e-7b22-8352-8c188a166e3e, read-only architecture review of streamlining options)

### keywords

- [Read only], sticking to our rules strictly, asset_archive_policy, asset_mod_utility_contract, asset_archive_writer, assetcatalog_load, asset_source_debug, modpack_pdmod, modmgr, c3842, c3844, typed archives, .pdmod, catalog IDs, dependency closure, FileProvider, source-only

## User preferences

- when the user said "[Read only]" and "sticking to our rules strictly" -> keep similar asset-pipeline reviews advisory unless implementation is explicitly requested [Task 1]
- when the user asked for something "as simple and powerful as possible, without it being convoluted" -> bias future architecture work toward consolidating duplicated rule surfaces instead of adding more special cases [Task 1]
- when extraction, architecture, referencing, loading/unloading, cataloging, add-ons, and overrides are all in scope -> answer at the system-architecture level rather than as isolated file-by-file comments [Task 1]

## Reusable knowledge

- The current strict contract remains: typed `.pdxxx` archives are the editable source, `.pdmod` is transport, public asset refs are catalog IDs, dependencies should stay self-contained, generated runtime products are private source-hashed cache, and ROM fallback after extraction is a failure [Task 1]
- `asset_archive_writer` is already the right seam for centralizing root descriptors, `_meta/*` metadata, hashes, provenance, validation, source handles, and sidecars; future simplification should continue migrating family emitters through that seam instead of creating parallel writers [Task 1]
- The main simplification opportunity is a single family-contract registry that defines extension, descriptor, required/optional slots, utility operations, dependency roles, and template hints so C helpers, Python conformance, docs, and tests stop carrying partial duplicate rule copies [Task 1]
- `c3844` Scenario hardening is evidence that this project prefers source-bound accessors over direct runtime-table use when source proof matters; keep that direction if similar source/runtime debates come up again [Task 1]

## Failures and how to do differently

- Do not treat this review as adopted implementation guidance; nothing was changed or verified in the repo during this rollout [Task 1]
- Do not "simplify" the archive system by weakening c3842/c3844 source-only and typed-archive invariants; simplify by consolidating policy surfaces and dependency-closure logic instead [Task 1]

# Task Group: PD2 OG ROM Backport Feasibility
scope: Read-only feasibility analysis for translating current PD2 PC-port changes back into an original-hardware ROM update, with per-feature classification between direct backports, PC-authoring-to-ROM workflows, and PC-runtime-only systems.
applies_to: cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike; reuse_rule=reuse for OG-game backport, original-hardware, or N64 feasibility questions in this checkout; do not reuse as a PC-port implementation plan.

## Task 1: Assess which current PD2 changes are feasible to translate back into the OG ROM target, success

### rollout_summary_files

- rollout_summaries/2026-06-04T16-00-58-7yJG-og_rom_backport_feasibility_features_vs_pc_runtime.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\sessions\2026\06\04\rollout-2026-06-04T12-01-03-019e935d-cdea-7eb1-8713-bab0b3c72b20.jsonl, updated_at=2026-06-04T16:04:21+00:00, thread_id=019e935d-cdea-7eb1-8713-bab0b3c72b20, OG-ROM feasibility framing and backport mental model)

### keywords

- OG ROM backport, translating our changes, original hardware, N64 ROM target, PC-first, source-/catalog-first, jump, Tiny Mode, collision, Forge, scenario authoring, capsule sweep, authored saves, PC-runtime-only

## Task 2: Split the requested features into direct backports, authored-to-ROM compilation, and not-practical-as-is buckets, success

### rollout_summary_files

- rollout_summaries/2026-06-04T16-00-58-7yJG-og_rom_backport_feasibility_features_vs_pc_runtime.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\sessions\2026\06\04\rollout-2026-06-04T12-01-03-019e935d-cdea-7eb1-8713-bab0b3c72b20.jsonl, updated_at=2026-06-04T16:04:21+00:00, thread_id=019e935d-cdea-7eb1-8713-bab0b3c72b20, per-feature feasibility split with hardware stipulations)

### keywords

- 4MB Expansion Pak, direct backports, authored-to-ROM compilation, `.pdmod`, zip/VFS, ImGui, OpenGL/SDL, ENet, dynamic spawn points, bot jump flags, custom maps, mission data, lightweight editing, hard caps

## User preferences

- when the user clarified "translating our changes we made on our version here as an update for the OG game, not rewriting all we have done to now build for N64" -> treat future questions here as OG-ROM backport/translation analysis, not dual-target PC-port work [Task 1]
- when the user listed jump, Tiny Mode, authored save files, level editor, collision, more bots, and dynamic spawn points together -> answer with a per-feature classification instead of a single yes/no [Task 1][Task 2]
- when the user specified original hardware "with the 4mb expansion pak" -> explicitly discuss N64 CPU/memory limits and avoid PC-era runtime assumptions [Task 2]

## Reusable knowledge

- The right mental model for this repo is: PC tools author content, ROM runs compact baked data; OG-ROM backporting should not be framed as carrying over the PC runtime stack [Task 1]
- Strong direct backport candidates are compact gameplay/data changes such as jump/crouch-jump, Tiny Mode cheat behavior, selected collision improvements, dynamic spawn point rules, and modest bot jump/path metadata [Task 1][Task 2]
- Feasible only through baked authoring are custom maps, missions, and similar content that can be compiled from PC-side source into compact ROM-friendly setup/assets; authored saves may carry compact selections/configuration, not full PC archive payloads [Task 2]
- Poor or unrealistic backport candidates as-is are anything that depends on PC filesystem/runtime infrastructure: `.pdmod`/zip/VFS loading, ImGui or node-editor UX, OpenGL/SDL, ENet/social distribution, JSON-heavy runtime parsing, full Forge-style geometry editing, or large bot-count stress tests [Task 1][Task 2]
- Even with the 4MB Expansion Pak, the OG target still needs hard caps, precomputed data, and a small runtime footprint; Expansion Pak helps budget but does not remove the architectural constraints [Task 2]

## Failures and how to do differently

- If a question mixes "our version here" with original-game wording, confirm early whether the user wants ROM-backport feasibility or a PC-port implementation plan; the initial framing in this rollout drifted too PC-centric before the user corrected it [Task 1]
- Do not conflate PC authoring tooling with original-hardware runtime feasibility; a PC-first content pipeline can still feed ROM-friendly baked data without implying the ROM can run the same tools or archive/runtime stack [Task 1][Task 2]
- Treat full Forge-like runtime editing, geometry generation, and PC-style mod browsing as separate R&D tracks rather than straightforward OG backports [Task 2]

# Task Group: PD2 Asset Pipeline Card Decisions and c3838 Handoff
scope: Kanban Card Decisions workspace behavior, archived asset-format decision retention, and the read order between archived decisions and the active implementation card.
applies_to: cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike; reuse_rule=reuse for PD2 `tools/kanban` decision-workspace and asset-pipeline card-routing questions in this checkout; verify live card ids if the board has changed again.

## Task 1: Convert Asset Decisions into a selected-card Card Decisions workspace, success

### rollout_summary_files

- rollout_summaries/2026-05-23T17-11-59-Njc1-card_decisions_workspace_c3839.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\archived_sessions\rollout-2026-05-23T13-12-04-019e55d2-7fe3-72e1-9031-89e7b1ba963d.jsonl, updated_at=2026-05-24T23:09:30+00:00, thread_id=019e55d2-7fe3-72e1-9031-89e7b1ba963d, per-card decision workspace replaced the global Asset Decisions tab)

### keywords

- Card Decisions, Asset Decisions, x_card_task_context, card_id, c3839, tools/kanban/state.json, selected-card workspace, active_card_id, HTTPServer

## Task 2: Preserve c3824 decision history and seed c3838 implementation context, success

### rollout_summary_files

- rollout_summaries/2026-05-23T17-11-59-Njc1-card_decisions_workspace_c3839.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\archived_sessions\rollout-2026-05-23T13-12-04-019e55d2-7fe3-72e1-9031-89e7b1ba963d.jsonl, updated_at=2026-05-24T23:09:30+00:00, thread_id=019e55d2-7fe3-72e1-9031-89e7b1ba963d, archived decisions preserved under c3824 while c3838 became the implementation handoff)

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

# Task Group: PD2 Asset Pipeline Format Freeze and Modding Priority Order
scope: Asset-pipeline planning decisions around the clean `.pdweapon` contract, staged format-freeze sequencing, and the active Kanban order that places reusable editor work behind archive-format and mod-utility priorities.
applies_to: cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike; reuse_rule=reuse for PD2 asset-pipeline planning, `tools/kanban` priority edits, and modding-lane context sync in this checkout; verify live card ids/order if the board has changed.

## Task 1: Define the clean `.pdweapon` format and make the freeze-before-sweep process explicit, success

### rollout_summary_files

- rollout_summaries/2026-05-22T12-51-20-xKwG-pd2_clean_weapon_format_and_format_freeze_priority.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\archived_sessions\rollout-2026-05-22T08-51-25-019e4fbd-849e-7e92-8a0d-1042167f762a.jsonl, updated_at=2026-05-22T20:25:26+00:00, thread_id=019e4fbd-849e-7e92-8a0d-1042167f762a, clean `.pdweapon` target and staged asset-family freeze process recorded)

### keywords

- c3824, c3832, pdweapon, weapon-archive-clean-format.md, self-contained archive, format freeze, extraction/examples/validators, _meta, asset pipeline, c3824-s19, c3824-s20

## Task 2: Insert `c3835` as the shared gameplay node-editor foundation behind the asset-pipeline cards, success

### rollout_summary_files

- rollout_summaries/2026-05-22T03-51-04-lwD5-kanban_shared_gameplay_node_editor_foundation.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\archived_sessions\rollout-2026-05-21T23-51-04-019e4dce-e3c1-7183-b352-d4056d84700f.jsonl, updated_at=2026-05-23T16:50:57+00:00, thread_id=019e4dce-e3c1-7183-b352-d4056d84700f, reusable gameplay node-editor foundation card placed directly behind `c3824`/`c3832`/`c3834`)

### keywords

- c3835, node editor, imgui-node-editor, typed pin colors, wires colored by the pins they connect, tools/kanban/state.json, context/pillars/modding.md, priority order, c3834

## User preferences

- when the user asked, "Does the card reflect the ongoing process, specifically, defining the format for each other asset type before implementing the full sweep?" -> make the staged workflow explicit in the tracking card instead of describing only the end state [Task 1]
- when the user said "everything we need for the weapon is fully self-contained" -> preserve the `.pdweapon` contract as a self-contained archive rule when planning related asset-family formats [Task 1]
- when the user-established priority chain kept asset pipeline first, then `.pdweapon`, then mod utilities, then the reusable node-editor foundation -> preserve the manual Kanban order instead of re-sorting by topic or novelty [Task 2]
- when work is framed as a "shared gameplay node editor foundation", treat it as reusable editor/framework architecture rather than a narrow weapon-only graph tweak [Task 2]

## Reusable knowledge

- The clean target `.pdweapon` archive layout is `weapon.ini` plus purpose folders (`models/`, `materials/`, `textures/`, `animations/`, `sounds/`, `behavior/`, `projectiles/`, `entities/`, `ui/`) and `_meta/` for manifest/inventory/provenance/validation/hashes [Task 1]
- `c3824` is the cross-asset migration-plan card, `c3832` is the first concrete clean `.pdweapon` format card beneath it, and `c3814` remains lower-priority runtime/parity work [Task 1]
- The default process in this lane is: define each asset-family format first, freeze the contracts, then rebuild extraction/examples/validators in the implementation sweep [Task 1]
- `tools/kanban/state.json` is the authoritative manual ordering source; when a new active card belongs in the middle of the queue, shift later `order` values and then mirror the resulting priority into `context/tasks.md`, `context/pillars/modding.md`, and `context/session-log.md` [Task 2]
- After these planning updates, the modding direction is: clean archive formats first (`c3824` / `c3832`), utility contracts next (`c3834`), then the reusable editor foundation (`c3835`), then broader extraction/examples/validators [Task 1][Task 2]

## Failures and how to do differently

- If the card text only describes the end state, the user may reject it as incomplete; spell out the sequencing and add explicit freeze/rebuild subtasks when the workflow itself is part of the requirement [Task 1]
- For Kanban priority inserts, do not append the new work blindly. The risk is corrupting the intended active order; place the card structurally, shift later cards, and verify the JSON still parses cleanly [Task 2]

# Task Group: PD2 Kanban Mobile Session UX and Response-only Transcript
scope: Phone-first `tools/kanban` board access, card-scoped/headless Codex session orchestration, queued follow-ups, and the response-only mobile session transcript contract.
applies_to: cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike; reuse_rule=reuse for PD2 `tools/kanban` mobile board and session UX work in this checkout; verify API routes and state card ids if the board/session model changes.

## Task 1: Mobile Kanban access, card editing, and card-scoped/headless Codex sessions, success

### rollout_summary_files

- rollout_summaries/2026-05-24T00-16-23-OIhl-kanban_mobile_board_and_codex_session_response_only_cleanup.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\archived_sessions\rollout-2026-05-23T20-16-28-019e5757-0f82-7be3-81d5-3d9999169023.jsonl, updated_at=2026-05-24T16:25:28+00:00, thread_id=019e5757-0f82-7be3-81d5-3d9999169023, phone-first board and queued session follow-up flow landed)

### keywords

- tools/kanban/server.py, tools/kanban/index.html, mobile UI, card-scoped session, headless Codex session, queued_messages, POST /api/sessions/<session-id>/queue/<message-id>/steer, cloudflared, local Kanban HTTP smoke, c3836-s11

## Task 2: Response-only session transcript cleanup, success

### rollout_summary_files

- rollout_summaries/2026-05-24T00-16-23-OIhl-kanban_mobile_board_and_codex_session_response_only_cleanup.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\archived_sessions\rollout-2026-05-23T20-16-28-019e5757-0f82-7be3-81d5-3d9999169023.jsonl, updated_at=2026-05-24T16:25:28+00:00, thread_id=019e5757-0f82-7be3-81d5-3d9999169023, mobile transcript now renders response-only output)

### keywords

- response-only transcript, last_message, command execution entries, thread markers, queue panel, response-only timeline smoke, c3836-s12, buildEventFromCodexJson, renderSessionTimeline

## User preferences

- when the user asked "I’d like to be able to interface with the kanban board on my phone; viewing, rearranginc, modifying, creating cards." -> treat phone Kanban as a first-class workflow, not a desktop-only convenience [Task 1]
- when the user asked to "dispatch a headless Codex session for a given card" and see "the responses etc for the card’s specific session" -> keep per-card session launch/history visible in the UI and include project context about other active cards/running sessions [Task 1]
- when the user said the board/sessions should be "its own tab" -> keep the session workflow explicit and user-facing rather than hiding it inside board internals [Task 1]
- when the user said "i only need to see the responses in there, not the weird extra log details" -> default the visible session timeline to response-only output, not debug/event output [Task 2]

## Reusable knowledge

- `tools/kanban/server.py` is the session source of truth: creation, resumption, queueing, drain behavior, steer behavior, and session retrieval all live there [Task 1]
- The Kanban board already has a full-screen session overlay, so new session-view features should extend that surface instead of inventing another page [Task 1]
- Card-scoped session prompts should include the target card plus other Active cards and other running sessions for context [Task 1]
- Direct follow-up messages sent while a Codex turn is already running should persist in durable session state as `queued_messages`, remain visible from mobile, and drain after the current turn finishes [Task 1]
- The mobile transcript contract is "response-only": show Codex response text and actionable question cards in the main pane, keep queue state in the separate queue panel, and leave raw logs/stdout/stderr on disk for diagnostics [Task 2]
- Card-session previews should prefer `last_message` only instead of falling back to stdout/stderr tails when the goal is a clean response preview [Task 2]
- Project-state updates around Kanban work should still be mirrored into `context/tasks.md`, `context/session-log.md`, and `UNRELEASED.md` so the workflow remains discoverable [Task 1][Task 2]

## Failures and how to do differently

- If follow-up prompts arrive while a session is already running, do not treat them as immediate requests or overwrite the active turn; queue them and drain the queue after the current turn finishes [Task 1]
- If the visible transcript still shows thread markers, turn markers, command execution items, or other internal event types, the renderer is too permissive; keep only assistant response text and choice-question cards in the transcript path [Task 2]
- If queue state needs to survive refreshes or remote/mobile usage, store it in durable session state instead of keeping it only in UI memory [Task 1]

# Task Group: PD2 Tiny Mode Voice Playback Polish
scope: Scoped gameplay/audio polish changes around Tiny Mode voice playback, including where to hook pitch changes and how to verify them in this checkout.
applies_to: cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike; reuse_rule=reuse for PD2 Tiny Mode or voice-playback behavior changes in this checkout; verify cheat ids and audio-path assumptions if the sound system changes.

## Task 1: Tiny Mode voice pitch, success

### rollout_summary_files

- rollout_summaries/2026-05-23T20-44-50-ot87-tiny_mode_voice_pitch.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\archived_sessions\rollout-2026-05-23T16-44-55-019e5695-5e5f-7232-9526-7e8946ce81ab.jsonl, updated_at=2026-05-23T21:02:18+00:00, thread_id=019e5695-5e5f-7232-9526-7e8946ce81ab, Tiny Mode now applies a slight pitch bump to voice lines and bark channels)

### keywords

- CHEAT_SMALLJO, sndApplyTinyVoicePitch, sndStart, sndAdjust, propsnd.c, PSTYPE_CHRTALK, SND_TINY_VOICE_PITCH_SCALE, voice-config dialogue, build-session.ps1, run-pd-tests.ps1, [audio][voice][tiny][static]

## User preferences

- when the user asked "When turning on the tiny mode cheat, make all voice lines play at a slightly higher pitch" -> handle similar requests as a scoped gameplay/audio polish change, not a broad audio-system rewrite [Task 1]
- when the repo context shows a clear fix and no blocker, default to implement-first instead of waiting for design discussion or a plan-only response [Task 1]
- when finishing PD2 code changes, update live context files and release notes alongside the implementation so the change is recorded in the repo's working memory [Task 1]

## Reusable knowledge

- `CHEAT_SMALLJO` is the Tiny Mode cheat flag in `src/game/cheats.c` [Task 1]
- `src/lib/snd.c` is the central hook for pitch-biasing voice starts, but `src/game/propsnd.c` also needs coverage because some bark lines start through `PSTYPE_CHRTALK` prop channels instead of the mapped voice-config route [Task 1]
- The implementation used a shared helper, `sndApplyTinyVoicePitch(s16 sound, s32 channeltype, f32 pitch)`, with a `1.12f` scale constant so the Tiny Mode bias stays centralized [Task 1]
- `sndStart()` already sees the resolved sound ref, so it is a good insertion point for global audio behavior like Tiny Mode voice pitch bias [Task 1]
- Voice-config classification here is based on audio config indices for dialogue/voice slots, not just human-readable asset labels [Task 1]
- The proven verification path was an all-target `build-session.ps1` build plus `run-pd-tests.ps1 -Selector "[audio][voice][tiny][static]"` for the focused static guards [Task 1]

## Failures and how to do differently

- If a clean `build-session` run times out on the wrapper's default 60-second active-build watchdog during linking, rerun earlier with a longer `-BuildTimeoutSeconds` instead of assuming the compile failed [Task 1]
- If the repo already has unrelated dirty files during context sync, avoid overwriting them just to make the working tree look clean; keep the implementation scoped to the requested change [Task 1]
