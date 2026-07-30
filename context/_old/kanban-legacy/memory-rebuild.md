# PD2 Rebuilt Memory List

This is the clean memory set rebuilt on 2026-05-20. Apply these entries verbatim as the new durable memory set for `C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike`.

## Task Group: PD2 Asset Pipeline: Typed pdxxx Asset Archives

scope: Native asset authoring, storage, sharing, validation, and runtime consumption for base-game and mod content.
applies_to: Future PD2 asset-pipeline, content extraction, base-content rebuild/validation, mod content, Public Mods, and online-required-content work.

### Durable Rules

- All game content should be saved per asset as the relevant typed `*.pdxxx` file.
- Base-game assets and mod assets should be treated natively and equally by the asset pipeline; mods are not a second-class or separate content model.
- Each `.pdwpn`, `.pdhead`, `.pdbody`, `.pdarena`, `.pdmesh`, `.pdanim`, `.pdsfx`, `.pdvoice`, `.pdsong`, `.pdui`, `.pdfont`, `.pdlang`, and similar typed file is a self-contained archive for that asset.
- Changing a typed asset archive extension to `.zip` should expose the descriptor plus all authored source files needed to edit or clone that asset.
- Authored files inside typed archives should be modern and accessible: INI/TSV metadata, GLTF/GLB/OBJ models/maps/animations, PNG/TGA textures, WAV/OGG/MP3 audio, TTF/OTF fonts, and related readable files.
- The engine should use those files natively at the game-facing boundary. If native use requires conversion in either direction, build the two-way conversion pipeline rather than shipping opaque authored blobs.
- `.pdmod` is transport only for Public Mods, sharing, and online-required delivery. It is not the primary authoring surface.
- Authored `.bin` payloads are invalid in modder-facing archives. Runtime cache may be private, readable, rebuildable, and deleteable, but it is not shipped as the source of truth.

## Task Group: PD2 F6 Debug Completion Semantics

scope: F6 debug hotkey behavior in Campaign and Combat Simulator.
applies_to: Future debug-hotkey, mission-completion, and campaign-end routing work.

### Durable Rules

- F6 in Campaign and F6 in Combat Simulator are separate behaviors.
- Campaign F6 should behave like real mission completion, not like a loose objective/HUD shortcut.
- Campaign completion must set all appropriate mission-completion flags and state, including difficulty-active objectives, death/abort state, end-stage flow, and final-campaign Credits routing.
- Combat Simulator-only debug fallbacks, such as bot-freeze behavior, must remain separate and gated to the correct context.
- If Mike reserves runtime validation for himself, record the state as build/static verified and Mike-pending runtime playtest.

## Task Group: PD2 Press/Hold Input Semantics

scope: Press-vs-hold input behavior across gameplay and UI.
applies_to: Future action-map, interaction, firing, vehicle, weapon, and UI input work.

### Durable Rules

- Press and hold are the same physical input until release timing or the held threshold decides the semantic result.
- Releasing before the held threshold is a press.
- Crossing the held threshold is a held input.
- Held input consumption depends on the action, not a single global rule.
- Door/open interactions can consume the held action immediately after threshold activation.
- Sustained actions such as full-auto fire should remain active until a natural stop condition: release, empty magazine, player death, round end, weapon change, vehicle entry, or similar state transition.
- UI progress and gameplay consumption must agree; fixing only the visible prompt is incomplete.

## Task Group: PD2 Skedar Benchmark Parity and Surface Locomotion

scope: Skedar benchmark behavior, CPU/GPU parity, and benchmark-local Skedar jump/surface locomotion.
applies_to: Future Swarm benchmark or Skedar wall/jump parity work.

### Durable Rules

- Investigate and plan before editing Skedar benchmark regressions.
- Optimize for CPU/GPU benchmark parity across `base:mp_skedar`, not isolated symptom fixes.
- Keep benchmark-only fixes separate from normal Combat Sim AI unless Mike explicitly widens the scope.
- The durable fix boundary is shared benchmark-local movement intent, not shader-only tweaking.
- `GPU_FULL` is the parity default, while `GPU_POS_ONLY` remains diagnostic.
- Surface contact correction belongs in surface-locomotion ownership helpers, not ad hoc world-ground recomputation.

## Task Group: PD2 Jump Collision and Bot Jump Verification

scope: Player and bot jump/collision behavior around rendered surfaces and overhead blockers.
applies_to: Future physics-collision, jump, capsule sweep, and bot movement work.

### Durable Rules

- Player jump collision is much improved: Mike confirmed the player can now jump onto objects that previously caused fall-through.
- Keep the player path on the shared collision/capsule/mesh helpers rather than one-off rendered-surface fixes.
- Bot jumping is not proven complete. Treat it as unimplemented, unverified, or not yet confirmed until direct runtime evidence says otherwise.
- When fixing jump collision, keep player and bot vertical movement aligned where feasible, but do not claim bot parity without a focused test or Mike playtest.
- Kanban/context should distinguish build/test verified work from Mike-pending runtime validation.

## Task Group: Kanban Board Planning and Staleness Rules

scope: `tools/kanban` board behavior and agent use of Kanban state.
applies_to: Future Kanban UI, task tracking, planning, and stale-card maintenance.

### Durable Rules

- Kanban status remains the primary navigation: Backlog, Active, Blocked, Done.
- Pillars are scoped filters/dropdowns within status, not the main navigation.
- Daily Flow is a peer top-level tab and starts collapsed by default.
- Starred and priority values sort cards rather than becoming extra filter controls.
- Manual card order matters and should affect agent planning; agents should not ignore user-arranged ordering.
- Same-session completed work still gets tracked on the board.
- When updating Kanban, check whether existing cards became stale, invalid, or contradicted by newer decisions. Surface those cards for adjustment instead of silently leaving bad context behind.

## Task Group: PD2 Read-Only Assessments and Scope Expansion

scope: Read-only codebase assessments, roadmap reviews, ratings, and architectural planning.
applies_to: Future review-only or architecture-discovery requests.

### Durable Rules

- When Mike asks for a read-only assessment, keep it read-only and deliver the requested rubric/review before coding.
- Separate design maturity from verified product maturity.
- Use roadmap, context, task, session-log, and test docs before broad source spelunking.
- Architectural work often expands as the real scope is discovered. Do not silently defer important discovered scope just because it is larger than expected.
- Surface the scope decision to Mike, then expand the task when that is needed to complete the work properly the first time.

## Task Group: PD2 Queued Isolated Builds and Cleanup Discipline

scope: Queued isolated build/test workflow.
applies_to: Future PD2 build verification where other sessions may also be building.

### Durable Rules

- Default to `devtools/build-session.ps1 -Session <short-id> -Target all` for queued isolated verification, not shared `Build/`.
- Reuse the same session ID for reruns within one task.
- Watch queue status/ETA and inspect live stdout/stderr when diagnosing slow or stuck builds.
- Treat watchdog timeout exit code `124` as a hung-build failure unless current evidence proves otherwise.
- Clean up with `devtools/build-session.ps1 -Remove -Session <short-id>` when the session is done.
- Do not pass `-NoQueue` unless Mike explicitly asks.
- If Mike says to skip tests, stop forcing verification and record the skipped/pending state honestly.

## Task Group: PD2 Targeted pd-tests Routing

scope: Focused `pd-tests` execution and selector use.
applies_to: Future targeted verification work.

### Durable Rules

- Prefer `devtools/run-pd-tests.ps1` with `-Scope` or `-Selector` for focused `pd-tests` lanes when applicable.
- Use scope aliases for common test surfaces instead of broad or brittle raw invocations.
- Keep test/docs alignment in the same slice when product rules change.
- Build queue behavior belongs in the queued-build memory, not this memory.
- Stale historical runner-failure details should not dominate future verification choices unless they reappear in the current repo.

## Task Group: PD2 Client-Hosted Online and Connect-Code Contract

scope: Listen-host/client-hosted online, connect-code UI, NAT-aware handoffs, and trust/security hardening.
applies_to: Future online interoperability and shipping-scope work.

### Durable Rules

- Current online scope is listen-host/client-hosted behavior; dedicated-server productization is deferred unless Mike explicitly revives it.
- No raw IP should appear in player-facing UI surfaces. Connect codes are the share/join mechanism.
- Stale direct-IP UI, docs, or tests are bugs once product behavior is connect-code-only.
- Player-facing remote handoffs should use the hole-punch-aware client path instead of bypassing NAT traversal.
- Mod transfer integrity, malformed wire strings, and listen-host trust boundaries are security surfaces, not polish.
- Categorize dirty files by lane and avoid sweeping unrelated changes into online work.

## Task Group: PD2 Mod Sharing and Public Mods Trust Rules

scope: Public Mods, direct mod sharing, online-required mod delivery, and received-mod enable policy.
applies_to: Future mod sharing and connected-player mod access work.

### Durable Rules

- Mods should have their files validated for security before install/enable.
- Mod sharing between connected players should be seamless and native once validated.
- Friend-sourced direct/requested mod sharing should auto-accept and hot-enable after validation.
- Non-friend sources should prompt the player before enabling after download/install.
- Sharing should use registered/known mods and safe IDs, not arbitrary peer-provided filesystem paths.
- `.pdmod` remains the transport wrapper for sharing/Public Mods/online-required delivery, while typed `*.pdxxx` archives remain the content units.

## Task Group: PD2 Menu Architecture Guidance

scope: Menu graph, menu transitions, and controller-facing menu architecture.
applies_to: Future menu cleanup and UI architecture work.

### Durable Rules

- Prefer proper menu architecture over ad hoc push/pop or one-off state flags.
- Narrow slices are useful when they move toward the correct architecture, but do not avoid a necessary architectural fix just because it is extensive.
- Controller-facing menu work must preserve real controller usability, not only satisfy a technical migration.
- If the correct fix is broad, surface that scope and get the decision rather than hiding it behind a small patch.

## Task Group: PD2 Log-First Runtime Diagnostics

scope: Runtime bug diagnosis for black screens, load failures, catalog misses, lifecycle cleanup, and scenario/debug-launch issues.
applies_to: Future gameplay/runtime crash or missing-object investigations.

### Durable Rules

- Start from logs when Mike points at a runtime log or the symptom suggests load/bootstrap/lifecycle failure.
- For black screens, HUD-only loads, missing objects, or debug scenario failures, check launch path ownership, load/manifest state, catalog/provider registration, and teardown lifecycle before guessing at rendering.
- Keep the durable diagnostic pattern, not every resolved bug detail.
- Kanban cards should state whether a fix is hacky/local or architectural/systemic. Architectural/systemic fixes are preferred when the issue is an ownership or lifecycle class.

## Task Group: PD2 Catalog as Asset Reference Authority

scope: Catalog-owned asset identity, lookup, provider, and reference behavior.
applies_to: Future asset, loader, mod, base-content, network-content, and gameplay reference work.

### Durable Rules

- The catalog is the single source of truth for asset references in the game.
- Gameplay and systems should ask the catalog by asset identity rather than guessing file paths, ROM file numbers, fallback order, archive locations, or provider internals.
- Base content, mods, network-delivered content, validation, rebuild, load, unload, and dependency behavior should converge through the catalog identity layer.
- Provider details and fallback order belong behind catalog APIs, not scattered through gameplay callsites.

## Task Group: PD2 Layer-Aware Input Authority

scope: Input ownership across gameplay, menus, modals, analog values, held actions, and legacy pad mirrors.
applies_to: Future action-map, input routing, UI, gameplay control, and modal/menu ownership work.

### Durable Rules

- Input authority is layer-aware.
- Gameplay actions, menu actions, held actions, analog values, and legacy pad mirrors should respect the active input layer.
- No surface should consume or mirror input that belongs to a higher-priority menu, modal, text capture, or UI layer.
- Keep build-verification workflow details in the queued-build memory, not here.

## Task Group: PD2 Controller-First Interaction Surfaces

scope: Project-wide interaction design and input expectations.
applies_to: Future UI, gameplay, menus, social, modding, tools, and interaction surfaces.

### Durable Rules

- Controller is a first-class citizen across the whole project.
- Every interaction surface should be designed, implemented, and verified with controller usability in mind, not added as an afterthought.
- Menu/UI work should respect the ImGui/menu-pool/input-context architecture unless a new architecture is explicitly chosen.
- Trim surface-specific Social shell details unless the current task directly needs them.

## Task Group: PD2 Onboarding and Major Project Prompts

scope: No-code onboarding, roadmap synthesis, and paste-ready session-start prompts.
applies_to: Future onboarding or planning-only requests.

### Durable Rules

- When Mike asks for no-code onboarding, major project lists, roadmap synthesis, or paste-ready prompts, stay in planning/prompt-generation mode and do not implement.
- Organize major project prompts around top-level project pillars rather than a narrow bug list.
- Use `context/designs/full-release-roadmap-2026-04-27.md` as the steering source when it is current.
- Read the project context before making claims about live state.
