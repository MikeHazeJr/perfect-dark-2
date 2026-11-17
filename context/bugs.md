# Bug Tracker

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
`--savedir`. A fixture whose processes share one install and pass neither
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

B-1152 OPEN, medium, cause READ 2026-11-17 (MODS), not fixed yet: a weapon mod cloned from a base gun embeds a copy of the base mesh that claims the base mesh's file slot (887), so the base mesh was loaded in its place. Still seen on base 7 in the landing turn's `mod_author_weapon_play_smoke` log (`PDWEAPON.NESTED.MESH_SOURCE: owner=mod:weapon_smoke_falcon id=mod:weapon_smoke_falcon__mesh_model_falcon2_hi action=claimed filenum=887`, where the base rows say `action=kept filenum=887`). Cause: the cloned mesh's `_meta/manifest.json` keeps the base mesh's `source_filenum_symbol`; `loaderWalkerMeshSourcePlanManifest` (`port/src/loader_walker_mesh_source_plan.c:298`) resolves it to the base file number for a row that is new (the mod's own id), and the nested weapon registration (`assetcatalog_scanner.c`, the `has_mesh_source` block before the `PDWEAPON.NESTED.MESH_SOURCE` line) binds the mod's archive to that number. A mesh with no symbol takes a private slot there (`assetCatalogResolveModelPrivateSlot`), which is what a mod-owned mesh should get. Fix planned in two places: the mod author's clone drops `source_filenum_symbol` from a mesh manifest it moves out of the `base:` namespace, and the nested registration never lets a mesh of a non-bundled owner take a file number from its manifest (it gets a private slot, logged), so mods saved before the fix stop claiming base slots too. The mod vehicle path already does this (`meshSourcePreparePrivate`).

B-1153 OPEN, low, RUN 2026-11-15 (`maian_crystal_pistol_play_smoke`, `results-20261115T003916Z.json`): a `.pdmesh` that carries only `mesh.ini` and `model.gltf` (the Needler's needle, the crystal placeholder) loads, but the loader first asks for `model.mtl` and `model.nodes.json` and logs `ERROR: fsFileLoad: could not find file` for each, twice. Members the descriptor does not name should not be reported as errors. Not investigated; CORE's loader or the mesh compiler.

B-1154 FIXED by CORE on `week-pd/core` dcee4202 (not merged here yet; a cloned gun needs a base 7 mesh and a rebuilt clone), medium, seen 2026-11-15 in the same rerun's screenshots (MODS): the held Falcon 2 copy is drawn with a blue and white striped surface that does not look like the Falcon's own texture. Possibly the same cause as B-1152 (the cloned mesh and the base mesh sharing a file slot) or a texture reference that is not followed into the mod's namespace. Cause (CORE, 2026-11-16 01:30 UTC): not the clone. The base Falcon 2 shows the same stripes; every reflection-mapped model did (144 of base 6's 733 meshes). The ROM's display list sets the RSP texture scale (`G_TEXTURE`) before each texture load, the mesh exporter skipped it and the compiler used 0xffff, so each reflection repeated 32 times. `model.render.json` schema v4 adds a `texture` row (`scale_s`, `scale_t`) that the exporter writes and the compiler replays; base 7 carries the re-exported meshes.

B-1155 FIXED on dev by the crystal pistol work (landed as 361abab8; CRYSTAL PISTOL's week note of 2026-11-16 05:30 UTC lists it), in this branch since the merge of dev 841650c7; `mod_author_weapon_play_smoke` is rerun on the merged build. Was: OPEN, low, found by Mike's asset session (crystal pistol asset README, "Scale and frame"), not checked by MODS in a run: the Needler dev mod's held model is authored muzzle toward -Z, while native held models are authored muzzle toward +Z and `bondgun.c` turns every held model 180 degrees about Y, so the Needler is drawn backwards. Fix in `tools/build_needler_mod.py` (it negates z when it writes the held mesh), then regenerate the archive. Owner of the Needler: T-MODDING-009.

B-1156 OPEN, medium, RUN 2026-11-15 19:32 UTC (MODS, coordination hub): under Windows PowerShell 5.1 every hub command (`status`, `wait-turn`, `start`, `finish`, `health`) throws "The property 'Count' cannot be found on this object" while a running `game-runtime` item is past twice its ETA. `tools/CodexCoordination/CodexCoordination.ps1` `Test-ResourceBusy` has no pattern for the resource name `game-runtime` (its game pattern is `^(game|smoke|multiplayer|capture|editor)$`), so the `switch` falls to `default { @() }`, which outputs nothing, and `$patterns.Count` on `$null` is an error under `Set-StrictMode -Version 2` in 5.1 (PowerShell 7 gives 0, so `pwsh` callers see nothing). In both versions a dead `game-runtime` runner is never auto-reaped, because the unknown resource reports busy. Seen: the MODS waiter called the hub through 5.1, got this error on every call once CRYSTAL's smoke (alive, slow) passed 2x its ETA, counted the errors as waits and cancelled its own item (requeued in place; MULTIPLAYER, CORE and CRYSTAL call the hub with `pwsh`). Fix for the hub owner: `$patterns = @(switch -Regex ($ResourceName) { ... })` and `game-runtime` in the game pattern. Workaround: call the hub with `pwsh`. Propagation check: the only other `$x = switch` in the PowerShell tools (`devtools/build-headless.ps1` `$targets`) takes a validated value and is never read with `.Count`.

B-1157 OPEN (the vehicle swap part is fixed on week-pd/mods), medium, READ 2026-11-15 (MODS, while checking vehicle step V1 before its smoke, not seen in a run): a custom model slot (`MODEL_CUSTOM_START` to `MODEL_CUSTOM_END`) has no world scale. `g_ModelStates[slot].scale` is never set and stays 0, and `objInit` (`src/game/propobj.c` 2871) sizes every new object by `g_ModelStates[modelnum].scale / 4096`, so an object created with a private-slot model gets model scale 0 (not drawn, no collision size) unless its caller sets one. Forge sets 1.0 after creation (`port/src/forge/forge_runtime.c` 665, 744, 851); `--debug-swap-vehicle` now gives the mod vehicle the replaced hoverbike's slot scale (`src/game/setup.c`, logged as `VEHICLE.SWAP: modelnum N takes the world scale of modelnum 58 (409/4096)`). Not checked: graph projectiles whose mesh has a private slot are created through `func0f08ae0c` -> `objInitWithModelDef` -> `objInit` and then multiplied by the graph's projectile scale (`src/game/bondgun.c` 6184, 6265), which would keep 0; CRYSTAL reported its crystals in game, so where those get their size is the thing to check. General fix still open: a declared world scale for a custom slot (a mesh or owner key, or one default), which levels that place mod vehicles will need as well. Search: `rg -n "g_ModelStates\[[^]]*\]\.scale" src port`.

B-1158 FIXED AGAIN on week-pd/mods (MODS) 2026-11-16, high for smokes, first fix wrong (see the 07:04 UTC update), RUN 2026-11-15 22:34 and 22:38 UTC (`mod_vehicle_drive_smoke`, `maian_vehicle_drive_smoke`, both on base 3, receipts under `C:/Users/mikeh/Perfect-Dark-2/pd2-smoke/mods/receipts/`): `--debug-mount-bike` crashed the client (ACCESS_VIOLATION in `modelSetRootPosition` <- `chrMoveToPos` <- `bootDebugMountBikeTick`, `port/src/main.c`) on the first gameplay frame. The hook moves Player 0 beside the bike with `chrMoveToPos`, which sets the chr's model root, but Player 0's body model is made by the player tick (`playerTickChrBody`, `src/game/player.c`), not at stage load, so on the first frame `chr->model` was NULL. The hook had no frame settle, although `--debug-spawn-at`'s comment says it uses "the same gate --debug-mount-bike uses" (`lvframenum >= 4`). Fix: the hook waits for `lvframenum >= 4` and a non-NULL Player 0 model. Not caused by the vehicle swap: the swapped vehicle was created at the hoverbike's scale and the crash is on the player's chr; `vehicle_flow` (the same hook on the base hoverbike) is rerun as the control. Update 2026-11-16 07:04 UTC (MODS): the fix above was wrong for solo play, and it stopped `--debug-mount-bike` from ever firing on a campaign stage. A solo player outside a cutscene has no body model at all: the normal tick calls `playerRemoveChrBody` every frame (`src/game/player.c`, the TICKMODE_NORMAL branch), which frees it through `chrRemove` (`chr->model = NULL`), so the model wait never ended. Every vehicle smoke since 2026-11-15 22:38 UTC shows the hook armed, the bike created (`VEHICLE.SWAP`), Player 0 in walk mode with control, and no mount, on CI training and on Escape (receipts `20261116T0517*`, `20261116T064*`). My 05:33 UTC week-note reason (CI training's bike is in the hangar, outside the active props) was wrong too: the active list runs on into the paused list (`propActivate`), so the hook's walk sees every listed prop. Real fix: `chrMoveToPos` (`src/game/chraction.c`) skips the model root, the CHRINFO ground and `chrSetLookAngle` when the chr has no model (and no aibot); the player's position is the prop's and its facing is `vv_theta`, both still set. That also makes `--debug-spawn-at` and any `chrMoveToPos` aimed at a solo first-person player safe (the same latent NULL dereference). The hook keeps the `lvframenum >= 4` settle, drops the model wait, and logs once when it is ready but finds no free hoverbike. Proof: the three Escape vehicle smokes, requeued. Proven 2026-11-16 08:48 UTC (turn q_20261116070701327_eb4de76b, client d21bf9f0, base 3; receipts `20261116T0848*`, `20261116T0852*`, `20261116T0856*`): `--debug-mount-bike consumed ... result=MOUNTED` once in each of the three Escape vehicle smokes, no crash. CORE reviewed and approved the `chrMoveToPos` change (week note 07:31 UTC). The three smokes then failed on a step of their own (an on-foot wait right after the hover bed is let go, which the same-frame mount makes impossible); fixed in the fixtures (3617ffc8), not a game bug.

B-1159 CLOSED, not a bug (answered by CORE 2026-11-16 01:20 UTC: Escape starts in grab mode), was high for vehicle smokes, found by MODS 2026-11-16 00:30-00:45 UTC on shared base 3 with the week-pd/mods client at 0503fe0a (receipts under `C:/Users/mikeh/Perfect-Dark-2/pd2-smoke/mods/receipts/20261116T00*`): on Escape (`--launch-mission base:escape`), after the intro is skipped (the smoke's cutscene-skip assist, action 72), Player 0 never returns to MOVEMODE_WALK. The graph's `end_cutscene` runs at 00:48.71 (it calls `playerEndCutscene`, which sets walk mode for every player through `bwalkInit`), control is granted at 00:48.73, and for the next 130 s the smoke facts read `tick:1 normal:1 update:1 control:1 unpaused:1 alive:1 walk:0 bike:0`, so something after the skip takes the mode away again (not instrumented: no log line names the mode). Effect: `offline_gameplay_ready` never holds and `--debug-mount-bike` (which needs walk mode) never fires, so `vehicle_flow`, `mod_vehicle_drive_smoke` and `maian_vehicle_drive_smoke` all time out (9/20, 21/27, 23/29) with no mount. Not caused by the vehicle work: `vehicle_flow` drives the base hoverbike with no mod. `vehicle_flow` last passed on 2026-07-04 as the 10-assertion CI-training version (`--skip-intro`); the 2026-09-22 Escape rewrite (34b0dee3, 20 assertions) has no recorded pass. The window also lost focus at the first gameplay frame (`INPUTCTX: focus LOST`), which only flushes held input and does not touch the move mode. Unknown: whether a player who skips Escape's intro by hand can walk (if not, this is player-facing). Update 2026-11-16 02:17 UTC: on CI training (`--launch-mission 0x26`) the same skip assist ends the CI camera intro and walk mode does come back (`walk:1`), so this looks particular to Escape's intro (or its level script), not to skipping in general. CI training then forces Agent Select open: at 02:17 UTC because a fresh install has no agent, and at 03:58 UTC still, with the smoke agent loaded by `--launch-load-agent`, because CI training opens it until a game file is chosen there (`g_FileState`, `src/game/menutick.c`), then opens the main menu. So the vehicle smokes now stage the smoke agent profile, load it through Agent Select, close the main menu with Back (injected actions, which need no window focus) and only then wait for walk mode and the mount. Answer (CORE, 2026-11-16 01:20 UTC): not a bug. Area 51 Escape starts with the player pushing the hover bed, as the original does: right after the intro the stage's AI list runs `revoke_control`, `chr_grab_object` (the player, tag 0, `base:model_hoverbed`) and `grant_control`, so the move mode is `MOVEMODE_GRAB` and walk mode comes back only when the player lets go of the bed (the native `aiChrGrabObject` does the same). The smokes' `offline_gameplay_ready` wait and the `--debug-mount-bike` hook need walk mode, so on Escape a smoke has to let go of the bed first (Use), or accept grab mode (`bmoveSetMode(MOVEMODE_BIKE)` leaves a grab cleanly), or use a stage that starts in walk mode, as the mod vehicle smokes now do (CI training). `vehicle_flow` (Escape) needs the same change.

B-1230 ANSWERED by CORE 2026-11-16 05:50 UTC (fix: the boost keys move into the vehicle graph with explicit units, physics v3 has none and v2 is refused after the base re-extract; MODS builds the vehicle graph runner on week-pd/mods), was OPEN for CORE (vehicle schema), low (authoring trap, no gameplay change), found by MODS 2026-11-15 in the vehicle survey, row filed 2026-11-16 at CORE's request: `physics.json` (`pd2.vehicle.physics.v2`) key `boost_time_ticks60` is counted in 240 Hz ticks, not 60 Hz. `hoverbikeUpdateMovement` (`src/game/propobj.c`, the full-throttle bonus) adds `lvupdate240` to `bike->maxspeedtime240` and caps it at `TICKS(boost_time_ticks60)` (NTSC: the value itself), and the bonus is `maxspeedtime240 * boost_speed / TICKS(boost_time_ticks60)`. So the base hoverbike's 2400 is a 10 s ramp, not 40 s (corrected 2026-11-16 per CORE: the 1800 first quoted here is the example template's, `template/tri_vehicle.pdvehicle`, with `boost_speed` 7.0; the base holds 5.0 and 2400, checked on shared base 7); an author who reads the name and writes 240 for 4 s gets 1 s. The game's behavior is the original's, only the key's name is wrong. Fix options (CORE's call, question 3 of `context/designs/modding/vehicle-graph-v1.md`): move the boost keys into the vehicle graph with explicit units (`{1800, ticks240}`), or a physics v3 that renames the key to `boost_time_ticks240` and reads v2 as is. Ends with the extractor task in `vehicle-graph-v1.md` (CORE's review 2026-11-16 09:58 UTC, rule 5): the base hoverbike's v1 graph and v3 physics behind a vehicle export version, then v2 refused at runtime and reported by conformance.
B-1231 FIXED on week-pd/mods (MODS) 2026-11-16, low (no shipped vehicle forbids dismounting): the PC vehicle exit key (`ACTION_VEHICLE_EXIT`, F) dismounted without asking the vehicle's behavior. `bbikeApplyMoveData` (`src/game/bondbike.c`) ran its directional dismount with no `assetRuntimeVehicleAllows(..., "dismount")` check, while the activate path (`bbikeHandleActivate`) has one, so a vehicle whose behavior says `allow_dismount: false` could still be left with F on PC. Found while routing both dismounts through the vehicle graph. Fix: the exit key's dismount (`bbikeExitToChosenSide`) runs through `event.vehicle.dismount_requested` for a `pd.vehicle_graph.v1` vehicle (`og.vehicle.dismount_seat`) and only with the permission for a v2 vehicle. Propagation: the activate path checks it; the network receivers apply the authority's committed state (`currentPlayerApplyHoverbikeState`) and rightly do not re-check; no other path dismounts.
B-1232 OPEN (MODS found by CORE's review 2026-11-16 09:58 UTC; the wire is MULTIPLAYER's), medium before the Maian vehicle goes into a networked match: a remote rider's vehicle boost never reaches the host. The move packet has no boost field, and a remote actor skips the input block in `bbikeApplyMoveData` (`src/game/bondbike.c`) that reads `ACTION_VEHICLE_BOOST`, so on a listen host a client's vehicle heats up and hides its crystals on the client while the host's copy stays cool and slower (corrections, and turret lockouts that differ). Older than the vehicle graph commits. The design (`context/designs/modding/vehicle-graph-v1.md`) has the host run the graph and clients predict no heat. Fix options: a boost bit in the move message (the host runs `boost_heat` with the client's input; MODS's preference), with the phase and toggled parts sent in the vehicle state for remote clients; or `boost_heat` on the host only with the phase in the vehicle state. Owner of the wire: MULTIPLAYER.
B-1233 FIXED on week-pd/mods (MODS) 2026-11-17, before it reached dev; high had it landed: every action added to the game made each player's saved bindings file unreadable. READ and measured, not seen in a run: `actionmap_profile::decode` (`port/src/actionmap_profile.cpp`) required exactly contexts times `ACTION_COUNT` rows ("The complete binding file is missing required rows."), `Store::initialize` then blocked the store, so the saved bindings were ignored (defaults in play) and every later save was refused ("The current binding file is invalid. Load a valid profile to replace it."). The real `%APPDATA%\perfectdark\input-bindings.ini` on the test PC, written by a dev build after the crystal pistol landing, has 12 contexts of 123 actions (last `GunRollRight`); `ACTION_VEHICLE_BOOST` makes `ACTION_COUNT` 124, so the first dev build after the vehicle landing would have refused it. Named profile files (`input-profiles/`) had the same fate. The crystal pistol's two actions did not show it only because every file from before them was already cut off at an unnamed action and went through B-1184's rewrite. Fix: a versioned document that lists, for every context, the same leading run of the action list and nothing after it is a complete file from an older build (saved names are positional, new actions are appended): `decode` accepts it, keeps its bindings and leaves the actions it could not know at their defaults, and reports how many it listed; `Store::initialize` applies it and writes it out complete (a failed rewrite leaves the older file, which reads the same way next time); `actionmapLoadBinds` logs `ACTIONMAP: bindings file from an older build (N of M actions per context): bindings kept, new actions take their defaults, file brought up to date`. Any other missing row (a context cut short, a hole, a missing context, contexts that stop at different actions) is refused as before. Tests: four cases tagged `[upgrade]` in `tests/test_actionmap_profile.cpp`. Not covered, and the same class: a NEW input context (V4b's turret context would be the thirteenth) still makes every saved file invalid, and a file from a NEWER build (an action this build does not know) is still refused; both need a decision before V4b adds its context. Pattern for whoever owns the input pillar's records: a persisted table keyed by a list that only grows must read an older complete file, or every release with a new entry discards the player's settings (no SP number taken here; CORE and MULTIPLAYER number theirs on their branches). A limit of any saved complete file, seen in the same real file: a default that moves does not reach it. It keeps `vehicle.VehicleExit=F,JOY1_X,JOY1_A,0` and `vehicle.VehicleHandbrake=SPACE,JOY1_A,0,0` (older defaults), so after the upgrade the new `vehicle.VehicleBoost` default (`LEFT_SHIFT,JOY1_A`) shares controller A with exit and handbrake in that profile until the Vehicle controls are reset to defaults or remapped; a new profile gets exit on X, handbrake on B and boost on A. The upgrade does not unbind or move anything on its own.

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
