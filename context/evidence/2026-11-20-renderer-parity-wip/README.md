# WIP: 2.0 renderer parity experiments, saved 2026-11-20

**Work in progress. Not for dev.** This folder holds what the renderer session
(`claude-week-pd-bgfx-classic`) had in its scratchpad when Mike handed its
wrap-up to the MULTIPLAYER session on 2026-11-20, about 17:20 UTC. MULTIPLAYER
stopped the running smoke batch on Mike's order and saved these files here so
they are not lost. Nothing in this folder is built into the game.

Branch `week-pd/bgfx-parity-proof` (draft PR 22). Workbench decisions D-041 and
D-042, under D-004.

## What is here

| Path | What it is |
|---|---|
| `exp1-driver.log` | The batch driver's own log for experiment 1, with one line appended by MULTIPLAYER that records the stop. |
| `exp1/` | What the driver wrote: the client and pd-tests build logs, the pd-tests log, the two static audits, and for each smoke its runner log, the install listings before and after, and the `PARITY:` lines. |
| `tools/bgfx_exp.sh` | The driver script (build, pd-tests, audits, freeze the client, then the smoke batch, each under its hub queue). |
| `tools/make_cn_fixtures.py` | Wrote the constant-noise smoke fixtures of experiment 1 (already on this branch). |
| `tools/make_glsl_fixtures.py` | Wrote the three `renderer_parity_*_classic_glsl_smoke.json` fixtures of experiment 2 (already on this branch, commit 1db3e1c8). |

The session's `exp2` scratch folder is not copied: its three edited files
(`port/fast3d/scenario_scene_renderer.cpp`, `scenario_scene_renderer_bgfx.cpp`,
`scenario_scene_renderer_bgfx.h`) are byte for byte what commit 1db3e1c8 on
this branch holds, and its other three files are unchanged copies of tracked
files.

The parity images and client logs are not in git (they are made from the ROM):
`D:\PD2-wt\bgfx-parity-evidence\exp1` on Mike's machine.

## Experiment 1 (Agent Select noise): results so far

Client: `week-pd/bgfx-parity-proof` 38881c77, frozen copy, shared base 13.
Build, pd-tests (2552 cases, all passed), the native write guard and the bgfx
dependency audit all passed before the smokes.

Seven smokes were queued. Three finished, all passed. Differing pixels are out
of 3,686,400 (2560 x 1440).

| Smoke | Result | First capture | Later capture |
|---|---|---|---|
| `renderer_classic_default_smoke` | PASS 10/10 | (no capture) | (no capture) |
| `renderer_parity_menu_smoke` | PASS 13/13 | 580 differ, max delta 2 | 601 differ, max delta 2 |
| `renderer_parity_menu_constant_noise_smoke` | PASS 14/14 | 497 differ, max delta 2 | 493 differ, max delta 2 |
| `renderer_parity_solo_smoke` (Defection) | stopped 15 s after the game started | none | none |
| `renderer_parity_solo_constant_noise_smoke` | never started | | |
| `renderer_parity_mp_smoke` (Felicity) | never started | | |
| `renderer_parity_mp_constant_noise_smoke` | never started | | |

The renderer session's own reading of these numbers (its message of 17:26 UTC,
not checked by anyone else): the 4,630-pixel Agent Select patch of the landing
proof does not appear on current dev with base 13 even without the flag; the
noise accounts for about 80 to 110 of the remaining differences; the other
490 or so differ by at most 2 levels and come from somewhere else. Which change
removed the old patch is not proven.

The stop left one interrupted install in this worktree's smoke store:
`.claude/smoke-storage/installs/20261120T171315507949Z-renderer_parity_solo_smoke-842edb14`.
It was not removed.

## Experiment 2 (Defection's scene shader): written, never built

Commit 1db3e1c8 ("Build the bgfx scene program from the GL shader text") adds
`--debug-bgfx-scene-classic-glsl` and three smokes. The renderer session
committed it locally at 16:53 UTC and had not pushed it. It has not been
compiled or run anywhere. MULTIPLAYER pushed it unchanged together with this
folder; hosted CI on PR 22 is its first build.

## Mike's answers to the two decisions

The renderer session's report of 2026-11-20 14:48 UTC put two decisions to
Mike. Decision 1 (Workbench D-041): accept the parity differences, or chase
their causes first; options (A) accept them now, (B) prove the causes, then
accept what is left, (C) require zero differences before any further 2.0 work.
Decision 2 (Workbench D-042): split-screen parity; options (A) defer it until
split-screen exists, (B) build a developer-only two-view frame now, (C) drop
split-screen from the parity requirement.

Mike's two replies in that session, word for word:

1. 2026-11-20 16:07:54 UTC: "B. We may add Split Screen back in –but, each player might want to play on a different mode (classic vs new)"
2. 2026-11-20 16:41:50 UTC: "B for the first question as well"

How they map, as the renderer session recorded them after the second reply:
the first reply's "B." answers Decision 2 (a developer-only two-view frame
now) and carries the note about each player's mode; the second reply gives B
for Decision 1 (prove the causes, then accept what is left).

Still open with Mike: how to build the two-view frame. The renderer session
offered (A) renderer only, (B) the game's own split-screen on a netplay host,
its pick, (C) a developer-only second local player. No answer yet.
