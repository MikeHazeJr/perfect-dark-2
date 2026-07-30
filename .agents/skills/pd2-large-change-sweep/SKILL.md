---
name: pd2-large-change-sweep
description: Use for Perfect Dark 2 when making or auditing broad cross-system changes, especially networking, match flow, lobby return, manifest distribution, controller/input, collision, asset pipeline, save/wire protocol, or any user request to sweep for gaps, stubs, stale context, stale tests, or disconnected runtime flow.
---

# PD2 Large Change Sweep

## Overview

Use this skill to turn a broad change into a source-of-truth audit before or during implementation. The goal is to catch disconnects between runtime code, tests, context docs, Workbench state, release notes, and user-facing flow before they become follow-up bugs.

Always use `context-manager` first when this repo's `context/` directory exists. Treat this skill as the sweep layer that runs after the normal context start, constraint check, and decision-request check.

## Sweep Workflow

1. **Anchor the requested flow.** Restate the user-facing path in concrete terms. For networking or match flow, include: add/connect, join, social lobby, room/lobby, settings sync, manifest/distribution, ready gate, match start, live play, match end, return to connected lobby, disconnect/reconnect, and drop-in/drop-out.
2. **Check source of truth first.** Read live headers, constants, APIs, and build targets before trusting docs. For protocol work, verify `NET_PROTOCOL_VER` in `port/include/net/net.h`; for build targets, verify CMake and active build scripts.
3. **Map runtime evidence.** Find the code paths that prove each flow step works, using `rg` before broader file reads. Prefer concrete handlers, dispatch tables, state transitions, tests, and call sites over comments.
4. **Find drift and stubs.** Search for stale versions, deprecated targets, old product paths, `TODO`, `stub`, `placeholder`, `not wired`, `future`, `deprecated`, and historical assumptions that now contradict live behavior.
5. **Separate fix-now from sprint-ledger.** Patch low-risk source-of-truth drift immediately. Record larger runtime gaps as explicit ordered follow-ups tied to the active card instead of leaving them implicit.
6. **Update project context as part of the work.** Update Workbench items/evidence/notes, `context/tasks.md`, relevant `context/pillars/*.md`, and `context/session-log.md` when the session closes. If user-visible behavior changed, update `UNRELEASED.md`.
7. **Verify the edited state.** Run focused tests or static checks proportional to the change. For docs/JSON-only sweeps, run JSON parsing and whitespace/diff checks; for code behavior, use the isolated build/test session workflow.

## Asset Pipeline c3842 Gate

For any asset-pipeline sweep, extraction change, catalog/provider load change, runtime asset adapter, typed `.pdxxx` archive layout, Modding Hub asset tool, or distribution/manifest change, enforce c3842 before implementation. The short form is public editable source, runtime-cache-only products:

- Public editable source files inside the typed archive are the native game-client source.
- Generated renderer, GPU, collision, audio-codec, animation, graph-runtime, room/portal, or other engine-ready products are source-hashed cache only.
- Reject preview-only paths, descriptor-only "load" claims, opaque `.bin` payloads, raw preprocessed dumps, and parallel authored runtime duplicates.
- Reject public asset-reference fields that carry numeric or legacy-symbol identities such as `model_id = 42`, `model = MODEL_*`, `filenum`, `modelnum`, `weapon_id`, `sound_id`, or `texnum`; authored references use catalog IDs only.
- Treat runtime ROM/RomProvider fallback after extraction as an asset-chain failure, not a valid fallback. Record and fix it under `c3844` at the owning asset-family boundary.
- Before closeout, run `python tools/asset_native_source_guard.py` and focused `[modding][pdxxx][c3842]` tests when code/test changes are involved.
- Any exception must be recorded as an explicit c3842 gap on the active card before proceeding.

## Required Surfaces

For every large-change sweep, inspect at least these surfaces:

- **Runtime code:** live constants, dispatch handlers, state machines, lifecycle calls, ownership boundaries, and teardown/return paths.
- **Tests:** focused unit/static tests, smoke selectors, and any tests that still mention removed targets or old protocol versions.
- **Context:** `context/README.md`, `context/constraints.md`, `context/tasks.md`, affected pillar docs, and `context/session-log.md` at closeout.
- **Workbench:** active item ownership, dependencies, notes, status, evidence, validation/performance gates, and the completed-vs-incomplete split.
- **Release notes:** `UNRELEASED.md` for user-visible fixes or capabilities.
- **Historical docs:** roadmaps, audits, or bug records only when they are currently presented as live guidance. Do not rewrite historical records just to remove old wording.

## Networking And Match-Flow Checklist

When the sweep is about online play or match flow, build a matrix with these rows and fill each with evidence, gaps, and next action:

- Friend/connect-code entry and invite/share behavior.
- NAT, hole punch, listen-host client path, and any broker/fallback assumptions.
- Room/social lobby ownership, ready state, player membership, and return path.
- Match settings sync, playlist/arena/options, and late resync.
- Manifest digest, required assets, distribution, validation, and hot registration.
- Ready gate, countdown, abort paths, client leave, and asset failure handling.
- Stage load and lifecycle cleanup, including manifest clear/rescan.
- Live gameplay sync: players, bots/NPCs, projectiles, entities, props, doors/lifts, weapons, score tracking, killfeed, jumping, and surface/collision state.
- Match end, endscreen/dialog stack, save prompts, and flow back to the connected lobby.
- Drop-in/drop-out, reconnect, stale client cleanup, and independent play versus grouped play.

## Gap Rules

- Treat stale docs that contradict live source as bugs in the context system.
- Treat placeholder metrics, unused serializers, old build targets, dead protocol text, and comment-only "future" paths as explicit sweep findings.
- Do not mark a gap complete because a nearby feature exists; record the exact missing wire, lifecycle, or test surface.
- Preserve the user's concrete wording for visible symptoms and player-facing flows.
- When a large gap would turn into a rabbit hole, stop and present the options; otherwise, patch narrow drift and continue.

## Closeout Shape

End the sweep with:

- What was corrected immediately.
- What runtime evidence proves the flow today.
- What gaps remain, ordered for the next sprint.
- What Workbench/context/release-note surfaces were updated.
- What verification was run, and what was intentionally not run.
