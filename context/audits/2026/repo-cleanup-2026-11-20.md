# Repository cleanup, 2026-11-20

Mike's order (2026-11-20, about 16:40 UTC): Codex is disabled and the other PD sessions are being archived;
push and merge all PD work, fix collisions, and clean up the repository before fresh sessions start. Carried
out by `claude-week-pd-multiplayer` (16:40 to 19:00 UTC). Nothing was deleted. Before anything changed, every
uncommitted source, test, fixture and context file of the canonical checkout (106 files) was saved on origin as
`week-pd/inflight-20261120` (68a98015). Start here if you pick up any of the work listed below.

## What landed on dev

| Commit | What | Owner of the work | State |
|---|---|---|---|
| acbce249 | `Modding - T-MODDING-011`: the reviewed V5 world-hit delivery (Mike's decision 2A), with one lost seven-line hunk restored from `week-pd/inflight-20261114`; closes B-1279 | `codex-pd-local-cue-20261010` (October) | Built, pd-tests passed; never run in the game |
| 65bf5d00 | `Menus - T-MENUS-008`: Unlock All as one stored flag, cheat repairs, training availability; three training-save hunks of T-ENGINE-007 | Manager 3 `codex-012282b0-8ddc2a4f`; Manager 1 `codex-f659e012-9bc298f7` | Built, pd-tests passed; never run in the game |
| 993765b4 | `Theater - T-THEATER-001`: the persistent replay reader | Manager 1 | Built, pd-tests passed; nothing calls it yet |
| 490f2f34 | `Modding - T-MODDING-016`: embedded weapon validation, the B-1152 clone guard | Manager 2 `codex-d5caa37e-164844cc` | Built, pd-tests passed; never run in the game |
| 4ac5f8c7 | `Release - T-RELEASE-007`: the sample-mod packaging step, switched off (no mod has `release: true`) | Manager 2 | Python cases and packaging passed for its owner; built here |
| 43665b0f | Merge of `week-pd/multiplayer-coop` (PR 13): co-op as missions again (B-1274), AI buddies, partner drop-out S1, rejoin S2 host side (client still refuses, B-1277) | MULTIPLAYER, then Manager 1 | See Verification |
| records commit | This file, the Workbench data, the week note, the weekly audit of 2026-11-20, B-1279, B-1280, SP-99 | | |

Also pushed with dev: the 14 commits that were only on local dev before the cleanup (the Grid browser and stage
gateway 973decb7, the paired Swarm baseline 7a54e87d, the gun noise callers 264587ac, the Grid model import fixes
2f75be44 and 71eb950f, T-TOOLING-004/-015/-016, the Room consent replay record, and CORE's local merges of its
landings).

## Verification

- The committed tree: dev plus PR 13 (d1932f55, the same tree as 43665b0f) built from its commits alone in
  `D:\PD2-wt\multiplayer` (client, updater, test binary) and passed pd-tests in full: 2,564 cases, 583,896
  assertions. Before acbce249, dev's own commits did not build (B-1279).
- Game smokes on that build, shared base 14: `mp_combat_local_scorelimit_smoke` 24/24; `crystal_pistol_ammo_smoke` 29/31, then 31/31 on a rerun of the
  same build (a 1.2 s frame stall squeezed its reload steps, B-1282); `network_coop_mission_end_smoke` 126/132:
  the host's results screen ignored the injected Next Mission tap, because 149820cb1 (on origin/dev since
  2026-11-19) turns ImGui navigation off while the window lacks focus (B-1281). Not caused by the cleanup's
  commits, which do not touch that path; the smoke passed 132/132 on ddac61d6 the day before.
- The canonical checkout as it stands, with the files left in place below: 2,811 cases, 6 failed, all from files left in place:
  three pins read `src/game/chr.c`, which is CRLF on disk in Manager 4's uncommitted copy (CI checks out LF);
  one pins committed `bot.c` text that Manager 4's rewrite changes; one is the parked GGV lane's own case
  (`test_gameplay_graph_values.cpp:5998`); one is the V5 test written ahead of its code.
- Hosted CI runs on the push of dev.

## Left uncommitted on purpose

All of it is in the canonical checkout as it was, and saved on origin in `week-pd/inflight-20261120`.

- **Manager 4's jump and ground-support rewrite** (T-ENGINE-006): a design choice for Mike, decision **D-052**.
  It changes what every player stands on, narrows simulant jumping to Hard and above (committed `bot.c` cites
  Mike's directive of 2026-05-17 for every difficulty), was never built or run by its owner, and one committed pin
  fails against it (`tests/test_scene_dispatch.cpp:419`).
- **The switch that bundles the Crystal Pistol** in a release (`dev-mods/dev-mods.json`,
  `dev-mods/crystal_pistol/mod.json`): Mike's decision **D-053** (N-1088 holds it for provenance and BYOR). A
  release cut from the canonical checkout while these two files stay edited bundles the pistol.
- **The October gameplay-graph lane (GGV)**: parked by Mike's decision 2A of 2026-11-15. 20 untracked files and a
  CMakeLists.txt hunk; the canonical checkout's builds compile them as dead code, and one of their 239 cases fails.
  Its 32 bullets in `UNRELEASED.md` describe the parked `bondgun.c` rewrite, which is not on dev.
- **A V5 test written ahead of its code** (`tests/test_weapon_graph_v2_catalog.cpp`): it expects `wgV2Wake` to
  ignore a stale ticket; the committed core cancels it, so the case fails.
- **Leftovers already on dev in another form**: three comment lines in `propobj.c`, a duplicate B-1261 test, an
  October draft of a tracked test (`.inflight-20261117`), one blank line in the modding pillar.
- **Scratch and evidence folders** (62 folders, about 207,000 files, 20.7 GB): `.claude/...` and `__cp_review/`.
  Not deleted; owners and dates in the table below.

## Other branches and leftovers

- PR 8 (`week-pd/mods-v4`, Codex's), PR 12 (`week-pd/core-s6`) and PRs 14 to 21 were already merged before the
  cleanup. PR 22 (renderer, draft) was left open, as Mike asked.
- `week-pd/hubfix` and `week-pd/keygen`: their commits are on dev as equivalent patches (`git cherry`).
- `week-pd/inflight-20261114`: the snapshot of November 14; it still holds the parked `bondgun.c` rewrite.
- `stash@{0}` ("PR 8 local edits before 4d60e5e0e"): nothing exists only there; every line it adds is in HEAD or
  the working tree, or was deliberately rewritten later.
- Every other worktree is clean apart from scratch folders, except two that carry a regenerated
  `updater_pubkey.h`, which is never committed: `D:\PD2-wt\x4-check` and
  `C:\Users\mikeh\Perfect-Dark-2\.worktrees\pd2-workbench-asset-audit`.

## Renderer wrap-up

Mike reported the renderer session archived in mid-work (about 16:38 UTC). Its 7-smoke parity batch (hub item
`q_20261120163113586_5dba97df`) was stopped at 17:24 UTC with 3 of 7 finished and passed; the item was released
as abandoned. Its drafts are saved as WIP commit 0f03f29c on `week-pd/bgfx-parity-proof` (PR 22), with its own
unpushed experiment-2 commit 1db3e1c8 under it; nothing went to dev. Mike's two answers to its parity decisions
(D-041, D-042) are recorded word for word in Workbench notes N-1147 and N-1148, the week note, and
`context/evidence/2026-11-20-renderer-parity-wip/README.md` on that branch. Handoff: N-1149.

## Build time

One clean full build of the canonical checkout (dev 43665b0f plus the files left in place), fresh session folder,
machine otherwise idle: **3:01 wall** (181.5 s), against about 2:32 usually.

| Phase | Wall |
|---|---|
| Client and updater, through the wrapper | 68.6 s |
|   configure | 2 s |
|   generated headers (382 small Python steps, about 2 cores busy) | 13 s |
|   bgfx, bx, bimg | 4 s |
|   client compile and link (879 sources, 922 s of CPU, 20 of 24 cores busy) | 46.5 s |
|   updater | 2 s |
| Test binary, through the wrapper | 112.8 s |
|   test compile and link (528 edges, 1,807 s of CPU, 17 cores busy on average) | 107 s |
| Hub queue calls between the steps | about 9 s |

Where the time goes:
- **No compiler cache.** Every wrapper build compiles cold, because its ccache probe always reads as failed
  under Windows PowerShell 5.1 (B-1280; one-line fix described there, not applied). With a working cache a clean
  rebuild of unchanged sources took about 10 s in April.
- **The test binary is most of it** and ends on one file: `tests/test_asset_native_source_contract.cpp` takes
  62 s and starts 43 s in, so nothing else can finish the build sooner. Next: `test_mod_external_archive_static.cpp`
  49 s, `test_forge_level_doc.cpp` 35 s, `tests/main.cpp` (Catch2) 30 s, `test_gameplay_graph_values.cpp` 28 s
  (parked GGV test, compiled only because of the uncommitted CMake hunk), `test_net_lifecycle_static.cpp` 24 s.
  25 test files take 10 s or more each (475 s together).
- **Generated headers** cost 13 s at low parallelism on every clean build.
- **The drive.** In the `D:` worktree (HDD) the client part of the merged tree's build recompiled 146 files in
  334 s with under 3 cores busy, against 879 files in 46.5 s here on `C:` (NVMe): compiles there wait on the disk.

## Per-file table

Taken before anything changed (16:45 UTC). "Change" is the uncommitted diff against dev at that time.

| Group and owner | Path | Last modified (UTC) | Change | What it is | What happened |
|---|---|---|---|---|---|
| **Weapon graph v2 world-hit delivery (V5).** `codex-pd-local-cue-20261010`, T-MODDING-011, the reviewed V5 patch (N-0477, N-0481, N-0539; compile repairs N-1134, N-1135) | `port/include/weapon_graph_v2_runtime.h` | 2026-10-11 20:18 | +59/-3 | Needed unfinished work. Mike's decision 2A of 2026-11-15 says to carry it onto dev, and local commit 264587ac already called it. | Committed as acbce2493. |
|  | `port/src/weapon_graph_v2_runtime.cpp` | 2026-10-11 20:18 | +225/-15 |  |  |
|  | `port/include/weapon_nested_runtime_harness.h` | 2026-10-11 19:57 | +11/-0 |  |  |
|  | `port/src/weapon_nested_runtime_harness.c` | 2026-11-20 03:06 | +1016/-4 |  |  |
|  | `src/game/prop.c` | 2026-11-17 09:36 | +94/-11 |  |  |
|  | `src/game/bondgun.c` | 2026-11-20 03:09 | +29/-4 |  |  |
|  | `src/include/game/bondgun.h` | 2026-11-17 06:22 | +3/-0 |  |  |
|  | `port/src/pdmain.c` | 2026-11-20 01:48 | +1/-0 |  |  |
|  | `tests/test_weapon_graph_v2_equipped.cpp` | 2026-11-17 06:22 | +13/-0 |  |  |
|  | `tests/test_weapon_graph_v2_native.cpp` | 2026-10-11 18:00 | +19/-1 |  |  |
|  | `tools/smoke-verify/generate_weapon_mesh_ingress_fixtures.py` | 2026-10-11 18:27 | +43/-0 |  |  |
|  | `tools/smoke-verify/tests/graph_v2_world_hit_smoke.json` | 2026-10-11 20:07 | new, 21734 bytes |  |  |
| **V5 test written ahead of its code.** `codex-pd-local-cue-20261010`, T-MODDING-011, the reviewed V5 patch (N-0477, N-0481, N-0539; compile repairs N-1134, N-1135) | `tests/test_weapon_graph_v2_catalog.cpp` | 2026-10-11 18:00 | +9/-1 | Needed unfinished work that cannot pass: it expects a stale wake to be ignored, the committed graph core cancels it. | Left in place, uncommitted. On origin in week-pd/inflight-20261114 and week-pd/inflight-20261120. |
| **Unlock All, cheats, training availability.** Manager 3, `codex-012282b0-8ddc2a4f`, T-MENUS-008 (N-0844, N-0847, N-0855 to N-0859, N-0874); three hunks of `training.c` are Manager 1's, T-ENGINE-007 (N-0849, N-0852) | `port/src/savefile.c` | 2026-11-18 19:33 | +27/-0 | Needed unfinished work: complete as source, never built or run by its owners. | Committed as 65bf5d005. |
|  | `port/include/savefile.h` | 2026-11-18 19:33 | +1/-0 |  |  |
|  | `src/game/gamefile.c` | 2026-11-18 19:33 | +2/-49 |  |  |
|  | `src/include/game/gamefile.h` | 2026-11-18 19:33 | +1/-1 |  |  |
|  | `src/include/constants.h` | 2026-11-18 19:33 | +1/-1 |  |  |
|  | `src/game/cheats.c` | 2026-11-18 19:53 | +163/-146 |  |  |
|  | `port/fast3d/pdgui_menu_cheats.cpp` | 2026-11-18 19:53 | +69/-68 |  |  |
|  | `tests/test_menu_graph.cpp` | 2026-11-19 16:35 | +53/-0 |  |  |
|  | `src/game/mainmenu.c` | 2026-11-19 16:49 | +55/-30 |  |  |
|  | `src/include/game/mainmenu.h` | 2026-11-18 19:34 | +1/-0 |  |  |
|  | `port/fast3d/pdgui_menu_solomission.cpp` | 2026-11-19 16:49 | +4/-4 |  |  |
|  | `src/game/challenge.c` | 2026-11-19 15:19 | +5/-1 |  |  |
|  | `src/game/training.c` | 2026-11-18 19:35 | +43/-20 |  |  |
|  | `src/game/trainingmenus.c` | 2026-11-18 19:35 | +8/-6 |  |  |
|  | `src/include/game/training.h` | 2026-11-18 19:35 | +1/-0 |  |  |
|  | `port/fast3d/pdgui_menu_training.cpp` | 2026-11-18 19:35 | +8/-11 |  |  |
|  | `tests/test_training_renderer_imgui.cpp` | 2026-11-18 19:35 | +26/-1 |  |  |
|  | `port/src/assetcatalog_api.c` | 2026-11-19 16:35 | +2/-1 |  |  |
|  | `src/game/mplayer/mplayer.c` | 2026-11-19 16:35 | +2/-1 |  |  |
|  | `src/game/inv.c` | 2026-11-18 19:40 | +5/-14 |  |  |
|  | `src/game/lv.c` | 2026-11-19 16:35 | +7/-8 |  |  |
|  | `tools/smoke-verify/tests/menu_settings_keyboard.json` | 2026-11-18 19:35 | +5/-10 |  |  |
|  | `context/pillars/menus.md` | 2026-11-19 13:59 | +2/-0 |  |  |
|  | `context/bugs.md` | 2026-11-20 16:32 | +5/-1 |  |  |
|  | `context/session-log.md` | 2026-11-20 16:32 | +4/-0 |  |  |
|  | `context/tasks.md` | 2026-11-19 22:09 | +2/-0 |  |  |
| **Release notes.** Manager 3 (one cheats line), `codex-pd-resume-20261023` (32 gameplay-graph lines), `codex-pd-local-cue-20261010` (one netcode line) | `UNRELEASED.md` | 2026-11-20 16:32 | +71/-0 | Mixed. The cheats line and the netcode line describe code on dev. The 32 gameplay-graph lines describe the parked rewrite of `bondgun.c`, which is not on dev: outdated for now. | The two valid lines committed in 65bf5d005; the 32 others left in place, uncommitted. |
| **Theater replay reader.** Manager 1, `codex-f659e012-9bc298f7`, T-THEATER-001 (N-0851, N-0869) | `port/include/theater_format.h` | 2026-11-18 19:51 | +27/-2 | Needed unfinished work: reviewed source, nothing calls it yet. | Committed as 993765b44. |
|  | `port/src/theater_format.c` | 2026-11-18 19:51 | +131/-65 |  |  |
|  | `tests/test_theater_format.cpp` | 2026-11-18 19:53 | +83/-3 |  |  |
| **Embedded weapon validation, archive clone guard.** Manager 2, `codex-d5caa37e-164844cc`, T-MODDING-016 and T-RELEASE-007 (N-0846, N-0870, N-0873, N-1088) | `port/src/mod_asset_validate.cpp` | 2026-11-18 19:44 | +12/-13 | Needed unfinished work. | Committed as 490f2f348. |
|  | `port/include/mod_asset_validate.h` | 2026-11-18 19:34 | +7/-8 |  |  |
|  | `tests/test_mod_asset_validate.cpp` | 2026-11-19 16:35 | +66/-0 |  |  |
|  | `port/src/asset_archive_clone.c` | 2026-11-19 16:35 | +5/-8 |  |  |
|  | `tests/test_asset_archive_clone.cpp` | 2026-11-19 19:56 | +59/-0 |  |  |
|  | `tests/test_vehicle_model_parts.cpp` | 2026-11-19 16:35 | +2/-1 |  |  |
| **Bundled sample-mod packaging.** Manager 2, `codex-d5caa37e-164844cc`, T-MODDING-016 and T-RELEASE-007 (N-0846, N-0870, N-0873, N-1088) | `devtools/release.ps1` | 2026-11-19 17:01 | +36/-1 | Needed unfinished work; inert while no mod has `release: true`. | Committed as 4ac5f8c73. |
|  | `tools/verify_pdxxx_modder_workflow.py` | 2026-11-18 19:39 | +43/-9 |  |  |
|  | `tools/tests/test_pdxxx_modder_workflow.py` | 2026-11-18 19:44 | +18/-0 |  |  |
|  | `dev-mods/README.md` | 2026-11-18 19:44 | +3/-3 |  |  |
|  | `port/src/modmgr.c` | 2026-11-19 16:35 | +89/-0 |  |  |
|  | `context/evidence/2026-11-18-v005-edited-source-matrix.md` | 2026-11-18 19:57 | new, 8402 bytes |  |  |
| **The switch that bundles the Crystal Pistol.** Manager 2, `codex-d5caa37e-164844cc`, T-MODDING-016 and T-RELEASE-007 (N-0846, N-0870, N-0873, N-1088) | `dev-mods/dev-mods.json` | 2026-11-19 16:35 | +2/-2 | Unclear: a product decision that is Mike's (N-1088 holds it). | Left in place, uncommitted. A release cut from this checkout would bundle the pistol while these two files stay edited. |
|  | `dev-mods/crystal_pistol/mod.json` | 2026-11-18 19:44 | +2/-2 |  |  |
| **Jump and ground-support rewrite.** Manager 4, `codex-8e25800d-a840aeee`, T-ENGINE-006 (N-0861, N-0867, N-0872); `chr.c` also holds three lines of the swarm observer; `types.h` also holds an outdated comment hunk of the October lane; `smoke_harness.c` also holds two leftover hunks that are older forms of code already on dev | `src/game/bot.c` | 2026-11-19 16:35 | +178/-168 | Needed unfinished work, and a real design choice: see the decisions section. | Left in place, uncommitted. On origin in week-pd/inflight-20261120. |
|  | `src/game/botmgr.c` | 2026-11-18 19:32 | +2/-0 |  |  |
|  | `src/game/chr.c` | 2026-11-18 19:45 | +110/-73 |  |  |
|  | `src/game/bondwalk.c` | 2026-11-18 19:45 | +22/-30 |  |  |
|  | `src/include/game/bot.h` | 2026-11-18 19:38 | +1/-0 |  |  |
|  | `src/include/types.h` | 2026-11-18 19:32 | +3/-6 |  |  |
|  | `src/lib/capsule.c` | 2026-11-18 19:45 | +49/-1 |  |  |
|  | `src/include/lib/capsule.h` | 2026-11-18 19:45 | +13/-0 |  |  |
|  | `src/lib/collision.c` | 2026-11-19 16:35 | +2/-0 |  |  |
|  | `src/lib/meshcollision.c` | 2026-11-19 16:35 | +55/-6 |  |  |
|  | `src/include/lib/meshcollision.h` | 2026-11-19 16:35 | +12/-0 |  |  |
|  | `tests/test_jump_two_stage_design.cpp` | 2026-11-18 19:42 | +39/-9 |  |  |
|  | `context/pillars/physics-collision.md` | 2026-11-18 19:46 | +20/-0 |  |  |
|  | `port/src/smoke_harness.c` | 2026-11-20 02:39 | +373/-13 |  |  |
|  | `port/src/smoke_fixture_schema.c` | 2026-11-20 01:48 | +3/-1 |  |  |
|  | `tools/smoke-verify/tests/complex_ledge_player_smoke.json` | 2026-11-18 19:34 | new, 2861 bytes |  |  |
|  | `tools/smoke-verify/tests/complex_ledge_simulant_smoke.json` | 2026-11-18 19:34 | new, 2590 bytes |  |  |
|  | `tools/smoke-verify/tests/villa_tabletop_player_smoke.json` | 2026-11-18 19:42 | new, 3128 bytes |  |  |
|  | `tools/smoke-verify/tests/villa_tabletop_simulant_smoke.json` | 2026-11-18 19:42 | new, 2835 bytes |  |  |
| **Swarm pose observer.** `codex-pd-local-cue-20261010`, T-BENCHMARKING-007 (N-0596, N-0598, N-0603; compile repair N-1133) | `port/include/swarm_audit_pose.h` | 2026-10-12 19:35 | new, 383 bytes | Needed unfinished work; its only caller lines are in the uncommitted `chr.c`. | Left in place, untracked, with `chr.c`. On origin in week-pd/inflight-20261120. |
|  | `port/src/swarm_audit_pose.c` | 2026-11-20 03:00 | new, 7781 bytes |  |  |
| **October gameplay-graph lane (GGV), parked.** `codex-pd-local-cue-20261010` and `codex-pd-resume-20261023`, T-MODDING-011, the October gameplay-graph lane (N-0587 to N-0829; compile repairs N-1125, N-1132) | `port/include/gameplay_graph_bitrep.h` | 2026-10-13 10:11 | new, 2642 bytes | Outdated for dev: Mike's decision 2A parks this lane. Its only caller, the rewrite of `bondgun.c`, was parked on 2026-11-16. Compiled into this checkout's client as dead code; one of its 239 test cases fails. | Left in place. On origin in week-pd/inflight-20261114 (21 of these byte for byte) and week-pd/inflight-20261120 (all). |
|  | `port/include/gameplay_graph_divshift.h` | 2026-10-13 10:26 | new, 2061 bytes |  |  |
|  | `port/include/gameplay_graph_values.h` | 2026-10-26 20:50 | new, 25575 bytes |  |  |
|  | `port/include/graph_persistent_actor.h` | 2026-10-12 18:45 | new, 4403 bytes |  |  |
|  | `port/include/maian_vehicle_mod.h` | 2026-10-13 03:02 | new, 7641 bytes |  |  |
|  | `port/src/gameplay_graph_bitrep.cpp` | 2026-10-13 10:11 | new, 2663 bytes |  |  |
|  | `port/src/gameplay_graph_divshift.cpp` | 2026-10-13 10:26 | new, 2002 bytes |  |  |
|  | `port/src/gameplay_graph_values.cpp` | 2026-11-20 02:39 | new, 272249 bytes |  |  |
|  | `port/src/graph_persistent_actor.cpp` | 2026-10-12 18:45 | new, 8297 bytes |  |  |
|  | `port/src/maian_vehicle_mod.cpp` | 2026-10-13 03:02 | new, 5690 bytes |  |  |
|  | `tests/support/native_hand_lifecycle_oracle.c` | 2026-10-23 21:26 | new, 46471 bytes |  |  |
|  | `tests/support/native_hand_lifecycle_oracle.h` | 2026-10-23 21:26 | new, 18590 bytes |  |  |
|  | `tests/support/native_shooting_flow_oracle.c` | 2026-11-20 02:39 | new, 233219 bytes |  |  |
|  | `tests/support/native_shooting_flow_oracle.h` | 2026-10-13 10:22 | new, 29576 bytes |  |  |
|  | `tests/test_gameplay_graph_bitrep.cpp` | 2026-10-13 10:11 | new, 10820 bytes |  |  |
|  | `tests/test_gameplay_graph_divshift.cpp` | 2026-10-13 10:26 | new, 20787 bytes |  |  |
|  | `tests/test_gameplay_graph_values.cpp` | 2026-11-20 02:56 | new, 528088 bytes |  |  |
|  | `tests/test_graph_persistent_actor.cpp` | 2026-10-12 18:46 | new, 13414 bytes |  |  |
|  | `tests/test_maian_vehicle_mod.cpp` | 2026-10-13 03:06 | new, 27048 bytes |  |  |
|  | `CMakeLists.txt` | 2026-11-20 01:48 | +12/-0 |  |  |
| **Leftovers of earlier landings.** `codex-d5caa37e-164844cc` (T-MODDING-018, no note), `codex-pd-resume-20261023` (N-0815, N-0816), no owner for the blank line | `src/game/propobj.c` | 2026-11-20 05:45 | +3/-0 | Already on dev in another form: comment lines and a test that duplicate committed ones, an October draft of a test whose newer copy is tracked, one blank line. | Left in place, uncommitted. Safe to discard. |
|  | `tests/test_custom_projectile_log_static.cpp` | 2026-11-20 01:48 | +18/-0 |  |  |
|  | `tests/test_gun_load_target_storage.cpp.inflight-20261117` | 2026-10-26 20:05 | new, 2320 bytes |  |  |
|  | `context/pillars/modding.md` | 2026-11-19 17:57 | +1/-0 |  |  |
| **Project records.** every session (Workbench server), the weekly audit job, every week session (week note) | `tools/Workbench/data/changelog.jsonl` | 2026-11-20 16:29 | +1389/-0 | Needed: the project's own records. | Committed as the records commit that adds this file. |
|  | `tools/Workbench/data/notes.jsonl` | 2026-11-20 14:39 | +864/-0 |  |  |
|  | `tools/Workbench/data/roadmap.json` | 2026-11-20 16:29 | +2030/-37 |  |  |
|  | `context/audits/2026-11-20-full.md` | 2026-11-20 13:21 | new, 55796 bytes |  |  |
|  | `context/week-pd-coordination.md` | 2026-11-20 16:41 | new, 569381 bytes |  |  |

| Group and owner | Folders | Files | Size | Newest file (UTC) |
|---|---|---|---|---|
| **Front-end control audit runs.** `codex-36063664-58715203`, T-MENUS-003, V-004 | `.claude/frontend-followup-1119/`, `.claude/frontend1119/`, `.claude/lobby-ui-verification-20261119/` | 23191 | 5176.7 MB | 2026-11-20 00:29 |
| **Manager 3 freeze records.** `codex-012282b0-8ddc2a4f`, T-MENUS-008 | `.claude/menu1118/` | 9 | 0.0 MB | 2026-11-18 19:58 |
| **September menu runs.** `codex-43ce463e-29f381f8`, T-MENUS-003 to -005 | `.claude/menu0927/` | 8801 | 2338.8 MB | 2026-09-30 02:38 |
| **Startup timing runs (P-001).** `codex-a2a26e2b-7dc21084` | `.claude/p001-stage-ledger-20261119/`, `.claude/p001-startup-20261118/`, `.claude/p001-startup-20261119/` | 72262 | 8874.4 MB | 2026-11-19 18:24 |
| **October gameplay-graph evidence.** `codex-pd-resume-20261023`, T-MODDING-011 | 32 folders matching `.claude/gameplay-*/` | 270 | 92.7 MB | 2026-10-26 20:56 |
| **October graph and model work.** `codex-pd-local-cue-20261010`, T-MODDING-011, T-TOOLING-006 | 6 folders matching `.claude/pd-gameplay-semantic-20261013/`, `.claude/pd-maian-models-20261013/`, `.claude/pd-retained-native-candidate-20261012/`, `.claude/pd-weapon-scatter-graph-plan-20261012/`, `.claude/smoke-process-cues/`, `.claude/joint-planning-20261011/` | 8902 | 446.3 MB | 2026-10-14 00:18 |
| **Shared integration evidence.** many sessions (72 notes), T-TOOLING-008, T-TOOLING-011, T-MODDING-011 | `.claude/pd-initial-integration/` | 4264 | 756.2 MB | 2026-11-19 15:59 |
| **Modder manifest fix.** `01a127d5...`, T-MODDING-012, T-TOOLING-013 | `.claude/pd-modder-manifest-fix-20261012/` | 125 | 5.1 MB | 2026-10-12 13:55 |
| **Grid browser and PR 8 landing.** `codex-b8057579-d2768fbc`, `codex-d248f028-1c880c6c`, T-MODDING-021, T-MODDING-015 | `.claude/grid-browser-20261119/`, `.claude/pr8-local-build-4d60e5e0e/`, `.claude/pr8-preserved-untracked-4d60e5e0e/` | 23 | 81.3 MB | 2026-11-19 21:34 |
| **Compile repair patches.** `jenkins-root-review-compile-unblock-20261120` (N-1124, N-1125, N-1137) | `.claude/jenkins-compile-unblock-N1124-N1125/` | 15 | 0.0 MB | 2026-11-20 03:11 |
| **Manager 4 freeze records.** `codex-8e25800d-a840aeee`, T-ENGINE-006 | `.claude/movement-nov18-verify/` | 2 | 0.0 MB | 2026-11-18 19:53 |
| **Older asset and netcode evidence.** `01a0f5c1...` (T-TOOLING-009), `codex-f9571c12-6af53c63`, `codex-0e8900ca-467b81e8` (T-TOOLING-005), `codex-385f1c85-675768a6` | `.claude/asset-inspection/`, `.claude/asset1012-gltf-features/`, `.claude/cleanup-manifests/`, `.claude/mp0927-movement/` | 319 | 170.1 MB | 2026-11-14 19:06 |
| **Smoke store and its self-tests.** the smoke tools (T-TOOLING-006, -015, -016) | `.claude/native-write-policy-tests/`, `.claude/smoke-storage-selftests/`, `.claude/smoke-storage-validation/`, `.claude/smoke-storage/` | 89219 | 3220.9 MB | 2026-11-20 14:38 |
| **Early crystal pistol renders.** no note; 38 PNG renders and two Blender scripts of 2026-10-13 | `__cp_review/` | 40 | 17.1 MB | 2026-10-14 01:28 |
| **Total** | 62 folders | 207442 | 20.7 GB | |
