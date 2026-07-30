# Task Group: Shanties n Such Repo-local Codex Skill Lookup
scope: Finding project-authored Codex skill artifacts in the D-drive `Shanties n Such` checkout and separating repo-local tools from global memory skills or unrelated gameplay code.
applies_to: cwd=D:\Game Dev\Shanties n Such\Shanties n Such; reuse_rule=reuse for repo-local Codex skill/tooling discovery in this checkout or similar "is this in the project or only in global Codex memory?" questions; re-check the live checkout before assuming the same absence of repo-local skill files.

## Task 1: Identify project-local custom Codex skills, success

### rollout_summary_files

- rollout_summaries/2026-06-21T14-17-15-g54b-project_local_custom_skill_lookup_shanties.md (cwd=D:\Game Dev\Shanties n Such\Shanties n Such, rollout_path=C:\Users\mikeh\.codex\sessions\2026\06\21\rollout-2026-06-21T10-17-20-019eea8a-f3af-79d0-be71-b1d77b882457.jsonl, updated_at=2026-06-21T14:19:22+00:00, thread_id=019eea8a-f3af-79d0-be71-b1d77b882457, confirmed the checkout had no repo-local Codex skill files and traced the remembered skill to global memory)

### keywords

- SKILL.md, skills folder, .codex, .agents, project-local skill, repo-local skill, global memory skill, Skills.cs, CodexCoordination.ps1, CurrentTask, Title required

## User preferences

- when the user corrected the request to "skills within our project directory" and "one that we made, not a standard Codex one" -> search the active repo checkout first instead of the session/global skill list [Task 1]
- when the user is asking about skills or tooling that may exist in more than one layer -> answer explicitly whether each hit is repo-local or global, rather than mixing them together [Task 1]

## Reusable knowledge

- No repo-local Codex skill definition was present in `D:\Game Dev\Shanties n Such\Shanties n Such`: no `SKILL.md`, no tracked `skills/` directory, and empty `.codex` / `.agents` folders at the project root [Task 1]
- Broad "skill" matches in this checkout can be false positives from Unity gameplay code; the concrete file hits were `Assets/Scripts/Player/Skills.cs` and `Assets/Tests/EditMode/SkillsAndCommissionTests.cs` [Task 1]
- The remembered custom skill was likely the global memory skill at `C:\Users\mikeh\.codex\memories\skills\shanties-codex-coordination\SKILL.md`, not a project-authored file inside the checkout [Task 1]
- `D:\Game Dev\Shanties n Such\Shanties n Such\Tools\CodexCoordination\CodexCoordination.ps1` is the canonical coordination manager here; it accepts `status`, `register`, `heartbeat`, `chat`, `queue`, `start`, `finish`, `flag-stale`, `clear-stale`, `complete`, `cleanup`, `prune-chat`, and `prompt` [Task 1]
- `register` uses `-CurrentTask`, and `complete` requires `-Title` plus `-Summary` [Task 1]
- Related skill: skills/shanties-codex-coordination/SKILL.md [Task 1]

## Failures and how to do differently

- If `CodexCoordination.ps1 register` fails with an invalid parameter, check the real script contract first; this repo uses `-CurrentTask`, not `-Task` [Task 1]
- If `CodexCoordination.ps1 complete` says `Title is required for complete.`, include both `-Title` and `-Summary` instead of retrying the same call shape [Task 1]
- If searching for repo-local Codex skills returns noisy gameplay code, narrow the search to `SKILL.md`, `skills/`, `.codex`, and `.agents` instead of grepping generic `skill` terms [Task 1]

# Task Group: Shanties n Such Git Snapshot Commit and Push
scope: Safe Git-only commit/push workflow for the `Shanties n Such` Unity repo when the user wants current unpushed work committed and pushed without disturbing shared Unity coordination.
applies_to: cwd=D:\Game Dev\Shanties n Such\Shanties n Such; reuse_rule=reuse for Git snapshot commit/push work in this checkout when the task is repository-only and not Unity/editor/build/test/capture work; re-check live branch/remote state before reusing any exact commit assumptions.

## Task 1: Commit and push all currently unpushed changes safely to GitHub, success

### rollout_summary_files

- rollout_summaries/2026-06-20T17-31-36-vG8C-commit_and_push_large_unity_worktree_safely.md (cwd=D:\Game Dev\Shanties n Such\Shanties n Such, rollout_path=C:\Users\mikeh\.codex\sessions\2026\06\20\rollout-2026-06-20T13-31-43-019ee616-865c-74e2-994a-3654dd83ec76.jsonl, updated_at=2026-06-20T17:45:05+00:00, thread_id=019ee616-865c-74e2-994a-3654dd83ec76, inspected the shared coordination state first, waited out slow LFS staging safely, and verified the pushed `main` head directly)

### keywords

- git push, git add -A, git-lfs, index.lock, origin/main, fast-forward, git ls-remote, Unity repo, large snapshot, LFS upload, fd9e490

## User preferences

- when the user asked to "Commit and push all currently unpushed changes safely to GH" -> default to an inspect-first safe-push workflow instead of a blind commit/push [Task 1]
- when queue authority is in question for a Shanties task, the user asked: "Would it help for you to add yourself to the queue and do what you need to when it’s your turn and you have authority?" -> decide whether queueing is actually needed; do not take a queue slot for pure Git-only work [Task 1]

## Reusable knowledge

- `git fetch origin` only brought in a new tag (`v0.12.0`) here; `origin/main` still matched local `HEAD` before the commit [Task 1]
- The mixed snapshot was large even before staging, with `3,381` untracked files and the biggest groups under `Assets/ThirdParty`, `docs/ProceduralBuildings`, and `Assets/ShantiesNSuch` [Task 1]
- In this repo, `git add -A` across the full tree can take several minutes and may leave `git-lfs filter-process` running after a wrapper timeout; the finished index held `3,457` staged paths [Task 1]
- The safe verification chain that worked was `git rev-parse HEAD`, `git rev-parse origin/main`, `git merge-base --is-ancestor origin/main HEAD`, `git status --short --branch`, `git push origin main`, then `git ls-remote origin refs/heads/main` [Task 1]
- The push uploaded LFS objects successfully (`1150/1150`, about `1.1 GB`), and the final remote head matched commit `fd9e4901764cbcb3745a1d361768f1a23e805a5a` [Task 1]

## Failures and how to do differently

- If broad staging times out in this repo, assume slow LFS-heavy staging first and give it more time before treating it as a hard failure [Task 1]
- If `.git/index.lock` appears after a timeout, check for live `git` or `git-lfs` processes before deleting the lock; in this run the underlying add was still active and waiting was the right fix [Task 1]
- The Shanties coordination queue is for Unity/editor/build/test/capture authority, not for a pure Git snapshot commit/push [Task 1]

# Task Group: PD2 Stable Release Publish and Repair
scope: Stable release/publish work in the PD2 repo, including repo hygiene, release-script repair, and direct verification of the final GitHub release state.
applies_to: cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike; reuse_rule=reuse for PD2 stable release/tag/publish work in this checkout when the goal is a real remote release, not just a local build; re-check live GitHub/tag state and repo-local `context/` before assuming the same version or script behavior.

## Task 1: Build, tag, push, and publish Stable release `v0.1.102`, success

### rollout_summary_files

- rollout_summaries/2026-06-17T17-53-05-B6sQ-pd2_stable_release_commit_publish_and_release_script_repair.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\sessions\2026\06\17\rollout-2026-06-17T13-53-10-019ed6b7-1c16-7ae3-bfaa-1d6e2e53522c.jsonl, updated_at=2026-06-18T02:49:10+00:00, thread_id=019ed6b7-1c16-7ae3-bfaa-1d6e2e53522c, ignored huge generated smoke artifacts, repaired the release path, and published the stable GitHub release)

### keywords

- release.ps1, v0.1.102, gh release create, gh release view, .gitignore, smoke-verify-runs, body too long, commit hook, Modding - c3844, d309521a

## Task 2: Repair the release process and capture the verification rules, success

### rollout_summary_files

- rollout_summaries/2026-06-17T17-53-05-B6sQ-pd2_stable_release_commit_publish_and_release_script_repair.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\sessions\2026\06\17\rollout-2026-06-17T13-53-10-019ed6b7-1c16-7ae3-bfaa-1d6e2e53522c.jsonl, updated_at=2026-06-18T02:49:10+00:00, thread_id=019ed6b7-1c16-7ae3-bfaa-1d6e2e53522c, diagnosed the failed script publish, fixed zip pruning, and verified the final public release directly)

### keywords

- devtools/release.ps1, Select-Object -Skip, gh auth status, gh release view, release not found, HTTP 422, UNRELEASED.md, concise release notes, backups\\PerfectDark-v0.1.102-win64.zip

## User preferences

- when a PD2 release/publish run is in scope -> treat the real remote release as part of done-state, not just a local build, pushed tag, or script banner [Task 1][Task 2]
- when generated proof/cache trees under `.claude` are present during release work -> treat them as ephemeral by default and avoid bundling them into commits unless the user explicitly wants them preserved [Task 1]

## Reusable knowledge

- `git add -A` on this repo can be dominated by generated smoke/cache output; the release run measured about `474,297` untracked files and roughly `15 GB` under `.claude` before ignore rules were added [Task 1]
- The durable ignore rules for release hygiene were `.claude/smoke-verify-runs/`, `.claude/smoke-verify-install/mod-cache/`, and `.claude/smoke-verify-install/logs/` [Task 1]
- Release commit hooks enforce the exact pillar name from Kanban metadata plus a `Refs:` line that includes the subject card ID; this run required `Modding` for `c3844` [Task 1]
- `devtools/release.ps1` had a single-victim pruning bug because `Select-Object -Skip` can yield a scalar; wrapping the result with `@(...)` fixed the cleanup path [Task 1][Task 2]
- GitHub release creation can fail even when the build/sign/tag steps succeeded. The reliable post-script check is `gh release view <tag>` rather than trusting the script summary [Task 1][Task 2]
- Full `UNRELEASED.md` release notes were too large for GitHub here (`HTTP 422 ... body is too long (maximum is 125000 characters)`), but concise manual notes plus the built assets succeeded [Task 1][Task 2]
- Final published release state for this run: `v0.1.102`, target commit `d309521a1191616fc02922537b2dc5b0db5911a7`, published at `2026-06-18T02:48:42Z`, assets = zip, `.sha256`, `.sig`, `PerfectDark.exe`, `Updater.exe` [Task 1]
- The stable backup zip lived at `backups\PerfectDark-v0.1.102-win64.zip`, and the build products came from `Build\PerfectDark.exe` and `Build\Updater.exe` [Task 1]
- Keep a running simplified list of release-note bullets in `UNRELEASED.md` for completed PD2 tasks, using concise release-ready wording instead of session-log detail [ad-hoc note]

## Failures and how to do differently

- If `git add -A` stalls or times out during release prep, stage tracked changes first and only add the required untracked source/docs files after ignoring generated bulk [Task 1]
- If a release commit is rejected, check `context/designs/commit-message-standard.md` and the live Kanban metadata before retrying; the wrong pillar name and missing subject-card `Refs:` were both real blockers here [Task 1]
- If `gh release create --notes-file UNRELEASED.md` fails with `body is too long`, switch to concise notes instead of trying to force the full changelog through [Task 1][Task 2]
- If the script says COMPLETE but `gh release view <tag>` says `release not found`, trust the GitHub query, repair the path, and verify the final public state directly [Task 1][Task 2]
- Quote peeled-tag revspec checks carefully when validating retagged releases; a combined PowerShell command mangled `v0.1.102^{}` once during this run [Task 2]

# Task Group: PD2 c3844 Final Proof and Readiness Closeout
scope: Verified late-stage `c3844` proof work for Public Mods runtime delivery, B-801 source-only readiness, and the final Needler/runtime closeout sweep in the PD2 repo.
applies_to: cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike; reuse_rule=reuse for PD2 parity/closeout work in this checkout when the user wants live-context-first proof, exact counts, scoped logging, screenshot evidence, and cleanup; re-check `tools/kanban/state.json` and repo-local `context/` before assuming the lane is still in the same state.

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

## Task 3: Run the final Needler visual/runtime proof sweep and record the closeout evidence, success

### rollout_summary_files

- rollout_summaries/2026-06-12T03-06-34-0ISs-needler_visual_proof_final_c3844_sweep.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\sessions\2026\06\11\rollout-2026-06-11T23-06-44-019eb9cb-b0cd-7023-911c-f784548fa7e0.jsonl, updated_at=2026-06-18T00:01:26+00:00, thread_id=019eb9cb-b0cd-7023-911c-f784548fa7e0, offline validators passed, stale Public Mods assertions were fixed, Needler runtime proof passed 43/43, and screenshot/context closeout landed)

### keywords

- c3844, needler, results-20260617T235837Z.json, needler_graph_runtime_visual_smoke, screenshot proof, BONDGUN.SOURCE, MODASSET.RENDER, NEEDLER SOURCE MODEL RENDERED, B-933, asset_native_source_guard.py

## User preferences

- when the user says "Read mandatory context first" for PD2 closeout work -> start from the live repo-local `context/` tree and `tools/kanban/state.json`, not from older summaries [Task 1][Task 2][Task 3]
- when the user requires scoped logging and "prevent lingering PerfectDark/PerfectDarkServer/WerFault processes" -> treat log masks and explicit process cleanup as first-class proof criteria, not afterthoughts [Task 1][Task 3]
- when the user asks for exact pass/fail counts -> preserve the per-gate and per-smoke totals instead of collapsing the report to "green" [Task 3]
- when the user asks to "verify that things are loading properly via both logging and also visual inspection of screenshots" -> keep both log evidence and inspectable screenshot artifacts for final proof runs [Task 3]
- when the user says "do not implement new feature work unless a regression blocks completion" -> stay in verification/fix mode during final sweeps and only patch true blockers [Task 3]
- when the user requires updates to context, Kanban, and release notes -> treat those edits as part of the deliverable, not separate admin [Task 1][Task 2][Task 3]
- when the user wants remaining integer-only runtime limits treated as private migration debt -> preserve that distinction in closeout artifacts and do not describe it as public archive behavior [Task 2]

## Reusable knowledge

- Received Public Mods `.pdmod` delivery only matches local install behavior when the inbox path is validated by `modmgrValidateArchiveFile()` and then installed through `modmgrInstallArchiveFile()`; keep that shared contract intact for both Hub import and received-mod install [Task 1]
- The strongest runtime proof for this lane covered inbox delivery, enable prompt, install into `mods/installed`, catalog scan, and actual runtime loads from installed content, plus negative proof for public `.bin`, public `.tsv`, and invalid nested typed archives [Task 1]
- The B-801 readiness closeout fixed narrow timing/state gaps without flipping the larger c3849 Wave 7 policy. Wave 7 remains explicitly gated on default flip criteria, per-family fatal cutover, MP mismatch refusal/protocol pins, and toggle retirement [Task 2]
- The fast authoritative gate set for this lane was `python tools\asset_native_source_guard.py`, `python tools\verify_pdxxx_modder_workflow.py`, archive conformance selftest, checked-in example conformance, retained `Build\data\ntsc-final` conformance, audio/mesh/animation validators, focused tests, targeted smoke result JSON, and the isolated build-session wrapper [Task 2][Task 3]
- The updated final proof artifact for this lane was `.claude/smoke-verify-runs/results-20260617T235837Z.json` plus screenshots in `.claude/smoke-verify-runs/screenshots/20260617T195640-needler_graph_runtime_visual_smoke/`, with `needler_graph_runtime_visual_smoke` passing `43/43` [Task 3]
- The concise proof log chain to grep first was `BONDGUN.SOURCE`, `MODASSET.RENDER`, and `NEEDLER SOURCE MODEL RENDERED`; broad smoke-log greps were too noisy [Task 3]
- Repo-local `context/` files are canonical first. Sync parent mirrors only when those files actually exist there and only after the repo-local copies are updated [Task 2][Task 3]
- When Mike says the log is "in build", check `C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike\Build\logs\game client` first; when he says the release folder or install directory, check `C:\Users\mikeh\Downloads\Perfect Dark 2.0\logs\game client` first [ad-hoc note]
- Related skill: skills/pd2-card-closeout/SKILL.md [Task 3]

## Failures and how to do differently

- If a Public Mods smoke exits too early, leave enough time for cold install/extraction/catalog activation before sentinel shutdown; the first run failed because the proof path had not finished [Task 1]
- If grouped source-gate smokes stall, fix the timing/state boundary instead of forcing retries: wait for the right boot/source signals and shorten path-length-sensitive generated cache names where needed [Task 2]
- If a final static guard fails after runtime work already looks correct, check for stale test-contract drift before changing runtime behavior; the `.pdarena` / `.pdhead` source-ownership failure was a stale expectation, not a new runtime regression [Task 3]
- If screenshot-proof notes still say the visual inspection is incomplete after artifacts were reviewed, correct the context files before closing the lane [Task 3]
- Broad `git status` and repo-wide hygiene checks can be noisy or slow in this workspace. Use narrower tracked-file checks, and do not pass parent paths outside the repo root to `git diff --check` [Task 3]

# Task Group: Shanties n Such Codex Coordination Across Checkouts
scope: Shared Codex session coordination, queueing, stale handling, and cross-checkout workflow wiring for the `Shanties n Such` Unity project.
applies_to: cwd family=`D:\Game Dev\Shanties n Such\Shanties n Such` and `C:\Users\mikeh\OneDrive\Documents\Shanties n Such`; reuse_rule=reuse for Codex session startup, queue/compile coordination, or coordination-tooling changes across Shanties checkouts; verify which checkout is active and whether it shares the canonical `.codex-coordination` root before changing docs or queue behavior.

## Task 1: Create and refine the canonical shared coordination hub with FIFO queueing, durable results, and stale handling, success

### rollout_summary_files

- rollout_summaries/2026-06-17T03-55-25-VOEt-shanties_codex_coordination_fifo_queue_stale_result_logging.md (cwd=D:\Game Dev\Shanties n Such\Shanties n Such, rollout_path=C:\Users\mikeh\.codex\sessions\2026\06\16\rollout-2026-06-16T23-55-30-019ed3b8-33cd-7771-b02f-20c9d97ba12d.jsonl, updated_at=2026-06-19T13:50:49+00:00, thread_id=019ed3b8-33cd-7771-b02f-20c9d97ba12d, canonical coordination hub with FIFO queue, queue-results log, and stale-flag validation)

### keywords

- Shanties n Such, Codex coordination, FIFO queue, queue-results.md, flag-stale, clear-stale, heartbeat, AGENTS.md, docs/CODEX_COORDINATION.md, schtasks.exe, SHANTIES_CODEX_COORDINATION_ROOT

## Task 2: Port the shared coordination workflow into the OneDrive checkout and enforce queue use for future sessions, success

### rollout_summary_files

- rollout_summaries/2026-06-19T21-22-33-3oqK-shanties_asset_replacement_tracker_and_codex_queue_port.md (cwd=C:\Users\mikeh\OneDrive\Documents\Shanties n Such, rollout_path=C:\Users\mikeh\.codex\sessions\2026\06\19\rollout-2026-06-19T17-22-33-019ee1c3-9cf3-7871-924b-6788d8b5b722.jsonl, updated_at=2026-06-19T21:35:16+00:00, thread_id=019ee1c3-9cf3-7871-924b-6788d8b5b722, ported the wrapper/docs into the OneDrive checkout and pointed them at the canonical shared queue root)

### keywords

- OneDrive checkout, compile queue, shared queue root, Tools/CodexCoordination/CodexCoordination.ps1, Docs/CODEX_COORDINATION.md, AGENTS.md, codex-asset-tracker-019ee1c3, q_20260619210514275_44e2587d, q_20260619211319457_086c4cea

## User preferences

- when the user asks for a way for “different Codex sessions … to be able to communicate and coordinate,” including goals, plans, current tasks, chat, queueing, and completed-task archiving -> default to a durable repo-local shared hub, not ad hoc per-thread coordination [Task 1]
- when the user asks for “hooks” so “all tasks use it” or says new sessions should “see and use it automatically” -> put the workflow in repo-surface files like `AGENTS.md` and `Docs/CODEX_COORDINATION.md`, not just in a chat explanation [Task 1][Task 2]
- when the user asks for “a prompt I can then share with any sessions” -> keep a copy-pasteable startup prompt current in the repo docs/tool output [Task 1]
- when queue behavior is being designed, the user corrected it to “queue at any time,” “behind whatever else is in front,” and “no timer” -> future queue changes should preserve FIFO visibility rather than time-sliced lease language [Task 1]
- when compile/import/build/test/capture work is in scope for Shanties checkouts, the user expects the queue system to be mandatory so sessions do not collide [Task 2]

## Reusable knowledge

- The canonical coordination manager is `D:\Game Dev\Shanties n Such\Shanties n Such\Tools\CodexCoordination\CodexCoordination.ps1`, and the live shared state is `.codex-coordination\` under that checkout unless `SHANTIES_CODEX_COORDINATION_ROOT` overrides it for testing [Task 1]
- Repo hooks live in `AGENTS.md` and `docs/CODEX_COORDINATION.md` in the canonical checkout; the OneDrive checkout mirrors the wrapper/docs under `Tools/CodexCoordination\` and `Docs/CODEX_COORDINATION.md` while sharing the canonical queue root [Task 1][Task 2]
- The queue is FIFO per resource with a monotonic `order`; `status` reports `next` or queue position, `finish` immediately clears live occupancy, and durable results are appended to `.codex-coordination\queue-results.md` [Task 1]
- `flag-stale` and `clear-stale` are part of the coordination contract, and `heartbeat` is the validation point that surfaces or auto-clears stale items only when the owner session is gone, done, or idle; `waiting_approval` sessions are intentionally preserved [Task 1]
- The OneDrive checkout should reuse the canonical shared root at `D:\Game Dev\Shanties n Such\Shanties n Such\.codex-coordination` instead of creating a second queue, so sessions across both checkouts stay coordinated [Task 2]
- The required session flow visible in the OneDrive port is `status`, `register`, queue expensive work, `start`, `finish`, `heartbeat`, `complete`, with compile/import verification explicitly queued [Task 2]
- Related skill: skills/shanties-codex-coordination/SKILL.md [Task 1][Task 2]

## Failures and how to do differently

- If Windows task installation through `New-ScheduledTaskAction` / `New-ScheduledTaskTrigger` / `New-ScheduledTaskSettingsSet` fails with `Invalid class`, switch immediately to the working `schtasks.exe` fallback instead of assuming the cleanup task cannot be installed [Task 1]
- If PowerShell JSON state lookups start failing with singleton-object errors like `The property 'Count' cannot be found on this object` or `The property 'status' cannot be found on this object`, normalize singleton results through helper accessors and wrap scalar results with `@(...)` where list behavior is required [Task 1]
- If same-second queue requests both appear as `next`, the sort key is too weak; add or preserve a separate monotonic sequence instead of relying only on timestamps [Task 1]
- If `status` prints blank stale lines, check PowerShell condition grouping; the fix here was explicit parentheses around whitespace/null checks [Task 1]
- If a checkout appears to lack queue tooling, verify whether it should share another checkout’s canonical coordination root before creating new state; the OneDrive port only worked cleanly after reusing the older D-drive queue [Task 2]

# Task Group: Shanties n Such Asset Replacement Tracker
scope: Unity editor tooling and validation for tracking procedural stand-ins that still need authored assets in the OneDrive `Shanties n Such` checkout.
applies_to: cwd=C:\Users\mikeh\OneDrive\Documents\Shanties n Such; reuse_rule=reuse for placeholder-art tracking, seeded replacement-request updates, or editor validation work in this checkout; verify the current asset-tracking paths and compile state before assuming the same menu items or tracker contents.

## Task 1: Add the in-editor asset replacement tracker and placeholder validation hook, success

### rollout_summary_files

- rollout_summaries/2026-06-19T21-22-33-3oqK-shanties_asset_replacement_tracker_and_codex_queue_port.md (cwd=C:\Users\mikeh\OneDrive\Documents\Shanties n Such, rollout_path=C:\Users\mikeh\.codex\sessions\2026\06\19\rollout-2026-06-19T17-22-33-019ee1c3-9cf3-7871-924b-6788d8b5b722.jsonl, updated_at=2026-06-19T21:35:16+00:00, thread_id=019ee1c3-9cf3-7871-924b-6788d8b5b722, added the tracker asset, editor window, scene scan, and placeholder validation hook)

### keywords

- AssetReplacementTracker.asset, ReplacementAssetTrackerEditorUtility, PlaceholderAssetMarker, Tools/Shanties n Such/Asset Replacement Tracker, procedural stand-ins, mangrove, dock board, roof shingle, BuildingSettlementLayoutPlanner.cs, asset-replacement-tracker-import.log

## User preferences

- when the user said “use procedural ones for now and make a live tracker (accessible in Unity’s Shanties menu dropdown) where I can see what is needed to replace” -> future art-placeholder work should prefer a live in-editor tracker over informal notes or hidden TODOs [Task 1]
- when the user named structures, mangrove trees, boards, shingles, ship/vehicle builder parts, player pieces, props, tools, weapons, and audio -> seed or update replacement requests by concrete asset/system buckets instead of one generic placeholder category [Task 1]
- when the user asked to “create a hook to ensure ash sessions are using it appropriately” -> include an explicit validation/check flow, not just a data asset [Task 1]

## Reusable knowledge

- The tracker asset path is `Assets/ShantiesNSuch/AssetTracking/AssetReplacementTracker.asset`, and the editor menu path is `Tools/Shanties n Such/Asset Replacement Tracker/...` [Task 1]
- New code introduced in this rollout included `ReplacementAssetRequest`, `ReplacementAssetTracker`, `PlaceholderAssetMarker`, `ReplacementAssetTrackerEditorUtility`, and `ReplacementAssetTrackerWindow` [Task 1]
- The seeded starter requests covered thatch roof material, weathered board material, mangrove bark/leaf materials, dock board prefab, roof shingle prefab, ship/vehicle structural pieces, player building kit pieces, shared props/tools/weapons, and world/construction audio placeholders [Task 1]
- The validation hook scans open scenes for `PlaceholderAssetMarker` usage and flags placeholder-looking renderer materials that are missing a marker [Task 1]
- Unity batch verification succeeded after compile repair; the headless creation path is `ReplacementAssetTrackerEditorUtility.CreateOrRefreshTrackerAsset`, and the final log line confirmed `Seeded 9 starter request(s).` [Task 1]

## Failures and how to do differently

- If the first Unity batch run creates the tracker asset but seeds `0` requests, let import/compile finish and rerun the creation step; the first pass here executed before the newer seeding path was active [Task 1]
- If Unity compilation fails while landing the tracker, check for pre-existing repo blockers before blaming the new feature; `BuildingSettlementLayoutPlanner.cs` was already broken until the missing terrain helper methods were restored [Task 1]
- If broad source-search patterns miss tracker files, narrow the search to `Assets/ShantiesNSuch/AssetTracking` instead of assuming the files were not created [Task 1]

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
