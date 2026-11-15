# Week PD multiplayer plan (Nov 15 - 21, 2026)

Owner: MULTIPLAYER week session (hub id `claude-week-pd-multiplayer`).
Branch `week-pd/multiplayer`, worktree `D:\PD2-wt\multiplayer`, base dev `a8ab67f3` plus the shared
records commit `8b6c0bda`.
Live status table: `context/week-pd-coordination.md` in the canonical checkout (one shared copy).

State on 2026-11-14: the base a8ab67f3 builds and passes its unit tests on hosted CI. The local build and
the boot smoke are CORE's to run first. Mike's decision for this session (2026-11-14): start source work
now, checked by hosted CI on the pushed branch, and limited to what CI can check; nothing is called
working in-game until the local boot smoke passes. Nothing has been run in a game by this session.

Progress log:
- Unit A (2026-11-14): scenario selection rules have one owner (`matchConfigApplyScenarioRules`) and
  `mpStartMatch` initializes the chosen scenario on every start path. Addresses section 4 item 1 and the
  automation half of item 6. Known limit: Capture the Case still wraps teams 4-7 inside its own init on
  each peer after the network start is serialized; that normalization moves to the authority in the
  Capture the Case unit. Hosted CI run 43630446515 green (1858 cases).
- Unit B (2026-11-14, `71d37ef1`): `mpEndMatch` logs `MATCH.RESULT`, `MATCH.RESULT.CHR` and
  `MATCH.RESULT.TEAM` on every peer; score-limit endings log their reason; `--match-scorelimit` and
  `--match-teamscorelimit` seed real limits for a launched match. Addresses the witness half of
  section 4 item 6.
- Unit C (2026-11-14): the stage-end commit returns the match room to LOBBY, so a listen host can
  start a second match in the same room. Addresses section 4 item 4 at source level; the runtime
  confirmation is a two-match networked smoke once game runs are allowed.
- Unit D (2026-11-14, `c040cc83`): the listen host sends one reliable `SVC_PLAYER_SCORES` packet to the
  match roster right before the terminal packet, so every end screen ranks the same Combat result.
  Existing message, no wire change, protocol stays v61. Addresses the score half of section 4 item 5;
  the end reason on the wire waits for the v62 bundle.
- All four units: local build on D: green and the full pd-tests suite green (1863 cases after unit D).
  Hosted CI green for units A and B; one run covers C and D. None has been run in a game.

- Unit E (2026-11-14, `cef2f744`): `port/src/net/net_scenario_state_wire.c` and its header. A pure codec
  (no game globals, linked into pd-tests) for one envelope (scenario id, revision) plus the King of the
  Hill body: live hill index, hill room and position as the host resolved them, occupying team, hold
  time, the pending-move flag and the hill time setting. Hill count and pads are stage data every peer
  loads itself, so they are not on the wire. Six Catch2 cases. Nothing sends or applies it yet.
- Hosted CI note: run 43632391920 stopped at the native write routes guard for `net.c`. The guard hashes
  a window that is not at the reviewed call (it counts lines in comment-blanked text), so comment-only
  edits above the call move the pin. Re-pinned that one entry in `88ede450` after checking the call's
  real surroundings are unchanged; the mapping fix is reported to the guard owner. Before every push,
  run `python -B tools/native_write_policy_guard.py --compile-commands .claude/session-builds/mp/compile_commands.json`.
- Smoke fixtures written, not yet run (game runs wait for CORE): `mp_hold_the_briefcase_local_smoke`,
  `mp_hacker_central_local_smoke`, `mp_pop_a_cap_local_smoke`, `mp_king_of_the_hill_local_smoke`,
  `mp_capture_the_case_local_smoke`. Each starts the scenario offline on Complex with three simulants
  and a 40 second limit and asserts the rules line, the scenario number at start, the time-limit end and
  the result witness. Their timings assume a fast install; expect to retune them for the D: hard disk.

- First game runs (2026-11-14, CORE's runner with `-Install D:\PD2-wt\shared-base`):
  - Baseline client (a8ab67f3), unchanged `listen_host_match_smoke`: FAILED 45/58. The client joined
    and authenticated; the host autostart hook never fired (`results-20261114T201305Z.json`).
  - Diagnostic `c18ae6ce` made the hook say why: `remote_in_lobby=0` for 100 s after the join.
  - Root cause B-1120 / SP-75: `main.c` and `smoke_harness.c` see project `bool` as a 1-byte `_Bool`
    (through `native_write_policy.h`) and laid out `struct netclient` differently from every other file.
    Fix `0cc573d4`: the shared net.h structs spell their flags `s32`; guard test added.
  - With the fix: `listen_host_match_smoke` PASSED 58/58 (`results-20261114T205302Z.json`). The same run
    is the first runtime proof of units B, C and D: identical `MATCH.RESULT` lines on host and client,
    `MATCH: final scores published before stage end`, and the room moving `Loading -> Lobby` after the
    match. Still unproven: a second match in the same room, any kill or score, bots over the network.

- SP-75 root fix (2026-11-14, Mike: "You may fix it, just be thorough / comprehensive"; B-1121,
  B-1123): no project header or C file includes `<stdbool.h>`. Headers shared by C and C++ spell
  boolean results `s32`; C files built without the precompiled header include the new
  `port/include/pd_bool.h`; the five fast3d `gfx_*.h` keep a scoped one-byte `bool` (push_macro,
  `_Bool` in C, keyword in C++, pop_macro); `modmgr.c` normalizes two compared arrays; three C++ menu
  files spell their own declarations of C symbols `s32`. `tools/bool_layout_audit.py` now follows
  push/pop scopes, separates contained ENet flips, reports leaking scopes and compares every named
  `bool` symbol across files; CI runs it on `pd` and `pd-tests`. Audit before the C++ declaration fix:
  `pd` 0 mixed header declarations (was 291), 0 flips into project code (was 28), 4 mixed symbols
  (the B-1123 declarations, fixed in the same commit). Build, unit tests, audit rerun and smokes on the
  merged tree: next entry.
- Verification of 8a4f32aa (2026-11-14): client and tests build with no new warnings; full pd-tests 1873
  cases / 337851 assertions green. Merged CORE's shared commits `week-pd/guard-fix` (1cc34bbb; took the
  shared guard file, guard tests 9/9 and the full guard pass on my compile database), `week-pd/runner`
  (6567be8d) and `week-pd/extract-1` (77049d63, B-1111; pd-tests list conflict, both sides kept). On
  77049d63: build green, pd-tests 1876 cases / 337881 assertions green, final audit PASS (`pd` 799 files:
  0 mixed header declarations, 0 mixed symbols over 1,579 named, 0 flips into project code, 6 contained
  ENet flips, 0 leaking scopes; `pd-tests` 349 files PASS). Hosted CI run 43646262037 on 6567be8d.
  Test-run gotcha: pd-tests output must go through a pipe; redirected to a file, 14 native write policy
  cases fail because the policy refuses inherited disk outputs (not a regression).
  Smokes from here on: my own runner copy, `-Install D:\PD2-wt\shared-base-2`, my build.
- Game runs on base 2 (2026-11-14, build 77049d63, receipts under
  `D:\PD2-wt\multiplayer\.claude\smoke-verify-runs\`): `boot_smoke` PASS 14/14
  (`results-20261114T221824Z.json`, 92.5 s, no re-extraction); `listen_host_match_smoke` PASS 58/58
  (`results-20261114T222549Z.json`), so SP-75 holds on the two-process path.
- `mp_combat_net_bots_smoke` first run 58/65 (`results-20261114T224518Z.json`): roster reached the client
  (`PARTICIPANTS: 5 active (2 players, 3 bots)` on both sides) but the 200 s scripted exit landed 2.5 s into
  the match, because on the D: disk the client needed about 130 s to boot. It also exposed B-1124: seven
  two-process fixtures that share one install ran against the real user profile (`%APPDATA%/perfectdark`).
  Fix 4b7b2742: `--portable` on both processes of the seven, a CI fixture-isolation test, and a 330 s budget
  for this fixture. Rerun PASS 65/65 (`results-20261114T230205Z.json`): first networked Combat match with
  simulants. Bots scored 3, 3 and 2, scores replicated through the match, time-limit end, and host and client
  logged identical `MATCH.RESULT.CHR` lines; both processes saved inside the install. Not yet looked at: how
  the client renders bot movement and weapons.
- `network_reconnect_smoke` (2026-11-14, `results-20261114T232009Z.json`): 18/71, blocked by the runner, not
  the product. With `-Install` only the shared install is seeded from the template; the two per-process
  installs of a `separate_process_installs` fixture start clean, so the host re-extracts everything from the
  ROM and misses its 120 s wait for `NET: created server` on the D: disk. Asked CORE (runner owner) to seed
  per-process installs from the template. The single-install runs show my binary's cache stamps match base 2
  (`pdmesh skipped=733 (fast-cache)`), so SP-75 changed no cache identity.
- Merged `week-pd/runner` (CORE: B-1124 refusal 71b9de82, per-process seeding 34848d15) and
  `week-pd/extract-2` (c78c0510, base 3) as b82250d5: build green, pd-tests 1884 cases green. Smokes moved to
  `D:\PD2-wt\shared-base-3`.
- Offline objective scenarios, Complex, three simulants (2026-11-14/15): Hold the Briefcase PASS 24/24
  (base 2, `results-20261114T234832Z.json`), King of the Hill PASS 25/25 (base 3,
  `results-20261115T004916Z.json`), Hacker Central CRASH on both bases (B-1125, `htmInitProps` divide by zero).
  Root cause of the crash is an extraction-output gap reported to CORE: 16 of 18 multiplayer arenas have no
  setup objects in the public scenario source (empty `objects.json` and `setup.fields.json`; Complex loads
  with `numobjs=2`), so they have no weapon, ammo or shield pickups in any mode. Next on my side: a pickup
  assertion in the multiplayer fixtures, and the Hacker Central guard once Mike picks the behavior.
- Capture the Case 25/25 (`results-20261115T013308Z.json`) and Pop a Cap 24/24 (`results-20261115T013959Z.json`)
  offline on base 3. Reconnect rerun on CORE's seeded per-process installs: 113/116
  (`results-20261115T022051Z.json`); the reconnect itself passes end to end, and the three misses are one expected
  line, a stale lobby cutscene the client no longer has on reconnect (question to CORE before touching the
  pinned fixture).
- B-1125 fixed in 86de8e08 (Mike, 2026-11-15: "can we place some so there are enough?"): a Hacker Central bank
  with fewer distinct pads than the mode needs is filled to 8 with walkable pads (navigation waypoints, spaced
  widest first, pure picker `port/src/scenario_pad_fallback.c`, 12 Catch2 cases); the selection loop, a terminal
  without a pad, the uplink's pick and `scenarioCreateObj` are guarded. `mp_hacker_central_local_smoke` PASS 31/31
  (`results-20261115T023803Z.json`: 8 pads added from 144 waypoints, terminal on pad 104, uplink on pad 114).
  The propagation check corrected the root cause: besides the missing setup objects, the scenario source keeps
  only intro spawn rows, so no arena has case or hill pads. Capture the Case cases are created off-world, King of
  the Hill's hill never moves, Hold the Briefcase always uses pad 0 (B-1126, SP-80). Asked CORE to carry the intro
  case and hill commands; the same fallback for those three modes waits for Mike.
- Merged into dev on Mike's OK (2026-11-15, "Merge our work"): merge commit e7503299, tree identical to
  da7f042b; local dev only, not pushed. Work continues on this branch; later units go to dev by the same route.
- Canonical-checkout edits (2026-11-15, Mike: commit them): five verified commits fast-forwarded onto dev
  (55812690, local only): T-TOOLING-007 agent guidance with the SP-75 line, T-TOOLING-005 cleaner, T-TOOLING-006
  D-011 floor and the opt-in retained modes, T-ASSETS-052 glTF limits (plus the stale material-order pin 14->15).
  Verified on a temp branch first: build, pd-tests 1899/1899, the CI tool self-tests, 171 retained-wrapper checks,
  31 runner-supervisor tests. Held back, with the reason given to Mike: CORE's uncompiled T-MODDING-011
  GGV/bondgun lane (already saved on `week-pd/inflight-20261114`) and the live crystal pistol files. Merged dev
  into this branch (4a26fe59). Branch pushes are held while dev holds unpushed commits.
- B-1126 fill-in (2026-11-15, Mike: "get all the MP modes functional", the same fill-in as Hacker Central):
  dbc660f1. Capture the Case bases farthest apart plus up to six respawn pads per base, King of the Hill up to six
  hills one per room, Hold the Briefcase an eight-pad bank; three new pure pickers with 15 unit cases; pd-tests
  1914/1914. The three local smokes assert placement outcomes; their game runs are queued on the hub.
- Unit F written (2026-11-15, not yet built or run): `SVC_SCENARIO_STATE` 0x5a, wire v62. The King of the Hill body
  gains `contested`, `scoring_team` and `scores`; the host sends the state in the stage baseline
  (`NET_RESYNC_FLAG_SCENARIO`), on every change and every 30 ticks while only the hold timer moves, and in the
  reconnect snapshot. A networked client's `kohTick` only presents it: hill room lighting (room bounded by this
  stage's room count before it indexes `g_Rooms`), capture and point announcements (moved into shared helpers so
  host and client play the same), a locally run hold timer between snapshots, and the host's hill time for the
  HUD without touching the client's own setting. Fixture `mp_king_of_the_hill_net_smoke` (port 27241) asserts the
  baseline send and the client taking the host's hill; `koh_net_compare.py` (session scratchpad) checks that
  both end on the same hill room and identical score lines.
- Local smokes on dbc660f1 (2026-11-15, my receipts under `D:\PD2-wt\multiplayer\.claude\smoke-verify-runs\`):
  `mp_capture_the_case_local_smoke` PASS 31/31 (`results-20261115T063003Z.json`: bases on pads 172 and 122, rooms
  33 and 15, six respawn pads each), `mp_hold_the_briefcase_local_smoke` PASS 29/29 (`results-20261115T063743Z.json`:
  eight-pad bank, briefcase on pad 141), `mp_hacker_central_local_smoke` PASS 31/31 (`results-20261115T064512Z.json`,
  the refactored candidate collection unchanged: terminal pad 106, uplink pad 197). Each run takes about 7.5 to 9.5
  minutes, of which about 4.5 is `storage.py seed` making the private install from shared-base-3.
- Units G to J written and built (2026-11-15): wire bodies and client presentation for Pop a Cap, Hold the
  Briefcase, Hacker Central and Capture the Case (details in the commit and in B-1127), plus B-1127 itself, found
  on the way: a client numbers players its own way (itself 0, the host's player in the client's slot), and
  `SVC_PLAYER_SCORES` applied slots unchanged. Also B-1128 (a client's early `scenarioInitProps` at
  `SVC_STAGE_START` ran on the previous stage; removed) and the King of the Hill client log now prints on the first
  state, not only on a room change. Full pd-tests on the tree with step 7 below: 1,931 cases, 359,072 assertions,
  all passed. Networked smokes for all five modes are next.
- Step 7 table fix (ea187a63, 2026-11-15): `mpconfigs.c` now uses designated initializers for all 44 configs,
  slots 8..MAX_BOTS-1 are disabled simulants, and 5 to 8 players use the 4-player difficulty (Mike to confirm).
  `mpApplyConfig` fills `stage_id` from `stagenum` through the catalog and logs `MPCONFIG: applied ...`.
  `matchStartFromChallenge` (the ImGui challenge launch) now calls `challengePerformSanityChecks`, which only the
  legacy menu tick called, so challenges from the ImGui screen had no simulants; the check treats zero joined
  players as one. `tests/test_mpconfig_table.cpp` pins it (fails on the positional table). Still open: the Room
  has no entry for challenges or presets (Mike's call), and no game run of a challenge yet.
- Notes for the token scenarios (Hold the Briefcase, Hacker Central, Capture the Case), read 2026-11-15: a
  human's briefcase or uplink pickup is decided on the host (`propobj.c:19527-19549`, server only) and sent as
  `SVC_PROP_PICKUP`, which a client replays through `propPickupByPlayer` (`netmsg.c:6322`). Simulant pickups and
  drops send nothing (finding 3), so on a client a case or briefcase a simulant took stays on the ground and one
  it dropped never appears. `htbTick` and `htmTick` also create a new token on any peer that cannot find one.
  Their seam therefore needs, besides a state body (holder slot or ground position, per-player timers and
  points), host-only token creation and the simulant pickup and drop sent like a human's. Units G to J built the
  first two: a client takes the holder from the host for its HUD and radar and never makes a token of its own.
  Still to build (next unit after the networked smokes): the simulant pickup and drop messages; until then a
  briefcase, uplink or case a simulant carries still lies where it was in a client's world.
- Networked smokes on 4a467190 (2026-11-15, 2 humans + 3 bots on Complex, receipts under
  `D:\PD2-wt\multiplayer\.claude\smoke-verify-runs\`): King of the Hill 78/78 (`results-20261115T072451Z.json`),
  Pop a Cap 76/76 (`results-20261115T073804Z.json`), Hold the Briefcase 79/79 (`results-20261115T074919Z.json`),
  Hacker Central 80/80 (`results-20261115T080027Z.json`), Capture the Case 77/77 (`results-20261115T081148Z.json`),
  all one binary (sha256 4C45D9C6...). `net_compare.py` (session scratchpad) maps the client's slots 0 and 1
  and finds every final result line identical on both sides (B-1127 confirmed in play: Hacker Central's three
  deaths include the client's own player; a simulant's King of the Hill point agrees). The matches are 35 s with
  idle humans: no token pickup, pop, download or capture happened, so those paths are unproven over the network.
  The token re-runs after unit K get longer matches.
- Finding 9, part (2026-11-15): the Room now offers the original's Team Score limit (1 to 400 points or no
  limit, 0-based store with 400 = no limit like `menuhandlerMpTeamScoreLimitSlider`; shown with teams on),
  Display Team (with teams on) and Kills Score (Hold the Briefcase, Hacker Central, King of the Hill and Capture
  the Case, as in their original option lists; not Pop a Cap). All three already rode in the room settings and
  the match start, so no wire change. Not done: King of the Hill hill time (`g_Vars.mphilltime` is in neither
  the room settings nor the stage start; the host's value reaches clients only through the v62 hill body) and
  Smart slow motion (it decides per peer from `bgRoomIsOnPlayerScreen` for every player, which a client cannot
  answer for the host's screen, so host and client would run different time scales; needs a host-decided time
  scale first). On slow motion is the same on every peer and stays offered.
- Unit K written (2026-11-15, building once the networked smokes have their binary): B-1129, simulant pickups and
  drops. `SVC_BOT_PICKUP` 0x4D (v62, unreleased): the host announces each simulant pickup that took effect and a
  client replays `botPickupProp` on its copy of the simulant; `botinvDrop` makes drops on the host only and
  announces them with `SVC_PROP_SPAWN`/`SVC_PROP_MOVE`, so dropped weapons, briefcases, uplinks and cases carry the
  host's sync ids on every peer. B-1130 on the way: a human's Capture the Case capture now reaches the clients
  (`SVC_PROP_PICKUP` with `TICKOP_NONE`). B-1131 / SP-81 recorded, not fixed as a class: a client stops reading a
  packet at the first message it declines. Static pins in `tests/test_net_bot_pickup_static.cpp`. Witness: the
  token net fixtures gain `BOT.PICKUP`/`BOT.DROP` counts and forbid `BOT.PICKUP: client skipped`.
- Notes for step 7 (Challenges and presets), read 2026-11-15 before the fix above: besides the positional
  initializers landing in `stage_id[64]` and `head_id[64]` (finding 10), `struct mpconfig` now holds
  `simulants[MAX_BOTS]` = 32 while `mpconfigs.c` authors 8 per config, so slots 8..31 are zero: type 0 and every
  difficulty 0 = `BOTDIFF_MEAT`, which `mpApplyConfig` (`mplayer.c:4561`) copies for all 32, so a fixed table would
  still add 24 MeatSims to every challenge. `difficulties[MAX_PLAYERS]` is 8 wide; the table authors 4 (one per
  player count of the original). `stage_id` is empty, but `catalogStageIdByStagenum` can resolve it from
  `stagenum` in `mpApplyConfig`; head and body ids already fall back to the catalog by number. Planned fix:
  designated initializers (`.stagenum =`, `.mpheadnum =`, ...), slots past the authored 8 forced to
  `BOTDIFF_DISABLED`, and 5 to 8 players using the 4-player difficulty (to confirm with Mike: the original has no
  such counts). `mpconfig.jumpimpulse` has no reader.

Next units, in order:
1. Re-run the other two-process baselines against the fix (reconnect first: its harness events used
   the wrong client slot before the fix), then the five local per-scenario fixtures.
2. Unit F: King of the Hill on the seam. Split `kohTick` into a host-only part and a presentation part,
   capture and apply between `g_ScenarioData.koh` and the wire struct, add `SVC_SCENARIO_STATE`, send it
   in the stage baseline, on change and at a low rate while the hold timer runs, and bump the protocol
   to v62 with the test pin in the same commit. This is the first unit that needs a game run to prove.
3. Then Pop a Cap, Hold the Briefcase, Hacker Central and Capture the Case, each adding its body to the
   codec first and its game side second.

Held until game runs are allowed: the unchanged re-run of the existing multiplayer smokes (step 1 of
section 6), and every per-mode smoke. Smoke fixtures can be written before then; they cannot be run.

Evidence labels used below:
- VERIFIED: I read the cited source lines myself.
- REPORTED: found by a read-only code map on 2026-11-14, cited but not re-read by me.
- RECEIPT: a recorded smoke or test result, with its date and wire protocol.

Nothing in this document has been confirmed by running the game. Step 1 of the work order exists to
replace inference with receipts.

## 1. Scope and how I read the brief

Week goal (Mike): "getting the game in a properly playable state using our asset extraction, all game
behavior converted to our behavior graphs, single player functioning, multiplayer modes functioning,
and work on our custom mods."

My scope: every multiplayer mode working, local and networked, each with a test or scripted smoke run
covering match start, scoring, round end and rejoin.

Readings I am planning around (flagged to Mike, section 8):
- "Local" means one human plus simulants, offline. `context/constraints.md` has an active constraint
  "No local multiplayer (single local player only)". Split-screen is not revived this week.
- "Networked" means the in-client listen host plus clients. The dedicated server target is removed.
  All proof is on this one machine: two or three game processes over loopback.
- "Rejoin" means a dropped participant reconnecting to a running match. Joining a match as a new
  player mid-match is refused by design today (REPORTED `port/src/net/net.c:3143-3158`) and stays so.
- Build on the existing networking. Everything here is additive to protocol v61 and its lifecycle
  (stage transaction, reconnect snapshot, rooms). Nothing is replaced.

Not in this week's scope unless Mike says otherwise: host-side movement authority (audit finding F05),
NAT traversal and TURN, voice, host migration, Theater.

## 2. Mode inventory: original game and this port

| Mode family | Original game | This port today |
|---|---|---|
| Combat Simulator scenarios | Combat, Hold the Briefcase, Hacker Central, Pop a Cap, King of the Hill, Capture the Case | All six present as native C callbacks: `src/game/mplayer/scenarios.c:181-256` (VERIFIED). Catalog ids `base:combat`, `base:hold_the_briefcase`, `base:hacker_central`, `base:pop_a_cap`, `base:king_of_the_hill`, `base:capture_the_case` (REPORTED). King of the Hill and Capture the Case are team-only (`scenarios.c:258-266`, VERIFIED). |
| Simulants | Up to 8. Six difficulties (Meat, Easy, Normal, Hard, Perfect, Dark). Twelve special types (Peace, Shield, Rocket, Kaze, Fist, Prey, Coward, Judge, Feud, Speed, Turtle, Venge). Team orders. | Up to 31 bots with one human, 32 participants total (`MAX_BOTS` 32, `MAX_MPCHRS` 40). All AI is native C plus AI lists; none of it is on behavior graphs (REPORTED). Bot profiles are catalog assets (`.pdbotprofile`). |
| Settings | Options (one-hit kills, slow motion off/on/smart, fast movement, display team, no radar, no auto-aim, no player highlight, no pickup highlight, kills score, per-scenario highlight and radar options, mobile hill, hill time), limits (time, score, team score), teams, weapon sets, handicaps, music. | Same `MPOPTION_*` flags (`src/include/constants.h:2964-2984`, VERIFIED) plus port additions: spawn with weapon, no drug blur, auto random weapon, friendly fire, no player on radar, no doors, bot jump (`constants.h:2985-2999`). Setup is the ImGui Room (`port/fast3d/pdgui_menu_room.cpp`), which exposes only part of this (section 4, item 9). |
| Arenas | 16 arenas plus Random. | 47 authored rows in `port/src/arenadata_authored.c` (REPORTED), mirrored by `tools/base-content-coverage/matrix-v1.json`: 18 standard (13 Dark; 5 Classic: Temple, Complex, Felicity and two test grids), 14 solo-mission maps listed as arenas (whether they are playable as arenas is an open roadmap decision), 13 bonus rows left over from the merged mod pack, 2 random rows. All are always unlocked. Mod arenas come through the catalog. |
| Challenges | 30 challenges, 1 to 4 players. | Logic present, but three problems: the Challenges screen cannot be reached from the Room; the launch path probably never adds the challenge's bots; and the config table is misaligned with the current structs (section 4, item 10). |
| Presets and Quick Start | 14 preset games, Quick Start, Advanced Setup. | Dialogs and ImGui renderers exist but have no entry point from the Room (REPORTED). Presets share the misaligned config table. The Room has its own save/load of match setups. |
| Co-Operative | 2 humans, or 1 human with AI buddies. Options: radar, friendly fire. | Network only, exactly 2 humans, no AI buddies over the network. Offline entry points were removed; the engine path for AI buddies still exists (REPORTED). |
| Counter-Operative | 2 humans, one plays the opposition. | Network only, exactly 2 humans, host is always Joanna (REPORTED). |

The project's own coverage contract (T-TOOLING-003) is 108 Combat Simulator cells (18 standard arenas
x 6 scenarios), 120 challenge cells and 126 co-op and counter-op cells. None of those cells has a
runnable fixture today. This plan produces the first per-mode fixtures; it does not try to run all cells.

## 3. What has actually been proven

Every multiplayer receipt predates the current smoke storage layout (changed 2026-10-03) and all
networked ones were taken at protocol v57 or v58. The wire is now v61 (`port/include/net/net.h:13`).

| Area | Latest receipt | Covers | Does not cover |
|---|---|---|---|
| Combat, local | `combat_sim_release_cycle` 56/56, 2026-08-26 | 1 human + 1 bot on Chicago: fire and hit, injected kill matrix and awards, time-limit end, Play Again twice | real bot kills, score-limit end |
| Combat, local, many bots | `combat_sim_chicago_match_start` 20/20, 2026-08-13 | start with 12 bots | anything after start |
| Bot cohort (T-ENGINE-005) | `combat_sim_defect_cohort` never run | nothing yet | spawn, attribution, rig defects B-249, B-242, B-174, B-183 |
| Combat, networked | `listen_host_match_smoke` 58/58, 2026-08-13, v58 | 2 humans, 0 bots, Felicity: start, both spawn, time-limit end, end screen | any kill or score, bots, return to room, second match |
| Rejoin | `network_reconnect_smoke` 116/116, 2026-08-26, v58 | timeout, reconnect, exact world and inventory restore, fire | bots, any objective scenario |
| Co-op, networked | `network_coop_stage_transaction_smoke` 96/96 on 2026-08-26 (v58); 90/96 on 2026-09-27 with stale assertions | start and stable gameplay on Defection | objectives, mission end, rejoin |
| Counter-op, networked | `network_counterop_stage_transaction_smoke` 98/98, 2026-08-26, v58 | start and gameplay | everything after |
| Five objective scenarios | none | nothing, local or networked | everything |
| Movement ordering (Oct 11) | pd-tests `[net][move]` green in CI on `ac43b227` | CLC_MOVE / SVC_PLAYER_MOVE admission and ACK ordering | ordinary-client runtime proof |

## 4. Findings that drive the plan

Ranked by how much they block "every mode working".

1. **Objective scenarios are never initialized when started from the Room, offline or online.**
   VERIFIED: `scenarioInit()` is called only from `mplayer.c:4501` (preset apply), `mplayer.c:4647`
   (setup-file load) and `scenarios.c:515` (the legacy picker). The Room start path copies the plan
   straight into `g_MpSetup` (`port/src/net/matchsetup.c:1417-1426`) and never calls it. So King of
   the Hill and Capture the Case do not get Teams forced on (`kingofthehill.inc:163-167`,
   `capturethecase.inc:123-126`), and per-scenario state in the shared `g_ScenarioData` union is not
   reset between modes. This is the likely reason no objective scenario has a receipt even locally.
2. **The five objective scenarios replicate no scenario state.** VERIFIED: `g_ScenarioData` is
   referenced only in `src/game/mplayer/` and `src/game/bot.c`; the scenario files contain no net
   calls; every peer runs `scenarioTick()` locally (`src/game/lv.c:3072`). Briefcase holder, hill
   position and owner, Pop a Cap victim, hacker terminal and download progress, case and base state
   are each derived independently per peer. Several depend on local random choices (REPORTED:
   `kingofthehill.inc:193,283`, `popacap.inc:139-154`, `capturethecase.inc:226`). Hacker Central
   and Pop a Cap keep their scores in `g_ScenarioData`, which `SVC_PLAYER_SCORES` does not carry
   (VERIFIED `netmsg.c:5876-5896` writes team, deaths, points and kill counts only). Combat needs
   none of this, which is why it is the only scenario with networked receipts.
3. **Bots in networked matches are unproven and probably broken for clients.** Every networked
   fixture runs 0 bots (REPORTED). Non-authority clients return from `botTick` before `chrTick`
   (VERIFIED `src/game/bot.c:1644-1646`), and the same file records that skipping `chrTick` once made
   bots invisible (`bot.c:1747-1754`). Bot pickups, drops, objective flags and thrown projectiles send
   nothing (REPORTED `bot.c:1118-1121`, `botinv.c:1049-1057`, `botact.c:357,523`). Team orders from a
   remote human never reach the host AI (REPORTED `botcmd.c:202-236`).
4. **A second match in the same room is refused on a listen host.** VERIFIED: the room moves to
   LOADING at countdown (`netmsg.c:10203`); the only code that moves it on to MATCH, POSTGAME and
   back to LOBBY is `hubTick()` (`port/src/hub.c:96-117`), called only from `server_main.c:469`,
   which the client build excludes (`CMakeLists.txt:571`); starting requires LOBBY (`netmsg.c:10514`).
   Needs a runtime confirmation, then a fix at the stage-end commit.
5. **Match end carries no result.** VERIFIED: `SVC_STAGE_END` is one mode byte (`netmsg.c:3826-3833`).
   Clients rank the end screen from the last periodic `SVC_PLAYER_SCORES`, sent every 300 ticks
   (REPORTED `net.c:3849`), and force a single end reason (REPORTED `netmsg.c:3927`). A client
   pressing End Game in the ImGui pause menu may end the match only for itself (REPORTED
   `pdgui_menu_pausemenu.cpp:189-195`).
6. **Automation cannot start a non-Combat networked match, and no log line witnesses scoring.**
   VERIFIED: `--launch-mp-room` copies only the scenario id string (`port/src/main.c:1195-1208`);
   `--host-autostart` sends the integer `g_MatchConfig.scenario` (`main.c:3839-3851`); the start
   writer rejects a string and integer that disagree (`netmsg.c:9674-9682`). REPORTED: no kill or
   objective witness exists; score-limit endings log nothing; there is no score-limit flag.
7. **Rejoin restores no scenario state**, and fails outright if an unsupported dynamic prop type
   exists (REPORTED `netmsg.c:4856-4861`, `1505-1511`). Bot inventories and objective flags are not
   in the snapshot.
8. **Team play over the network is thin.** Each human's team comes from that client's own config
   (VERIFIED `net.c:576`, `netmsg.c:10708-10769`). Whether a player can pick a team in a network room
   before the match is unverified. Capture the Case match-start spawns ignore team bases (REPORTED:
   the spawn orchestrator has no scenario branch), and the in-match team switch offers 8 teams while
   Capture the Case indexes a 4-entry array by team (REPORTED `ingame.c:278-279`,
   `capturethecase.inc:468-473`).
9. **The Room exposes only part of the settings.** REPORTED: no Kills Score, Display Team, Smart
   slow motion, team score limit (fixed at 400) or King of the Hill hill time; hill time is not in
   the stage-start message either. The handicap slider treats a nonlinear engine value as a linear
   percentage (`pdgui_menu_room.cpp:3122-3131` against `mplayer.c:205-224`). `MPOPTION_NODOORS` has
   a toggle but no gameplay consumer. Handicap is forced to 1.0 online (`mplayer.c:209-211`).
10. **Challenges and presets are broken at the data level, not only unreachable.** VERIFIED: the
    compile-time table `g_MpConfigs[]` (`src/game/mpconfigs.c`) is the only source
    (`src/game/challenge.c:477-478`) and uses positional initializers written for the N64 structs,
    but `struct mpsetup` now has `char stage_id[64]` between `scenario` and `stagenum`
    (`types.h:4175-4197`) and `struct mpconfigsim` has `head_id[64]` and `body_id[64]` after `type`
    (`types.h:5097-5106`). By C initializer rules the stage, limits and weapons land inside
    `stage_id`, and each simulant's head, body, team and difficulties land inside `head_id`, leaving
    difficulty 0 (MeatSim, enabled) everywhere. Confirmed by the compiler: the green hosted CI build
    of a8ab67f3 (run 39809150838, 2026-10-22) emits 1,685 warnings for `mpconfigs.c` (1,276 "excess
    elements in scalar initializer", 396 "braces around scalar initializer", 13 int-to-char
    overflows), with notes naming `g_MpConfigs[0].setup.stage_id[4]` and
    `g_MpConfigs[0].simulants[0].head_id[3]`. REPORTED on top of that: the ImGui challenge launch
    starts the match without adding the challenge's bots (`matchsetup.c:1556-1601`), and the screens
    have no entry.
11. **Co-op and Counter-op menus and flow.** Offline co-op entry was removed. The Room's co-op
    friendly-fire toggle never reaches the gameplay flag, there is no radar toggle, and mission,
    difficulty and counter-op player choice are not broadcast. Mission end runs Next or Retry
    locally with no host coordination (all REPORTED, `pdgui_menu_room.cpp`,
    `pdgui_menu_endscreen.cpp:750-796`).
12. **The bot defect cohort (T-ENGINE-005) is committed but has never run in a game.** Its smoke
    fixture exists and is unrun. Original-game quirks in simulant types are listed in section 8 as
    a question, not as planned fixes.
13. **Graph weapons refuse network and bot use** (REPORTED `port/src/weapon_graph_v2_catalog.cpp:76-78`).
    That is CORE's runtime, but it decides whether MODS' custom weapons can appear in my matches.
14. **Movement is still client-reported** (September audit, finding F05). Not addressed this week.

## 5. Design: one host-authoritative scenario state seam

Principle: the host runs the rules, every client presents the host's state. I replicate scenario
*state*, not scenario *logic*, so the seam still holds if CORE moves the rules onto behavior graphs.
This follows patterns the net layer already uses for NPCs, props and reconnect (one reliable typed
snapshot, applied idempotently), rather than adding one event message per scenario action.

- **Match-start initialization.** One function owns scenario initialization at match start for
  every path (Room, challenge, preset, listen host, receiving client): it runs the scenario's init,
  applies what the scenario requires (teams for King of the Hill and Capture the Case) and clears
  the shared state. The Room and the lobby validate the same requirement before start.
- **Authority split.** Each scenario's tick is divided into a host-only part (anything that changes
  `g_ScenarioData` or awards points) and a presentation part (HUD, radar, highlights, sounds) that
  runs on every peer. Offline play is the host path with no peers.
- **One typed message.** A new reliable `SVC_SCENARIO_STATE` carries the active scenario's state in
  wire form. Props and holders are addressed by prop sync id or participant slot; pads and rooms by
  index (same stage data on all peers, same extracted assets). No raw pointers, no asset numbers.
- **When it is sent.** In the stage baseline, on every authoritative change, at a low rate while a
  timer is running, inside the terminal match-end packet, and inside the reconnect snapshot.
- **Match result.** The terminal packet also carries final scores and the end reason (time limit,
  score limit, team score limit, host ended), so every end screen ranks the same result.
  `SVC_PLAYER_SCORES` is additionally sent when a score changes instead of only every 300 ticks.
- **Wire version.** One bump, v61 to v62, with `g_TestExpectedNetProtocolVer` updated in the same
  commit (`tests/test_versions.cpp:46`).
- **Validation.** Decode fully before applying, bound every index and count, reject messages from
  a non-server sender, and reject a scenario id that does not match the running match.

State carried per scenario (from `src/include/types.h:4263-4342`, VERIFIED):

| Scenario | Authoritative state |
|---|---|
| Hold the Briefcase | token location (pad, dropped prop, or holder slot); per-player held time feeding points |
| Hacker Central | uplink location or holder; terminal pad and team; downloading player, terminal and progress; per-player hack points |
| Pop a Cap | victim order and current victim; victim age timer; per-player kill and survival counts |
| King of the Hill | hill index, rooms and position; occupying team; elapsed hold time; pending hill move; hill time setting |
| Capture the Case | per-team case location or holder; base rooms and team mapping; team spawn pads; captures |
| Combat | none (kills and scores already replicate) |

## 6. Work order

Each step ends with its tests green and a row update in the coordination note. Commits are small and
stay on `week-pd/multiplayer`. Steps 1 to 4 are the core of the week; 5 to 8 follow as time allows and
stay visible in the table either way.

1. **Baseline at v61 (first hours after GO).** Build once. Run pd-tests `[net],[networking]`,
   `[reconnect]`, `[combat-sim],[player-init]`.
   Re-run the existing multiplayer smokes unchanged: `combat_sim_chicago_match_start`,
   `combat_sim_release_cycle`, `combat_sim_defect_cohort` (first ever run), `listen_host_match_smoke`,
   `network_reconnect_smoke`, co-op and counter-op stage smokes. Record what is red before I change
   anything.
2. **Test plumbing.** Seed the runtime scenario from the catalog id in `--launch-mp-room`. Add
   automation flags for score and team-score limits. Add witness log lines for score changes,
   objective events and the end reason, on host and client. Add smoke script actions that drive an
   objective deterministically where bots cannot be relied on.
3. **Combat, complete.** Networked with bots: bots visible and animating on clients, bot kills and
   deaths scored identically on both sides, bot projectiles. Score-limit end. Final result in the
   terminal packet. Return to room and a second match (room state fix). Rejoin with bots present.
4. **Objective scenarios.** First the shared match-start initialization, proven by one local smoke
   per scenario (start, a scoring event, end). Then the network seam, one scenario at a time,
   simplest state first: King of the Hill, Pop a Cap, Hold the Briefcase, Hacker Central, Capture
   the Case. Each gets the authority split, its snapshot fields, client apply, bot objective state,
   reconnect restore, and a networked and a rejoin smoke. Human team choice in network rooms is
   confirmed or fixed before King of the Hill.
5. **Simulants.** Run and close the T-ENGINE-005 cohort smoke (Car Park, 12 bots), a 31-bot stress
   run, and a spawn and behavior sanity pass across the six difficulties and twelve types. Route team
   orders from remote humans to the host.
6. **Settings and arenas sweep.** The options the Room lacks, limits, weapon sets, teams and
   handicaps, each asserted through a witness line. Local start with bots on all 18 standard arenas;
   a networked sample; a sample of solo-mission and mod arenas.
7. **Challenges, presets, Quick Start.** Repair the config table (designated initializers plus
   catalog ids), add the challenge's bots on launch, restore an entry point. The entry point needs
   Mike's call on where it lives.
8. **Co-op and Counter-op.** Re-prove start at the new protocol and repair the stale co-op
   assertions. Make friendly fire, radar and difficulty take effect and sync. Host-coordinated
   mission end with a return to the room. Rejoin. Depends on CORE's campaign missions being stable.

## 7. Tests and smoke runs

- **Unit tests (pd-tests).** Round-trip and rejection cases for every scenario's snapshot codec,
  using real storage the way `tests/net_move_fixture.c` does for movement. A table test that every
  challenge and preset config resolves to a real stage, weapons and bot set. Static pins for the new
  fixtures, following `tests/test_net_lifecycle_static.cpp`.
- **Smoke naming.** `mp_<mode>_local_smoke`, `mp_<mode>_net_smoke`, `mp_<mode>_rejoin_smoke` under
  `tools/smoke-verify/tests/`, per-process scripts under `tools/smoke-verify/fixtures/`.
- **What each asserts.** Start: the scenario witness and both players spawned. Scoring: the same
  score and objective witnesses, with the same values, in the host and client logs. End: end reason,
  final result identical on both sides, end screen, return to room. Rejoin: scenario state after
  reconnect equals the host's, then play continues.
- **Running.** Always pass `-SourceBinary` and the shared ROM. Ports already used by fixtures:
  27200-27202, 27204, 27210-27217, 27230; mine start at 27240. Every build, test run and game run
  goes through the hub FIFO first.

## 8. Decisions and asks

For Mike:
1. Disk: builds are refused on C: by the storage floor. My preference matches CORE and MODS: week
   worktrees and builds on D:. Then I recreate this worktree there.
2. Confirm "local" means one human plus simulants (no split-screen).
3. Wire bump to v62 for scenario state and match result. Informational unless you object.
4. Original-game simulant quirks found in the decompiled code: JudgeSim's loop has no `break`, so it
   ends up targeting the lowest-ranked enemy (`src/game/bot.c:4424-4445`, VERIFIED); DarkSim's
   unlimited-ammo flag is cleared rather than set (`bot.c:443-449`, VERIFIED, marked `@bug` in the
   decomp); CowardSim's run-away action is never assigned (VERIFIED by grep). Keep them exactly as
   shipped in 2000, or fix them? My default this week is to keep them and log them.
5. Where Challenges, presets and Quick Start should live in the new Room.
6. Co-op and Counter-op priority, and whether offline co-op with AI buddies gets its entry back.
7. Solo-mission maps as arenas: in the sweep or not (open roadmap decision).

For CORE (posted in the coordination note):
1. Are Combat Simulator scenario rules, bot AI or match flow part of this week's graph conversion?
2. Who lifts the network and bot refusal for graph weapons?
3. The shared base install, and how smoke storage should use it from a worktree.
4. One agreed FIFO resource name for game runs.

## 9. Risks

- The disk decision and CORE's Step 0 gate both sit in front of any build. Until then this plan is
  untested reading.
- Most "REPORTED" items have not been run. The order after step 1 may change with what it shows.
- Five scenarios plus bots plus co-op is more than a week if each needs deep repair. The order is
  chosen so the most-played paths land first and the table always shows the true state.
- If CORE converts scenario rules mid-week, the authority split in the `.inc` files will conflict.
  Mitigation: ask first (section 8), keep my edits to the state seam and thin call-site gates.
- The smoke harness keeps its storage per checkout (`tools/smoke-verify/storage.py:197-207`).
  Running it from a worktree would create a second extraction unless the shared base install works
  with it. To be settled with CORE before the first run.

## 10. Workspace and hygiene

- One worktree, one build session id (`mp`), at most one reference build kept.
- No ROM copy and no second asset extraction in the worktree.
- Scratch files stay in the session scratchpad and are removed at the end of each step.
- No merge to dev or main without Mike's OK. Rebase on dev when CORE lands the week base.
- End of week: `git worktree remove`, `git worktree prune`, delete build and cache folders, report
  the space freed.

## Where to look

- Live table and cross-session asks: `context/week-pd-coordination.md` (canonical checkout).
- Net layer: `port/src/net/net.c`, `port/src/net/netmsg.c`, `port/include/net/netmsg.h`.
- Scenario rules: `src/game/mplayer/scenarios.c`, `src/game/mplayer/scenarios/*.inc`.
- Match start: `port/src/net/matchsetup.c`, `src/game/mplayer/mplayer.c`.
- Bots: `src/game/bot*.c`, `src/game/mplayer/mpspawn_orchestrate.c`.
- Challenges and presets: `src/game/mpconfigs.c`, `src/game/challenge.c`.
- Harness: `tools/smoke-verify/README.md`, `tools/smoke-verify/run.ps1`.
- Coverage contract: `tools/base-content-coverage/README.md`.
