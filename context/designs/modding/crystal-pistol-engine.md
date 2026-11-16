# Maian crystal pistol: engine support for a fully working mod weapon

Owner: CRYSTAL PISTOL session (`week-pd/crystal-pistol`, worktree `D:\PD2-wt\crystal-pistol`,
build session id `crystal`). Started 2026-11-15 on Mike's instruction: "get the crystal pistol
fully functional in game ... muzzle flash, ammo, and crystals disappearing as they're fired and
regrowing on reload are all required, so implement them". For the mod magazine/ammo format:
"pick a sensible default that fits the .pdweapon format, note it in the week note for CORE, and
keep going. CORE can change it later."

Test mod: `dev-mods/crystal_pistol` (ids `mod_crystal_pistol:*`), built from the asset folder
`D:\Game Dev\PD2-Assets\crystal-pistol` by `source/scripts/build_pdweapon.py`. MODS keeps its own
recipe (`dev-mods/maian_crystal_pistol` on `week-pd/mods`); the keys below are for both.

## Behaviour to reach (Mike, 2026-11-15)

- Primary select-fires single crystals that stick where they land. Three landed crystals near
  each other detonate.
- Secondary charges all loaded crystals (sound, neon glow), then launches each from its own socket
  as a proximity mine. It cannot be cancelled and fires even if the wielder dies.
- Each fired crystal disappears from its socket; reload regrows them in their sockets.
- The charge fans from the sockets; rolling the gun shapes the spread (new roll input).
- The fan is plus or minus 15 degrees along the spine (Mike's pick, 2026-11-15), the default of a
  tunable node param (`launch_fan_deg`).
- 5 crystals on the spine, 1 in the barrel (the asset already has this: Socket_0 barrel, 1-5 spine).

## Format decisions (defaults; CORE may change them)

### Ammo: `behavior/settings.json`, flat keys per ammo slot N (0 or 1)

| Key | Value | Meaning |
|---|---|---|
| `ammoN_clip` | int 1..255 | Defines slot N with this clip size. Absent: the slot has no ammo. |
| `ammoN_reload` | `magazine` (default) or `regrow` | `magazine`: one timed refill of the whole clip. `regrow`: one round per `ammoN_regrow_ticks60`, starts on reload or when the clip runs dry, firing interrupts it once a round is loaded. |
| `ammoN_reserve` | `none` (default) or a native ammo type name (`pistol`, `smg`, `rifle`, `shotgun`, `magnum`, ...) | `none`: no reserve pool, the clip refills for free (type 0, `AMMOFLAG_NORESERVE`, HUD shows the clip only). A type name draws on the player's reserve of that type. |
| `ammoN_regrow_ticks60` | int > 0 | Regrow time per round. Required for `regrow`. |
| `ammoN_seat_part_base` | int | Round j sits on model POSITION part `base + j`. The engine scales that part's matrix: 1 while loaded, 0 when spent, 0..1 while regrowing. |

A function whose `ammo_slot` names a slot with no `ammoN_clip` now fires unlimited (ammoindex -1)
with a warning, instead of firing zero shots (B-1151).

### Charge launch: params on the secondary's function node (v1 `fire.charge_release`)

`charge_time_ticks60`, `charge_sound_catalog_id`, `launch_part_base` (socket seats, 110),
`launch_aim_part_base` (a second POSITION part per socket, seat to tip, 130; the first and last
tips set the fan's line and order), `launch_speed`, `launch_fan_deg` (see below),
`charge_hide_part` / `charge_show_part` (toggles swapped during the charge: normal crystals 120,
charged crystals 121). Count = the loaded rounds of the function's ammo slot. The charge is
committed: owned by a match table, not the hand; it releases at the deadline even if the owner died
(from the last socket transforms seen).

The fan (Mike's pick, 2026-11-15): each round launches from its own seat, and the rounds spread
evenly from `-launch_fan_deg` to `+launch_fan_deg` around the crosshair, along the spine: the line
from the first seat's aim tip to the last's, re-centred on the crosshair, so it rolls with the gun
and keeps the authored order (round 0 at `-launch_fan_deg`). Six rounds at the default:
-15, -9, -3, 3, 9, 15 degrees. `launch_fan_deg` is a number from 0 to 90, default 15; anything else
is refused when the graph registers (SP-76). Without two aimed sockets (a remote player's charge on
a host that has no first-person gun for it) the line is the rolled gun's up; bots, which do not
roll, fan vertically by the same value. The release logs `BONDGUN.CHARGE: fan ... spread=<full
degrees>`.

### Projectile and entity graphs

- `projectile.cluster` (new module): `count` (3), `radius`, `explosion_ref`; same projectile asset
  (weaponnum + gunfunc) and same owner.
- `entity.proxy_trigger` gains `arm_on` = `launch` (old) or `land` (arm when it sticks or comes to
  rest); its `radius` now also applies to bots and NPCs.

### Model parts (the test mod's `.pdmesh`)

0 root; 50 muzzle (`MODELPART_GUN_MUZZLEPOS`); 55 hold; 90 flash toggle under 50 (flash triangles
bound to the muzzle matrix, render class 1); 110..115 crystal seats (POSITION, round j = 110 + j,
0 = barrel); 120 toggle "normal crystals" (one DL, each crystal's triangles bound to its seat's
matrix, vertices seat-local); 121 toggle "charged crystals" (neon copies, same binding); 130..135
aim tips (POSITION, seat + launch direction); an explicit BBOX node.

## Engine facts this relies on (worktree line numbers at 19338dae)

- Held-gun rwdata is `u32 unk0a6c[32]` (`types.h:2235`), unchecked in `modelInit`
  (`bondgun.c:5300`). DL node = 6 words on x64, TOGGLE 1, POSITION/GUNDL/BBOX 0. Layout above = 27.
- Toggles reset to visible every frame (`bondgun.c:9207`), then `bgun0f098030` (9213). Re-apply
  own visibility after 9213. Hidden toggles leave their subtree's matrices unset: never put a seat
  POSITION under a toggle. Edit matrices after `modelUpdateMatrices` (9401) and before render.
- Part matrix: `modelGetPart` + `modelFindNodeMtxIndex(node, 0)` into `mtxallocation`; camera
  space with the 0.1 scale; world = `camGetProjectionMtxF()` x (M x 10). Muzzle read 9566-9589.
- Muzzle flash: parts 90-92 hidden every frame, one shown when `hand->flashon` (9591,
  `bgun0f0a4e44` 8759), needs part 50 and `muzzlez` != 0.
- Vertices are never transformed by the compiler: they live in the frame of the matrix their
  triangle references (`faces.json`/`render.json` `matrix_index`).
- Mod weapons play no gun animations (`WEAPONFLAG_00002000` unset), so `reload_animation` must be
  NULL; the NULL-animation reload is about 56 ticks and ignores the trigger (`bondgun.c:1690-1753`).
- `bgun0f098ca0` (1073) returns -1/2 with no reserve, and -1 with the trigger held auto-switches the
  weapon (1322-1363, 1419-1433). `bgun0f098df8` (1116) caps by reserve.
- `casingeject` must be `CASING_NONE` (-1); ammo type must be 0..0x20 (`g_AmmoTypes[33]`).
- Fired projectiles: `bgunCreateFiredProjectile` (5950); thrown: `bgunCreateThrownProjectile`
  (5649) / `bgunCreateThrownProjectile2` (5516, used by bots, no net guard). Owner =
  `hidden >> 28`. Stick decision 8418-8538, `objLand` 4980 (deletes a crystal stuck to an
  off-screen chr at 5065), attach hook after 8810, rest hook 9317. `g_Proxies` registry at
  propobj 115; `coordTriggerProxies` uses a fixed 250 (20636).
- Death: `bgunHandlePlayerDead` (7612) resets the hands; a pending launch must live in a match
  table like `g_PlayersDetonatingMines` (propobj 145/163), reset in `setup.c` 356-361 and 510.
- Gun roll: insert `Rz(roll)` into `sp234` after the gangsta rotation (9220-9224), as
  `bgunUpdateGangsta` (8073) does.
- Input: actions in `port/include/actionmap.h` (ACTION_COUNT 121); names `s_ActionNames`
  (`actionmap.cpp:310`) were missing for 105-120 (B-1184); defaults 2974-3124; menu rows
  `pdgui_menu_mainmenu.cpp` `s_BindableActions`; smoke names `s_SmokeActionTable` (3668).

## Status (2026-11-15)

Landed on `week-pd/crystal-pistol` (6e43ca22 and later) and merged into dev on 2026-11-16, everything behind a mod-weapon check:

| Piece | Code | Proven in game |
|---|---|---|
| Mod magazine, self-loading ammo, HUD clip gauge | `loader_pool.c` adapter, `bondgun.c` hooks | `crystal_pistol_ammo_smoke` 31/31 (again on fa85ec7d's client, and on the dev merge build 361abab8) |
| Regrow reload, interrupted by a shot | `bondgun_mod.c` `bgunModTickIncReloadRegrow` | same run |
| Crystals leave their seats and regrow in place | `bgunModApplyHeldModel` (seat scaling) | same run, screenshots |
| Three landed crystals detonate | `bgunModProjectileLanded`, `propobj.c` stick and rest hooks | walls: ammo 31/31; floor: `crystal_pistol_cluster_smoke` 19/19 (six crystals at y -763, two clusters; needed B-1187) |
| Crystals stuck in a character count toward a cluster | landed position of the topmost parent | `crystal_pistol_bot_embed_smoke` 18/18 after B-1212 (failed every run before): `CHRTEST result=hit`, three crystals `embedded=1`, then `cluster detonated ... count=3` |
| Committed charge, neon look, per-socket mines, release after death | `bgunModTickIncAttackCharge`, `bgunModChargeTick` | charge 24/24: six fanned launches, launch after `kill_player`; 25/25 on 6f31fb70 with Mike's fan: both volleys `spread=30` (plus or minus 15) along the upright spine (`vertical=1.00`) |
| Mines stick and arm where they land | mine `.pdprojectile` (sticky, scale 0.15) + `arm_on land` | walls: charge 24/24; floor: `crystal_pistol_mine_bot_smoke` 22/22 (needed B-1187, B-1188) |
| Mines trigger on enemies only (radius 200) | `bgunModProxyDecision` in `coordTriggerProxiesFor` | mine_bot 22/22: the bot (`by=1`) sets them off; mines still in the air go off with the blast |
| Gun roll input, fan follows the roll | `actionmap.*`, `bondmove.c`, `bgunModRollTick`, pose in `bondgun.c` | roll 17/17: 90 degrees, fan vertical 1.00 upright, 0.00 rolled; screenshots: rolled about the bore the gun stays at the lower right, on its side |
| Muzzle flash | model toggle 90 under part 50 (engine unchanged) | viewmodel 31/31: `BONDGUN.FLASH` on all three shots |
| Spent crystals and mines are not pickups | `bgunModGsetFiresSpentRounds`, `weaponCreateProjectileFromGset` | mine_bot 22/22 (before B-1188 the bot collected the resting mines) |
| Mod weapon name | `loaderPoolSetPublicWeaponName`, `bgunGetName` | not checked on screen yet |
| Shots, splats, spawn checks stop at walls on scenario stages (B-1189); Farsight x-ray still shoots through | `bgTestHitScenarioMesh` (`bg.c`), `meshraygrid.c` | `hitscan_bg_scenario_smoke` 14/14 (three Falcon 2 shots `HIT_BG`, none `MISS`, sparks and bullet holes); `farsight_xray_wall_smoke` 14/14 (from the hip `HIT_BG`; aimed in x-ray the next shot goes through the wall) |
| Bots use mod weapons (B-1211): derived bot preferences, self-loading magazine always full to a bot, kept across a function switch | `s_publicWeaponBotPrefsBuild` (`loader_pool.c`), `botact.c`, `botinv.c` | `crystal_pistol_bot_charge_smoke`: the bot fights with the pistol, primary shots paced by the recovery time (magazine 6 to 0 over about a second), regrows in about 4.4 s; the Needler with 12 armed bots 21/21 on base 4 (they fire it, no crash) |
| Bot charge: a full magazine at 150..1500 units commits every crystal, a vertical fan of mines at the target (plus or minus `launch_fan_deg`), alive or dead; then a primary round before the next | `bgunModBotChooseFunction`, `bgunModBotChargeShot`, `bgunModBotChargeTick` (`lv.c`) | `crystal_pistol_bot_charge_smoke` on fe847f51: `choose ... dist=232 charge=1`, six committed, `fan ... spread=30 bot=0`, six launched, the mines arm and kill the player (fixture corrected: its first volley can kill the target) |
| Third person: spent crystals from the owner's clip, charge glow, roll about the barrel | `bgunModHeldRoll3P`, `bgunModApplyHeldModel3P` (`chr.c` `chr0f022214`) | `BONDGUN.MODEL3P` on a bot's gun: 6/6 to 0/6 as it fires, back to 6/6 when it regrows, `charging=1` during its charge; screenshots show the crystals on its gun |
| Online: roll actions on the wire (v63 `UCMD_GUNROLL_LEFT/RIGHT`), every peer rolls and charges every player's gun; the host launches a remote player's volley from its sockets with that player's roll (a socket-less fan only when it has none) | `bondmove.c`, `net_player_move_wire.c`, `bgunModChargeRelease` | `crystal_pistol_net_roll_charge_smoke` 30/32 (both misses were fixture expectations, corrected): each peer logs `roll=90` for its own player and the other; the host launches the client's volley from its sockets with its roll and arms its mines; the corrected fixture 34/34 on base 4 |
| Crystals stuck in a character (target held 240 units ahead, bots hold fire) | native `objEmbed`; diagnostic `BONDGUN.PROJECTILE.CHRTEST` | `crystal_pistol_bot_embed_smoke` 18/18 after B-1212 (failed every run before): `CHRTEST result=hit`, three crystals `embedded=1`, then `cluster detonated ... count=3` |

Rerun on b922e164 with shared base 4 (2026-11-15, 22:46 to 23:44 UTC), all pass: roll 17/17, online
roll and charge 34/34, mines with bots 24/24, the Needler with 12 armed bots 21/21, ammo 31/31,
cluster 19/19, viewmodel 31/31 (after c16d3297 held its taps 600 ms: a 150 ms tap inside a stall
fired nothing).

Not done (follow-ups): a crystal that hits an off-screen character is deleted (native embed rule:
`objEmbed` needs that character's model matrices, built only when it is drawn). If a host marks
characters on screen from its own camera only (`chrTick` uses `func0f08e8ac` against the current
view; not yet checked for remote players), remote players' crystals that hit a character the host
is not looking at vanish instead of sticking. Unverified; a networked embed smoke would show it.
Bugs on the way: B-1184 to B-1189, B-1210 to B-1212 (`context/bugs.md`). B-1212 is engine-wide: no
triangle of a generated model could be hit on PC (`Gtri` lacked its 64-bit pad).

Smoke harness notes (2026-11-15): a bot held by `--debug-place-bot-near-player` floats at eye height
in its fall pose and is re-relocated every frame. Crystals passed through it, and through any
generated-model character, because of B-1212 (`BONDGUN.PROJECTILE.CHRTEST`, `CHRMODEL` and `CHRWALK`
found it); with the fix they stick. Proximity
still works: the mine-on-bot smoke uses it. A released bot moves and knocks the player around. Since
B-1211 bots fight with mod weapons: target-dummy smokes pass `--debug-bots-hold-fire`. Aim with `--debug-spawn-at` plus `--debug-force-first-person-look`
(fixed in B-1186). Stalls of one to two seconds (the session's first GL capture, the first crystal
spawn, the first explosion, and whole-process freezes on the shared HDD) land every input inside them
in one frame: keep presses about 2 s clear of them, hold each about 600 ms, and rerun once before
suspecting the game.
