# Step 9 finalization batch and the v0.1.106 prerelease (2026-11-22)

Integration (claude-pd-integration-20261121) ran the lanes' finalization smokes in C:\PD2\candidate with the
candidate's own `tools/smoke-verify/run.ps1`: `-Prepare` outside the hub's game-runtime slot, `-Execute` inside
it, each install to the Recycle Bin after its run (receipts and logs kept under
`C:\PD2\candidate\.claude\smoke-storage\receipts`). The base install was `shared-base-15` (base 14 plus the
candidate exe, archives re-extracted once). The checkout moved only between records, after checking that the
game-code diff (`src port include CMakeLists.txt`) was exactly the expected PR's files; the exe was rebuilt clean
only when that diff was non-empty.

## Candidates

| Candidate | dev | Game code since the previous candidate | PerfectDark.exe sha256 | pd-tests (cases / assertions) |
|---|---|---|---|---|
| 1 | 96882002 | (start) | 72d1e9cc...3140 | 2,636 / 720,734 |
| 2 | fbf2822d | PR 45, D-052's ground search (chr.c, bot.c, bot.h) | 1f0fcfe5...d513 | 2,637 / 720,772 |
| 3 | 83d3aeb2 | PR 48, B-1306 (bot.c +30) | 6ac0a87c...115e | 2,639 / 720,832 |
| 3b | 1e9c6a34 | PR 53, B-1312 (pdgui_bridge.c +17, pdgui_menu_room.cpp +11) | d2cefa89...6a48 | 2,640 / 720,856 |
| 3c | eb3ba06da | PR 54, B-1313 (asset_runtime.c +70/-8, asset_runtime.h +7) | 0ca2010f...e2d6 | 2,641 / 720,867 |

## Results by group

| Group | Smokes | Result | On |
|---|---|---|---|
| G1, local MP modes | Combat, Hacker Central, Hold the Briefcase, Capture the Case, King of the Hill, Pop a Cap | 6/6 (Combat in R4 with PR 50's fixture; B-1305's fixtures) | 3 |
| G2, ledges and tables | complex_ledge_player, complex_ledge_simulant, villa_tabletop_player, villa_tabletop_simulant | 0/4: their first runs ever; harness and fixture faults, B-1310 (PR 51, held for candidate 4) | 3 |
| G3, co-op | network_coop_reconnect 55/55, network_coop_mission_end 132/132, coop_ai_buddies_offline 42/42 (after B-1311's fixture and B-1312's fix) | 3/3 | 3, 3, 3b |
| G4a, networked modes | the six mp_*_net smokes (protocol 67) | 6/6 | 3 |
| G4b, reconnect | five mp_*_reconnect smokes, 120/120 each | 5/5 | 3 |
| G5, weapons | boot 14/14, held_weapon_cadence 29/29, projectile_events 70/70, mod_author weapon create and play 22/22 each, held_weapon_shot 30/30 (PR 46's fixture) | 6/6 | 1, 3 |
| G6, authored mods | character_tool_create 26/26, crystal_pistol_viewmodel 31/31, crystal_pistol_ammo 31/31; authored_bundled_first_run 26/26 and authored_bundled_user_copy 18/18 (after B-1313; 24/26 and 17/18 on 3b); maian_vehicle_room_drive 50/50 and maian_vehicle_room_boost 60/60. Known misses: character_tool_play 38/41 (a stale fixture, not the game); maian_vehicle_drive 37/38 and maian_vehicle_boost 38/48 on Escape, where the mission's cutscene ends the ride (B-1241, the same counts since 2026-11-17) | 7/7 proofs | 3b, 3c |
| G7, Room | room_load_preset 40/40, room_quick_start 40/40 | 2/2 | 3b |
| Startup A/B (V-015) | empty cohort 18/18, five detected but disabled mods 23/23: about 0.02 s more at the mod manager's start | pass | v0.1.105 template |

## The v0.1.106 prerelease

- Published 16:47:51 UTC as "Perfect Dark 2 v0.1.106 (Dev)":
  https://github.com/MikeHazeJr/perfect-dark-2/releases/tag/v0.1.106, tagged on dev 5b8ef3d0. Its game code is
  the version commit's (180cce91: candidate 3c, eb3ba06da, plus CMakeLists.txt's version); the commits between
  are tooling and context only (q7 B-1314, q10 B-1315 and B-1316).
- Run from the fresh clone `C:\PD2\release-clone` as `release.ps1 -Prerelease -KeepOldReleases -Version 0.1.106
  -NotesFile <concise notes> -SigningKey <the main checkout's dev-keys key>`, holding the hub's Git lane and then
  its build slot. The script made no commit of its own (the version came through the build queue, q8), and step 6
  kept the older Dev prereleases.
- Signed with the release key: its public half is the key `port/include/updater_pubkey.h` embeds (`3e26c3d0...`, the
  same as v0.1.105's), checked from the public files before the run and by `sign-release.ps1` at step 2b.
- Assets: PerfectDark-v0.1.106-win64.zip, 46,814,697 bytes and 42 files (GitHub's sha256 ddc9b780..., the hash the run signed), its .sha256 (97 bytes) and .sig (64 bytes), PerfectDark.exe (113,046,225 bytes) and Updater.exe (12,868,823 bytes).
- The first real run (16:20 to 16:25 UTC) published nothing: it hit B-1315 (a key made on the spot in a clone
  without `dev-keys/`, and the regenerated header committed) and stopped on B-1316 before its push. Both were
  fixed through the build queue (q10, 5b8ef3d0) before this run. Evidence: `C:\PD2\release-evidence-20261122`, and the
  clone's local branch `b1315-evidence`.

## The record, as it was kept

Candidate: dev 96882002 (PRs 31, 33, 39, 40 and 42 in). Built clean in C:\PD2\candidate, -Target full:
- PerfectDark.exe sha256 72d1e9cc...3140;
- pd-tests: 2,636 cases, 720,734 assertions, all passed.

Base 15: C:\Users\mikeh\Perfect-Dark-2\pd2-smoke-bases\shared-base-15, a copy of base 14 with the candidate exe.
- --extract-assets-only pass 1 re-extracted 86 weapons, 47 arenas, 87 scenarios and 134 meta archives, 0 failed.
- Pass 2 wrote nothing.

Runner: the candidate's tools/smoke-verify/run.ps1, -Prepare outside the slot, then -Execute in the hub
game-runtime slot. Policy: the defaults plus discard_completed, so each install goes to the Recycle Bin
after its run, with receipts and logs kept. Receipts: C:\PD2\candidate\.claude\smoke-storage\receipts\<id>.

| Group | Smoke | Result | Note |
|---|---|---|---|
| G1a | mp_combat_local_scorelimit_smoke | FAIL 28/31 | timelimit end, no score; 0 bot-on-bot hits in 240 s, 6 on the player, 20 STUCK, 1 suicide |
| G1a | mp_hacker_central_local_smoke | FAIL 38/39 | no score in 150 s; 17 bot-on-bot hits, 1 death |
| G1a | mp_hold_the_briefcase_local_smoke | FAIL 36/37 | chr 8 scored 4; the fixture wants spawn>=1, and no one died |

G1a ran 04:41 to 05:00 UTC (1,148 s). All simulants log "BOT.TEAM: team=1 teamisonlyai=1 teams=0".
Reported to MP-RULES (Session C) at 05:03.
| G1b | mp_pop_a_cap_local_smoke | PASS 32/32 | 4 STUCK, 21 bot-on-bot hits |
| G1b | mp_king_of_the_hill_local_smoke | FAIL 36/38 | spawn=0 (fixture) and no score; 22 STUCK, 2 bot-on-bot |
| G1b | mp_capture_the_case_local_smoke | FAIL 38/40 | spawn=0 (fixture) and no score; 21 STUCK, 5 bot-on-bot, 8 on the player |

G1b ran 05:17 to 05:38 UTC (1,288 s).

MP-RULES' verdict (05:27):
- The spawn= checks are a fixture mistake: MODE_EVENT_SPAWN counts respawns only. All six local fixtures get relaxed.
- Combat and Hacker Central (and the STUCK-heavy runs) are a real D-052 regression in walking simulants' ground search:
  - With Bot Jumping on (the default since PR 23), the search reaches only manground + 20 in chrFindMovementGround.
  - The original search reaches 69 units.
  - So simulants stall on stairs and ramps.
- The fix is in progress (src/game/chr.c, bot.c, bot.h, the six fixtures). It changes no archive kind, so base 15 stays.
- Held for the rebuilt candidate: G1 (all six), G2, G3, G4.

| G5a | boot_smoke | PASS 14/14 | base 15: DATA INTEGRITY 2037 validated, 0 re-extracted (pass 1 of base 15 did the re-extract); 0 "program refused"; six MODE.GRAPH program lines; LANG.STAGE source=archive on citraining |
| G5a | held_weapon_cadence_smoke | PASS 29/29 | no smoke/boltbeam warnings |
| G5a | projectile_events_smoke | PASS 70/70 | 8 in_flight lines, each modules= equal to native= (pinball grenade hit_sound+bounce_smoke, rocket launcher thrust, Slayer powered and fly-by-wire thrust, Devastator both trail, knife woosh+hit_sound, Super Dragon grenade trail); PROJECTILE.EVENTS fuse.differ=0 rules.compared=75 rules.differ=0; WEAPON.EVENTS rules.compared=70 rules.differ=0; LANG.STAGE source=archive on Felicity; no smoke/boltbeam warnings |

G5a ran 06:07 to 06:23 UTC (915 s), 3 pass, 0 fail. Not covered by a smoke: the homing rocket's home+trail
line (projectile_events_smoke fires the rocket launcher's primary only) and the simulant-fired Slayer
(Core Weapons 03:29: no program, pd-tests cover it).
| G5b | held_weapon_shot_smoke | FAIL 29/30 | the RC-P120's fire-mode toggle (action 22 at 01:04.32, press consumed by a frame) did not select the cloak: both later shots used the primary. Super Dragon, tranquilizer and Laptop gun toggles worked. RC-P120 models compiled at the give (late-add, missed by pre-scan). Reported to Core Weapons; rerun on the rebuild |
| G5b | mod_author_weapon_create_smoke | PASS 22/22 | |
| G5b | mod_author_weapon_play_smoke | PASS 22/22 | |

G5b ran 07:17 to 07:36 UTC (1,144 s), 2 pass, 1 fail.

### Candidate 2: dev fbf2822d (PR 45 merged by MP-RULES)

- Clean -Target full build in C:\PD2\candidate in 54 s (compiler cache warm from 96882002), 07:38 to 07:39 UTC.
- PerfectDark.exe sha256 1f0fcfe51b4c3c6497fa36b463c13f48ac3c7ff89ee14d215630e3b9d2e9d513.
- pd-tests: 2,637 cases, 720,772 assertions, all passed (07:40 UTC).
- Base 15 unchanged (PR 45 changes no archive kind). Bundled samples re-staged from fbf2822d.

G5b's held_weapon_shot_smoke failure is a smoke fault (Core Weapons, received by 07:45 UTC): under extended controls the
RCP120's alt button toggles the cloak itself (bondgun.c bgunConsiderToggleGunFunction, the RCP120 case), so the
secondary's mode_toggle node is never pressed. PR 46 (tools only) fires the primary and asserts its line; the
cloak's drain of 24 stays covered by pd-tests. Rerun in R4 with PR 46's definition.

| Record | Smoke | Result | Note |
|---|---|---|---|
| R1 | mp_combat_local_scorelimit_smoke | FAIL 28/31 | 240 s, time-limit end, all scores 0 and deaths 0; 21 DMG_SCALED, 4 DMG_APPLIED, no kill; STUCK 0 (was 20); simulants BOT.TEAM team=1 teamisonlyai=1 teams=0 |
| R1 | mp_hacker_central_local_smoke | FAIL 38/39 | ended by scorelimit=10 inside 150 s (chr 9 scored 10); the fixture wants reason=timelimit |
| R1 | mp_hold_the_briefcase_local_smoke | PASS 37/37 | chr 10 scored 3, time-limit end |

R1 ran 07:50 to 08:10 UTC (1,195 s) on candidate 2. Sent to MP-RULES by 08:11 UTC with the receipts.

MP-RULES' verdict on R1 (received by 08:23 UTC): both failures are fixture assumptions, fixed as B-1305
(fixtures and a static test only; no data or binary change):
- Hacker Central: a simulant downloaded five times (43 s to 124 s, 2 points each) and reached the default
  score limit 10 inside 150 s. The five objective smokes now pass --match-scorelimit 100
  --match-teamscorelimit 400 and require both BOOT lines.
- Combat: the simulants are enemies (21 bot-on-bot hits); kills are rare because shields refill from
  Felicity's two pickups. Both Combat score-limit smokes (local and the networked host) seed
  --debug-mp-options 0x10200001 (One-Hit Kills) and require "MATCH: options=0x10200001 onehitkills=1".
Reruns after B-1305 and PR 46 land: Combat and Hacker Central (local), plus any R2 smoke that ends by
score limit; G4's networked Combat runs with the new fixture.
| R2 | mp_capture_the_case_local_smoke | PASS 40/40 | time-limit end; chr 10 (team 1) scored 6; STUCK 10 (was 21) |
| R2 | mp_king_of_the_hill_local_smoke | PASS 38/38 | time-limit end; chr 10 (team 1) scored 6; STUCK 1 (was 22) |
| R2 | mp_pop_a_cap_local_smoke | PASS 32/32 | time-limit end; chr 10 scored 2; STUCK 0 (was 4) |

R2 ran 08:19 to 08:45 UTC (1,544 s), 3 pass. None ended by score limit, so no R2 rerun after B-1305.

### Release dry run (10:02 to 10:04 UTC, fresh clone C:\PD2\release-clone at dev 1dc7a7b8)

- release.ps1 -DryRun -Prerelease exit 0, in the hub's build slot. Version 0.1.106 (previous max 0.1.105).
- Pre-build sync, sign, tag, push, GitHub release and prune all skipped as a dry run should (B-1304 holds):
  0 commits ahead of origin/dev; the only change is the local CMakeLists.txt version bump (restored after,
  or the real run would compute 0.1.107).
- Zip PerfectDark-v0.1.106-win64.zip, 44.6 MB, 42 files = v0.1.105's 35 plus the five mods/bundled/*.pdmod,
  bundled-mods.sha256 and Open Mods Folder.lnk; nothing dropped, no ROM, base/, server or tests.
- Typed archives validated (20 in the distribution plus each bundled sample's own); BYOR check: no
  ROM-derived content among the 42 release files. "Data: MISSING" is expected (v0.1.105 had no data/ either).
- The clone excluded /Tools/Workbench/exports/ in .git/info/exclude (B-1309). Its first two attempts failed
  on the harness, not the release: git refuses the main checkout as a --reference (core.hooksPath), and
  Windows PowerShell turned git's stderr progress into an error.

### Candidate 3: dev 83d3aeb2 (PR 48 B-1306, plus PR 46 and PR 47 fixtures; game code vs candidate 2: bot.c +30)

- Clean -Target full build 40 s (10:08 UTC). PerfectDark.exe sha256 6ac0a87c5371f6c76a9ca7056085f127fb9c6e95e8f765819a724fb04243115e.
- pd-tests: 2,639 cases, 720,832 assertions, all passed (10:09 UTC). Base 15 unchanged. Samples re-staged (same inventory).

| Record | Smoke | Result | Note |
|---|---|---|---|
| R1' (c3) | mp_combat_local_scorelimit_smoke | FAIL 32/33 | "BOOT: --debug-mp-options applied user=0x10200001", but "MATCH: options=0x10200000 onehitkills=0" (bit 0 cleared before the match); ended reason=scorelimit at 03:58 by a chance kill (chr 10) |
| R1' (c3) | mp_hacker_central_local_smoke | PASS 41/41 | B-1305 score limits; time-limit end |
| R1' (c3) | mp_hold_the_briefcase_local_smoke | PASS 39/39 | |

R1' ran 10:20 to 10:40 UTC (1,165 s) on candidate 3. Combat sent to MP-RULES at 10:41.
| R2' (c3) | mp_capture_the_case_local_smoke | PASS 42/42 | time-limit end; chr 8 scored 3; STUCK 1 (21 on c1, 10 on c2): slot 0 at 00:27.48, 2 s after bot_idle who=1, sent to MP-RULES |
| R2' (c3) | mp_king_of_the_hill_local_smoke | PASS 40/40 | time-limit end; chr 8 scored 7; STUCK 0 (22 on c1, 1 on c2) |
| R2' (c3) | mp_pop_a_cap_local_smoke | PASS 34/34 | time-limit end; chr 10 scored 2; STUCK 0 |

R2' ran 10:59 to 11:19 UTC (1,217 s) on candidate 3, 3 pass.

MP-RULES on R2' CTC's one STUCK (received by 11:22 UTC): a route case, not station. Slot 0 is chrnum 2, simulant 3
(team 2), on a GETCASE -> GETITEM route to the shield at (-1218,560,-443), room 24's upper level about 460
units up; under 10 units in 3 s, moved once; the SPAWN.CLIP burst is the watchdog's own waypoint search. STUCK
21 -> 10 -> 1 (CTC) and 22 -> 1 -> 0 (KOH) across the candidates: nothing more to fix.
| R3 (c3) | complex_ledge_player_smoke | FAIL 7/10 | 00:23: ledge_landing_missing chrnum=5001 takeoff=1 descended=1 landed=0 (no land on the y=551 support, room 24) |
| R3 (c3) | complex_ledge_simulant_smoke | FAIL 8/11 | 00:24: ledge_landing_missing chrnum=0 takeoff=1 descended=0 landed=0 |
| R3 (c3) | villa_tabletop_player_smoke | FAIL 6/10 | 00:22: authored_tabletop_contact_missing takeoff=0; the coffee table never bound (step 1/10, phase=table_bound) |

R3 ran 11:27 to 11:33 UTC (350 s) on candidate 3, 0 pass. All four G2 smokes came with D-052 (386a9389, 11-20);
no recorded pass in context. Sent to MP-RULES at about 11:35.

MP-RULES on R3 (received by 11:39 UTC): the four G2 smokes never passed on any base. D-052's commit (386a9389)
lists them as "not run yet", and their first admission was refused for disk. R3 was their first run ever; not a
regression from PR 45, PR 48 or base 15. Smoke and harness faults, filed as B-1310:
- complex_ledge_player: the fixture presses ACTION_MOVE_FORWARD, but bondmove.c reads ACTION_AXIS_MOVE_Y, so the
  player jumped in place (feet 512 -> 636 -> 512 at x -1392, z -700; the ledge needs z -628 to -500). Walk-off
  needs a backward axis, which injection cannot set yet.
- villa_tabletop_player: the contact ray at the authored table-top point in smoke_harness.c (x -4065, y -240,
  z 2348, room 17) missed the coffee table: a harness code change.
- complex_ledge_simulant: the jump fired and the simulant cleared the ledge, but HOLD does not stop item pickups,
  and the harness never saw the "descended above 553" sample it needs before counting a landing.
Decision (MP-RULES' recommendation, taken): candidate 3 does not wait on G2. G3, G4, G6, G7 and A/B run now; G2's
fixed smokes need a new binary and go to the next candidate. Whether D-052's ledge and tabletop proofs must pass
before a release is Mike's call (MP-RULES asks him); until then that landing is unproven in play, and the release
notes say so.
| R4 (c3) | held_weapon_shot_smoke | PASS 30/30 | PR 46's fixture: the RCP120 primary line |
| R4 (c3) | mp_combat_local_scorelimit_smoke | PASS 31/31 | PR 50's fixture (600 s backstop); score-limit end |
| R4 (c3) | villa_tabletop_simulant_smoke | FAIL 7/11 | authored_tabletop_contact_missing (B-1310, the harness's table point), same as the player twin |

R4 ran 11:41 to 11:52 UTC (671 s) on candidate 3 at checkout fb107aef. MP-RULES' 21 s build ran 11:49:34 to
11:49:56 during the Combat smoke, which passed. G1 is now 6/6 on candidate 3 (R1' 2/3 + this Combat rerun, R2' 3/3).
B-1310's fix is PR 51 (smoke_harness.c and bot.c: a binary change), held for the next candidate.
| R5 (c3) | network_coop_reconnect_smoke | PASS 55/55 | B-1277 |
| R5 (c3) | coop_ai_buddies_offline_smoke | FAIL 38/42 | B-1278's first game run since its 11-21 fix: on Defection (buddies=1 of 2, max 1) offline_gameplay_ready never held; NET.HEARTBEAT chrs=0; the debug end-mission at 01:24 brought no end screen; ran to 422 s |

R5 ran 11:58 to 12:11 UTC (812 s) on candidate 3. Sent to MP-RULES at about 12:13.

MP-RULES on R5's coop_ai_buddies_offline_smoke (received by 12:14 UTC): the game did everything right (Room, Start
Match, Defection, the buddy spawned "1 of 1", the mission end at 84.57 and the end screen at 84.58). Fixture timing:
the cutscene-skip assist fired in the CI fly-in, so Defection's ~64 s intro was not skipped; --debug-end-mission
40:complete counts stage time with the intro and fires at the first gameplay frame, so gameplay never held still
for the wait's 2 s. Fix (test only): --debug-end-mission 100:complete. Rerun after it lands.
| R6 (c3) | network_coop_mission_end_smoke | PASS 132/132 | B-1281 |
| R6 (c3) | mp_combat_net_bots_smoke | PASS 71/71 | protocol v67 |
| R6 (c3) | mp_hold_the_briefcase_net_smoke | PASS 88/88 | protocol v67 |

R6 ran 12:19 to 12:39 UTC (1,232 s) on candidate 3, 3 pass.
| R7 (c3) | mp_capture_the_case_net_smoke | PASS 87/87 | protocol v67 |
| R7 (c3) | mp_hacker_central_net_smoke | PASS 89/89 | protocol v67 |
| R7 (c3) | mp_king_of_the_hill_net_smoke | PASS 84/84 | protocol v67 |

R7 ran 12:46 to 13:14 UTC (1,681 s) on candidate 3, 3 pass.
| R8 (c3) | mp_pop_a_cap_net_smoke | PASS 82/82 | G4a complete, 6/6 |
| R8 (c3) | mp_hold_the_briefcase_reconnect_smoke | PASS 120/120 | |
| R8 (c3) | coop_ai_buddies_offline_smoke | FAIL 38/42 | PR 52's 100 s end in effect: the intro ran 00:20 to 01:24, the buddy spawned, the end came at 02:00 with the end screen; but offline_gameplay_ready began only once (00:15.77 on 0x26), reset at 00:15.81 and never re-armed on 0x30; wait_condition_timeout at 07:00 |

R8 ran 13:22 to 13:46 UTC (1,396 s) on candidate 3 at checkout 11cb964d. Sent to MP-RULES.

MP-RULES on R8's coop_ai_buddies_offline_smoke (received by 13:51 UTC): a code bug, B-1312, needing a new binary.
The offline Room pushed imgui_menu (depth 2, with absolute mouse mode) at 16.97 and it stayed on top through the
whole mission until the end screen took the slot at 02:00.33; offline_gameplay_ready needs layer_game on top. Cause
(pdgui_menu_room.cpp): the solo Room's Start Match paths (Combat and Campaign) call pdguiSoloRoomClose(), which only
clears flags; Leave releases MENU_TYPE_ROOM first. --launch-offline-room opens the Room without the Main Menu's push,
so the Room owns imgui_menu and the layer outlives it (B-1042 fixed this for the listen host's lobby only). Fix:
release MENU_TYPE_ROOM before closing on both Start Match paths; its own PR, held until the cut; candidate 4 reruns
the smoke (expect INPUTCTX to pop imgui_menu at Start Match, then offline_gameplay_ready).
Release notes: the offline AI-buddy co-op start is proven up to the mission end (the mission played, the buddy
spawned, it ended to its results); whether a player starting from the Room keeps the menu layer above gameplay is
unproven in play.
| R9 (c3) | mp_capture_the_case_reconnect_smoke | PASS 120/120 | |
| R9 (c3) | mp_hacker_central_reconnect_smoke | PASS 120/120 | |

R9 ran 14:09 to 14:20 UTC (666 s) on candidate 3, 2 pass. G4b 3/5.
| R10 (c3) | mp_king_of_the_hill_reconnect_smoke | PASS 120/120 | |
| R10 (c3) | mp_pop_a_cap_reconnect_smoke | PASS 120/120 | |

R10 ran 14:29 to 14:40 UTC (676 s) on candidate 3, 2 pass. G4 complete: G4a 6/6, G4b 5/5.

### Startup A/B pair (V-015; 14:41 to 14:49 UTC; cold pair, warm not done; runner of the candidate clone)

Template: the published v0.1.105 zip extracted (client 4efa7f4f..., 8556dc8de), pins checked: receipt.json
e9b12b45..., the client hash, all 36 manifest files; the pair folder copied to C:\PD2\pd2-startup-ab (14 files,
hashes equal) because its fixtures resolve next to the runner's checkout.
| Cohort | Result | modmgr initializing -> scanning -> scan complete -> initialized | found / enabled / components |
|---|---|---|---|
| startup_empty_mods | PASS 18/18 | 00:03.85 -> 00:03.85 -> 00:03.85 -> 00:03.85 | 0 / 0 / 0 |
| startup_detected_disabled_mods | PASS 23/23 | 00:03.83 -> 00:03.84 -> 00:03.85 -> 00:03.85 | 5 / 0 / 0 |
Verdict for V-015: five detected but disabled packages add about 0.02 s to v0.1.105's mod manager start; both
cohorts reach the title. First audio and first input response are separate witnesses that need a person; not
recorded. Approximate pair (retained copies, not the incident's own bytes), as STARTUP-AB.md says.

### Candidate 3b: dev 1e9c6a34 (candidate 3 plus PR 53, B-1312; game code: pdgui_bridge.c +17, pdgui_menu_room.cpp +11)

- Clean -Target full build 38 s (14:50 UTC). PerfectDark.exe sha256 d2cefa895f5eb811f2a32eb8694292a7aaf8ad3131da6ceaf5d85a98c4516a48.
- pd-tests: 2,640 cases, 720,856 assertions, all passed (14:51 UTC). Base 15 unchanged. Release candidate.
- R1' to R10 stand on candidate 3's binary: PR 53 touches only the solo Room's co-op start (Combat's Room start
  already released the pool in mpStartMatch; the new release is a no-op there).

| Record | Smoke | Result | Note |
|---|---|---|---|
| R11 (3b) | coop_ai_buddies_offline_smoke | PASS 42/42 | B-1312's fix proven: the offline co-op start from the Room works end to end |
| R11 (3b) | authored_bundled_first_run_smoke | FAIL 24/26 | the bundled Maian Hover is rejected at load: "behavior graph refused: node 'boost' (og.vehicle.boost_heat) param 'boost_speed' uses unknown variable '$boost_speed'", transactional catalog admission -1; the other four samples load |
| R11 (3b) | authored_bundled_user_copy_smoke | FAIL 17/18 | same rejection |

R11 ran 14:57 to 15:04 UTC (427 s) on candidate 3b. A release blocker: the dry-run zip carries this package.
Posted for Authored Mods in the hub.
| R12 (3b) | character_tool_create_smoke | PASS 26/26 | |
| R12 (3b) | crystal_pistol_viewmodel_smoke | PASS 31/31 | |
| R12 (3b) | character_tool_play_smoke | FAIL 38/41 | fixture fault, not the game: the four simulants were allocated with mod:character_standin_veteran and rendered; the smoke expects "MATCHSETUP: bot slot ..." (removed from the game in b88d7ed66, 2026-08-14) and the chrRender-matrix-ondemand trace (only when model->matrices is NULL). Posted for Authored Mods |

R12 ran 15:12 to 15:20 UTC (464 s) on candidate 3b.
| R13 (3b) | crystal_pistol_ammo_smoke | PASS 31/31 | |
| R13 (3b) | room_load_preset_smoke | PASS 40/40 | N-1193's check (preset loaded, SCENARIO.PRESET committed, the B-1295 name) |
| R13 (3b) | room_quick_start_smoke | PASS 40/40 | |

R13 ran 15:28 to 15:34 UTC (366 s) on candidate 3b, 3 pass.

### Candidate 3c: dev eb3ba06da (3b plus q6 edbb0db8 and PR 54, B-1313; game code: asset_runtime.c +70/-8, asset_runtime.h +7)

- q6 (edbb0db8, 15:43 UTC): UNRELEASED.md +43/-2 and devtools/release.ps1 +4/-2 only (v0.1.106's lines,
  protocol 67, -KeepOldReleases). PR 54 merged by Core Weapons at 15:44 UTC as eb3ba06da (parents edbb0db8,
  4293c91e); all files since 3b: those two, asset_runtime.c/.h, tests/test_asset_runtime_adapters.cpp +55,
  context/bugs.md +30. dev-mods and tools/stage_bundled_samples.py unchanged since fbf2822d, so the staged
  samples (C:\PD2\pd2-authored-smoke\stage) stand.
- Checkout moved 1e9c6a34 -> eb3ba06da at 15:46 UTC (no record open). Clean -Target full build 42 s (15:47 UTC).
  PerfectDark.exe sha256 0ca2010f113d64d603317e70b4ce243d1d2345ea7bfc81f578b4f84af3bae2d6.
- pd-tests: 2,641 cases, 720,867 assertions, all passed (15:48 UTC).
- R14 prepared from 15:49 UTC: authored_bundled_first_run, authored_bundled_user_copy (the nested layout,
  B-1313's direct proof), maian_vehicle_drive, maian_vehicle_boost (the dev-mods copy, one archive deep: the
  old path).

| Record | Smoke | Result | Note |
|---|---|---|---|
| R14 (3c) | authored_bundled_first_run_smoke | PASS 26/26 | B-1313 proven: the bundled Maian Hover loads (nested layout) with the other four samples |
| R14 (3c) | authored_bundled_user_copy_smoke | PASS 18/18 | |
| R14 (3c) | maian_vehicle_boost_smoke | FAIL 38/48 | B-1241, known since 2026-11-17 (same count): on Escape, objective 2 completes and cut_tra_mid dismounts the rider at 28.84, 0.65 s into the boost. The variables load on this path: "boost_speed=7.00 ramp=240 overheat=960 overheated_boost=0.50 recover=480" |
| R14 (3c) | maian_vehicle_drive_smoke | FAIL 37/38 | B-1241, same count as 2026-11-17: the cutscene dismounts at 29.07, before the steering step |

R14 ran 15:57 to 16:05 UTC (465 s) on candidate 3c. B-1241 moved the boost, heat and steering proofs to the
Blank Room (maian_vehicle_room_drive_smoke 50/50 and maian_vehicle_room_boost_smoke 60/60 on 2026-11-17); the
Escape pair stays for the mount and the seats. R15 runs the two room smokes on 3c.
| R15 (3c) | maian_vehicle_room_boost_smoke | PASS 60/60 | B-1241's Blank Room proof: boost to full, overheat, recovery |
| R15 (3c) | maian_vehicle_room_drive_smoke | PASS 50/50 | accelerate and steer with no mission to end the ride |

R15 ran 16:15 to 16:19 UTC (255 s) on candidate 3c, 2 pass. Candidate 3c is the release candidate.

### Candidate 4 (after the cut)

Core Weapons' PR 44 and PR 49 (held weapon slices 6a to 8b) and MP-RULES' PR 51 (B-1310). Reruns: the four G2
smokes (complex_ledge_player, complex_ledge_simulant, villa_tabletop_player, villa_tabletop_simulant); Core Weapons'
held-weapon rows 6a to 8b of context/designs/modding/held-weapon-events.md and held_weapon_cloak_smoke; the six G1
local modes and coop_ai_buddies_offline_smoke if PR 51's bot.c change reaches them; character_tool_play_smoke
once Authored Mods updates its fixture.
