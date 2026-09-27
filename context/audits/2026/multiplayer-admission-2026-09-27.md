# Multiplayer admission checkpoint — 2026-09-27

Owner: T-NETWORKING-011, codex-385f1c85-675768a6. Attribution: Codex / GPT-6.
Status: partial; the complete multiplayer plan and both release gates stay open.

## Changes

Match stage, baseline, cutscene/end, shared gameplay and direct GPU swarm packets
use the immutable admitted client mask plus current room/game state. A guest
who declines content remains a connected room member without receiving a stage
packet that excludes its identity. Failed decline sends retain consent; new
valid offers reset the previous decline. Preparation is established before
manifest checking so a synchronous consent decision owns the final state.

The optional smoke driver supports three peers and explicit manifest decline.
Campaign autostart preserves its requested mission across Combat room defaults.
Co-op/Counter-Op fixtures now require the production spawn preparation and commit
phases in order. The decline fixture permits exactly one disconnect after its
25-second and later 10-second lobby witnesses and scripted exit.

## Evidence and limits

- Integrated isolated client/tests builds passed. Multiplayer plus required
  c3842 cohort: 194 cases, 16,838 assertions, zero failures; native-source guard
  passed. Unrelated `tests/test_menu_graph.cpp` changed after the build snapshot;
  that drift is recorded, not hidden as a fully frozen test result.
- Binary: `4A3BA1AC7B44661B6F795C3FE1087FF096DDEF96968A2A97078033B28656B6C5`.
- Fresh runtime receipt: `.claude/smoke-verify-runs/results-20260927T082253Z.json`.
  Three independent installs: **94/101**, missing distributed Needler effect
  gameplay/render witnesses. Transfer, spawn, accepted-pair gameplay, declined
  guest lobby stability and shutdown ordering were observed. Keep every effect
  assertion; projectile rendering alone does not prove the effect path.
- Campaign pair: **90/96**, with six stale `SPAWN.ORCH: apply` assertions.
  Both players reached gameplay and exited normally. Current source logs
  `prepare` and `commit`. Corrected per-process assertions pass the retained
  host log45/45 and guest35/35; deleting commit records fails both. This is log
  re-evaluation, not a fresh runtime pass; the original receipt stays red.
- Moving the decline log's sole disconnect before readiness fails the corrected
  ordered assertion; the original normal log passes11/11.
- During runtime, the menu session changed `port/fast3d/pdgui_menu_agentselect.cpp`
  and `tests/test_menu_graph.cpp`. The 2,128-file pre/post manifests record both.
  These runs qualify observations on the named binary, not current-source or
  release acceptance. Receipt: `.claude/session-builds/mpfinish0923/mp0927-audience-validation.json`.
- Previous decline95/100 and99/100, co-op47/96, interrupted retry and intermediate
  static-contract failure remain retained. None is relabeled a pass.
- No WAN, relay, audible voice, visual review, physical controller or full mouse/
  keyboard journey acceptance is claimed.

## Next

The runtime and source holds were released for the graph session's source window.
Retain the missing-effect regression during that cutover. Coordinate the next
integrated freeze, build and repeat both corrected fixtures (and Counter-Op).
Then continue rooms/return/reconnect, movement authority/reconciliation, loss and
latency, social/device validation, public relay and migration from the full plan.
