# PD2 Reviewed Memory Rebuild

Generated from tools/kanban/memory-review.json after Mike review on 2026-05-20.
Entries with status remove are excluded.

<!-- mem-002: adjust -->
# Task Group: PD2 Mod Pipeline: Typed pdxxx Asset Archives

scope: Mod content authoring, sharing, and runtime consumption in `C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike`.
applies_to: Future PD2 mod-pipeline, content extraction, Public Mods, and online-required-content work.

## Durable Rules

- All game content should be saved per asset as the relevant typed `*.pdxxx` file.
- Each `.pdwpn`, `.pdhead`, `.pdbody`, `.pdarena`, `.pdmesh`, `.pdanim`, `.pdsfx`, `.pdvoice`, `.pdsong`, `.pdui`, `.pdfont`, `.pdlang`, and similar typed file is a self-contained archive for that asset.
- Changing a typed asset archive extension to `.zip` should expose the descriptor plus all authored source files needed to edit or clone that asset.
- Authored files inside typed archives should be modern and accessible: INI/TSV metadata, GLTF/GLB/OBJ models/maps/animations, PNG/TGA textures, WAV/OGG/MP3 audio, TTF/OTF fonts, and related readable files.
- The engine should use those files natively at the game-facing boundary. If native use requires conversion in either direction, build the two-way conversion pipeline rather than shipping opaque authored blobs.
- `.pdmod` is transport only for Public Mods, sharing, and online-required delivery. It is not the primary authoring surface.
- Authored `.bin` payloads are invalid in modder-facing archives. Runtime cache may be private, readable, rebuildable, and deleteable, but it is not shipped as the source of truth.

<!-- mem-004: adjust -->
# Task Group: PD2 F6 Debug Completion Semantics

scope: F6 debug hotkey behavior in Campaign and Combat Simulator.
applies_to: Future debug-hotkey, mission-completion, and campaign-end routing work.

## Durable Rules

- F6 in Campaign and F6 in Combat Simulator are separate behaviors.
- Campaign F6 should behave like real mission completion, not like a loose objective/HUD shortcut.
- Campaign completion must set all appropriate mission-completion flags and state, including difficulty-active objectives, death/abort state, end-stage flow, and final-campaign Credits routing.
- Combat Simulator-only debug fallbacks, such as bot-freeze behavior, must remain separate and gated to the correct context.
- If Mike reserves runtime validation for himself, record the state as build/static verified and Mike-pending runtime playtest.

<!-- mem-005: adjust -->
# Task Group: PD2 Press/Hold Input Semantics

scope: Press-vs-hold input behavior across gameplay and UI.
applies_to: Future action-map, interaction, firing, vehicle, weapon, and UI input work.

## Durable Rules

- Press and hold are the same physical input until release timing or the held threshold decides the semantic result.
- Releasing before the held threshold is a press.
- Crossing the held threshold is a held input.
- Held input consumption depends on the action, not a single global rule.
- Door/open interactions can consume the held action immediately after threshold activation.
- Sustained actions such as full-auto fire should remain active until a natural stop condition: release, empty magazine, player death, round end, weapon change, vehicle entry, or similar state transition.
- UI progress and gameplay consumption must agree; fixing only the visible prompt is incomplete.

<!-- mem-006: keep -->
# Task Group: Perfect Dark 2 Skedar benchmark parity and surface locomotion

scope: Skedar benchmark behavior in `C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike`, especially CPU/GPU parity on `base:mp_skedar`, benchmark-local jump/surface intent sharing, and keeping benchmark-only fixes separate from normal Combat Sim AI.
applies_to: cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike; reuse_rule=reuse for future PD2 Swarm benchmark or Skedar wall/jump parity work in this checkout family, but treat exact smoke fixtures, telemetry counts, and current default benchmark mode as branch/time specific

## Task 1: Investigate GPU/CPU Swarm parity and wall/jump behavior before editing, partial

### rollout_summary_files

- rollout_summaries/2026-05-18T16-48-33-DSex-skedar_gpu_benchmark_bot_parity_investigation.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\sessions\2026\05\18\rollout-2026-05-18T12-48-33-019e3bfd-4177-7621-bd15-d5e158a3e865.jsonl, updated_at=2026-05-18T16:53:57+00:00, thread_id=019e3bfd-4177-7621-bd15-d5e158a3e865, structural parity diagnosis and fix-plan boundary)

### keywords

- Swarm, GPU_POS_ONLY, GPU_FULL, bot parity, wall-loco, surface_loco, chrSurfaceLocoTick, chrTrySkJump, TESTSCEN, ACTION_TESTSCEN_GPU_FULL_TOGGLE, swarm_cpu_smoke.json, swarm_gpu_smoke.json

## Task 2: Implement and verify benchmark-local CPU/GPU parity on `base:mp_skedar`, success

### rollout_summary_files

- rollout_summaries/2026-05-18T16-16-37-8nu7-skedar_swarm_behavior_parity_and_surface_locomotion.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\sessions\2026\05\18\rollout-2026-05-18T12-16-37-019e3be0-05bb-7ee0-928a-f0ff9ce3f387.jsonl, updated_at=2026-05-18T17:39:24+00:00, thread_id=019e3be0-05bb-7ee0-928a-f0ff9ce3f387, shared movement-intent helper, GPU_FULL default, and smoke/static verification)

### keywords

- base:mp_skedar, swarmTestApplyMovementIntent, GPU_FULL, GPU_POS_ONLY, chrSurfaceLocoApplyContactPos, chrStartSkJump, SWARM.BEHAVIOR.JUMP, SURFACE_LOCO.PIN, skedar_swarm_cpu_behavior_smoke, skedar_swarm_gpu_behavior_smoke, tests/test_catalog_provider_static.cpp

- Related skill: skills/pd2-isolated-build-session/SKILL.md

## User preferences

- when the user says "Investigate first and plan" on a Skedar benchmark regression -> do root-cause analysis and produce a decision-complete plan before editing [Task 1]
- when the user frames the issue as GPU benchmark bots not matching CPU benchmark bots, plus missing wall walking/jumping -> optimize for parity across the benchmark modes, not just isolated symptom relief [Task 1][Task 2]
- when the request is specifically about the Skedar benchmark, keep verification focused on `base:mp_skedar` and avoid widening the fix into normal Combat Sim bot AI unless the user explicitly asks [Task 1][Task 2]

## Reusable knowledge

- The original mismatch was structural: GPU Swarm launched in `GPU_POS_ONLY` by default, while the CPU benchmark path in `port/src/swarm_test.c` owned stronger behavior like target reassertion, `chrTrySkJump`, wall-pin telemetry, and CPU-side overrides [Task 1]
- The durable fix boundary was a shared benchmark-local movement helper, `swarmTestApplyMovementIntent(...)`, used by both CPU and GPU benchmark code rather than a shader-only tweak [Task 1][Task 2]
- GPU benchmark readback now carries jump and surface-transition intent, and the default benchmark mode for parity moved to `GPU_FULL`, with `GPU_POS_ONLY` retained as a diagnostic toggle [Task 2]
- Surface contact correction for this lane belongs in `chrSurfaceLocoApplyContactPos(...)` and related surface-loco ownership helpers, not in `chrSetPos` world-ground recomputation [Task 2]
- Verification now has focused static coverage plus `tools/smoke-verify/tests/skedar_swarm_cpu_behavior_smoke.json` and `tools/smoke-verify/tests/skedar_swarm_gpu_behavior_smoke.json` on `base:mp_skedar` [Task 2]

## Failures and how to do differently

- symptom: a planned fix only touches GPU movement or the shader path; cause: the parity split also lives in CPU-only behavior blocks and Skedar jump/surface systems; fix: map the logic across `swarm_test.c`, `swarm_gpu.cpp`, `surface_loco.c`, and `chraction.c` before choosing the patch boundary [Task 1]
- symptom: the smoke pass misses stable wall-contact behavior; cause: the first run was too short or sampled the wrong density cycle; fix: extend the smoke window and rerun long enough to catch `SURFACE_LOCO.PIN` / trace telemetry [Task 2]
- symptom: the isolated build watchdog kills an otherwise healthy verification run; cause: this benchmark session pattern can outlive the default timeout; fix: rerun the same session with a longer `-BuildTimeoutSeconds` and keep the same isolated build id [Task 2]

<!-- mem-007: adjust -->
# Task Group: PD2 Jump Collision and Bot Jump Verification

scope: Player and bot jump/collision behavior around rendered surfaces and overhead blockers.
applies_to: Future physics-collision, jump, capsule sweep, and bot movement work.

## Durable Rules

- Player jump collision is much improved: Mike confirmed the player can now jump onto objects that previously caused fall-through.
- Keep the player path on the shared collision/capsule/mesh helpers rather than one-off rendered-surface fixes.
- Bot jumping is not proven complete. Treat it as unimplemented, unverified, or not yet confirmed until direct runtime evidence says otherwise.
- When fixing jump collision, keep player and bot vertical movement aligned where feasible, but do not claim bot parity without a focused test or Mike playtest.
- Kanban/context should distinguish build/test verified work from Mike-pending runtime validation.

<!-- mem-008: adjust -->
# Task Group: Kanban Board Planning and Staleness Rules

scope: `tools/kanban` board behavior and agent use of Kanban state.
applies_to: Future Kanban UI, task tracking, planning, and stale-card maintenance.

## Durable Rules

- Kanban status remains the primary navigation: Backlog, Active, Blocked, Done.
- Pillars are scoped filters/dropdowns within status, not the main navigation.
- Daily Flow is a peer top-level tab and starts collapsed by default.
- Starred and priority values sort cards rather than becoming extra filter controls.
- Manual card order matters and should affect agent planning; agents should not ignore user-arranged ordering.
- Same-session completed work still gets tracked on the board.
- When updating Kanban, check whether existing cards became stale, invalid, or contradicted by newer decisions. Surface those cards for adjustment instead of silently leaving bad context behind.

<!-- mem-009: adjust -->
# Task Group: PD2 Read-Only Assessments and Scope Expansion

scope: Read-only codebase assessments, roadmap reviews, ratings, and architectural planning.
applies_to: Future review-only or architecture-discovery requests.

## Durable Rules

- When Mike asks for a read-only assessment, keep it read-only and deliver the requested rubric/review before coding.
- Separate design maturity from verified product maturity.
- Use roadmap, context, task, session-log, and test docs before broad source spelunking.
- Architectural work often expands as the real scope is discovered. Do not silently defer important discovered scope just because it is larger than expected.
- Surface the scope decision to Mike, then expand the task when that is needed to complete the work properly the first time.

<!-- mem-010: keep -->
# Task Group: Perfect Dark 2 queued isolated builds, queue observability, and cleanup discipline

scope: Queued isolated build/test workflow in `C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike`, especially queue cleanup precision, watchdog/log observability, timeout tuning, per-step log preservation, and default verification etiquette when other sessions may also be building.
applies_to: cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike; reuse_rule=reuse for future PD2 verification work in this checkout family when builds may overlap or queue state matters, but treat exact session ids and transient queue occupants as run-specific

## Task 1: Stop hung front-of-queue builds and add live log capture/watchdog support, success

### rollout_summary_files

- rollout_summaries/2026-04-28T05-21-58-HAlS-queued_build_watchdog_and_live_log_capture.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\sessions\2026\04\28\rollout-2026-04-28T01-21-58-019dd289-7bc1-7f33-8c62-5287131d5ad4.jsonl, updated_at=2026-04-28T20:51:25+00:00, thread_id=019dd289-7bc1-7f33-8c62-5287131d5ad4, strongest source for queue cleanup precision, log capture, and watchdog semantics)

### keywords

- build-session.ps1, queue, watchdog, stdout, stderr, -Tail, -Follow, exit 124, active.json, _build-session.out.log, _build-session.err.log, stale lock, ui568, cat581

- Related skill: skills/pd2-isolated-build-session/SKILL.md

## Task 2: Add targeted `pd-tests` runner and queued isolated build verification flow, partial

### rollout_summary_files

- rollout_summaries/2026-04-28T05-09-07-1ufd-queued_isolated_pd_tests_build_session_pipeline.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\sessions\2026\04\28\rollout-2026-04-28T01-09-08-019dd27d-b900-7b83-9b05-87a31ed8d34f.jsonl, updated_at=2026-04-28T20:47:53+00:00, thread_id=019dd27d-b900-7b83-9b05-87a31ed8d34f, strongest source for queue-by-default verification flow, scope aliases, and timeout tuning)

### keywords

- run-pd-tests.ps1, pd-tests, Catch2 selectors, -Scope, -ListScopes, queue ETA, BuildTimeoutSeconds, NoQueue, tv573, isolated build, cleanup

- Related skill: skills/pd2-isolated-build-session/SKILL.md

## Task 3: Clarify queued isolated build rules in social-shell and trust/security runs, success/partial

### rollout_summary_files

- rollout_summaries/2026-04-28T05-09-40-FNyn-social_shell_build_session_queue_rule.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\sessions\2026\04\28\rollout-2026-04-28T01-09-40-019dd27e-3766-7d42-8663-b009d072b08f.jsonl, updated_at=2026-04-28T20:47:29+00:00, thread_id=019dd27e-3766-7d42-8663-b009d072b08f, strongest source for codifying the queue rule in docs)
- rollout_summaries/2026-04-28T05-12-38-Db7B-client_hosted_trust_security_queued_build_verification.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\sessions\2026\04\28\rollout-2026-04-28T01-12-38-019dd280-ee48-7020-b3e4-3400a5412e7b.jsonl, updated_at=2026-04-28T20:46:42+00:00, thread_id=019dd280-ee48-7020-b3e4-3400a5412e7b, useful for queue-state awareness and dirty-tree categorization during verification)

### keywords

- build-session.ps1, shared Build, -NoQueue, same session id, queue status, ETA, -Remove, ui568, sec581, tv573, swarm275

- Related skill: skills/pd2-isolated-build-session/SKILL.md

## Task 4: Preserve per-step headless logs and tighten timeout diagnostics, partial

### rollout_summary_files

- rollout_summaries/2026-04-29T04-34-45-gc06-pd2_readonly_assessment_plus_build_logging_fix.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\sessions\2026\04\29\rollout-2026-04-29T00-34-45-019dd784-9d12-7742-8ead-8cca63c0e2c6.jsonl, updated_at=2026-04-29T04:40:58+00:00, thread_id=019dd784-9d12-7742-8ead-8cca63c0e2c6, strongest source for per-step headless log persistence and valid target vocabulary)

### keywords

- build-headless.ps1, _build-headless, timeout diagnostics, per-step logs, det584, CMAKE_TRY_COMPILE_TARGET_TYPE, client server tests all, invalid target pd-tests

- Related skill: skills/pd2-isolated-build-session/SKILL.md

## User preferences

- when the user said "Kill it. That is too long." about an active build -> stop hung builds instead of waiting for self-recovery [Task 1]
- when the user corrected "I only wanted it removed from the beginnin of the queue because it had hung, it's fine if it gets re-added on it's own" -> only remove the hung front-of-queue instance; do not suppress legitimate requeues [Task 1]
- when the user asked "Can you check it's current log output" and later said "There should be a live stdout or some way to obeserve it available to you, if it isn't add it." -> prefer direct log inspection and add observable build output if the wrapper does not already expose it [Task 1][Task 4]
- when the user explicitly said "use `.\devtools\build-session.ps1 -Session <short-id> -Target all`", "do not use shared Build/", "do not pass -NoQueue unless Mike explicitly asks", "Reuse the same session id for reruns", "watch the queue status/ETA while waiting", and "clean up with `.\devtools\build-session.ps1 -Remove -Session <short-id>`" -> default future verification to the queued isolated wrapper and treat queue discipline as part of the task, not optional hygiene [Task 2][Task 3]
- when the user later said "skip tests this time" after repeated queue/watchdog friction -> stop forcing verification once they explicitly want it skipped and record the state instead of trying another build [Task 2]

## Reusable knowledge

- The queue wrapper tracks active/waiting state under `.claude/session-builds/.queue/` and per-session locks under `.claude/session-builds/.locks/`; `devtools/build-session.ps1 -List` is the main queue/status inspection command [Task 1]
- New queued builds capture wrapper stdout/stderr to `.claude/session-builds/<session-id>/_build-session.out.log` and `.claude/session-builds/<session-id>/_build-session.err.log`, and `build-session.ps1` supports `-Tail`, `-Tail -Session <id>`, and `-Tail -Follow` [Task 1]
- `build-headless.ps1` now writes per-step files like `_build-headless-<timestamp>-<step>.out.log` and `.err.log` inside the isolated build directory, and `build-session.ps1` can surface those recent headless logs on timeout and via `-Tail` [Task 4]
- `-List` prints timeout/log metadata for wrappers started after the capture patch, and watchdog timeouts return exit code `124` after stopping the child process tree and clearing the active queue slot [Task 1]
- `devtools/build-session.ps1` only accepts `client`, `server`, `tests`, or `all` as `-Target` values; `pd-tests` is not a valid wrapper target [Task 4]
- `devtools/run-pd-tests.ps1` is the scoped `pd-tests.exe` entry point, with `-Scope` aliases including `catalog`, `catalog-provider`, `catalog-identity`, `input`, `manifest`, `save`, `netbuf`, `connectcode`, `network-lifecycle`, and `spawn`, plus `-ListScopes` [Task 2]
- Foreground queued wrapper runs are the reliable path in this sandbox; direct background queue launches can be reaped or fail to persist [Task 2]
- The queue timestamp display bug was fixed by preserving `DateTime` values / round-trip UTC parsing, so `build-session.ps1 -List` now reports real elapsed and wait times instead of `0s` [Task 2]
- The durable queue workflow is: start with `.\devtools\build-session.ps1 -Session <short-id> -Target all`, watch `-List` / queue ETA, reuse the same session id for reruns within the same task, and clean up with `-Remove -Session <short-id>` when done [Task 2][Task 3]
- Queue churn is real in this repo: status can advance while a stop request is being prepared, and another session can remove/requeue entries; re-check current active metadata and processes before killing or rerunning [Task 1][Task 3]

## Failures and how to do differently

- symptom: a stop request targets the wrong queued session; cause: queue state advanced between inspection and cleanup; fix: re-check the current active record and process tree before killing anything [Task 1]
- symptom: a running wrapper shows no logs or still reports an old long timeout after the script was patched; cause: older wrappers cannot retroactively gain log capture or new defaults; fix: only expect `_build-session.*.log` files and newer timeout metadata on sessions started after the patch [Task 1]
- symptom: a long build dies with empty or unhelpful console output; cause: earlier headless steps kept output in memory until exit, so a watchdog kill could erase the evidence; fix: inspect the file-backed `_build-headless-*` logs and keep per-step log redirection in place [Task 4]
- symptom: the wrapper reports configure failure even though CMake generated successfully; cause: the wrapper/build-headless path can misclassify the result in this environment; fix: inspect the captured step logs before assuming the source patch is wrong [Task 4]
- symptom: the default watchdog stops a clean `-Target all` build; cause: the active-build timeout is too short for that verification run; fix: raise `-BuildTimeoutSeconds` for intentionally slow clean builds instead of treating exit `124` as a code failure [Task 2][Task 3]
- symptom: a queue entry or lock survives after timeout/abort; cause: stale wrapper/lock state remained; fix: confirm the lock owner is gone, then use targeted `-Remove -Session <id> -Force` and verify the session disappears from `-List` [Task 1][Task 3]
- symptom: verification loops consume time after the user has deprioritized it; cause: the queue/watchdog blocker became the main obstacle but the workflow kept retrying; fix: stop once the user says to skip tests or interrupts the rerun and preserve the build-pending/skipped state explicitly [Task 2]

<!-- mem-011: adjust -->
# Task Group: PD2 Targeted pd-tests Routing

scope: Focused `pd-tests` execution and selector use.
applies_to: Future targeted verification work.

## Durable Rules

- Prefer `devtools/run-pd-tests.ps1` with `-Scope` or `-Selector` for focused `pd-tests` lanes when applicable.
- Use scope aliases for common test surfaces instead of broad or brittle raw invocations.
- Keep test/docs alignment in the same slice when product rules change.
- Build queue behavior belongs in the queued-build memory, not this memory.
- Stale historical runner-failure details should not dominate future verification choices unless they reappear in the current repo.

<!-- mem-012: keep -->
# Task Group: Perfect Dark 2 client-hosted online interoperability, connect-code contract, and trust/security hardening

scope: Listen-host/client-hosted online shipping work in `C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike`, especially low-risk trust/security fixes, NAT-aware client routing, no-raw-IP join-surface rules, and queued verification expectations for this lane.
applies_to: cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike; reuse_rule=reuse for future PD2 client-hosted online slices in this checkout family, but treat exact protocol/build status as branch/time specific

## Task 1: Trust/security hardening for current client-hosted online, partial

### rollout_summary_files

- rollout_summaries/2026-04-28T05-12-38-Db7B-client_hosted_trust_security_queued_build_verification.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\sessions\2026\04\28\rollout-2026-04-28T01-12-38-019dd280-ee48-7020-b3e4-3400a5412e7b.jsonl, updated_at=2026-04-28T20:46:42+00:00, thread_id=019dd280-ee48-7020-b3e4-3400a5412e7b, strongest source for the current trust/security lane and queued verification state)

### keywords

- NET_PROTOCOL_VER 46, SVC_DISTRIB_BEGIN, SHA-256, connect code, raw IP cleanup, zero-length wire strings, CLC_MOVE, listen host, stat integrity, queued verification

- Related skill: skills/pd2-isolated-build-session/SKILL.md

## Task 2: Route remote handoffs through the hole-punch-aware client path, partial

### rollout_summary_files

- rollout_summaries/2026-04-29T04-44-09-mF4w-pd2_online_interoperability_holepunch_handoff_proof.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\sessions\2026\04\29\rollout-2026-04-29T00-44-09-019dd78d-3633-7781-9ef7-10bc03d84a4c.jsonl, updated_at=2026-04-29T04:54:31+00:00, thread_id=019dd78d-3633-7781-9ef7-10bc03d84a4c, strongest source for NAT-aware handoff routing and B-293)

### keywords

- netStartClientWithHolePunch, netStartClient, group_session.c, spectator.c, netholepunch.c, B-293, [net][interoperability][static], interop596

- Related skill: skills/pd2-isolated-build-session/SKILL.md

## Task 3: Align the QC/connect-code contract with no-raw-IP join surfaces, partial

### rollout_summary_files

- rollout_summaries/2026-04-29T04-41-49-3sQF-pd2_qc_connectcode_checklist_alignment.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\sessions\2026\04\29\rollout-2026-04-29T00-41-49-019dd78b-12e1-7501-a886-20dfb9011739.jsonl, updated_at=2026-04-29T04:51:38+00:00, thread_id=019dd78b-12e1-7501-a886-20dfb9011739, useful for the user-facing rule and the static guard)

### keywords

- No raw IP in any UI surface, Connect codes are the sole mechanism for sharing/entering server addresses, context/qc-tests.md, tests/test_connectcode.cpp, [connectcode][qc][static]

## User preferences

- when working in this lane, the user said dedicated-server product work is deferred unless explicitly revived -> default to listen-host/client-hosted shipping scope and do not spend time on standalone dedicated-server productization [Task 1][Task 2]
- when the repo/user lane says "No raw IP in any UI surface" and "Connect codes are the sole mechanism for sharing/entering server addresses." -> treat stale direct-IP UI, QC docs, and tests as bugs, not legacy documentation trivia [Task 1][Task 3]
- when the user said "Track your progress and as your last step, determine what is next and then start that, do this recursively until completion" -> after each completed slice, proactively choose and start the next smallest safe follow-up instead of stopping after the first patch [Task 1]
- when the user later asked what was dirty and whether it was in scope -> categorize modified files by lane instead of treating unrelated dirty files as generic noise [Task 1]

## Reusable knowledge

- The durable trust/security gaps for current client-hosted online are concrete: mod-transfer integrity, wire string parsing, raw-IP join-surface cleanup, and stat trust on listen hosts [Task 1]
- `SVC_DISTRIB_BEGIN` is the integrity boundary for mandatory mod hash verification; END-only checks are not sufficient [Task 1]
- Zero-length or malformed wire strings should fail safely rather than exposing pointers into following payload bytes [Task 1]
- The listen host can inventory-gate `CLC_MOVE`-driven weapon selects before they affect server-side state, which is a low-risk first stat-integrity barrier [Task 1]
- The normal join UI already used the NAT-traversal waterfall in `port/src/net/netholepunch.c`; invite handoffs and spectator handoffs needed the same `netStartClientWithHolePunch(addr)` path to stay consistent with listen-host/client-hosted shipping assumptions [Task 2]
- `netStartClient()` remains the low-level primitive owned by `net.c`; player-facing remote join paths should prefer the hole-punch-aware wrapper rather than bypassing it [Task 2]
- Listen-host startup in `netStartServer` already starts NAT discovery and `netDisconnect` tears it down; `tests/test_net_lifecycle_static.cpp` now pins that ordering alongside the handoff routing [Task 2]
- `tests/test_connectcode.cpp` and `context/qc-tests.md` are part of the real online contract surface because stale direct-IP phrasing in docs/tests can reintroduce product confusion after the UI has moved to connect-code-only joins [Task 3]
- The current dirty tree can include unrelated catalog/provider, input/transition, and build-tooling changes; verification in this lane should leave those alone unless they become the active task [Task 1]

## Failures and how to do differently

- symptom: code changes exist but verification is still inconclusive; cause: the queued isolated build hit watchdog/turn-interruption before clean completion; fix: keep the slice marked build-pending and rerun with the same session id and a larger timeout only when the user wants verification resumed [Task 1]
- symptom: a remote player-facing join path still uses raw `netStartClient(addr)` even though the main UI already uses hole punching; cause: a handoff path bypassed the higher-level waterfall; fix: route it through `netStartClientWithHolePunch()` and pin it with a static lifecycle test [Task 2]
- symptom: concurrent build processes remain visible after a run or a stray Ninja survives timeout; cause: other active sessions or a stalled fallback build are sharing the environment; fix: do not disturb unrelated sessions, but explicitly check for surviving build processes after timeout before moving on [Task 1][Task 3]

<!-- mem-013: adjust -->
# Task Group: PD2 Mod Sharing and Public Mods Trust Rules

scope: Public Mods, direct mod sharing, online-required mod delivery, and received-mod enable policy.
applies_to: Future mod sharing and connected-player mod access work.

## Durable Rules

- Mods should have their files validated for security before install/enable.
- Mod sharing between connected players should be seamless and native once validated.
- Friend-sourced direct/requested mod sharing should auto-accept and hot-enable after validation.
- Non-friend sources should prompt the player before enabling after download/install.
- Sharing should use registered/known mods and safe IDs, not arbitrary peer-provided filesystem paths.
- `.pdmod` remains the transport wrapper for sharing/Public Mods/online-required delivery, while typed `*.pdxxx` archives remain the content units.

<!-- mem-014: adjust -->
# Task Group: PD2 Menu Architecture Guidance

scope: Menu graph, menu transitions, and controller-facing menu architecture.
applies_to: Future menu cleanup and UI architecture work.

## Durable Rules

- Prefer proper menu architecture over ad hoc push/pop or one-off state flags.
- Narrow slices are useful when they move toward the correct architecture, but do not avoid a necessary architectural fix just because it is extensive.
- Controller-facing menu work must preserve real controller usability, not only satisfy a technical migration.
- If the correct fix is broad, surface that scope and get the decision rather than hiding it behind a small patch.

<!-- mem-015: adjust -->
# Task Group: PD2 Log-First Runtime Diagnostics

scope: Runtime bug diagnosis for black screens, load failures, catalog misses, lifecycle cleanup, and scenario/debug-launch issues.
applies_to: Future gameplay/runtime crash or missing-object investigations.

## Durable Rules

- Start from logs when Mike points at a runtime log or the symptom suggests load/bootstrap/lifecycle failure.
- For black screens, HUD-only loads, missing objects, or debug scenario failures, check launch path ownership, load/manifest state, catalog/provider registration, and teardown lifecycle before guessing at rendering.
- Keep the durable diagnostic pattern, not every resolved bug detail.
- Kanban cards should state whether a fix is hacky/local or architectural/systemic. Architectural/systemic fixes are preferred when the issue is an ownership or lifecycle class.

<!-- mem-016: adjust -->
# Task Group: PD2 Catalog as Asset Reference Authority

scope: Catalog-owned asset identity, lookup, provider, and reference behavior.
applies_to: Future asset, loader, mod, base-content, network-content, and gameplay reference work.

## Durable Rules

- The catalog is the single source of truth for asset references in the game.
- Gameplay and systems should ask the catalog by asset identity rather than guessing file paths, ROM file numbers, fallback order, archive locations, or provider internals.
- Base content, mods, network-delivered content, validation, rebuild, load, unload, and dependency behavior should converge through the catalog identity layer.
- Provider details and fallback order belong behind catalog APIs, not scattered through gameplay callsites.

<!-- mem-017: adjust -->
# Task Group: PD2 Layer-Aware Input Authority

scope: Input ownership across gameplay, menus, modals, analog values, held actions, and legacy pad mirrors.
applies_to: Future action-map, input routing, UI, gameplay control, and modal/menu ownership work.

## Durable Rules

- Input authority is layer-aware.
- Gameplay actions, menu actions, held actions, analog values, and legacy pad mirrors should respect the active input layer.
- No surface should consume or mirror input that belongs to a higher-priority menu, modal, text capture, or UI layer.
- Keep build-verification workflow details in the queued-build memory, not here.

<!-- mem-018: adjust -->
# Task Group: PD2 Controller-First Interaction Surfaces

scope: Project-wide interaction design and input expectations.
applies_to: Future UI, gameplay, menus, social, modding, tools, and interaction surfaces.

## Durable Rules

- Controller is a first-class citizen across the whole project.
- Every interaction surface should be designed, implemented, and verified with controller usability in mind, not added as an afterthought.
- Menu/UI work should respect the ImGui/menu-pool/input-context architecture unless a new architecture is explicitly chosen.
- Trim surface-specific Social shell details unless the current task directly needs them.

<!-- mem-019: keep -->
# Task Group: Perfect Dark 2 onboarding and major project prompts

scope: No-code onboarding, roadmap synthesis, and paste-ready session-start prompts for `C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike`.
applies_to: cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike; reuse_rule=reuse for future onboarding or planning-only requests in this repo, but refresh the roadmap source if the project planning docs change

## Task 1: Read onboarding prompt and list major projects with starter prompts, success

### rollout_summary_files

- rollout_summaries/2026-04-28T04-06-53-X23S-pd2_onboarding_major_project_prompts.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\sessions\2026\04\28\rollout-2026-04-28T00-06-53-019dd244-bd45-7960-bea2-6e61edc1d6f0.jsonl, updated_at=2026-04-28T04:25:50+00:00, thread_id=019dd244-bd45-7960-bea2-6e61edc1d6f0, best source for planning-only onboarding behavior)

### keywords

- pd2-codex-onboarding-prompt, full-release-roadmap-2026-04-27, no-code onboarding, session-start prompts, Input, Catalog, Mod pipeline, online connectivity, Forge, Grid, Studio

## User preferences

- when the user said "don’t make any code changes, just list of each major project we need to complete, along with a prompt to get s session started on it" -> for similar onboarding asks, default to planning/prompt-generation only and avoid implementation [Task 1]
- when the user named examples like "Input, Catalog, Mod pipeline, campaign / combat sim / online connectivity etc." -> organize onboarding around top-level project pillars rather than a narrow bug list [Task 1]

## Reusable knowledge

- `context/designs/full-release-roadmap-2026-04-27.md` is the best single source for PD2 major workstreams and release sequencing [Task 1]
- The onboarding prompt expects a consistent read order: `context/working-preferences.md`, then the roadmap, then the design/audit/context files [Task 1]
- The release framing preserved in the docs is catalog-first architecture, action-map-driven input, in-client listen-host connectivity, `.pdmod` mod flow, and Forge/Grid/Studio tools [Task 1]

## Failures and how to do differently

- symptom: onboarding prompt lookup takes longer than it should; cause: the file may be in the repo context tree rather than an assumed root location; fix: go straight to `context/pd2-codex-onboarding-prompt.md` when the repo copy is available [Task 1]

