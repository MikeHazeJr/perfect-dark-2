# Bug Tracker

## 2026-11-21 B-1283 Bare `cmd` in PowerShell opened a 3D-printing slicer about 260 times: it resolves to an extensionless msys script, which Windows opens by file type (T-TOOLING-017, fixed and guarded)

Severity high: it crashed Claude on Shadowbane on 2026-11-21. Found by claude-pd-integration-20261121.

The cause has two parts:
- PowerShell started from the Bash tool resolves the bare name `cmd` to the extensionless bash script
  `C:\devkitPro\msys2\usr\bin\cmd`, ahead of `C:\Windows\System32\cmd.exe`. Every script that loads
  `devtools/_build-env-prelude.ps1` resolves it to `C:\msys64\usr\bin\cmd`, because the prelude puts msys
  `usr\bin` ahead of System32.
- PowerShell cannot execute such a file. A statement, or the only command of an assignment, hands the file to
  the Windows shell, which opens it by file type. Only a call in the middle of a pipeline is refused, with
  "Cannot run a document in the middle of a pipeline". On Shadowbane, extensionless files (`HKCR\.` =
  `PhotonWorkShop`) open with `C:\Program Files\AnycubicPhotonWorkshop\AnycubicPhotonWorkshop.exe`, which is
  built on Chitubox.

What happened: Session A's scratch deleter ran on 2026-11-21 from 05:12 to 05:15 UTC. It ran
`$links = cmd /c "dir /s /b /al ..."` once per folder: 130 folders in its dry run and 130 in the real run, so the
slicer was started about 260 times. Each open returned nothing, which is why the dry run "passed". Its
`cmd /c "rmdir ..." | Out-Null` was refused, so no folder was deleted, only 38 loose files. Session A stopped
answering at 05:17 UTC.

Fix:
- `devtools/_build-env-prelude.ps1` aliases `cmd` to `$env:SystemRoot\System32\cmd.exe`. Aliases win over PATH,
  so every prelude script runs the real one.
- `tools/tests/test_powershell_open_by_type.py`, which runs in CI, refuses bare `cmd`, `Invoke-Item` and `ii` in
  tracked PowerShell.
- The deletion was finished by a Python deleter that starts no shell.

Mike's standing rule (2026-11-21): launch programs only by their full `.exe` path, and never open a file or
folder by its type.

A machine setting for Mike, not changed by any session: `HKCR\.` = `PhotonWorkShop` makes every extensionless
file open in the slicer. Removing that default makes Windows ask instead.

## 2026-11-21 B-1295 The Room's preset names carry the language file's line feed into its logs and the preset's setup name (T-MENUS-009, fixed on week-pd/mp-rules, found in review, not seen in a game)

Found while writing the finalization smoke for the Room's Load Preset (claude-pd-mp-rules-20261121).
`mpGetPresetNameBySlot` returns the language file's text, and the language file ends each of these menu
items with a line feed (`"No Shield\n"`, `src/assets/*/lang/mpweapons.json`), which the original menus read as
the end of the item. PR 27 (09052ace) used it as it came: the `ROOM: preset loaded` and
`SCENARIO.PRESET committed` lines broke across two log lines, so no smoke could match them, and the preset's Room
document took the name with the line feed. The list itself looked right: ImGui measures and draws a trailing line
feed as nothing. Fix: `mpCopyPresetNameBySlot` (`src/game/mplayer/mplayer.c`) copies the name up to the line
feed; the Room's list, its label, its logs and `scenarioLoadPreset` use it, and the Room no longer calls
`mpGetPresetNameBySlot`. Propagation: the arena names (`mpmenu.json`) and simulant names (`misc.json`) the Room
lists carry no line feed, and the end screen already cuts names at one (`pdgui_menu_endscreen.cpp`). Tests:
`[preset]` in `tests/test_scenario_save_transaction_static.cpp`. In the game: `room_load_preset_smoke` (finalization
batch) requires both lines whole.

Severity low.

## 2026-11-21 B-1292 A refused mod game mode stays in the legacy mode picker, whose ASSET.CHAIN check would stop the game (T-MODDING-026, open, found in review, not seen in a game)

Found while writing gamemode-graphs slice 2 (claude-pd-mp-rules-20261121). When a `.pdgamemode` does not
hydrate (its `rules.json` is refused, or since slice 2 its `mode.graph.json` is missing or refused),
`assetRuntimeHydrateCatalogEntry` turns its binding off and the catalog does not activate it, but the entry stays
`enabled`, and `assetCatalogIterateByType` filters on `enabled` alone. The legacy menu's mode picker
(`scenarioScenarioMenuHandler` through `assetCatalogIterateUnlockedByType`, `scenarioCtxAccepts`,
`scenarioBindingForEntry`) would then meet a mode with no hydrated binding and call `sysFatalError` ("ASSET.CHAIN:
gamemode ... has no active public-source rules binding"). Base modes are not affected: the extractor writes their
programs and conformance holds them to the original table. The Room lists the six base modes by index
(`catalogGameModeIdByScenarioIndex`), and no mod mode can be chosen anywhere yet, so only a broken mod mode met by
the legacy picker reaches it.

Severity low. Fix to make with slice 6 (a mod's mode, the picker): selectors skip a mode whose activation was
refused and the mod manager shows why; a base mode without a binding stays fatal, as the ASSET.CHAIN policy wants.

## 2026-11-20 B-1282 The Crystal Pistol ammo smoke fails when one frame stalls during its reload steps (T-MODDING-008, open, test only)

Severity low (test only). `crystal_pistol_ammo_smoke` failed 29/31 on dev plus PR 13 (d1932f55, shared base 14,
`results-20261120T182245Z.json`) and passed 31/31 on a rerun of the same build ten minutes later
(`results-20261120T184249Z.json`). In the failing run the game stalled about 1.2 s at 13.2 s into the script
(log 01:32.29 to 01:33.52, an install on the `D:` hard disk). The harness then delivered the 13.7 s fire tap, the
14.2 s reload tap and the 14.5 s fire press within a few frames, so a third shot landed before the reload began:
regrowth started at 3/6 instead of 4/6, and the "interrupted by trigger" step never happened. Firing, the empty
clip's regrowth and the full clip all worked. The fixture already allows for a first-explosion stall at 6.6 to
12.8 s but not for one at 13.2 s.

Fix to make: wait on game state between the steps (for example on the regrow start line) instead of fixed times,
or give the reload sequence a margin.

## 2026-11-20 B-1281 Smokes cannot press menu buttons since 149820cb1: ImGui navigation is off while the game window lacks focus (T-MENUS-003, open, test harness)

Severity medium (test only for players, but it stalls every smoke that drives an ImGui menu with injected
actions in a background window, and that is every smoke the runner starts). Found 2026-11-20 in the cleanup:
`network_coop_mission_end_smoke` failed 126/132 on dev plus PR 13 (d1932f55, shared base 14,
`results-20261120T183429Z.json`). The co-op host reached its results screen, the smoke injected `ACTION_USE` at
4 s (`SMOKE.ACTION.INJECT: player=0 action=24 down=1 ...`) and nothing followed: no `GAMELOOP.COOP: host chose
next mission`, the next stage never went live, the wait timed out after 360 s. The same smoke passed 132/132 on
ddac61d6 (2026-11-19), where the same tap chose the next mission in the same frame; that host had also logged
`INPUTCTX: focus LOST`, as every runner-started game does (SP-51).

Cause: 149820cb1 ("Preserve front-end input and controller text entry", 2026-11-19, on origin/dev) made
`pdguiDriveImGuiNav` (`port/fast3d/pdgui_backend.cpp`) require `!inputCtxIsFocusLost()`. With focus lost, ImGui
gets no nav input at all, so the endscreen's default button (`pdgui_menu_endscreen.cpp`, B-1193) is never
activated. Neither the endscreen nor `pdguiNavActionAllowed` changed between the two runs. For a player with the
window focused nothing changes. About 38 smoke tests and fixtures inject menu actions (`ACTION_USE`,
`ACTION_MENU_*`); any that act on an ImGui menu after the focus loss stalls the same way.

Fix to make: let actions the smoke harness injects drive ImGui navigation while it owns them (the action map
already counts smoke-owned actions; see `SMOKE.ACTION.INJECT` in `port/src/actionmap.cpp`), or have the runner
give the game window focus. Then re-run
`network_coop_mission_end_smoke` and the other endscreen and Room smokes.

## 2026-11-20 B-1280 The build wrapper never uses its compiler cache: the ccache probe always reads as failed (T-TOOLING-006, fixed 2026-11-21 on dev in 7828e6d6 and efeb7007)

Fixed by claude-pd-integration-tooling-20261120. The probe now reads the process handle, so Windows
PowerShell 5.1 keeps the exit code. `CCACHE_COMPILERTYPE=gcc` stops every PCH user missing in a fresh build
folder. `CCACHE_NOHASHDIR=1` shares results across session folders, and `CCACHE_DEPEND=1` drops the second
preprocessor run. Measured on C:: a new session folder with a warm cache hits 1,465 of 1,467 units. The
original finding follows.


Severity medium (every AI build compiles cold; a clean client plus test binary took 3:01 on 2026-11-20).
`devtools/build-headless.ps1` (the probe at lines 328 to 347) starts `ccache.exe cc.exe --version` with
`Start-Process -PassThru` and redirected output, waits up to 5 s, and passes the launcher to CMake only when
`$probe.ExitCode -eq 0`. Under Windows PowerShell 5.1, which `powershell -File devtools/build-session.ps1` runs, a
process started that way reports an empty `ExitCode` unless its handle was read before it exited, so the test is
always false: the wrapper prints "ccache compiler probe failed or timed out; building without ccache launcher" and
configures without ccache. All seven session builds of the canonical checkout log that line. Reproduced
2026-11-20: the same `Start-Process` call exits within the 5 s with an empty exit code; with `$null = $probe.Handle`
right after `Start-Process`, the exit code is 0.

Fix to make (not applied in the cleanup, because it turns ccache on for every session's build): read the handle
after `Start-Process`. Check first that ccache does not hang in the sandboxes the probe guards against (the comment
above it). The ccache store at `%USERPROFILE%/.cache/ccache` holds 0.9 of 5 GiB from older builds.

## 2026-11-20 B-1279 Local dev did not build from its own commits: 264587ac called gun-noise functions only uncommitted files defined (T-MODDING-011, fixed)

Severity high for anyone who built local dev from its commits (the client did not compile). Commit 264587ac
(2026-11-19, "Migrate gun noise callers to delivery receipts", local only) changed `bgunDecreaseNoiseRadius` in
`src/game/bondgun.c` to call `wgV2RuntimeNoiseBegin`, `wgV2RuntimeObserveNoise` and `wgV2RuntimeNoiseComplete`
and to use `wg_v2_delivery`. Those existed only in the uncommitted copies of `port/include/weapon_graph_v2_runtime.h`
and `port/src/weapon_graph_v2_runtime.cpp` in the canonical checkout, and every build there compiled the working
copies, so no build saw the gap. Found in the 2026-11-20 cleanup by comparing the symbols at HEAD with the working
copies.

Fix: acbce249 commits the rest of the reviewed V5 patch the callers belong to (the runtime, the world-hit harness,
the `prop.c` delivery path, with one lost seven-line hunk restored from week-pd/inflight-20261114). The merged tree
(dev plus PR 13, d1932f55) built from commits alone, client and test binary, in a clean worktree and passed
pd-tests in full. SP-99.

## 2026-11-19 B-1278 The offline AI-buddy smoke waits on Agent Select, which a game started in the background never satisfies (T-NETWORKING-007, fixed in code 2026-11-21, not yet run in a game)

Fix (claude-pd-mp-rules-20261121): boot flag `--launch-offline-room` (`port/src/main.c`). It drops into CI, keeps the
Main Menu from auto-opening, and `bootLaunchOfflineRoomTick` (after the `--launch-load-agent` tick in `pdmain.c`)
waits for the staged agent, marks the file selected as Agent Select leaves it (so CI does not force Agent Select),
waits for the CI menu gate (`menuCiReadyForMenuOpen`, the gate the CI menus use) and then makes the Main Menu's own
Combat Simulator call, `menuhandlerMainMenuCombatSimulator(MENUOP_SET, ...)`, so the Room opens by the same code as a
click. With no active agent it stands down and Agent Select opens as usual. `coop_ai_buddies_offline_smoke` now boots
with `--launch-load-agent smoke --launch-offline-room` and waits only for gameplay and the results. Pinned by
`[b1278]` in `tests/test_net_mission_end_static.cpp`. To prove: that smoke in the milestone finalization batch.

Severity low (test only, but it leaves the offline co-op start with simulant buddies unproven in a run).
`coop_ai_buddies_offline_smoke` failed 20/38 in rerun 2 (q_20261119011551074_865057f0, ddac61d6 on shared base 9,
`results-20261119T014139Z.json`). The held-tap fault of the first run is gone: the tap lands on the PD logo, the
title is left and Agent Select opens. The smoke then waits for `agent_select_ready`, and that condition needs the
game window to hold keyboard focus; a game the smoke runner starts never has it (`focus_lost=1
agent_select_imgui_reject=0x1`), so the wait runs out after 120 s and every later step fails. SP-51. The hosted
start is proven (`network_coop_ai_buddies_smoke` 29/29, 2026-11-17). dev's front-end work of 2026-11-19
(T-MENUS-003) does not change this: its own Agent Select run needed a focused window.

Fix to make: a way into the offline Room that passes neither Agent Select nor the main menu, for example a boot
flag that opens the offline Room as `--launch-mp-room` opens a hosted one; `--debug-room-tab campaign` and
`--debug-auto-start-match` then start the mission as they do in the hosted smoke.

## 2026-11-19 B-1277 A co-op client's rejoin is refused at the scheduler check of the reconnect prop set (T-NETWORKING-007 S2, fixed in code 2026-11-21 at protocol v66, not yet run in a game)

Cause (found by reading the code, 2026-11-21, claude-pd-mp-rules-20261121): the check assumed every world prop with
no parent is on the scheduler chain, and the stage setup does not guarantee that. `setupCreateObject`
(`src/game/setup.c`) gives a prop to an object whose pad is -1 (a level script places it or hands it to a chr
later) and to one inside another object, and lists neither; an inside object whose container was never created
also keeps no parent. Such a placeholder is on no list on the host and on the client's fresh stage alike, so a
co-op mission (Defection here) fails the check while Combat Simulator arenas, which carry none, pass. A second
gap in the same reader: a child put back into the world with the host's paused bit went through `propPause`,
which does nothing for a `PROPFLAG_DONTPAUSE` prop, and the host's active bit alone cannot say whether a prop
with no parent is listed at all.

Fix (v66): the world row's lifecycle byte carries the host's scheduler presence (`NET_RECONNECT_LIFECYCLE_SCHEDULED`,
bit 8), and the writer refuses a corrupt chain (`NET_RECONNECT_PROP_STATE_SCHEDULER_MEMBERSHIP`). The receiver
places every world prop with the pure planner `netReconnectPlanWorldPropPlacement` (`port/src/net/net_reconnect.c`):
on no list where the host has it on none, the receiver's own active/paused choice where both sides list it, and the
host's active/paused state where only the host lists it (`netmsgReconnectListWorldProp` lists a paused row even for
a DONTPAUSE prop and registers its rooms, SP-92). Pinned by `[b1277]` in `tests/test_net_reconnect.cpp` and
`tests/test_net_lifecycle_static_2.cpp`. To prove: `network_coop_reconnect_smoke` in the milestone finalization
batch.

Severity high for co-op drop-in/drop-out S2 (the partner cannot rejoin; the host's mission carries on, S1).
`network_coop_reconnect_smoke` failed 50/55 in rerun 2 (q_20261119011551074_865057f0, ddac61d6 on shared base 9,
`results-20261119T013233Z.json`). B-1275's refusal is gone and the host side is complete: `MANIFEST-HOST: mission
weapons listed`, the partner un-parked, `PLAYER.INIT.COMMIT reconnect`, the host's whole sequence matched. The
client then refuses the end of the prop set at a later check of the same reader: `NET.RECONNECT.WORLD end rejected
reason=scheduler slot=282 syncid=283 a=0 b=0` (`netmsgSvcReconnectPropEndRead`). An object the host sends with no
parent is on neither the active nor the paused list on the client. The client disconnects for good (`terminal=1`,
`retryable=0`) and returns to the menu; the host parks the partner a second time and ends the mission alone.

Not known: which object slot 282 is. What narrows it: `NET.RECONNECT.WORLD begin ... removed=0 detached=0
local_weapon_pruned=0`; the host only stood still; the refusal comes 0.1 s after the client's stage start. The
reader changes list membership only for an object that had a parent on the client and has none on the host: it
detaches it, then calls `propUnpause` (which always lists it) or `propPause`, and `propPause` does nothing for a
prop whose flags carry `PROPFLAG_DONTPAUSE` (the row reader has already copied the host's flags by then). An object
with no parent on either side keeps the membership the client's fresh stage gave it. The host's state writer does
not check list membership for a level object (only the spawn writer does).

Added 2026-11-19 with this entry: the scheduler refusal first logs `NET.RECONNECT.WORLD scheduler detail`: the prop
type, object type, model number, the prop and object flags, the lifecycle bits the host sent (4 = on the host's
active list), whether the client's prop had a parent before the snapshot, and the first dynamic sync id. Next: read
that line from the next run of `network_coop_reconnect_smoke`, then fix the cause.

## 2026-11-18 Training completion saves through the Controller Pak path (T-ENGINE-007)

Firing Range, Device Training, and Holo Training awarded progress in memory and then invoked the legacy file manager. That path resolves a Controller Pak serial. The PC Agent Profile already serializes the medals and training flags through `saveSaveAgent`, which is also the Campaign result path. The three success handlers in `src/game/training.c` now use that save for the active named agent. Source and scoped diff review are complete. Build and ordinary tests still need a success and failure in each Training family, immediate restart through Agent Select, and persistent Duel eligibility.

## 2026-11-17 B-1276 Two network stage-transaction smokes time out before the stage is live on shared base 9 (T-NETWORKING-011, fixed in the fixtures, proven 2026-11-17: 100/100 and 102/102)

Severity low (test only, but it hides the co-op and counter-op start checks). `network_coop_stage_transaction_smoke`
failed 81/100 in the co-op branch's turn q_20261117051637463_af6eb56c (`results-20261117T110217Z.json`): both
processes waited for `network_stage_live` from boot with 220 s, and the host started the co-op stage at 217 s. On base
9 the listen host's server is up about 1:50 after start and the client joins about 1:30 later; the mission-end smokes
(300 s) went live at 3:29 and 3:44 in the same turns. `network_counterop_stage_transaction_smoke` has the same 220 s
and passed narrowly. Fix: both first waits 300 s, test timeout 420 s (schedule bound 368 s). Other network fixtures
with shorter first stage waits wait for `network_listen_ready` first, so the boot is not on their clock.

## 2026-11-17 B-1275 A co-op client's rejoin is refused at the end of the reconnect prop set (T-NETWORKING-007 S2, fixed, proven 2026-11-19; the rejoin now stops at B-1277)

Severity high for co-op drop-in/drop-out S2 (the partner cannot rejoin). First run of `network_coop_reconnect_smoke`
(47/52, `results-20261117T103226Z.json`): the host parked the timed-out partner (S1), un-parked it at the same spot
(spot_clear=1) and committed the reconnect; the client loaded the stage, began the world snapshot (`props=295`,
matching the host's `prop=295 dynamic_prop=2`) and rejected `SVC_RECONNECT_PROP_END` (0x58) as malformed, terminal,
so it disconnected with no retry and the host parked it again. `netmsgSvcReconnectPropEndRead` has eleven rejections
and logged none. No chr was made after the stage start on the host (no `NET.NPC: host announced` line). Each
rejection now logs `NET.RECONNECT.WORLD end rejected reason=<check> slot= syncid= a= b=`; the next run names it.

Cause, from the rerun (`results-20261117T141505Z.json`): `reason=dual_weapon slot=270 syncid=271 a=272 b=271`,
and SP-93 again. The host logged `NET: weapon 3 ('base:falcon2_silencer') has no session catalog id`, and the same
for the comms rider (53), the tracer bug (54), the laptop gun (14) and the silenced Falcon's model (251), in every
co-op and counter-op run today. A co-op or counter-op lobby start lists the stage, the players and the host's base
set, but no weapons, while the mission names its own: the starting loadout, the guards' guns, the items a level
script hands out. The reconnect snapshot names a weapon prop and its dual partner by session id, so on the client a
pair of silenced Falcons came back as unarmed with no partner number, and the dual check refused the whole set. The
same gap made any mission weapon a client receives by id (a drop, a spawn) unarmed.

Fix: the co-op and counter-op lobby start lists every base weapon (`netLobbyManifestAppendMissionWeapons`,
`MANIFEST-HOST: mission weapons listed=N`; a mod's custom weapon stays out, it is not on every client); their world
models follow (B-1273). Every co-op and counter-op net smoke now forbids both session-id warnings. Pinned by
`[b1275]` in `tests/test_net_mission_end_static.cpp`. SP-93.

Proven by rerun 2 (2026-11-19, q_20261119011551074_865057f0): none of its five runs logged a session-id warning
and the dual-weapon refusal is gone. The rejoin is refused at a later check instead (B-1277).

## 2026-11-17 B-1274 Networked co-op and counter-op run as a Combat Simulator match: no objective checks during the mission, no co-op AI branches (T-NETWORKING-007, fixed in code on week-pd/multiplayer-coop, not yet run)

Severity high for networked co-op and counter-op since 2026-04-09: the missions do not play as the original.
Found while researching co-op drop-out (S1) on 2026-11-17: the host log of `network_coop_mission_end_smoke`
(2026-11-16) shows `MATCH: mpReset normmplay=1 mplay=1 ... netmode=1` for a two-player co-op mission.

Root cause: the stage load calls `mpReset` for the co-op and counter-op roles (`port/src/pdmain.c`, coopplayernum or
antiplayernum set). The original `mpReset` then cleared `normmplayerisrunning` for those roles; commit 87e5347f0
(2026-04-09, "remove dead coop/anti config swap (Phase 2)") collapsed that to always true, saying "with coop/anti
gone, normmplayerisrunning is always true in mpReset()". Networked co-op and counter-op use the same roles, so every
networked mission has run as a match since. What that turns off (all gated on `normmplayerisrunning == false`):
the per-frame objective checks (`playerRenderHud`, `objectivesCheckAll`; objectives are checked once, at the mission
end, so no "objective complete" during play and no mid-mission SVC_OBJ_STATUS), the AI's co-op branches
(`if_coop_mode`, `scenario_source_runtime.c`), the guards' AI-versus-AI flag, and the mission death rules; the
match's tune switching ran instead. The smokes did not see it: they end the mission with `--debug-end-mission`.
The same commit also dropped the co-op and counter-op radar display option (`coopradaron`, `antiradaron`;
`g_NetCoopRadar` is carried in the lobby plan but never applied): not fixed here.

Fix (week-pd/multiplayer-coop): `mpReset` clears the flag again for the co-op and counter-op roles; a co-op host
alone (AI buddies) and the offline Room's co-op start clear `mplayerisrunning` and `normmplayerisrunning` before the
stage, as the original's mission list does, so a Combat Simulator match played before cannot leave them set; the
smoke harness counts a co-op host alone as a network mission; a client's objective-check skip, which now runs
every frame, logs once per stage. Pinned by `[b1274]` in
`tests/test_net_mission_end_static.cpp`. To run: every co-op and counter-op net smoke, plus the two AI-buddy smokes.
SP-95 (written as SP-94 at first; dev's SP-94 is B-1259's).

## 2026-11-17 B-1273 A dropped gun, and a replacement uplink, reach clients drawn as the roof gun (T-NETWORKING-011, fixed, checked in play)

Severity medium to high for networked Combat Simulator with simulants and the token modes: visible on every client.
Found by SP-93's new model warning on 4789effa (base 7): in `mp_hacker_central_net_smoke` (`results-20261117T014259Z.json`)
a simulant died and the host logged `NET: model 253 ('base:model_chrcmp150') has no session catalog id` and `model 246
('base:model_chrleegun1')` as it announced the guns it dropped (`BOT.DROP: host announced simulant 4 weapon=10
syncid=44`); in the carrier-death run the replacement uplink gave `model 19 ('base:model_chrdatathief')`.

Root cause: SVC_PROP_SPAWN names a weapon prop's model by session catalog id (`netWriteModelRef`), and the client's
reader builds the prop from that number with no fallback (`setupLoadModeldef(modelnum)`), so a missing id makes it
model 0, `MODEL_ROOFGUN`. The match manifest lists weapons (the pool, the set, the token) but not the models their
props are drawn with, so every gun a dying simulant or player drops, B-1209's replacement uplink and a dropped
briefcase were drawn on clients as the roof gun. The weapon number itself was right (B-1272 made the token's right).

Fix: the host's lobby start adds the world model of every weapon the manifest lists
(`netLobbyManifestAppendWeaponModels`, `playermgrGetModelOfWeapon`, every game mode, `MANIFEST-HOST: weapon world
models listed=N`), and the scenario token's own model (`scenarioTokenModelNum`: the uplink's prop is
`MODEL_CHRDATATHIEF`, which is not the weapon's mapped model; `MANIFEST-HOST: scenario N token model M`). The token-mode,
carrier-death and Combat-with-simulants net smokes forbid the model warning. Pinned by `[b1273]` in
`tests/test_net_bot_pickup_static.cpp`. SP-93.

Checked in play 2026-11-17 on 326df90c (built in hub turn q_20261117014917169_52e3ea4a, pd-tests 2045/2045, base 7):
`mp_hacker_central_net_carrier_death_smoke` PASS 107/107 (`results-20261117T035213Z.json`) and `mp_combat_net_bots_smoke`
PASS 71/71 (`results-20261117T040515Z.json`). The host lists the uplink's model (`MANIFEST-HOST: scenario 2 token model 19
as 'base:model_chrdatathief'`) and 13 weapon world models (`listed=13 for 28 entries`; 26 in Combat); in the carrier-death
run five dropped guns are announced (weapons 8, 2, 8, 8, 5) and the replacement uplink is placed, with no `has no session
catalog id` line on either side, and the client replays simulants taking the dropped guns as the right weapons (items 44,
45, 49 as weapons 8, 2, 8).

## 2026-11-16 B-1272 A scenario's token reaches clients as no weapon: the uplink and the briefcase have no session catalog id (T-NETWORKING-011, fixed; checked in play for the uplink and the briefcase)

Severity high for networked Hacker Central, Hold the Briefcase and Capture the Case. Seen in the first run of
`mp_hacker_central_net_carrier_death_smoke` (d44b87d0, base 7, `results-20261116T212817Z.json`, 102/104): the host
killed the uplink's carrier, made the replacement and announced it (`SCENARIO.NET: host announced the new uplink
syncid=48 ... rc=0`, B-1209 working), and when simulant 3 took it the host sent `BOT.PICKUP: host sent simulant 3
item 48 weapon=46` while the client replayed `item 48 weapon=1 result=1`: its copy of the uplink was a
`WEAPON_UNARMED` weapon prop. (The run's other failure was the spec's: the match ended on the score limit, 5
downloads, before the time limit.)

Root cause: messages name a weapon by its session catalog id (`netWriteWeaponRef`), and a reader turns id 0 into
`WEAPON_UNARMED` without a word. The session catalog is built from the match manifest, whose weapon pool takes only
weapons with an MP weapon slot (`s_manifestSkipMpWeaponSlot`); the data uplink (46) and the briefcase
(`WEAPON_BRIEFCASE2`) have none, so no Combat Simulator manifest listed them. Every message naming one reached
clients as no weapon: an announced token (B-1209's replacement uplink or case, a dying carrier's dropped case), a
simulant's held weapon (`SVC_CHR_STATE`, the resync), a player's gun in `SVC_PLAYER_STATS`, reconnect inventory. A
client's token scans look for the token's weapon number on the ground (`htmNetPresent`, HTB and CTC alike), so a
replacement or dropped token also vanished from its radar and HUD. Tokens placed at the stage start are made on
both machines from the scenario's own template, so the first one was right, which is why the scoring smokes passed.

Fix: `scenarioTokenWeaponNum` (`scenarios.c`) names each scenario's token (Hacker Central the data uplink, Hold the
Briefcase and Capture the Case the briefcase), and the host's lobby start adds that weapon's catalog closure to
the Combat Simulator manifest after the scenario's own (`MANIFEST-HOST: scenario N token weapon W as '<id>'`), or
refuses the start. `netWriteWeaponRef` now says once per weapon, while a session catalog is live, when it sends a
real weapon with no session id (`NET: weapon N ('<id>') has no session catalog id; clients read it as unarmed`),
forbidden in the four Hacker Central, Hold the Briefcase and Capture the Case net smokes. Co-op and counter-op
manifests scan the stage's setup and AI lists for weapons (`manifestBuildMission`), a separate path. Pinned by
`[b1272]` in `tests/test_net_bot_pickup_static.cpp`. SP-93.

**Checked in play for the uplink (b2a2a720, base 7, `mp_hacker_central_net_carrier_death_smoke` 106/106,
`results-20261116T215453Z.json`):** the host's manifest listed it (`MANIFEST-HOST: scenario 2 token weapon 46 as
'base:datauplink'`), the host killed the first carrier at 23.4 s and announced the replacement (syncid 48), and the
client replayed simulant 4's pickup of it as `item 48 weapon=46 result=0` and showed simulant 4 as the holder; no
`has no session catalog id` warning. **And for the briefcase** (b2a2a720, `mp_hold_the_briefcase_net_carrier_death_smoke` 102/102,
`results-20261116T222007Z.json`): `MANIFEST-HOST: scenario 1 token weapon 79 as 'base:briefcase2'`; the host killed the
carrier at 23.5 s, the dying simulant's case was announced (`BOT.DROP: host announced simulant 2 weapon=79 syncid=48`),
and the client replayed simulant 4's pickup of it as `item 48 weapon=79 result=0` and showed simulant 4 as the holder.

## 2026-11-16 B-1271 Hacker Central: a simulant that reaches the terminal facing away stands there and never downloads (T-NETWORKING-011, fixed with Mike's OK; checked in play)

Severity medium (high when first seen): the simulant it catches scores nothing for the rest of the match; it takes a simulant reaching range facing away, local or networked. First seen in `mp_hacker_central_net_smoke` on binary
a669d118 (base 7, `results-20261116T173009Z.json`, 85/86, the only failure the score count): simulant 2 took the
uplink at 17.8 s, reached the terminal at 42.0 s (`HTM: player 2 download not facing the terminal at 42.0 s xz=240
y=62 angle=171 weapon=2 action=44`, 240 units away, armed, facing away, still in `MA_AIBOTGOTOPROP`) and logged
nothing more until the match ended at about 2.5 min: no new goal, no stuck relocation, no download. The same binary
run locally (`mp_hacker_central_local_smoke`, 32/32, `results-20261116T174929Z.json`) scores: there the simulant
logged the same `not facing` at 240 units with weapon 2 and action 44, then 0.3 s later `in progress ... weapon=1
action=46` (unarmed, `MA_AIBOTDOWNLOAD`), and completed three downloads (score 6). So something in the networked
host keeps the simulant from leaving `MA_AIBOTGOTOPROP` for `MA_AIBOTDOWNLOAD`; `bot.c` makes that switch when
`g_ScenarioData.htm.playernuminrange` names the simulant, which `htmTickChr` sets whenever it is within 250 units.
Not found by reading. **Diagnostic (746d79a7):** while a simulant holds the uplink without downloading, `HTM:
simulant player N holds the uplink, not downloading (<reason>) ... action= actiontype= inrange= target= weapon=
netmode=` every 10 s. Next run: `mp_hacker_central_net_smoke`.

**Cause (found by reading the code, 2026-11-16; the queued run's diagnostic will confirm it):** the original's
download rule can deadlock a simulant, and B-1207 removed what used to break it. Within 250 units (and 200 up or
down) of the terminal, `htmTickChr` counts the simulant in range (`playernuminrange`), so its next decision is
`MA_AIBOTDOWNLOAD`: it stands (`chrStand`) and `botinvTick` unarms it (in that action it skips weapon scoring and
switches to `WEAPON_UNARMED`). A download then starts only if it faces the terminal within 45 degrees, and the only
code that turns it toward the terminal (`botTick`'s target angle, `MA_AIBOTDOWNLOAD && htm.dlterminalnum != -1`)
needs a download already running: `htmTickChr` starts one each frame and cancels it in the same call while the
simulant faces away, so `dlterminalnum` is always -1 when the turn is decided. A simulant that reaches range facing
away therefore stands there for good. Locally it walked in facing the terminal (in range at 100 degrees while still
walking, 45 degrees when it stood: download started 0.3 s later); over the network it reached range walking away
(171 degrees) and stood. Until B-1207 the port's stuck watchdog moved any simulant that stood 3 s in
`MA_AIBOTDOWNLOAD`, which broke this deadlock and also cancelled every good download. **For Mike:** the fix changes
the original's simulant behaviour: while in `MA_AIBOTDOWNLOAD` before its download starts, a simulant turns toward
the terminal (at its normal turning speed, about 1 s for half a turn), so it starts instead of standing. Recommended;
the alternative is to leave the original rule, with a simulant sometimes standing at the terminal forever.

**Run on c35b35a2 (2026-11-16, `mp_hacker_central_net_smoke` 86/86, `results-20261116T201815Z.json`): networked
Hacker Central scored, so the deadlock is intermittent.** Simulant 3 took the uplink at 18.8 s and spent 46 s
fetching items (`holds the uplink, not downloading (out of range) ... xz=545` to `2371 ... action=42`,
`MA_AIBOTGETITEM`), went for the terminal (`action=44`), reached it at 65.1 s 230 units away facing it (4 degrees,
still armed: `not holding the uplink`), was unarmed in `MA_AIBOTDOWNLOAD` 0.1 s later (`in progress ... weapon=1
action=46`) and completed four downloads (85.2, 105.3, 125.3 and 145.3 s). The host and the client both rank it
first with 8 points. As the cause says, it takes a simulant reaching range facing away (171 degrees in the a669d118
run) to stand for good; the fix question for Mike stands.

**Caught by the diagnostic (b2a2a720, `mp_hacker_central_net_carrier_death_smoke` 106/106,
`results-20261116T215453Z.json`; the smoke does not need a score):** after the host killed the first carrier,
simulant 4 took the replacement uplink and started a download at 47.3 s (`in progress ... angle=43 weapon=1
action=46`), drifted into the terminal (`not facing the terminal at 49.2 s xz=46 ... angle=89`), which cancelled it,
and from 50.9 s to the end of the match stood 183 to 198 units away facing 160 to 175 degrees away: `holds the
uplink, not downloading (not facing the terminal) ... action=46 actiontype=1 inrange=4 target=-1 weapon=1`, every
10 s. That is the deadlock in the cause: in `MA_AIBOTDOWNLOAD`, standing (`ACT_STAND`), unarmed, counted in range,
never turned toward the terminal. Nobody scored in that match. So a simulant also gets there by losing its facing
during a download, not only by arriving facing away.

**Fix (Mike's OK 2026-11-16, "fix the bug"; constraints.md):** in `botTick`'s target angle, right after the branch
that turns a downloading simulant toward its download's terminal, a simulant in `MA_AIBOTDOWNLOAD` whose download
has not started turns toward the nearest Hacker Central terminal (`botHtmNearestTerminal`, Hacker Central only), at
the ordinary turning speed (about 1 s for half a turn), so the download starts once it faces within 45 degrees.
This departs from the original on purpose. Pinned by `[b1271]` in `tests/test_net_bot_pickup_static.cpp`. Not
built yet; the queued carrier-death run builds it.

**Checked in play (4789effa, base 7):** `mp_hacker_central_net_carrier_death_smoke` 106/106
(`results-20261117T012828Z.json`) and `mp_hacker_central_net_smoke` 88/88 (`results-20261117T014259Z.json`, it
needs a score): no `holds the uplink, not downloading (not facing the terminal)` line in either, 5 and 2 downloads,
scores 10 (the score limit) and 4.

## 2026-11-16 B-1270 A co-op client crashes when the host removes a chr whose rooms an NPC resync changed (T-NETWORKING-011, fixed; checked in play)

Severity critical for networked co-op and counter-op: the client crashes. Seen in `network_coop_deepsea_end_smoke`
on binary a669d118 (base 7, `results-20261116T173916Z.json`, 116/126): as the intro cutscene ended, the client read
the host's first chr removal (`NET.NPC: client removed chr syncid=1 chrnum=64 body=140 dead=0 dropitems=0 (the host
removed it)`, the intro actor) and crashed at once: `FATAL: ACCESS_VIOLATION PC=... (+0x153681)`, backtrace
`propIsOfCdType` (prop.c, reading `chr->actiontype` with `chr` NULL) <- `cdCollectGeoForCyl` <-
`cdFindGroundInfoAtCyl` <- `chrSetPos` <- `netmsgSvcNpcMoveRead` <- `netStartFrame`. B-1206's follow-up (a669d118)
made this the first removal message ever to reach a client; before it, no client removed a chr this way.

Root cause: the client's NPC resync (`netmsgNpcResyncRecordApply`, every SVC_NPC_RESYNC, including the one at the
stage start) wrote the host's rooms straight into `prop->rooms`, without taking the chr out of its old rooms' prop
lists or putting it into the new ones. A chr whose rooms changed stayed listed in its old rooms. While it lived
that was harmless (the listing points at a live chr), but when it was removed, `chrRemove` and `propFree` cleared
only the rooms named in `prop->rooms`, and `propFree` left a `PROPTYPE_CHR` prop with no chr in the old rooms'
lists. The next NPC move's collision query in such a room read it. The host never runs a resync apply, so only
the client crashed. The same direct write is in the bot resync (`netmsgSvcChrResyncRead`) and the prop resync
(`netmsgSvcPropResyncRead`), and SVC_PROP_MOVE and the lift move moved the registration only when `prop->active`,
which leaves a paused, listed prop in its old rooms (SP-92).

Fix (746d79a7): new `propSetRooms` (prop.c) copies a rooms list exactly (up to and including its terminator; the
NPC snapshot digest covers the rooms) and moves the prop's registration with it when the prop is in the world
(active, or listed in its first room). All five paths use it. The prop resync also applies to objects only now, as
the host sends: `prop->obj` shares storage with `prop->chr`, so a row whose id named a chr on the client wrote
object fields into the chr. Pinned by `[b1270]` in `tests/test_net_mission_end_static.cpp`. Not changed: the
bot-authority stub update (`netmsgClcBotMoveRead`, a stub with no room registration) and the reconnect
snapshot paths, which keep their own staged registration.

**Checked in play (c35b35a2, base 7, `network_coop_deepsea_end_smoke` 126/126, `results-20261116T195117Z.json`):**
the client read the same removal at 75.3 s (`client removed chr syncid=1 chrnum=64 body=140 dead=0 dropitems=0 (the
host removed it)`) and one more in Defense (`syncid=147 chrnum=56`), and played on to the end screen and a clean exit
(`MEMPC [shutdown]: all 5 allocations intact`); no fatal in either process. The reconnect snapshot's prop state
(`netmsgSvcReconnectPropStateRead`) uses `propSetRooms` too since ae1fa9fd (SP-92 audit).

## 2026-11-16 B-1209 Hacker Central's new uplink, and a new briefcase, exist only on the host (T-NETWORKING-011, fixed; announcement checked in play, its weapon type was B-1272)

Severity high for networked Hacker Central: once the first uplink carrier dies, no client sees the uplink or can
take it, and the client skips every host pickup of it. Seen in `mp_hacker_central_net_smoke` on binary 5cd5d0e5
(639c9a8a, base 7, `results-20261116T150839Z.json`, 84/86): simulant 3 died at 94.7 s carrying the uplink; the
host announced its two guns (`BOT.DROP: host announced simulant 3 weapon=2 syncid=57`, `weapon=5 syncid=58`), then
`htmTick` found no uplink and made a new one at crate pad 69 (`HTM: uplink placed pad=69 source=crate`), prop 59, a
dynamic id. The client logged nothing about it, and when simulant 3 took it at 102 s: `BOT.PICKUP: client skipped
item 59 for simulant 3, it is not in this world (no prop has this sync id; first_dynamic=39 ...)`.

Root cause: the original drops no uplink at a carrier's death; `htmTick` makes a new one wherever a crate stands
(`htbCreateUplink`), and Hold the Briefcase's `htbTick` does the same for its case when none is left
(`htbCreateToken`; rare, since a dying carrier drops the case as an announced weapon). A networked client runs
neither (T-NETWORKING-011: it shows the host's scenario and never makes its own), and the host never announced
the prop, so it had an id only on the host (SP-85).

Fix: new `scenarioNetAnnounceProp` (`scenarios.c`): on the listen host, `SVC_PROP_SPAWN` then `SVC_PROP_MOVE` for
the new prop, as `botinvDrop` announces a simulant's drop (log `SCENARIO.NET: host announced the new <uplink |
briefcase> syncid=... rc=0`). `htmTick` and `htbTick` call it right after making the prop during the match; a
client's `htmNetPresent` already finds a ground uplink by scanning its props. No wire change. Pinned by `[b1209]`
in `tests/test_net_bot_pickup_static.cpp`. Not changed: the crate that the new uplink replaces is hidden on the
host only (`OBJHFLAG_DELETING`, regenerating); the prop dirty check's `SVC_PROP_RESYNC` carries hidden state.

## 2026-11-16 B-1208 A client's unchanged settings resend cancels the host's ready gate (T-NETWORKING-011, fixed; checked in play)

Severity medium: a networked start fails whenever a client sends its settings again, with nothing changed, inside
the ready gate. The host cancels the start (`NET: ready gate aborted by '<name>'`) and someone must press Start
again; an autostarted smoke fires once, so its match never starts. Seen in `network_coop_mission_end_smoke` on binary
5cd5d0e5 (639c9a8a, base 7, `results-20261116T145034Z.json`, 62/130; 130/130 at 490e8baf): the host's autostart
added the client to room 1 at 02:18.78 and fired at 02:19.05; the client's Room opened as it was moved in, its match
setup reset sent `CLC_SETTINGS` (same body, head, team, handicap and name; no rename line followed), and that
reached the host at 02:19.21, inside the gate: `PLAYER.INIT.ROLLBACK ready-gate reason=settings_changed client=1`,
and both machines waited 300 s for a stage that never started.

Root cause: `netmsgClcSettingsRead` rolls the gate back for every valid `CLC_SETTINGS` from a preparing participant
(B-1103, SP-67), changed or not, and a client sends one whenever its Room opens (`matchConfigInit` calls
`netClientSettingsChanged` after the match setup reset) and whenever a settings menu closes, changed or not
(`pdgui_menu_playerconfig.cpp` calls the send "idempotent and cheap").

Fix: a candidate equal to the client's published settings, field for field and raw from the wire (name, body,
head, options, team, handicap, fovy, fovzoommult), keeps the gate and publishes nothing (`NET: CLC_SETTINGS
client=N repeats its settings during the ready gate; gate kept, nothing published`); any difference still rolls the
gate back before publishing, as SP-67 requires. New smoke-only seam `--debug-ready-gate-settings-repeat` (the B-1103
seam's twin: the same ordinary send at the PREPARING boundary with nothing changed) and
`ready_gate_settings_repeat_smoke` (port 27218): the gate keeps and the match starts on both machines;
`ready_gate_settings_rollback_smoke` stays the negative twin (its changed handicap still rolls back). Pinned in
`tests/test_prepared_stage_transaction_static.cpp` (the B-1067 and B-1103 cases).

**Checked in play (a669d118, base 7):** `ready_gate_settings_repeat_smoke` (`results-20261116T170717Z.json`): `NET: CLC_SETTINGS client=1 repeats its settings during the ready gate; gate kept, nothing published`, then the match started on both machines; its one failure was the smoke's own step order (the stage start logs two lines before the rollback snapshot release), fixed in 746d79a7. `network_coop_mission_end_smoke` passed 130/130 (`results-20261116T171610Z.json`).

## 2026-11-16 B-1207 Hacker Central simulants carry the uplink to the terminal but never complete a download (T-NETWORKING-011, fixed; checked in play locally and networked)

Severity medium to high for Hacker Central with simulants: no simulant scores, so a match against simulants has no
contest; humans are not affected. Seen in `mp_hacker_central_local_smoke` before CORE's B-1204 fix (df1734a2,
`results-20261116T101515Z.json`) and after it (490e8baf, 8dafe7f0, `results-20261116T130655Z.json`, 31/32, the
missing line is the nonzero score). In the second run the uplink stands on its pad (pad 67, y 2), simulant 2 takes it
at about 4 s and heads for terminal 0 (pad 7 at (-3305,36,1606), room 39) at 4.4, 22.8 and 41.5 s, fetching shields
in between, and no download completes in 150 s. The terminal is reachable: it stands on room 39's ground floor
(floor y 0, pad y 53) with route point pad 221 123 units away at the same height.

What the code needs (`htmTickChr`): the carrier within 250 units (xz) and 200 (y) of the terminal, facing it within
45 degrees and unarmed (`aibot->weaponnum == WEAPON_UNARMED`), for 20 s without a break. A simulant switches to
unarmed and turns to the terminal only in `MA_AIBOTDOWNLOAD`, which it enters after a check fails within range
(`playernuminrange`). It turns only while a download is active (`botTick` reads `dlterminalnum`), while
`scenarioTickChr` runs after that facing step and cancels the download in the same tick when a test fails. Which
test fails is not known yet. Diagnostic (this commit, offline and host): `HTM: uplink holder A -> B at T s
pos=(...)` and `HTM: player N download <out of range | not facing the terminal | not holding the uplink | in
progress> at T s xz= y= angle= weapon= action=` on each change (300 lines a stage at most), and `HTM: player N
download complete at T s points=P`. Next run: `mp_hacker_central_net_smoke` in the queued objective-scoring turn,
and `mp_hacker_central_local_smoke` after it. If the cause is the original's own logic, the fix changes how the
mode plays and goes to Mike first.

**Root cause (found 2026-11-16 with the diagnostic; not the original's logic):** in `mp_hacker_central_net_smoke`
on 5cd5d0e5 (`results-20261116T150839Z.json`) simulant 3 reached the terminal three times (32.6, 54.4 and
142.3 s: 209 to 230 units away, facing it, unarmed, `in progress` in `MA_AIBOTDOWNLOAD`), and each time, about 3 s
later, `STUCK: bot slot=1 ... relocated to safe waypoint 73 (-698,394,-1889) penalty=25.0%` moved it 4,330 units
away in the same frame (`out of range ... xz=4330`). The port's stuck watchdog (`botTick`, Phase F 2026-04-02, not
in the original) relocates a simulant that moved less than 10 units in 3 s while its action counts as navigation
intent, and `MA_AIBOTDOWNLOAD` was on that list, though a download is 20 s standing at the terminal. So every
download a simulant began was cancelled 3 s in, with a 25% health penalty. **Fix (in code, not yet run):**
`MA_AIBOTDOWNLOAD` leaves the intent list; nothing else changes, so the original's download rules apply again (no
gameplay decision needed). King of the Hill was checked for the same trap (a simulant holding the hill stands in
`MA_AIBOTGOTOPOS`): its simulants scored 1 and 2 points with one relocation a run in three earlier runs, so that
case stays as it is. Pinned by `[b1207]` in `tests/test_net_bot_pickup_static.cpp`. The same run found B-1209.

**Checked in play locally (a669d118, base 7):** `mp_hacker_central_local_smoke` 32/32 (`results-20261116T174929Z.json`): a simulant reached the terminal, turned and unarmed in 0.3 s, and completed three downloads (`HTM: player 3 download complete at 99.7 s points=1`, then 119.8 and 139.8 s; score 6), with no stuck relocation. Networked, a simulant still scores nothing, for another reason: B-1271.

**Networked, checked in play (c35b35a2, `mp_hacker_central_net_smoke` 86/86, `results-20261116T201815Z.json`):**
simulant 3's downloads ran to completion on the host four times, with no watchdog move, and the host and the client
results both show its 8 points. A simulant that reaches range facing away still stands for good: B-1271.

## 2026-11-16 B-1206 A co-op client keeps every chr the host removes, then refuses every NPC snapshot (T-NETWORKING-011, fixed; snapshot sweep and removal message checked in play, the second once B-1270 was fixed)

Severity high for networked co-op and counter-op: a guard that a level script removes stays frozen where it stood on
the client, and NPC state correction stops for the rest of the stage while the host resends a full snapshot about 20
times a second. Seen in `network_coop_deepsea_end_smoke` on binary df1734a2 (db7c8b51, base 7,
`results-20261116T121205Z.json`, 120/126; the Next Mission into Defense itself passed): from the first snapshot
after Deep Sea loaded, `NET: SVC_NPC_RESYNC rejected incomplete snapshot tick=2698 server_count=51
local_ready_count=53`, then 52 against 55 until the stage ended, 883 rejections in 42 s, each followed by `SVC_NPC_SYNC
snapshot mismatch` and `requesting npc resync`. Both machines had the same 290 initial ids, the two chrs the host's
AI made (syncid 292 and 293) reached the client (`client made the host's chr`), and no chr died. So the host removed
2 chrs in the stage's first second and one more later, and the client kept all three.

Root cause: every in-stage removal of a chr goes through `chrTick`'s `CHRHFLAG_DELETING` branch (`aiRemoveChr`
sets the flag; so do a corpse's fade, an off-screen reap and a robot's death), and a co-op client runs no NPC AI or
actions (`chraTick` returns early for NPCs on a client; B-1191 already keeps a client's fall check from deleting
one), so nothing removed the client's copy, and no message told it to.

Fix (wire v64, still unreleased, so no bump; ledger in `net.h`): new `SVC_NPC_REMOVE` 0x7f {sync id, flags; bit 0
= the host's removal dropped the chr's items}. The host writes it in that branch, before the removal, in co-op and
counter-op (log `NET.NPC: host removed chr syncid=... dropitems=...`). A client removes its copy at once, as the
host's `chrTick` and prop loop do (`chrRemove`, then `TICKOP_FREE`), dropping the items first only when the host
did (log `NET.NPC: client removed chr ... (the host removed it)`). A client that is still loading cannot take the
message (its reader needs `CLSTATE_GAME`), which is when a level script's first removals happen, so
`netmsgSvcNpcResyncRead` also removes every NPC of the client's that the snapshot does not name, after it has read
the whole snapshot, and then counts again (log `... (the host's snapshot does not name it)`). Pinned by `[b1206]`
in `tests/test_net_mission_end_static.cpp`. Pattern SP-85 (its removal case). Not covered by the message: the
counter-op takeover in `player.c` (the anti player takes a guard's body) removes the guard outside `chrTick`; if
that ever runs on one machine only, the snapshot sweep removes the other copy at the next snapshot.

**Checked in play 2026-11-16** on binary 5cd5d0e5 (639c9a8a), `network_coop_deepsea_end_smoke` 126/126
(`results-20261116T143714Z.json`; 120/126 with 883 refused snapshots before): no snapshot refused. But none of the
host's four removals reached the client as a message. Each came on a frame where `netEndFrame` throws away the game
tick's messages: three before the stage replication was active (two guards in Deep Sea's first tick, one in
Defense's), which the release snapshot covered (the client's sweep removed all three), and the intro's actor
(syncid 1, chrnum 64) on the frame that published the cutscene END authority packet: its message was dropped and no
later snapshot came, so the client kept that chr for the rest of Deep Sea. **Follow-up fix (in code, not yet run):**
`chrTick` queues the removal (`netServerNpcRemoveQueue`, same `host removed chr` line), and `netEndFrame` sends the
queue from the ACTIVE entity broadcast, after any authority packet and ahead of the frame's spawns, moves and states
(`NET.NPC: host sent the removal of chr syncid=...`). The release baseline, a new stage, a reset and a rolled-back
start empty the queue (`N queued chr removal(s) not sent: <why>`); a chr made after the stage start that was never
announced is skipped (no client has it); a full queue (64) asks for a full NPC snapshot. The pins in `[b1206]`
follow. The class, every one-shot message written in the game tick, is SP-91.

**The queued removal in play (a669d118, base 7):** in `network_coop_deepsea_end_smoke` (`results-20261116T173916Z.json`) the host queued and sent the intro actor's removal as the intro ended (`NET.NPC: host removed chr syncid=1 chrnum=64 ... action=3`, then `host sent the removal of chr syncid=1`), and the client removed it (`client removed chr syncid=1 ... (the host removed it)`), the first removal message any client has read. The client then crashed in the next NPC move: B-1270.

## 2026-11-16 B-1205 A client skips a simulant's pickup of an item it still has respawning (T-NETWORKING-011, fixed; checked in play)

Severity medium: the shield or crate a simulant took on the host stays on the client's floor until the client's own
respawn cycle, so the two worlds disagree about it; no crash. Seen in `mp_combat_net_scorelimit_smoke` on binary
df1734a2 (db7c8b51, base 7, `results-20261116T120430Z.json`, 75/76, the only failure): `BOT.PICKUP: client skipped
item 13 for simulant 3, it is not in this world (the item is waiting to respawn; ... objtype=21 timetoregen=21 ...)`
and the same for item 19 (`objtype=20`, `timetoregen=17`), a shield and a multi-ammo crate. On the host, simulant 3
took item 13 at 03:10.37 and again at 03:30.93 (host log time), 20.5 s apart, with a 20 s respawn wait
(`TICKS(1200)`): the second time the moment it was back. The client's copy still had 17 to 21 ticks (about 0.3 s)
to wait. Item 19 went the same way, and its next pickup 20 s later was replayed fine.

Root cause: each machine starts the respawn wait when it applies the earlier pickup
(`propExecuteTickOperation(..., TICKOP_FREE)` sets `timetoregen`), so the client's wait ends later than the host's
by the message's delivery delay plus any difference in counted frame time. `netmsgSvcBotPickupRead` refused any item
with `timetoregen != 0`. `netmsgSvcPropPickupRead` (a human's pickup on the host) had the same exposure the other
way: it took the still-respawning item without the respawn's own steps (a shield's charge is refilled when it
comes back).

Fix (client only, no wire change): `objTick`'s respawn steps moved unchanged into `objTickRegen(prop, elapsed60)`
(`propobj.c`), and the new `objRegenNow(prop)` runs them to the end: the fade-in start (the item comes back, with
its respawn sound) and then the finish. Both pickup replays call it first when this client still has the item
respawning (logs `BOT.PICKUP: client finished item N's respawn for simulant S, T ticks before its own wait ended`
and `NET: SVC_PROP_PICKUP client finished prop N's respawn ...`), then replay as before, so the wait starts again
as the host's did. Pinned by `[b1205]` in `tests/test_net_bot_pickup_static.cpp`. Seen working once in play
2026-11-16 (490e8baf, `mp_hold_the_briefcase_net_smoke` 84/84): `BOT.PICKUP: client finished item 6's respawn
for simulant 3, 27 ticks before its own wait ended`, then `client replayed simulant 3 item 6`; the Combat
score-limit re-run that failed on it is queued.
**Checked in play 2026-11-16** on binary 5cd5d0e5 (639c9a8a), `mp_combat_net_scorelimit_smoke` 76/76
(`results-20261116T143007Z.json`; it failed on this race before): the client finished an item's respawn early
four times, three simulant pickups (38, 39 and 47 ticks early) and one host-player pickup (`SVC_PROP_PICKUP client
finished prop 8's respawn for client 0, 54 ticks ...`), replayed all 19 simulant pickups, skipped none, and the
match ended by score limit 1 on both machines.

## 2026-11-16 B-1204 Objects on scenario-source stages are dropped from 100 units under their pad, so many sit inside a floor or fall to the one below (T-NETWORKING-011, fixed by CORE)

**Fixed** by CORE on week-pd/core 9a9cfc9f, cherry-picked here as 0ad19dca: each scenario pad keeps its own
`PADFLAG_HASBBOXDATA` (the ROM's flags in pads.json), so a boxless pad's six box floats stay unread and
`padUnpack` gives its usual default, so objects stand on their pads again. No re-extraction (every base pad without the bit
has exactly that default box); a contract pin refuses the forced bit. **Checked in play 2026-11-16** on binary
490e8baf (8dafe7f0), base 7: in `mp_hold_the_briefcase_net_smoke` (84/84, `results-20261116T125201Z.json`) the
case stands on pad 67 at y 4, a simulant takes it at 18.4 s and scores, and simulants collect the upper-floor
crates and weapons (y 278 to 291) they could never take before; `mp_combat_local_scorelimit_smoke` (24/24,
`results-20261116T125730Z.json`, no kill in 240 s before) ends by score limit 1 after a simulant's kill.

**Root cause (read from data and source, 10:15 UTC):** `s_scenarioPadFlags` (`port/src/scenario_source_runtime.c`)
sets `PADFLAG_HASBBOXDATA` on every packed pad, and the extractor gives each pad that had no box in the ROM a default
box of -100..100 (`romextract_pdarena.c`, `f32 bbox[6] = { -100.0f, 100.0f, ... }`). Complex has 226 pads; only 5
carry the ROM flag, and their boxes start at the pad (y 0 to 105). The other 221 now claim a 200-unit box.
`setup.c` places a pad's object from `centre`: the pad position when the pad has no box data, otherwise the
box centre moved down by half its height, so here 100 under the pad. `func0f06a730` (`propobj.c`) then
looks for a floor from there. Pads sit about 52 above their floor, so the search starts about 48 units under
it. With another floor below (Complex rooms 24, 26, 39: floors at 0 and 276), the object lands on that one,
276 units down (the ground-floor weapons at y 2 and 15 from pads at y 328). With no floor below, it stays where
the search started: weapons at pad - 98, crates at pad - 95, the case at pad - 96. The collision mesh confirms
each case, for example pad 73 at y 328 over a single floor at 276 put the case at 232. Every observed height
matches pad - 100 plus the object's own bottom offset. Fix options for CORE: set the flag only when the source pad
has it (records then vary in length, as in the ROM), or keep the layout and write a zero box for pads without one,
so `centre` is the pad, as in the original. Either needs no re-extraction. Check B-1113's room assignment and
anything else that reads pad boxes. Solo stages that load from scenario sources would be affected the same way.

Severity high for every mode with simulants on a multi-level arena (they waste the match fetching items they cannot
take, and an objective that lands there is never taken); possibly for players too, if a sunken item is hidden. Found
with the BOT.TEAM, BOT.CMD and BOT.GOAL lines (80bf07b5) in `mp_hold_the_briefcase_local_smoke` and
`mp_hold_the_briefcase_net_smoke` on base 7 (`results-20261116T094935Z.json`, `results-20261116T100549Z.json`).
It is not a networking difference. In both runs every simulant has `teamisonlyai`, takes the get-case command at
2.4 s and starts routes to items and to the case. Some items are never collected, although simulants reach them
(xz within 1 to 50 units, 135 to 148 units above). On Complex these are the weapons at (-3087,230,-717) room 2,
(-4888,230,-378) room 22 and (-3642,-321,1334) room 42, and the ammo crate at (-3064,-42,69) room 21; one simulant went
for the room 22 weapon five times in 150 s. Items at y 2 to 15 on the ground floor, and the raised crate at y 509,
are collected (`BOT.PICKUP`). The networked case landed at y 232 in all three networked runs (base 3 pad 173; base 7
pads 74 and 73) and was never taken. The local case landed at y 4 (pads 134 and 58) and was taken at 11 s and at
64 s. So the local/networked difference was where the case landed (their rng streams differ), not the network.
An item 42 to 46 units under the floor fails `botTestPropForPickup`'s line of sight (`cdTestLos06`, DOORS|BG) from
a simulant standing over it. The crate at y 509, 42 under its 551 floor, was collected only from the floor below,
where that line is clear. (My first guess, a wrong floor from `func0f06a650`, was wrong: that function has no
callers; the root cause is above.) Next, CORE's call: the fix, then a game check that the briefcase, weapons and
crates rest on their own floors (on Complex: rooms 2, 21, 22, 24 and 42).

## 2026-11-16 B-1203 Single-process smokes ran against the real user profile (T-NETWORKING-011, fixed in the runner)

Severity high (test isolation; it writes the user's real data). Found when three local smokes on 340eca64
(`mp_hacker_central_local_smoke`, `mp_capture_the_case_local_smoke`, `mp_combat_local_scorelimit_smoke`;
`results-20261116T080701Z.json` to `results-20261116T083802Z.json`) each crashed 0.03 s into boot: every one logged
`save dir: C:/Users/mikeh/AppData/Roaming/perfectdark` and died in `strlen` while loading `input-bindings.ini` from
there. Cause: `tools/smoke-verify/run.ps1` builds a single-process launch from `--smoke <test>`, the crash-handler
switch and the fixture's own `boot_args`, and adds `--savedir` only under `-ReuseInstallId`. B-1124 wrongly said this
path always passes `--savedir`, so its runner guard and `tools/tests/test_smoke_fixture_isolation.py` checked only
fixtures with a `processes` list. 81 single-process fixtures name neither `--portable` nor `--savedir` (the eight
`mp_*_local` smokes, `mp_room_flow`, the main-menu Joanna, swarm, stage-sky, vehicle and mission-intro smokes among
them), so every run of them on this machine, by any session, read and rewrote the real profile's `pd.ini`,
`input-bindings.ini` and `playerstats.json` (and could migrate an install's `pd.ini` into it, `fsInit`). The last
such run was CORE's `stage_sky_depth_defection` (week-pd/core 3b303305c, started 07:18:15Z, saved playerstats there
at 07:19:35Z); MULTIPLAYER's local smokes did the same all week. The crash itself is B-1184, fixed on dev by CRYSTAL
(6e43ca22) but not yet on this branch: actions 105-120 had no name in `s_ActionNames`, CORE's build (which has the
names) rewrote the real bindings file with all 123 rows per context, and this branch's loader read a NULL name for
the first row it did not know (`MenuSkipUp`). Fix on `week-pd/multiplayer`: once both strategy branches have built
the arguments, `run.ps1` adds `--portable` when the fixture's `boot_args` name neither flag (the game runs from its
install, so `--portable` keeps the install as base dir and moves the save dir and `$H` into it); `-ReuseInstallId`
still replaces the arguments with explicit directories. Pinned by `test_runner_isolates_single_process` in
`tools/tests/test_smoke_fixture_isolation.py`, which fails on the old runner. Nothing in the real profile was
restored or edited; that is Mike's call. Class: B-1124, the other launch path; SP-90.

## 2026-11-16 B-1202 In a networked match every simulant updates once per player each frame (T-NETWORKING-011, fixed in code)

Severity high for networked matches with simulants (they think, turn, check pickups and earn scenario points twice
as fast with two players, three times with three; offline is unaffected). Found by reading the Hold the Briefcase
scores: the networked 150 s run on 21eec732 (`results-20261115T121447Z.json`) gave a simulant 7 points in a 151 s
match, and a point needs 30 s held (`htbTickChr`, `TICKS(7200)`), so 5 is the most possible. Root cause: `lvRender`
loops over every player of a networked match (`if (g_NetMode) forcesingleplayer = false;`, "tick all players"), so
`propsTickPlayer` runs once per player each frame. Its first pass sets `PROPFLAG_NOTYETTICKED` on every prop, and
the original `botTick` ran its update only while that flag was set (`chrTick`, called later in `botTick`, clears it),
as `chrTick` still does for its full tick. 41431a3d ("FIX 1", 2026-04-04) changed `botTick` to read
`g_Vars.lvupdate240` alone, on the belief that `chrTick` cleared the flag before `botTick` ran (it does not: nothing
ticks a simulant's chr before `botTick`), so every later pass in the frame ran the whole update again: AI
(`botTickUnpaused`), turning, stuck checks, `scenarioTickChr` (Hold the Briefcase hold time, Hacker Central
download, Pop a Cap survival) and `botCheckPickups`. Fix: `updateable` again needs the flag as well as a nonzero
update; a later pass logs `BOT.TICK: simulant N passed again in one frame (players=P)` once per match. Pinned in
`tests/test_net_bot_pickup_static.cpp` (`[b1202]`). Pattern SP-87. Built 2026-11-16, the full suite passes (1993
cases). Seen in play 2026-11-16 (binary c5a12bca, `mp_simulant_types_net_smoke` PASS 107/107): the host logged `BOT.TICK: simulant 4 passed again in one frame (players=2)`, so the second per-player pass happens and now skips the update; the Hold the Briefcase re-run checks the effect on points. This changes how fast simulants act in networked matches, back to the offline speed.
Not covered (open, a question for Mike and CORE under SP-87): a prop in the background, in no player's view, still gets
N times the elapsed time, because `propsTickPlayer` adds the frame's time to every prop's `propupdate240` and every
slot's `slotupdate240` on each pass and advances `runstateindex` on each pass, and a background tick spends that
accumulated time (the original split-screen scheduler does the same). So a simulant out of every player's view still
acts at twice the speed with two players, and a co-op guard out of view on the host likewise. Points earned out of
view can therefore still beat the 30 s rule in the Hold the Briefcase re-run. **Fixed in code 2026-11-16 with option (b)** (CORE agreed on the hub, 05:59 UTC): in a networked match `propsTickPlayer` counts the frame's time once (`netrepeatpass`: on a pass after the first, no prop or slot gains time, the background slot does not advance and no background prop ticks), so every prop gets real time as offline; local split screen keeps the original per-pass accounting. Background pickups no longer regenerate N times as fast either. Pinned in `tests/test_net_bot_pickup_static.cpp` (`[sp87]`); pd-tests 2,019 cases green; not yet run in game.
**Checked in play 2026-11-16** on binary 490e8baf (8dafe7f0), `mp_hold_the_briefcase_net_smoke` 84/84
(`results-20261116T125201Z.json`): the simulant that took the case at 18.4 s scored at 48.4, 78.4, 108.3 and
138.3 s, one point per 30 s held as in the original (7 points in 151 s before the fix), and the client's points
and final result lines match the host's.

## 2026-11-16 B-1201 A co-op client crashes when the host leaves while it is in the Room after a mission (T-NETWORKING-011, fixed)

Severity high (client crash). Found by the first run of `network_coop_mission_return_smoke` on binary 0587e908
(5a1dfcda, 125/129, `results-20261116T042438Z.json`): Return to Room worked on both machines (`GAMELOOP.COOP: host chose
return to room`, the client's `host returned to room 1; leaving the mission results`, each Room opened), then the
host's scripted exit came first, the client read `NET: disconnected from server: Server is shutting down`, and 10 ms
later crashed: ACCESS_VIOLATION in `chrTickAttack` (chraction.c, `chr->act_attack.animcfg`), from `chraTick` <
`chrTick` < `propsTickPlayer` < `lvRender`. Root cause: since B-1197 a player in the Room after networked results is in
the lobby state while the ended mission stays loaded behind the Room, so `netDisconnectWithIntent`'s `wasingame` (the
game state only) was false and the client stayed in that stage, now offline. With `g_NetMode` at `NETMODE_NONE` the
client's suppression of co-op NPC AI in `chraTick` no longer applied and its global tick ran again, unpaused (the
results exit unpauses), so a guard the host had left in `ACT_ATTACK`, whose action data a client never sets up, ran its
attack tick. Same class as SP-84's "safe only by order" note. B-1198 had also removed the results push that paused the
stage on an in-game disconnect. Fix: the disconnect also leaves for CI when an ended gameplay stage (not CI) is still
loaded (`leftendedstage`, from `g_MainIsEndscreen`; log `NET: disconnect leaves the ended stage ... behind the Room for
CI`), and whenever it leaves a stage it first freezes it (`lvSetPaused(true)`, the update zeroed, every prop's catch-up
time dropped) until CI loads. Pinned in `tests/test_net_mission_end_static.cpp` (`[b1201]`). Built 2026-11-16, the
full suite passes (1992 cases). **Fixed in play 2026-11-16** (binary c5a12bca = c3d737c3,
`network_coop_mission_return_smoke` PASS 129/129, `results-20261116T054147Z.json`): after the host's Return to Room
and its exit, the client logged `NET: disconnect leaves the ended stage 0x30 behind the Room for CI` 20 ms after
`Server is shutting down`, and did not crash.

## 2026-11-16 B-1200 A second networked Combat match in the same room is refused (T-NETWORKING-011, fixed in code)

Severity high (no rematch in a room after a match with a random spawn weapon, the default). Found by
`mp_combat_net_rematch_smoke` on binary 30973012 (f8d79928, 79/94, `results-20261116T020226Z.json`), the first run to
get past B-1195 and B-1197: the host left its results with Return to Room, stayed in the arena behind the Room
(`MENU.MPENDSCREEN: networked results closed; no local stage change`, `BOT: match chr table retired`), re-armed and
fired its second match, and the lobby start refused it: `PLAYER.INIT.ROLLBACK lobby-write detail=random/Fiesta spawn
mode retains a typed ID`, `BOOT: --host-autostart started match 2 of 2 rc=-5`. Root cause: a random spawn weapon is
rolled per match (`netLobbyStartResolveRandomSpawn`) and the start commit copies the resolved config, roll included,
into `g_MatchConfig` (`netLobbyStartCommit`; a client stores it too from `SVC_STAGE_START`), which the match reads at
every spawn; the next `netLobbyStartWritePrepare` refuses a random mode that still holds a typed ID. Fix: the match
end forgets the roll for random and Fiesta modes (`netMatchConfigForgetSpawnRoll`, from the host's stage end commit
and a client's stage end read, which may host next; log `NET: match end forgets the rolled spawn weapon ...`); the
refusal stays for a genuinely mixed configuration. Pinned in `tests/test_host_autostart_rematch_static.cpp`
(`[b1200]`); the rematch smoke requires the log line. Not yet built or run.

## 2026-11-16 B-1199 The mission-end Agent Profile save is refused in networked co-op and counter-op (T-NETWORKING-011, fixed in code)

Severity medium (mission progress of a networked co-op or counter-op mission may not be saved; no crash). Every
networked mission end on the host logs `ERROR: SAVE: Agent Profile serialization failed name='Dark'; destination
preserved` right after `endscreenPushCoop`/`endscreenPushAnti` (co-op run `results-20261116T011832Z.json` and
counter-op run `results-20261116T014445Z.json`; once on the counter-op client too, in its disconnect teardown,
B-1198). `saveWriteAgentDocument` (savefile.c) calls `agentProfileWriteJson`, whose `profileDocumentValidate`
refuses the document (a scalar: auto difficulty, auto stage index, thumbnail, sound mode, control modes; a challenge
value; or the preferences) and keeps its reason to itself. Diagnostic added (not yet run): the failure path logs
`SAVE.DIAG: agent document ...` with those scalars and the public preferences validator's verdict. Not checked: whether
a solo mission end fails the same way. The codec is CORE's; report the diagnostic's first result to CORE.
Named 2026-11-16 by the diagnostic (co-op run `results-20261116T031858Z.json`): `SAVE.DIAG: agent document
autodifficulty=0 autostageindex=0 thumbnail=0 soundmode=1 controlmode=8,9 preferences=ok`; the validator allows control
modes up to 8. Root cause (ours, not the codec's): `saveCaptureAgentDocument` takes player 2's control mode from slot 1
in co-op and counter-op, but in a networked mission slot 1 is the remote partner, whose control mode is
`CONTROLMODE_NA` (9, `netPlayersAllocate`). So every networked mission-end save was refused and the progress was not
saved. Fix in code: `saveAgentPlayer2Slot()` (savefile.c) uses slot 1 only in a local mission and slot 5 otherwise, as
outside co-op, for both the save and the load. Pinned in `tests/test_net_mission_end_static.cpp` (`[b1199]`). Not yet
built or run.

## 2026-11-16 B-1198 A co-op or counter-op client disconnected mid-mission pushes results for both players (T-NETWORKING-011, fixed in code)

Severity low (a results screen for both players flashes up while the client drops to CI; the agent save attempt in it
fails, B-1199). Seen in `network_counterop_mission_end_smoke` (`results-20261116T014445Z.json`): the host's scripted
exit came first, the client read `NET: disconnected from server: Server is shutting down`, and `netDisconnect` ended
the stage (`mainEndStage`), which pushed `endscreenPushAnti` for player 0 and player 1: by then `g_NetMode` is no
longer networked (`g_NetMode = NETMODE_NONE` comes first in `netDisconnectWithIntent`), so `mainEndStage`'s skip of
remote players (B-1193) no longer applies. Fix in code: `netDisconnect` ends the stage with
`mainEndStageWithoutResults()` (pdmain.c), which pushes no co-op, counter-op or solo results on the way to CI and logs
`GAMELOOP: stage ended by a disconnect; no mission results`; a networked Combat match still ends through `mpEndMatch`.
The counter-op smoke checks one push per machine at the mission end and exactly two in all. Pinned in
`tests/test_net_mission_end_static.cpp` (`[b1198]`). Not yet built or run.

## 2026-11-16 B-1197 Closing networked Combat results loaded CI on the listen host (T-NETWORKING-011, fixed in code)

Severity high (the host's next match never starts after it leaves its results). Found by `mp_combat_net_rematch_smoke`
on binary 9078eb6b (3cad23d4, 77/91, `results-20261116T010715Z.json`), right after B-1195 let the host's global tick
run under the results: Continue (`GAMELOOP.MP: endscreen EXIT_TO_ROOM`) popped the results menus, and the next
`menuTick` ran the offline close of `MENUROOT_MPENDSCREEN` (`GAMELOOP.CAMPAIGN: MPENDSCREEN → CITRAINING lobby
return`): it unpaused MP, cleared `mplayerisrunning`, and loaded CI as a networked host, whose player setup rolled
back to the title (`PLAYER.INIT.ROLLBACK phase=stage stage=0x26; returning to title`), so the host never re-armed its
second match. The same close never ran before because the host's tick was deferred (B-1195) and a client's too
(B-1190 part 2). B-1195's simulant gate held through it (`BOT: match chr table retired` once, no crash). Fix: that
close does nothing while `g_NetMode` is not `NETMODE_NONE` and logs `MENU.MPENDSCREEN: networked results closed; no
local stage change`; the networked exits decide what follows (Return to Room keeps the stage until the host's next
match, the host's mission choices start it, Disconnect ends the session first, after which the offline close runs as
before). `menuReset()` clears the close state at every stage load, so the co-op path, whose tick is deferred after its
exit, never hits it later. Pinned in `tests/test_host_autostart_rematch_static.cpp` (`[b1197]`); the rematch smoke
forbids the CI return and `PLAYER.INIT.ROLLBACK` and requires the new line. Not yet run.

## 2026-11-15 B-1196 A networked co-op client is sent moves for a chr it does not have (T-NETWORKING-011, fixed in code)

Severity medium until the chr is named (the client cannot see or place that chr; if it is an enemy, the client is
shot by someone it cannot see). Seen in `network_coop_mission_end_smoke` on binary 8c56ad72 (119/121; both failures
are this): the client logged `NET: prop with syncid 335 does not exist` 720 times, every 55 ms on average (the
20 Hz `SVC_NPC_MOVE`), from 1.5 s after Defection's gameplay start until the mission end. Both sides agree on the
initial ids (`last initial syncid=331 first dynamic syncid=332`), so 335 is the host's fourth prop allocated after
the stage start: on the host every such prop takes `g_NetNextSyncId++` (`propAllocate`), on a client it stays 0
unless a spawn message names it, and `SVC_PROP_SPAWN` carries objects only. `SVC_NPC_MOVE` goes to every chr
`netNpcIsReplicationReady` accepts (`PROPTYPE_CHR`), so 335 is a chr. Not a scripted spawn: Defection's AI lists
have no `spawn_chr_at_pad`/`spawn_chr_at_chr`, and the co-op buddy spawn (`player.c`) needs `normmplayerisrunning`
0, which networked co-op is not. The initial NPC snapshots matched (36 and 52 NPCs), and no periodic check compares
the count, so nothing else notices. Diagnostics added (not yet run): the host logs each chr with a dynamic id once
when it first sends its move (`NET.NPC: host replicates a chr created after the stage start ...`), and the client
logs each unknown id once with its nearest local chr (`NET.NPC: client has no chr for syncid=...`). That says
which chr it is and whether the client holds an unlinked copy (deterministic creation on both sides) or none
(host-only creation, which needs a spawn message).
2026-11-16: the re-run on binary 9078eb6b passed 121/121 with no `NET.NPC` line on either side, so the host made no
chr after the stage start that time: the spawn is conditional. Likely source (by reading, pattern SP-85): the shared
AI lists (`gailists.c`) have an unalerted guard "create a spawn", a clone of itself searching for the player
(`try_spawn_clone2(CHR_SELF, GAILIST_SEARCH_FOR_PLAYER, ...)`, opcode 0x00ca, `aiDuplicateChr`, only for a
`CHRCFLAG_CLONEABLE` guard); in the failing run a guard shot the host as gameplay began. Co-op AI runs only on the
host, and no message names a chr the host's AI makes, so a co-op client never sees a clone (or a `spawn_chr_at_*`
guard) while the host's copy can shoot it. Fix needed: a chr spawn message (the host announces a chr with a dynamic
id, with its held weapons and hat, before its moves; the client makes a puppet with the same ids), plus a host
debug trigger so a smoke can prove it.
Fixed in code 2026-11-16 (not yet built or run): new `SVC_NPC_SPAWN` 0x7e (unreleased v62, co-op and counter-op
only). Each net frame the host announces, once and reliably, every replication-ready chr whose sync id is dynamic
(`netServerNpcSpawnAnnounce`, net.c), with its chr number, body, head, place, angle, rooms, team, flags, held
weapons (sync id, weapon, model, left hand, dual) and hat; its moves and states wait 15 ticks after that, since
ENet does not order the reliable spawn against an unreliable move. The client makes the chr at the host's place
without a second spot search (`chrSpawnReplicated`, chraction.c, idle list; its AI stays suppressed there), binds
the host's ids to the chr, weapons and hat, and sets the chr number, team and flags; a spawn for an id it already
has is ignored. Logs: `NET.NPC: host announced a chr made after the stage start ...`, `NET.NPC: client made the
host's chr ...`. `--debug-clone-guard <seconds>` (port/src/main.c) clones a living guard once on the host, idle so
it cannot end a smoke's mission, with the guard's weapons, team and flags; `network_coop_mission_end_smoke` uses it
at 20 s and requires both NET.NPC lines and no `client has no chr`. Pinned in
`tests/test_net_mission_end_static.cpp` (`[b1196]`). Open: a co-op client that reconnects does not get chrs made
before it rejoined (T-NETWORKING-007); a chr the host makes before the stage's replication release is counted in the
host's NPC snapshot (`SVC_NPC_RESYNC`) before its spawn is announced (the broadcast starts at the first active
frame), so that snapshot would still be rejected, as it was before this fix.

## 2026-11-15 B-1195 The listen host crashes when it leaves Combat results for the Room (T-NETWORKING-011, fixed in code)

Severity high (the host crashes in every networked Combat match with simulants once it presses Continue on its
results). Seen in `mp_combat_net_rematch_smoke` on binary 8c56ad72 (8a34e695): host `GAMELOOP.MP: endscreen
EXIT_TO_ROOM`, the Room opens, then `FATAL: EXCEPTION ... CODE=0xc0000094` (integer divide by zero) in
`botChooseGeneralTarget` (`bot.c`, `% g_MpNumChrs`), called from `lvRender` > `propsTickPlayer` > `botTick` >
`botTickUnpaused`. Root cause: the host's stage end commit (`netmsgSvcStageEndCommit`, MP branch) unlinked every room
member's player, the host's own too, so from the results on `playermgrRestoreLocalPlayerContext` failed (`GLOBAL.TICK:
deferred` right after the results opened) and the host's `lvTick` never ran. `lvSetPaused(true)` from the results push
never zeroed the update, and `lvRender`'s `propsTickPlayer` kept ticking the simulants with the last live delta, AI
included. Continue then ran `mpClearRuntimeChrState()` (B-368), which sets `g_MpNumChrs` to 0 while the ended arena
stays loaded behind the Room, and the next AI tick divided by it. Same class as B-1190 part 2 on the client (SP-84).
Fix: (1) the commit skips the listen host's own netclient, as the client's stage end read does; the next stage's
player allocation replaces the link (`netPlayersAllocate`). With its global tick running, the host's results pause
holds, and after Continue the game-over pause keeps the arena's update at 0 (once B-1197 stopped the offline results
close from unpausing it and loading CI). (2) `botTick` treats a simulant whose
match chr table is retired (`g_MpNumChrs <= 0`; a live match always counts its simulants) as scenery: no AI, spawning,
room recovery or stuck repair, speeds zeroed, `chrTick` kept for the model, and one log line `BOT: match chr table
retired; simulants are inert until the next stage`. (2) is needed besides (1) because `mpApplyConfig` (presets,
challenges) replaces `g_MpSetup` whole, which would lift the game-over pause in the Room. Tests: `[b1195]` in
`tests/test_host_autostart_rematch_static.cpp`; the rematch smoke requires the inert line. Run on binary 9078eb6b
(`results-20261116T010715Z.json`): no crash, the inert line logged once after Continue; the run stopped on B-1197.

## 2026-11-15 B-1194 A client skips some simulant pickups: the item is not in its world (T-NETWORKING-011, fixed)

Severity medium (a simulant takes a weapon on the host that stays on the ground on the client; no crash). Seen in
`mp_simulant_types_net_smoke` on binary 91a54a5d (`results-20261115T215645Z.json`, 106/107, the only failure):
`BOT.PICKUP: client skipped item 26 for simulant 14, it is not in this world`, and the same for items 23, 25, 29 and
30, while the host sent all of them (`BOT.PICKUP: host sent simulant 14 item 26 weapon=5 result=1`) and the client
replayed later ones (items 34 to 43). No `BOT.DROP` lines, so these are not dropped weapons. The skipped ids are the
lowest, which suggests the arena's starting weapon pickups have no counterpart with the same sync id on the client.
The 3-simulant runs never took one. Each skipped item was the host's first pickup of it (no earlier pickup on
either side), so it is not respawn-timer drift: the reader's refusal (`netmsgSvcBotPickupRead`: no prop for the sync
id, not an object or weapon, `timetoregen != 0`, or deleting) holds from the start. Diagnostic added 2026-11-16
(not yet run): the host's `BOT.PICKUP: host sent` line now carries the item's object type and position, and the
client's skip line names the test that refused and what it holds under the id (type, object type, respawn timer,
position, and the first dynamic id). Initial ids are prop slot index + 1 on each side (`netSyncIdsAllocate`), so a
"no prop has this sync id" with a low id means the client's setup left that slot free.
Named 2026-11-16 (`mp_simulant_types_net_smoke` on binary 9078eb6b, 106/107, `results-20261116T013126Z.json`):
both sides log `last initial syncid=17 first dynamic syncid=18`, so every skipped item (23 to 58, 14 of them) is a
dynamic prop; the client has no prop under any of those ids, and on the host every one is weapon 8 (`objtype=8`,
a weapon) that no `BOT.DROP: host announced` line covers (those four, 33, 34, 40 and 53, were replayed fine).
Root cause: a simulant's held weapon is a prop made when it equips it, after the stage start, so it has a dynamic
id on the host and none on a client. `chrBeginDeath` drops the held weapons (`objSetDropped`, then `chrTick`'s
`objDropRecursively`) on both machines: the host's copy lies in the world under an id the client never learns, and
the client's puppet drops an unnumbered copy, so a later host pickup cannot reach it. Fix in code (pattern SP-85):
the host announces each held weapon with a dynamic id once the death drop puts it in the world
(`netmsgSvcPropSpawnWrite` + `netmsgSvcPropMoveWrite`, as `botinvDrop` does; log `NET.DROP: host announced chr ...
held weapon=... syncid=...`), and a client's puppet retires its own held models at death instead of dropping them
(`OBJHFLAG_DELETING`, as a weapon switch does). Pinned in `tests/test_net_bot_pickup_static.cpp` (`[b1194]`). Not
yet built or run.
2026-11-16, run on binary 732dd2fe (540f12d8, `results-20261116T033141Z.json`, 106/107): that root cause was wrong.
The host logged no `NET.DROP: host announced chr` line at all, while 27 pickups were still skipped, every one the
match's rolled spawn weapon (weapon 5 this time, weapon 8 last time), all dynamic ids, the client holding no prop under
any of them. A simulant's lethal damage goes through `chrDie` (chraction.c), whose `botinvDropAll` makes fresh props
for its inventory and announces them (`BOT.DROP`, 3 this run, all replayed) and deletes the held models, so held
weapons never reach the world on that path. About 12 deaths, 24 simulant spawns and 32 host pickups, with the first
skipped items lying in the world six seconds into the match, so where those weapons come from is still open. The
held-weapon announcement and the puppet's retire stay (they cover `chrBeginDeath` and `botDisarm` drops of weapons with
dynamic ids, as a co-op clone's are), but they are not this bug's fix. Diagnostic for the next run: the host's
`BOT.PICKUP: host sent` line now carries the item's `dynamic`, object `flags` (a drop carries AICANNOTUSE), `hidden2`
(the regen flag) and setup `pad`. Open.
Named 2026-11-16 (binary 0587e908, 5a1dfcda, `results-20261116T044504Z.json`, 106/107): every skipped item but one
carries `flags=0x00004000` (`OBJFLAG_ASSIGNEDTOCHR` alone) and a `pad` of 3 to 13, which are the simulants' chr
numbers this run (`SIMULANT: allocating chrnum=0..13`); the replayed `BOT.DROP` props carry `0x20004000`. That is a
simulant's own held model (`weaponCreateForChr` stamps the owner's chrnum in `pad`), and the one path that puts it in
the world is `botDisarm` (bot.c), run when a simulant takes a hit with `FUNCFLAG_DISARM`: it clears
`weapons_held[HAND_RIGHT]` and drops that model itself, or, with no held model, makes a fresh one (`AICANNOTUSE`,
the one outlier, item 36 with `0x20004000` and pad 0). The death-drop announcement only looked at `weapons_held`,
which `botDisarm` had already cleared, so it never fired. Worse, `SVC_CHR_DAMAGE` runs `chrDamage` on the client, so
the client's puppet ran the same disarm and dropped an unnumbered copy of its own. Fix (pattern SP-85): the chr drop
block in `chrTick` notes every weapon with a dynamic sync id the chr carries before `objDropRecursively` and announces
each one the drop leaves in the world (`netmsgSvcPropSpawnWrite` + `netmsgSvcPropMoveWrite`; log `NET.DROP: host
announced chr ... dropped weapon=... syncid=... flags=...`), so any drop path is covered, not only the held slots; and
on a networked client `botDisarm` retires the puppet's held model and drops or makes nothing. Pinned in
`tests/test_net_bot_pickup_static.cpp` (`[b1194]`, two cases). Built 2026-11-16, the full suite passes (1992 cases).
**Fixed in play 2026-11-16** (binary c5a12bca = c3d737c3, `mp_simulant_types_net_smoke` PASS 107/107,
`results-20261116T055414Z.json`): no client skip at all (24 host pickups sent, 24 replayed; 23 were skipped on
0587e908), and the host logged 16 `NET.DROP: host announced chr N dropped weapon=10 ... flags=0x00004000` lines, the
disarmed simulants' own guns, under low dynamic ids such as 21 and 24 like the ones skipped before.

## 2026-11-15 B-1193 The networked co-op and counter-op mission end is local on each machine (T-NETWORKING-011, Mike: host control)

Severity high for networked co-op and counter-op (the next mission or a retry drops the other player; no mission
end has ever run in a smoke). Found by a read-only map of the code on 760ba423 (not run; no fixture ends a mission).
What happens today: (1) each machine pushes a results dialog for both players (`pdmain.c` mainEndStage loops over
`PLAYERCOUNT()`; `endscreenPushCoop`/`Anti` check nothing), so each shows the other machine's player too, and
counter-op draws `renderSoloEndscreen` twice a frame; (2) the host's stage end commit leaves the partner in
`CLSTATE_GAME` (`netmsgSvcStageEndCommit`, co-op branch), so it is not in the next lobby start's roster and
`netServerMatchInProgress()` stays true (new connections refused); (3) Next Mission, Retry and Main Menu act only on
the machine where they are pressed (`pdgui_bridge.c`): a host's Next or Retry reloads alone, its player allocation
finds one of two players and the rollback disconnects the client; a client's reloads alone while connected;
(4) networked co-op runs with `normmplayerisrunning` 1 (`mpReset`), so the death branch (`player.c`, both players
dead in co-op, Bond dead in counter-op) and the pause menu's End Match end the stage on a client too, without the
host; (5) the host's Room draws over the co-op results, as in B-1192, because the 2P results dialogs are not
registered in the menu pool; (6) inferred: `netServerStageEnd` wipes the outgoing buffers, which can drop an
`SVC_OBJ_STATUS` queued in the same frame, and a client decides completed or failed from its cached statuses.
Note: `src/lib/main.c` is not compiled; the live `mainEndStage` is in `port/src/pdmain.c`. Fix (Mike, 2026-11-15,
host control), in code: (1) `mainEndStage` pushes results only for this machine's own player (`isremote`, still set
there; the participant type does not work, the co-op stage load makes both local); the four 2P results dialogs are
registered as `MENU_TYPE_ENDSCREEN_SOLO`, so the lobby gate (B-1192) and the rematch re-arm also wait on them;
(2) the co-op/anti stage end commit returns the partner to `CLSTATE_LOBBY` (linkages kept for the results), and
`g_NetLastCounterOpClientId` keeps the counter-op client past the end; (3) the host's results screen offers Next
Mission (before Skedar Ruins), Retry Mission and Return to Room (`pdguiEndscreenNetMission*` in `pdgui_bridge.c`):
Next and Retry go through the Room's lobby start, which takes the other player along; Return to Room sends the new
`SVC_MISSION_RETURN` 0x7c (unreleased v62) and the client leaves its results for the Room; the other player sees
"Waiting for the host to choose what comes next" and Disconnect; (4) the both-dead and Bond-dead ends and the pause
menu's End Game no longer end the mission on a client (End Game there leaves the session); (5) the final objective
statuses go out ahead of `SVC_STAGE_END` (`netServerSendFinalObjectives`); (6) Mike, 2026-11-15, "in co-op don't
you want to see stats from all players?": each machine's results show its own player's combat stats and then a
PARTNER block with the other player's, read by the per-machine `isremote` (`pdguiEndscreenNetMissionStats`, not
the current player, which on a networked machine can be whichever player was ticked last), and the host sends every
player's final kills and shots by region with the final objective statuses (`SVC_MISSION_STATS` 0x7d, unreleased
v62; the host decides hits and kills on the mission's characters, so a client's own count can be off). Pinned in
`tests/test_net_mission_end_static.cpp` (`[b1193]`). Game check: `network_coop_mission_end_smoke` (new): the host ends co-op Defection at 40 s with
`--debug-end-mission 40:complete` (the level script's end, host or offline only), both machines wait for their
own results (`endscreen_visible` now also sees the solo results type), the host presses its default Next
Mission and the client, pressing nothing, must be taken into the next mission. First run, binary 91a54a5d
(`results-20261115T212355Z.json`, 111/119): proven in play: the end at 40 s, the six final objective statuses on
the client just before `SVC_STAGE_END` (all complete), `NET: mission end returns client=1 to the lobby`, one
results push per machine (host Bond, client its own player) and both results screens up. Not reached: the host's
Use press did nothing (no `MENU.GRAPH.FIRE`), so the next mission never started. The solo results screen never took
window focus and had no default button, and its content child comes before the action bar, so keyboard and
controller navigation had no button to activate. Fix: `SetWindowFocus` when it appears, as the multiplayer
results do, and `SetItemDefaultFocus` on the button the comments already call the default (Next Mission, else
Retry; offline too); the client's Disconnect is not a default. Two assertions also named the test instead of the
per-process scenarios. Re-run after the current turn.

## 2026-11-15 B-1191 Simulants were invisible on networked clients (T-NETWORKING-011, finding 3)

Severity high (every networked match with simulants: the clients' players could not see them; no
crash). Proven by the reworked `mp_combat_net_bot_render_smoke` (`results-20261115T154128Z.json`, 64/66): the
client turned its player straight at the simulant 120 units away (`look_dot=1.000`, a spot the host had checked
for line of sight), yet the simulant's prop never got an on-screen flag (`prop_flags=0xc` for 20 s), the render
audit logged `propsSort-scan` but never `propsSort-included` or `chrRender-modelRender`, and its animation frame
stayed at 4.00. Cause: on a non-authority client `botTick` (`src/game/bot.c`) returned `TICKOP_NONE` before
`chrTick`, and `chrTick` is what advances a character's animation, places its model and marks it on screen
(`chr.c`: `PROPFLAG_ONTHISSCREENTHISTICK`). `bot.c` already recorded the same symptom for frozen bots (B-217 v2).
Fix, after the co-op NPC pattern: the client runs `chrTick` for the simulant as a puppet tick
(`s_BotPuppetTick`, read through `botIsPuppetTick()`), and `chraTick` (`chraction.c`) skips the AI and the
action dispatch for it, as it already does for co-op NPCs on a client; the host stays authoritative for
position, angle and action type (`SVC_CHR_MOVE`). Known limit: the client does not run the simulant's actions,
so the simulant plays the animation it has (its stand animation) while it moves; running and attacking
animations would need the host's animation state on the wire. Pinned in `tests/test_mp_chrnum_static.cpp`
(`[finding3]`). Game check: the render smoke, re-queued. The client view in that run is also black outside one
doorway (room 33 of Felicity drawn black); recorded separately, not yet explained.

Part 2, 2026-11-15. The render re-run on that fix (`results-20261115T175033Z.json`, 64/66) showed the puppet ticked
(`prop_flags=0x4`) but never on screen: culling saw the simulant at y -2552 while the host had it at -534 (ground
-693, room 37), one sample in six read y -30000 (the no-floor value), and the model had no animation
(`anim=0 frame=-1.00`). Two causes. First, `netmsgSvcChrMoveRead` wrote `prop->pos` (and copied the rooms) only,
while a character's position is rebuilt every tick from its model root plus `manground` (`chr.c`, the root
position callback); on a client those stayed stale, so the puppet tick moved the simulant back to the old root and
let it fall, and at `manground <= -30000` that code can even call `chrDie` on the client. The rematch run shows
the same: the client's `SVC_CHR_SYNC` checksum never changed (32 `chr desync detected`, local value constant).
Second, the puppet path skipped `botApplyMovement`, which the host calls before `chrTick` to choose the
third-person animation (run, strafe, stand, die) from the speeds `SVC_CHR_MOVE` already carries. Fix: the move
reader applies the host's position through `chrSetPos` (ground, rooms with registration, model root, force to
ground), as remote players' moves are applied, and keeps the host's look angle; the puppet tick calls
`botApplyMovement` before `chrTick`; the root position callback no longer kills a puppet for a fall only the client
saw (the host sends deaths). Pinned in `tests/test_mp_chrnum_static.cpp` (`[b1191]`). Still not on the wire: the
aiming pose toward a target and which death animation plays (the client picks one). Propagation: the co-op NPC
reader `netmsgSvcNpcMoveRead` makes the same bare `prop->pos` write; the co-op run verified the NPC snapshot
(`SVC_NPC_SYNC snapshot verified ... count=36`, idle NPCs), but walking NPCs on a client are unproven, left to the
co-op step. Propagation done in code 2026-11-16: `netmsgSvcNpcMoveRead` places the NPC through `chrSetPos` (the
current rooms kept when a move has none), and the root position callback no longer kills a co-op or counter-op NPC
on a client for a fall only the client saw (the host decides; `chraTick` already suppresses that NPC's AI there).
Pinned in `tests/test_mp_chrnum_static.cpp` (`[coop][b1191]`). Walking NPCs on a client are still not proven in
play: no smoke checks an NPC's place on the client. Game check: `mp_combat_net_bot_render_smoke` PASS 66/66 on binary 91a54a5d
(`results-20261115T214359Z.json`): the client's simulant is on screen (`prop_flags=0xc4`, `propsSort-included`,
`chrRender-modelRender`), animated (animation 89, frames advancing) and at its height (y -604). Fixed in play.
Observation: the client still logs some `chr desync detected` (14 in that run, 2 resyncs, against 32 before): the
`SVC_CHR_SYNC` checksum compares exact positions while a client extrapolates puppets between moves; not acted on.

## 2026-11-15 B-1192 A listen host's Room drew over its own results screen and took focus (T-NETWORKING-011, finding 4)

Severity high for networked matches (the host could not leave its results screen with Use or A, so no rematch;
no crash). Seen in `mp_combat_net_rematch_smoke` (`results-20261115T174226Z.json`, 71/85, binary 86c1025f) after
the B-1139 fix: the host had one results dialog, and the harness's Use press reached it (`ENDSCREEN.DIAG: input
action_bar_visible=1 accept_pressed=1 ... suppressed=0`), but Return to Room never fired. At stage end the host is
back in the lobby state with `s_InRoom` still set (B-1042 keeps it so the Room returns after a match), so
`pdguiLobbyRender` drew the Room in the same frame as the results screen (`MENU_IMGUI: room OPEN` straight after
`MENUPOOL: acquired endscreen_mp`). The Room's first frame calls `SetWindowFocus`, so keyboard and controller
navigation had no item on the results screen. The offline results screen has no Room beside it, and the same press
works there (`combat_sim_ui_release_cycle`). Same cause as B-1190 on the client. Fix: `pdguiLobbyRender`
(`port/fast3d/pdgui_lobby.cpp`) draws neither the Room nor the social lobby while
`menupoolIsActive(MENU_TYPE_ENDSCREEN_MP)`; Return to Room (`endscreenGraphMpContinue` →
`pdguiEndscreenExitToRoom`) releases the menu pool and the Room opens on the next frame. The host autostart re-arm
already waits on the same query. Pinned in `tests/test_host_autostart_rematch_static.cpp` (`[b1192]`). Game check:
the rematch smoke, queued (q_20261115173307314_394b483c). Co-op and counter-op use the solo results screen; left to
the co-op end-flow decision.

Part 2, 2026-11-15. The rematch on binary 91a54a5d (`results-20261115T213538Z.json`, 72/90) kept the Room back as
intended (no `room OPEN` on the host after the end), and Use reached the results screen again (`accept_pressed=1
suppressed=0`), yet Return to Room still never fired; the co-op mission end smoke showed the same on the solo
results screen. So the Room was not the whole cause. The input debounce disables the action bar on the frame the
results window appears, which is when ImGui picks its first focus item, and ImGui skips disabled items there, so
no button had focus and Use pressed nothing. Fix: `SetItemDefaultFocus` on the primary button of both results
screens (Return to Room or Play Again; Next Mission or Retry), which names the default even while it is disabled,
and the solo screen now takes focus when it appears, as the multiplayer one does. Game check: the rematch and the
co-op mission end smokes, next turn.

## 2026-11-15 B-1190 Networked clients never see the match results (T-NETWORKING-011, decided: option A)

Decided by Mike, 2026-11-15: option A. Everyone sees the results and presses Continue to go back to the Room, and
while a player is still on the results, the others see that player in the lobby as "reviewing match stats". The
host's next match start takes any player still on the results along (part of option A as proposed). Done the same
day, in three parts. (1) A client never pushed its own end screen: `mpParticipantsDecodeActiveMask` records every
player of a client's match as remote, and the B-1139 skip in `menutick.c` (remote participants get no dialog) then
skipped all of them. The client's stage start now marks its own player local (a client owns runtime slot zero),
so participant types mean the same on host and client. The player's `isremote` was no alternative: the host's
stage end commit clears it before the push. (2) `netmsgSvcStageEndRead` holds the lobby UI back
(`pdguiLobbyHoldForResults`) until the results screen is open; then the B-1192 gate holds it; a 300-frame fallback
logs `LOBBY: results screen did not open within` and shows the lobby. menuTick also logs `MENU.MATCHEND: end screen
waits for an open dialog` once if a dialog delays the push. (3) "Reviewing match stats": the host sets
`netclient.reviewing_results` for everyone in the room at stage end and clears it on that player's Continue
(`CLC_LOBBY_RESYNC`, or the listen host's local resync) or when the next match takes the player along
(`LOBBY.REVIEW:` lines); `lobbyUpdate` copies it into the roster in the lobby state only, `SVC_LOBBY_ROSTER` rows end
with a flags byte (bit 0, unreleased v62, no bump), and the Room and the social lobby show "reviewing match stats".
Part 2, same day: the rematch on binary 91a54a5d (`results-20261115T213538Z.json`) showed the client still without
results while connected: its fallback fired (`LOBBY: results screen did not open within 300 frames`) and its end
screen was pushed only when the host shut down. Cause: `netmsgSvcStageEndRead`'s loop that unlinks the room's peers
also unlinked the client itself, and without `g_NetLocalClient->player` there is no local player
(`playermgrGetLocalPlayerNum`), so `mainTick` deferred the whole `lvTick` (`GLOBAL.TICK: deferred`, B-1090) and
menuTick never pushed the results. The co-op path does not unlink, which is why co-op results appeared at once.
Fix: the loop skips the local client. (The host also unlinks itself at its commit, but its push runs in the same
frame before that.)
Pinned in `tests/test_host_autostart_rematch_static.cpp` (`[b1190]`) and `tests/test_net_lobby_roster.cpp`. Game
check: the rematch smoke (its client stays on its results, so the second match has to take it along), queued with
binary eab25274. Original entry follows.

Severity medium (every networked mode: the client's player gets no results screen; no crash). Seen in
two runs: `mp_king_of_the_hill_net_smoke` (`results-20261115T132151Z.json`) and
`mp_combat_net_rematch_smoke` (`results-20261115T150756Z.json`). At stage end the client logs `MATCH: endscreen
shown` (that is `mpEndMatch`, not the screen) and in the same frame opens its Room (`MENU_IMGUI: room OPEN`); its
end screen dialogs appear only minutes later, when the host shuts down and the Room closes. Cause:
`netmsgSvcStageEndRead` (`port/src/net/netmsg.c`), on the multiplayer path, sets the local client to
`CLSTATE_LOBBY` before `mainEndStage`, so the lobby UI opens the Room before `menutick.c`'s "match is ending"
branch can push the end screen. The end screen's networked Continue (`endscreenGraphMpContinue`: exit to room,
lobby resync) shows the client was meant to pass through it. Earlier status-table entries that read "end screen
PASS" for networked modes were true for the host only. Not fixed: whether clients should see the results screen
(and how a client still on it joins the host's next match) changes how the end of a networked match plays, so it
is Mike's call. Option A: every player sees the results and presses Continue to go back to the Room, as in the
original and as the Continue code expects; the host's next match start takes any client still on its results
screen along. Option B: keep the straight return and show the last match's results in the Room.

## 2026-11-15 B-1139 A listen host could not leave its multiplayer end screen with Use (T-NETWORKING-011, finding 4)

Severity high for networked matches (the host is stuck on the end screen with keyboard or controller;
no rematch; no crash). Seen in `mp_combat_net_rematch_smoke` (`results-20261115T150756Z.json`, 70/84 after
the B-1135 fixture fix): the first match ended on time, the client went straight back to its Room
(the networked client never shows an end screen), but the host stayed on its end screen although
the harness pressed Use 4 s after it appeared; the same press leaves the offline end screen
(`combat_sim_ui_release_cycle`). The host's end screen had two dialogs (`count=2`, the render
alternating mpindex 0 and 1): `menutick.c`'s "match is ending" branch pushes a dialog for every active
player participant, which in the original were all local players, and on a listen host the remote
client's player is a participant too. Fix: that branch skips remote participants for the
multiplayer end screen (`mpGetParticipant(i)->type == PARTICIPANT_REMOTE`) and still counts them as
player numbers. Co-op and counter-op keep their branches as they are until Mike decides their end
flow. Pinned in `tests/test_host_autostart_rematch_static.cpp` (`[finding4]`). Game check: the rematch
smoke, re-queued.

## 2026-11-15 B-1138 Teams past the fourth spawned in team 1's region, and some teams got no respawn sector (T-NETWORKING-011, SP-82)

Severity medium (unfair spawns in team matches with five or more teams, or with team colours that
are not the first ones in order; no crash). Found by the SP-82 sweep after B-1136, from source. The
port allows `MAX_TEAMS` (8) teams, but both team spawn paths looked at the first 4 team bits only:
the PC's match-start orchestrator (`mpspawn_orchestrate.c`: `s_Anchors[4]`, the sector count, the
4-sector cap and `orch_team_bits_to_sector`, whose fallback put any later team in sector 0, team
1's) and the respawn helper `playerMpResolveTeamSpawnParams` (`player.c`), which left a team past
the fourth with no team at all. The respawn helper also passed the raw team number as the sector,
while `spawnPoolSelect*` (`spawnpool.c`) takes a sector in 0..num_teams-1, so with Red and Blue
active Blue (team 2 of 2) never got its sector when no teammate was placed yet. Fix: the
orchestrator has one sector per active team up to `MAX_TEAMS` (`ORCH_MAX_SECTORS`), and the respawn
helper counts all team bits and gives the team's place among the active teams, as the orchestrator
already did. Matches with the first two or four contiguous teams are unchanged. Pinned in
`tests/test_mp_chrnum_static.cpp` (`[sp82]`). Not run in game: no smoke has five or more teams yet.

## 2026-11-15 B-1137 A challenge started from the challenge screen had no player in it (T-NETWORKING-011, finding 10)

Severity high for challenges (the mode does not work from a fresh session; no crash). Seen in the
first run of `mp_challenge_local_smoke` (`results-20261115T133628Z.json`, 27/30): Challenge 1
started, ran to its time limit and showed its end screen, but `PARTICIPANTS: 1 active (0 players,
1 bots)` and the results listed only the simulant. `matchStartFromChallenge`
(`port/src/net/matchsetup.c`), used by both the ImGui challenge screen and `--launch-challenge`,
applies the challenge, adds its simulants (`challengePerformSanityChecks`, ea187a63) and calls
`mpStartMatch`, but the local player becomes a participant only in the Room's match start
(`matchStartCommit`), which this path skips. So the player spawned in the arena without being in
the match: no score, no result, and the simulant count was chosen for "one player" only by the
fallback in the sanity check. Fix: the launch clears the participant table and adds the local
player in slot 0 on team 0 (`challengeApply` sets every player's team to 0) before the simulants
are added. Pinned in `tests/test_mp_chrnum_static.cpp` (`[finding10]`). Game check: the challenge
smoke requires `PARTICIPANTS: 2 active (1 players, 1 bots)` and two result lines.

## 2026-11-15 B-1136 Simulants never scored their Pop a Cap or Hacker Central points (T-NETWORKING-011, SP-82)

Severity medium (wrong scores and rankings in two modes, local and networked; no crash). Seen in
the 150 s networked Pop a Cap smoke (`results-20261115T130939Z.json`): a simulant was the victim
for the whole match and the host sent two survival-point changes, yet every final score was 0 on
both sides. `func0f18d0e8` (`src/game/mplayer/mplayer.c`) turns an mpchrnum into its slot in
`g_MpAllChrConfigPtrs`. It split players from simulants at the N64's player count, written as a
literal 4 (`arg0 < 4`, `g_BotConfigsArray[arg0 - 4]`), while the port's mpchrnums put players at
0 to `MAX_PLAYERS - 1` (8) and simulants after them, as `MPCHR` and the inverse `func0f18d074`
already do. So simulant 0 (mpchrnum 8) looked up simulant 4's config: not found with fewer than
five simulants (-1, so no points), the wrong simulant with more. Effects: `pacCalculatePlayerScore`
dropped every simulant's victim kills and survival points and `htmCalculatePlayerScore` every
simulant's download points; players 5 to 8 (mpchrnums 4 to 7) took simulant 0 to 3's points
instead of their own; a JudgeSim's "attack the leader" could not see a simulant in the lead; and
Capture the Case's team bits (`ctcInitProps` after wrapping teams) went to the wrong character.
Fix: the boundary is `MAX_PLAYERS`, with a range check. Pinned in `tests/test_mp_chrnum_static.cpp`.
Game check: the Pop a Cap net smoke's victim should end with its survival points. Related, not
fixed: the PC's match-start orchestrator (`mpspawn_orchestrate.c`) has 4 spawn sectors, so teams 5
to 8 start in team 1's sector (`orch_team_bits_to_sector` falls back to 0); see SP-82.

## 2026-11-15 B-1135 Two smoke fixtures could never run: their timeout was shorter than their own waits (T-NETWORKING-011)

Severity low (test tooling; no game effect, but a wasted runtime turn each time). `smokeParseJson`
(`port/src/smoke_harness.c`) refuses a schedule whose latest event plus every `wait_until` timeout
(plus 1 s per controller attach or detach) is not strictly below the file's `timeout_seconds`. It
logs `SMOKE: timeout_ms=... must exceed schedule bound=...` and the run fails on every assertion.
The refusal happens inside the game, after the runner has built the install, so nothing caught it
earlier. Seen 2026-11-15: `mp_combat_net_rematch_smoke` (new this week; 640 s against a 730 s
bound) failed 44/84 in 259 s without starting a match. A sweep of all 180 schedules found one more:
`combat_sim_defect_cohort` (T-ENGINE-005; 180 s against 195 s), which explains why that gate had
never produced a result. Fix: the timeouts are now 760 s and 210 s; neither fixture's waits
changed. Class fix: `tests/test_smoke_schedule_bounds_static.cpp` reads every JSON file under
`tools/smoke-verify/tests` and `fixtures` with the harness's arithmetic and fails on any schedule
that does not fit; it also pins that arithmetic in `smoke_harness.c` so the two cannot drift. Checked
by putting the old 640 s back: the test failed with the harness's exact numbers.

## 2026-11-15 B-1134 Capture the Case teams started the match anywhere instead of at their base (T-NETWORKING-011, finding 8)

Severity medium (the mode starts wrong; respawns were right). The original placed every player on its
team's base pads at match start (`ctcChooseSpawnLocation`). The PC's match-start orchestrator
(`mpOrchestrateMatchStartSpawns`, `src/game/mplayer/mpspawn_orchestrate.c`) assigns all
participants with a Hungarian solve around team anchors that it spreads as far apart as the spawn
pool allows, and never asked the scenario, so in Capture the Case a team could start next to the
other team's base. Respawns already went through `ctcChooseSpawnLocation`. Fix: the orchestrator
asks `scenarioGetTeamSpawnAnchor` for each active team and, for Capture the Case, anchors that
team's region on its base's home pad (`ctcGetTeamSpawnAnchor`), before the team regions are drawn;
logged as `SPAWN.ORCH: team N starts at its base`. A team without a base keeps the spread anchor.
The bases are set by `ctcInitProps` during stage setup, before the orchestrator runs (checked in the
Capture the Case net smoke logs). Other scenarios are unchanged. Pinned in
`tests/test_net_team_limit_static.cpp`; game check: the Capture the Case smokes count the line.

## 2026-11-15 B-1133 The capsule sweep classified a hit by geometry its collision test never recorded (T-NETWORKING-011)

Severity high (a crash, intermittent, in any mode; plus wrong hit classification). Seen in
`mp_hold_the_briefcase_net_smoke` on c3c66668 (2026-11-15, `results-20261115T085746Z.json`,
receipt `20261115T084611729343Z-mp_hold_the_briefcase_net_smoke-4c39af09`): the host died 2.5 s into
the match, `FATAL: ACCESS_VIOLATION PC=+0x5f3bdc`, which `addr2line` on the frozen binary puts at
`cdGetGeoFlags` (`src/lib/collision.c:104`, `switch (g_CdObstacleGeo->type)`), right after the host
force-moved the client's player. Not related to B-1129 (no simulant pickup or drop had happened).
Cause: `collision.c` has "Exam" queries, which record the obstacle's geometry
(`cdSetObstacleVtxColPropFltGeo`), and "Test" queries, which record only its prop
(`cdSetObstacleProp`). Every original caller reads geometry only after an Exam query. The PC capsule
sweep (`capsuleSweep`'s stage 1, `src/lib/capsule.c:303-329`) tests the volume with `cdTestVolume`, a
Test query, and then classifies the hit with `cdGetGeoFlags()`, which reads `g_CdObstacleGeo`: NULL
until the first Exam query of the process (the crash: a host whose first capsule collision came
before any Exam query), and afterwards whatever surface an earlier Exam query hit, so a wall hit
could be classified as floor or ceiling from someone else's flags. Fix: `cdTestVolume` records the
geometry it hit (it has it, `collisions[0].geo`), and `cdGetGeoFlags` / `cdGetObstacleNormal` answer
"no flags" / leave the caller's normal when no geometry is recorded, logging that once. Original
behavior is unchanged: no original caller reads geometry after `cdTestVolume`. The capsule's stage 1
now classifies by the surface it actually touched, which can change how players and simulants
respond to wall contact where the leftover flags said floor or ceiling. Propagation: the only other
readers of the obstacle geometry, `surface_loco.c:378,611` and `propobj.c:4187`, read it after
`cdExamLos08` / `cdExamLos09`, which are Exam queries. Pinned in
`tests/test_collision_obstacle_static.cpp`; game check pending (the briefcase smoke re-run).

## 2026-11-15 B-1132 The in-match team switch offered 8 teams in Capture the Case (T-NETWORKING-011, finding 8)

Severity low (gameplay; no crash found). The port's in-match team switch for networked team
matches (`menuhandlerNetTeamSwitch`, `src/game/mplayer/ingame.c`) offered `MAX_TEAMS` = 8 teams, and
the host's `netmsgSanitizeClientTeam` (`netmsg.c`) accepted any team below 8. Capture the Case has 4
bases (`ctcGetMaxTeams`) and 4-entry team arrays; `ctcInitProps` folds teams into 0..3 only at match
start. A player who switched to team 5 mid-match had no base, spawned anywhere, and every case
counted as the other team's. Checked for out-of-bounds: `ctcChooseSpawnLocation` bounds the team
index (B-1126), and the simulant code that indexes by team uses the simulant's own, folded team.
Fix: the switch offers `scenarioGetMaxTeams()` teams, and the host keeps the current team when a
client in a match asks for one past that. Propagation: `scenarioGetMaxTeams`,
`scenarioIsRoomHighlighted` and `scenarioHighlightRoom` indexed `g_MpScenarios` by
`g_MpSetup.scenario` without the range check the other 21 lookups have; they now check it quietly
(they run per menu tick or per room).

## 2026-11-15 B-1131 A client stops reading a packet at the first message it rejects (T-NETWORKING-011, SP-81)

Severity medium (silent loss of reliable messages; no instance seen in a log yet). Found reading
the client receive loop for B-1129: `netClientEvReceive` (`port/src/net/net.c`) runs
`while (!rc && netbufReadLeft(&cl->in) > 0)`, and 14 read handlers in `port/src/net/netmsg.c`
return `src->error ? src->error : 1` when they reject a well-formed message (a prop or character
this client does not have, a stage it is not in). Every message after it in the same packet is then
dropped with only `NET: malformed or unknown message 0x..` in the log. Before B-1129 the likely
trigger was `SVC_PROP_PICKUP` for a weapon a simulant dropped, which a client never had under the
host's sync id. Not fixed as a class: several handlers reject before reading the rest of their
message (`netmsgSvcPropSpawnRead` returns before the weapon fields), so simply continuing would
misread the following bytes as new messages. Each handler has to read its whole message first and
then return 0 on a rejection. The new `netmsgSvcBotPickupRead` does that. Search and checklist in
SP-81.
Worse half, found by the SP-81 audit (2026-11-15): four in-match readers returned 0 with part of
their message unread, so the loop read the tail as the next messages (a stream desync, not just a
dropped packet): `netmsgSvcPlayerStatsRead` (a player with no prop here: the hand ammo, ammo mask,
held ammo and attacker id were left), `netmsgSvcPropMoveRead` (a prop this client does not have:
the projectile tail was left), `netmsgSvcChrDisarmRead` (not in the game yet, or a chr victim: the
grenade position was left; its writer also skipped the position when given none, which the reader
cannot see) and `netmsgSvcPropSpawnRead` (no prop allocated: the object fields were left).
Fixed (2026-11-15, built; pd-tests 1,951 cases green): the first three read their whole message
into locals before deciding; the disarm writer always sends a position with positive damage; the
spawn path stops the packet instead of desyncing (it allocates props while it parses, so its
read-first rewrite is separate); pickup, use and character damage skip a message they cannot apply
with a warning and return 0. Left failing closed on purpose: the reconnect, auth and vehicle state
readers (the dispatcher's terminal disconnect for a rejected reconnect message depends on it) and
the hoverbike raw-use rejection. Pinned in `tests/test_net_reader_consumes_static.cpp`.
`netmsgSvcPropSpawnRead` second pass (2026-11-15, built; pd-tests 1,953 cases green): it now parses
the whole row (header, weapon or laptop fields, object block, projectile block) before it decides
or allocates, skips a declined row with 0 outside a reconnect (not in the game, a sync id it already
has, an owner it cannot resolve, no prop allocated, a topology mismatch) and still fails closed
inside one; it also no longer writes `realrot` through a NULL `prop->obj`. Its writer sent the
projectile block only with a non-NULL projectile, a condition the reader could not see; it now
clears `OBJHFLAG_PROJECTILE` in the written value when there is no projectile. Simulant drops
(B-1129) arrive as spawns, so this matters more than before. Open: the lobby, catalog, distribution
and admin readers the audit lists (`audit_readers.py`, session scratchpad: 27 readers return before
their last read, most of them on purpose or on a broken buffer).
Third pass (2026-11-15, the open list): every remaining reader and every writer that announces a row
count was checked (a second scan, `audit_count_writers.py`, found the writers; the first scan missed
returns inside row loops). The worst finds are in the resync transaction, which
`netServerPublishPendingResyncs` (`net.c`) sends as one packet: character, prop, score and NPC
resyncs, then the NPC digest, stage flags, objective statuses and the v62 scenario baseline. A short
row anywhere garbles everything after it. Fixed: `netmsgSvcChrResyncWrite` wrote a 4-byte null
placeholder for a simulant without a prop or aibot while the reader reads a whole row for every
count; it now counts and writes whole rows only (it is also in the reconnect exact state, where a
garbled message is a terminal disconnect). `netmsgSvcChrResyncRead` and `netmsgSvcPropResyncRead`
returned 0 from inside their row loop when the client was not in the game yet, leaving the other
rows to be read as messages; they now read every row and apply none. `netmsgSvcPlayerScoresWrite`
wrote `g_MpNumChrs` as the count and then skipped participants without a config; it now writes the
number of rows it sends. `netmsgSvcNpcResyncRead` declines an incomplete snapshot with 0 (it has
read every record by then), so the digest, flags, objectives and scenario state after it still
apply. `netmsgClcAdminRead` returned 0 for an unauthenticated or non-server command without reading
its arguments (a desync) and 0 for an unknown subcode; it now consumes the arguments by the layout in
`server_admin.h` and stops the packet on an unknown subcode. Checked and left as they are: the
catalog and distribution readers (they stop only on a broken count, OOM or an empty id, and those
are reconnect control messages that fail closed on purpose), `CLC_MANIFEST_STATUS` (every send is
its own packet, so its early stops lose nothing; its order is a security pin), `CLC_BOT_MOVE` (its
count check is a security pin and stops without desync), the room list (count and rows use the same
predicate), room settings and playlist updates and the NPC "not ready" gate (they stop after or
before a whole message, never inside one). Pinned in `tests/test_net_reader_consumes_static.cpp`
(11 cases); the 224 `[net]` cases pass.

## 2026-11-15 B-1130 A human's Capture the Case capture never reached the clients (T-NETWORKING-011)

Severity high for networked Capture the Case. Found reading the pickup path for B-1129: touching
your own case while carrying the other team's returns `TICKOP_NONE` from `scenarioPickUpBriefcase`
(`scenarios.c`), and `propPickupByPlayer` (`propobj.c`) sends `SVC_PROP_PICKUP` only for a nonzero
tick operation. So on the host the capturer scored, dropped the carried case (announced, it goes
back to its base) and lost it from the inventory, while the capturing client kept the case in its
own inventory, heard no point sound and saw no capture message. Fix: the host notes whether the
player carried a case before the pickup and, when a capture took it, sends `SVC_PROP_PICKUP` with
`TICKOP_NONE`; every client replays the capture (inventory, point, messages) and the touched home
case stays put. Pinned in `tests/test_net_bot_pickup_static.cpp`; game check pending.

## 2026-11-15 B-1129 Simulant pickups and drops were not sent to clients (T-NETWORKING-011, finding 3)

Severity high for networked play with simulants (every mode; the token modes worst). Simulant AI
runs only on the host (`botCheckPickups` from the bot tick), and nothing told the clients:
- A pickup (`botPickupProp`, `bot.c`): the weapon, crate, shield, briefcase, uplink or case stayed
  where it was in a client's world; a human walking onto it there got nothing, because the host,
  which decides human pickups, no longer had it.
- A drop (`botinvDrop`, `botinv.c`, from `chrDie`, which a client also reaches when it applies
  `SVC_CHR_DAMAGE` through `chrDamage`, `chraction.c:5125`): each client made its own copy
  of every item a dead simulant dropped. A prop a client allocates gets sync id 0 (`prop.c:211`),
  so the host's `SVC_PROP_PICKUP` for its own copy (host sync id) could never resolve on the
  client: the client's player did not get the weapon, the local copy stayed, and the rejected
  message stopped the rest of the packet (B-1131).
Fix: the host wraps each simulant pickup (`botPickupPropAndAnnounce`) and sends the new
`SVC_BOT_PICKUP` 0x4D {simulant, item sync id taken before the pickup} when the pickup took effect
(a tick operation, a deletion mark for the scenario tokens, which return `TICKOP_NONE`, or a Capture
the Case capture); a client replays the same `botPickupProp` on its copy of the simulant plus the
`TICKOP_FREE` that `botCheckPickups` runs, and skips (logs, returns 0) a pickup it cannot apply.
`botinvDrop` makes drop props on the host only and announces each with `SVC_PROP_SPAWN` and
`SVC_PROP_MOVE`, after a Capture the Case case went back to its base, as the player drop does, and
retires a half-dropped prop as the player drop does. The client token searches
(`htbNetPresent`, `htmNetPresent`, `ctcNetPresent`) skip a copy marked for deletion. Wire stays
v62 (unreleased when this was added). Pinned in `tests/test_net_bot_pickup_static.cpp`; game check
pending.

## 2026-11-15 B-1128 A networked client set up the objective scenario on the previous stage (T-NETWORKING-011)

Severity medium (reads and changes the wrong stage's data; harmless so far only because the old
stage was still loaded). Seen in `mp_king_of_the_hill_net_smoke` (2026-11-15, build 71f23c32,
install `20261115T065303450187Z-mp_king_of_the_hill_net_smoke-8c2bfc97`): on the client,
`netmsgSvcStageStartRead` called `scenarioInitProps()` right after `mpStartMatch()`, which only
requests the stage, so King of the Hill picked "hill 0 of 6 at pad=176 room=84" from the stage
still loaded; the real stage load then ran it again ("SETUP: calling scenarioInitProps", hill 5 of
6 at pad 192 room 7, the host's own pick). For Hacker Central, Hold the Briefcase and Capture the
Case that early call also created objects on the old stage's pads. The comment justifying it (the
`normmplayerisrunning` guard "can be false" on a client) does not hold: the client's `setup.c` path
ran. Fix: the early call is removed; the stage load sets the scenario up as on every peer, and the
host's scenario state (v62) corrects anything a client picked. Propagation: no other
`scenarioInitProps` caller runs before the stage loads (`setup.c:2900` only).

## 2026-11-15 B-1127 A networked client applies the host's character-slot data to the wrong two players (T-NETWORKING-011)

Severity high (every networked scoreboard). Found reading the code for the scenario state seam;
the slot swap itself is in the `mp_combat_net_bots_smoke` logs of 2026-11-14
(receipt `20261114T225327900327Z-mp_combat_net_bots_smoke-2a52b4b2`): the host logs
`NET: allocated playernum=0` for its own player and `1` for the client, while the client allocates
the host's player to `playernum=1` and itself to `0`. A client runs its own player as player 0:
`netPlayersAllocate` (`port/src/net/net.c`) gives the local client player number 0 and gives the
host's player the slot the host assigned this client (k); everyone else keeps the host's number.
`g_PlayerConfigsArray`, `g_Vars.playerstats` and `g_MpAllChrPtrs`/`g_MpAllChrConfigPtrs` follow that
local numbering (`netmsg.c:3715` places each roster config at `runtime_playernum`;
`player.c:3435`), and `netSyncIdsAllocate` swaps the two players' prop sync ids to match. But
`SVC_PLAYER_SCORES` writes the host's slot index and the client applies it to its own slot of the
same number (`netmsgSvcPlayerScoresRead`), with each player's `killcounts[]` indexed the same way,
so slots 0 and k would trade team, deaths, points and kill counts on the client. Not visible in
`mp_combat_net_bots_smoke` (2026-11-14) because both humans ended with score 0 and 1 death. Any
scenario state indexed by character slot (Pop a Cap victims and counts, the briefcase holder) has
the same exposure.
Verified in game 2026-11-15 on 4a467190 (networked smokes of all five objective modes, two humans
and three simulants on Complex; host and client logs compared by `net_compare.py`, session
scratchpad): after mapping the client's slots 0 and 1, every final result line agrees, including the
three deaths in Hacker Central (one of them the client's own player, host slot 1), the two in
Capture the Case and a simulant's hill point in King of the Hill.
Fix (2026-11-15, built; pd-tests 1,931 cases green): `netChrSlotFromHost`
(`net.c`, declared in `net.h`) maps a host slot to the client's own (0 <-> k, its own inverse, the
identity on the host, offline and for simulants); `netmsgSvcPlayerScoresRead` maps the slot and
every `killcounts[]` index, and `scenarioNetLocalizeSlots` (`scenarios.c`, run on every received
scenario state) maps every character slot in it: the Pop a Cap victim and per-slot counts, the
briefcase holder, the uplink holder, download player, player in range and per-slot progress and
points, and each case holder.
Propagation: no other message in `netmsg.c` indexes characters by slot (the lobby kill feed
matches by name). The networked smokes' cross-log score comparison is the check.

## 2026-11-15 B-1212 Every G_TRI1 triangle index read as 0 on 64-bit, so no triangle of a generated model could be hit (T-MODDING-008)

Severity high (engine-wide on PC). `Gtri` in `include/PR/gbi.h` had no
`PLATFORM_64BIT` padding. On 64-bit each `Gwords` word is 8 bytes, so w1
starts at byte 8, but `Gtri.tri` sat at bytes 4..7, the zero upper half of w0:
every `gdl->tri.tri.v[...]` read 0. `Gtri4` (PD's four-triangle command) was
already padded (`pad2[4]`), so stock ROM models, which draw with G_TRI4, were
fine. Generated models (the mod asset compiler emits `gSP1Triangle` only)
lost every triangle in the readers that decode G_TRI1: `bgTestHitOnChr`
(projectiles into characters, and single-player bullets, which use the
precise test; multiplayer bullets use the cheap box test), the room hit tests,
`meshWorldAddRenderedRoom`, the x-ray room renderer and `texLoadFromGdl`. The
readers skip an all-zero triangle without a word, so it never showed in a log.
Found by `crystal_pistol_bot_embed_smoke`: crystals passed within 12 units of
an on-screen bot whose bounding boxes they hit, and the triangle test
(`BONDGUN.PROJECTILE.CHRWALK`: generated body, owned display lists, vertices
present) found nothing. Fix: the same 4-byte pad before `Tri tri` on 64-bit
little-endian; a unit test decodes `gSP1Triangle` and G_TRI4 words through
the union. Pattern: SP-83.

## 2026-11-15 B-1211 Bots never used mod weapons: public weapons had zeroed bot preferences (T-MODDING-008)

Severity high (every `.pdweapon` weapon in a match with simulants). The public
weapon adapter (`loaderPoolInstallPublicWeaponAdapter`, `port/src/loader_pool.c`)
zeroed the weapon's `aibotweaponpreference` apart from the reload delay.
`botinvTick` only considers a function whose `haspriammogoal`/`hassecammogoal`
bit is set, so a bot spawned with a mod weapon (the spawn weapon arms bots too)
switched to unarmed at once: bots never fired the crystal pistol or the
Needler. The last report's "bots throw single mines" was read from code, never
seen. Fix: `s_publicWeaponBotPrefsBuild` derives defaults until the format
carries bot preferences (CORE): each executable function usable, scored like
a mid pistol, attack range by function kind (a committed charge at explosive
range), no ammo goals for a self-loading magazine, reload delay = the full
regrow. Two follow-on gaps fixed with it (`botact.c`, `botinv.c`): a
self-loading magazine counted as empty once fired, so the bot would drop the
gun until it regrew, and a switch between two functions that share one
self-loading magazine refilled it from nothing (the native switch returns the
rounds to a reserve and reloads). Bots now report such a magazine as always
full, and keep its rounds across the switch. Two more found reading the bot
fire path (`bot.c`, `botact.c`): a mod weapon's shot has no recoil animation,
so `botactGetShootInterval60` gave 0 and a bot would fire every frame (all six
crystals in a tenth of a second); it now uses the function's recovery time,
else 15 ticks. And a bot fires a "regular" weapon only with rounds loaded, so a
mod function with no magazine (the Needler's, which fires without ammo per
B-1151) would never fire; `botactFiresWithoutAmmo` lets it. Pattern: SP-37.

## 2026-11-15 B-1210 The world mesh grid drops triangles past its 128-cell cap (T-MODDING-008, found, not fixed)

Severity low today (only stages wider than 32768 units on an axis). Found by
the B-1189 unit test. `meshWorldFinalize` (`src/lib/meshcollision.c`) caps the
grid at `MESHGRID_MAX_DIM` (128) cells of 256 units per axis and clamps each
triangle's cell range with `if (cx1 >= cellsx) cx1 = cellsx - 1` but leaves
`cx0`/`cz0` unclamped, so a triangle wholly past the cap gets `cx0 > cx1` and
is listed in no cell. Grid queries (`meshFindFloor`, `meshFindCeiling`, the new
`meshGridRayCast`) cannot see it; `meshFindFloor` also skips lookups past the
cap. `meshGridRayCast` falls back to testing every triangle on a capped grid.
Proper fix for CORE: size cells to the stage (or clamp both ends and the
lookups), then drop that fallback.

## 2026-11-15 B-1189 Shots, splats and spawn checks ignored walls on scenario-source stages (T-MODDING-008)

Severity high (every multiplayer arena; Mike asked for it to be fixed). The
B-1187 pattern in the other `bgTestHitInRoom` callers: on a scenario-source
stage no room has vertex batches, so the player's hitscan never registered a
background hit. In MULTIPLAYER's smoke logs every player shot is
`COMBAT: SHOT_RESULT MISS` (16 of 16, 28 of 28), never `HIT_BG`: shots went
through walls and floors (no wall hits or sparks; props behind walls could be
hit). The same gap left blood splats without walls, made every spawn-pool
candidate look open (its clearance rays never hit), and the `gfxdata` guard in
`explosions.c` skipped every scorch mark. Fix: `bgTestHitScenarioMesh`
(`bg.c`) answers from the world collision mesh through a new grid-walked,
flag-masked ray cast (`src/lib/meshraygrid.c`, `meshRayCastWorldMasked`).
Shots, splats and sticky projectiles stop at floors and `BLOCK_SHOOT`
surfaces, so they pass movement-only clip walls (50 on Felicity, 82 on
Complex) as native render-geometry hits did; spawn clearance uses every
surface. The Farsight's x-ray mode still skips the whole background test and
shoots through walls. Callers: `shotCalculateHits`, `shotTestLos` (`prop.c`),
`splat.c`, `spawnpool.c` (2), the sticky test in `propobj.c`; scorch marks are
allowed without `gfxdata`. Hits carry texture -1 (default surface sound,
sparks and bullet holes).

## 2026-11-15 B-1188 Spent mod rounds lying on the floor were collected as the whole weapon (T-MODDING-008)

Severity medium (mod weapons with a self-loading magazine). Found by
`crystal_pistol_mine_bot_smoke`: crystal mines that came to rest next to a
bot vanished without arming (`freed ... landed=0 timer240=-1`). Bots and
players pick up any weapon object unless it carries
`OBJFLAG_UNCOLLECTABLE` (`bot.c` pickup filter, `objTestForPickup`). Native
mines are protected by a weapon-number rule (`timer240 >= 0`); custom weapons
only by a sticky-device entity's `sticky_pickup_policy none`. So a resting
crystal or crystal mine was collected as the crystal pistol plus ammo, by a
bot passing by and by its owner, whom the owner's own mines ignore. Fix:
`weaponCreateProjectileFromGset` marks projectiles of a mod function whose
ammo slot is self-loading (`bgunModGsetFiresSpentRounds`) uncollectable:
they are spent rounds with no reserve to refill. Open for MODS/CORE: custom
deployables with a reserve (a mod mine weapon) still have no "not while
deployed" pickup rule like the native mine list.

## 2026-11-15 B-1187 Sticky projectiles fell through floors on scenario-source stages (T-MODDING-008)

Severity high (every sticky projectile on every stage whose background comes
from a scenario source, which includes the multiplayer arenas: mines, bolts,
knives, laptop guns, mod crystals). Found by the crystal pistol smokes:
crystals fired at Felicity's floor fell through it and left the world, and
mines fired at the floor dropped through the bounce fallback instead of
sticking. `func0f06cd00` (`propobj.c`) tests a sticky projectile's path with
`bgTestHitInRoom` for loaded rooms and `cdExamLos09` for unloaded ones. On a
scenario-source stage `bgLoadRoom` only marks a room loaded and never builds
its vertex batches, so `bgTestHitInRoom` never hits, and `cdExamLos09` only
checks walls (`GEOFLAG_WALL | BLOCK_SIGHT | BLOCK_SHOOT`). Net effect: sticky
projectiles hit only walls of rooms that were off screen. Fix: on such a
stage, with the world collision mesh built, the hit test ray-casts it
(`meshRayCastWorld`; the stage's collision tiles are compiled from the same
mesh) and faces the normal toward the projectile; ROM-background stages keep
the native path. New `bgIsScenarioSource()`. Cost: one brute-force mesh ray
per sticky projectile per tick (3785 triangles on Felicity); a grid walk
would cut it if many projectiles fly at once.

## 2026-11-15 B-1186 The forced first-person look put the camera 159 units too high (T-MODDING-008)

Severity low (smoke tooling only; no shipped path uses it). Found when the
crystal pistol smokes that use `--debug-force-first-person-look` fired
crystals from 130 units above the camera: they started inside or above the
ceiling, never touched the room geometry and flew out of the world
(`freed ... at=(986,-18082,32319) landed=0`). `bootDebugForceFirstPersonPreRenderTick`
(`port/src/main.c`) built the forced camera position as the player prop
position plus `vv_eyeheight`, but the player's prop already sits at eye
height (spawns place it at ground + `vv_eyeheight` in `player.c`), so the eye
height was counted twice. Since shots, muzzle positions and the bot placement
hook all derive from the forced camera, every gameplay check under a forced
look ran from the wrong place. Fix: the prop position is the camera position.
Only the crystal pistol smokes use the flag (it was added for screenshots in
957ad3a2), so no other fixture moves.

## 2026-11-15 B-1185 A held mod gun with six or more display lists overwrote the hand's data (T-MODDING-008)

Severity high (silent memory corruption; latent until a mod gun has enough
parts). Found reading the code before building the crystal pistol's part
layout. The held gun model's per-node runtime data (rwdata) lives in
`struct hand` `unk0a6c[32]` (`src/include/types.h`), and `modelInit` writes
`rwdatalen` words into it without a check (`src/game/bondgun.c`, the
`bgunTickMasterLoad` init loop). ROM guns fit because their GUNDL nodes cost
no rwdata, but the mod mesh compiler emits DL nodes (type 24), which cost 6
words each on x64 (three pointers), plus 1 per toggle. A mod gun with six or
more display lists overran into `handsavedata` and then `handmodel`. Related:
`bgunSetPartVisible` and the muzzle-flash collector wrote a toggle's word for
any node under the part number, toggle or not.
Fix on `week-pd/crystal-pistol`: a held model larger than `unk0a6c` gets a
per-player, per-hand side buffer of 1024 words (`bgunHeldRwdataFor`); one larger
than that is not drawn (logged `BONDGUN.MODEL: ... (B-1185)`). Toggle writes go
through `hand->gunmodel.rwdatas` and only for TOGGLE nodes. The crystal pistol
is laid out to fit the original 32 words (27: one DL per look, crystals bound to
their seat matrices) and its smokes forbid the B-1185 log line.

## 2026-11-15 B-1184 Sixteen input actions had no saved name, so the bindings file broke after the first launch (T-MODDING-008)

Severity high (binding saves refused). Found by the input survey for the new
gun roll actions. `s_ActionNames` (`port/src/actionmap.cpp`) stopped at action
104, but actions 105-120 (menu skip, dev hotkeys, GPU swarm toggle, vehicle
look, handbrake and use) were added later without names. Their pd.ini keys
registered as `(null)` (`configLoad: malformed keyvalue line: (null)=E` in
old logs), and writing the complete bindings file stopped at the first unnamed
row (a bare `gameplay.`), so the next launch refused the file and blocked every
later binding save ("The current binding file is invalid").
Fix: names for 105-120 (`MenuSkipUp` ... `VehicleUse`) and the new gun roll
actions (121-122); the table is now unsized with a `static_assert` that it has
exactly `ACTION_COUNT` names, so a new action without a name no longer builds.
An existing file written by an affected build is still refused once (it really
is truncated); loading a profile or deleting `input-bindings.ini` replaces it.

## 2026-11-15 B-1183 Dying while holding a mod weapon crashed the game (T-MODDING-008)

Severity high (crash in ordinary multiplayer). Found re-running the Needler
viewmodel fixture with a wait for gameplay on the B-1180 client: the twelve bots
killed the player at about 65 s, the held Needler was dropped as a physics
object, and the client crashed in `projectileFall` (`src/game/propobj.c`, the
`bbox->xmax` read after `objFindBboxRodata`; backtrace via addr2line). A
modeldef built from an author source hierarchy (`model.nodes.json`) gets a
bounding box only if the hierarchy lists one; the flat source path always adds
one, extracted models list their own, and base weapons drop a separate world
model that has one, but a mod weapon's single model is also its dropped and
pickup object. Not caused by B-1180 to B-1182 (drop physics untouched).
Fix on `week-pd/crystal-pistol`: `generatedModeldefEnsureHierarchyBounds`
(`port/src/modasset_compiler.c`) links a bounding box from the mesh bounds under
the root when a skeleton-less, non-character hierarchy has none. Extracted models
(native skeletons) are untouched. Verified on the branch client (shared base 3,
single process, `--portable`): the Needler fixture with twelve bots played 52
death screams in 30 s of combat and passed 21/21 with no crash; the same fixture
on the client without this fix crashed after 15. Static pin `[b1183]`.

## 2026-11-15 B-1180 A held mod gun drew without depth testing (T-MODDING-008)

Severity high (every mod weapon's first-person model draws wrong). Found
running the Tripo-built Maian crystal pistol model (test mod
`dev-mods/crystal_pistol`, OBJ + texture, render class 0) in game on shared
base 3: the gun loaded and rendered, but far-side parts (the muzzle bezel,
the barrel crystal, inner faces) showed through the body. With every material
set to `pd_render_class = 3`, which makes the generated display list emit
`G_RM_AA_ZB_OPA_SURF`, the same model drew clean and matched the expected
180-degree pose. Cause: `bgunRender` (`src/game/bondgun.c`) set
`renderdata.zbufferenabled = false` for any skeleton-less generated gun model
(added in 957ad3a2 with no recorded reason). Generated display lists for
author meshes set combine and geometry state but inherit the node's render mode,
which `modelApplyRenderModeType1` makes non-z-buffered when that flag is off,
so the whole gun drew in triangle order without depth. This is also why held
mod models looked turned about 225 degrees (MODS stand-in runs): with no depth,
near and far sides swap visually; the pose is the native 180 degrees.
Fix on `week-pd/crystal-pistol`: the exception is removed; every held gun is
drawn with the z-buffer. The static pin in
`tests/test_mod_external_archive_static.cpp` now requires the exception's absence.
Verified: the unmodified crystal pistol test mod passes
`crystal_pistol_viewmodel_smoke` 28/28 and its captures show a clean, depth-correct
gun pointing into the screen; the Needler still renders solid.

## 2026-11-15 B-1181 A mod weapon whose activation failed was still equipped, and the game crashed (T-MODDING-008)

Severity high (crash). Found with the crystal pistol test mod: its
`function.empty` secondary made the public weapon adapter fail (B-1182), the
manifest rolled the weapon back and cleared its custom slot (zeroed: no aim
settings, no functions), but match setup kept the user-picked spawn weapon
(`SETUP: user-picked spawn weapon ... preserved`), the player was given and
equipped slot 86, and the first gun tick dereferenced the null aim settings in
`bgunSwivelWithDamp` (`bondgun.c`, backtrace via addr2line). Nothing checked that
a weapon id named an installed definition. Fix on `week-pd/crystal-pistol`: new
`catalogManagerWeaponIsUsable` (`port/src/catalog_mgr_weapons.c`; every installed
definition, from the loader pool, the public adapter or the v2 runtime, carries
aim settings); `bgunEquipWeapon` equips unarmed instead of an unusable id (it is
the funnel for every equip path), `invGiveSingleWeapon` and `invGiveDoubleWeapon`
refuse it, and the spawn pick in `setup.c`, `player.c` and `bot.c` falls back to
the weapon set instead of keeping it, each with a warning naming B-1181.
Verified with a test-mod variant whose functions use `ammo_slot` 5: the adapter
rejects it, setup logs `picked spawn weapon ... has no installed definition`,
the player spawns with the Falcon 2 and the match runs to a clean exit.

## 2026-11-15 B-1182 The mod weapon adapter rejected function.empty, which base weapons and conformance accept (T-MODDING-008)

Severity medium (a valid archive fails to load; with B-1181 it crashed).
`function.empty` is how every extracted base weapon writes a missing secondary
(`function_type: none`, `source: empty_slot`), and
`tools/asset_archive_conformance.py` accepts it, but
`s_publicWeaponFunctionBuild` (`port/src/loader_pool.c`) treated the valid held
function as an unsupported function type 0 and failed the whole adapter. Fix on
`week-pd/crystal-pistol`: new inline `weaponGraphHeldFunctionIsExecutable`
(`port/include/weapon_graph_runtime.h`) treats a function.empty slot as absent;
the adapter installs a NULL native function for it, as base weapons have, and
counts only executable functions. Behavioural test: a graph with a hitscan
primary and a function.empty secondary compiles, registers, and only the primary
is executable. Verified in game: a test-mod variant with a function.empty
secondary installs (`PUBLIC_ADAPTER: installed ... funcs=1`) and renders.

## 2026-11-15 B-1126 Capture the Case, King of the Hill and Hold the Briefcase lose their arena positions on extracted arenas (T-NETWORKING-011)

Severity high for Capture the Case (the mode cannot be played as designed),
medium for King of the Hill and Hold the Briefcase. Found by the propagation
check for B-1125; same root cause. These scenarios take their places from the
arena's intro commands, which the extracted arenas do not carry (see B-1125):
- Capture the Case: each team's case goes on its `INTROCMD_CASE` home pad and
  its players respawn on `INTROCMD_CASERESPAWN` pads. With none,
  `spawnpadsperteam[].homepad` stays -1 and `ctcInitProps` places every case on
  pad -1, so `setupCreateObject` creates it off-world: there is no case to take,
  and no base room. `mp_capture_the_case_local_smoke` still passes (25/25,
  2026-11-15) because it asserts only the match lifecycle.
- King of the Hill: hills are `INTROCMD_HILL` pads. With fewer than two,
  `kohInitProps` and the hill move in `kohTick` both use pad 0, so the hill is
  always the room of pad 0 and never moves (no crash: both paths guard
  `hillcount >= 2`).
- Hold the Briefcase: `htbCreateToken` replaces a random multiplayer ammo crate,
  else a random case pad, else pad 0. With neither, the briefcase starts and
  respawns on pad 0 in every match.
Fix options: (1) CORE carries the intro CASE, CASERESPAWN and HILL commands
through extraction and the runtime intro builder (restores the original
places on every arena; asked in the week note); (2) the walkable-pad fallback
from B-1125 for arenas that still lack them, for example mod arenas. (2) changes
how the modes place things on such arenas, so it waits for Mike. Class: SP-80.

Fix (Mike, 2026-11-15: "get all the MP modes functional", including the same
fill-in as Hacker Central for these three). Option (2), used only where the
arena's own pads are missing; option (1) is still asked of CORE and makes the
fill-in idle again on the standard arenas. Candidates are the same walkable
pads as B-1125 (`scenarioCollectFallbackCandidates` in
`src/game/mplayer/scenarios.c`: navigation waypoints, else every pad in a room;
in a room and not on a lift). New pure pickers in
`port/src/scenario_pad_fallback.c`, unit-tested:
- Capture the Case (`ctcFillMissingPads`, called in `ctcInitProps` after the
  teams get their base slots): a base in play whose home pad is not a pad in a
  room gets the walkable pad farthest from the other bases in play
  (`scenarioPadPickFarthest`; with no usable base at all the first is random,
  one `rngRandom` call); a base in play with no usable respawn pad gets up to
  six walkable pads nearest its home pad among those nearer to it than to any
  other base (`scenarioPadPickNearest`, distance in three dimensions), so its
  players respawn at their own base. Each team's case placement is logged
  (`CTC: team T case on base B pad=P room=R respawnpads=N prop=ok`).
  Hardening found on the way: `baserooms[]` no longer dereferences a NULL case
  prop, and `ctcChooseSpawnLocation` refuses a team with no base slot (index
  -1 used to read before the array).
- King of the Hill (`kohFillMissingHills`, start of `kohInitProps`): with fewer
  than two usable hills the match gets up to six, one pad per room
  (`scenarioPadReduceToRooms`: the pad nearest the room's middle, rooms crossed
  by two or more waypoints when there are enough), in rooms none of the
  arena's own hills is in, spaced apart (`scenarioPadPickSpaced`). Listed hills
  that are not in a room are dropped. The original `@bug` (a single hill used
  the room of pad 0) is fixed in `kohInitProps` and in the hill move in
  `kohTick`. Logged: `KOH: hill I of N at pad=P room=R` and
  `KOH: hill moved to I of N pad=P room=R`.
- Hold the Briefcase (`htbTopUpBank`, start of `htbInitProps`): with no
  multiplayer ammo crate and no usable bank pad, the bank is reset and gets
  eight spaced walkable pads (the HTM picker), so the briefcase starts and
  respawns on a random real pad. Logged:
  `HTB: briefcase placed pad=P source=crate|bank|pad0 prop=ok|none`.
Arenas with their own pads keep the original placement and make no extra
random calls. Picks use `rngRandom`; whether a networked client picks the same
pads as the host is not established (the client runs `scenarioInitProps` from
its own stage-start path), so the network work (scenario state sent from the
host) has to carry these placements, not assume them. The
three local smokes now assert the outcome, not the fill-in, so they stay
valid once CORE's extraction carries the arenas' own pads: every team's case
on a pad in a room with respawn pads, the starting hill a real room chosen
from two or more hills, the briefcase on a real pad from a crate or the bank.

## 2026-11-15 B-1125 Hacker Central divides by zero when the arena has too few placement pads (T-NETWORKING-011)

Severity high (crash at match start). `mp_hacker_central_local_smoke` (offline,
Complex, three simulants, shared base 2, build 77049d63; receipt
`D:\PD2-wt\multiplayer\.claude\smoke-verify-runs\results-20261115T001057Z.json`)
crashed in `scenarioInitProps` right after the simulants spawned:
`EXCEPTION 0xc0000094` (integer divide by zero) at
`htmInitProps`, `src/game/mplayer/scenarios/hackthatmac.inc:305`
(`rand = rngRandom() % data->numpads;`, symbolized with addr2line). Not B-1113:
the rerun on shared base 3 with `week-pd/extract-2` merged crashes the same way
(`results-20261115T005428Z.json`).

Cause. Hacker Central puts its terminal and its data uplink on pads from a
bank: the arena's Capture the Case pads (`INTROCMD_CASE` and
`INTROCMD_CASERESPAWN`, added by `scenarioReset`) plus the
`MODEL_MULTI_AMMO_CRATE` crates among the active props (setup objects are
created before `scenarioInitProps`). It picks `htmNumProps()` (2) distinct
entries. An empty bank divides by zero; a bank with fewer usable entries than
that never leaves the inner `do ... while (padnum <= 0)`. The extracted arenas
carry neither source (an extraction-output gap, CORE's area):
- No setup objects on 16 of 18 arenas: in shared base 3,
  `base_scenario_mp_*.pdscenario` has an empty `objects.json` (60 bytes) and
  `setup.fields.json` (`"rows": []`) for all but Ravine and Sewers. Complex
  allocates `numobjs=2` (the training stage before it: 254). So those arenas
  also have no weapon, ammo or shield pickups (`WEAPON_MPLOCATION00`..`15` and
  the crates are setup objects); the spawn-with-weapon fallback hides that.
- No intro case or hill pads on any arena: the extractor
  (`s_buildIntroSpawnsTable`, `port/src/romextract_pdarena.c`) writes only
  `INTROCMD_SPAWN` rows to `spawns.json` (and those rows are empty on all 18
  arenas), and `port/src/scenario_source_runtime.c` rebuilds the intro script
  from `spawns.json` alone. Spawns work only through the waypoint fallback in
  `playerReset` (`SPAWN: populated 24 spawn points from 144 navmesh waypoints`).
Asked CORE in the week note. Same class: B-1126, SP-80.

Fix (Mike, 2026-11-15: "can we place some so there are enough?"):
- `htmTopUpBank`: when the bank has fewer distinct usable pads than the mode
  needs, fill it to `HTM_FALLBACK_BANK_PADS` (8) with walkable pads. Candidates
  (`scenarioFallbackPads`, `src/game/mplayer/scenarios.c`) are the arena's
  navigation waypoints, the source the spawn fallback uses, or every pad in a
  room when there are none; a pad must be in a room (room > 0, the condition
  under which `setupCreateObject` places an object) and not on a lift. The
  pure picker `scenarioPadPickSpaced` (`port/src/scenario_pad_fallback.c`)
  shuffles the candidates once with `rngRandom` and takes pads at the widest
  spacing that reaches the target (1000, 500, 250, 125, then 0 units), never a
  pad twice and never one the bank already has. A bank that is already big
  enough is left alone, so full arenas keep the original placement and random
  sequence.
- The selection loop asks for at most as many picks as the bank has usable
  entries, and a terminal with no pad is skipped with an error instead of being
  created off-world.
- The uplink, when no crate is left to replace, takes only bank entries that
  are still pads and not under the terminal. The original took any entry,
  including the -1 the terminal selection leaves, so the uplink was created
  off-world and `htmTick` created it again (a new prop each try) until a pick
  landed on a real pad. When no entry is left it uses the pad the selection
  reserved for the uplink, else pad 0 as before.
- `scenarioCreateObj` returns NULL instead of activating a NULL prop when the
  pad is in no room or the model fails to load.
- Logs: `HTM: bank had N usable pad(s) of 2 needed; added M walkable pad(s)`,
  `HTM: terminal 0 placed pad=P prop=ok`, `HTM: uplink placed pad=P source=...`.
Tests: `tests/test_scenario_pad_fallback.cpp` (12 cases).
`mp_hacker_central_local_smoke` now also requires the terminal and the uplink
on real pads and forbids the no-pad errors.

Verified 2026-11-15 on 86de8e08 (shared base 3, Complex, three simulants):
`mp_hacker_central_local_smoke` PASS 31/31
(`D:\PD2-wt\multiplayer\.claude\smoke-verify-runs\results-20261115T023803Z.json`).
The log shows `SCENARIO.PADS: HTM fallback added 8 of 8 pad(s) from 144 waypoint
candidate(s), existing=0`, `HTM: terminal 0 placed pad=104 prop=ok` and one
`HTM: uplink placed pad=114 source=bank prop=ok` (no re-creation), and the match
ran to its 40 second time limit with all four participants in the result.
pd-tests 1896 cases green.

## 2026-11-14 B-1124 Two-process smokes on a shared install ran against the real user profile (T-NETWORKING-011)

Severity high (test isolation; it writes the user's real data). Found on the
first run of `mp_combat_net_bots_smoke`: both game processes logged
`save dir: C:/Users/mikeh/AppData/Roaming/perfectdark`, loaded and saved
`playerstats.json` there, and the real profile's `pd.ini` and `playerstats.json`
carry the run's timestamps (2026-11-14 17:44 local), as do new files under its
`mod-cache`. Cause: `tools/smoke-verify/run.ps1` (multi-process launch, about
line 1741) adds `--basedir/--savedir/--moddir/--debug-home-path` only when a
fixture sets `separate_process_installs`; the single-process path always passes
`--savedir` (WRONG, corrected 2026-11-16 by B-1203: only `-ReuseInstallId` did, and 81 single-process fixtures
also ran against the real profile). A fixture whose processes share one install and pass neither
`--portable` nor `--savedir` therefore falls back to the game's default save dir
(`fsInit`, `port/src/fs.c`: `$H` = `%APPDATA%/perfectdark` on Windows). Seven
fixtures did: `listen_host_match_smoke`, `listen_host_peer_smoke`,
`mp_combat_net_bots_smoke`, `network_coop_stage_transaction_smoke`,
`network_counterop_stage_transaction_smoke`,
`network_stage_player_rollback_smoke`, `ready_gate_settings_rollback_smoke`.
Every run of them on this machine, by any session and through any checkout's
runner, read and rewrote the real profile; MULTIPLAYER's own runs today include
three through CORE's runner (B-1120 investigation) and two through its own.
The profile was NOT restored or edited by any session; that is Mike's call.
Fix on `week-pd/multiplayer`: both processes of those seven fixtures start with
`--portable` (base and save dir from the executable, which is inside the run's
install, so data resolution is unchanged); new
`tools/tests/test_smoke_fixture_isolation.py` (CI step) fails on any fixture
with a `processes` list that has neither `separate_process_installs` nor
`--portable`/`--savedir` on every process. Asked of CORE (runner owner): make
the runner itself refuse or isolate such a launch, so a fixture cannot reach the
real profile again.

## 2026-11-14 B-1123 Three C++ menu files declared C functions and an array with the 1-byte C++ bool (T-NETWORKING-011)

Severity medium (latent: no wrong value today). Class: SP-75. The pdgui menu
files avoid `types.h` and declare the game functions they call themselves, in
`extern "C"` blocks. There `bool` is the one-byte C++ keyword, while the C
definitions use the project `bool` (`s32`): `pdgui_menu_mainmenu.cpp` declared
`extern bool g_PlayersWithControl[]` (C++ indexed it with a 1-byte stride over
4-byte elements) and `void lvSetPaused(bool)`; `pdgui_menu_solomission.cpp`
declared `bool isStageDifficultyUnlocked(s32, s32)`; `pdgui_menu_botsetup.cpp`
declared `bool mpIsParticipantActive(s32)`. Today every value is 0 or 1, the
C++ code touches only element 0 of the array, and the calls pass constants, so
nothing misbehaves; element 1 to 3, or any non-0/1 value, would. Found by the
symbol check added to `tools/bool_layout_audit.py` (it compares every named
`bool` function and global across all files: 1,579 in the client, these 4
disagreed; a header-only check cannot see declarations made inside a source
file). Fix on `week-pd/multiplayer`: the four declarations are spelled `s32`.

## 2026-11-14 B-1122 keygen.ps1 garbles the non-ASCII text of updater_pubkey.h on every run (tooling, for CORE)

Severity low (comment text only; the key bytes are right). `devtools/keygen.ps1`
reads `port/include/updater_pubkey.h` with `Get-Content -Raw`, which in Windows
PowerShell 5.1 decodes a BOM-less file with the ANSI code page, then writes it
back as UTF-8. Each run re-encodes every non-ASCII character once more: the em
dash in the trust-properties comment is already one round garbled at HEAD
(`â€”`) and a fresh worktree's first build (keygen runs when
`dev-keys/ed25519-private.pem` is missing) turned it into `Ã¢â‚¬â€` and left
the tracked header modified. Fix: read with `-Encoding UTF8` (or
`[IO.File]::ReadAllText`) and restore the comment to plain ASCII `--`. Found by
MULTIPLAYER; handed to CORE as the tooling owner. Workaround: do not commit the
build's change to that header (`git checkout -- port/include/updater_pubkey.h`).

## 2026-11-14 B-1121 Project `bool` had two widths in 28 C files and 291 header declarations (T-NETWORKING-011)

Severity high (latent; B-1120 was the instance that broke matches). Class:
SP-75. After B-1120, the full audit (`tools/bool_layout_audit.py`, every client
translation unit preprocessed with its own flags) still found 28 C files where
`<stdbool.h>` turned project `bool` (`s32`) into the 1-byte `_Bool` partway
through, and 291 header declarations seen with both widths: 290 prototypes,
globals and typedefs (for example `sndIsMp3`, `modelTickAnim`,
`g_NetLocalBotAuthority`) and one struct member (`player.h`
`player_orchestrated_spawn_plan.prepared`, unused by the two files that saw it
1-byte). Minority views: `main.c` 241 sites, `smoke_harness.c` 142,
`pdgui_charpreview.c` 22, `modasset_compiler.c` 22, `modmgr.c` 15,
`assetcatalog_api.c` 2, `netmanifest.c` 1, `romdata.c` 1, and `netmsg.c`, which
called `netManifestTypeAcceptsCatalogAsset` with an `int` return while its
definition returned `_Bool` (only the low byte is defined). Sources: the four
headers `native_write_policy.h`, `net/net_manifest_type.h`, `catalog_checked.h`,
`bondgun_cache.h`; seven C files with their own `#include <stdbool.h>`; the
fast3d `gfx_api.h`; ENet (contained by `net/netenet.h`). Root fix on
`week-pd/multiplayer`: no project code includes `<stdbool.h>`; shared headers
spell boolean results `s32`; C files built without the precompiled header
include the new `port/include/pd_bool.h`; the five gfx headers keep a scoped
one-byte `bool` (push_macro, `_Bool` in C, keyword in C++, pop_macro);
`modmgr.c` stores 0/1 in two arrays it compares with `!=`. Every other `bool`
use in the 22 affected files was reviewed as 0/1-valued; none of them writes a
struct with `bool` members raw to disk or the network. Guards: the audit
(reports mixed sites, flips that reach project code, and leaking scopes) runs in
CI on `pd` and `pd-tests`; `tests/test_bool_layout_static.cpp` forbids
`<stdbool.h>` in project headers and C files. Verification: see the progress
log of `context/designs/in-flight/week-pd-multiplayer-plan.md`.

## 2026-11-14 B-1120 Two layouts of struct netclient break every listen-host match smoke (T-NETWORKING-011)

Severity high. Root cause, fix and guard are in source on `week-pd/multiplayer`.
`listen_host_match_smoke` failed 45/58 on the a8ab67f3 baseline client
(receipt `D:\PD2-wt\core\.claude\smoke-verify-runs\results-20261114T201305Z.json`):
the client joined and authenticated but `--host-autostart` never fired. A
log-only diagnostic (c18ae6ce) printed `remote_in_lobby=0` for 100 s after the
join (`results-20261114T202711Z.json`). Cause: `port/src/main.c` and
`port/src/smoke_harness.c` see project `bool` as 1-byte `_Bool` (pulled in by
`native_write_policy.h` before `net/net.h`) while every other file sees `s32`,
so they lay out `struct netclient`, `netpreservedplayer` and `netrecentserver`
differently and index `g_NetClients[]` at the wrong addresses. Proven by
preprocessing both files with their exact flags; an audit of all 650 client C
files found no other mismatched shared struct. Fix 0cc573d4: the flags in those
three structs are spelled `s32`. Guard: `tests/test_bool_layout_static.cpp`.
Class: SP-75. Probably also why the networked smokes that passed in August
regressed after `native_write_policy.h` was added to these files in October.
Runtime proof: with the fix, `listen_host_match_smoke` passes 58/58 (receipt
`D:\PD2-wt\core\.claude\smoke-verify-runs\results-20261114T205302Z.json`,
week-pd/multiplayer build at 0cc573d4, shared base data): autostart fires, both
players spawn, scores replicate, time-limit end, end screen on both sides.
Still to re-run against the fix: the reconnect smoke (its harness events used
the wrong client slot) and the other two-process smokes.

2026-11-14 week-pd/mods findings while building the in-game mod author (ids B-1140 to B-1159 and B-1230 to B-1249 are the MODS ranges for this week). None of these is fixed yet unless it says so.

B-1140 FIXED 2026-11-14 (d377380f): the parse loop is now `iniParseText` in `port/src/ini_parse.c` (no log, no catalog; `iniParseBuffer` is a logging wrapper) and a line inside a section that is not a comment, a section header or `key = value` is refused with its line number. `tests/test_ini_parse.cpp` links the real loop; its B-1140 case would not return on the old code. Full pd-tests green. Not run in the game. Was: high, found by reading: the shared INI parser hangs on a non-comment line that has no `=` inside a section. `port/src/assetcatalog_scanner.c` `iniParseBuffer`: the `if (!eq) { continue; }` branch does not advance `line`, and the line terminator has already been overwritten with NUL, so the loop re-reads the same line forever. Reachable from any typed archive descriptor or loose `.ini` the scanner, loader walkers, mod manager, packers or netdistrib parse. `tools/asset_archive_conformance.py` `KEY_VALUE_RE` accepts `key: value`, so a descriptor the offline validator accepts can hang the game at scan. No unit test covers the real parser: pd-tests links a different stub (`tests/test_mod_external_archive_static.cpp:133`). Planned fix: advance the line, and move the parser into its own source file so tests link the real one. Propagation check owed: `bodyHeadSourceReadIniBytes` already rejects such lines (it fails the file); other hand-written line loops not yet audited.

B-1141 OPEN, low, found by reading: the mod manager reads `mod.json` strings without decoding JSON escapes (`port/src/modmgr.c` `json_tok_string` copies the raw token), so a name containing a double quote or a backslash is listed with its escape backslashes, and a unicode escape is shown literally. The in-game packers and the mod author write correct JSON; the loader's display is what is wrong.

B-1142 FIXED 2026-11-14 for save and pack (`port/src/mod_asset_validate.cpp`: `modAuthorSave` and both `.pdmod` packers now run one in-game Validate that reads every public `.json`, `.gltf` and `.ini` member strictly, in embedded archives too, and runs the body, head and character readers; 1884 unit cases pass and the base sweep is unchanged at 308 of 349). Still open: the weapon and vehicle families have no content reader in it yet, and the enable path does not call it. Was: medium, proven by a unit run on 2026-11-14: release validation (`assetArchiveValidateFile/Bytes/Opened`) checks layout, forbidden members and references, not member content. A `.pdweapon` whose `behavior/settings.json` is `{ not json` passes it (`modAuthorSave` returned OK with that override and both validators accepted the result). `weaponGraphArchiveValidateRootFile` only checks that members exist. So a save or pack can succeed for an archive its loader then refuses. Planned fix: one in-game Validate that runs each family's real reader (weapon graph compile, strict body/head/character readers, vehicle physics and behavior JSON), called by save, pack and enable.

B-1143 OPEN, low, test tooling, reproduced twice on 2026-11-14: `pd-tests.exe > out.txt` (stdout to a regular file) exits 14 and writes 2 bytes to stdout; with stdout on a pipe the same binary passes all 1874 cases. `pd-tests.exe "[author],[clone]" > out.txt` passes (18 cases), `"~[author]~[clone]"` shows the failure, so it comes from cases older than this branch. Not investigated. Use a pipe (the wrapper and hosted CI do).

B-1144 OPEN, low, found by reading: `theme_archive_authoring.c` and `voice_archive_authoring.c` stage their result as `<name>.candidate.pdtheme` / `.pdvoice`, which still ends in a typed extension, so an interrupted save leaves a second archive the catalog scanner would pick up. The mod author stages as `<name>.pd<kind>.candidate` instead (`ASSET_ARCHIVE_CLONE_STAGING_SUFFIX`).

B-1145 FIXED 2026-11-14 by CORE (c18dce71 on `week-pd/extract-1`, merged here with `week-pd/extract-2` as 40552210; shared base 2 and 3 are re-extracted without the `mtllib` line). Rerun on `D:/PD2-wt/shared-base-3` at 40552210: arenas 47 of 47, all 349 base templates save as release-valid mods. Was: medium, proven by a local run on 2026-11-14 against the shared extraction (5,562 archives, client ee34aaf1): 41 of the 47 base `.pdarena` archives fail the in-game release validator. Each embeds a `.pdscenario` whose `collision.obj` starts with `mtllib scenario.mtl`, and no `scenario.mtl` member exists, so `assetArchiveValidateBytes(..., RELEASE)` reports "references missing internal file in collision.obj: scenario.mtl". The six that pass (attackship, deepsea, extra17, infiltration, mp_random_multi, mp_random_solo) are presumably the ones without that file. Effect for modding: a base arena cannot be used as a template (the rebuild refuses a source that is not release-valid) and cannot be packed. Owner of the fix is the extractor or the policy (CORE): either stop writing the `mtllib` line, write the material file, or stop treating it as a required reference. Reported in the week note. Evidence: `[base-sweep]` case in `tests/test_mod_author.cpp` with `PD2_BASE_TEMPLATE_DIR=D:/PD2-wt/shared-base/data/ntsc-final`: weapons 86 of 86, vehicles 1 of 1, characters 63 of 63, bodies 68 of 68, heads 84 of 84 save as release-valid mods; arenas 6 of 47.

B-1146 FIXED 2026-11-14 (c7d712d5), high, found by reading and proven fixed by a run: the Modding Hub Weapons creator saved every weapon in the retired root layout (root `behavior.graph.json` and `nested_payloads.json`, a `[references]` section, loose import folders, base ids kept on copied payloads), which the release validator and `tools/asset_archive_conformance.py` reject, and it enabled the mod without validating the archive. Save now hands the template and the edits to `modAuthorRuntimeSave`; the creator writes no archive itself. Evidence: `weapon_tool_create_smoke` PASS 26/26 on base 3 at 40552210 (`D:/PD2-wt/mods/.claude/smoke-verify-runs/results-20261114T235226Z.json`).

B-1147 FIXED 2026-11-14 (c7d712d5), high, found by reading and confirmed by the smoke's graph line: the creator read a template's behavior only from a root `behavior.graph.json`. Clean archives (every base weapon) keep it in `behavior/primary.graph.json`, `behavior/secondary.graph.json` and `behavior/shared-context.json`, so every base template was opened with a made-up single-shot graph and saved with it: the mod lost the template's behavior without a word. The three files are now composed into the editor document (`port/src/weapon_graph_editor_io.cpp`) and split back on save; a template whose behavior cannot be read or shown is refused. Evidence: `WEAPONMOD.DEBUG.CREATE.GRAPH nodes=4 links=2 primary=primary_action secondary=secondary_action` for `base:falcon2`.

B-1148 FIXED 2026-11-14 (c7d712d5), high, found by measuring the base data: the weapon graph editor kept each node's settings as text in 512 bytes (`PDGUI_WEAPON_GRAPH_PARAM_LEN`) and cut longer text short, which gives broken JSON; nodes past 48 and links past 96 were dropped silently. The largest base weapon function (`base:reaper` primary) needs 854 bytes, so real guns could not pass through the editor. The limit is 2048 and anything that does not fit is a load error. Settings are shown one per line with numbers in their shortest exact form instead of 18 digits.

B-1149 FIXED 2026-11-15 (8f0ac3a3) and seen in the game: `mod_author_weapon_play_smoke` rerun on shared base 3 (`results-20261115T010040Z.json`, still 20/21 for B-1151 only): the saved `weapon.ini` has `posx = 9`, `posy = -15.7`, `posz = -23.8`, `muzzlez = 2` and the screenshots show the gun held at the lower right instead of filling the view. Detail: `modAuthorSave` copies `posx`, `posy`, `posz` and `muzzlez` from the template's manifest into the saved descriptor unless the descriptor or the creator already gives them. Unit level only (new `modManifestReadNumber`; base sweep still 349 of 349); the play smoke has not been rerun since. Was: high, RUN 2026-11-14 (`mod_author_weapon_play_smoke`, known red 20/21, `results-20261114T215943Z.json`): a weapon mod made from a base template is drawn at the camera because it has no view position. The mod weapon adapter (`loaderPoolInstallPublicWeaponAdapter`, `loader_pool.c` about 375-447) takes `posx`, `posy`, `posz` and `muzzlez` only from `weapon.ini`; base archives keep them in `_meta/manifest.json`. Next (MODS): the mod author copies them into the descriptor when it saves from a base template.

B-1150 FIXED on dev by the crystal pistol work (landed as 361abab8; CRYSTAL PISTOL's week note of 2026-11-16 05:30 UTC lists it), in this branch since the merge of dev 841650c7; `mod_author_weapon_play_smoke` is rerun on the merged build. Was: OPEN, medium, same run: a mod weapon has no name in the HUD or the inventory (`SPAWN.WEAPON ... name=0`). The adapter sets none; the catalog has it in `ext.weapon.name`. Asked of CORE in the week note (crystal pistol ask 8).

B-1151 FIXED on dev by the crystal pistol work (landed as 361abab8; CRYSTAL PISTOL's week note of 2026-11-16 05:30 UTC lists it), in this branch since the merge of dev 841650c7; `mod_author_weapon_play_smoke` is rerun on the merged build. Was: OPEN, high, same run: a mod weapon whose function has `ammo_slot` 0 or 1 fires zero hitscan shots. The adapter sets no ammo definition and `bondgun.c` (about 2286-2300) clamps the shot count to a loaded count that was never set. `ammo_slot` -1 (unlimited) works. Every weapon made from a base gun template is affected. Asked of CORE (crystal pistol ask 1).

B-1152 FIXED on `week-pd/mods-v4`, RUN in the game 2026-11-17 03:18 UTC (MODS; turn q_20261117013646273_5a67705b, build 15837054, base 7: `mod_author_weapon_play_smoke` 22/22, and the log shows the mod's mesh in a slot of its own, `PDWEAPON.NESTED.MESH_SOURCE: owner=mod:weapon_smoke_falcon id=mod:weapon_smoke_falcon__mesh_model_falcon2_hi action=claimed filenum=2016`, where it used to claim 887, and the held model built from the mod's own `held_hi.pdmesh`; not on dev yet. What was written: the clone side: `asset_archive_clone.c` drops `source_filenum_symbol` from a cloned mesh's manifest, unit case `[b1152]`, and the weapon play smoke forbids the mod mesh taking file 887; mods saved before the fix still carry the symbol until they are saved again. The loader guard for those was written afterwards, 2026-11-17 03:40 UTC, READ with source pins `[b1152]` in `tests/test_public_mods_static.cpp`, not compiled or run when this was written: `weaponNestedPrepareMember` takes the owner's bundled flag, plans a non-bundled owner's mesh without a file number, drops the number its manifest resolved to and logs `keeps a slot of its own` once, so the mesh gets a private slot when it is bound; no fixture can prove it in a run, because an archive saved before the fix holds a copy of a base gun's mesh and cannot be committed), medium, cause READ: a weapon mod cloned from a base gun embeds a copy of the base mesh that claims the base mesh's file slot (887), so the base mesh was loaded in its place. Still seen on base 7 in the landing turn's `mod_author_weapon_play_smoke` log (`PDWEAPON.NESTED.MESH_SOURCE: owner=mod:weapon_smoke_falcon id=mod:weapon_smoke_falcon__mesh_model_falcon2_hi action=claimed filenum=887`, where the base rows say `action=kept filenum=887`). Cause: the cloned mesh's `_meta/manifest.json` keeps the base mesh's `source_filenum_symbol`; `loaderWalkerMeshSourcePlanManifest` (`port/src/loader_walker_mesh_source_plan.c:298`) resolves it to the base file number for a row that is new (the mod's own id), and the nested weapon registration (`assetcatalog_scanner.c`, the `has_mesh_source` block before the `PDWEAPON.NESTED.MESH_SOURCE` line) binds the mod's archive to that number. A mesh with no symbol takes a private slot there (`assetCatalogResolveModelPrivateSlot`), which is what a mod-owned mesh should get. Fix planned in two places: the mod author's clone drops `source_filenum_symbol` from a mesh manifest it moves out of the `base:` namespace, and the nested registration never lets a mesh of a non-bundled owner take a file number from its manifest (it gets a private slot, logged), so mods saved before the fix stop claiming base slots too. The mod vehicle path already does this (`meshSourcePreparePrivate`).

B-1153 OPEN, low, RUN 2026-11-15 (`maian_crystal_pistol_play_smoke`, `results-20261115T003916Z.json`): a `.pdmesh` that carries only `mesh.ini` and `model.gltf` (the Needler's needle, the crystal placeholder) loads, but the loader first asks for `model.mtl` and `model.nodes.json` and logs `ERROR: fsFileLoad: could not find file` for each, twice. Members the descriptor does not name should not be reported as errors. Not investigated; CORE's loader or the mesh compiler.

B-1154 FIXED by CORE on `week-pd/core` dcee4202 (not merged here yet; a cloned gun needs a base 7 mesh and a rebuilt clone), medium, seen 2026-11-15 in the same rerun's screenshots (MODS): the held Falcon 2 copy is drawn with a blue and white striped surface that does not look like the Falcon's own texture. Possibly the same cause as B-1152 (the cloned mesh and the base mesh sharing a file slot) or a texture reference that is not followed into the mod's namespace. Cause (CORE, 2026-11-16 01:30 UTC): not the clone. The base Falcon 2 shows the same stripes; every reflection-mapped model did (144 of base 6's 733 meshes). The ROM's display list sets the RSP texture scale (`G_TEXTURE`) before each texture load, the mesh exporter skipped it and the compiler used 0xffff, so each reflection repeated 32 times. `model.render.json` schema v4 adds a `texture` row (`scale_s`, `scale_t`) that the exporter writes and the compiler replays; base 7 carries the re-exported meshes.

B-1155 FIXED on dev by the crystal pistol work (landed as 361abab8; CRYSTAL PISTOL's week note of 2026-11-16 05:30 UTC lists it), in this branch since the merge of dev 841650c7; `mod_author_weapon_play_smoke` is rerun on the merged build. Was: OPEN, low, found by Mike's asset session (crystal pistol asset README, "Scale and frame"), not checked by MODS in a run: the Needler dev mod's held model is authored muzzle toward -Z, while native held models are authored muzzle toward +Z and `bondgun.c` turns every held model 180 degrees about Y, so the Needler is drawn backwards. Fix in `tools/build_needler_mod.py` (it negates z when it writes the held mesh), then regenerate the archive. Owner of the Needler: T-MODDING-009.

B-1156 OPEN, medium, RUN 2026-11-15 19:32 UTC (MODS, coordination hub): under Windows PowerShell 5.1 every hub command (`status`, `wait-turn`, `start`, `finish`, `health`) throws "The property 'Count' cannot be found on this object" while a running `game-runtime` item is past twice its ETA. `tools/CodexCoordination/CodexCoordination.ps1` `Test-ResourceBusy` has no pattern for the resource name `game-runtime` (its game pattern is `^(game|smoke|multiplayer|capture|editor)$`), so the `switch` falls to `default { @() }`, which outputs nothing, and `$patterns.Count` on `$null` is an error under `Set-StrictMode -Version 2` in 5.1 (PowerShell 7 gives 0, so `pwsh` callers see nothing). In both versions a dead `game-runtime` runner is never auto-reaped, because the unknown resource reports busy. Seen: the MODS waiter called the hub through 5.1, got this error on every call once CRYSTAL's smoke (alive, slow) passed 2x its ETA, counted the errors as waits and cancelled its own item (requeued in place; MULTIPLAYER, CORE and CRYSTAL call the hub with `pwsh`). Fix for the hub owner: `$patterns = @(switch -Regex ($ResourceName) { ... })` and `game-runtime` in the game pattern. Workaround: call the hub with `pwsh`. Propagation check: the only other `$x = switch` in the PowerShell tools (`devtools/build-headless.ps1` `$targets`) takes a validated value and is never read with `.Count`.

B-1157 OPEN (the vehicle swap part is fixed on week-pd/mods), medium, READ 2026-11-15 (MODS, while checking vehicle step V1 before its smoke, not seen in a run): a custom model slot (`MODEL_CUSTOM_START` to `MODEL_CUSTOM_END`) has no world scale. `g_ModelStates[slot].scale` is never set and stays 0, and `objInit` (`src/game/propobj.c` 2871) sizes every new object by `g_ModelStates[modelnum].scale / 4096`, so an object created with a private-slot model gets model scale 0 (not drawn, no collision size) unless its caller sets one. Forge sets 1.0 after creation (`port/src/forge/forge_runtime.c` 665, 744, 851); `--debug-swap-vehicle` now gives the mod vehicle the replaced hoverbike's slot scale (`src/game/setup.c`, logged as `VEHICLE.SWAP: modelnum N takes the world scale of modelnum 58 (409/4096)`). Not checked: graph projectiles whose mesh has a private slot are created through `func0f08ae0c` -> `objInitWithModelDef` -> `objInit` and then multiplied by the graph's projectile scale (`src/game/bondgun.c` 6184, 6265), which would keep 0; CRYSTAL reported its crystals in game, so where those get their size is the thing to check. General fix still open: a declared world scale for a custom slot (a mesh or owner key, or one default), which levels that place mod vehicles will need as well. Search: `rg -n "g_ModelStates\[[^]]*\]\.scale" src port`.

B-1158 FIXED AGAIN on week-pd/mods (MODS) 2026-11-16, high for smokes, first fix wrong (see the 07:04 UTC update), RUN 2026-11-15 22:34 and 22:38 UTC (`mod_vehicle_drive_smoke`, `maian_vehicle_drive_smoke`, both on base 3, receipts under `C:/Users/mikeh/Perfect-Dark-2/pd2-smoke/mods/receipts/`): `--debug-mount-bike` crashed the client (ACCESS_VIOLATION in `modelSetRootPosition` <- `chrMoveToPos` <- `bootDebugMountBikeTick`, `port/src/main.c`) on the first gameplay frame. The hook moves Player 0 beside the bike with `chrMoveToPos`, which sets the chr's model root, but Player 0's body model is made by the player tick (`playerTickChrBody`, `src/game/player.c`), not at stage load, so on the first frame `chr->model` was NULL. The hook had no frame settle, although `--debug-spawn-at`'s comment says it uses "the same gate --debug-mount-bike uses" (`lvframenum >= 4`). Fix: the hook waits for `lvframenum >= 4` and a non-NULL Player 0 model. Not caused by the vehicle swap: the swapped vehicle was created at the hoverbike's scale and the crash is on the player's chr; `vehicle_flow` (the same hook on the base hoverbike) is rerun as the control. Update 2026-11-16 07:04 UTC (MODS): the fix above was wrong for solo play, and it stopped `--debug-mount-bike` from ever firing on a campaign stage. A solo player outside a cutscene has no body model at all: the normal tick calls `playerRemoveChrBody` every frame (`src/game/player.c`, the TICKMODE_NORMAL branch), which frees it through `chrRemove` (`chr->model = NULL`), so the model wait never ended. Every vehicle smoke since 2026-11-15 22:38 UTC shows the hook armed, the bike created (`VEHICLE.SWAP`), Player 0 in walk mode with control, and no mount, on CI training and on Escape (receipts `20261116T0517*`, `20261116T064*`). My 05:33 UTC week-note reason (CI training's bike is in the hangar, outside the active props) was wrong too: the active list runs on into the paused list (`propActivate`), so the hook's walk sees every listed prop. Real fix: `chrMoveToPos` (`src/game/chraction.c`) skips the model root, the CHRINFO ground and `chrSetLookAngle` when the chr has no model (and no aibot); the player's position is the prop's and its facing is `vv_theta`, both still set. That also makes `--debug-spawn-at` and any `chrMoveToPos` aimed at a solo first-person player safe (the same latent NULL dereference). The hook keeps the `lvframenum >= 4` settle, drops the model wait, and logs once when it is ready but finds no free hoverbike. Proof: the three Escape vehicle smokes, requeued. Proven 2026-11-16 08:48 UTC (turn q_20261116070701327_eb4de76b, client d21bf9f0, base 3; receipts `20261116T0848*`, `20261116T0852*`, `20261116T0856*`): `--debug-mount-bike consumed ... result=MOUNTED` once in each of the three Escape vehicle smokes, no crash. CORE reviewed and approved the `chrMoveToPos` change (week note 07:31 UTC). The three smokes then failed on a step of their own (an on-foot wait right after the hover bed is let go, which the same-frame mount makes impossible); fixed in the fixtures (3617ffc8), not a game bug.

B-1159 CLOSED, not a bug (answered by CORE 2026-11-16 01:20 UTC: Escape starts in grab mode), was high for vehicle smokes, found by MODS 2026-11-16 00:30-00:45 UTC on shared base 3 with the week-pd/mods client at 0503fe0a (receipts under `C:/Users/mikeh/Perfect-Dark-2/pd2-smoke/mods/receipts/20261116T00*`): on Escape (`--launch-mission base:escape`), after the intro is skipped (the smoke's cutscene-skip assist, action 72), Player 0 never returns to MOVEMODE_WALK. The graph's `end_cutscene` runs at 00:48.71 (it calls `playerEndCutscene`, which sets walk mode for every player through `bwalkInit`), control is granted at 00:48.73, and for the next 130 s the smoke facts read `tick:1 normal:1 update:1 control:1 unpaused:1 alive:1 walk:0 bike:0`, so something after the skip takes the mode away again (not instrumented: no log line names the mode). Effect: `offline_gameplay_ready` never holds and `--debug-mount-bike` (which needs walk mode) never fires, so `vehicle_flow`, `mod_vehicle_drive_smoke` and `maian_vehicle_drive_smoke` all time out (9/20, 21/27, 23/29) with no mount. Not caused by the vehicle work: `vehicle_flow` drives the base hoverbike with no mod. `vehicle_flow` last passed on 2026-07-04 as the 10-assertion CI-training version (`--skip-intro`); the 2026-09-22 Escape rewrite (34b0dee3, 20 assertions) has no recorded pass. The window also lost focus at the first gameplay frame (`INPUTCTX: focus LOST`), which only flushes held input and does not touch the move mode. Unknown: whether a player who skips Escape's intro by hand can walk (if not, this is player-facing). Update 2026-11-16 02:17 UTC: on CI training (`--launch-mission 0x26`) the same skip assist ends the CI camera intro and walk mode does come back (`walk:1`), so this looks particular to Escape's intro (or its level script), not to skipping in general. CI training then forces Agent Select open: at 02:17 UTC because a fresh install has no agent, and at 03:58 UTC still, with the smoke agent loaded by `--launch-load-agent`, because CI training opens it until a game file is chosen there (`g_FileState`, `src/game/menutick.c`), then opens the main menu. So the vehicle smokes now stage the smoke agent profile, load it through Agent Select, close the main menu with Back (injected actions, which need no window focus) and only then wait for walk mode and the mount. Answer (CORE, 2026-11-16 01:20 UTC): not a bug. Area 51 Escape starts with the player pushing the hover bed, as the original does: right after the intro the stage's AI list runs `revoke_control`, `chr_grab_object` (the player, tag 0, `base:model_hoverbed`) and `grant_control`, so the move mode is `MOVEMODE_GRAB` and walk mode comes back only when the player lets go of the bed (the native `aiChrGrabObject` does the same). The smokes' `offline_gameplay_ready` wait and the `--debug-mount-bike` hook need walk mode, so on Escape a smoke has to let go of the bed first (Use), or accept grab mode (`bmoveSetMode(MOVEMODE_BIKE)` leaves a grab cleanly), or use a stage that starts in walk mode, as the mod vehicle smokes now do (CI training). `vehicle_flow` (Escape) needs the same change.

B-1230 FIXED on `week-pd/core-s5` by CORE 2026-11-17 (the extractor writes the base hoverbike's `pd.vehicle_graph.v1` graph, whose `og.vehicle.throttle_boost` holds the boost with its unit, `{ "value": 2400, "unit": "ticks240" }`, and `pd2.vehicle.physics.v3` physics without the boost keys, behind the meta export version `_vehiclegraph`; the runtime refuses a v2 behavior or v2 physics and conformance reports them; the typed example vehicle is v1 too). ANSWERED by CORE 2026-11-16 05:50 UTC (fix: the boost keys move into the vehicle graph with explicit units, physics v3 has none and v2 is refused after the base re-extract; MODS builds the vehicle graph runner on week-pd/mods), was OPEN for CORE (vehicle schema), low (authoring trap, no gameplay change), found by MODS 2026-11-15 in the vehicle survey, row filed 2026-11-16 at CORE's request: `physics.json` (`pd2.vehicle.physics.v2`) key `boost_time_ticks60` is counted in 240 Hz ticks, not 60 Hz. `hoverbikeUpdateMovement` (`src/game/propobj.c`, the full-throttle bonus) adds `lvupdate240` to `bike->maxspeedtime240` and caps it at `TICKS(boost_time_ticks60)` (NTSC: the value itself), and the bonus is `maxspeedtime240 * boost_speed / TICKS(boost_time_ticks60)`. So the base hoverbike's 2400 is a 10 s ramp, not 40 s (corrected 2026-11-16 per CORE: the 1800 first quoted here is the example template's, `template/tri_vehicle.pdvehicle`, with `boost_speed` 7.0; the base holds 5.0 and 2400, checked on shared base 7); an author who reads the name and writes 240 for 4 s gets 1 s. The game's behavior is the original's, only the key's name is wrong. Fix options (CORE's call, question 3 of `context/designs/modding/vehicle-graph-v1.md`): move the boost keys into the vehicle graph with explicit units (`{1800, ticks240}`), or a physics v3 that renames the key to `boost_time_ticks240` and reads v2 as is. Ends with the extractor task in `vehicle-graph-v1.md` (CORE's review 2026-11-16 09:58 UTC, rule 5): the base hoverbike's v1 graph and v3 physics behind a vehicle export version, then v2 refused at runtime and reported by conformance.
B-1231 FIXED on week-pd/mods (MODS) 2026-11-16, low (no shipped vehicle forbids dismounting): the PC vehicle exit key (`ACTION_VEHICLE_EXIT`, F) dismounted without asking the vehicle's behavior. `bbikeApplyMoveData` (`src/game/bondbike.c`) ran its directional dismount with no `assetRuntimeVehicleAllows(..., "dismount")` check, while the activate path (`bbikeHandleActivate`) has one, so a vehicle whose behavior says `allow_dismount: false` could still be left with F on PC. Found while routing both dismounts through the vehicle graph. Fix: the exit key's dismount (`bbikeExitToChosenSide`) runs through `event.vehicle.dismount_requested` for a `pd.vehicle_graph.v1` vehicle (`og.vehicle.dismount_seat`) and only with the permission for a v2 vehicle. Propagation: the activate path checks it; the network receivers apply the authority's committed state (`currentPlayerApplyHoverbikeState`) and rightly do not re-check; no other path dismounts.
B-1232 OPEN (MODS found by CORE's review 2026-11-16 09:58 UTC; the wire is MULTIPLAYER's), medium before the Maian vehicle goes into a networked match: a remote rider's vehicle boost never reaches the host. The move packet has no boost field, and a remote actor skips the input block in `bbikeApplyMoveData` (`src/game/bondbike.c`) that reads `ACTION_VEHICLE_BOOST`, so on a listen host a client's vehicle heats up and hides its crystals on the client while the host's copy stays cool and slower (corrections, and turret lockouts that differ). Older than the vehicle graph commits. The design (`context/designs/modding/vehicle-graph-v1.md`) has the host run the graph and clients predict no heat. Fix options: a boost bit in the move message (the host runs `boost_heat` with the client's input; MODS's preference), with the phase and toggled parts sent in the vehicle state for remote clients; or `boost_heat` on the host only with the phase in the vehicle state. Owner of the wire: MULTIPLAYER.
B-1233 FIXED on week-pd/mods (MODS) 2026-11-17, before it reached dev; high had it landed: every action added to the game made each player's saved bindings file unreadable. READ and measured, not seen in a run: `actionmap_profile::decode` (`port/src/actionmap_profile.cpp`) required exactly contexts times `ACTION_COUNT` rows ("The complete binding file is missing required rows."), `Store::initialize` then blocked the store, so the saved bindings were ignored (defaults in play) and every later save was refused ("The current binding file is invalid. Load a valid profile to replace it."). The real `%APPDATA%\perfectdark\input-bindings.ini` on the test PC, written by a dev build after the crystal pistol landing, has 12 contexts of 123 actions (last `GunRollRight`); `ACTION_VEHICLE_BOOST` makes `ACTION_COUNT` 124, so the first dev build after the vehicle landing would have refused it. Named profile files (`input-profiles/`) had the same fate. The crystal pistol's two actions did not show it only because every file from before them was already cut off at an unnamed action and went through B-1184's rewrite. Fix: a versioned document that lists, for every context, the same leading run of the action list and nothing after it is a complete file from an older build (saved names are positional, new actions are appended): `decode` accepts it, keeps its bindings and leaves the actions it could not know at their defaults, and reports how many it listed; `Store::initialize` applies it and writes it out complete (a failed rewrite leaves the older file, which reads the same way next time); `actionmapLoadBinds` logs `ACTIONMAP: bindings file from an older build (N of M actions per context): bindings kept, new actions take their defaults, file brought up to date`. Any other missing row (a context cut short, a hole, a missing context, contexts that stop at different actions) is refused as before. Tests: four cases tagged `[upgrade]` in `tests/test_actionmap_profile.cpp`. Not covered, and the same class: a NEW input context (V4b's turret context would be the thirteenth) still makes every saved file invalid, and a file from a NEWER build (an action this build does not know) is still refused; both need a decision before V4b adds its context. Pattern for whoever owns the input pillar's records: a persisted table keyed by a list that only grows must read an older complete file, or every release with a new entry discards the player's settings (no SP number taken here; CORE and MULTIPLAYER number theirs on their branches). A limit of any saved complete file, seen in the same real file: a default that moves does not reach it. It keeps `vehicle.VehicleExit=F,JOY1_X,JOY1_A,0` and `vehicle.VehicleHandbrake=SPACE,JOY1_A,0,0` (older defaults), so after the upgrade the new `vehicle.VehicleBoost` default (`LEFT_SHIFT,JOY1_A`) shares controller A with exit and handbrake in that profile until the Vehicle controls are reset to defaults or remapped; a new profile gets exit on X, handbrake on B and boost on A. The upgrade does not unbind or move anything on its own.
B-1234 FIXED and PROVEN IN GAME 2026-11-17 (MODS, `week-pd/mods-v4` c2050b8d7; the cause was MEASURED in the running game and so is the fix, at the end of this entry), medium: riding the Maian hover vehicle test mod dropped the frame rate on the test PC (i9-12900KF, RTX 3080; the game window is 2560 by 1440 on a USB virtual display). SYMPTOM, from the client logs of every retained vehicle smoke run (`VEHICLE: input` is logged every 30 frames while a player rides and `BMOVE:` every 120 frames throughout; `vehicle_fps.py` and `vehicle_fps_series.py` in the session scratchpad): the base hoverbike saved as a mod (`mod_vehicle_drive_smoke`) rode at 60 frames a second in every run, while the Maian vehicle on the same stage and route rode at 60 in three of the first nine runs, at 32 to 41 in five and at 16.8 in one, then at 28 and 19 in the landing's rerun (2026-11-17 02:36 UTC). Every slow run is at 60 until the player mounts and slow from the first second of the ride, and holds one speed for the whole ride. `lvupdate240` stops at 8, so below 30 frames a second the game falls behind real time (at 16.8 a 1 s boost ramp took 1.72 s); that is how it was found, when `maian_vehicle_boost_smoke` held boost for 5.5 s of real time, got 3 s of game time and no overheat. CAUSE, measured 2026-11-17 02:43 to 02:54 UTC (turn q_20261117013209684_748edab4, the landing build on base 7, three runs with a read-only sampler on the client's main thread: `rip_sampler.py`, `rip_report.py` and `rip_chain.py` in the session scratchpad, samples in `profile_b1234/`): the shot hit test. With one player the game asks every frame what the player is looking at (`lv.c`: `propFindAimingAt` -> `shotCalculateHits` -> `objTestHit` -> `func0f0849dc` -> `bgTestHitOnObj` in `src/game/bg.c`). That display list walk checks memory before it reads it, with `modelRodataIsReadable` (`port/src/model_rodata_guard.c`), which is a `VirtualQuery` system call: one for every vertex batch and, on the native path, one for every command as well. A compiled mod model has one vertex batch per triangle, so one full test of the vehicle made 5,829 system calls. In the two Maian rides the main thread went from 0.18 and 0.31 CPU-seconds per second on foot to 0.96 and 0.98 mounted, with 90% and 95% of its samples in the one chain `ntdll` <- `modelRodataIsReadable` <- `bgTestHitOnObj`: about 30 ms and 48 ms of every frame, at 29 and 20 frames a second. The base bike's model, walked on the native path, showed the same chain in 35% of samples during its ride and 36% before the mount, about 3 ms a frame; it held 60 only because the model is small. No power throttling was reported and the load stayed on performance cores. The guard's own header estimated 100 ns a call and about 100 calls a tick; in this client a call costs several microseconds and the walk made thousands. Why rides differed in speed is NOT measured; the likely reason is that a test walks parts only until the ray hits one, and walks nothing when the ray misses the model's box, so the cost depends on where the rider's view points. FIX (c2050b8d7): every check stays and the repeated questions go. `model_rodata_guard.h` gains a span (`model_rodata_span_t`), the run of committed, readable pages a walk's last query found, and `modelRodataIsReadableInSpan`: a range inside the span is answered with two compares, any other range is asked of the operating system as before and its run joins or replaces the span. Both hit-test walkers (`bgTestHitOnObj` and `bgTestHitOnChr`) keep one span on their stack for the native per-command check and the vertex batch check, so a walk asks once per run of readable memory. The span lives for one walk only, so memory freed or re-protected between walks is asked about afresh. The probe also now refuses a range that wraps past the top of the address space, which it used to call readable. Tests: `tests/test_model_rodata_guard.cpp` (`[model-rodata-guard]`, four cases over pages laid out for the purpose: readable, no access, never committed; one shows that a span answers from memory, which is why it must not outlive its walk); the two source pins on the walkers in `tests/test_asset_native_source_contract.cpp` follow the new call and also require the span's declaration. PROPAGATION: the two hit-test walkers were the only loops over this probe that run every frame. The model relation updates in `src/lib/model.c` probe once per node (2% to 3% of the samples; left alone). `extractGfxTris` in `src/lib/meshcollision.c` probes once per command while a prop's collision mesh is built, once per prop and not per frame (left alone; told CORE, whose file it is). TO VERIFY: a game turn that builds the fix and rides both vehicles with the sampler attached; expected 60 frames a second and the chain gone from the samples. This is engine code in CORE's area, changed on MODS' branch because the vehicle found it; CORE was told in the week note with the numbers. Ruled out earlier, kept for the record: sharing vertices in `build_vehicle_mesh.py` (the model compiler writes three vertices and one vertex load per triangle whatever the source shares, so the 17,487 vertices are the model's 5,829 triangles), the v1 graph's tick (it fires once per update), other sessions' builds, and the screen saver (`matrix.scr` ran through runs of every speed). `maian_vehicle_boost_smoke` keeps its 10 s boost hold; that is the test's tolerance for a slow machine, not part of this fix. PROVEN 2026-11-17 11:48 to 12:08 UTC (turn q_20261117080550197_5ee34c8d, client 0f329f66, shared base 9, the same sampler attached; `vehicle_fps_today.py` in the session scratchpad): the Maian vehicle rode at 60 frames a second in all three of its runs (30 frames per 0.50 s between the rider's witness lines: 60.0 as the driver in each, and 58.8 to 61.2 over 13 intervals as the gunner), and so did the base bike's mod. Over the 17 s mounted in `maian_vehicle_seats_smoke` the main thread spent 87% of its samples in the frame limiter's sleep and 0.8% in the hit test chain (it was 90% and 95%); while riding it used 0.12 CPU-seconds per second (it was 0.96 and 0.98).
B-1235 FIXED in code 2026-11-17 (MODS, week-pd/mods-v4; READ, hosted CI compile only, not run in the game), low until Forge levels exist: a mod arena's `load_mode` was read as a number (`assetcatalog_scanner.c`, the `ASSET_ARENA` case, `iniGetInt`) while `arena.ini` carries the mode's name, as the extractor and the mod author write it (`romextract_pdarena.c`: `load_mode = ARENA_LOADMODE_PLAYABLE` or `ARENA_LOADMODE_CANVAS`). A name read as a number is 0, so every mod arena was playable, and a mod level made from a canvas template (a campaign stage used as a build canvas, B-254) would have run its mission scripts, characters and cutscenes. Base arenas were never affected: they register through the loader pool, which resolves the name. Found by the Forge F1 survey (`forge-mode-scope.md`, item 3). Propagation check: the mod distribution's registrar (`port/src/net/netdistrib.c`, the arena case of a mod received over the network) read it the same way. Fix: one helper, `catalogArenaLoadModeFromIni` (`assetcatalog_scanner.c`), reads the two names and still reads a number, and both readers use it. Proof so far: source pins in `tests/test_public_mods_static.cpp`; the behavior waits for a Forge level made from a canvas template.
B-1236 FIXED and PROVEN IN GAME 2026-11-17 (MODS, `week-pd/mods-v4`; found by READING the code and the client log while checking B-1234's neighbours; proof at the end of this entry), medium: a compiled model's part with more than 2,044 triangles had no collision for the triangles past that count. `extractGfxTris` (`src/lib/meshcollision.c`), which builds a prop's collision mesh from its model's display lists when the prop is created, read at most 4,096 commands of one list and stopped without a log line. The cap is the guard for a native list, which has no recorded length. The mod asset compiler writes two commands per triangle (a vertex load and a triangle), so the cap ended every compiled part at its 2,044th triangle. Seen in every Maian vehicle run on record: the client log says `MESHCOL: extracted 5192 local-space triangles from model` for a model of 5,829 (the hull part is 2,681 triangles; the other four parts are 600 to 874 and were whole). The missing 637 are the hull's last triangles in file order. What a player would have seen: movement and the jump's ray tests against the vehicle passing through that part of the hull (not confirmed in play; nobody reported it). FIX: a list the compiler generated is read to the length the compiler recorded for it (`modAssetCompilerGeneratedGdlBytesRemaining`, the same bound the shot hit test uses); the 4,096 cap stays for native lists under a name (`MESH_EXTRACT_NATIVE_COMMAND_CAP`); and a list that does not end within what the walk may read is now logged (`MODEL.RODATA.MISS: site=MeshExtract.GfxEnd`, reason `generated-list-without-enddl` or `native-command-cap`) and counted in the `MESHCOL: skipped` summary, so a cut-off mesh can no longer be silent. Tests: `[b1236]` in `tests/test_vehicle_model_parts.cpp` (the hull is longer than the cap; the source pins); `maian_vehicle_drive_smoke` now requires `MESHCOL: extracted 5829 local-space triangles from model`. PROPAGATION: no other runtime display list walker has a fixed command cap that a compiled list can exceed (the shot hit test bounds compiled lists by their recorded length and caps native ones at 65,536; the 8,192 caps in `romextract_pdmesh.c` and `romextract_pdarena.c` read native ROM lists at extraction). TO WATCH in the next game turn: whether any base model's native list reaches the cap, which would now show as `native-command-cap` lines; none is expected (N64 lists are short). The file is CORE's; CORE was told in the week note. PROVEN 2026-11-17 (turn q_20261117080550197_5ee34c8d, client 0f329f66, shared base 9): all three Maian runs log `MESHCOL: extracted 5829 local-space triangles from model` (it was 5192), and none of the turn's eleven client logs has a `MeshExtract.GfxEnd` line, so no base model's native list reached the cap either.
B-1237 FIXED and PROVEN IN GAME 2026-11-17 (MODS, `week-pd/mods-v4` 2a193d51; found by an independent READ of the new Forge code; proof: the screenshot of `forge_session_save_smoke` in turn q_20261117041043360_545fad61 shows the mode badge, the grid cue, the reticle, the camera readout and the level file's markers while the camera flies), medium for anyone using Forge: since 2026-05-01 the Forge HUD has drawn nothing while the editor camera flies. `pdguiForgeHudRender` (`port/fast3d/pdgui_forge_hud.cpp`) returns early when `pdguiIsActive()` is true, meant as "a menu is open" (the line is from 2026-04-25). On 2026-05-01 commit 10f29f26 gave the editor an input context of its own, `g_CtxForgeEditor`, pushed for the whole of FREEFLY (`forgemode.c`), and `pdguiIsActive()` is "the top context is not gameplay". So on every FREEFLY frame the HUD returned before its mode badge, and the reticle, the ghost preview, the grid cue and the camera's position and angle readout below it were never reached; only the playtest mode's HUD was drawn. What a player saw: no reticle and no coordinates while editing. Nobody reported it. FIX: the HUD's test now leaves out the editor's own context, the same exemption `gameplayInputSuppressed` has had since 10f29f26 (`inputctx.c`). The new world markers of the level file (`forgehudDrawLevelFile`) sit below that line and would never have been drawn either. PROOF, once a game turn runs: the screenshot `forge_session_save_smoke` takes should show the badge, the reticle and the readout with the markers. PROPAGATION (read 2026-11-17, every caller of `pdguiIsActive()`): one more draws nothing while Forge flies for the same reason, the achievement toast (`pdgui_achievement_toast.cpp`), which does no harm in an editor and is left as it is. The interact prompt is hidden too, which is right there. The rest mean "keep the game's own input away" (`input.c`: pad sticks, mouse deltas, raw keys, the mouse lock and the cursor) or "ImGui may take navigation" (`pdgui_backend.cpp`), and for those the editor's context counting as an overlay is what is wanted: the editor camera reads its input through the action map (`actionAxis`, `actionHeld`), which has had its own exemption for this context since 10f29f26. One consequence, by reading, not a fault of this entry: the mouse does not turn the editor camera (its deltas are zeroed while the editor's context is on top); the right stick and the bound keys do.
B-1238 FIXED and PROVEN IN GAME 2026-11-17 with B-1240 (MODS; MEASURED the same day: the reading was right about the axes and the run showed a larger fault beside it, B-1240, which is why this entry's own test, `ahead` at the middle, failed). The move and placement axes now are the view's: forward (-sin t, ., cos t), right (-cos t, 0, -sin t) in `forgeUpdateFreefly`, `forgePlaceUpdate` and `forgeHeldUpdateFromCamera`. Proof pending with B-1240's. ORIGINAL ENTRY (found by READING, not seen in play; do not fix before a run measures it), medium if the reading is right: the Forge editor camera's own move and placement math reads its yaw mirrored. The camera's yaw is the player's turn angle t (`forgeApplyFreeflyToPlayer` writes `vv_theta` from it), and a player with turn angle t faces (-sin t, 0, cos t) (`bondmove.c`: `bond2.unk00.x = -vv_sintheta`, `.z = vv_costheta`; `player.c` starts a player with `vv_theta = 360 - atan2(look.x, look.z)` so that this is the pad's look). The view's right is up cross back (`mtx00016874`), which for that forward is (-cos t, 0, -sin t). But `forgeUpdateFreefly` (`src/game/forgemode.c`) moves the camera along forward (+sin t, ., cos t) and right (cos t, 0, -sin t), and `forgePlaceUpdate` and `forgeHeldUpdateFromCamera` (`port/src/forge/forge_core.c`) put the ghost and the held object along the same (+sin t, ., cos t). Both are the true axes mirrored in X. If so: facing north or south, forward and back are right and strafing left and right is swapped; facing east or west, strafing is right and forward and back are swapped; in between, movement slides off the way the camera looks. The older B-255 trace in the same function (a one-shot log of yaw, forward and right on the first move input, added 2026-04-25 "so Mike can verify in playtest that the look-relative transform is reaching the position update") suggests something of the kind was felt. NOT FIXED on purpose: the reading has not been checked against the running game, and a wrong correction would break flying. MEASUREMENT: `--debug-forge-session-save` turns the camera to yaw 217 and logs `FORGE.MARKERS.PROBE`, whose `ahead` point lies along (-sin t, 0, cos t) and whose `other_reading` point lies along (+sin t, 0, cos t); `forge_session_save_smoke` requires `ahead` at the middle of the picture. If that line passes, the reading is right and the fix is two sign changes in each of the three functions; if it fails, this entry is wrong and so is the facing the Level File section gives a new start (which uses the same reading). The Level File section does not depend on the mirrored math: it places at the camera's position, not ahead of it. PROVEN 2026-11-17 12:10 and 12:24 UTC (turn q_20261117080550197_5ee34c8d, client 0f329f66, shared base 9): with the camera at yaw 217 and 20 degrees down and the forward action held for 1.2 s, `FORGE.FLY.PROBE: moved=(407, -246, -540) distance=720 along_look=1.000 along_right=0.000`, on Complex (`forge_session_save_smoke` 35/35) and on the Blank Room (`forge_blank_room_session_smoke` 38/38) alike. The same run shows the injected forward action is read while the editor camera flies and the game window has no focus.
B-1239 FIXED and PROVEN 2026-11-17 (MODS, `week-pd/mods-v4` ff231ebb; FOUND BY A RUN, the first run of the base-level sweep in a game turn, shared base 8), low: in the Forge level document, a player start added to a level that has none and then removed did not give back the file that was read. `spawns.json` came out with a line of spaces inside its empty `"rows"` list. 46 of the 132 base archives the sweep reads have no start (7 arenas and 39 scenario archives, among them the unused test levels and `mp_grid2` and `mp_grid6`), and each failed the sweep's one check `text == what was read` after an add and a remove (pd-tests `[forge-level-doc]`: 39,133 of 39,179 assertions passed, 46 failed, all at that line). CAUSE: the first row added to an empty list re-lays the list out (a new line and indent after `[` and before `]`), and `Table::remove` did not undo that when the last row went. EFFECT: none on the game, the text is valid either way; what broke is the document's own rule that an edit taken back leaves the file as it was, so the member counted as changed and would have been written. FIX: a list read with no rows that has none again gives back the text that was read (`Table::text`, `startedEmpty`). A list read with rows that loses them all keeps its rows' layout. Unit case "A start added to a level that had none and taken out again leaves the file as it was read". PROOF PENDING: hosted CI on ff231ebb and the sweep in the next game turn. PROPAGATION: `objects.json` and `setup.fields.json` use the same table code, but nothing removes an object row (objects are left out by a flag), so only the spawns list can go back to empty. Why hosted CI did not find it: the sweep needs the player's own extracted levels, which CI does not have; the unit fixtures all had starts. PROVEN 2026-11-17 11:45 UTC (turn q_20261117080550197_5ee34c8d, client 0f329f66, shared base 9): the sweep that found it passes on every base level (`[forge-level-doc]` with `PD2_BASE_TEMPLATE_DIR` set: 39,397 assertions in 33 cases).
B-1240 FIXED and PROVEN IN GAME 2026-11-17 (MODS, `week-pd/mods-v4`; FOUND BY A RUN: the projection probe of `forge_session_save_smoke`, turn q_20261117041043360_545fad61, client 13dd514d, shared base 8), high for anyone using Forge: the editor camera's yaw and pitch do not reach the view. The camera flies (its position is the view's), its readout shows its angles, and it moves along them, but the picture keeps looking one way. MEASURED: with the editor's yaw set to 217 and its pitch to 0 the probe logged `ahead=(-0.146, 0.500) right=(0.142, 0.500) up=(-0.146, 0.068) behind=hidden other_reading=hidden native=320x220 camera=(-3817, 159, 349)`. Solving those numbers for the view's own axes gives a view along +X (forward (1.000, 0, 0.005)), 53 degrees off what yaw 217 means by the game's reading and behind what it means by the mirrored one. The screenshot agrees: the weapon pickup `setup_0000`, which is 1,940 units along +X and 169 above the camera, is marked just left of the middle and 5 degrees up, where a +X view puts it. So the markers' projection is right and the view is not the editor's. CAUSE (READ): the flying camera holds the player in the cutscene move mode (`forgeSetFreeflyMode`: `bondmovemode = MOVEMODE_CUTSCENE`), and `forgeApplyFreeflyToPlayer` writes the editor's yaw and pitch into `vv_theta` and `vv_verta` each tick. Nothing turns those into the view there: the look vectors a view is set from (`bond2.unk1c`, `bond2.unk28`) are refreshed by `bmoveUpdateHead` in the walk, bike and grab ticks, and `bcutsceneTick` is empty; and since Fix 5 of 2026-05-01 gave the observer a body, `playerTickThirdPerson` (`player.c`) overwrites `vv_theta` from the character model's own angle every frame and sets `vv_verta` to 0 when the player's body is shown. Which of the two froze the view in this run was not separated; by either, the editor's angles do not reach it. Before Fix 5 the body had no model, `chrGetInverseTheta` answered from `vv_theta`, and the yaw (never the pitch) may have survived that overwrite, which would be why turning seemed to work in April. Nobody has looked through the editor camera since 2026-05-01: B-1237 (no HUD while flying) dates from the same day and was not reported either. FIX: the editor owns the view while it flies. `forgeCameraOverride` (`forgemode.c`) gives the camera's position and the up and look vectors of its yaw and pitch, by the game's own reading of a turn angle (t faces (-sin t, 0, cos t), `bmoveUpdateVerta`), and `player0f0c1840` (`player.c`), which every way of setting a player's view passes through, uses them in place of what it was handed. The move and placement axes are made the same (B-1238), so the camera flies where it looks and the ghost stands where it looks. PROOF PENDING: the probe now turns the camera to yaw 217 and 20 degrees down and requires the point along that look at the middle of the picture, one along the camera's right right of it and one along its up above it; it also logs the player's own `vv_theta` and `vv_verta`. Queued in q_20261117080550197_5ee34c8d. RELATED, read on the way and fixed in 9107041f: with a keyboard and mouse the camera could not be turned even in principle (the aim axis is the right stick or the aim keys, and the editor's sidebar takes three arrow keys); hold the right mouse button over the world now turns it. PROPAGATION: the playtest mode of Forge (NORMAL) walks with the ordinary walk tick and is not affected. Any other code that parks the player in the cutscene move mode and expects `vv_theta` to steer the view would have the same fault; the observer camera of multiplayer is MULTIPLAYER's to check. PROVEN 2026-11-17 12:10 UTC (turn q_20261117080550197_5ee34c8d, client 0f329f66, shared base 9): the same probe, now with the camera turned to yaw 217 and 20 degrees down, logs `ahead=(0.500, 0.500) right=(0.646, 0.500) up=(0.500, 0.240) behind=hidden`, with `player_theta=217.0 player_verta=-20.0`: a point ahead of the camera is in the middle of the picture, one to its right is to the right, one above is above, one behind is not drawn. The same numbers on the Blank Room.
B-1241 FIXED in code 2026-11-17 (MODS, `week-pd/mods-v4`; the new place to drive is NOT run yet; FOUND BY A RUN, turn q_20261117080550197_5ee34c8d, client 0f329f66, shared base 9; a fault of where the vehicle smokes run, not of the vehicle and not of the mission), medium for the vehicle's proofs: `maian_vehicle_boost_smoke` (38/48) and `maian_vehicle_drive_smoke` (37/38) fail on Escape because the mission now ends the ride. Since dev's mission and AI graphs (48667628, in this branch from 22cd4a21) Escape's own script runs as written. From where `--debug-mount-bike` mounts the vehicle, about 0.4 s after it starts to move the log has `MISSION.GRAPH: event.objective.status_changed 'base:mission_escape' objective=2 ... status=complete`, and 0.8 to 0.9 s after that a cutscene loads (`base:animation_cut_tra_mid_cam`), the cutscene controls open and the player is out of the vehicle (`VEHICLE: dismounted player=0`, with no exit input). In the boost run that was 0.6 s after boost was pressed: one phase line (`cool -> ramping`, `ground=14.73`), no overheat, no black crystals, no recovery, so ten checks miss. In the drive run the steering input came 0.4 s after the dismount. `mod_vehicle_drive_smoke` passes (32/32): the objective completes there too, and its script ended before a cutscene began. `maian_vehicle_seats_smoke` passes (56/56): its seat checks come before its last drive, which the cutscene also ends. On 2026-11-16 the same two smokes ran twenty seconds on Escape with no cutscene (46/46 and 34/34). What the failed runs still proved is B-1234 and B-1236. FIX, to do: a place to drive that has no mission. The Blank Room shows an original level loads and plays, so the vehicle smokes move to an original level with a vehicle placed in it; that needs a level to be able to place a vehicle (the level document makes weapon places and ammo crates so far), which the Maian arena needs anyway. Until then the Escape smokes prove the mount, the seats, the frame rate and the collision mesh, and not boost, heat or steering. FIX WRITTEN: a level can now place a hoverbike (`FORGE_LEVEL_NEW_HOVERBIKE` in the level document: the 63-row record all five base hoverbikes share, `base:model_hovbike`, their three flag words; New Hoverbike Here in Forge's editor), `--debug-forge-level-vehicle` adds one to the scripted level edit 600 units in front of the first start, and `maian_vehicle_room_drive_smoke` and `maian_vehicle_room_boost_smoke` run the Escape smokes' inputs and checks on the Blank Room with that hoverbike, in a Combat Simulator match with no simulants, failing on any dismount. The Escape smokes stay for the mount and the seats. FIRST RUN 2026-11-17 (turn q_20261117134802790_ef485e13, client c88a828f, shared base 9): the new place works up to the mount (the hoverbike made from nothing loads on the Blank Room and takes the Maian vehicle's model, `VEHICLE.SWAP: hoverbike modelnum 58 -> 441`), and both room smokes failed there, before any driving (42/50 and 43/60), because the debug mount hook could not place the rider in view: B-1243, fixed. Rerun in the next turn.
B-1242 FIXED in code 2026-11-17 (MODS, `week-pd/mods-v4`; FOUND IN THE PICTURES of turn q_20261117080550197_5ee34c8d and READ in the code; not run), low: the Forge editor's control legend (the pills at the bottom right, `port/fast3d/pdgui_forge_hud.cpp`) named actions that do nothing while the editor camera flies, so two of its four rows read "Unbound" for every player. Its third row named the menu's tab keys (`ACTION_MENU_TAB_PREV`/`NEXT`), which are not live in FREEFLY, where the editor cycles its tabs with `ACTION_FORGE_TAB_PREV`/`NEXT`; its fourth named use and cancel-use (`ACTION_USE`, `ACTION_CANCEL_USE`), which `actionIsBlockedInFreefly` blocks, where the editor places with `ACTION_FORGE_SIDEBAR_ACTIVATE` and cancels a placement with `ACTION_FORGE_PLACE_CANCEL`. The pill text comes from the binding the resolver finds active (`actionmapGetActiveBindingVk`), and a blocked action has none. FIX: the rows name the editor's own actions, all bound in `g_ImcForge` (`setupForgeDefaults`), and the fourth row also shows the editor's show/hide key (`ACTION_FORGE_SIDEBAR_TOGGLE`). Test: `[forge][editor][static]` in `tests/test_editor_tool_hotkeys.cpp` (each pill names the editor's action in both the measuring and the drawing list, none names the old actions, and each is bound in `setupForgeDefaults`). NOT A BUG, for the record: in a smoke the first two rows read "Unbound" too, because the smoke's game window never has keyboard focus and `gameplayInputSuppressed` reports gameplay actions as suppressed while focus is lost; a player's focused window shows their keys.
B-1243 FIXED in code 2026-11-17 (MODS, `week-pd/mods-v4` e4532d24; FOUND BY A RUN: the first run of `maian_vehicle_room_drive_smoke`, turn q_20261117134802790_ef485e13, client c88a828f, shared base 9; a fault of a debug hook, not of the game), low (test-only): `--debug-mount-bike` could not put the rider on a vehicle the camera can see. On the Blank Room with the hoverbike the level edit made, everything up to the mount worked (`FORGE.LEVEL.EDIT: made hoverbike setup_0002 at (250, 50, 0) facing (-1.00, 0.00, 0.00)`, the level reloaded with `pads=12 spawns=9 objects=3`, `VEHICLE.SWAP: hoverbike modelnum 58 -> 441`, the match started with no simulants), and two seconds into the match the log said `BOOT: --debug-mount-bike placement failed for Player 0`; the smoke failed 42/50 with no `VEHICLE: mounted`. CAUSE (READ): the hook moves Player 0 beside the vehicle with `chrMoveToPos`, which asks `chrAdjustPosForSpawn`, and that refuses any place a player's camera can see unless the chr carries `CHRHFLAG_WARPONSCREEN`. On Escape the place beside the bike was out of the player's view when the hook ran; in the Blank Room, one open space with nothing between a start and the vehicle, it is in view whenever the player looks toward the vehicle, as in this run. FIX: the hook sets `CHRHFLAG_WARPONSCREEN` around the call and puts the flag back as it was after, as the game's own `chrSetPos` does; the volume test still keeps the rider out of walls and out of the vehicle. PROOF PENDING: the room drive and boost smokes in the next game turn. PROPAGATION: the one other debug placement hook in `main.c`, `--debug-spawn-at` (`bootDebugSpawnAtTick`), passes `chrMoveToPos`'s `force` argument, which in `chrAdjustPosForSpawn` allows a place in view (and also skips the wall test), so it has no such refusal; the mount hook keeps the wall test on purpose and so takes the flag instead. The rider's place is 150 units to the vehicle's side, and the eight places `chrAdjustPosForSpawn` tries when the first is refused are 60 units around it, so they fall in view together unless the first is near the edge of the picture. PROVEN 2026-11-17 (turn q_20261117154917611_f73cf2e8, client de514e91, shared base 9): `maian_vehicle_room_drive_smoke` 50/50 and `maian_vehicle_room_boost_smoke` 60/60 mount on the Blank Room and ride (boost to full, overheat with the crystals black, recovery, the turret lock set and cleared), which also closes B-1241.
B-1244 OPEN 2026-11-17 (MODS, `week-pd/mods-v4`; FOUND IN THE PICTURES of turn q_20261117154917611_f73cf2e8, client de514e91, shared base 9; cause NOT known yet), medium for vehicles in original levels: riding the Maian vehicle in the Blank Room, the rider's view draws the floor and not the walls or the ceiling. `maian_vehicle_room_drive_smoke`'s picture 1.8 s into accelerating shows the tiled floor up to a straight far edge about 6 degrees below the middle of the picture and dark blue above it, where the west wall and the ceiling should be; its steering picture and all four of `maian_vehicle_room_boost_smoke`'s show only dark blue and the HUD (with the player's held pistol drawn). A plain Combat Simulator match on the same room (`forge_blank_room_smoke`, same turn's base) draws the walls, the ceiling, a pillar and a simulant. Both smokes PASS: they check inputs, phases and the ride, not the picture. MEASURED so far: the renderer accepted the scene as usual (`SCENARIO.RENDER: rendered native source scene ... vertices=384 groups=1 textures=1`, the whole room in one draw); the driver's eye is part 160 at (0, 420, 450) model units on a model drawn at the base hoverbike's world scale (409/4096), about 42 units up and 45 ahead of the vehicle; in the boost run the ground speed fell from 26 to 0 about 1.2 s into full boost, where the vehicle reaches the west wall at that speed, so the wall's collision stopped it. NOT KNOWN: where the camera is and which room it is in while riding. Floor drawn and walls not, in the same draw call, rules out a missing room; a camera outside the room, a view matrix that turns the faces' winding round, or a clip distance are candidates, and none is checked. NEXT: a witness written for the next turn, `VEHICLE: view player=... camera=(x, y, z) camera_room=... vehicle=(x, y, z) vehicle_room=... floor_room=...` every half second beside the input line (`bondbike.c`), and then the rider's camera code (`bbikeTickGunner`'s driver counterpart and `player0f0c1840`).

2026-10-26 gun source contract correction following N0828 audit. asset_source_debug defaults ASSET_NONE; its source-only predicate is debug-selected, so the old non-FileProvider return0 allowed normal legacy model loading. fileProviderPath(nonfile)=NULL and modelSourceResolvePath(NULL) rejects; no supported public source can use that branch. Owned gunSourceRouteSource now fails unsupported/non-external requests and all exits return1; retained and supported external routes preserved. This intentionally supersedes original donor continuation under c3844/public-source constraint, not a compatibility claim. Native legacy block untouched but now unreachable through source routing. Updated source-route donor fixtures and added one explicit unsupported-provider case UNRUN. Independent review found no defect. Guard q_20261026205529526_c939af23/diff PASS; other8 pins and ORACLE suffix unchanged. 531 bindings/98 programs;47 whole entries/146 authored cases sinceOct23;zero accepted/complete conversions. No compiler/gameplay execution. Next audit/remove unreachable legacy MODEL/TEXTURES/DLS machinery and obsolete state producers, preserving source artifacts; public graph source/dependencies/ALL closure remain open. Latest .claude/gameplay-gun-source-closed-20261026/source-receipt.json. N-0829

2026-10-26 pre-conversion TEXTURES root repair: persistent nexttexturetoload now increments before the three-entry budget return, removing the documented third-entry revisit. Both loadable and resident/skipped entries advance; exact multiples3 retain existing next-tick DLS transition. Only this statement/order/comment changed; before/after native donor artifacts retained. Source review confirms fix by inspection. Static guard q_20261026204705964_1b110bb3 and diff PASS; other9 source pins unchanged. Regression requirements authored, no new executable cases this checkpoint; 525 bindings/97 programs,47 whole entries/142 authored cases unchanged. No compiler/gameplay execution; not runtime validated. Next graph conversion must preserve uintptr_t texture/pointer union full unsigned width, s16 numtexconfigs, u32 cursor, captured model/player and fresh texture-array access after texLoad. Legacy MODEL/DLS/dependencies, public graph source and ALL closure remain open. Latest .claude/gameplay-texture-cursor-fix-20261026/source-receipt.json. N-0826

2026-10-26 loader storage-width source repair discovered during GUN conversion. uintptr_t capacity targets addressed s32/u32 fields on x64; wide writes could overwrite adjacent master/gun/loadfile state. Queue cursors now u8** and capacities u32*, with matching API/static signatures, all4 production callers and loader pointer conversions; signed hand capacity uses corresponding unsigned alias. Field widths/layout and prior dirty header/CMake content preserved. N-0815/0816 narrow claims. Two actual gunctrl storage/signature regression cases authored UNRUN and connected to explicit pd-tests source list. Independent source review found no concrete defect; guard q_20261026200645567_7fcc32bc PASS; scoped diff and target-list readback PASS. Existing core/parser/fixture source pins and ORACLE07 suffix unchanged. No compiler/gameplay execution. Source repair is NOT runtime validated or a new conversion. Whole entries46, authored cases114, accepted/complete0; bindings488/programs89. Master GUN/HANDS/CARTS/parent and all gameplay/public source closure remain open. Latest .claude/gameplay-loader-target-width-20261026/source-receipt.json. N-0817

2026-10-26 N-0805: SwitchParentHost widened native control bitfields/signed bytes, use timer and disguised bit. Corrected fixture types and removed impossible dual2 cases after types.h audit. Production host already used native types. Previous raw-dual fixture claim superseded; all affected tests remain UNRUN. .claude/gameplay-hand-config-20261026/source-receipt.json.

2026-10-26 N0796 admission follow-up: full production loading trace confirms no downstream span validation. Public parser now rejects non-backward packed targets, including nonzero high word; source-reviewed and guard PASS. Two focused cases authored UNRUN. Admission source gap fixed; compiled/runtime validation still pending. N-0801; .claude/gameplay-repeat-admission-20261026/source-receipt.json. Earlier open-source statements below are historical.

2026-10-26 N-0796 follow-up: graph first traversal now rejects non-backward or stalled modern repeats and invalid signed arithmetic. This is source-only runtime mitigation; public parser admission remains open. See first-source-receipt.json under .claude/gameplay-animation-commands-20261026/.

2026-10-26 open source finding (N-0796 / T-MODDING-011): weapon_command_source.cpp:138-142 packs repeat_until_full target words without a backward-span check; weaponCommandSourceRead:175-184 only adds END-position validation before returning the source. A zero slot/zero target is accepted by this parser, while the native modern repeat loop adds a zero delta when crossed. Full downstream admission still needs inspection; source-only finding, not an executed hang. Fix valid repeat-domain admission and preserve separate regional formulas during animation conversion; no iteration-cap workaround. Details: .claude/gameplay-animation-commands-20261026/remaining-source-plan.md.

2026-10-13 pistol V6 finite artist inspection: hidden-pose diagnostic hold

The17-view source study saved/reopened with full indexed signature parity and
restored its original source. A subsequent raw-f32 data audit found10hidden
world-transform rows inconsistent with their supplied rigid collapse formula
(fullcollapse5/mixedrise2/consumed3; maximum artist error0.0756988). The25visible
rows agree. apply_pose sets hide_viewport before the diagnostic queries the
dependency graph; excluded-object stale evaluation is the source hypothesis,
not proof that local pose assignment failed. Hidden transform/BVH clearance
rows are NOT VERIFIED; no continuous/native fit claim follows. Preserve the
frozen source/images; next reviewed diagnostic must evaluate target poses before
exclusion, record local/basis/world transforms and restore source/visibility.
Evidence: weapon/resume-20261013/sampled-transform-audit-v1.json under
.claude/pd-maian-models-20261013/. No new native rerun or production fix yet.

Root observer V1 also implicitly converted a JSON timestamp through PowerShell
DateTime/Parse and produced an invalid negative interval. It is preserved.
V2 computes aware UTC arithmetic from the original raw ISO strings: first actual
controller-absent census15:01:09.4024832Z minus entry14:58:17.0392448Z gives
172.363239seconds. Native child/handle closeout was28.4921623seconds; original
terminal then exited0 normally, no process termination/cleanup. This is narrow
artist-process evidence, not canonical runner capability or an OSjob proof.

2026-10-13 00:38 UTC T-TOOLING-006 native receipt preflight: the first real
PS51 slice created its broker, observed job membership, transferred owned
handles and published an accounted terminal, then failed before ACK fault
probes. PowerShell variable names are case insensitive: foreach $mode
collided with the driver's validated Mode parameter. Failure journaling also
exposed PS51 array-subexpression conversion of List[object]. The original
controller retained its operation and finished canonical physical closeout:
root exited0, job active0, handles closed; no outstanding helper or RPC.
Both failure and closeout evidence remain preserved. V3 uses ackFault and
Events.ToArray; actual parsed binding/journal regressions and admission cases
pass23 on PS51, with no native factories. Independent V3 review and native
retry remain pending; no capability or build gate is opened. Workbench N-0622,
.claude/pd-retained-native-candidate-20261012/parent-closeout-v1/.

2026-10-03 17:47UTC T-TESTS-004/T-TOOLING-010 acceptance closed: exact620456a7
CI37141120356 passes four dependency fixtures,20storage,configure/clean targets,
full1,813/333,814 and artifact upload. Original16stale+1helper+3APIboundaries
repaired without relaxing source/identity/rollback bans. Hosted MinGW junction
fixture failures exposed separator switching even in ntpath; explicit fixture
conversion and nativeWindowsCPython CI boundary now match storage wrapper.
Prior failed receipts retained; runtime/fidelity acceptance remains independent.
See audits/2026/pd-source-contract-ci-repair-2026-10-03.md.

2026-10-03 T-TESTS-004 acceptance follow-up: original20 failures consist of16
obsolete implementation pins, one similarly prefixed function-selection error
and three lower-level catalog/provider calls outside their permitted boundary.
Repairs retain typed selection, exact identity, source-only loading and rollback
requirements. Full native PASS1,817cases/335,458assertions; builds/storage20/guard
PASS. CRLF-only checkout comparisons normalized without changing other bytes.
Exact published-source CI remains pending; no gameplay-gap closure inferred.
See audits/2026/pd-source-contract-ci-repair-2026-10-03.md.

2026-10-03 T-TOOLING-010 hosted CI portability: both d37aee06 and prior0ca5f1ab
fail configuration because dependency paths hard-code C:/msys64/mingw64 while
the selected hosted compiler is D:/a/_temp/msys64/mingw64. Selected-compiler
prefix resolution now feeds exact static archives and winpthread runtime DLL;
four tiny CMake contracts/native targets/focused1655/50/PE imports PASS. Exact
950561fa is published/remote-verified and D-013 decided. Hosted dependency
fixtures, relocated configuration and clean client/tests build PASS. Full CI
fails 20 source-contract assertions; T-TESTS-004 records the separate follow-up.
Their tests and inspected source trees match prior d37aee06 byte-for-byte; the
prior run stopped at configuration and supplied no unit-test baseline. The path
defect predates readiness diagnostics. See
audits/2026/pd-mingw-ci-portability-2026-10-03.md and retained ci-terminal.json.

2026-10-02 T-TOOLING-008 cleanup correction: exact launch identities and file-backed
stop helper are connected to both wrapper launch paths/ordinary cleanup; literal
Windows/UTC validation and fail-closed queries replace fragile filters.22checks
eachPS5.1/PS7 pass with actual hidden disposable processes and unrelated-process
preservation. Source guard/parser/diff pass; real PD cleanup validation awaits
app approval/runtime window. Historical failure and correction follow: the native preview
worker's managed-process path/time comparisons failed to identify its own
PID38316, and it released queues06:33:13 with an incorrect zero-game claim.
The game instead ended the fixed readiness timeout; native-account normalized
query confirmed zero06:36:04. Corrected preview-execution-3/release.json and
coordinator-closeout.json supersede the early claim. Before another permitted
preview, normalize Windows executable paths and creation timestamps, verify
current private owner/PID, and retain leases until process disappearance is
confirmed. A rejected filter/query is not zero-process evidence. The separate
actual Sky app approval denial is D010; no retry/alternate UI occurred. Failed
recipe/logs/full install remain preserved, no policy/pin changes or cleanup.

2026-10-02 T-TOOLING-008 native probe boundary corrections: audio registration
and sequence compilation formerly passed without a player. Native public file
voice/MP3 ownership and seqPlay now replace that success path; the corrected
audio gate passes23/23 with three native starts,1006ms scheduler window and exit0.
An unpolled delayed exit and inherited console flood aborted the first run; it
remains failed/pinned. The regular frame loop now polls the armed exit, and both
wrapper launch paths drain bounded stdout/stderr independently of game assertions.
Actual audio console710587bytes drains with131072 retained. Tiny35checks pass
eachPS5.1/7. Animation's fixed128part probe requested beyond valid short clips;
bounded full-stream counting observes15actualparts for BigSneeze. Fidelity and
ordinary UI remain open. Evidence: audits/2026/pd-initial-night-checkpoint-2026-10-02.md.

2026-10-01 readiness output persistence FIXED (N0310): stdout-only helper lost its
result when the user console closed. Probe now writes known latest.json plus
history, with a visible user cmd launcher. PS5 turns a null string File.Replace
argument into an invalid empty path; explicit NullString fixes second-result
replacement. Five synthetic file checks and safe CompileOnly pass each PS5.1/7.
First failure retained under20261001-readiness-file-output/. No desktop probe run.

2026-10-01 17:10 UTC T-TOOLING-009 material dependency scan FIXED: the full synthetic
parent/leaf ledger join exposed a second omission after broad public metadata
scanning: standard MTL `map_Kd base:texture_a` lines have no '=' and were ignored
by the key/value scanner. Bounded MTL command parsing now captures public catalog
references and excludes comments; focused discovery and member-verified join
regressions pass within93 Python contracts. No native texture or fidelity pass
is implied. Failed join logs retained in20261001-dependency-prep/.

16:10 attribution update: corrected selected-owner producer is accepted on C5
client, strict seven-member native receipt and five cases39assertions PASS. The
earlier zero-event22/23 failure below remains historical evidence.

2026-10-01 T-TOOLING-009 native attribution gap: first instrumented gate22/23 with
zero events. A selected `pdmesh::model.obj` FileProvider locator is not the typed
archive whose raw SHA is in the ledger. Source correction separates owner snapshot
from unchanged actual compiler inputs. Four focused C++owner assertions added;
corrected client/tests/runtime pending newwindow. Failedrecipe and source retained
in .claude/asset-inspection/20261001-native-proof/. No accepted strict receipt.

2026-10-01 12:12 UTC T-TOOLING-009 dependency inventory omission fixed: first CCTV
row missed four texture IDs in model.mtl because it scanned mesh.ini only. Bounded
public INI/material/JSON/glTF scanning now excludes _meta, records incomplete scans
and keeps dependency-source acceptance pending. Regression prevents schema/
behavior checks from accepting dependencies. Both actual source snapshots retained
in .claude/asset-inspection/20261001-native-emitter/. New producer still unbuilt;
this inventory repair is not native acceptance.

09:53 update: native texture-capacity/carrier/snapshot failure FIXED within the
bounded consumer contract. Final clientD9CFC0B8 passes texture12cases189649asserts,
source6/6, base animation19/19, body/head13/13 and all63 character templates.
Two residual4095 checks in catalog/map material binding were found by the first
rebuilt native wave and changed to the shared native domain. The resulting CCTV
load failure/AV did not recur; generic setup allocation-failure handling is not
claimed repaired. Marker tests are registered in CMake's explicit test list.
Old failed receipts remain history. Full per-member/render/listening/corpus and
ordinary UI/device acceptance are still open; menu executor blockage persists.
See .claude/pd-initial-integration/20261001-native-validation/summary.json.

Oct1 initial native character failure (historical, c3844 consumer lane): public-source
policy/hydration probe passes35 templates, then base:character_fbiguy fails at
base:model_cfbiguy because no retained native texture slot is available. There
are593 shared custom/generation tail slots under12-bit4096 texture packing.
Generation lifetime versus leak is not measured. Do not only increase the count
or recycle live slots. Native exit2, assertions2/6, one failed/27 untested; full
receipt and operational case ledger retained in 20261001-corrected-run. Ordinary
preview separately fails actual live foreground ownership11/29 after full180s
readiness. No input or rendered proof; initial scope remains incomplete.

08:23 update: source repair widens the entire native texture chain to unsigned
16-bit private identity (legacy decoding preserved), moves glass-hit sentinel
outside its range, protects snapshot rows and retains stage/GPU lifetimes. Native
allocator/marker/prefix cases prepared; no rebuilt proof yet. Own-worker read-only
facts: CodexSandboxDesktop, foreground0, input desktop read denied5. Menu retry
requires authorized interactive executor, no bypass or unrelated focus control.
Historical35pass/1fail/27untested retained. See20261001-texture-repair evidence.

Oct1 smoke failure receipt serialization FIXED: Get-Content decorates strings
with filesystem metadata. PS5.1 Depth50 JSON expanded the forbidden-pattern
witness, growing owned finalizer3992 to9.42GB after native exit. Contained owned
wrapper and recovered exact receipt in PS7 without relaunch. Line now copies plain
text; ten actual assertion/JSON/format checks pass in both PS5.1/7 (252 bytes).
Only that serialized witness held a log string in Test-Assertions.ps1; numeric
sequence/count fields stay exact. No unsafe deep decorated-string retry.

Oct1 T-TOOLING-008 private runtime fixture readiness defect: a retained bounded
variant reduced Agent Select readiness from canonical 180s to 30s. The run timed
out while weapon bootstrap was still active, before main-menu arming: 10/29
assertions, no input events or screenshots. This does not test menu/render
acceptance. Propagation found the same reduction in the private keyboard variant.
New private copies restore all canonical input steps and overall timeouts and
pass exact static comparison; native retry remains open. Both failed copies and
full-install receipts are retained. See the initial integration audit and
.claude/pd-initial-integration/20261001-readiness-corrected-prep/proof.json.

Oct1 T-TOOLING-008 storage launch defect FIXED at the script boundary: canonical
MSYS-first PATH selected Python unable to interpret Windows paths or provide the
native Windows handle contract. Storage and the related new inventory driver now
select verified native Windows Python without changing TEMP/TMP/PATH. PS 13 and
tiny inventory under that exact environment pass, plus storage 14/ledger 21 and
sourceguard. Native game retry is waiting for the parent resource window; this
does not claim rendered/menu/asset acceptance. See initial integration audit.

Sep29 T-MENUS-004 ordinary virtual-controller receipt873C is red26/29:
first A enters Create and first B leaves the editor, but second B logs
agent_select.back while releasing agent_create. menuRenderDialog queued the
legacy background/transition dialog as an interactive ImGui screen. Candidate
now admits only menu->curdialog to that queue for every dialog-backed menu;
character-preview rendering remains independent. Exact failed receipt:
.claude/smoke-verify-runs/results-20260930T022335Z.json. Native first-A/mapping
gate241/9995 and client2649B339 pass after this queue-admission fix. The ordinary
retry (results-20260930T023149Z.json) times out before attachment with Windows
foreground=0, so runtime validation of Back ownership remains open.

Sep29 T-MENUS-003 first-A investigation: native ImGui suppresses activation
when its navigation cursor is hidden; Agent Select's selected row also lived
inside an unfocused child region. Candidate now requests selected-row focus
on entry and reveals native navigation for mapped menu gestures. Native first
Accept and child-row regressions pass; isolated873C now enters Create with one
A. The earlier virtual15/29 run remains retained as a failed overlapping run.
SDL2.32.10 also omits Touchpad from its automatic virtual mapping. The smoke
fixture now installs an identity mapping only for its owned virtual device,
keeps strict binding/state checks, and records action/focus observations.
Hardware mappings and user bindings are unchanged. Complete-flow acceptance
remains open for the separate Back-ownership and foreground-access gates above.

Sep29 T-MODDING-002 fixture/production descriptor mismatch FIXED: new mesh.ini
used [mesh] while modelSourceResolvePath requires [model]. Host and fixture now
share canonical [model]; strict-reader negative regression prevents permissive
admission before failed conversion. Propagation check found no other new graph
fixture using the wrong section; existing intentional legacy/rejection cases
remain unchanged. Death retirement and replacement-held-model refresh are also
connected. Client/tests0, native148/4941, guard0, installed41/41 (26/26 cases),
frozen3408 drift0. Real world-hit/render acceptance still OPEN. Both installed
red receipts and final pass retained under asset0923lang/graph0929-*; see audit.

Sep29 T-MENUS-005: confirmed the Settings/Agent option mismatch is retired
save-side indexing, not current UI indexing: lvReset uses slot0. P1 save,
load and defaults now use0; obsolete co-op return swaps removed. Candidate
UNVALIDATED pending combined build and restart/return journeys. No save schema
change. Existing paused source7 hashes match. See Sep27 Settings coverage audit.

Sep27 T-NETWORKING-011 / multiplayer F14 ordering follow-up (P2): server snapshots
could rewind movement or repeat a teleport; stale client input changed ACK before
rejection. Shared movement admission now rejects stale/duplicate/half-range
records without mutations, with monotonic wrap-aware ACK handling. Integrated
builds and native172/14417/guard pass; runtime unrun, user paused04:50 ET.
This does not close the F05 client-position authority defect.
See context/audits/2026/multiplayer-movement-ordering-2026-09-27.md.

Sep27 T-MENUS-005 candidate: controller capture disconnect releases input and
preserves bindings; frozen asset0923lang gate passes235/9765, zero2010-file
drift (.claude/menu0927/capture-broad.xml). Subsequent Video/Game/mouse save
requests, retired split control removal and Agent dirty trigger await the
next integrated build. Propagation found indexed Settings options use slot0
while solo gamefile capture/apply uses slot4; this remains OPEN and requires
an adapter repair, not a claim that dirty invalidation alone fixes persistence.
Details: context/audits/2026/settings-control-coverage-2026-09-27.md.

Sep27 MP admission qualification remains open (T-NETWORKING-011): binary4A3BA1AC
transfers and spawns Needler but omits impact-effect witnesses on both accepted
peers (94/101). Do not weaken these assertions. Campaign90/96 failures instead
reference obsolete spawn apply logs; fixtures now require prepare and commit,
with retained-log positive and missing-commit negative checks. Current-source
reruns remain pending after concurrent menu drift and the graph source window.

Sep27 T-MENUS-003 Agent Select candidate: default toggles and deleted-default
cleanup ignored configSave failure, displaying success without persistence.
All three call sites now check the result; toggles roll back and deletion
cleanup exposes Retry. Propagation audit also found manual Up/Down and global
Accept competing with native Selectable focus. Candidate uses native row
activation/focus and defers graph transitions until End. Client/tests build and
broad234/9745 pass; actual Agent renderer failure injection and physical-device
validation remain pending. Ordinary virtual journey stopped before launch at
the B-801 memory guard (923MB available commit); no input acceptance is claimed.

2026-09-26 T-NETWORKING-011 declined-content stage audience remains open:
the red distribution run logged a guest DECLINE, then a malformed stage message
and disconnect. Source confirms readyGateCompactDeclinedRoster restores the
excluded client to its prior lobby binding and retains room membership, while
netServerStageStart sends the stage packet through netSendToRoom to every
connected room member. Next unit must target the committed match audience and
prove a declining peer remains connected in its room; do not relabel a missing
content peer as a working spectator. Candidate now routes stage/baseline/cutscene/frame packets through the frozen
match mask and live admitted state. Failed decline sends retain consent for
retry; successful decline keeps a connected lobby without loading countdown.
R1 red95/100 retained: synchronous decline was overwritten by PREPARING;
co-op red47/96 also exposed the smoke driver consuming a room-default arena.
Corrections and direct-swarm audience propagation pass the Sep27 build and
focused cohort. Full runtime qualification remains open as detailed above.

2026-09-26 T-NETWORKING-011 received-content extraction failed in the
ordinary pair (60/89) and six existing native PDCA tests. Both long install
and short temporary paths failed stage creation. Directory creation traversed
all ancestors even when a nearer existing destination was usable; a native
CRT probe could not stat the AppData ancestor even though the test destination
beneath it was accessible. The initial existing-directory precheck alone did
not fix this. PDCA now finds the nearest existing prefix and creates only the
missing suffix. Client/tests builds and 80 focused cases/12,431 assertions plus
native guard pass; unchanged ordinary pair retry passed 89/89 at
results-20260926T201254Z.json. Keep the red runtime receipt
results-20260926T195912Z.json and native mp0926-pdca-before/r2/diagnostic receipts.
Propagation: fsCreateDir also avoids redundant creation of existing directories;
modpack's helper already checks existence first. No path-length closure claimed.

2026-09-26 T-NETWORKING-011 distribution queue lifecycle: pending requests
retained reusable client pointers without transport generation, accepted
unbounded duplicates, and survived client reset. Source now binds queued work
to client/peer/connectID, coalesces admitted duplicates, preserves FIFO order,
and rejects explicit global/per-connection budget overflow. Client reset
retires pending and active transfers before slot reuse. Both catalog and
manifest producers share the same queue. Native/focused 80 cases/12,431
assertions, builds/guard and unchanged two-install89/89 pass; this does not
close large-transfer performance or WAN acceptance.

2026-09-23 T-MENUS-003 Forge Remove All previously called `botmgrRemoveAll`,
which clears bot roster pointers and count without retiring live character
props. Those characters continued in the world and occupied the character
roster even though the Bots tab reported zero. Forge now retires its tracked
characters through `chrRemove`/prop teardown, compacts both MP rosters, then
releases only its config slots; non-Forge roster entries remain. Add checks
actual roster publication and cleans a partial legacy allocation before it
reports success. Propagation check: stage reset still uses the old
`botmgrRemoveAll` contract, while both Forge exit-play and Bots-tab Remove All
use live retirement. Final isolated client/tests builds pass; ordinary play
validation remains pending.

The same Forge review found Spawn Near Me passed the high config-slot index
returned by `s_spawnBot` to `s_teleportBotNearPlayer`, which indexes the
sequential `g_MpBotChrPtrs` roster. A newly added bot could therefore miss the
teleport. `s_spawnBot` now returns the published roster index. Ordinary
placement and room-membership behavior still need a client walkthrough.

2026-09-23 T-MENUS-003 reachable Room Level Editor is a data-only shell. The
Room tab's "Launch Level Editor" makes `s_LEActive` true, "Spawn at Camera"
only appends to `s_LESpawned`, and the overlay shows a fixed stub camera with
free-fly "in development" text. `s_LESpawned` is referenced only inside the
same UI renderer, so no world object is created. This is an advertised dead
creator action in a reachable multiplayer room, not validated Forge behavior.
Mike chose to retire this data-only Room tab and keep Forge as the creator
route. The tab, launch action, overlay, and their private 518-line shell have
been removed; the three live Room tabs retain their order and Back behavior.
Propagation check: searched all `s_LESpawned` references and the separate Forge
entry; no production bridge was found. Fixed-source `menu0923` r7 client and
focused menu tests pass; Room walkthrough remains pending.

2026-09-23 T-INPUT-008 smoke-runner isolation: `run.ps1` globally reaped every
`PerfectDark.exe --smoke` process on entry and exit. The 14:32:29Z menu Agent
run exited -1 during extraction when a separately queued multiplayer runner
started at 14:32:32Z. Both runners used valid but different coordination
resources. Cleanup is now restricted to process IDs launched by this runner
and their fault children; a source regression guard rejects global smoke-path
matching. Propagation check: both single-client and multi-process cleanup
sites were inspected. Parser and mocked two-runner process isolation passed;
fixed-source r7 `[b927]` passed 21/21. Ordinary runner behavior remains
blocked at foreground ownership, independent of cleanup;
the failed receipt is `.claude/smoke-verify-runs/results-20260923T143229Z.json`.

2026-09-23 T-ASSETS-001/T-EXTRACTION-001: Isolated NTSC-final extract-only
boot produced 476/476 regional language sources and registered all 476, but
then stopped on 221 catalog registration failures. All 84 `.pdhead`, 68
`.pdbody`, and 63 `.pdcharacter` archives failed admission. One traced example,
`base:head_carrington`, contains `mesh.pdmesh` with an older public export
than `meshes/base_model_cheadcarrington.pdmesh`; both claim
`base:model_cheadcarrington`. Head/body/character fast caches use only their
own stamp/entry presence, so a changed top-level dependency does not
invalidate its nested copy. The catalog correctly rejects divergent public
sources with the same ID. Repair the extraction dependency lifecycle without
overwriting user edits, then prove clean and upgraded installs both boot with
zero catalog failures. Propagation check: head, body, and character extractors
all embed dependent typed archives and share this stale-cache pattern.
Initial failure receipt: `.claude/session-builds/asset0923lang/logs/game client/pd-client.3.log`.
Upgrade retry refreshed all 84/68/63 and cleared those 215 failures. The
remaining six were obsolete renderer-effect placeholder archives emitted by an
older extractor but absent from a clean current install; their old shader IDs
have no visual consumer. A strict provenance-and-public-digest migration now
retains only those exact untouched archives under effects/_retired and refuses
edited/unknown source. The migration client build passes; upgraded boot and
edited-source negative receipts now pass: final client fresh and upgraded
installs each register 5,562/5,562 active archives with zero failures, while
the edited legacy archive retains its exact SHA-256 and is rejected loudly.
The initial 221-failure boot is superseded for this migration scope. Full
fresh conformance covers 9,018 root / 10,019 recursive archives in 27
families. Propagation audit also found
embedded typed sources in weapon, arena, and metadata extractors; their
dependency invalidation policy still needs a separate source-consumer audit.
Follow-up 2026-09-23: weapon mesh/animation/audio and arena/mission scenario
parents now check current public dependencies before warm reuse. Four edited
top-level sources refreshed 11 weapons, one arena and one mission; 5/5 sampled
nested copies match and full catalog registration remained 5,562/5,562.
Three directly edited parents failed loudly and retained exact SHA-256. This
closes the identified warm-cache dependency class for these five parent
families, while the broader emitter inventory and gameplay source use remain
open. Receipt: `.claude/session-builds/asset0923deps/dependency-batch-receipt.json`.

2026-09-23 T-MENUS-003 social Player Profile displayed a 3D head/body preview
placeholder that could never resolve: `share_profile_t` carries only statistics,
with no appearance/catalog IDs. The preview promise has been removed from the
reachable modal; actual received stats and public-mod actions remain. Propagation
check found no other `charpreview` use in the social UI. Client build and
ordinary social-device walkthrough is still pending for this edit. The isolated
client build and broad input/menu/settings gate pass on this source.

2026-09-23 T-MENUS-003 Forge Bots tab had stale status text: `forgeRuntimeTick`
already consumes Add/Remove requests, but the UI called the botmgr wire a
future pass. The core incremented desired active/frozen counts on request and
runtime incremented them again after successful spawn. Source repair removes
runtime double increments, rolls back failed requests, and lets Remove All
cancel pending Adds; the UI now labels requested counts and pending commands.
The follow-up now maps Smart Aggression to native bot difficulty, applies the
selected default difficulty for Any/Near-Me spawns, freezes added Frozen bots
by slot, and frees Forge config slots on Remove All so later Adds can use them.
The Bots tab uses a validated difficulty combo and disables Add at capacity.
This follow-up compiles in fixed-source `menu0923` r7 client and remains
unexercised in ordinary play because foreground ownership is unavailable.

2026-09-23 T-MENUS-004: The generic ImGui typed-dialog renderer treated every
SELECTABLE `handler` as a callable function. For `MENUITEMFLAG_SELECTABLE_OPENSDIALOG`
the game ABI stores a dialog-definition pointer there, and for
`MENUITEMFLAG_SELECTABLE_CLOSESDIALOG` the handler can be null. Thus reachable
fallback buttons could call data as code or leave Done inert. The renderer now
defers the selected action until after `ImGui::End`, applies close/open flags
through menu-graph push/replace/pop edges, and gives Cancel precedence for a
simultaneous press. Propagation check: inspected the purpose-built ImGui dialog
registrations and the legacy `menuitemSelectableTick` contract; remaining
generic selectable handlers require current ordinary-client coverage. Isolated
client and tests build, focused `[menu_graph]` PASS846/846 and broad
`[input],[menu],[settings]` PASS9732/9732; controller/MKB runtime remains open.

2026-09-22 19:18 ET: T-NETWORKING-011 ordinary listen-host multiplayer start
can reject a valid Random weapon selection. On the short-path real-peer
distribution fixture, `matchConfigSelectWeaponSet(6)` resolved to Random set 13;
the lobby writer transmitted the menu index and exact slots, but the server
reader tested index 6 against the resolved Random constants, re-rolled slots,
and rejected `weapon set metadata and exact slots disagree` four times before
ready/transfer. Two-path `netmsg.c`/lifecycle guard correction applied, not yet
built or runtime-verified. Propagation: surveyed all `mpPrepareWeaponSet` callers;
writer and match setup already distinguish requested and resolved domains.
Retain RED35/67 receipt `results-20260922T231327Z.json`; no F12 runtime verdict.

2026-09-22 18:59 ET: T-NETWORKING-011/F12 transfer BEGIN previously allowed
512 MiB while the sender capped expanded archives at 50 MiB and END rejected
above 256 MiB. Its 200 MiB session check combined received compressed bytes
with incoming expanded bytes, without reserving pending transfers. The lobby
progress bar divided compressed bytes by expanded bytes. Source6 now has one
expanded-size budget and bounded compressed receive geometry; native budget4,
client/tests builds and source guard pass on full3623 SHA4463E90D. Propagation
check covered sender/BEGIN/CHUNK/END/progress paths. Consent and pacing remain
separate open defects. First two-process smoke stopped before any transfer on
an `ASSET.SOURCE_ONLY` fatal: the long per-test host install path exceeded all
private animation cache path budgets. This is a source-observed path limitation
in `modasset_compiler.c:4533`, not proof that F12 distribution failed or passed
at runtime. Retain RED20/44; the shorter install path cleared this first
blocker and exposed the separate lobby-start defect above.

2026-09-22 18:19 ET: T-NETWORKING-011 synthetic FT socket peer initially
treated a preceding presence ping as an FT frame and exited before acknowledging
the game's valid INIT. First ordinary run stopped red at 4/6; the game sender
then abandoned after no INIT ACK. Root cause was the fixture peer assuming all
datagrams on the shared presence socket were FT. The peer now filters non-PDFTX
frames and expects the production `file` kind / `files` folder. Propagation
check: all peer receive paths use the common filtered `received()` function.
Corrected r2 on the unchanged client passes signed bidirectional UDP, saved
file/sidecar and ACK replay (6/6, one publication). Retain r1 red and r2 green
receipts under `.claude/mp0908-repairs/followups/ft-storage/`.

2026-09-22 17:52 ET: T-NETWORKING-011/F04/F12 receiver completion repair is
component-verified on frozen full2149 SHA8337DB9E: isolated client/tests builds
0, native storage5 exact5/0 failures, native asset source guard0. The earlier
menu client build stopped first red because the removed filename sanitizer still
served the local music-to-mod converter. Propagation search found one remaining
caller; source6 r2 restores that local helper while remote inbox naming uses the
new safe peer/digest helper. Current combined client build passes. Real signed
socket delivery, sidecar JSON parse, receiver restart and UI/WAN remain open.
Receipts: .claude/mp0908-repairs/followups/ft-storage/{build-r2-client-exit,
build-r2-tests-exit,storage5-exit,storage5-name-audit,native-guard-r2-exit}.json.

2026-09-08 23:06 batch22 connected texture/source pipeline ACCEPTED within scope (source35).
Full2145 24440303AB0D8D1EB47802A8BA52AA3D71F7D10FCFB818CFDCDB1135EE097BD2;
changed488 BAF7FC71; client6D4FE54E/tests233ECA5C unchanged through final audits.
Client/tests build0, native26 PASS (unchanged helper inputs), affected source-contract1
PASS, source guard0, warm installed24 PASS35/35 in39.1s using completed fresh
extraction20260909T025106Z. Actual texLoad pixels, descriptor edit, stage modes,
GBI tile fields, rejection/reset, sparse reverse index, custom menu texture and
suppressed/enabled base-overlay image/material selection all pass. Audit PASS all
3503 original texture material records,169 stage assignments,87 standalone+45 nested
scenario graphs,132 unchanged nonmaterial graph comparisons and coherent hashes.
All110 public command IDs/593 commands exact, no source/fixup failures.
Additive atomic upgrades preserve edited descriptors/images/graph topology and
versioned omissions; incomplete archive views reject rewrite. All12 occupied-count
index-bound mistakes fixed or removed across8 extraction/loading paths. Suppressed
base overlays now select public base sources through shared image/material policy.
Earlier compile/link reds, initial legacy-DMA crash, safe rejections and120s extraction
timeout remain in batch22 receipts; timeout allowance is now bounded240s. This is
native/source acceptance, not rendered appearance, full graph gameplay or all-family
parity. Full equipped model-generation ownership/graph publication, palette/mipmap
parity, larger textures and requested pack remain open. No staging/commit/push.
Evidence: .claude/asset0905-continuation/batch22-r9-{client-build,tests-build,
affected1,native-guard,texture-pipeline-installed24}-exit.json; batch22-combined26
JUnit/name audit; batch22-r9-{texture-material-audit,base-command-coverage,
post-runtime-freeze}.json; results-20260909T030500Z.json. Root queues finished;
source holds released. MP storage remains private/paused; menu native work idle.


2026-09-08 22:59 source33 settled r8: all12 occupied-count/index-bound mistakes repaired or removed across8 loading/extraction paths. Shared ordinary texture selection now chooses public base sources when overlays are suppressed; image/material policies match, cache tracks suppression mode. Installed24 fixture checks sparse reverse index before texLoad and exercises custom/menu source plus enabled/suppressed base overlay pixels/properties. Client/guard/warm24 next; native26 inputs unchanged. No full equipped-graph or rendered acceptance.


2026-09-08 22:57 batch22 r7 installed warm24 TERMINAL RED32/35: custom slot now safely rejected, not legacy DMA crash. Actual sparse catalog root cause:12 index loops used occupied count instead of pool high-water index; valid later rows skipped after removal. Corrected full class across8 sources (root additionally owns base_extended/langmanifest/romextract_pdmeta), each already checks occupied rows. Fixture enables its texture and next asserts actual reverse-index binding before texLoad. Native26 unchanged; source33 pending combined client/guard/installed24. Prior r3/r4/r5/r6/r7 reds preserved.


2026-09-08 22:45 batch22 r3 client/tests build0, combined native26 PASS, source guard0. Installed24 TERMINAL RED32/35 (98.9s): new ordinary custom texture case reached legacy DMA after g_NotLoadMod suppressed its public source. Exact addr2line traces texLoad:2361 from harnessTextureRuntime:839. Corrected custom-versus-base-overlay suppression and added a custom-slot boundary before any legacy directory lookup. Source30 now settled for r4 client/guard/installed24; native26 inputs unaffected, no unchanged rerun. Original red receipts retained; full texture/material acceptance pending.


2026-09-08 22:30 source-confirmed: archive readers intentionally filter unsupported/encrypted/unsafe entries. Rewriting a filtered view silently loses members. modArchiveCanRewriteSources now records complete original entry names/count/comment and blocks partial-view rewrites in generic replacement and texture/stage migrations. Source28 applied; native rejection/preservation cases and combined validation pending.

2026-09-08 21:58 texture generation/source-properties component ACCEPTED (batch21).
Source20 r2 83677B33; full2136 CC3BAE06/changed4731F8F2D7F/clientE23B7EEF/
tests5D62ADA3 unchanged. Client/tests build0, native texture7 PASS, source guard0,
fresh installed23 PASS33/33 in75.3s with DummyAudio. Actual catalog acquisitions
retain image+descriptor hashes, pixels/native identities and tile/surface data;
image-only and property-only edits coexist; retirement/reset/empty pool preserve
old users; actual model G_SETTIMG/G_SETTILE use selected retained generations.
Malformed image, bad/duplicate/wrong-ID properties and incomplete bundled legacy
metadata reject; final release permits reuse. Base110 command sources/593 commands
exact, no missing/source/fixup failures. Receipts batch21-* in asset0905-continuation;
install 20260909T015641Z-weapon_mesh_ingress_smoke, results-20260909T015757Z.json.
All root queues finished and source holds released. MP storage6 stays private,
paused and excluded. Base texture metadata migration, ordinary/stage override
source authority, palette/mipmap parity, larger images, complete equipped graph
and requested pack remain open. This proves native consumers, not rendered output.

Earlier findings and preparation history follow; current status above wins.

## 2026-09-08 File-transfer completion precedes durable save and loses retries

T-NETWORKING-011/F04/F12, source-confirmed after wire7. handleChunk ACKs the final
chunk before completeReceiver checks SHA and writes the inbox file. Sender frees
on that ACK; receiver frees after completion, so a lost final ACK cannot be
answered on retransmission. Data publication uses fopen wb and deletes the
destination on a short write, risking a previous file. buildInboxPath inserts
the remote Agent name directly into a directory; sidecar JSON interpolates names
without escaping. Next repair must use bounded safe peer paths, verified atomic
publication, final saved/rejected receipt and bounded completed-receipt replay.
An in-memory completed cache alone does not prevent receiver-restart replay.
The Sep22 source6 r2 component gate above verifies receiver commit behavior;
actual socket proof remains open. Native wire7 is separate.

## 2026-09-08 Signed file-transfer frame and chunk bounds are inconsistent

T-NETWORKING-011/F04/F12, source-confirmed21:01. FT_FRAME_LEN is1280 while
FT_SIG_OFFSET1256 + FT_SIG_LEN64 requires1320 bytes. Sender stack packets allocate
1280 and signFrame writes64 signature bytes at1256: a40-byte overrun, followed by
a truncated signature on the wire. Receiver storage is larger but accepts only
1280 bytes and verifies64 signature bytes, including40 bytes not received.
This prevents a trustworthy signed-file-transfer path; current activation probes
exercise the installed-package queue, not this socket/frame path.

handleInit also allocates chunk_present from unchecked advertised chunk_total,
without requiring ceil(file_size/1024). handleChunk computes the final copy size
from file_size minus offset without capping it to the received1024-byte payload.
A too-small chunk count can therefore make the copy read beyond the packet.
ACK/REJECT lookup is transfer-ID-only and does not bind the signing friend to
the transfer; chunk handlers ignore recipient/echoed count. Repair wire sizes,
geometry and peer/recipient admission with real signature/packet-boundary tests
before claiming received-content transport acceptance. At21:19 wire8 applied
under a fresh source grant (manifest72C4260E), validation pending. A shared
capacity-checked v2 wire helper and presence dispatch now own these boundaries;
native7 passed2898 assertions at21:24, client/tests/guard0 with no drift on full
438CF4A4/client4DDA994D/tests6A2856DE. Actual socket/receiver commit acceptance
remains open. The legacy sender's250ms delay after each ACK is also removed; only retries
wait. Distinguish all-chunks-acknowledged from verified/saved/installed receipt.

## 2026-09-08 Model source metadata and retained texture dependencies

T-MODDING-002. Model skeleton read private `_meta/manifest.json` before public
mesh.ini, while loose OBJ/glTF sidecars were ignored because metadata paths
required an archive separator. Correction applied with explicit compiler input
callbacks; verified in model-inputs installed22 (22 cases, 31 assertions). Geometry, public
metadata, external buffers and image bytes can now be supplied as one captured
read set. No immutable equipment-model acceptance yet.

The audit also found texture tile offsets/mask reductions still read from
`g_Textures` in texWriteTileFromDefinition, and tile/sound/surface attributes are
not emitted by romextract_pdtexture.c into texture.ini. Collision/sound callers
also consume texture numeric identities. A private model texture pool alone
cannot preserve those semantics if it reuses local IDs. Next owned texture/model
work must expose the missing public metadata, retain native texture identity and
pixel lifetime, and cover renderer/collision consumers. Do not bypass this with
zero defaults for extracted textures or claim geometry-only ownership as complete.

Propagation audit 21:03: mpStartMatch also hardcodes 169 surface/sound assignments
across 90 native texture identities according to stage ID. These stage semantics
must become public stage-owned overrides keyed by exact texture catalog ID, with
defined precedence and transition reset; copying the ROM texture table alone is
insufficient. Existing extractor cache-kind mismatch rewrites entire archives;
the metadata migration must preserve edited images, descriptors and extra members.
Inventory: .claude/asset0908-drafts/texture-generation/stage-properties-audit.json
and migration-review.md. No extraction/runtime fix for these findings applied yet.

## 2026-09-08 Digital action holds lack per-source ownership

T-INPUT-008; source-confirmed20:40, no runtime reproduction yet. fireVk resolves
each press/release independently and toggles one ActionState.held. Two distinct
bindings to one action can release each other. The per-frame wheel cleanup also
clears every held action that has a wheel binding, regardless of which physical
source holds it. This extends the raw Btn23/axis alias release problem to ordinary
multi-binding. The future coherent VK/dispatch repair needs per-source retained
ownership, source-specific wheel pulse retirement, and flush/smoke ownership
integration. Merely widening VKs cannot fix this class. Sep22 source candidate
now retains the physical source's original action winner and aggregates owners;
wheel cleanup retires only actual wheel pulses. Focus, device, context, and
binding boundaries retire matching owners. Sep22 isolated client/updater/tests
builds, direct owner3/26, smoke-owned source1/53 and actionmap regression15/481
pass. First peer FT compile red and two stale source-contract reds are retained.
Ordinary menu/runtime and hardware proof remain open; high-button/axis VK
separation and glyph truth remain open.

## 2026-09-08 Archive rescan resets loaded baseline before activation preparation

T-MENUS-006; source-confirmed20:36, runtime reproduction pending. Installation
calls modmgrRescanDirectory before activation preparation. The rescan restores
all archive rows with loaded=0 after closing handles, even if previously loaded.
A subsequent requires-restart plan therefore cannot recover that earlier loaded
baseline. The retained-plan repair preserves its own preparation baseline across
retries; it does not yet preserve pre-install archive generations/state. Needs
coordinated catalog/activation lifecycle work and an existing-archive regression.
The current discovered-folder smoke must not be described as this proof.

## 2026-09-08 Installed activation retry can skip a failed persistence step

T-MENUS-006; MP read-only propagation finding, menu owns correction. In
modmgrEnableInstalledChecked, disabled activation publishes enabled1 before
persistent Apply. Failed component/config save leaves enabled1/dirty1; retry
then takes the already-enabled sync-only branch and can report success without
retrying persistence. A fresh Apply plan after late failure can also lose the
original requires-restart loaded baseline. Do not wire FT retries to the current
helper. Repair needs retained operation intent/baseline plus explicit completion
or discard semantics. Sep8 20:31 correction applied: caller-owned persistent
activation plan, install-only API, importer Retry/Discard and departure guard.
MP owns FT migration. Actual changed-selection failure/retry validation pending.
Core Apply2 proves SAME owned-plan retry only and does not cover this helper.

Sep8 21:15 updated: activation core2 PASS35/35 in79.5s now verifies the replacement
helper's new/already-enabled persistence retry and real late-load retry preserving
original loaded1 restart baseline. FT actual retained queue/retry/explicit discard
also passed. Receipt menu0908/activation-core2-exit.json, client811BE779. The prior
helper retry defect is corrected; rendered UI/Agent-switch and the separate
pre-install archive-rescan baseline gap remain open.


## 2026-09-08 Received-mod activation retry can skip failed persistence

T-NETWORKING-011/F04/F15 propagation finding, source-confirmed20:11; actual
failure/retry reproduction pending. modmgrEnableInstalledChecked first publishes
enabled1, then its persistent prepared Apply may fail component/config saving.
It frees that plan while leaving the in-memory enabled flag. The next call takes
the already-enabled branch into modmgrSyncCatalogToRegistryChecked, which rebuilds
without saving, and may report success with unsaved selection. Menu owns helper
repair; N-0168 records exact paths/lines and direct handoff. MP file-transfer caller
also ignores Apply results and clears its prompt on failure; its UI void-casts
acceptance and dismisses. Repair both checked retry semantics and caller/UI state,
then prove saved selection and actual activation under injected failure/retry.
21:16 update: menu retained-plan API and MP FT caller/UI repair are applied.
Public Mods7 source-contract cases and actual core FT save-failure/retry/discard
passed; ft-activation-runtime-review.json checks exact events/current FT4 against
Core2 full049E558F/client811BE779. Retry saved selection and loaded the package;
discard cleared the queue while preserving enabled1/dirty1. This is installed
queue/core proof, not actual download, ordinary UI or Agent-switch acceptance.

Related source ownership gap: file_transfer.c pending activation rows contain
sender/mod identity but no local Agent owner; fileTransferInit/Shutdown do not
clear that queue. Agent rebind can therefore retain another Agent's prompt and
transfer state. MP follow-up must bind pending plans/prompts to local Agent and
stable mod ID, cancel owned in-flight state on rebind, retain failures for retry,
and avoid claiming a partially enabled package was kept disabled on dismissal.
20:52 update: pending rows now own local Agent + stable mod ID and a persistent
plan. Shutdown/rebind free plans and old transfer buffers; discard reports no
rollback. An actual Agent-switch reproduction remains pending.

## 2026-09-08 Voice burst negotiation can discard speech onset

T-NETWORKING-011/F09/F10. Source review: a new PTT/VAD burst sends HELLO, and
broadcastVoiceFrame drops encoded media until the peer returns its challenge.
Fresh challenge admission prevents replay, but the initial speech can be lost
over the handshake round trip. Preserve fresh-session security while evaluating
bounded outgoing pre-roll or consent-scoped pre-negotiation. The accepted native
transport tests answer HELLO before media and do not prove onset quality. VAD is
now source-applied and native17/component-verified on frozen full2156 59011833,
but actual sockets, audible onset and physical-device acceptance remain open.
The VAD threshold does not itself solve negotiation pre-roll.

## 2026-09-08 Audio generation and effective pitch ownership

T-MODDING-002: sound leaf slots were reused on catalog reset while native command
owners could still retain their IDs. Per-play source reopening also selected
new bytes after an edit. Applied generation adapter owns decoded source and
slot; each playing voice receives independent PCM. Shared file/PCM voice start
also rejects finite pitch factors whose product is nonfinite, which could
otherwise leave an infinite cursor in the loop mixer. Batch18 audio36 and installed20 dummy-mixer checks passed: retained PCM/slots,
playing voice after release, loop timing and overflow-allocation rejection.
Client isfinite and menu linkage build failures remain separately recorded;
r3 clientCF294A6D/full80E30B41 is accepted. Full command/graph integration is open.

## 2026-09-08 Interface Delete leaves stale catalog after partial file removal

T-MENUS-006. interfaceDeleteModDir removes known manifests before attempting
rmdir; remaining files can make rmdir fail after mod.json was already removed.
The UI previously skipped catalog refresh in this case and reported success
after unchecked refresh on complete deletion. Sep8 19:37 one-file source repair
reconciles actual files after either outcome (including theme directories),
consumes checked sync and offers Retry refresh without repeating deletion.
Status preserves partial-file truth, wraps long errors, and gives Back priority
over retry. Batch18 r2 exposed local extern declarations acquiring C++ linkage
in the extracted helper; moved both to the global extern C block under root's
sole-source correction grant. Settled manifest interface-delete-linkage-applied.json.
Batch18 r3 clientCF294A6D/full80E30B41 passed with no drift (menu receipt
interface-delete-client-postcheck.json). Actual UI/input verification pending.
Filesystem deletion scope is unchanged; both earlier client reds remain retained.


## 2026-09-08 Alternate Mod Manager callers discard activation failures

T-MENUS-006; source-confirmed propagation of main Apply failure loss. Reload
cleared dirty and transitioned stage after failed rebuild; archive enable returned
an applied-success index after void Apply/sync; Weapon Save completed its modal
after void Apply. Sep8 19:27 caller4 source repair adds checked reload/sync and
installed activation, verifies actual enabled publication and carries failures
to archive out_error and Weapon Save status. Restart deferrals get truthful
wording. Manifest .claude/menu0908/apply-complete-review/callers/applied-four.json.
Client/tests build0 and affected Weapon source contract1/100 assertions plus
guard PASS on fullFBCC1DBA, no drift. Actual core/UI runtime remains pending.
Interface Delete, file transfer, Agent transient
Apply and session retirement are still explicit propagation follow-ups.


## 2026-09-08 Main Apply loses failure and cannot retry its own publication

T-MENUS-006; source-confirmed. Main UI mutated component runtime before saving,
called void Apply, refreshed away desired choices and displayed success after
ignored save/rebuild failure. Its old baseline also rejected retry after its
own successful registry publication. Sep8 18:56 source repair adds prepared
intent, checked Apply/rebuild, actual publication verification, retained Agent/
restart state and post-attempt baseline. Main UI keeps failure/choices and guards
departure until checked success or explicit discard. Applied-ten.json in
.claude/menu0908/apply-complete-review. Catalog-plan4/51 and settled apply-r3
main5/139 assertions, client/tests builds and guard PASS with no source drift.
Actual core/UI failure journeys remain pending. Other void Apply/reload/sync/install callers remain
the propagation follow-up; no full transaction rollback is claimed.


## 2026-09-08 Metadata-only import rollback changes lifecycle state

T-MODDING-002 installed batch15 failed all three new folder/loose/typed late
replacement cases despite valid dependency admission and logged rollback.
`catalogActivationLedgerRestoreRetiredSnapshot` unconditionally promoted every
enabled REGISTERED row to ENABLED, even when it never owned a payload. Exact
row restoration was therefore impossible for inert admitted command sources.
Fix preserves prior REGISTERED/ENABLED state for unowned metadata, while still
clearing retired loaded/active pointers, reference counts and runtime state.
Actual-ledger regression and retired-ownership regression passed2. Corrected
installed batch16 passed18/18 inner23/23 outer72.3s; folder/loose/typed late and
cyclic replacements restore exact rows, native pointers and provider counts. The shared helper
covers all import rollback callers; do not normalize away this fixture failure.
Batch15 receipts remain failed15/18 inner,15/23 outer98.599s, source unchanged.

## 2026-09-08 Social group and voice state lack active-Agent ownership

T-NETWORKING-011/F07/F15, source-confirmed lifecycle gap; live reproduction
pending. agentSessionActivate -> prefsAgentPublishActive rebinds the social handle,
but socialHubBringOnline returns immediately once online. socialRebindToActiveAgent
only changes name/handle/code; no group/voice reset. The only source caller of
groupSessionShutdown is socialHubGoOffline, which has no production call sites.
Transport disconnect preserves peers while suppressing automatic rejoin. The group
snapshot has no local Agent owner, and voice signs using the current handle while
retaining prior PTT/peer state. Accepted audience admission must bind the session
to its Agent and revoke queued audio/routes on identity change; filtering only by
current group peers is insufficient. Sep8 19:11 source repair binds group state to
the local Agent, prevents an identity switch from adopting a live ENet session,
and retires old peer probes/routes after transport teardown. Voice now gates send
and receive by accepted current-owner membership, resets on Agent changes, and
clears revoked playback. Seven-path manifest source-owner7-settled.json SHA774288F4
was frozen for the coordinated client/native6 checkpoint. Native6 passed40
assertions on tests09DD35F0/fullD9048BFF with no drift, and clientE69F3AF4 built
with Opus. Actual Agent/voice runtime verdicts remain pending;
transport/replay/mixing/VAD remain open.

## 2026-09-08 Bot-variant scanning hides admission failures

T-MENUS-006; source-confirmed. ScanBotVariants returned0 for overlong paths,
unreadable/non-directory roots and silently skipped parse/registration errors;
aggregate ScanComponents skipped overlong children. Sep8 18:29 exact-owner
repair returns negative failure while retaining admitted counts; only optional
directory absence is zero. No atomic rollback is claimed. Source manifest
.claude/menu0908/scanner-status-review/applied-two.json; build and actual
filesystem/client validation pending. Full Apply must consume these statuses.

## 2026-09-08 Movement decoder accepts nonfinite fields and unwired stack data

T-NETWORKING-011/F05, source-confirmed. Eleven ordinary floats and conditional
zoom reached movement state without finite validation; weapon_id was never
initialized by the legacy wire decoder. Sep8 18:25 numeric7 extracts the actual
codec, zero-initializes a local candidate, rejects NaN/Inf transactionally and
publishes only a complete record. Both ingress scratch records initialize before
optional-force-field inspection. Native4 plus affected inventory contract passed5/362 at18:36 on A832459C/
fullD0FE92DF; matching client compiled, movement runtime remains pending. Wire layout and weapon clamp unchanged. Both receive
paths share the correction; acknowledgement/force policy and actual movement
simulation remain open, so this is not server-authoritative movement acceptance.

## 2026-09-08 Save-only authoring reports persistence success after failure

18:24: native six production-executor cases/79 assertions PASS, tests build0
and source guard0/no drift (save-status6-exit.json, save-status-native-guard-exit.json).
Tests use controlled destination callbacks; actual adapter/UI/client proof
remains pending. Silent enable refusal is checked in all three save-only paths.

T-MENUS-006; source-confirmed. Audio import and chrome authoring used the void
modmgrSaveConfig path, which ignored configSave failure and merely logged Agent
failure. Audio success sound preceded saving; missing registry matches also
fell through to success. Sep8 18:16 source repair adds checked destination and
Agent-identity results, connects both audio paths and chrome, and moves audio
success after persistence. Source manifest .claude/menu0908/save-status-review/
applied-seven.json. Six orchestration cases and matching client/UI verification
are pending. Propagation check found full Apply still needs end-to-end status;
this repair does not claim complete Apply or cross-file rollback.

## 2026-09-08 Animation command namespaces and failed admission (T-MODDING-002)

17:54 bounded FIX VERIFIED: exact native command namespaces and category/order
repairs pass installed15/18 outer58.904s. Public-source coverage independently
matches110 unique base command sources to593 native commands, zero missing IDs
or fixup failures. Batch14 receipts preserve source408ABDB/full20965BC6/clientD988.
Original failed runs below remain evidence. Full mod-import transactional order,
immutable slot retirement and graph gameplay still require implementation.


17:45 installed fixture15/17 outer passes, but base-loading acceptance is RED:
loaderWalkerMarkBaseArchiveEntry overwrites semantic category with base. The new
compilation pass consequently skipped all110 base command sources; native pool
logged animations=0/guncmds=0. Restore the animation category after provenance
marking and reject category disagreement, plus require110 native base commands
in the installed fixture. Original batch13 pass is retained as fixture evidence
only; supplemental review records the failed base-content coverage.

17:24 installed RED: the first strict-reader client stops boot with61 failed
command admissions. The emitter writes trigger=z with an optional zero slot;
the reader omitted that supported shape. The walker also compiles commands
before all animation rows and audio FileProviders have been registered.
Retained batch12-command-installed15-exit/result (7/17 outer,97s), unchanged
source4036A3E/full2093EA1D/clientB56D. Repair registration order and source parity;
do not weaken dependency validation or claim this installed run passed.

Public command references such as mod_a:reload and mod_b:reload were stripped to
reload by lookup and deferred fixups. The bare-name registry could bind a different
mod's animation. The base walker additionally ignored source parser failure and
logged success; missing character-clip references did not set its rollback flag.
New strict public reader and catalog-ID loader entry point are integrated in the
walker, local scanner, and distribution caller. Complete typed scalar/reference
preflight precedes pool writes; exact qualified IDs survive lookup/fixup/replacement.
Six reader cases passed; the combined8 gate stopped on an unrelated obsolete
glTF-buffer prohibition assertion (retained JUnit, no source drift). That contract
now follows the actual shared buffer loader. Indirect/self command include cycles
are rejected before publication, with installed rollback proof staged. Revised
affected contracts and installed proof are pending. Immutable dependency leases and removal
of the remaining fixed command backing pool are still separate required work.

## 2026-09-08 remote lobby roster omits the connected listen host (T-NETWORKING-011)

Open, verified16:49 in the installed two-client pair-r2 capture. The observer
authenticates as client1 and sees host client0's Crispy Monkey room with1/4
capacity, but Connected Players shows only its own Agent and Players1/32.
The server-side listen-host row fix is insufficient for remote roster views.
Trace complete authoritative player identity/state replication and the separate
selected-room membership projection before claiming lobby acceptance. Evidence:
`.claude/mp0908-repairs/runtime/room-list-pair-r2-visual-review.json`.

17:19 private roster18 draft addresses this through explicit presentation
snapshots and room-filtered shared views. It is uncompiled/unapplied; seven native
cases and the SDL Join pair are unrun. Review also found the Counter-Op preview
holding a pointer to a block-local player view after its lifetime, and selection
following a compacted row instead of client identity. Both are corrected only in
the reviewed roster18 unit, integrated at17:27 with all settled hashes matching.
Provisional reconnect publication is explicitly excluded to preserve the existing
restoration transaction. At17:42 native7 and21 affected cases are accepted;
client and SDL Join verification were pending. At17:59 Join-r2 shows correct
global/room rosters but remains FAILED35/37 due one initial malformed0x7b warning.
Empty body/head IDs were incorrectly sized2 vs writer3; corrected canonical
lobby_roster_wire.c and two focused regressions passed the affected native9 at
18:12 (331490B9/fullC9A6A2C9). Alternate zero-length empty decoding remains
compatible; undersized output is atomic. Propagation found existing room-wire
already handles both encodings. Matching-client Join-r3 passed37/37 at18:50 on6C12FA9B/full639A68A8,
with both initial packets accepted and global2/joined-room2 visible. Initial
roster omission/empty-ID rejection now has native and installed proof; distinct
profile mapping, third-peer isolation and full multiplayer acceptance remain open. See repair audit and roster9-empty-r3-exit.json.

## 2026-09-08 Empty weapon visibility list executes terminator (T-MODDING-002)

Severity: invalid memory read / unintended model visibility mutation. Found while
preparing owned graph equipment. `bgunExecuteGunVisCommands` tests END only after
executing the current row and advancing. `loader_pool.c:parseGunvisArray` can
legitimately materialize a terminator-only list, which the consumer executes and
then reads past. The new equipped builder returns NULL for an empty list, but the
consumer must also honor END before dispatch. Propagation search found this one
consumer; menu part visibility already checks its sentinel before execution.
Consumer now checks END before dispatch. Installed harness calls null and a single
END row with null hand/model storage; the explicit required log line is present.
Client build and native-source guard pass. Installed weapon_mesh_ingress_smoke
passes14 cases/16 outer assertions in54.0s on client980529BE/source399078794E7
(full native20908B2CFF2A), including required EMPTY_VISIBILITY proof. Receipt:
.claude/asset0905-continuation/batch11-visibility-installed14-result.json.

## 2026-09-08 primary Lounge invalidates authoritative room lists (T-NETWORKING-011)

Source/native corrected16:28; remote room-list reception verified16:49 through
pair-r2's31 assertions and both observer images. Full player-roster acceptance
remains separate and open above.
`roomsInit` previously opened permanent room0
with zero capacity and a zero-initialized creator. `netmsgSvcRoomListRead`
correctly rejects zero capacity transactionally, so that one entry prevents
every room in the list from being published. `hubTick` updates only state.
Initialize capacity to HUB_MAX_CLIENTS and creator to the unowned sentinel;
preserve valid capacity through permanent-room reset. Add a direct fresh
initialization/reset case, then verify room-list reception in the next client.
The earlier host Create18 receipt proves assignment, not list reception.

## 2026-09-08 catalog toggle uses enabled-only identity lookup (T-MENUS-006)

16:30: actual catalog10, affected lifecycle1 and native guard PASS with no
drift on source2087E5AC; test build green. Direct tests prove re-enable and
rejected-teardown restoration. Matching client build remains pending; full
typed runtime and Apply UI acceptance are separate and still open.

16:23 update (N-0130): seven paths applied/frozen, receipt
`.claude/menu0908/modmgr-apply-review/catalog-applied-seven.json`. Both resolver
sites repaired; checked mutation outcomes and ten actual-catalog tests added.
Compilation and execution remain pending the MP-owned joint build.

Open, high impact. At 16:19 root traced both publication and rollback in
`assetCatalogSetEnabled` to `s_resolveLocked`, which returns only enabled rows.
Re-enabling a disabled row therefore aborts at its transaction check, and a
failed disable cannot restore its enabled bit after it was set to zero.
Use the existing `s_resolveAnyLocked` at both mutation sites while keeping
ordinary gameplay resolution enabled-only. Both repairs are prepared in
`.claude/menu0908/modmgr-apply-review/catalog-fragments.json`, not applied or
tested yet; ten actual-catalog tests include re-enable and rejected teardown.

> Live one-off defects and release regression gates only. Recurring architectural
> classes belong in [systemic-bugs.md](systemic-bugs.md). Release priority and
> ownership come from the canonical Workbench, not from this file.
>
> The complete pre-consolidation ledger is preserved verbatim at
> [_old/bugs/2026/bugs-through-2026-08-12-pre-v1-consolidation.md](_old/bugs/2026/bugs-through-2026-08-12-pre-v1-consolidation.md)
> with SHA-256
> `80A567D268C22901B6117AF310BE206336EC5CC9A9E61B221172F20296093D4E`.

Back to [index](README.md).

## 2026-09-08 Configured sound identities exceed reverse table (T-ASSETS-046)

Critical, corrected and installed-verified15:50. Public audio
installed6 stops0/6,3/14 outer on unchanged371/F0A104AC/client21C5C296. The
admitted base:sfx_unlabeled_avrm source has correct public controls but packed
sound identity32928 exceeds assetcatalog_load.c's4096-entry reverse table.
The table now covers the complete unsigned16-bit admission domain; installed
checks require the exact packed identity and reject negative/wider inputs.
Corrected installed6 passes14/14 outer on371/9D551466/client5A39CAD6 in70.1s;
guard and client build pass. Receipt batch8-packed-audio-installed6-exit/result.json.
Evidence: batch8-public-audio-installed6-exit/result.json. Propagation audit
also finds sndStart and Scenario audio helpers collapse configured references
to leaf IDs; preserving configured source identity through those callers is
separate open work, alongside PCM indexing/migration/chains. No playback parity
claim is made by extending catalog resolution alone.

## 2026-09-08 Base audio seed has no reverse identity (T-ASSETS-046)

Critical, fixed and installed-verified15:37. Base audio registration initialized ext.audio.sound_id and
runtime_index but leaves source_soundnum at -1. The source-authority replacement
transaction correctly preserves existing native identity, thereby preserving an
unset reverse route. Installed diagnostic192216Z confirms SFX candidates=0 while
voice-file and music targets are admitted; no audio identity cases can begin.
Configured aliases also omit their packed reference and MP3 file provenance.
The two-path correction initializes identity at base seeding, preserving
source replacement rules. Applied15:29; focused8, guard and installed8/15 outer
pass on unchanged371/F0A104AC/client21C5C296. Prepared patch in
`.claude/asset0908-drafts/audio-seed-identity/`. Native PCM extraction indexing is
a separate defect; this fix does not establish waveform parity or migration.
Evidence: batch8-audio-identity-preflight-exit/result.json, clientFA930431 and
unchanged360/7EB79621. Public-audio6 and weapon14 are unrun after this first-red.
Applied-source receipt: `.claude/asset0905-continuation/batch8-audio-seed-applied.json`.
Acceptance: batch8-audio-seed-installed8-exit/result.json and focused8 JUnit/name
audit. Actual source file handles, repeated replacements, custom slots and
folder/received late rejection rollback pass; no audible or native waveform
indexing parity is claimed by this identity repair.

## 2026-09-08 Agent Create control overflow / focus transition (T-MENUS-003, N-0126)

15:29 update: clientFA930431 builds with the layout and diagnostics. One
changed-source desktop run remained at AgentSelect because supported window
activation failed twice;9/22,0events/no captures, unchanged source7EB79621.
No layout/navigation/provenance pass. Startup retries stopped; see
`.claude/menu0908/agent-layout-provenance-exit.json` and the menu audit.

Layout repair applied, unvalidated. Desktop observation after real window
activation showed long body/head names and reset text extending into the black
preview. Source manually drew names outside a fixed reserved width and left
the controls group unconstrained. AgentCreate23AF9A96 contains controls in a
scrollable NavFlattened child and wraps labels with separate count/reset rows.
Matching client/visual/navigation gates remain pending. Preview request was
body `base:dark_combat`, head `base:head_dark_combat`; black rendering remains
an independent asset-owner handoff, not solved by the layout change.

Input cause remains open: desktop1's real focus gain was followed by a global
Agent Select Accept before the readiness stability interval completed. Fixture
events stayed0/10, result11/22; no scripted route or GL-capture acceptance.
ActionMap9872D522 adds verbose-only VK edge, prior-held and focus diagnostics
to establish provenance. No guessed input behavior change or further unchanged
startup retry. Evidence is in the menu audit and `.claude/menu0908/` receipts.

## 2026-09-08 Mod Manager malformed state publication (T-MENUS-006, N-0125)

16:04: matching clientE36F3211 buildPASS, source387 unchanged and native guard
reports ok. Bounded source/build/direct helper-IO repair is complete; ordinary
UI/restart and related full Apply/component-state defects remain open.

16:02: focused production persistence14 PASS/256 assertions with zero source
or binary drift, receipt `.claude/menu0908/modmgr-persistence14-exit.json`.
The source defect is repaired and direct helper/file-IO validation passes.
Matching client build/native guard and ordinary UI/restart proof are pending.

15:56: four product/test paths applied after explicit release and frozen with
exact hashes in `modmgr-state-review/applied-four.json`. Shared CMake/build
pending; all fourteen cases and runtime remain unverified. Prior findings below
describe the repaired source baseline, not a validated current runtime result.

15:50 follow-up: the paired writer also inserts unescaped IDs into JSON and
directly truncates the destination. A five-path strict-load/ordered-publication
and escaped atomic-save candidate with fourteen tests (15:52 revision) is prepared in
`.claude/menu0908/modmgr-state-review/manifest.json`, UNAPPLIED/UNRUN pending
the shared source window. This does not close checked Apply or component state.

Open; high impact on saved mod selection. Source review in the menu session
found `modmgrLoadModsEnabledJson` disables live registry rows before validating
its input, accepts EOF in place of a closing array, ignores invalid/non-string
tokens, and reports successful load. The shared tokenizer accepts unterminated
strings. A truncated or malformed `mods-enabled.json` can therefore publish a
partial/empty selection and suppress fallback. This has not been runtime-tested.

Required repair: validate a complete ordered candidate before any live flag or
order change; reject malformed strings, overlength IDs, invalid separators and
trailing content, preserving prior live state on rejection. Cover the actual
production reader and publication path. The related disabled-state iterator,
late-registration replay, ordered restart persistence and checked Apply defects
remain open in the menu audit. No product changes during the joint source freeze.

## 2026-09-08 menu dedicated-target configure failure (T-MENUS-003)

OSK context correction passed;87 further cases pass (296 total), including all
OSK cases. The Agent popup contract then failed a stale whole-function count
(four suppressions versus three) because the new Back branch also suppresses
activation. The test now checks suppression between each actual popup open and
its body submission, preserving ownership without prohibiting other guarded
transitions. Product unchanged;54 failed/unrun remain. Receipt:
`.claude/menu0908/current-menu-context141-exit.json`.

Pointer geometry correction passed;11 further cases passed (209 total). The
OSK replacement-context section then failed because CreateContext restores the
previous current context when one already exists. The test never switched to
its replacement. It now explicitly selects that context, verifies the switch,
and uses scope cleanup to restore the original even on assertion failure.
Product unchanged;141 failed/unrun cases remain after rebuild. Receipt:
`.claude/menu0908/current-menu-geometry152-exit.json`.

First client compile stopped at MainMenu's new read-only tab producer: its
ImGuiWindow/SkipItems observation needed imgui_internal.h. Added the missing
declaration include only. No client linked or ran; retry requires refreeze.
Receipt: `.claude/asset0905-continuation/batch6-hand-client-compile-error.log`.

Next152 remaining: the first two Settings keyboard/controller cases passed;

Diagnostic established the pointer cause: expected Actions NavId was correct,
but NavRectRel produced center(212,48), above the current Actions row. Native
HoveredId was0 and the press activated a different ID. Test pointer geometry now
uses the native cursor position where the component submits its first Button;
the press must activate the exact Button ID before release. Propagated to Editor
Close's row rectangle. Product unchanged. Diagnostic receipt:
`.claude/menu0908/pointer-diagnostic-junit.xml`. Coordinate correction unrun.
pointer Actions stopped with its synthetic movement and press sharing one
frame. The precise pointer failure remains under investigation; both cases submit an
ordinary hover frame before press/release (Actions and Editor Close). Product
unchanged; the cadence hypothesis is unverified. Retain198 current passes. Receipt:
`.claude/menu0908/current-menu-remainder154-exit.json`.

The hover-frame retry failed the same pointer case with zero new passes, so
cadence is not established as the cause. Added focused native pointer position,
hovered window/item and active-ID diagnostics before release. Next gate is the
single failed case under this diagnostic build, not a broader unchanged retry.
Receipt: `.claude/menu0908/current-menu-remainder152-exit.json`.

The210 remainder passed56, then the cold Settings helper harness failed opening
Actions despite matching NavId. ImGui's FocusApi explicitly leaves nav visibility
unchanged; a cold context has it hidden and cannot activate buttons. The test
fixture now applies the production visible-focus policy when requesting native
focus and asserts that prerequisite. No product change. Current retained total
is196 exact passes;154 failed/unrun cases remain after the test-only rebuild.
Receipt: `.claude/menu0908/current-menu-remainder-exit.json`.

The third shared build passed. Current menu gate then passed140/350 cases and
stopped at the old capture-only scroll source assertion. The integrated OSK
correctly adds its neutral-release gate to scroll ownership. Updated only
test_right_stick_scroll.cpp to require the shared sampled axis and both owner
guards; propagation search found one stale expression. Product unchanged.
Retain140 exact current-unit passes and run the210 failed/unrun cases after the
test-only rebuild. Receipt: `.claude/menu0908/current-menu-exit.json`.

The corrected configure passed; the next build stopped linking the dedicated
Firing Range target. Windows COFF retained reference-pointer sections for
unrelated training dialogs despite function/data section GC. The existing
PDGUI_TRAINING_RENDER_TEST define now excludes unrelated renderer bodies and
registration while retaining the exact weapon-list renderer and shared helpers.
Normal client compilation still includes all dialogs. This is a test-link
correction, not a game behavior change; another frozen build is pending.
Link error: `.claude/asset0905-continuation/batch6-hand-build-tests-menu-link-error.log`.

The first shared build stopped before compilation at CMakeLists.txt:1332:
`target_compile_features no known features for C compiler GNU version 15.2.0`.
The supported headless wrapper forces compiler identification and does not
populate this feature table. The new dedicated menu targets redundantly queried
it despite project-wide C11/C++20 settings. Removed that one redundant call;
both targets now inherit the same standards as pd-tests. Propagation search
found no other target_compile_features call. Correction awaits a fresh queued
build; no test or runtime pass is implied. Error receipt:
`.claude/asset0905-continuation/batch6-hand-build-tests-configure-error.log`.

## 2026-09-08 multiplayer source audit

T-NETWORKING-010 records 18 prioritized source findings in
[the multiplayer audit](audits/2026/multiplayer-audit-2026-09-08.md).
The main defect classes are listen-host lobby dispatch, nontransactional room
membership, stale room settings/playlist replay, cross-channel distribution
completion and approval, receive allocation failure, client-position movement
authority, chat endpoint/delivery, and voice audience/mixing. All remain open.
No repair or new build/runtime/visual/controller acceptance is claimed.

## 2026-09-06 menu batch source findings

T-MENUS-006 source review confirms archive import can lose the previous file:
`modmgrCopyFileAtomic` unlinks the destination before rename, then deletes the
candidate if rename fails. Hub import and received archive installation reach
this helper. A separate scratch correction will use the existing checked
atomic replacement API; no current product edit or multi-file transaction claim.
Component-state persistence also iterates only enabled entries while its writer
requires disabled entries, and rebuild replays state before package loading can
re-register rows. Truthful Apply failure reporting must include refused catalog
retirement and mount/admission outcomes; changing only the UI result is insufficient.

T-MENUS-003 propagation review at14:01 ET confirms Room character preview can
redirect a selected public body/head mesh through a native filenum alias:
`pdguiCharPreviewRequest` -> `menuRenderModel` -> `modeldefLoadFromHandle`
replaces the supplied FileProvider handle with the first external filenum
match. The body/head manager path retains the selected source, so its passing
load alone cannot establish preview correctness. Asset root owns the narrow
`src/game/modeldef.c` explicit-source preservation and real selected-triangle
witness, alongside native primary alias admission and both-order hand/private
collision rejection. No preview/runtime proof yet; menu.c remains unchanged.

Tracked under T-MENUS-003/T-INPUT-006/T-MENUS-006: reachable Extended Key
Bindings generated 21 rows over 19 actions, unchecked saves dismissed capture
on failure, and missing device bindings could overwrite slot zero. The current
batch adds row guards/count correction, checked persistence and bounded device
slot lookup. Shared interface deletion also lacked Back and opened its status
popup under the closing popup's ID. Affected focused cases now pass within302;
the client builds, but ordinary menu verification is blocked in asset boot.

The remaining caller audit additionally establishes source defects under
T-MENUS-003: Social polls raw Back around editors and bypasses shared ownership;
Agent/Solo/Cinema/Main Menu/Results allow competing forward and Back outcomes;
Firing Range can push through both global Accept and native selection and
override its focused Back footer; Cinema clamps away its valid Back index on
the next frame. Exact scopes and source hashes are in
`.claude/menu0906-next/remaining-audit/README.md`. These repairs are a subsequent
scratch unit, not part of the previously accepted 235 cases. Mod Manager core
Apply/save status and enabled-only component-state enumeration also need a
separate review; UI staging cannot establish disk/runtime rollback.

Historical propagation review found the underlying Main Menu could handle Back
while the Modding Hub overlay was visible. Current source checks Hub visibility
before `ImGui::Begin`, makes the parent window non-interactive, and returns
before Main Menu Back/Social/body actions. The focused production-source
contract in `tests/test_menu_graph.cpp` pins that ordering. The source gap is
corrected; ordinary controller/MKB and visual navigation proof remains open.

T-INPUT-008 source audit also confirms raw joystick buttons23-32 (zero-based
slots22-31) share virtual keys with synthetic stick directions/triggers.
`input.h` allocates both meanings inside the same32-slot player stride, and
`actionmap.cpp` sends raw button and axis events to those same keys. They cannot
be independently bound; labels such as Btn32/Axis6+ reveal the ambiguity but do
not repair dispatch. This is source-confirmed, with no physical reproduction.
Sep22 source correction keeps all old values/names as compatibility aliases and
appends distinct precise button/axis keys. Capture, dispatch, saved names,
Settings and glyph candidates now use the same conversion; the selected
physical-source winner is retained through release. Scoped client/test builds
and direct converter/resolver/glyph cases pass. The broader menu suite remains
red on four unrelated source-contract assertions, and an ordinary raw joystick
plus physical/visual proof has not run. Old ambiguous profiles remain ambiguous
until rebound; see `.claude/menu0906-next/raw-vk-audit` for the compatibility
design and `.claude/menu0922/raw-vk-final-junit.xml` for the retained broad red.
Follow-up source review found a legacy alias could be selected by one physical
source after its other source was shadowed by a precise bind, while the generic
alias label still listed both. The glyph callback now carries the resolver's
actual winning physical VK; focused source and production-helper cases pass
12/957 on rebuilt client/tests. Ordinary device/visual proof and the retained
broad test red keep T-INPUT-008 partial.

---

## Routing rules

Sep6 asset continuation: T-ASSETS-049 also owns music sync cursor defects found
during real Vorbis integration. NaN playback rate reached a float-to-integer
cursor cast; an oversized seek narrowed before its frame bound and retained the
original huge fractional offset. Applied ordered-rate handling and clamped the
floating seek before narrowing, preserving a bounded fraction. A real PCM mixer
regression is prepared; build and runtime validation are pending. T-ASSETS-048
also requires transactional registration of character body/head dependency edges;
source policies alone cannot make embedded custom children usable.

- A confirmed current defect stays in **Active 1.0 defects** until its root cause,
  propagation audit, implementation, and required runtime proof pass.
- A historical fix that only needs broad release revalidation belongs in a
  **Release regression cluster**, not as a separate active task.
- Product or infrastructure gaps belong in Workbench tasks or validations. They
  are not duplicated here as bugs.
- Post-1.0 defects remain visible but are never selected by the default 1.0
  prompt unless Mike promotes them.
- The next unused numeric bug ID is **B-1108**. Never reuse an archived ID. If an
  old unnumbered symptom recurs, assign a new B-number with current evidence.

Status terms:

- `confirmed`: current source or a current production repro proves the defect.
- `repro_required`: a credible report exists, but the current build still needs
  a bounded reproduction before code changes.
- `regression_gate`: implementation exists; a broader release receipt must prove
  the behavior remains fixed.
- `post_1.0`: retained, but outside D-004 release scope.

---

## Active 1.0 defects

The 2026-09-06 menu audit under T-MENUS-003 found shared input ownership
defects: the ImGui bridge overwrites physical arrow keys with action-map state
while its broad keyboard-capture gate swallows navigation bindings; glyph
lookup uses an incomplete private context list; menu scrolling reads gameplay
aim axes that are correctly suppressed. T-MENUS-004 repairs destructive modal
Accept overriding visibly focused Cancel in shared confirmation, End Match and
Agent Copy/Delete. T-MENUS-005 repairs binding capture leaking into parent
navigation and missing controller-accessible clearing. These are confirmed
source defects; behavioral and ordinary-client acceptance are pending.

The same audit's propagation pass confirms additional T-MENUS-004/003 defects:
Solo Restart, Agent Copy/Delete and Room Change Character open popups under a
different ImGui ID scope from their renderers; Agent and Room parent shortcuts
ignore child owners; native combo Back can close both combo and parent screen.
These remain unrepaired at the first shared verification freeze. The menu audit
records exact production locations and the stable-owner repair boundary.
The first actual ImGui gate additionally proved that forced default focus kept
the navigation cursor hidden, preventing Enter/controller activation. The
bounded 11:10 ET repair makes that focus visible across the affected dialogs and
also removes Cheats Unlock Everything's global Accept override with No priority.
Its matching build and remaining behavioral gate are pending.

The active user-directed asset lane has three newly source-confirmed classes
recorded by Workbench without reusing historical bug IDs: T-ASSETS-047 owns
missing bot-profile manifest unlock metadata; T-ASSETS-048 owns fixed-head-only
character consumers rejecting native default/random/integrated-head templates;
the current asset audit records legacy base MP3 framing/sample limits, reverse
mapping loss on generic audio replacement, and advertised Vorbis entrypoints
that only decode WAV. The initially reported missing private MP3 decoder wiring
was an incomplete trace and is explicitly withdrawn in T-ASSETS-046.

The 2026-09-05 user-directed asset audit also tracks confirmed source-reference,
typed graph value, glTF buffer, and animation-tail defects in T-ASSETS-039 and
T-ASSETS-040. Exact producers, consumers, and remaining architectural gaps are
recorded in the [current asset contract audit](audits/2026/asset-source-runtime-contract-2026-09-05.md).
B-1107's replacement implementation and verification run under T-ASSETS-038.

| ID | Severity | Status | Current truth | Workbench route | Closure gate |
|----|----------|--------|---------------|-----------------|--------------|
| B-919 | Low | confirmed | The collision hit-sound condition in `propobj.c` compares `WEAPON_COMBATKNIFE` twice. The intended second family is not proven, so this must not be fixed by guesswork. | V-010 | Verify intended base behavior from authoritative source, correct the shared mapping, add a focused propagation test, and prove combat-knife plus bolt-family impacts in an ordinary client. |
| B-1106 | Critical | source_connected | Exact frozen client `36EA05D1...` completed clean 733/733 `.pdmesh` extraction, then `base:sp_body_118` / nested `base:model_cchicrob` failed the runtime render-stream contract and rolled back 64 Main Menu loads. The exact first mismatch is 33 corners across 11 faces in Type-3 groups `node_2_dl`, `node_6_dl`, and `node_10_dl`: the producer resets geometry knowledge between consecutive opaque/translucent lists while retaining the live RSP vertex cache, but flattened schema v2 had no typed cache provenance and the consumer conflated each vertex's earlier `G_VTX` load-time snapshot with later triangle-time geometry. Current schema v3 records every ordered `vertex_load`, every triangle's three exact `vertex_cache_slots`, one Type-3 `vertex_scope` that retains the bounded 64-slot table, and one non-Type-3 `vertex_cache_reset` that clears it. C/Python validators simulate the same table, reject unloaded/false snapshots, malformed arrays, and matrix indices above 32766, and require every flattened load to have a restorable draw-state domain. The compiler's deterministic Type-3 baseline owns inherited lighting and texture-generation state but not caller-owned fog, which remains fail-closed. Schema v2 remains strict; schema-v3 load/draw divergence requires explicit load provenance. Export/cache v25/v28 plus generated-modeldef cache v12 invalidate predecessors. Luna xhigh identified the retained-slot and parser/bounds gaps; Sol integrated the exact source, validators, conformance cases, and propagation contracts. Exact source-frozen client `7EF396C9...` / tests `4D14886D...` pass focused 143/8, complete 67,451/1,231, all conformance/workflow/source/native guards, and zero source overlap. No replacement extraction or visual receipt is claimed yet. | V-010 | Run one clean 733/733 extraction and ordinary 3440-wide smoke proving the Main Menu manifest commits, `base:model_cchicrob` builds, both door models activate exactly once, and Carrington/Campaign/Combat Simulator retain correct visuals plus V-009. |
| B-1107 | Critical | implemented | Metadata colmesh success bypassed typed runtime activation/hydration, and colmesh unload omitted adapter detach. T-ASSETS-038 now prepares private geometry, activates and hydrates adapters, publishes once, rolls back acquired state on rejection, and detaches before geometry release. The 2026-09-05 ordinary client passes all 19 Scenario transaction cases and match start with strict readiness retained. The corrected V-006 fixture now passes 32/32 including clean exit on unchanged client `2097E1C8...`; source manifest `44186EA3...` is unchanged across the continuation run. | T-ASSETS-038 / V-006 | Retain the original timeout receipt and `.claude/asset0905-continuation/v006-result.json` (32/32 pass). T-TESTS-003 corrected both affected fixture roots to timeout_seconds=125. |
| B-1109 | High | source_connected | The gun model loader's queue targets were declared `uintptr_t *` while pointing at 4-byte counters (`gunctrl.memloadremaining`, u32, and `gunctrl.handmemloadremaining`, s32). On 64-bit every read and write through `loadmemremaining` covered 8 bytes; for `memloadremaining` the upper half is `masterloadstate`, `gunloadstate` and `loadfilenum`. Two effects in `bgunTickGunLoad`: the capacity clamp `loadsize > remaining` could never fire for gun and cartridge models, because the load-state bytes made `remaining` huge, and an underflow at the end of the texture stage would borrow into those state bytes. Latent on dev a8ab67f3 (`src/game/bondgun.c:4839-4850` and `:4971-4976` there). First found by the paused T-MODDING-011 lane (Workbench note N-0817, never built). Fixed on `week-pd/core` 2026-11-14: targets are `u8 **` and `u32 *`, the signed hand counter is addressed through its unsigned alias, both read-modify-write sites and all four call sites are typed, and a real clamp now logs `BGUN.LOAD.CLAMP`. | T-MODDING-011 / V-010 | `tests/test_gun_load_target_storage.cpp` passes in pd-tests; the campaign auto-runner and a Combat Simulator cycle show no `BGUN.LOAD.CLAMP` line and no gun load failure. If a clamp does fire, raise the gun memory budget rather than restore the unchecked path. |
| B-1110 | Medium | source_connected | First-launch extraction is about 20 times slower on a hard disk than on NVMe. On 2026-11-14 a windowed first launch from `D:\PD2-wt\shared-base` (D: is a 7200 rpm Seagate ST2000DM008) reached `LOADER.UNIVERSAL.SUMMARY: scanned=5562 registered=5562` after 19 min 33 s; the accepted headless run of 2026-09-23 on C: (NVMe) reached the same line in about 60 s. The 733 meshes alone took over 80 s on D: against 4.7 s on C:. Consequences: `boot_smoke` (clean install, scripted exit at 90 s) cannot pass on a hard disk (7/14 on 2026-11-14, no crash, every miss is a line that had not been reached yet), every clean-install fixture inherits the same cost, and a player installing to a hard disk waits about 20 minutes on first launch. Profiled 2026-11-15: every extracted archive went through `modArchiveFinish`, which forced it to disk (`_commit`) and then renamed it with `MOVEFILE_WRITE_THROUGH`. Timed on 300 new files of 45 KB: on D: 99.3 ms per file with both, 60.5 with the flush only, 44.5 with the write-through rename only, 1.0 with neither; on C: 1.9 against 0.6. Over about 5,600 archives that is about 9 minutes on D:. Fixed the same day: the ROM extractors publish with `modArchiveBeginDerived` (same temp file and atomic replace, no per-file flush, no write-through), and the directory cache stamp, which lets a later launch skip a directory without opening its files, first calls `modArchiveDerivedBarrier` for its directory and is not written if that fails. A deferred flush of files the lazy writer has already written cost 3.6 to 40 ms per file on D:. Crash safety: a stamp never reaches the disk ahead of the archives it describes, and an archive torn by a power cut before its barrier reads back as a written prefix plus zeros, so its end record is missing and the per-file validators reject it and extract it again. Player edits and authored archives keep the durable writer. The flushes of one barrier run side by side on the boot pool (stamps are written on the catalog-work manager thread): 1,000 new files took 17.6 ms each one at a time and 8.3 ms with four at once. Measured live on D:, clean `--extract-assets-only` run in a fresh folder: 3 min 33 s to the loader summary with the barrier one file at a time (15679a9a), 2 min 47 s with it on the boot pool, against 19 min 33 s before; both 5,569 of 5,569 registered, 0 envelope failures, 0 LOUDFAIL, all 23 stamps written. Propagation (SP-89): the mod-transfer extraction `pdcaExtractArchiveBegin` syncs every member the same way (MULTIPLAYER's area, not changed). | V-010 / T-TOOLING-003 | `tests/test_modarchive_derived.cpp` (`[b1110]`: derived publish and atomic replace, barrier per folder, removed intermediates, four publishing threads, stamp written only after its barrier, every extractor derived, durable writer unchanged). Live timing done (above). A clean install on D: still needs about 3 minutes before the title, so `boot_smoke` (fixed 90 s exit) and the other 137 clean-install fixtures keep running from the pre-extracted shared base on D:, as this week already does. Remaining gate: a windowed first launch on a hard disk in Mike's stable-build run. |
| B-1111 | Critical | source_connected | Objective operands in public scenario and mission source were byte-swapped, and the graph objective path used them as written. `port/src/romextract_pdarena.c` read the second word of `destroy_object`, `collect_object`, `throw_object`, `complete_flags` and `fail_flags` commands through `PD_BE32`, although the setup loader has already converted that word to host order (`port/src/preprocess/filesetup.c`, `PD_CONV_VAL` on `cmd1`). So `setup.fields.json` and `objectives.json` carried tag 6 as `tag_100663296` and flag 0x00002000 as `stage_flag_0x00200000`. `port/src/scenario_source_runtime.c` swapped the `setup.fields.json` values back when it rebuilt the setup stream, which made the native path correct and hid the wrong numbers. The mission graph path did not: `objectiveCheck` takes its status from graph operands when a mission graph is active (`src/game/objectives.c`), and those came from `objectives.json` unswapped. Effect in the 14 missions that have a `.pdmission`: every flag criterion tested the wrong stage flag and every non-zero object tag named an object that does not exist, so `collect_object` criteria read as failed and `destroy_object` criteria as complete. Evidence on 2026-11-14: across all extracted scenarios 226 of 231 objective flag masks match a flag the stage's own AI lists use only after a byte swap (175 as written, by coincidence of twin flags); every non-zero `collect_object` tag in the corpus has the form 0xNN000000; in the campaign run Chicago logged `tag=100663296 present=0`, then `if_objective_failed objective=0 ... branch=1` and `phase=failed` in the first second of play, and Defense did the same. The campaign fixture never saw it because it force-completes objectives; the seven missions without a `.pdmission` were unaffected because they run the native path. Fixed on `week-pd/core` 2026-11-14: the extractor writes the real values, the setup rebuild writes them as they are, the scenario cache kind is bumped so every install re-extracts, and `objectiveCheckGraphSource` now refuses a criterion whose tag or flag differs from the live setup command (fatal with mission source debugging, a warning and the native path otherwise). | T-EXTRACTION-001 / V-010 | After re-extraction no `tag_` reference above 0xffff and no objective flag mask that the stage's AI lists never touch; the campaign run shows Chicago and Defense with `present=1` and no `phase=failed` before the forced end; no `criteria tag or stage flag differs` line in any mission. Then one objective per kind completed by real play (flag, collect, destroy, holograph). |
| B-1113 | High | source_connected | Props on pads outside the collision-derived room boxes were never created in extracted stages, among them objective objects. The game puts a pad into a room by testing it against the room boxes (`bgFindRoomsByPos` in `setupPreparePads`), and `setupCreateObject` only creates a prop when its pad has a room. The background file carries one authored box per room (the start of section 3). The scenario-source path (`bgBuildScenarioSourceTables` in `src/game/bg.c`) did not have them and bounded each room by its collision triangles instead, which is never larger than the authored box and is empty for a room with no collision (11 of 140 rooms in the Carrington Institute, 18 of 167 at dataDyne). First seen as Defense's "retrieve the RC-P120" objective reading failed two seconds into play (`tag=5 present=0`): the RC-P120 sits on pad 282 at x 1630.0, exactly on the authored edge of room 8 and outside its collision box. Measured on the B-1111 build: 605 pads without a room across the 23 solo stage setups (265 with the authored boxes; those are outside every room in the original game too), and in one Agent campaign run 44 props not created, among them the RC-P120 and the sensitive-info prop in Defense, the 10 hover cars that circle the dataDyne tower in Defection, plants, a desk and chairs in Air Base, a glass in Deep Sea and a classic weapon in the training stage. The multiplayer arenas were not affected (no pad without a room under either rule). Fixed 2026-11-14: the extractor writes a per-room table to a new scenario member `rooms.json` (schema `pd2.scenario.rooms.v1`: authored box, origin and brightness range per room; required for a clean archive; scenario cache kind bumped), stage load applies the boxes before pads are placed, a base scenario without the member logs a warning, and the per-stage count of pads without a room and every prop that is not created are logged (`SETUP.PADS: ... without_room=`, `SETUP: object ... prop not created`). See B-1114 and B-1115 for the origin and the brightness range. | T-EXTRACTION-001 / V-010 | `tests/test_room_table_source_static.cpp`. Offline: all 132 extracted scenario archives carry the member and its 9,393 boxes equal the background files' for every stage compared. Agent campaign on the re-extracted base: pads without a room per stage equal the offline prediction at all 18 stage loads, props not created 44 to 21 (each on a pad outside every authored room), `tag=5 present=1` in Defense with no failed phase, and the Villa light-flood cap warning no longer fires. The campaign fixtures require the room-table line for every stage and the RC-P120's presence. Still to do by hand: pick up the RC-P120 in Defense and see objective 3 complete; look at the hover cars in Defection. |
| B-1114 | High | source_connected | Room lights could not be shot out in extracted stages: no light had a usable position. Two causes on top of each other. (1) The extractor writes a light's quad as four nested `[x, y, z]` corners (`"bbox": [[..], [..], [..], [..]]`), and the loader added for B-943 read it with a flat-array reader that stops at the first inner `[` and reports failure, which the loader took as "field absent". So no quad was ever read and every light kept an all-zero quad. (2) The background file stores each quad relative to its room's origin: `lightsHandleHit` (`src/game/dlights.c`) subtracts the room origin from the shot before testing the quad, and `lightTickBroken` and the explosion code add it back. The scenario-source path keeps every room origin at zero because its geometry is in world coordinates, so even a correctly read quad would have sat away from its light by the room's origin (in 14 campaign stages 3 of 2,078 quads lie inside their room's authored box as written, all 2,078 once the origin is added). Effect: a shot at a light fixture never broke it, and explosions tested lights at the wrong place. B-943 was verified by source pins only, never by shooting a light. Found 2026-11-14 in the propagation check for B-1113: cause 2 from the data, cause 1 by the new stage-load check on its first run (`149 of 161 room lights ... lie outside their room's box` in every stage). Fixed the same day: the quad is read with a nested-array reader and a light without a readable quad is an error line, each `rooms.json` row carries its room's `origin`, the light loader adds it to all four corners so quads are world coordinates (every light of the 39 stages checked fits the 16-bit storage; one that did not would be dropped with an error), room origins stay zero on this path (documented in `bg.c`, pinned by the test), and stage load counts quads outside their room's box (`outside_room=`) and warns when there are any. Not covered: light glares and the sparks of a broken light are still absent in extracted stages, because that code needs the room graphics data this path does not load (`src/game/artifact.c` returns early without it). | T-EXTRACTION-001 / V-010 | `tests/test_room_table_source_static.cpp` (`[b-1114]`). Offline, over every extracted scenario: origins equal the background file's, no light outside its room's box after adding the origin, none beyond 16-bit range. Campaign run: `outside_room=0` and `without_quad=0` at every stage load; the campaign fixtures forbid the B-1114 warning, an unreadable quad and a dropped light. Still to do by hand: shoot out a light in Defection and see the room darken. |
| B-1115 | Medium | source_connected | In extracted stages a room with its lights out was about twice as bright as authored. Each room of the background file has a brightness range (`br_light_min`, `br_light_max` in the room table; 128/255 for almost every room, 255/255 for the few sky rooms). `roomInitLights` (`src/game/dlights.c`) takes the level a room keeps without its lights from a quarter of the minimum. The scenario-source path had no such data and gave every room 255/255 (`bgBuildScenarioSourceTables`), so that level was 63 instead of 32 for a room with lights, and 216 instead of 210 for a room with none. It shows when lights are shot out (not possible before B-1114) or switched off by a mission script. Found 2026-11-14 in the same propagation check as B-1114. Fixed the same day: each `rooms.json` row carries `brightness: [minimum, maximum]` as authored, and stage load writes it into the room table before the light setup runs; a pair outside 0 to 255 or with the minimum above the maximum is ignored. A scenario made by hand without the member keeps 255/255. | T-EXTRACTION-001 / V-010 | `tests/test_room_table_source_static.cpp` (`[b-1115]`). Offline: every row's pair equals the background file's for the stages compared. Campaign run: the room-table line reports `brightness=` equal to the number of rooms at every stage load. Still to do by hand: compare a darkened room (lights shot out in Defection) with the original game. |
| B-1116 | Medium | confirmed | The settings file the client writes contains input lines its own loader rejects. `s_ActionNames` in `port/src/actionmap.cpp` names fewer actions than the `InputAction` enum has (the table stops at `TestScenVisToggle`; `ACTION_COUNT` is 121 and the later entries, among them the vehicle look, handbrake and use actions, have no name), so the writer prints a bound action without a name as `(null)=E` in each of the four player sections of `pd.ini`, and `configLoad` logs `ERROR: configLoad: malformed keyvalue line: (null)=E` four times at the next start. Effect: those actions can never be rebound or keep a saved binding, and every start after the first logs four error lines that hide real ones. Seen 2026-11-14 in the restart process of the campaign fixture (both the B-1111 and the room-table runs); not caused by either. | T-INPUT-008 / V-010 | Give every action a config name (and fail the build when the table and the enum differ in length), then start the client twice on one save folder and see no `configLoad` error and the vehicle-use binding kept. |
| B-1117 | Medium | source_connected | The PC solo mission menu offers all four special assignments to every agent on Agent difficulty, while the original game unlocks them. `missionIsAccessible` (`port/fast3d/pdgui_menu_solomission.cpp`) asks `isStageDifficultyUnlocked(stage, DIFF_A)`, which for a special assignment only decides the difficulty (its own comment: Agent is gifted "if the bonus mission is available at all") and always answers yes on Agent. Whether an assignment is available at all is decided by `getNumUnlockedSpecialStages` and `func0f104720` (`src/game/mainmenu.c`): one assignment per difficulty Skedar Ruins was completed on (Mr. Blonde's Revenge, then Maian SOS, then WAR!), and The Duel once all 32 firing range weapons have a medal. So a new agent can start WAR! and The Duel from the first menu. Found 2026-11-14 while extending the campaign runner to the special assignments; the runner uses the game's own rule and refuses an assignment that rule does not offer, so its fixture seeds an agent that qualifies. Decision 8, Mike 2026-11-15: "Unlock", as in the original. Fixed the same day: the rule is one function, `soloStageIsOffered` (`src/game/mainmenu.c`), built on the original's `getNumUnlockedSpecialStages` and `func0f104720`; the PC menu's `missionIsAccessible` and the runner's `specialAssignmentOffered` both call it. Players of this port lose the early access they had. | T-RELEASE-002 / decision 8 in the Finish Tracker | `tests/test_debug_campaign_complete_hotkey.cpp` (the one-rule pins). A fresh agent sees no special assignment until Skedar Ruins is done; the special assignments fixture (an agent that qualifies) still passes. |
| B-1118 | Low | source_connected | Presence showed "in-match" to friends while the player was in the main menu. `mainChangeToStage` (`port/src/pdmain.c`) sets presence to in-match for a gameplay stage when the session looks like multiplayer or a mission is in progress, and tested the second with `g_MissionConfig.stageindex >= 0`. That field is an unsigned byte, so the test was always true, and the Carrington Institute stage, which is also the main menu backdrop, read as a match for any loaded agent: exactly what the 2026-05-17 fix in that block meant to stop. Seen in every campaign log (`PRESENCE: local state -> in-match` at the menu right after the agent loads). Fixed 2026-11-14: a mission is in progress when the stage being entered is one of the 21 solo stages (`soloStageGetIndex`). | T-RELEASE-002 | `tests/test_debug_campaign_complete_hotkey.cpp` (`[b-1118]`). A campaign run shows presence online at the menu and in-match from the first mission's load, and Credits online as before. |
| B-1119 | High | source_connected | On the behavior-graph path a character told to walk, jog or run to its preset pad did not move. The AI command takes pad 9000 (`PAD_PRESET`) to mean the character's preset pad, which the native `chrGoToPad` resolves itself (`chrResolvePadId`). The graph helper `s_aiGraphExecuteGoToPad` (`port/src/scenario_source_runtime.c`) checked the raw 9000 against the pad table, refused it, and returned "handled", and a graph answer of "handled" skips the native handler (`aiJogToPad` in `src/game/chraicommands.c`), so nothing happened. Seen once in Defense in every campaign run of 2026-11-14, at all three difficulties: `SCENARIO.GRAPH: cannot jog_to_pad AI action for 'base:scenario_defense' (missing runtime pad id 9000)`. Earlier on 2026-11-14 CORE wrote in the week note that the native handler ran instead in this case; that was wrong and is corrected there. Fixed the same day for walk, jog and run to pad (one shared helper): the preset pad is resolved before the check, the native call is unchanged. Propagation check the same day: in base content pad 9000 appears with 7 command kinds (scan of every extracted `ai/ailists.json`): the chr and target distance-to-pad checks (the graph already resolves it there), walk and jog to pad (fixed here), `face_entity` (the graph passes it to the native function) and `chr_move_to_pad` (already resolved). So base play is covered. Eight graph sites were listed here as still refusing 9000; a later audit corrected the list: five do (fixed as B-1164), `if_object_distance_to_pad_less_than` already resolved it, and the room checks were a different defect (B-1163, with B-1162). Same family: `if_natural_anim` refused an animation with no public source and answered "not equal" without comparing (Infiltration, `base:animation_idle`); fixed 2026-11-14 as B-1161. | T-MODDING-011 / V-010 | `tests/test_asset_native_source_contract.cpp` (the B-1119 pins in its pad action block). Next Agent campaign run: no `cannot jog_to_pad` warning in Defense and an `AI action jog_to_pad ... resolved_pad=N` line. Then the same resolution at the eight mod-only sites, and a public source (or a number-only comparison) for `if_natural_anim`. |
| B-1160 | High | source_connected | The client wrote outside the folders it was told to use. (1) Thirteen menu saves (the end screen's exits, pause menu, solo mission menu, the agent-select default, options, updater, theme and theme loader) called `configSave("pd.ini")`. A bare name resolves like a read (`fsFullPath`: catalog, mods, then the base folder), so the file landed in `<base>/pd.ini`, while `configInit` reads `$S/pd.ini` (`CONFIG_PATH`): those saves were never read back (the save to `CONFIG_PATH` at a clean exit hid it), and on a read-only install they failed, which the agent-select default reports as an error. (2) `fsInit` then copied that stray `pd.ini` (or an `eeprom.bin`) from the working folder into a fresh explicit `--savedir`, so one run started with another run's settings (seen once: CORE's B-1119 Agent run of 2026-11-14 logged `migrated ./pd.ini`). (3) A `--moddir` folder that did not exist yet was dropped with a warning and modmgr fell back to discovery. From an executable in `D:\PD2-wt\shared-base-*` the first candidate `$E/../mods` is `D:\PD2-wt\mods`, the MODS session's git checkout: it was scanned as a mods folder (4 mods found, none enabled, so no test result changed) and the one-shot migration left its marker file there; a checkout folder with a root `mod.json` would have been packaged and renamed. modmgr also created `./mods` in the working folder on every start. (4) Fonts, themes and the migration bench kept their own copies of the discovery list and ignored `--moddir` entirely (the B-1031 class), and the bench probed each root by creating a file. Found 2026-11-14 by CORE from stray files in `shared-base-3` after its own campaign runs. Pattern: SP-78 (one copy of a path rule fixed, its siblings not). Fixed 2026-11-14: every save names `CONFIG_PATH` and `configSave` refuses a bare name (a `$` placeholder, an absolute path or `./` is required); an explicit `--moddir` is kept before it exists and modmgr creates it; one list of mod roots (`fsModRoots` in `port/src/fs.c`) serves modmgr, fonts, themes and the bench; no save migration into an explicit `--savedir`; the bench's migration creates no probe file and is refused under the native write policy. Seen, not changed (left to their owners): `net.c` and `netmsg.c` send `fsGetModDir()` to peers (with `--moddir` or an enabled mod that is a local path, and the receiver ignores it); `forgeSerializeBuildModDir` puts Forge maps under the first enabled mod's own folder; the client log goes to `./logs` of the working folder, so a direct run started in a shared base writes its log there, and two such runs share one log. | T-RELEASE-002 | `tests/test_save_paths_static.cpp` and `tests/test_config_save.cpp` (`[b1160]`); updated pins in `tests/test_native_reuse_boot_static.cpp` and `tests/test_smoke_input_events_static.cpp`. A campaign run whose `--moddir` does not exist creates it and scans only it, and after an end screen's Main Menu choice the run folder's `pd.ini` is updated and the base gets none. |
| B-1161 | Low | source_connected | On the behavior-graph path `if_natural_anim` refused an animation slot the ROM leaves empty. The graph resolves both animations it compares to their public `.pdanim` source first (`s_aiGraphResolveAnimationCatalogId` in `port/src/scenario_source_runtime.c`) and refuses when there is none, answering "not equal" without comparing. 257 of the 1,207 character animation slots are empty in the ROM (no frames, no header), so the extractor writes nothing for them (`romextract pdanim_chr: written=950 skipped=257` on a fresh extraction), and ANIM_IDLE (0), where every chr's `naturalanim` starts, is one of them. Seen once in every Agent campaign run: `cannot if_natural_anim AI action for 'base:scenario_infiltration' (current_natural animation base:animation_idle has no public .pdanim source)`. In that case native also answers "not equal", so play did not change; a script testing for the idle animation itself would have. Fixed 2026-11-14: `if_natural_anim` resolves through `s_aiGraphResolveComparableAnimationCatalogId`, which takes an empty base slot as it is (source "(empty base slot)") and otherwise defers to the strict resolver; actions that play an animation keep the strict resolver. A slot a mod fills is not empty. | T-MODDING-011 / V-010 | `tests/test_asset_native_source_contract.cpp` (B-1161 block next to the B-1119 pins). Next Agent campaign run: no `cannot if_natural_anim` warning in Infiltration, and the `AI condition if_natural_anim` note shows `current_anim_source=(empty base slot)` where the chr is idle. |
| B-1162 | High | source_connected | On the behavior-graph path lights changed by background scripts never changed. Background AI lists (ids 0x1000 and up) run on a blank chr with no prop (`src/game/game_00b820.c`), and they make 569 of the 590 `set_lights_state` calls in base content (light timers, switches and blackouts). The graph twin `scenarioSourceAiGraphExecuteSetLights` (`port/src/scenario_source_runtime.c`) computed the room only when `chr && chr->prop`, so for those scripts the room was -1, nothing changed, and it still answered "handled", which skips the native `aiSetLights` (which needs no prop: `chrGetPadRoom` uses the chr only for the preset pad). Seen in every campaign run: the first `set_lights` of seven stages logs `chr_rows=0 source_chr=-1 room=-1`. Found 2026-11-14 by an audit of every graph pad check (CORE, read-only subagent), confirmed in code and in the Agent run log. Fixed 2026-11-14: the room is decoded for any chr (B-1163's helper); a missing chr is refused only when the value names the preset pad. | T-MODDING-011 / V-010 | `tests/test_asset_native_source_contract.cpp` (set_lights block). Campaign fixtures forbid a `set_lights` line with `room=-1`; the next Agent run shows the first light change of each stage with its room. |
| B-1163 | Medium | source_connected | Five graph room commands checked their room argument as a pad. `if_chr_in_room` (type 0), `if_target_in_room`, `if_room_is_on_screen`, `if_obj_in_room` and `set_lights` take `chrGetPadRoom`'s encoding: below 10000 a room number, from 10000 the room of pad (value - 10000), 19000 the room of the chr's preset pad. The graph passed the raw value to `s_aiGraphRequireRuntimePad`, so a room number passed only while it happened to be below the stage's pad count (always true in base content, which uses plain room numbers: 632 `if_chr_in_room`, 590 `set_lights_state`, 21 `if_object_in_room`, 1 `if_room_onscreen`), and every pad form was refused. A room number past the room table was not caught either, and native then reads or writes past it (`bgRoomIsOnscreen`, the light tables). Fixed 2026-11-14: one helper, `s_aiGraphResolveRoomOrPad`, decodes the value, checks the room (1 to roomcount - 1) or the pad behind it (resolving the preset), and returns the room exactly as `chrGetPadRoom` does. | T-MODDING-011 / V-010 | `tests/test_asset_native_source_contract.cpp` (the five room blocks). |
| B-1164 | Low | source_connected | Five graph sites refused pad 9000 (`PAD_PRESET`) where the native helper resolves it to the chr's preset pad: `try_start_alarm`, `set_pad_preset`, `chr_set_pad_preset` (the target's preset was never set), `set_chr_preset_to_chr_near_pad`, `spawn_chr_at_pad`, and `if_chr_in_room` type 2 (G5 Building eyespy check). Base content does not use 9000 there, so only mod content was affected; the B-1119 propagation note listed `if_object_distance_to_pad_less_than` among them by mistake (it has resolved first since 160c149a) and missed the room commands (B-1163). Fixed 2026-11-14: `s_aiGraphRequireResolvedRuntimePad` checks the pad the native call will use; a preset that resolves below zero is the native call's own case and needs only the pad table. The natives still get the raw pad. Seen, not changed: `s_aiGraphRuntimeFailure` returns 1, so several Require helpers that `return` it report success after a failure (the path pads non-loop branches, `s_aiGraphRequireRuntimePathPointer`, `s_aiGraphRequireRuntimeCharacterPointer`, `s_aiGraphRequireRuntimeSceneRooms`); and about a dozen refusals return 1 without advancing, relying on the B-949 guard for their false path. | T-MODDING-011 / V-010 | `tests/test_asset_native_source_contract.cpp` (the B-1164 order pins). |
| B-1165 | Low | source_connected | On the behavior-graph path a character made by `duplicate_chr` was not registered as source-spawned. `spawn_chr_at_pad` and `spawn_chr_at_chr` register what they spawn (`s_aiGraphRegisterSourceSpawnedChr`), the `duplicate_chr` twin did not, so every later AI command for the clone failed the "source-derived" character check. Seen in the 60 second soak of 2026-11-14 (receipt `campaign-soak-20261115`), Defense only, where the dataDyne shock troopers are clones: 8 warnings `cannot <action> ... (runtime character pointer is not source-derived)` for `if_alertness`, `if_can_see_target`, `if_check_fov_with_target`, `if_near_miss`, `if_num_arghs_greater_than`, `run_to_pad`, `set_return_list` and `set_target_to_eyespy_if_in_sight`. No play changed only because of SP-79 (the failed check still answers "holds"). Fixed 2026-11-14: the clone is registered the same way. Also seen in that run, expected and not changed: 2 `cannot chr_copy_properties ... missing runtime character id 11 / 15`, a command about a chr that is gone, which native skips too. | T-MODDING-011 / V-010 | `tests/test_asset_native_source_contract.cpp` (duplicate_chr block). Next soak: no `not source-derived` warning in Defense. |
| B-1166 | High | source_connected | On the behavior-graph path a character renumbered by `set_chr_num` lost its identity. In Pelagic II, background list 0x1012 gives one of three setup guards (0x3c, 0x3e, 0x3f) list 0x404, which renumbers him to chr 0x60 (96); list 0x401 then waits at label 8 until chr 96 is dead, dying or knocked out before its scripted sequence goes on (lines spoken, stage flags 0x00010000 or 0x00020000 set, a walk and an animation, and stage flag 0x00000002 at the end, which list 0x1012 reads). The graph accepts a chr only if its number is in the setup source or it was registered as spawned, so every reference to chr 96 was refused: alive it is "not source-derived", gone it is "missing runtime character id 96", and both answer the condition false, while native finds him by his new number, and treats a chr that is gone as dead. So on the graph path that wait never ends, in real play only (the forced-completion runner and the 60 second soak never reach it). Found 2026-11-15 by the SP-79 audit (read-only subagent), confirmed in `base_scenario_pelagic.pdscenario` (`ai/ailists.json`). Fixed the same day: when `set_chr_num` renumbers a chr the graph already trusts (a setup chr or a source-spawned one), the new number is recorded as source-assigned, and both character resolvers and the chrnum provenance check accept it. It blocks objective 6 (all difficulties), so Pelagic II could not be finished by real play on the graph path: list 0x401 is a technician whose first pass sets flag 0x00000200 and whose second pass sets 0x00000100, the objective needs both, and the second pass starts only once chr 96 is down. Propagation: five base stages renumber chrs with `set_chr_num` (Carrington training 15 numbers, Infiltration 4, Defense 3, Pelagic II 1, Rescue 1); the fix covers every one, and only the Pelagic II case was traced to an objective. | T-MODDING-011 / V-010 | `tests/test_asset_native_source_contract.cpp` (B-1166 block next to the B-1165 pins). Proof in real play: Pelagic II through the guard's death on the graph path (Mike's play-through, decision 3). |
| B-1167 | Medium | source_connected | Three behavior-graph twins could crash where the native command does not, all in cases base levels never reach (found by the SP-79 audit). `if_hoverbot_next_step` read `hovercar->path->len` for a hoverbot with no path, which native never reads; `chr_begin_or_end_teleport` and `if_chr_teleport_full_white` asked for player slot -1 when the target chr was not a player and then wrote through a NULL player, where native acts on the current player; `set_chr_special_death_animation` wrote `chr->specialdie` with no chr, where native checks `if (chr)`. Fixed 2026-11-15: the hoverbot twin checks the path only when there is one and compares `nextstep` as native does; both teleport twins start from the current player; the death twin checks chr. Same change, SP-79: the 90 failure returns in 40 requirement helpers now log and return 0 (-1 for the three-way target-movement node), the ten callers of the three -1 node helpers read -1 as a refusal, and the two branches that keep going on purpose say why. | T-MODDING-011 / V-010 | `tests/test_graph_helper_failures_static.cpp` (`[sp79]`, `[b1167]`). Soak on the new client (17 missions x 60 s) to show no new refusals in base play. |
| B-1168 | Critical | source_connected | `devtools/release.ps1` ships the main checkout's `Build/data` wholesale: the dist step leaves out only `*.z64`, `base/`, `mod source files/` and `README.txt`, and `Test-ReleaseTypedArchiveTree` checks archive format, not where the content came from. A local launch from `Build/` extracts the ROM into `Build/data/ntsc-final`, so a release cut from that folder carries ROM-derived content. Checked read-only 2026-11-15: `Build/data` holds 8,050 `base_*` archives whose `_meta/provenance.json` names a `romextract_*` tool, 4,022 raw extracted ROM files (`files/`) and 52 ROM segments (`segs/`). The public stable release v0.1.102 (2026-06-18) has a 233.6 MB zip against 31.98 MB for v0.1.16, with the two executables about 84.6 MB uncompressed, which fits an extracted tree inside it; the zip's contents are NOT yet confirmed (needs Mike's OK to download it). Breaks the BYOR hard constraint. | T-RELEASE-002 | Script fixed 2026-11-15 on `week-pd/core`: the dist step leaves out raw ROM files and segments (`files/`, `segs/`), extraction cache stamps, and every archive whose provenance tool is `romextract_*` (a `base_` archive with no provenance too), then `Assert-ReleaseHasNoRomDerivedContent` refuses the release if anything ROM-derived or a `.z64` reached the dist. Run over today's `Build/data` (read-only): 12,140 files left out, 22 would ship (15 generated `ui_*.pdui`, 7 built-in palette themes, tool `pdgui_theme_loader`). `devtools/test-release-byor.ps1`: 12 checks, PS 5.1 and 7. Published asset removed 2026-11-15 on Mike's instruction ("go ahead and just remove it", no download to confirm): `PerfectDark-v0.1.102-win64.zip` and its `.sha256` and `.sig` are deleted from the v0.1.102 release; its page, tag, `PerfectDark.exe` and `Updater.exe` remain. Every other release zip, v0.0.34 to v0.1.16, is 27 to 35 MB (client and updater), so only v0.1.102 shipped the extracted tree. |
| B-1169 | High | source_connected | With no `--moddir`, a player's client takes the first existing folder of `$E/../mods`, `./mods`, `$E/mods` and the base `mods` (`fsModRoots`, `port/src/fs.c`) as its mods folder, runs the one-shot folder migration there (`modMigrateRun`, `port/src/modmigrate.c`: every subfolder with a root `mod.json` is packaged into a `.pdmod` and renamed, and a `.pdmod-migration-done` marker is written) and mounts what it finds. `$E/../mods` is a developer convenience (the executable in `Build/`, mods at the repository root) and is checked first, so a player who unzips the game into `C:\Games\PD2\` gets `C:\Games\mods`, which may belong to another game whose mods also use `mod.json`; those folders would be renamed. `./mods` follows the working folder, which a shortcut can set anywhere. The release text tells players that `mods/` lives next to the executable. Found 2026-11-15 by the decision 3 setup check (code read; no player run). B-1160 fixed only a missing explicit `--moddir`. | T-MODINFRASTRUCTURE-002 | Mike, 2026-11-15: option A, fitted alongside the LocalAppData user mods folder MODS shipped on `week-pd/mods` (91198952; there the migration already runs only in that user folder, but the old install candidates were still scanned and mounted). Fixed: one rule, `modsInstallRoots` (`port/src/mods_install_roots.c`, one-commit branch `week-pd/installmods`, 546263fe on a8ab67f3): the repository-root mods folder when the executable's parent holds `CMakeLists.txt` and `.git`, then `<base>/mods`; nothing beside the install, nothing under the working folder. On `week-pd/core` `fsModRoots` uses it, the mod manager creates the install's own folder instead of `./mods`, and install-from-file and map import fall back to it instead of `./mods`; `[b1169]` plus the B-1160 scan (no file may name `$E/../mods` or `./mods`). Left: MODS wires `modsInstallRoots` into its candidate list after the user folder. |
| B-1170 | Medium | source_connected | The `menu_settings_keyboard` smoke (settings and keyboard bindings) could not run on any branch: since 6044d0fa it pages with `PageUp` and `PageDown`, which the smoke harness key table (`s_KeyTable`, `port/src/smoke_harness.c`) did not have, so the harness refused the whole fixture at parse time (`SMOKE: key event has unresolved key (key='PageDown', scancode=0)`, a 3.1 s run with no scripted exit). Found 2026-11-15 by the decision 3 setup pack; a scan of every fixture's key names found no other unresolved key. Fixed: both keys added; `[b1170]` in `tests/test_smoke_input_events_static.cpp` fails when any fixture names a key the table lacks. | T-TOOLING-006 | Rerun on the fixed client (4f6ffe57, 2026-11-15 06:00 UTC): the fixture now parses and boots to the agent screen, but the window never got focus (`focus_lost=1`, `menu_key_ready:0`, also with `-MaintainForeground`), most likely because the desktop was locked overnight. Rerun with `-MaintainForeground` on 2026-11-15 17:05 UTC with the desktop unlocked: the window has focus and the menu comes up, so the focus part is resolved; the run now stops at the Tab navigation (B-1174). |
| B-1174 | Medium | fixture updated, rerun pending | The Nov 15 Base 4 `-MaintainForeground` run acquired the game window and used Return to load an Agent. The main menu stayed focused on Play after four Tab taps and the fixture timed out at 8 of 28 events (22/49 assertions). The raw client log records each Tab toggling the Social shell, consistent with the Menu/Pause `ACTION_SOCIAL_TOGGLE` mapping. | T-MENUS-003 | Nov 18 switched the fixture to three Down taps through the primary menu and retained the real Settings-focus and tab assertions. Source checks pass. A new ordinary-client run awaits canonical storage admission. Prior logs remain in `D:\PD2-wt\core\.claude\week-pd-receipts\base4-campaign-20261115\menu_settings_keyboard-*`. |
| B-1175 | High | source_connected | On every stage built from scenario source the player started without the mission's intro weapons, ammo and outfit. The setup intro script also gives the starting loadout (`INTROCMD_WEAPON`, `INTROCMD_AMMO`), the outfit (`INTROCMD_OUTFIT`) and the watch time, but the extractor wrote only its spawn pads (`spawns.json`, `s_buildIntroSpawnsTable` in `port/src/romextract_pdarena.c`) and `s_setupBuildStageBlock` (`port/src/scenario_source_runtime.c`) rebuilt the intro from those alone, so `playerReset` never saw a weapon command: the Villa log of the base 4 run has `playerReset intro` with no `GAMELOOP.WEAPON ... INTRO gave` line. Read-only census of all 121 ROM setup files: the setups hold 59 weapon, 59 ammo and 79 outfit commands, nearly all in the solo setups (Crash Site in the snow outfit, Pelagic II in the wetsuit, Chicago in the trench coat, Air Force One as the stewardess, Maian SOS as Elvis), none of them reached the game. The campaign runs passed because they finish objectives through the debug path, not by play. Fixed on `week-pd/core`: a new scenario member `intro.json` (`pd2.scenario.intro.v1`, `port/include/scenario_intro.h`, strict reader `port/src/scenario_intro.cpp`) carries every intro command but the spawns, in order, with catalog weapon ids and outfit names; the runtime rebuilds the intro as spawns, then those rows, then the end. An unreadable `intro.json` stops the stage like an unreadable `spawns.json`; an absent one means no such commands (a custom level may have none). Credit offsets point into setup data the scenario does not carry, so the extractor refuses a setup with one (no base setup has one). Scenario cache kind gains `_intro_mpsetup`. | T-MODDING-011 | In game: the release campaign logs an `INTRO gave` line for each mission with an intro weapon, and a play screenshot of Crash Site shows the snow outfit. Propagation: every other `g_StageSetup.intro` reader (`player.c` inventory reset, `mplayer/scenarios.c`, `net/netmanifest.c`) reads the same rebuilt script. |
| B-1176 | High | source_connected | Multiplayer arenas were extracted from the stage's solo setup file. Every scenario was built from `st->setupfileid`, but a match reads `mpsetupfileid` (`setupLoadFiles`, `src/game/setup.c`), and a multiplayer arena's solo setup is all but empty, so in public source 16 of 18 arenas had no setup objects (no weapons, ammo or shields; Ravine and Sewers had only a lift), and none had their spawn pads, briefcases, case respawns or hills. This is the root of MULTIPLAYER's 01:05 and 03:20 asks and of B-1125 (Hacker Central divided by zero with no crates) and B-1126 (cases off-world, a fixed hill); MULTIPLAYER worked around B-1126 with generated placements. The census: the multiplayer setups hold 241 spawns, 64 cases, 384 case respawns and 75 hills. Fixed on `week-pd/core`: a scenario of kind `mp` (the Dark, Classic and Bonus arenas) is built from the multiplayer setup, so its `objects.json`, `setup.fields.json`, `spawns.json`, AI lists, paths and the new `intro.json` (B-1175) come from the file a match reads; the manifest records `setup_source`. The Solo Missions arenas keep the solo setup, as today. | T-MODDING-011 | In game: an arena match logs its setup objects and intro rows (cases and hills), and MULTIPLAYER's generated placements add nothing on a base arena. Open question for MULTIPLAYER: in a match on a Solo Missions arena the original reads that stage's multiplayer setup (empty), while scenario source loads the solo setup with its guards. |
| B-1177 | High | source_connected | Every multiplayer arena built from its multiplayer setup (B-1176) failed to load. **Seen in game 2026-11-15 22:25 UTC** (`mp_arena_setup_source` on the base 5 candidate, frozen CORE client 0056eae6): `SCENARIO.SOURCE: invalid public setup.fields.json for 'base:scenario_mp_complex': unsupported setup source field 'multi_ammo_crate.slots[0].model' on 'setup_0001'`, then the asset-chain FATAL, so Complex never started. Root cause: the slots of a multiplayer ammo crate are empty in the setup file (model 0, quantity 0; the match fills them when it starts), the extractor writes model 0 as an empty model catalog id, and the setup reader resolves every slot model through `s_setupResolveModelnum`, which refuses an empty id. All 9,956 crate slots in base 5 are empty (24 scenarios: every multiplayer arena and the extra scenarios). The offline checks missed it: the conformance checker does not compile setup fields against the catalog, and the in-game check was the first to load a multiplayer arena's setup. | T-MODDING-011 / scenario source | Fixed on `week-pd/core` (pd-tests 1,932 cases; in-game rerun queued): an empty crate slot model is model 0, as the setup file has it; a named model that does not resolve is still refused. Base 5's data stays valid, so no re-extraction is needed. Propagation: only this field carries an empty catalog id in base 5 (scan of every scenario's `setup.fields.json`). |
| B-1178 | Low | fixed on `week-pd/core` | `entity.autogun`'s `beam_interval` counts shots (the deployed Laptop Gun makes a tracer beam when `firecount % beaminterval == 0`, `propobj.c`, the original's literal 4), but the graph compiler's unit rule (`keyNeedsUnit`: a key containing `interval`, `timer`, `duration`, `cooldown` or `_time` needs a unit suffix) refused that documented name, so the only spelling that compiled was `beam_interval_ticks60`, a name that says 60 Hz ticks, which the runtime read as a shot count: an author who wrote 60 for one second got a beam every 60 shots. No data used it. Found by CORE 2026-11-16 in the propagation check for MODS's B-1230 (the vehicle key `boost_time_ticks60` counts 240 Hz ticks); every other `_ticks60` weapon graph field reaches a 60 Hz field or `weaponGraphTicks60To240`. | T-MODDING-011 / weapon graph schema | Fixed on `week-pd/core`: `beam_interval` is exempt from the unit rule (as `timer_starts` is), the field is `autogun_beam_every_shots`, the parser reads only `beam_interval`, and the compiler and the IR validator refuse `beam_interval_ticks60` with a message naming the shot count (refused, not ignored). Every other parsed key the unit rule catches is a time or already exempt; `weapon-graph-module-parameters.md` states the unit. pd-tests case `[b1178]`. Pattern SP-88. |
| B-1179 | Low | fixed on `week-pd/core` | A chr's surface-normal blend (surface locomotion, `src/game/surface_loco.c`) advanced once per player per frame. `chrTick` runs once per player each frame and only the first call is the frame's full tick, but the blend countdown and `chrSurfaceLocoTick` (the floor and wall raycasts) sat after the fulltick blocks, so with two players the 8-frame blend took 4 frames and the floor was sampled twice. Cosmetic; one player is unchanged. Reported by MULTIPLAYER 2026-11-16 05:25 UTC (the class of their B-1202, SP-87). | T-MODDING-011 / chr tick | Fixed on `week-pd/core`: both run inside `if (fulltick)`. Not changed: the blend counts rendered frames, not 240 Hz time, so it is shorter at higher frame rates. pd-tests case `[b1179]`. |
| B-1250 | High | fixed on `week-pd/core` | Every launch of a `week-pd/core` client re-extracted all 87 scenarios and 47 arenas (about 3 minutes on D:, and every smoke install at boot). The pdscenario cache kind reached 525 chars with the object rules (3accc882), and the fast-cache stamp readers read at most 511 (`PDEXTRACT_CACHE_KIND_MAX` 512 in `romextract_pd_cache.c`, whose own comment warned of exactly this), so every kind compare failed and the in-place guard forced a rewrite on every run. Seen in CORE's turn q_20261116060755455_2b33c611 (a second extraction pass wrote 87 scenarios again). | T-MODDING-011 / extraction cache | Fixed on `week-pd/core`: the readers hold 2047 chars and the stamp writer refuses, loudly, a kind they could not read back (`EXTRACT.CACHE`). The stamp format is unchanged, so no existing stamp is invalidated. pd-tests case `[b1250]`; SP-86 instance. |
| B-1251 | High | fixed on `week-pd/core` | Extraction could not load on a scenario-source stage: its `objects.json` was refused (`object rule engine_hum names no sound effect ('base:sfx_ship_hum')`) and the stage hit the legacy-setup FATAL, so the release campaign stopped at its third mission (25/54). The hum resolved in the catalog, but `s_setupResolveHumSound` bounded sound numbers at 0x7fff, and the game's configured sounds are packed 16-bit values with the top bit set (`SFX_SHIP_HUM` 0x810c). Introduced by 3accc882 the same day. | T-MODDING-011 / object rules | Fixed on `week-pd/core`: the bound is 0xffff and the value is kept as the packed s16 the sound code takes, as `effect_gameplay_runtime.c` does (a voice entry is accepted only when it carries a configuration). pd-tests case `[b1251]`. |
| B-1252 | Low | fixed on `week-pd/core` | A client's `version:` log line, the title screen's version text and the About panel's commit name the commit its build directory was last configured at, not the commit built. `CMakeLists.txt` runs `git rev-parse --short HEAD` and `--abbrev-ref HEAD` with `execute_process` at configure time and writes them into `versioninfo.h`; an incremental build after a commit does not configure again, so the stamp lags behind. Seen 2026-11-16: CORE's frozen client built at 14f8d527 (it carries B-1250's message) logged `version: week-pd/core 576d86ec8`. A receipt that takes the commit from that line records the wrong one; the smoke runner's binary hashes are unaffected. | build / provenance | Fixed on `week-pd/core`: the `pd_versioninfo` target (`CMakeLists.txt`) runs `cmake/versioninfo.cmake` on every build, which reads the commit and branch from git and writes `versioninfo.h` through `configure_file`, leaving it untouched when nothing changed, so only the files that include it rebuild after a commit; every compiling target depends on it. pd-tests case `[b1252]`. |
| B-1253 | Medium | fixed on `week-pd/core` | In an arena match on a campaign stage built from scenario source, the SP-in-MP transport overlay (`src/game/setup.c`, B-228 Option E) created the stage's lifts a second time. An arena on a campaign stage (the Solo Missions arenas) loads that stage's own scenario (`kind = solo`; no arena-only scenario exists for it), so the main pass already creates its lifts (their `flags2` carry no difficulty or player-count exclusion), and the overlay then loaded a second copy of the same setup and created every lift and escalator step in it again, wired to the second copy's door structs and registered in `g_Lifts` over the first. Affected: Infiltration (4 lifts became 8) and CI Training (2 became 4, where multiplayer runs on it); Chicago, Villa, G5 Building and Pelagic have no lifts. The overlay was written for ROM-era arena setups that had no lifts; scenario source changed what the main pass loads. Found by reading the code while converting B-228's list to an object rule (CORE, 2026-11-16). | T-MODDING-011 / B-228 | Fixed on `week-pd/core`: the overlay runs on a ROM stage only; on scenario source the main pass applies the row rule `"multiplayer": true` (object rules), and the new smoke `combat_sim_infiltration_lifts` requires the 4 lifts created once and no overlay line. SP-80 instance. MULTIPLAYER owns B-228. |
| B-1254 | Low | fixed on `week-pd/core` | `if_bot_respawning` (0x0176, `aiIfBotRespawning`, `src/game/mplayer/mpaicommands.c`) read `chr->aibot` through a null pointer when the running chr had no aibot: its guard skipped the command but had no return (the original's missing return, noted in the decomp), so the next line read the null pointer. The base game runs the command only in the simulant dead list (global list 0x0028) on simulants, so only a mod or level that runs it on another chr (a guard, a background chr) would crash. Found while writing the command's graph twin (CORE, 2026-11-16). | T-MODDING-011 | Fixed on `week-pd/core`: the native guard returns; the graph twin treats no chr or a chr without an aibot as false (`tests/test_ai_simulant_twins_static.cpp`). |
| B-1255 | Low | fixed on `week-pd/core` | `mp_init_simulants` (0x0185, `aiMpInitSimulants`, `src/game/mplayer/mpaicommands.c`) never retried an incomplete simulant wave from the AI list. B-174's spawn wave made the handler return false without moving ("leave the opcode in place": the retained wave cursor retries on the next AI pass), but the B-949 guard in `chraiExecute` force-advances any command that returns false without moving, so the command was skipped: the stage's background list 0x1000 ran `rebuild_teams` and `rebuild_squadrons` before every simulant had spawned, and only the host's botTick failsafe retried (it never runs on a non-authority client). Seen only when an initial spawn is refused. Found while writing the command's graph twin; `tests/test_combat_sim_defect_cohort.cpp` still pinned the intended shape (CORE, 2026-11-16). | T-MODDING-011 / B-174 / B-949 | Fixed on `week-pd/core`: the handler and the graph twin end the tick with the cursor on the command (`return true`) while the wave is incomplete; the shadow walk accepts that stop. Propagation: no other handler relies on returning false without moving to retry (the B-949 cases are refusals meant to be skipped). |
| B-1256 | Low | fixed on `week-pd/core` | The AI edge parity probe of the campaign runner (`port/src/autocampaign.c`, AI lists as graphs slice 2) zeroed its counters only when a mission was queued through `menuhandlerAcceptMission`, which a campaign does for its first mission only; the later missions are queued from the end screen (`pdguiEndscreenNextMission`), so every later line carried the campaign's running totals (the 2026-11-16 soak: `compared` 1.13 million on Defection up to 19.7 million on Skedar Ruins). Detection was not lost (the counts only grow, so the first mission with a difference fails the run), but the per-mission numbers were wrong. Found reading the soak's lines (CORE, 2026-11-16). | T-MODDING-011 | Fixed on `week-pd/core`: the end screen's route resets the counters when it queues the next mission; `tests/test_ai_edge_shadow.cpp` pins both reset points. |
| B-1257 | Medium | fixed on `week-pd/core` | Typed AI args (ai-typed-operands.md slice 3) wrote every asset argument the extraction-time catalog could not name as a number: animations and sounds (never named), 15 solo weapons (unarmed, the horizon scanner, the suitcase, devices), a spawn's "no head" (-1) and a cutscene's "no weapon" (255); and the dual-use `entity` fields carried an asset key's name. The archive scan refuses a number under an asset key, so conformance failed with 23,762 errors (CORE's turn q_20261116162649494_8c375cad, 2026-11-16), all in AI args; play was unaffected (release 61/61, specials 49/49). Found by that turn's conformance run (CORE). | T-MODDING-011 | Fixed on `week-pd/core` (slice 3b): every asset argument is a catalog id or null; animations and sounds are catalog kinds (a sound's MP3 priority and response are arguments of their own); weapons are named from the base weapon table; `entity` is `target` or `source`; the extractor, the compiler and conformance refuse an asset by number; one naming rule for extractor and compiler (`port/src/ai_catalog_refs.c`). |
| B-1258 | Low | fixed on `week-pd/core` | The base catalog registered the 380 character animation slots whose symbols are placeholders (ANIM_001B, ...) as `base:animation_unlabeled_<ordinal>` (`assetcatalog_base_extended.c`), while the character animation extractor writes their archives as `base:animation_character_<ordinal>` (`romextract_pdanim_chr.c`), so each such slot had two catalog ids, one with no archive. Found by the slot-id check slice 3b added to conformance (CORE, 2026-11-16). | T-MODDING-011 | Fixed on `week-pd/core`: the base registration uses the extractor's rule ("character"); conformance checks the rule against every authored animation archive on each run (0 of 950 differ). |
| B-1262 | High | fixed on `week-pd/core` | CORE's dev merge into `week-pd/core` (9f38d6dd) dropped the two lines that start `modmgrScanDirectory` (`port/src/modmgr.c`) from an empty mod registry and root list (`g_ModRegistryCount = 0; g_ModsRootCount = 0;`): both parents had them, and the conflict resolution kept the new fsModRoots body without them. Every rescan (the mod author's save, an Apply) then appended each mod again, so enabling one failed with `MODAUTHOR.ENABLE.FAIL ... Duplicate installed package identity` and the second root could be skipped as already known. Found by MODS's `mods_user_folder_smoke` (20/23) in CORE's merge proof turn q_20261117033645172_617e7a0e (2026-11-17); not on dev. | T-MODDING-011 / merge | Fixed on `week-pd/core`: the two resets are back at the top of the scan. Propagation: a scan of every file the merge changed for lines both parents had and the result lost found only these two and one comment that was reworded on purpose. |
| B-1259 | Medium | fixed on `week-pd/core` | `setupGetCmdIndexByProp` (`src/game/setuputils.c`) compared each setup command's word `cmd[5]` with the prop pointer: the N64 layout of `struct defaultobj`, whose 32-bit `prop` field sits at byte 0x14. On the 64-bit port the field is an 8-byte pointer at 0x18 and `cmd[5]` is the padding before it, so the lookup never matched and always returned -1. Two callers: the object events' payload (slice 2; every `object=` was empty in CORE's turn q_20261117000144009_bab5471d, 2026-11-17), and `objTick`'s regeneration of an object inside another object (multiplayer only), which looked up its parent at index `pad - 1` instead of its own index plus `pad`: it reparented to the wrong object or, for a negative offset, found none and never came back. Found reading the turn's payloads (CORE). | T-MODDING-011 / SP-94 | Fixed on `week-pd/core`: the lookup reads the object's own `prop` field, on object commands only (`setupCmdObj`, shared with `setupGetObjByCmdIndex`); `tests/test_level_graph_program.cpp` pins it. Propagation: no other code reads a pointer through a fixed `u32` word of a setup command (SP-94's search). |
| B-1261 | Medium | fixed on `week-pd/core-s6` | Every base projectile that a graph record reaches took wrong projectile flags at spawn. `projectileApplyGraphRuntime` (`src/game/propobj.c`) ORed the record's `flags` into `projectile->flags`, but the extractor writes the weapon function's flags there (`s_writeProjectileArchive`, `romextract_pdweapon.c`: `projectile.spawn_state` `flags` = `weaponfunc.flags`), and every other reader of a record's `flags` (`bondgun.c`, `bondmove.c`, `chraction.c`) takes them as FUNCFLAG_* bits. The two sets overlap, so on shared base 7 (28 projectile records, 12 flag values): the Slayer's fly-by-wire rocket got PROJECTILEFLAG_SLIDING (from FUNCFLAG_FLYBYWIRE 0x800), which sends a projectile down the floor-sliding branch of `projectileTick` and skips its retick; the N-bomb got PROJECTILEFLAG_FALLING (FUNCFLAG_DISARM 0x400), which its landing test treats as landed; the homing rocket got PROJECTILEFLAG_LIGHTWEIGHT (FUNCFLAG_HOMINGROCKET), half gravity once it is unpowered; the Laptop gun and the Devastator's wall hugger got PROJECTILEFLAG_00000100 (FUNCFLAG_STICKTOWALL), no random spin on a bounce; and most thrown and fired projectiles got PROJECTILEFLAG_00002000 (FUNCFLAG_NOMUZZLEFLASH), harmless alone. The spawn paths have no base-weapon guard (`bgunGetProjectileGraph`), so base weapons went through it since the OR came in (2026-05-24). Not yet seen in play; found from source and the base archives while surveying the Projectiles row (CORE, 2026-11-17). | T-MODDING-011 / SP-96 | Fixed on `week-pd/core-s6`: the OR is gone; the spawn sites already derive projectile flags from the function's flags as the original does, and the graph's own latches (homing, bounce slide, lightweight) are unchanged. No dev mod authors a non-zero `flags`. Pin in `tests/test_custom_projectile_log_static.cpp` (`[b1261]`). |
| B-1265 | High | fixed on `week-pd/core-s6` (read from the code and an extracted archive; not yet seen in a game run) | A thrown base grenade or mine loses its fuse at the throw. `bgunCreateThrownProjectile2` (`src/game/bondgun.c`) sets the thrown object's fuse from the function's activation time (`weaponobj->timer240 = activatetime60`, times four) and its first-bounce boost (`unk08c = 0.1f`), and then applies the function's launch record (`projectileApplyGraphRuntime`, `src/game/propobj.c`). The record of every base throw carries two placeholders, because the extractor (`port/src/romextract_pdweapon.c`) writes the same motion node for a throw as for a fired projectile and a throw has neither value: `"timer60": -1` and `"reflect_angle": 0` (seen in shared base 9: `base:grenade__projectile_timed_grenade` has `timer60` -1, `activation_time60` 240, `reflect_angle` 0, and a game log shows it registered and activated at boot). The runtime took any `timer60` that was present as authored, so the fuse became -1 ("no timer"), and any `reflect_angle` that was present as authored, so the boost became 0. By the code this means: a hand grenade a player throws goes off on its first tick (the prime-time adjustment that follows turns a fuse below the prime time into 0); a simulant's thrown grenade, a timed mine, a proximity mine, the proximity grenade, a thrown Dragon and both N-bomb functions never arm or go off on their own (their arms in `weaponTick` need a timer of 0 or more), though a shot or a blast still sets them off and a remote mine still answers its detonator. The weapon graph runtime has been on for base weapons in the product since 2026-06-17 and no report of this exists, so it needs the game's confirmation: `projectile_events_smoke` (this branch) throws each of these and requires the grenade's, the timed mine's and the rocket's fuse lines. Found while checking a doubt raised by the sentry survey for slice 5 (CORE, 2026-11-19). | T-MODDING-011 | Fixed on `week-pd/core-s6`: `projectileApplyGraphRuntime` applies a record's `timer60` only when it is 0 or more and its `reflect_angle` only when it is above 0; a placeholder leaves the launch site's own value. A fired projectile is unchanged, because its launch site sets the same two numbers from the same record before the record is applied (a held rocket keeps the original's 1 where the record's -1 used to replace it). The extractor still writes the placeholders, so no archive changes. Pins in `tests/test_custom_projectile_log_static.cpp`. Not run in the game yet. |
| B-1263 | Medium | fixed on `week-pd/core-s6` | Projectile slices 1 and 2 looked a flying object's archive up by the object's own weapon number and function (`weapon->gset`, in `port/src/projectile_events.c`), but a fired projectile does not carry the gun's: `bgunCreateFiredProjectile` (`src/game/bondgun.c`) makes a rocket as WEAPON_ROCKET or WEAPON_HOMINGROCKET, a crossbow bolt as WEAPON_BOLT and a Devastator or Super Dragon round as WEAPON_GRENADEROUND (the Super Dragon's with function 2). So a rocket found no archive at all (no weapon archive has a projectile for WEAPON_ROCKET): its fuse rule fell back to the native rule and never reached `base:rocketlauncher__projectile_rocket` or the Slayer's programs, and its events were logged as `weapon=base:rocket`; a crossbow bolt used the bolt item's archive (`base:bolt__projectile_*`), not the crossbow's, and a Devastator round the grenade round item's; and the thrown Laptop gun, an autogun object, was not recorded at all. A mod that rewired the rocket launcher's or the crossbow's program would have changed nothing. Found by reading `bondgun.c` while planning slice 3, before the branch's first game run (CORE, 2026-11-19); `projectile_events_smoke` would have failed on these lines. | T-MODDING-011 | Fixed on `week-pd/core-s6`: `projectileApplyGraphRuntime` (`src/game/propobj.c`), which every launch that applies a record already calls, binds the object's prop and prop generation to that record's archive (`projectileEventBind`, a ring of 64); the events and the fuse rule identify a projectile by its binding (the gun and function whose held function names the archive) and fall back to the object's own weapon and function, which is right for everything thrown. Launches that apply no record (a simulant's rocket in `botact.c`, the chopper's, an AI's thrown grenade through `objDrop`) are unbound and keep the native rules, as before. Pins in `tests/test_custom_projectile_log_static.cpp`. Not run in the game yet. |
| B-1264 | Medium | fixed on `week-pd/core-s7` | Projectile slice 3 gave a gun lying on the floor the rules of what it fires. An object no launch bound is identified by its own weapon number and function (B-1263's fallback, "right for everything thrown"), and for a gun that fires a different object that names the wrong archive: a rocket launcher, a Slayer, a Devastator, a Super Dragon or a crossbow that a guard or a player drops, or that lies in a level as a pickup, is a weapon object with the gun's number, and its function's projectile archive describes the rocket, the grenade round or the bolt. Slice 3's rule events fire for any weapon object in flight, a dropped gun included, so the bounce and rest rules would have spent a dropped launcher's or Devastator's fuse (harmless: a gun has no fuse) and the landing rule would have landed a dropped crossbow as a bolt (pushed in along its flight); every such choice would also have counted as a parity difference, so the campaign fixtures' `rules.differ=0` could fail on any mission where a guard drops one. Slices 1 and 2 only mislabelled such an object's records (`projectile=` named the ammunition's archive). Found by reading while writing slice 4's damage rule, which fires for pickups too, before the branch's first game run (CORE, 2026-11-19). | T-MODDING-011 | Fixed on `week-pd/core-s7`: `projectileIdentify` (`port/src/projectile_events.c`) attaches an archive to an unbound weapon object only when `projectileFunctionLaunchesItself` says its function launches that very object (`projectileRulesLaunchedObject` gives its own weapon number and function back: every thrown item, and a rocket, bolt or grenade round under its own number); otherwise the object has no projectile archive, no entity archive and no program, and the original rules by weapon number answer, which give a gun none. A mod weapon's objects carry the weapon's number whatever they are, so those are left to the launch's binding, as before. Pins in `tests/test_custom_projectile_log_static.cpp`. Not run in the game yet. |
| B-1266 | High | fixed on `week-pd/core-s11`; RUN 2026-11-20 (turn q_20261120032344891_82b8da70, client built from 7bdfbdad, re-extracted base: `scene_rooms_defection` 19 of 21 on dev's client and 21 of 21 with the fix; the captures show Defection's intro, black before, and the rooftop's floor, skyline and sign frame; 132 scenes equal their background files room by room; 12 other smokes and the three campaigns green) | **The scene of every base stage is missing its largest rooms, in the published v0.1.105 too.** Mike's reports on v0.1.105 (Workbench N-1072, N-1073, N-1074): no picture during Defection's intro, Defection floors and walls missing or flipped-looking, a Carrington Institute computer face. `s_buildBgVisualExports` (`port/src/romextract_pdarena.c`) sized each room's work buffer from the room's COMPRESSED length (`room_span * 8 + 4096`, at least 65536) and then skipped the room, with a bare `continue` and no log line, when its inflated length exceeded an eighth of that buffer. So every room whose display data inflates past 8192 bytes (or past its stored size plus 512) was left out of `scene.glb`; the guard dates from the first version of the export (cd57ca2b, 2026-05-21). Measured against the ROM for all 60 background files (2,909 rooms): Defection and Extraction lose 14 of 167 rooms with 10,479 of 19,780 triangles (53.0 percent: rooms 2, 57, 128 to 133 and 162 to 167, the last six the largest of the stage, its exterior); Attack Ship 64.4 percent (29 rooms), Air Force One 53.5 (23), Crash Site 46.5 (9), Pelagic II 44.8 (24), Investigation 42.1 (14), Deep Sea 36.0 (20), Air Base 32.8 (8), Area 51 25.8 (22), Villa 24.1 (9), Skedar Ruins 23.7 (6), Chicago 18.8 (4), Carrington Institute and Defense 16.5 (5), G5 10.3 (3); of the Combat Simulator arenas Felicity loses 2 rooms, Area 52, Base and Complex 1 each. The rooms that were exported match the ROM triangle for triangle. Collision, room boxes, portals and navigation come from other tables and are complete, so play went on over floors that were not drawn; no smoke looks at pixels, and the scene renderer has drawn the campaign since the summer. Fix: the buffer is sized from the room's inflated size, which the rzip header states (checked equal to the real inflated length for all 2,909 rooms); a room that has display data and cannot be exported is logged, counted and fails the export (`ROMEXTRACT.SCENE: ... left_out=N`), the only quiet skips left being the room with no data and the room that is a header and nothing else (four in the game: Crash Site rooms 4 and 5, Air Force One room 1, Skedar Ruins room 137; the first extraction with the fix refused four stage archives over them, which is how they were found); the scene export version and the arena cache stamp change so an install re-extracts its scenes once (the scenario kind, which is also the level format authored levels declare, does not change). | V-010 / rendering (N-1122) | `tools/verify_scene_rooms_against_rom.py` passes on a re-extracted install (every scene equals a background file room by room; on base 12 it fails 56 of 132 scenes with exactly the rooms above); the extraction log shows `left_out=0` for every stage; captures of Defection's intro and rooms and of the Carrington Institute before and after; the campaign smokes stay green. |
| B-1267 | Medium | fixed on `week-pd/core-s11`; RUN 2026-11-20 (the same turn: every stage activation logs its single-sided share, Defection 18,292 of 19,780 triangles, `front=counter_clockwise`; GL against bgfx on the menu backdrop differs in 464 pixels of 3,686,400 with a largest difference of 2, was 2,290 and 7; no missing wall in the Defection, Villa, Complex and Felicity captures) | The scene kept no cull mode and the scene renderer drew every room surface from both sides (`glDisable(GL_CULL_FACE)`). The room display lists set and clear `G_CULL_BACK` (every list does so before its first triangle; no list culls front faces or both): on Defection the game draws 92.5 percent of the triangles with back faces culled and 7.5 percent from both sides; over the game between 0 and 20 percent of a stage is two-sided. With everything two-sided the back of a room's shell shows wherever the original showed nothing, which matters more once the exteriors B-1266 left out are drawn. Fix: the extractor follows the geometry mode through each display list and a material is the texture selection, the block list and the cull mode; a two-sided material has glTF's `doubleSided` and every base material names `pd2_material.cull` (`back` or `none`). The renderer culls back faces only for `cull: back`, so a scene written before the field and an authored level draw as before. The game's front faces read counter-clockwise from the eye (fast3d's own test, and 70 to 88 percent of single-sided triangles face their room's centre); the renderer takes the front face from the sign of the view-projection's determinant, so the y flip of a flipped framebuffer is followed (`scenarioSceneMatrixKeepsWinding`, unit-tested against the game's own lookat and perspective). The bgfx path does the same on its OpenGL backend. | V-010 / rendering | Captures inside and outside rooms on a re-extracted install show no missing inner wall and no outer shell from behind; `SCENARIO.RENDER: culling` logs the single-sided share per stage; one capture with `--renderer bgfx`. |
| B-1268 | Medium | fixed on `week-pd/core-s13`; RUN 2026-11-20 (turn q_20261120151958397_423b68b2, client built from 86719c93, base 14 candidate: every stage's renderer reports the ROM's reflective triangles (Defection 1,537, Skedar Ruins 432, the Institute 376, Air Force One 321, Investigation 314, Chicago 242, Crash Site 231, Pelagic II 179, G5 87, Villa 18, Attack Ship 4); captures before and after show the window frames and desk edges of Defection's offices and the frame of the rooftop sign as lit metal where they were flat pale colour; GL against bgfx on the menu backdrop 548 differing pixels, largest difference 2, as before the change (545 and 2), and 152 to 185 with difference 1 in a match; the campaigns release and specials and 7 other smokes green. Re-extraction: 8,439 reflective triangles over the 87 scenes, lit_other 0, all 132 scenes equal the ROM room by room with the reflective triangles compared, conformance ok over 9,026 archives) | Reflective room surfaces are exported as plain textured triangles. A room display list turns on `G_LIGHTING` and `G_TEXTURE_GEN` (geometry mode bits 0x60000) for its environment-mapped surfaces: the vertex's colour index then names a normal, the texture coordinates come from that normal, and the shade from the room's light. The scene export ignores those bits, so such a triangle gets the normal read as a colour and the vertex's own (unused) texture coordinates. Measured against the ROM: Defection and Extraction 1,537 triangles (7.8 percent) in 102 rooms, Skedar Ruins 432, Carrington Institute and Defense 376 in 13 rooms, Air Force One 321, Investigation 314, Chicago 242, Crash Site 231, Pelagic II 179, G5 87, Villa 18, Attack Ship 4, and 20 in the background file `bg_oat`. A candidate for the Carrington Institute computer face of N-1073 (that one was B-1290). The whole ROM, measured by `tools/verify_scene_rooms_against_rom.py`: 3,761 such triangles in 12 background files, every one lit and texture-generated together (never lit alone, never linear generation), with the mode set before its vertices are loaded; G_TEXTURE scales 0x0400 to 0x1000. Fix: the export keys such a material with its G_TEXTURE scale and names it (`pd2_material.texgen`, `texgen_scale`, `texgen_uv_scale`; COLOR_0 keeps the normals, as the colour table does; the UV scale has its own key because `uv_scale` says a material is animated, and under that key the first extraction failed conformance on every reflective material), and both scene renderers (GL, and bgfx through `vs_pdscene.sc`) light and map it per vertex as fast3d does at G_VTX: the normal after roomHighlight (it runs over every entry of the table G_COL points at), lightsSetForRoom's ambient of brightness x 0.5882353 plus one light of the room's brightness along (0x4d, 0x4d, 0x2e), opaque, and UVs from the normal against this view's reflection vectors (`scenarioSceneLookAtReflect`: guLookAtReflect's right and up, quantised to signed bytes as the game stores them), (dot + 1) / 4 times the G_TEXTURE scale, then the material's runtime UV scale. A room's model matrix has no rotation (the camera's is in the projection, `camGetOrthogonalMtxL`), so the light and the reflection vectors are world directions. The ROM verifier compares the reflective triangles room by room in a scene whose exporter names them; the conformance tool checks the fields. Scene export version and arena cache kind gain `_texgen_b1268` (one re-export on boot). | V-010 / rendering | The scene carries normals for those triangles and names the material; the scene shader generates their coordinates and lights them as fast3d does for a ROM stage; matched captures. |
| B-1269 | Medium | open | Translucent room surfaces are drawn unsorted, and decals without their depth mode. The game draws a room's translucent blocks after the opaque pass in room draw order and, inside a room, in the order a tree of split planes gives for the camera (`bgRenderRoomPass`, `ROOMBLOCKTYPE_PARENT`); the scene merges all rooms per material and draws the blend groups in material order with depth writes off. Every surface of a translucent block list is given alpha blending and no depth write whatever render mode its display list sets (opaque and cutout surfaces in those lists included), and the decal render modes (`ZMODE_DEC`: 17 mode commands on Defection) are not carried, so a decal lies at the depth of the floor under it. Share of blend materials per stage: Chicago 46 of 105, Area 51 31 of 115, G5 28 of 83, Carrington Institute 15 of 81, Defection 9 of 72 (402 triangles). | V-010 / rendering | The scene carries each material's render mode and room order; the renderer sorts translucent groups by room distance, writes depth where the mode does and offsets decals; matched captures on Chicago and Area 51. |
| B-1290 | High | fixed on `week-pd/core-s11` (dev d43a7f72); SEEN 2026-11-20 (turn q_20261120055333846_bc8b2bf6, `scene_monitor_defection` 18/18 with two clients from the same place, the view proven by `look_dot=1.000`: the head of `week-pd/core-s12` without this change draws half of the PC's screen, cut along its diagonal with the casing's colour in the other half, and the head with it draws the whole screen; the pictures are kept beside the receipts, `core-tv-20261120-0553`) | **Half of every monitor and TV screen is not drawn: the Carrington Institute computer face of Mike's v0.1.105 reports (N-1073).** The screen is a prop's, not room geometry (the Institute's scene has no animated monitor material; its 8 `single_monitor` objects use `base:model_goodpc`). `tvscreenRender` (`src/game/propobj.c`) copies the screen node's vertices 0 to 3 as the screen's corners and draws triangles (0,1,2) and (0,2,3). A model compiled from typed source stores each node's triangles flattened, three vertices each (`port/src/modasset_compiler.c`, `fillGeneratedTriVertices`), so the node's six vertices are A B C A C D and vertex 3 is A again: the second triangle has no area and that half of the screen shows the casing behind it. Seen in the Grid session's capture of the Institute intro (`ci-motion-c.png`, 2026-11-18): the screen's text covers one triangle, split along the diagonal. All 15 screen nodes of the 12 base monitor models (235 monitor objects in the base scenarios: PCs, terminals, TV screens, comms hubs, consoles, the G5 alarm and laser switches) are one load of four vertices drawn as (0,1,2), (0,2,3), so all were affected. Fix: when the node has six vertices whose fourth and fifth repeat the first and third, the fourth corner is vertex 5. | V-010 / rendering (N-1073) | A capture of a monitor in the Carrington Institute shows its whole screen; `scene_rooms_citraining` and the menu backdrop captures before and after. |
| B-1291 | Low | fixed on `week-pd/core-s12`; RUN 2026-11-20 (turn q_20261120055333846_bc8b2bf6, `scene_monitor_defection` 18/18 twice: the hook waited from frame 4 to frame 130 for the intro, teleported, and the view stayed on the screen's centre, `look_dot=1.000` in 10 of 11 log lines, the first being the frame of the teleport. The teleport put the player on the nearest free floor, about 80 units from the asked point, which is why the view follows a point instead of taking angles) | `--debug-spawn-at` is undone in a mission that has an intro, and the hook still reports success. The hook teleports player 0 once, on the first frame from frame 4 on which the player has a body (`bootDebugSpawnAtTick`, `port/src/main.c`), before `lvTick`. A mission's intro is playing then: it animates the player's own body with absolute positions, and while the tick mode is the cutscene's the character tick moves the prop along with the animation (`chr0f0220ec`, `src/game/chr.c`), so the player is back in the intro's place on the same frame and starts the mission where the intro ends. Run 2026-11-20 (turn q_20261120051438463_9b96f251, a trial of a capture fixture on Defection, `--debug-spawn-at 198,-1983,721,144` with `--debug-force-first-person-look`): the log says `--debug-spawn-at consumed ... result=OK pos=(198,-1983,721) room=144` and the three screenshots show the helipad. Every fixture that used the hook before runs in an arena or in the front end, where no intro plays, so no existing result is affected. Seen in the same captures: the scripted `ACTION_AIM_*` holds of `scene_rooms_defection` did not turn the view (its five play screenshots show one direction), so a capture's direction has to come from a hook too. Fix: two companions, the old hook unchanged. `--debug-spawn-at-after-intro` holds the teleport until player 0 has had control (normal tick mode, walking) for one second of game time, which also covers an intro of several shots; `--debug-spawn-at-look-at x,y,z` turns the view, yaw and pitch, toward a world point every frame once the teleport has run and logs `look_dot` (1.000 when the camera looks straight at the point), so a fixture can require the view instead of trusting it. Without `--debug-spawn-at` both say they are ignored. `scene_monitor_defection` uses both. | smoke tooling | A capture fixture places the player in a mission after its intro and its screenshots show the intended view. |
| B-1260 | Low | fixed on `week-pd/core-s4` | The extractor wrote an objects.json weapon pickup's `weapon_catalog_id` and `secondary_weapon_catalog_id` from the extraction-time catalog only (`catalogWeaponIdByRuntimeWeaponNum`, `romextract_pdarena.c`), so the device and item weapons it does not know (tracer bugs, key cards, briefcase items, the disguise, flight plans, grenade rounds) came out empty, while `setup.fields.json` names the same weapons through the game's weapon table (`s_setupWeaponCatalogIdByRuntime`). Play was unaffected (the runtime builds pickups from `setup.fields.json`), but the public objects table did not say what 77 or more pickups in 26 archives of base 7 are. Found by the payload check of CORE's turn q_20261117010613172_f7fbd316 (2026-11-17): 6 of the 37 first object event payloads named a weapon their objects.json row did not (CORE). | T-MODDING-011 / B-1257 | Fixed on `week-pd/core-s4`: the row uses the same naming as `setup.fields.json`; conformance checks that every objects.json row names the weapons of its `setup.fields.json` record (`validate_object_weapon_refs`, `tests/test_ai_lists_source_conformance.py`). Takes effect with slice 4's re-extraction (`_ruleevents`). |
| B-1173 | Medium | source_connected | CORE's C: location policy refused every test install, with a misleading message. Each new copy reserves the larger of its size and `peak_run_gib` (2 GiB in `storage-policy.json`), and CORE had set `max_full_install_gib` 1.5 for C: (Mike: "only one or two at a time"), so `make_room` (`tools/smoke-verify/storage.py`) could never fit one and raised "Full-install budget blocked by active/pinned evidence" in an empty storage. CORE's queued base 4 game run (2026-11-15 14:07 UTC) lost its turn to this: all five tests refused in 4 minutes, nothing ran. The same 1.5 GiB example was in the week note for the other sessions. The earlier C: attempt had been refused at the disk floor first, so the budget was never reached until now. Fixed on `week-pd/core`: outside retained housekeeping, a budget that cannot fit one copy is refused at once with "max_full_install_gib ... cannot fit one install", before anything is written (`test_byte_budget_below_the_run_reserve_says_so`); CORE's C: location uses 3 GiB (two installs of about 0.45 GiB plus one run's 2 GiB reserve; actual use stays one or two installs, each deleted after its run). | T-TOOLING-006 | Propagation: the reserve also applies to reuse profiles (`profile_bytes`), covered by the same check. Week note example corrected to 3 GiB. |
| B-1171 | Medium | source_connected | A smoke storage location's own limits were undone on every harnessed run. The scripts send the whole checked-in `storage-policy.json` as the run's policy (`Invoke-SmokeStorage` in `tools/smoke-verify/lib/Storage-Harness.ps1`, `lib/Runner-Retention.ps1`; `run-asset-inspection.ps1` always names it with `-StoragePolicy`), and `Storage.__init__` (`tools/smoke-verify/storage.py`) let a sent policy win over the location file's `"policy"` (CORE's d28a28e7, decision 6B). CORE's first smoke with installs on C: (boot_smoke, 2026-11-15 08:12 UTC) was refused at the checked-in 300 GiB floor instead of the location's 200 GiB, and the C: limits Mike asked for ("only one or two at a time": two installs, 1.5 GiB) would have run as five installs and 8 GiB. The unit test built `Storage` directly with no sent policy, so it missed the harness path. Fixed (ec2ba001 on `week-pd/core`, 7a26faf4 on `week-pd/runner`): a location file's limits govern its root whatever policy is sent, the limits it leaves unset follow the sent policy, and the checkout's own drive (builds) keeps the sent policy. `test-storage.py` and a new harness check in `test-storage.ps1` fail on the old rule and pass on the new one; every storage suite passes. | T-TOOLING-006 | No other session had a C: location yet, so no one else's runs were affected. The C: run itself is still refused, now correctly under D-011: C: had 203.7 GiB free and a run needs the 200 GiB floor plus 4 GiB (question to Mike, 2026-11-15 08:30 UTC). |
| B-1172 | High | fixed on `week-pd/core`, seen in game (base data) | Stages drawn by the scenario scene renderer have no sky, and by the code no starfield and no always-drawn sky rooms. **Seen in game 2026-11-15 16:59 UTC** (`stage_sky_visual_villa`, base 4): in Villa's intro the ground and rock are drawn and everything above them is black where the daytime sky should be (`D:\PD2-wt\core\.claude\week-pd-receipts\base4-campaign-20261115\screenshots\003-villa_play_b.png`); Defection's night frame is too dark to judge the moon and stars. The two fixtures took their first two screenshots during the 90 s boot, so their timings need to move later. Found by reading code during the stage-check survey. `lv.c` skips `skyRender` whenever `scenarioSceneRendererIsActive()` (08d32a45, B-936, written so the held CI menu keeps its room backdrop); `bgRenderScene` (`src/game/bg.c`) skips the pre-pass that draws the moon, the planet, the fake sky and the shaft first with fog off, and the starfield (`starsRender`), whenever `sourcebg` is set; and `port/fast3d/scenario_scene_renderer.cpp` has no sky, stars, fog or sky-room handling of its own. The scene renderer was active on all 18 stages of the 2026-11-15 release campaign ("SCENARIO.RENDER: activated source scene" for each), so an outdoor stage (Villa, Chicago, Crash Site, Air Base) would show the clear colour where its sky should be, and a sky room would sit at its world position instead of moving with the player. Widened 2026-11-15 13:55 UTC (also from code): the scene renderer reads no room state at all (no `g_Rooms` brightness or light state, no dyntex, no flash), so the stage is drawn as a static model: room lights that are shot out or switched by AI, explosion and spark flashes (`roomFlashLighting`) and animated water and power textures (`dyntex.c`) would not show either. That also means the room light rules (B-1114 shootable lights, the `rooms.json` light rules) and any animated-texture or spark rule have no visible effect on these stages until the renderer reads them. | T-MODDING-011 / rendering | Visual check queued with CORE's base 4 run: `stage_sky_visual_villa` and `stage_sky_visual_defection` (new definitions in `tools/smoke-verify/tests/`) take screenshots in the intro and in play. If confirmed, the fix belongs to the scene renderer (sky dome, stars, fog, and the `sky`/`render_always` rows of `rooms.json` for sky rooms), not to the light rules. Owner since 2026-11-15 (Mike): CORE. Root cause of the missing sky, from code: fast3d drew the scene before the frame's display list ran (`gfx_pc.cpp`), so every display-list draw lands on top of it; the sky fill was therefore turned off (B-936), and the frame's own depth clear (`viPrepareZbuf`) then wiped the scene's depth, so props and characters are never depth-tested against stage geometry on these stages (also from code: the scene's depth comes from its own projection, which the game's draws cannot test against, B-936 note in `scenario_scene_renderer.cpp`). Step 1 (built on `week-pd/core`, in-game check queued): a port command `G_SCENE_RENDER_EXT` draws the scene in display-list order after the sky, the stars and the sun, before the props, into the current view with the camera of the player that queued it, and clears depth after it as the frame used to; the sky renders again and the stars draw on these stages. Step 2 (built 2026-11-15 on `week-pd/core`, pd-tests 1,931 cases; in-game check queued with CORE's base 5 turn, `stage_sky_depth_villa` and `stage_sky_depth_defection`): **the B-936 depth mismatch was the stage's BG scale.** The game's matrices work in world units times `bgGetScaleBg2Gfx` (0.5 on Villa, 1 on Defection) relative to the camera room's position (`playerAllocateMatrices`, and `mtxF2L` scales every model matrix's x, y and z columns), and the perspective's near and far planes are in those units, while the scene camera was built in world units with the same planes, so its depth values differed wherever the scale is not 1 (unit-tested in `tests/test_scenario_scene_camera.cpp` against the game's lookat and perspective). Now a world draw uses the game's own BG matrix, the draw space `(p - draw_offset) * world_scale` (`port/src/scenario_scene_camera.c`) times the BG projection loaded just before the command, and keeps its depth, so props and characters are hidden behind the scene's walls; the scene's translucent materials draw after the props' opaque pass, where the BG's translucent room parts drew (`SCENARIO_SCENE_PASS_*` in the command's w0); the held menu and the lock screen draw it as a backdrop and clear depth after it; sky rooms (`rooms.json` `sky`) move with the camera's room as `roomPopulateMtx` places them (the scene's `_PD_ROOM` vertex attribute); and the shader applies `roomHighlight`'s lighting (settled regional brightness, flash, the scenario's room highlight for King of the Hill and Capture the Case, the night-vision greyscale), captured per player view like the camera. `--debug-scene-matrix-probe` logs how far on-screen scene vertices land apart through the game's matrix and the scene camera. Also built (pd-tests 1,932 cases, in the same queued depth smokes): the stage fog (a56adf2a: bgRenderScene starts the fog around each scene pass, fast3d hands its fog state to the renderer, and the scene shader applies gfx_sp_vertex's formula; sky and render-always rooms stay unfogged), and the starfield as a `stage.json` `stars` rule (50db8302). **Animated textures built 2026-11-16 (23a55418):** the `dyntex.c` presets (river, monitor, ocean, arrows, teleportal, powerring, powerjuice) are scene data now, written by the extractor with the stage rules applied and moved each frame by the scene renderer (`context/designs/modding/animated-surfaces.md`); unit-tested against `dyntex.c`, in-game check queued on a private copy of base 6. Still missing on these stages: the per-colour light flag `roomHighlight` reads (a colour whose same-index vertex has `flags & 1` keeps its rgb and only scales its alpha by the room brightness; `dlights.c` marks the index lookup @bug?), which the scene's room lighting does not model yet; the extractor now logs a "B-1172 light-flag census" per scenario (rooms and colours that carry the flag, read as roomHighlight reads it). **Census 2026-11-16 02:45 UTC (CORE client 29238890 on a re-extracted copy of base 6): 0 flagged colours in all 87 scenes**, so for base data the scene lighting already matches roomHighlight; a custom level that sets the flag would still need it modelled (not built). **Animated textures seen in game 2026-11-16 02:46-02:58 UTC** (CORE client 29238890, private re-extracted copy of base 6; release 54/54, Complex 12/12, Villa 16/16): every visited stage logged its animated vertices with no unusable primitive (Attack Ship arrows 894 in 17 groups, Deep Sea river 246 and teleport 336, Villa ocean 219, Chicago river 120, Complex ocean 12, the monitor on Air Force One, Defection, Extraction and Investigation) and the first frame that moved them; Deep Sea's teleport held while its teleports were off. Census of that copy: 16 scenes with animated materials, none mismatched. **Seen in game 2026-11-15 22:26-22:33 UTC** (base 5, step 2 client with fog and stars): Villa's sky is blue with clouds where the base 5 client showed black, and the scene sits exactly where it did (it draws with the game's own matrix); Defection shows its stars and its moon room (6 sky vertices); no shader or upload failure. A few sky pixels now show through hairline cracks between scene polygons that black used to hide. The matrix probe compared nothing: its six frames fell in the intro cutscene (`compared=0`); it now counts only frames with scene vertices on screen (f3dea116), rerun queued on base 6. |
| B-1112 | Medium | source_connected | The in-game archive validator skips reference checks on text members larger than 4 MiB (`ASSET_ARCHIVE_TEXT_SCAN_LIMIT`, `port/src/asset_archive_policy.c`: `if (!text && size > LIMIT) return 0`). Found through MODS's B-1145: all 45 base arenas with a nested scenario carried the same dangling `mtllib scenario.mtl` line in `collision.obj`, but the four with a collision source over 4 MiB (infiltration, extra17, attackship, deepsea) passed release validation and the other 41 failed. A large member is exactly where a missing reference is most likely to go unseen. The limit protected no memory: the member was already extracted whole before its size was checked. | T-ASSETS-001 / V-006 | Fixed 2026-11-15 on `week-pd/core` (CORE): text members (.ini, .obj, .mtl, .json, the manifest) are checked whole up to 128 MiB, and above that the archive is refused ("too large to check for references") instead of passing; the in-memory manifest check no longer lets a second spelling's read hide an oversized first one. Measured on base 3 first: 83,268 scanned text members in 9,025 archives (819 nested), 19 over 4 MiB (17 `collision.obj`, 2 JSON), largest 5.03 MiB, none over 16 MiB. `[b1112]` (`tests/test_asset_archive_large_text.cpp`): a dangling `mtllib` in a member over 4 MiB is refused through the opened, memory and nested paths, and the same member with its `.mtl` passes. The opt-in sweep `[b1112-sweep]` (set `PD2_VALIDATE_ARCHIVE_ROOT`) ran the validator in the boot catalog's RELEASE mode over all 9,025 typed archives of base 3: 0 refused (13 s). |
| B-1105 | High | regression_gate | The shared ordered-material planner, request-owned failed-load latch, and SP-74 typed source binder now serve top-level and nested `.pdmesh` ingress with one canonical alias order. Exact client `395D48A6...` passes final focused 132/14, full 67,152/1,223, native-source guard, two-archive verification, fixture validation, 23/23 unchanged source hashes, and strict fresh-cache ordinary receipt `results-20260826T152922Z.json` at 54/54 in 33.8 seconds. The embedded Mauler source owns exactly one 13-material compiler/activation/Bond-gun/render chain, gameplay fires 11 shots, no runtime native/ROM boundary or endscreen occurs, and all four reviewed gameplay captures retain correct silver/green placement and the V-009 no-obstruction gate. | V-010 | Retain the final automation, exact binary hashes, strict receipt, and four reviewed captures. Revalidate this generic source/material contract through representative public mesh families and the V-010/V-013 release matrix; never credit the superseded `D1788982...` client or earlier rejected receipts as final closure. |
| B-249 | High | repro_required | A prior playtest reported bot-versus-bot deaths credited to player 0. Static tracing found no hardcoded player-0 write and the existing diagnostic must identify the upstream attacker attribution first. | V-010 | Run a controlled bot-only Combat Simulator match with no player damage, capture attribution diagnostics, fix the authoritative damage/death source, and prove player 0 remains at zero kills. |
| B-242 | Medium | repro_required | Players have previously spawned intersecting walls. Existing pad and ray validation does not prove a complete capsule fits at the committed spawn. | V-010 | Add one shared bounded capsule-placement validator for players and bots, search safe radial candidates or the next pad without infinite retry, then prove representative campaign and Combat Simulator maps. |
| B-174 | High | repro_required | Car Park previously stacked bots at one spawn and then congregated them at one destination. A mitigation and diagnostics exist, but no current ordinary-client closure receipt does. | V-010 | Reproduce with the original map and roster, prove distinct valid spawns and continued AI decisions, fix the shared spawn/path state if it recurs, and retain a timed gameplay artifact. |
| B-183 | High | confirmed | Character bodies or heads previously showed disconnected triangles or holes. The frozen ordinary two-client logs now reproduce a concrete assembly gap: `body0f02ce8c` for bodynum 86/headnum 4 has no `MODELPART_CHR_HEADSPOT`, so `body.c` safely skips the separate head attachment. This is current source evidence, but no capture yet proves the visible result or whether the exact public body source lost the attachment node. | V-010 | Trace exact catalog IDs for body 86/head 4, restore or deliberately classify the authored head attachment, and capture that pair with no warning, complete head/neck geometry, and exact source identity. Then retain representative human, Skedar, integrated-head, mixed body/head, campaign, and Combat Simulator captures. Current logs: `.claude/smoke-verify-install/logs/game client/pd-host.log:8153` and `pd-client.log:9654`. |
| B-1064 | High | regression_gate | The reconnect slice is production-verified at protocol v57, including endpoint-scoped retry identity, exact world/inventory restoration, resumed authority-accepted fire, clean exit, and corrected retained-log verification at 98/98; the immutable raw receipt remains rejected at 92/95 because it used the superseded B-1100 verifier. The Campaign/player-init unit prepares private player/network candidates before global publication, validates exact runtime-to-typed body/head identity and source modeldefs before player-prop allocation, propagates typed stage failure to disconnect/reset/title rollback, and reverses chr/model/weapon/fireslot attachments on every late chrbody failure. Frozen Air Base 25/25 and full Campaign plus restart 23/23 pass on client `2fc37b38...`. The rejected Combat Simulator run was independently traced to B-1101's legacy animation-frame decoder spin, not the candidate-first transaction. B-1101/B-1102 now pass frozen focused 2,057/27, full 65,073/1,186, the native-source guard, zero-mismatch 2,717/402-file manifests, and exact-client Combat Simulator 56/56 on `133A55C6...` with both starts, real CMP150 fire/hit, stats/awards, Play Again, clean exit, zero decoder/crash matches, and no leaked process. | T-ENGINE-004 | Retain the accepted reconnect, Campaign, Air Base, B-1101/B-1102 automation/runtime, source-manifest, and B-801 guard receipts without rerunning them; broader T-ENGINE-004 lifecycle work remains separately partial. |
| B-1065 | High | regression_gate | `matchOptionsForceBit` now records only option bits the engine actually adds, while one user-view query and shared replace/restore lifecycle preserve user-owned choices across match end, cancel, disconnect, room-settings replacement, and the next start. Frozen focused/full automation passes, and the exact-client two-cycle Combat Simulator receipt starts both matches with `user_options=0x00200001 forced=0`, reaches both results flows, uses Play Again, and exits cleanly. | T-ENGINE-004 | Retain the ownership/lifecycle tests and accepted `.claude/smoke-verify-runs/results-20260826T024226Z.json`; revalidate only through the broader T-ENGINE-004/release matrix. |
| B-1066 | High | regression_gate | One narrow `playerInitStageTransientDefaults` boundary establishes all seven stage-only weapon/input defaults for fresh allocation and `bgunReset`: `weaponnum`, `prevweaponnum`, `prevwasdualwielding`, `wantammo`, `passivemode`, `wantsjump`, and `jumpconsumed`. It preserves `client`, `isremote`, `ucmd`, queued-load ownership, and persistent preferences, and respawn does not use it. Frozen focused/full automation and Campaign transition/restart receipts pass; the exact-client replacement Combat Simulator receipt proves two starts, preserved user/weapon setup, real CMP150 fire/hit in the first cycle, both result flows, Play Again, and clean exit. | T-ENGINE-004 | Retain the focused transition contracts, accepted Campaign receipts, and `.claude/smoke-verify-runs/results-20260826T024226Z.json`; revalidate only through the broader lifecycle matrix. |
| B-1067 | High | regression_gate | The prepared lobby roster no longer relies on mutable lobby slots: source restores declined clients to their exact pre-transaction binding, compacts one accepted roster, derives remote configuration from validated client settings, preserves authoritative teams and the prepared typed mission ID, and orders session-catalog delivery ahead of the manifest. The same transaction aborts before publishing a preparing participant's valid settings change and reverse-unwinds every committed stage player if a later co-op/Counter-Op reset or spawn fails. Current-v58 ordinary-client receipts pass co-op 96/96, Counter-Op 98/98, later-player rollback 60/60, and settings rollback 43/43 on exact client `5DA75BE6...`, with epoch-correct release, clean exits, and no NPC mismatch/resync/rejection loop. | T-ENGINE-004 | Retain the four current-v58 receipts and source-linked transaction contracts; revalidate only through the broader T-ENGINE-004/release matrix. |
| B-1068 | High | confirmed | `scenarioLoad` resets and mutates live match/player/bot state before the file is completely validated. A present invalid typed arena, scenario, weapon, spawn weapon, bot profile, body, or head can fall through to a legacy integer or leave a partial prior/default value; arena loading also writes a stage-table index into the stagenum domain, and numeric casts can wrap. This contradicts V-006's validated fail-closed claim. | V-006 | Parse v3 into a complete local candidate with exact typed bindings and bounded values; allow v1/v2 numeric migration only when the typed field is absent and maps unambiguously; preflight every bot/profile and weapon; publish once; preserve byte-exact live state on every rejection. Re-run corrupt/truncated/mixed-validity load receipts plus a successful v1/v2-to-v3 migration and ordinary-client save/load/start smoke. |
| B-1069 | Critical | regression_gate | The network branch of `mpStartMatch` is now an immutable consumer: roster, teams, bots, options, weapons, spawn weapon, handicaps, typed stage, and RNG are finalized and validated before `SVC_STAGE_START`, while random/quick-team/challenge/backstop mutations remain offline-only. Frozen source contracts pin the post-freeze boundary. Both ordinary D-003 authority-role receipts consume the same session catalog, manifest, ready gate, two-player roster, typed stage, and spawns that the listen host serialized. | T-ENGINE-004 | Retain immutable-consumer tests and both current-product authority-role receipts indexed by `context/evidence/2026-08-14-d003-current-two-role-production.md`; revalidate through co-op/Counter-Op and release lifecycle gates. |
| B-1070 | Critical | regression_gate | Exact mode-typed stage resolution now accepts only the requested `ASSET_MAP` or `ASSET_ARENA` domain while preserving map-only campaign callers. The frozen ordinary listen-host/client receipt passes the valid typed Felicity arena through lobby preparation and the full match lifecycle. | T-ENGINE-004 | Keep the wrong-domain negative tests and accepted `.claude/smoke-verify-runs/results-20260813T091851Z.json` (56/56; binary `7261880C...`) in the release matrix; close with the remaining T-ENGINE-004 mode and rollback gates. |
| B-1071 | Critical | regression_gate | Protocol v55 removes the positional lobby handicap cache and transactionally transports each authenticated client's exact body/head IDs, team, nonzero handicap, options, FOV, and name before roster preparation. The frozen ordinary two-client receipt accepts both settings snapshots and consumes the compacted authoritative roster through spawn and endscreen. | T-ENGINE-004 | Retain pure roundtrip/rejection coverage and accepted `.claude/smoke-verify-runs/results-20260813T091851Z.json` (56/56; source `f86d23bb...`, binary `7261880C...`); close with reconnect and failure-rollback receipts that prove settings never drift or partially publish. |
| B-1072 | Critical | regression_gate | `mpPlayerSetDefaults` now clears exactly `gunfuncs[6]`, preserving neutral handicap and adjacent pointer state. A Luna xhigh propagation audit found no second live sibling, and the frozen ordinary two-client handshake retains handicap 128 plus exact typed identities through both spawns and match end. | T-ENGINE-004 | Keep the focused boundary test, propagation result, and accepted `.claude/smoke-verify-runs/results-20260813T091851Z.json` in the release matrix; close with the broader player-init transition and reconnect gates. |
| B-1073 | Critical | regression_gate | The shared lobby writer and both production callers now serialize the one canonical no-bot form, `sim_type=0`. The frozen ordinary listen host replays its own packet successfully, starts both clients, spawns both players, replicates scores, reaches the time-limit end, and shows the endscreen. | T-ENGINE-004 | Keep the 25-assertion canonicalization proof and accepted `.claude/smoke-verify-runs/results-20260813T091851Z.json`; close with repeated Combat Simulator start/return/restart coverage. |
| B-1074 | High | regression_gate | `mpCalculateAwards` iterated active participant `k` while excluding suicide-column `i`; `i` was stale from an earlier player loop, so KillMaster totals could include one participant's suicides, omit another participant's kills, and award the medal to the wrong player. Current source uses `k` for the suicide column and `MAX_PLAYERS` for human awards, with a verifier domain compile-checked against all 40 `MAX_MPCHRS` slots. The frozen ordinary two-cycle Combat Simulator receipt commits controlled non-tied totals in both cycles and correctly awards KillMaster to bot slot 8. | T-ENGINE-004 | Retain focused participant-domain and award-boundary tests plus `.claude/smoke-verify-runs/results-20260813T121222Z.json` as the ordinary production-path regression receipt; close with the remaining T-ENGINE-004 lifecycle matrix. |
| B-1075 | Critical | regression_gate | Manifest categories remain compact on v55, but exact catalog lookup now preserves the concrete map/arena/audio-family type through validation, diff, load, release, and disabled-row teardown. Transitions prevalidate and load every entering root, roll back new acquisitions in reverse on failure, release old roots only after success, and publish the new baseline last. Focused/related/full automation and the native-source guard pass on frozen `f86d23bb...`; the accepted ordinary two-client receipt contains one exact Felicity manifest load per process and zero mismatch, skip, rejection, rollback, crash, or fatal signatures. | T-ENGINE-004 | Keep atomic failure-path coverage and `.claude/smoke-verify-runs/results-20260813T091851Z.json` (56/56; binary `7261880C...`) in the release matrix; close with stage transition, reconnect, and friend-play manifest lifecycle receipts. |
| B-1076 | High | regression_gate | `mpStartMatch` is now the single Combat Simulator stage-request owner. It executes `menuStop` plus `SCENE_STAGE_TRANSITION_RELEASE_MENU_POOL` before publishing the next stage for offline Room/challenge, listen-authority, and receiving-client starts; caller-side post-request cleanup is removed. Exact client `04DB220E...` passes the ordinary title -> Agent Select -> Main Menu/Play graph -> Room -> gameplay -> endscreen -> real Play Again -> second gameplay/endscreen route 55/55 with one balanced Agent Select owner, two balanced Room owners, two stable typed gameplay/results barriers, no watchdog repair, clean exit, and intact allocations. The retained first run is rejected at 51/55 only because its fixture expected `pass` instead of the harness's emitted `satisfied`; product behavior completed both cycles. The same client preserves the invitee-authority D-003 route at 170/170 with one listen host, one signed typed match-server route, one join, and no probe/relay handoff. | T-ENGINE-004 | Retain both accepted receipts, the rejected verifier-calibration receipt, centralized-owner propagation tests, and V-009 as the visual regression gate in future gameplay captures. Revalidate through the broader base-game/release lifecycle matrix. |
| B-1077 | High | regression_gate | The deterministic near-player bot hook mixed floor and actor-root domains, then ordinary non-absolute CHRINFO regeneration overwrote the desired root height. Floor relocation now commits the desired root and cached ground together; the model records a persistent non-absolute root-height bias across animation init/frame regeneration while preserving authored absolute translation. No visibility, cull, or hit state is synthesized. The source-frozen two-cycle ordinary client held root y=159 for 15 seconds with zero failures, then produced two natural hits and completed both matches. | T-ENGINE-004 | Retain focused root/animation contracts, the bounded diagnostic `.claude/smoke-verify-runs/results-20260813T152854Z.json`, and the accepted 56/56 release receipt `.claude/smoke-verify-runs/results-20260813T153846Z.json`; close with the remaining T-ENGINE-004 transition/reconnect/network matrix. |
| B-1078 | High | regression_gate | The delayed legacy menu autosave queue still routed normal Combat Simulator match-end persistence through `filemgrSaveOrLoad`. On PC, the missing Controller Pak covered the healthy MP endscreen, so Play Again stopped receiving input. The queue now dispatches Agent Profile and MP-player writes to the canonical atomic PC-native JSON writers, strips the legacy player-name newline, consumes invalid sentinel indexes safely, and reports background save results without opening Pak UI. The frozen ordinary two-cycle receipt reaches both endscreens, uses the real Play Again accept edge, and returns without Pak UI or watchdog repair. | T-ENGINE-004 | Keep the 23-assertion focused contract and `.claude/smoke-verify-runs/results-20260813T121222Z.json` plus its native `SAVE.QUEUE` log evidence in the release matrix; close with the remaining T-ENGINE-004 lifecycle gates. |
| B-1079 | High | regression_gate | A direct/bootstrap Combat Simulator start could seed and consume `g_MatchConfig` before the Room renderer's private initialization latch was set. On Play Again, the first Room frame treated that production config as a cold entry and reset Chicago, the bot, and user options. Solo Room return now explicitly adopts the existing configuration and rebuilds only derived selectors. The frozen ordinary two-cycle receipt contains exactly one cold `MATCHSETUP` initialization, exactly one Room adoption, and starts cycle two with Chicago, two slots, and `user_options=0x00200001` intact. | T-ENGINE-004 | Retain the focused reset-versus-adopt ownership contract and `.claude/smoke-verify-runs/results-20260813T121222Z.json`; close with the remaining T-ENGINE-004 lifecycle gates. |
| B-1080 | Critical | regression_gate | Generated character clones share source rodata/GDL ownership, but identity-only provenance once rebuilt a direct generated pointer as a legacy segmented offset and let the hit walker cross mapped memory. Generated ownership now recognizes clones through shared root rodata, exposes exact remaining spans for simple, hierarchy, relocated GUNDL, and attached-head payloads, validates terminal `G_ENDDL` and command domains, preserves pointer width, and bounds character/object opaque/translucent walkers while retaining a native-only corruption fuse. Pure/focused/full automation and the native-source guard pass; the accepted ordinary client produced two natural hits with no `MODEL.GDL.REJECT` or crash. | T-ENGINE-004 | Retain the exact-span and generated-source contracts, full 59,094/1,117 suite, native-source guard, bounded diagnostic `.claude/smoke-verify-runs/results-20260813T152854Z.json`, and 56/56 ordinary receipt `.claude/smoke-verify-runs/results-20260813T153846Z.json`; close with the remaining T-ENGINE-004 matrix. |
| B-1081 | High | regression_gate | Smoke diagnostics are now fixture-owned and finite: every fixture assigns jump logging on or off; generated render steps emit once per owner/stage; the Needler witness arms once per owner; camera-only first-person forcing cannot enable weapon logging; and explicit/generated-audit weapon traces use an atomic 2,048-line budget plus one suppression witness. The exact audit client passed 13/13 with 33 render steps, zero capsule leakage, exactly 2,048 weapon lines plus one suppression line, a natural hit, and clean exit. The ordinary release client then passed 56/56 with all three diagnostic streams at zero. | T-ENGINE-004 | Retain focused contracts, `.claude/smoke-verify-runs/results-20260813T152854Z.json`, and `.claude/smoke-verify-runs/results-20260813T153846Z.json`; close with the remaining T-ENGINE-004 matrix and preserve explicit finite diagnostic ownership in future fixtures. |
| B-1082 | High | regression_gate | The friend-play identity generator duplicated an incomplete Agent Profile v2 object and omitted `besttimes`, so D-005 correctly rejected both generated roles before presence startup. The generator now clones the canonical validated Agent fixture and changes only `name`; focused `[d-003]` proves both outputs are deep-equal except for that name and retain all 21 best times. Current-product ordinary clients pass initiator authority 214/214 and invitee authority 218/218 on exact client `D1C91583...`. | T-ENGINE-004 | Retain the canonical-template/no-inline-schema contract and the paired receipts indexed in `context/evidence/2026-08-14-d003-current-two-role-production.md`. |
| B-1083 | High | regression_gate | Machine-global `mods-enabled.json` could not survive exact Agent v2-to-v3 migration, so the original authority fixture lost its selected custom weapon. The authority-only validated preference sidecar now migrates that selection into the Agent-owned v3 document while the joiner remains clean. Both current-product authority roles now complete one election/listen start/join, package transfer, manifest readiness, two-player stage start, gameplay, and clean exit without player-init rollback. | T-ENGINE-004 | Retain the authority-only migration sidecar, clean-joiner contract, and paired current-product receipts. |
| B-1084 | High | regression_gate | The campaign-derived Chicago arena's authored intro still owned gameplay after `--debug-force-first-person`, leaving the typed weapon switch queued and suppressing fire/effects. Reusable typed readiness now requires a stable gameplay interval and uses a bounded balanced production `ACTION_SKIP_CUTSCENE` assist when needed. Both current-product authority roles pass stable gameplay, source-backed weapon/effect execution, and clean exit on exact client `D1C91583...`. | T-ENGINE-004 | Retain the typed stable-readiness/assist contracts, paired current-product receipts, and V-009 unobstructed-frame visual gate. |
| B-1085 | High | regression_gate | Focus lifecycle now reaches input authority before consumable UI dispatch and reconciles `SDL_WINDOW_INPUT_FOCUS` once per pump. The verifier resolves exactly one visible top-level HWND per PID, checks GUI-thread active/focus ownership, requires fresh source `focus GAINED -> LOST`, and anchors only explicitly tagged post-loss sequences. The final focused batch passed parsing, JSON, assertion self-tests, embedded C# compilation, and 402 assertions in 13 cases. Invitee authority then passed 218/218 at `.claude/smoke-verify-runs/results-20260814T030245Z.json`: exact post-loss owner acquisition, `fire_held=1`, source-backed effects, two scripted exits, zero operational failures/leaks, and unchanged product/test/tool fingerprint `2784d121...`; initiator authority remains accepted 214/214 on the same product binary `D1C91583...`. | T-ENGINE-004 | Retain named-anchor fail-closed coverage and the paired current-product receipts; do not replace fresh SDL/GUI-thread proof with launch order or foreground z-order. |
| B-1086 | High | source_connected | Historical 3440-wide Carrington evidence retains the solid-white far-right door obstruction; the bounded 1920 repro was not wide enough for closure. Schema v2 `model.render.json` preserves exact geometry set/clear order, per-corner load-time geometry and colour state, recursive 64-slot ownership, complete face/group ownership, and exact legacy-v1 compatibility. Current source also centralizes parameter-byte `G_VTX` decoding across extraction, fast3d, collision, and arena conversion, restores the complete star domain, shares top-level cache state only for Type-3 pairs, and makes room collision honor nonzero destination slots. Exact frozen client `36EA05D1...` passed focused 97/7, full 67,361/1,230, conformance/workflow/native-source gates, and unchanged manifests; production receipt `results-20260826T193629Z.json` proved clean 733/733 extraction and clean exit. Its 20/26 visual result remains rejected because B-1106 exposed the missing Type-3 list-scope representation before either door activated. B-1106's exact schema-v3 cache-provenance replacement is now automation-accepted on frozen client `7EF396C9...` but not yet production-verified. This remains distinct from fixed B-1054/B-1055/B-1056. | V-010 | Retain both frozen automation units, then prove exact door state plus Carrington/Campaign/Combat Simulator visual gates at true 3440-wide resolution. |
| B-1087 | High | regression_gate | Smoke preflight treated a typed catalog ID as an install-relative Windows `remove_paths` entry, so `GetFullPath` rejected the fixture before launch. The nonexistent path was removed from all three affected fixtures, and a corpus-wide static contract rejects empty, absolute, parent-traversing, or colon-bearing remove paths across all 130 smoke definitions. Both current authority-role smokes now start from clean per-process installs and pass. | T-ENGINE-004 | Retain the corpus-wide path lint and paired current-product receipts. |
| B-1088 | High | regression_gate | An instantaneous gameplay-ready sample let the authored intro retake control after the verifier advanced. The reusable wait policy now requires a measured continuous stable interval, permits at most one bounded production cutscene-skip assist while false, balances every press/release on success or failure, and validates the whole strict fixture document including incompatible fields and cross-wait holds. Both current-product authority roles pass two stable gameplay windows with terminal assist receipts, no repeated assist/readiness timeout, clean exits, and zero leaks. | T-ENGINE-004 | Retain the pure state-machine/schema coverage and paired current-product receipts; never substitute a one-frame readiness sample. |
| B-1089 | Critical | regression_gate | The cutscene stream introduced at protocol v56 owns one match-room-scoped reliable START/ACCEPT/END lifecycle; the current aggregate protocol is v58. Only server/offline authority mints generations; requests use exact authenticated client identity; acceptance is transactional/idempotent; receivers require the frozen roster mask and reject stale/duplicate/conflicting state; terminal END and stage end preserve prepare/send/commit ordering; teardown retires authority state idempotently. Product freeze `cc8791c1...` passed 60,824 assertions in 1,152 full cases and the native-source guard. Current initiator authority passes 214/214 and invitee authority passes 218/218 on exact client `D1C91583...`, including ordered generation 1 END then generation 2 START, accepted/published/applied skip, stable gameplay, and clean teardown. | T-ENGINE-004 | Retain the v56 cutscene codec/state-machine/teardown contracts inside current protocol v58 and the paired v57-product receipts until the required v58 friend-play rerun. |

| B-1090 | Critical | regression_gate | Global scenario camera/condition work inherited the remote player left selected by a prior per-player loop, so the receiver's real local player never advanced into the authoritative cutscene. The runtime now resolves committed receiver-local identity by player pointer plus `!isremote`, restores it before in-client global `lvTick`, keeps dedicated simulation policy separate, routes graph/native cutscene operations through explicit presentation APIs, and reports success only after authority commit. The current 214/214 initiator and 218/218 invitee receipts both require local player 0 body-ready/committed/active/in-progress, ordered generation transitions, accepted/published/applied skip, stable gameplay, source-backed effects, and clean exit on exact client `D1C91583...`. | T-ENGINE-004 | Retain explicit receiver-local presentation identity, fail-closed global-tick coverage, paired current-product receipts, and the separate V-009 visual regression gate. |
| B-1091 | High | regression_gate | The first B-1064 production-smoke attempt never launched: one regex used an invalid JSON escape, discovery logged and skipped the malformed explicitly requested fixture, and the zero-selection branch returned exit 0. Discovery now throws on malformed JSON, zero selection throws, all 131 definitions parse, focused correction passes 339 assertions in 6 cases, the explicit unknown-selection check exits nonzero, and the later exact reconnect process run produced a fresh named artifact. | T-ENGINE-004 | Retain SP-56's fail-closed discovery/selection contracts and require every release runner to name a nonzero executed test in a fresh result artifact. |
| B-1092 | Critical | regression_gate | ENet reports the remote disconnect datum as zero to the initiating sender's acknowledged-disconnect event. The server now latches its first authenticated disconnect intent before entering ENet and consumes it exactly once during authoritative teardown. The exact `0f377e3e...` ordinary reconnect run records reason 5 with `transport_reason=0 server_intent=1 retryable=1`, preserves one reservation and endpoint credential, authenticates one retry, commits once, resumes authority-accepted fire, and exits cleanly; corrected retained-log verification passes 98/98 on unchanged product `7437d77c...`. | T-ENGINE-004 | Retain SP-57 across every server disconnect caller, including terminal kick/ban/policy races; keep the exact reconnect artifact in the release matrix. |
| B-1093 | High | regression_gate | `smokeCaptureReadinessFacts()` hardcoded runtime player 0 for player presence, cutscene, and control instead of following SP-54's receiver-local identity rule. Current source resolves the committed local player through `playermgrGetLocalPlayerNum()` and makes every readiness fact fail closed when that identity is unavailable or ambiguous. Product/verification aggregates `cee25049...`/`eddd3c9...` remained frozen while the isolated build, focused 510/9, complete 61,302/1,159 suite, and native-source guard passed. The later exact-client receipt proved this ordinary client really occupied runtime slot 0, so B-1093 was a valid structural correction but not the cause of that run's stalled readiness barrier. | T-ENGINE-004 | Retain the canonical local-player projection and exercise it whenever an ordinary client maps itself to a nonzero runtime slot; do not infer runtime slot from stable client ID. |
| B-1094 | Critical | regression_gate | `playerEndCutscene()` served both authored cutscenes and the ordinary Combat Simulator opening swirl, so a broad client return trapped joined players in `TICKMODE_MPSWIRL`. Authority policy now lives at `playerSetTickMode`: authored CUTSCENE exits remain guarded while local MP swirl completion is permitted. The exact `0f377e3e...` reconnect run reaches NORMAL gameplay before and after timeout, commits exact replay state, resumes authority-accepted fire, and exits cleanly; corrected retained-log verification passes 98/98 on unchanged product `7437d77c...`. | T-ENGINE-004 | Retain SP-58's transition-specific authority boundary and both protected CUTSCENE and permitted MPSWIRL tests. |
| B-1095 | High | regression_gate | A host once began its peer-dependent stage deadline before the runner could launch the peer, making cold startup consume the connection window. The host fixture now requires typed `network_listen_ready` before beginning `network_stage_live`. In the exact accepted reconnect logs, listen readiness satisfies first at 41.855 seconds, the stage-live wait begins next, the client joins and reaches gameplay, then reconnect/commit/fire and both clean exits complete; product remains `7437d77c...`. | T-ENGINE-004 | Retain SP-59's causal prerequisite ordering and forbid fixed sleeps or cache-dependent timeout inflation. |
| B-1096 | Critical | regression_gate | The exact `33C8FDF8...` rerun at `.claude/smoke-verify-runs/results-20260814T082045Z.json` advances through the B-1095 listener barrier, ordinary gameplay, timeout preservation, endpoint-scoped credential retention, reconnect authentication, manifest acceptance, stage replay, post-load `CLC_STAGE_READY`, and room-reservation reclaim. Its first authority snapshot failed opaquely before PREPARE. Source tracing found a disconnect death-drop projectile without its required reverse object link. Current source restores that shared invariant, preserves fail-closed validation, returns the first typed component and subject, rolls the compound packet back atomically, and maps only authority-local stage/state write or send failures to retryable timeout teardown. Frozen product `701931d8...` / client `C497DD0F...` and verification `00ec5fb4...` / tests `61FAA307...` pass the isolated build, focused 586/12, full 61,386/1,162, and native-source guard. The next ordinary receipt at `.claude/smoke-verify-runs/results-20260814T090751Z.json` is retained but rejected at 55/93: the projectile-owner failure is gone and no false file mismatch occurs; typed rollback identifies `world_prop_state`, prop 4, after 118 attempted bytes. Allocation order maps sync IDs 1-2 to players and 3-4 to their post-start held weapons; disconnect death marks the departing player's original held prop 4 deleting before allocating its replacement drop. Replacement source treats terminal non-regenerating deletion as exact-set absence while preserving regenerating setup transitions, reports omitted terminal counts/first ID on PREPARE, names every included prop-state predicate with compact identity, validates reciprocal dual links, and retires an unannounced death-drop candidate if `objDrop()` cannot commit. Product `b3df3ed0...` / client `F2A25827...` and verification `38c558b2...` / tests `26861DA1...` remained frozen while the isolated all/tests build, focused 623/13, full 61,423/1,163, and native-source guard passed. Exact client `BD0F9DAC...` then passes isolated builds, focused 468/15, full 66,705/1,204, and the native-source guard on unchanged 2,734/410-file freezes. Its strengthened ordinary run at `results-20260826T104217Z.json` passes 101/105: dynamic Cyclone prop 3 is attached, enters `weaponDeleteFromChr` as deleting/non-regenerating before the real timeout, and every wider exact-world/inventory/commit/fire/teardown contract passes. Normal cleanup fully frees prop 3 before PREPARE, producing `terminal_absent=0`; the pristine reconnect stage has no preexisting dynamic counterpart, producing `removed=0` with `exact_set=1`. Luna xhigh confirms the four failed persistence/removal assertions inverted the desired lifecycle. Corrected source captures the selected sync ID and requires one read-only authority-pool absence witness. A 34-character event token then exposed and structurally closed a 31-character parser truncation boundary. The parser-safe refreeze passes focused 483/16, full 66,720/1,205, guard, and zero drift on client `5AEC7918...`. Its ordinary receipt `results-20260826T111558Z.json` is retained/rejected at 93/109: exact retirement/absence, pristine world, both inventories, and one commit pass before an `objTickPlayer` null-object fault. Source tracing shows snapshot `SVC_PLAYER_STATS` replays the historical dead bit through live `playerDieByShooter`, which records death, drops inventory, and retires held props after exact restore. The corrected frozen unit now passes isolated builds, focused 718/20, full 66,960/1,209, the native-source guard, and zero product/verifier drift. Exact unchanged client `8262681E...` passes `.claude/smoke-verify-runs/results-20260826T121959Z.json` 116/116 with production retirement/read-only absence, one retryable reconnect, snapshot `live_side_effects=0`, exclusive prop topology, exact world and both inventories, one commit, 30 real Cyclone shots accepted by the authority, final credential retirement, clean exits, and zero failures, operational failures, or leaked processes. | T-ENGINE-004 | Retain exact product `1C184C57...`, verifier `E0FA8E6A...`, client `8262681E...`, accepted focused/full/guard logs, the immutable rejected stale-binary and crash receipts, accepted 116/116 ordinary receipt, and SP-60/SP-72/SP-73 contracts. Revalidate through the broader T-ENGINE-004/release matrix; do not accept null guards, cleanup delays, wire/counter mutation, or stale receiver state. |
| B-1097 | Critical | regression_gate | The retained exact `F2A25827...` ordinary reconnect receipt at `.claude/smoke-verify-runs/results-20260814T094551Z.json` rejected a live Cyclone (`prop=4`, `parent=1`) with `attachment_pair`. Root cause was the per-character clone copying only the `modelnode *` vector while `modelGetPart()` bisects the packed sorted `s16` sidecar immediately after it; equip then published one attachment pointer before the corrupted hand lookup returned null. Current source allocates the modeldef, nodes, pointer vector, and sidecar as one transaction, bounds lookup to `0..numparts-1`, requires the hand node before ownership, and keeps reconnect fail-closed. Frozen product `10eab368...` / client `6E4AC13...` and verifier `628d2c66...` / tests `D839DA85...` pass the isolated all/tests builds, focused 655/14, full 61,455/1,164, native-source guard, and unchanged 2,713/400-file fingerprints. Exact client `BD0F9DAC...` now provides the missing production attachment witness: before retirement, stable client 1's Cyclone prop 3 has the reciprocal player parent, attached character model, and nonnull hand node; `weaponDeleteFromChr` then enters the validated deleting/non-regenerating lifecycle. The same 101/105 run completes exact reconnect world/inventories, one commit, authority fire, and clean teardown; rejection is isolated to B-1096's stale persistence/removal expectations. Corrected source retains that attachment witness and correlates its sync ID to a later read-only full-retirement check; verification is pending. Exact unchanged client `8262681E...` passes `.claude/smoke-verify-runs/results-20260826T121959Z.json` 116/116 with production retirement/read-only absence, one retryable reconnect, snapshot `live_side_effects=0`, exclusive prop topology, exact world and both inventories, one commit, 30 real Cyclone shots accepted by the authority, final credential retirement, clean exits, and zero failures, operational failures, or leaked processes. | T-ENGINE-004 | Retain the reciprocal attachment witness, SP-61 model-clone/equip contracts, accepted 116/116 ordinary reconnect receipt, and exact frozen manifests. Revalidate through the broader T-ENGINE-004/release matrix; never require a fully freed dynamic prop to remain resident or to exist in a freshly loaded client stage. |
| B-1098 | Critical | regression_gate | The exact B-1097 replacement receipt at `.claude/smoke-verify-runs/results-20260814T101348Z.json` crashed before ENet while hiding the valid `base:head_christ` sunglasses toggle because generated DISTANCE/TOGGLE relation targets escaped the private head clone. Current source owns and remaps DISTANCE/TOGGLE/REORDER topology, preflights every indexed record, drops foreign HEADSPOT children, recalculates the private layout, and fails closed. Frozen product `920a236d...` / client `2039d142...` and verifier `96601f67...` / tests `c3987ec6...` pass isolated builds, focused 1,541/16, full 63,731/1,165, and the native-source guard. The unchanged-client ordinary receipt `.claude/smoke-verify-runs/results-20260814T110358Z.json` reaches listener publication, two complete stage loads, exact reconnect world/inventory commit, and clean teardown without the former access violation, proving this crash boundary is repaired; its broader reconnect result remains rejected for separate B-1099. | T-ENGINE-004 | Retain the frozen B-1098 topology contracts and no-crash production path, keep SP-62, and preserve the V-009 character/Needler visual gate in future captures. |
| B-1099 | Critical | regression_gate | Disconnect lobby return left the ordinary client in CI's global `TICKMODE_CUTSCENE=6`, then a validated replay minted the new match latch before asynchronous reset and made the general authority guard preserve that stale prior-stage state forever. The explicit `lvReset` boundary now permits only a client with a validated new-match latch and idle authority tracker to retire prior-stage CUTSCENE through the scoped authoritative setter into GE_FADEIN; the general `HasMatch()` guard remains unchanged. Exact client `0f377e3e...` logs prove `previous_tickmode=6 next_tickmode=0 stale_cutscene_retired=1`, two NORMAL gameplay barriers, exact world and both inventories, five real MagSec shots, authority acceptance, scripted exits, and no leaks. Frozen product `7437d77c...` passed isolated builds, focused 1,746/19, full 63,936/1,168, native-source guard, and corrected retained-log verification 98/98. | T-ENGINE-004 | Retain the narrow stage-load boundary, active-authority exclusion, invalid-candidate tests, exact runtime witness, and V-009 visual gate in future gameplay captures. |
| B-1100 | High | regression_gate | The immutable B-1099 replacement receipt remains rejected at 92/95 because it required optional `LOG.WPN.DIAG` instrumentation and forbade correct final credential cleanup across the whole process lifetime. Verification source now requires bounded production `COMBAT: SHOT_FIRED`, orders exactly one `credential_retained=0` teardown after scripted success, and statically rejects both stale forms. The separately hashed offline artifact `.claude/session-builds/v1m1engine004/b1099-retained-log-revalidation.json` passes the exact retained host/client/aggregate logs 98/98; compiled `[b1100]` passes 61 assertions in 1 case. Product fingerprint `7437d77c...` and client `0f377e3e...` are unchanged, verifier is `9f6c126b...`, and no game was rerun. | T-ENGINE-004 | Retain SP-64's product-event and lifecycle-phase rules; never rewrite the original rejected receipt or substitute optional diagnostics for production truth. |
| B-1101 | Critical | regression_gate | A clean exact-client Combat Simulator run reproducibly became CPU-live with no frame/log progress after the first real CMP150 shot. Native live sampling captured 16/16 main-thread instruction pointers inside `modelasmReadFrameData`, with the caller chain through `modelasmIterateThings1`, `modelasm00018680`, `modelSetMatricesWithAnim`, `chrTick`, and `propsTickPlayer`. The legacy reader subtracted frame-data `t6ptr8` from animation-header end `t3ptr8`; when that unrelated span reached zero, `gp` stayed zero and `while (v1 > gp)` could not progress. The correction removes every unbounded production bit-reader entry point; optimized transforms, generic transforms, root-motion `ANIMFIELD_08`, and camera fields all use one bounded descriptor/payload contract. Explicit `bytesperframe`, header, field-width, part-capacity, and zero-byte/no-op checks reject malformed data with typed diagnostics. An allocated animation object with `animnum=0` remains a normal bind-pose state. Isolated source-frozen verification passes focused 2,007 assertions/33 cases, full 64,681/1,181, and the native-source guard; product `9592a63d...` (2,715 files), verifier `c76aadc8...` (401 files), client `A0F46D54...`, and tests `B4D51B51...` remain unchanged with zero manifest mismatches. The first frozen smoke escaped the zero-progress loop and exposed B-1102; after that framing/fallback correction, exact client `133A55C6...` passes the clean two-cycle fixture 56/56 with real CMP150 fire/hit, both results cycles, Play Again, clean exit, zero decoder/crash matches, and no leaked process. | T-ENGINE-004 | Retain the bounded-reader contracts, rejected diagnosis receipts and sampler artifacts, accepted `.claude/smoke-verify-runs/results-20260826T024226Z.json`, unchanged source manifests, and V-009 visual gate in future gameplay captures. |
| B-1102 | Critical | regression_gate | The frozen B-1101 client `A0F46D54...` reached player initialization in the replacement Combat Simulator smoke, then rejected animation 1 frame 0 as `invalid_flags` and crashed in the intended bind-pose fallback. The immutable receipt is `.claude/smoke-verify-runs/results-20260824T124625Z.json` at 16/56 with exit 1 and `ACCESS_VIOLATION`. Root one was schema-v5 `pd_special_parts`: the compiler stored each flags byte beside `header_hex` but copied only the header bytes, so runtime consumed field bytes as discriminants. Root two was fallback ownership: it cleared `model->anim` and called a generic CHRINFO consumer that unconditionally dereferenced it. Extracted flags, exact descriptor bytes, full-u32 frame values, declared part topology, and explicit zero-frame semantics are now parsed fail-closed; producer and runtime share the same exact part/stream measurements; optimized decoding verifies its actual header/bit advancement; malformed v7 animation caches are invalidated by animation-only compiler v8; completed streams reject trailing descriptor bytes before publication; and rejection re-enters the complete matrix core with an explicit null animation input and null-safe bind scale without mutating `model->anim`. Luna xhigh's completed integrated audit additionally found and Sol corrected noncanonical integer/boolean token prefixes, leading-zero raw values, zero-frame part-count collapse, and prefix-only stream validation; behavior tests exercise the production scalar parser rather than only source strings. Frozen product `e6287a9c...` (2,717 files) / client `133A55C6...` and verifier `ad7c38f9...` (402 files) / tests `C9D4511F...` pass isolated builds, focused 2,057/27, full 65,073/1,186, and the native-source guard with zero manifest mismatches. Exact client `133A55C6...` then passes `.claude/smoke-verify-runs/results-20260826T024226Z.json` 56/56 with both cycles, real CMP150 fire/hit, root hold, stats/awards, Play Again, clean exit, zero animation/crash rejection matches, and no leaked process. | T-ENGINE-004 | Retain the exact producer/consumer framing, canonical scalar and zero-topology tests, cache-version boundary, accepted automation/runtime receipts, unchanged source manifests, and V-009 visual gate in future gameplay captures. |
| B-1103 | Critical | regression_gate | After complete wire and identity validation, source detects a valid `CLC_SETTINGS` from the exact preparing participant, aborts and restores the active ready-gate transaction before publishing new lobby settings, then recomputes team policy. The smoke-only trigger uses ordinary `netClientSettingsChanged`. Current protocol-v58 isolated builds, final full tests 66,421/1,200, native-source guard, and frozen manifests pass. Exact-client settings rollback passes 43/43: host logs ready-gate and complete lobby-transaction rollback before settings publication, sends `SVC_MATCH_CANCELLED`, starts no stage, kicks no peer, and both clients exit cleanly. | T-ENGINE-004 | Retain the accepted current-v58 settings-rollback receipt and malformed-packet no-mutation coverage; revalidate only through the broader release matrix. |
| B-1104 | Critical | regression_gate | `lvReset` treats all selected players as one stage transaction, freezes authenticated client-ID order for reset/spawn/semantic roles, and reverse-unwinds the actual commit ledger. The NPC wire path shares one model-complete readiness predicate, bounded transactional appends, and validate-before-apply resync. Exact v57 runtime `results-20260826T055628Z.json` remains rejected after exposing old Carrington NPC state before `SVC_STAGE_START` and asynchronous mutable-state checksums. Protocol v58 replaces the ambiguous wait mask with explicit inactive/waiting/release/active phases, carries one nonzero START/READY epoch, requires separate authority and exact-peer post-load latches, and publishes one complete fresh baseline from dedicated reliable storage. Pending ownership clears only after successful room queue; a canonical sync-ID-sorted digest of exact serialized target/room fields follows the full resync it validates. Current v58 product `03a65922...` / client `5DA75BE6...` passes isolated builds, final full tests 66,421/1,200, native-source guard, and unchanged product manifests. Production receipts pass co-op 96/96, Counter-Op 98/98, later-player rollback 60/60, settings rollback 43/43, initiator authority 214/214, reconnect 99/99, and the focus-independent invitee-authority route 170/170. The immutable route receipt remains rejected at 169/170 solely because its old regex rejected valid epoch 1; corrected static coverage passes 108/2 and separately hashed retained exact-client logs pass 170/170. That proof establishes one invitee-elected in-client ENet listen authority, one separately signed typed match-server route, exactly one initiator join, no probe/relay endpoint handoff, epoch-correct baseline/ACTIVE publication, stable gameplay, and clean exits. The integrated 210/218 fixture remains unchanged and rejected only for B-1085's independent foreground-focus witness. | T-ENGINE-004 | Retain all seven current-v58 receipts, the immutable rejected raw route receipt, corrected retained-log proof, route-type separation, epoch/atomic-baseline contracts, and V-009 visual gate; revalidate through the broader T-ENGINE-004/release matrix. |

These forty-eight are the only live one-off 1.0 bugs. Any newly reproduced release defect is
added here immediately and linked to the owning Workbench milestone.

---

## Release regression clusters

The archived ledger contained many `FIXED-PENDING-PLAYTEST`, `MITIGATED`, and
partially diagnosed rows. They remain evidence, but they are consolidated below
so they do not masquerade as 100-plus independent active tasks.

| Cluster | Historical IDs | Current owner | Required 1.0 evidence |
|---------|----------------|---------------|-----------------------|
| Public-source and graph parity | B-385, B-769, B-772, B-801, B-855, B-935, B-938, B-941 | V-003, V-005, V-010, V-012 | Controlled edits for all 27 public families, deliberate production graph consumers, fail-closed selected source, representative visuals/audio/gameplay, and no ROM/native/loose fallback. |
| Campaign and Combat Simulator lifecycle | B-228b, B-316, B-339 through B-345, B-359, B-360, B-365, B-368 | T-TESTS-002, V-010, V-013 | Full campaign through Credits plus repeated Combat Simulator start, end, return, restart, and stage transitions with no crash, stale manifest, black screen, or missing scene. |
| Bot spawn, attribution, and long-session stability | B-19, B-112, B-126, B-142, B-174, B-204, B-205, B-249 | V-010, V-013 | Multi-map bot spawn and navigation, correct kill credit, 31-bot long-duration stress, bounded stack use, clean crash diagnostics, and no silent process exit. |
| Collision and movement | B-145, B-147, B-206, B-242, B-335, B-339, B-350, B-367 | V-010 | Shared capsule/world collision across floor, wall, ceiling, corner, airborne movement, props, and spawn placement on representative campaign and Combat Simulator stages. |
| Menus, physical input, and vehicles | B-152 through B-154, B-198, B-203, B-209, B-230, B-298, B-317, B-351, B-356, B-361 | V-004, T-VEHICLES-002, V-010 | Real MKB and controller navigation, rebinding, live glyph switching, text focus, pause/end screens, root close/reopen, and complete hoverbike operation without input leakage. |
| Rendering and character presentation | B-18, B-179 through B-184, B-253, B-346, B-362, B-366, B-390, B-395 | V-010, V-013 | Representative stage, character, weapon, sky, text, transparency, material, and first-person captures with exact asset identities and no malformed geometry or stale render state. |
| Save, protocol, distribution, and shutdown | B-267 through B-293, B-327, B-349, B-1041 through B-1053 | V-006, T-RELEASE-005, V-013 | Corrupt/truncated rejection without partial mutation, signed identity and manifest checks, atomic replacement/rollback, exact package lifecycle, clean disconnect, and clean shutdown. |

If a cluster fails, promote the exact reproduced symptom to a new active B-number
instead of reopening every historical row.

---

## Locked recent regression gates

| IDs | Gate | Owner |
|-----|------|-------|
| B-1054, B-1055, B-1056 | Future gameplay captures must show an unobstructed first-person view, a lower-right correctly scaled held weapon, authored GLTF material colour, and readable non-white effect colour. | V-012 and V-013 |
| B-1057, B-1058, B-1059 | Friend play must retain distinct identities, fresh signed authority claims, exactly one listen authority, a separately typed signed match-server route, one idempotent non-authority join, failure rollback, and no probe or relay descriptor promoted to `netStartClient`. | T-RELEASE-005 and V-013 |
| B-1060 | Keep canonical declared room domains authoritative across archive ingress, allocation, portal traversal, and all second-hop consumers. The focused Air Base to Air Force One canary passed 25/25, then the source-frozen 17-mission campaign, live Credits, clean exit, and restart verification passed 23/23 on 2026-08-12. | V-010 and V-013 |
| B-1061, B-1062 | Keep Agent Profile v3 as the sole per-agent campaign and preference authority. Accept only complete v3 or exact v2 plus an optional validated INI migration, prepare before commit, preserve live state on rejection, refuse active deletion, and reject invalid campaign CLI relationships before arming. Final ordinary-client receipts are `results-20260813T030531Z.json`, `results-20260813T030952Z.json`, `results-20260813T031306Z.json`, and `results-20260813T032640Z.json`. | T-TESTS-002, V-010, and V-013 |
| B-1063 | Keep private generated caches under path-checked `$H/mod-cache`, with checked `$S` and `$B` fallbacks, one 128-bit filename key, and the full source SHA-256 in descriptors. The original nested save-root migration smoke passed 24/24 at `results-20260813T030531Z.json` with 594 `$H` cache references and no `$S`, `$B`, or fatal fallback. | T-RUNTIME-001, P-001, V-010, and V-013 |

---

## Post-1.0 bugs

| ID | Status | Route | Reason deferred |
|----|--------|-------|-----------------|
| B-223 | post_1.0 | UI presentation follow-up | Possible compounding of independent full-viewport dim layers has no current release repro. Promote only with a captured failure. |
| B-248-old | post_1.0 | T-MODDING-005 | The Grid free-fly observer gravity issue belongs to deferred Forge/editor work. |
| B-333 | post_1.0 | T-BENCHMARKING-006 | GPU swarm prediction, dedicated-authority alternatives, compression, active-prefix transfer, and reliability are benchmark extensions outside D-004. |

---

## Bug workflow

1. Capture the exact production symptom, build/source fingerprint, log, and
   minimal reproduction.
2. Add the B-number here immediately and link it to one Workbench milestone or
   validation owner.
3. Find the root cause and audit the bug class across sibling call sites. Add a
   systemic pattern to `systemic-bugs.md` when the class can recur.
4. Implement at the shared architectural boundary with explicit ownership,
   lifecycle, failure, and rollback behavior.
5. Run focused tests, the relevant full suite, and an ordinary-client or
   real-peer proof proportional to the defect.
6. Reject evidence if relevant source changed or an exclusive receipt overlapped.
7. On closure, update Workbench evidence/status and remove the row from Active
   1.0 defects during the next dated archive snapshot. Do not grow a second fixed
   history table in this live file.

#### T-ASSETS-050 — public numeric source admission (2026-09-06)

High: asset_runtime integer fields passed strtof directly to s32, admitting fractions and undefined nonfinite/overflow narrowing; glTF _PD_ROOM omitted finite rejection before u32 conversion. Narrow production fixes applied with real source hydration/importer regressions prepared, no build/runtime verdict. Propagation across shared integer consumers addressed by the common helper. Older structural JSON key search, float grammar and string decoding remain separate open source-admission work; no complete metadata-parser claim.

#### T-ASSETS-046 — standard audio duration propagation (2026-09-06)

Medium: psGetDuration60 assumed all public voice bytes were24kbps MP3, yielding incorrect length for mod WAV/Vorbis and other MPEG rates. Root applied actual decoded-frame duration and pre-pointer channel bounds, with independent WAV final-frame and compressed-format/preserved-playback cases prepared. Three static pins and native guard now enforce source decode and ownership ordering. Unbuilt/unrun; no performance or audible claim.

Catalog growth source defect (2026-09-06, pending): s_registerLocked inserts its new unoccupied row before rehashTable, which skips unoccupied rows and loses the successful insertion at the growth threshold. Cached entry->id and metadata loaded_data=self also survive pool realloc incorrectly. Catalog worker owns bounded core/cache repair and real forced-growth probe; no validation yet.

Mod Manager disabled component persistence (T-MENUS-006, source-confirmed): the
writer uses enabled-only assetCatalogIterateByType while its callback writes
!enabled, so disabled selections are omitted. Rebuild also replays preferences
before final package registration can re-enable rows. At2026-09-08 17:45 a private
nine-path candidate supplies complete atomic file merging, actual including-disabled
catalog snapshots, checked replay and later ordering, with IO8/catalog6/family1
cases prepared. Not integrated, compiled or run. Existing Apply core/UI still
ignore complete failure status; no rollback, restart or physical input claim.
See .claude/menu0908/component-state-review/README.md and integration-fragments.json.

Character selector/wire gap (2026-09-06 12:32 ET, source-only, next batch): room template callback rejects empty head binding, excluding35random_gender and2integrated templates despite all63 activation proof. ASSET_CHARACTER unlock default bypasses component eligibility; stage-start client/bot writers/readers reject head_session0; matchConfig defaults replace empty integrated head with dark_combat. Catalog worker maps exact shared policy/consumer correction; no source edit or runtime proof yet.

Batch5 actual body/head parser failure (2026-09-06 12:40 ET): symbolic type table used declaration order with CASS before MAIAN, while native constants are MAIAN3/CASS4. Authored named types selected wrong native type/rig; first C++ gate46pass then fails expected maian_tall_neck with cass_neck. Root fixes symbolic mapping to explicit native constants and expands all-six-type regression; no test expectation relaxation. Source311/C65DF594 unchanged during failure.

Batch5 installed boot failures — 2026-09-06 13:13 ET: ordinary menu smoke on source312/46DAC86C client2E4448B3 times out before boot readiness after pervasive BODYHEAD.PUBLIC_SOURCE candidate nested mesh hash failures. Exact chain: body/head byte preflight succeeds against identical mesh source, then common binder rechecks with NULL bytes through canonical File hashing, which passes the nested :: path to an ordinary file opener. Extraction worker prepares a VFS-aware nested path branch plus actual first-owner/equal/divergent installed probe. A separate actual emitted base_sp_head_21 head.ini says HEADBODYTYPE_CASS although headdata_authored uses HEADBODYTYPE_MAIAN. loader_enum_reverse.c and loader_pool.c deliberately preserved an obsolete swapped numeric table; root owns correcting both to named native constants and proving real emitter-helper to public-parser roundtrip. No fixes applied yet. Generic sp_head21 admission failure is independently being traced. All seven asset probes remain unrun; menu failed before inputs/captures. Prior helper passes and client build are retained, not runtime acceptance.

Body/head unlock source omission (2026-09-06 13:15 ET, source-only next correction): romextract_pdhead/pdbody public INI omit requirefeature; bodyHeadSourceParseIni defaults it to0 and registerBodyHeadPublicIni overwrites catalog manager eligibility originally set from g_MpHeads/g_MpBodies. Thus fresh public admission can erase native unlock requirements. Record for the forthcoming character selection/extractor parity unit; not yet corrected or runtime-proved. The public source must carry actual base eligibility, while explicit author edits remain authoritative.

MP75/native21 correction applied 2026-09-06 13:18 ET: s_BaseHeads now includes MP75 with authored sp_head_21 identity, preserving actual native21, MP ordering and requirement0 while eliminating the synthetic base:head_75 duplicate. The cached SP selector row count reduces by one because the duplicate disappears. Loader enum export/legacy decoding use named native constants; existing body/head source regression now calls the actual extraction name helper for all six type roundtrips. Corrected build/direct runtime proof remain pending. Old cached author-edited base files were not rewritten.

SFX public source domain mismatch (2026-09-06 13:35 ET): fresh source registration rejects208 native SFX archives because catalog_audio_public_source.cpp treats key_min/key_max/velocity_min/velocity_max as ordered MIDI7-bit ranges. Native n_sndplayer.c uses keyMin bits0..4 for volume channel/bits6..7 for chain ID, keyMax flags+FX mix, velocityMin chain low byte, velocityMax delay. Example base:sfx_unlabeled_avrm public values5/0/176/16 are valid packed bytes. Dependent explosion effect adds one activation failure, making boot ASSETCHAIN209. Exact source-domain fix drafted, not applied. Chain IDs encoded numerically and actual chain/delay preservation remain open c3842/runtime follow-up; do not call the admission fix full sound behavior parity.

Batch5 SFX admission runtime result — Sep6 13:49 ET: all1768 SFX and effect dependency now register, source313/9CB05A8E client9C2057D9 unchanged. Installed20260906T174745Z body/head retry stops51.5s,3/9 outer assertions,0/7 cases on one first-pass body failure: sp_body108/model_ceyespy native mesh bridge collides. The second scan says68/68, but first scan67/68 is authoritative failure. Prior install first scan68/68; investigate ordering among legitimate model_ceyespy/_hi/_lo aliases sharing FILE_CEYESPY75 before changing ownership rules. No further gates; current cumulative77 helper cases and16 Python/guard/selftest pass remain separate. Next UV draft review found source-destructive whole-archive regeneration/cache bump and generator-label rejection; draft stays unapplied pending source-preserving design. SFX chain audit uncovered possible native sound1-based vs extractor array0-based source identity mismatch; bounded evidence/correction draft authorized before chain semantics implementation.

Asset runtime boundary Sep6 14:12 ET: body/head native alias retry on314/DAB3EDD8/client72FBED1D is terminal red100.5s,3/10 assertions,0/8 cases. Full owner scan exposes2 first-pass body hand conflicts (dark_combat and sp_body132 selecting model_combathandslod_hand) plus1 dependent character; second-pass summaries do not erase failures. Inventory of733 mesh rows finds31 aliased native symbols, with one hand group shared by those bodies. Worker owns exact selected hand catalog ID/provider propagation through actual Bondgun queue, UNARMED path and same-filnum cached hand reload. Draft only; no new gate until coherent repair/refreeze. Current five affected source cases/native guard/client builds pass, cumulative80 helper cases retained separately. Standard-source authoring guide now documents current selected audio formats/public descriptor authority, body/head editable sources and explicit pending graph/audio/hand semantics. Source files remain frozen; documentation changes only.

### T-ASSETS-048: later metadata scan replaces admitted body/head sources (2026-09-08)

Critical; correction implemented, installed validation pending. Specialized body/head walkers bind public descriptor-selected geometry and native scalar pools, then the generic metadata-family table registers the same families again and replaces those handles with private manifest container paths. Installed CamSpy diagnostics prove native file75 and dependency are correct but body primary differs from the selected mesh primary. Removed duplicate body/head metadata entries and private field writers; the existing installed walker case now executes the later metadata pass before actual geometry checks. Evidence: batch6-native-alias-diagnostic-exit/result.json and source-batch6-body-head-single-owner.json under .claude/asset0905-continuation. Arena has a related dual-owner design pending its own public-source admission repair; this fix does not close Arena.

T-ASSETS-048 validation update14:27: the duplicate-body/head-owner fix passes the real later-scan plus selected-geometry witness in all9 installed cases,12/12 outer assertions, source347/EFF4FE2C clientBB6F044D. Arena follow-up remains open.
## 2026-09-08 legacy enabled-mod list loses final exact-fit ID (T-MENUS-006)

Source-confirmed: modmgrBuildEnabledList rejects pos+needed equal to2047 even
though g_ModEnabledList has2048 bytes. With32 enabled63-byte IDs and31 commas,
it silently omits the final ID from the fallback config. Ordered JSON is already
complete. Candidate uses a checked complete builder before persistence, retaining
output on failure; direct exact-fit/capacity/session/order/invalid-registry tests
prepared. Applied16:50 after runtime release; four-path hashes in
.claude/menu0908/legacy-list-review/applied-four.json. At17:16 native4 PASS/26
assertions on tests189B1644/full native2093EA1D, zero drift; tests build green.
Native guard PASS exit0/unchanged source; matching client B56D8334 build0 verified
at17:26 with owned hashes unchanged. Installed asset admission red remains separate.
Receipt .claude/menu0908/legacy-list4-exit.json.
Propagation: only one builder/caller in modmgr.c; primary JSON has separate
maximum-domain roundtrip coverage. Full Apply failure propagation remains open.
### 2026-09-26 T-MODDING-002 - Equipped graph ammo reference mismatch

Source-confirmed: v2 actions admitted ammo_slot=1 but the equipped adapter
constructed only ammos[0], leaving an accepted action without its ammo record.
The applied correction accepts explicit [slot0, slot1] source records with
null for unused slots, retains the original slot-zero object shorthand, and
validates all action and gate references before publication. Focused archive,
secondary-slot, missing-slot, gate, and rollback tests pass in the 59-case,
1,057-assertion graph cohort. Ordinary graph gameplay integration remains open.
Separate required c3842 static checks remain red on scanner comment/newline
matching, centralized voice registration, and superseded WAV metadata wording;
retain the failures until their current production contracts are tested.
September 27 resolution: function-body inspection, shared voice registration
and format-derived conformance checks pass the combined 130-case/9,749-assertion
cohort; 18 real-archive audio tests and native-source guard pass. Prior red
receipts remain historical; ordinary graph gameplay is still open.

### 2026-09-27 T-MODDING-002 - Idle admission and deferred shot ownership

Source-confirmed integration gaps, not a reported shipped-v1 regression:
the prepared v2 weapon's NULL function slots make native function counting and
idle ammo checks reject before graph admission. Separately, native attack
debits ammo/sets firing before handsTickAttack performs hit processing; selected
action lifetime must cover that delayed delivery and all gset snapshots.
Propagation audit found four gsetPopulateFromCurrentPlayer calls across noise,
hit calculation and attack dispatch. Full source map and mandatory production
proof are in context/designs/modding/held-graph-production-seams-2026-09-27.md.
September 27 ownership repair applied: selected gset copies now retain the
complete source entry, preventing premature equipped/model/command release.
Complete captured-archive mode preparation/publication also rejects partial
replacement and fences removed secondary modes against stale candidates.
Builds, 108 cases/4,018 assertions and native-source guard pass. Idle admission, ordinary deferred-delivery wiring
and native slot lifetime remain open; preserve topology and per-hand identity.

### 2026-09-27 T-RUNTIME-001 - Held model generation lifetime

The ordinary public-source held loader published raw model/texture pointers
into gun, hand and casing models without a separate native lifetime reference.
Catalog replacement could release its row while those pointers or cached
command lists remained stage-owned. Added an idempotent exact-generation stage
pin before native publication; playermgrReset releases pins only after player
roots are invalidated. Source-edit/rejection/retirement harness now requires
old pixels to survive caller retirement, and both repeated pin/release and
last-reference cleanup. Client/tests builds, 109 cases/4,040 assertions,
native-source guard and installed 37/37 source/lifetime harness pass. September
29 recheck confirms zero drift across all 2,011 recorded source files. This protects source
storage, not the separate v2 weapon/model numeric-slot identity contract.

### 2026-09-29 T-RUNTIME-001 - Retained model slot identity

Custom model slot reset and rollback previously overwrote identity regardless
of retained compiled-source users. Model generations now acquire an exact-ID
slot pin on publication and release it at final destruction. Reset removes
catalog reservations while preserving live pins; rollback preflights all live
identities before mutation and never restores operational reference counts.
Native tests cover final release, repeated rollback, reserved vs retained
ownership, conflicting/reordered identity, invalid pins and malformed IDs.
Installed source-generation harness now uses a real custom slot and checks
old/new source retention through slot reset. Client/tests builds, native123/
4,163, dependent body/head8/322, native guard and installed38/38 pass on
2,011 unchanged sources. Retain first header-ordering build red and correction.
Weapon slot lifetime and ordinary executable-graph cutover remain separate.

### 2026-09-29 T-MODDING-002 - Equipment identity and queued model source

Deferred v2 consumers retain equipment, but custom weapon slot reset/rollback
can still rebind its private identity. Equipment also carries only a numeric
model file binding, allowing a later queued load to select a newer same-ID
model. Implementing exact weapon pins and retained-generation queued loading;
explicit idle reload metadata must never project the first graph action.
Client/tests0, native143/4754, guard0, installed39/39 and source2012 drift0.
Exact model queue and weapon pin seams pass; ordinary v2 hand/action cutover
remains incomplete. Receipt: equipment-native-lifetime-2026-09-29.md.
