# Maian vehicle and Maian veteran: approved concepts and plan (week of 2026-11-15)

Owner: MODS week session, branch `week-pd/mods`. Mike approved the concepts on
2026-11-15: "Concepts approved. Build with the in-game mod tools, commit and
push as you go, no merges to dev." This note holds his brief, what exists in the
engine, and the plan. Everything below is marked READ (seen in source), RUN
(proven by a named run) or OPEN.

## 1. Mike's brief (2026-11-15, his words, lightly condensed)

### Weapon

The crystal pistol session is done (`week-pd/crystal-pistol`, draft PR #6).
Use its output as the weapon mod (`mod_crystal_pistol:crystal_pistol`; see
`maian-crystal-pistol-handoff.md` section 11).

### Vehicle

Maian two-seat hover vehicle: a driver plus a gunner on a rotating turret. The
gunner sits in an armoured cowl and is hittable only through a front opening of
about 70 degrees.

- Boost ramps to full over about 1 s. After about 4 s (a node variable) the
  crystals turn black: boost continues above base but below full speed,
  steering is briefly limited, and the turret cannot fire until the crystals
  are their normal colour again. Recharge is faster when stopped and scales
  with speed.
- Turret: 18 crystals, larger versions of the pistol's, each regrowing per
  shot. Full regrow = fire interval x 18, plus about 10 % scaled by speed; at
  10 % of max speed regrow keeps pace with steady fire, and faster driving
  causes a delay. Normal driving lowers the fire rate, normalised to current
  versus max non-boost speed.
- Lock-on: vehicles only. Shots track until they hit, the gunner releases
  tracking/zoom, or boost ramps. The boost curve weakens tracking; at full
  boost the turret cannot fire. A shot that loses tracking keeps its heading.
- Intent: firing while stopped is strongest, so the crew become sitting ducks.

### Character

Maian veteran: no traits; normal hitbox, speed and health. An older Skedar-war
veteran: weathered grey skin, scars, a worn harness with crystal inlays, a
holstered crystal pistol, three crystal chevrons on one pauldron. Male and
female variants on the Maian rig. Reuse the Maian animations; add driver and
gunner seats and get in/out; a calmer idle if easy. The simulant prefers Maian
weapons, holds cover and engages at range. A single-player ally can drive or
gun.

### Level

Waits for Forge mode. Mike, 2026-11-15: "Regarding levels, we need to finish
authoring the Forge mode first." The Maian arena is not part of this plan; it
is a later item that depends on Forge mode authoring being finished. Keep
going on the vehicle, the veteran and the crystal pistol.

## 2. What exists (survey, 2026-11-15)

READ, by a read-only survey of `week-pd/mods` at 27dc9204 (file:line as it
reported them; not re-read line by line).

### 2a. Vehicles

- `.pdvehicle`: `vehicle.ini` (`catalog_id`, `name`, `model_file`,
  `physics_file`, `behavior_graph`; `assetcatalog_scanner.c` 2734-2752),
  `physics.json` (`pd2.vehicle.physics.v2`: the 24 hoverbike tunables plus
  `hover[13]`, all required, `asset_runtime.c` 1202-1302; the conformance tool
  requires `archetype = hoverbike`), and `behavior.graph.json`
  (`pd2.vehicle.behavior.v2`), which is not a graph: three booleans,
  `allow_mount`, `allow_drive`, `allow_dismount`. The validator already allows
  `dependencies/assets/weapons/*.pdweapon` in a vehicle; nothing loads it.
- No vehicle graph schema or runtime exists. The graph schemas are weapon,
  projectile, entity, effect and prop (`weapon_graph_runtime.c` 446-460); the
  only runtime that runs a graph every tick is `prop_graph_runtime.c` 214-303.
- Runtime binding: only `base:vehicle_hoverbike` is bound (to MODEL_HOVBIKE,
  `assetcatalog_base_extended.c` 318, 1177). Scanned mod vehicles keep
  `runtime_index` -1, so no mod vehicle can be placed or driven. Scenario
  objects name a model id, not a vehicle id; Forge has no vehicles.
- Seats: driver only (`propobj.c` 18108-18346 mount and dismount from the
  sides; eye offset fixed in `bondbike.c` 295-302). `SVC_PROP_VEHICLE_STATE`
  has no seat field (`netmsg.h` 28-50).
- Input: accelerate, brake, steer, exit, look, handbrake, use; no boost and no
  fire action. `LAYER_VEHICLE_TURRET` exists with no actions
  (`inputlayer.c` 290-301); `SCENE_VEHICLE_KIND_TURRET` exists.
- Camera: first person only. Damage: the bike is an ordinary damageable object
  and the rider is hit as a normal player who keeps a hand weapon.
- Turrets and homing: the autogun (yaw/pitch, tracks characters only) and the
  hovercopter gun are references. `projectile.homing` steering works on any
  prop; the target is the sight's tracked prop at launch; the homing keys
  `target_filter`, `target_source`, `retarget_policy`, `lost_target_behavior`
  are parsed and unused; `gate.target_lock` is a name only.
- Boost: only the original hold-full-throttle bonus (`propobj.c` 7354-7376).
  Unit trap: its counter runs at 240 Hz although the key is
  `boost_time_ticks60` (2400 is 10 s, not 40).
- No Vehicles tool in the Modding Hub (vehicles appear only in the INI
  Editor); `asset_mod_utility_contract.c` 36 points vehicles at a "Props" tool
  that does not exist.
- Design notes that plan vehicles: `asset-archive-clean-formats.md` (hull,
  seats, cameras, hardpoints, damage states, mounted weapons), the September
  goal closure plan (a hover vehicle and a turret vehicle), and T-VEHICLES-002
  (full hoverbike operation, missing).
- Mike's October work (read only, `.claude/pd-maian-models-20261013/`): a
  Maian tandem hover vehicle model study (`vehicle/builder_v5.py`, driver
  saddle, gunner on a mast with a crystal turret, a cool/hot crystal colour)
  and `vehicle-policy-v2/` (a standalone, never integrated motion and turret
  policy: gunner fire slows the vehicle with a smooth envelope, boost breaks
  lock and refuses new shots, a lost shot keeps its heading).

| Requirement | State | Where the work goes |
|---|---|---|
| Place and drive a mod vehicle | missing | a model slot and runtime index for mod vehicles, placement by vehicle id |
| Two seats | missing | a seat table in the format; occupants beside `struct hoverbikeobj`; mount and dismount per seat; a seat field on the wire |
| Turret aim and fire | missing | turret actions on `LAYER_VEHICLE_TURRET`; yaw and pitch parts; fire the vehicle's `.pdweapon` |
| Boost and heat | partial (hold bonus only) | a boost action; ramp, heat, steering limit and fire lockout in the vehicle movement |
| Crystal looks (normal, black) | missing | object part visibility, as the pistol's seat parts |
| Lock-on at vehicles only | partial (steering works) | a vehicle-only target filter; track until hit, release or boost; a lost shot keeps its heading |
| Gunner cowl hit only from the front | missing | an angle test against the vehicle's forward vector where the gunner is hit |
| AI drives or guns | missing | no bot code touches vehicles |
| Vehicle behavior graph with node variables | missing | a new vehicle graph schema (CORE owns schemas), runtime modelled on the prop graph runtime |

### 2b. Characters and simulants

- `.pdcharacter`: `character.ini` (`body_asset`, `head_asset`, `body_archive`,
  `head_archive`, `head_policy` fixed/random_gender/integrated,
  `display_name`, `portrait_file`) plus the embedded body and head
  (`character_head_policy.c` 153-191). `.pdbody` / `.pdhead`: `type`,
  `ismale`, `height`, `canvaryheight`, `scale`, `animscale`, `model_scale`,
  `rig_class`, mesh and hand (`body_head_source.c` 109-178); 32 private slots
  each for mod bodies and heads.
- Base Maian content: bodies elvis1, theking, elviswaistcoat, maian_soldier;
  heads head_elvis, head_elvis_gogs, head_maian_s; four characters. All male,
  `HEADBODYTYPE_MAIAN`, height 106, animscale 0.5727699, the Elvis hand. There
  is no female Maian.
- The Maian rig is the human skeleton (`skeleton_symbol = SKEL_CHR`, bodies
  compile against g_SkelChr). A body is Maian through
  `type = HEADBODYTYPE_MAIAN` (rig class maian_tall_neck, head offset,
  animscale). Skinning is rigid: every vertex corner follows one of the body's
  matrices. A body mesh needs `model.nodes.json` with the CHRINFO root, the 14
  POSITION nodes the clips drive (parts 1-14, 19 matrices, HEADSPOT as part 4),
  BBOX and DL nodes, and `faces.json` with a matrix per corner. glTF skins are
  ignored.
- Animations: a new body is RACE_HUMAN (race comes from stock body numbers),
  so it plays the human clips; the idle is fixed per race; the walk follows
  `ismale`. Maian-specific clips are cutscenes only. Riding poses exist for
  players (`ANIM_ONBIKE`); there are no mount or dismount clips (mounting snaps
  the rider on). A mod clip gets a private number and cannot replace a base
  clip. Green blood is tied to stock body numbers (`chr.c` 3718-3734): a new
  Maian body would bleed red.
- Simulants: `.pdbotprofile` carries only `type_key`, `difficulty_key`,
  `target_body`, `requirefeature`; no character links to a profile; saved bot
  variant accuracy, reaction and aggression are never applied. Engagement
  range comes from each weapon's `bot_pref`, and a weapon without `bot_pref` is
  never picked by a bot, so today no simulant would use the crystal pistol.
  Multiplayer bots never take cover (cover is single-player AI only).
- Single-player allies: co-op buddies have hard-coded bodies (the Alien cheat
  buddy is already a Maian), ignore cover and follow the player. No AI drives
  or guns a vehicle; the hoverbike is bound to the current player.
- Tools: no Characters tool. The Skin Editor saves a loose texture and
  `skin.ini` with no `mod.json` and no typed archive, and in game a skin only
  tints the body. `modAuthorSave` already saves character, body, head and
  vehicle mods from templates.
- Mike's October veteran study (read only) is a human "Veteran Sergeant" (cap,
  moustache, cigar, camouflage), no armature, not on the Maian rig. The
  October vehicle policy source (`maian_vehicle_mod.*`) is not on this branch
  (it is on `week-pd/inflight-20261114` and in the canonical checkout).

| Requirement | State | Where the work goes |
|---|---|---|
| Male and female veteran on the Maian rig | partial (pipeline and male templates) | new `.pdbody`/`.pdhead`: an original mesh bound to the 14 body parts, `HEADBODYTYPE_MAIAN`, `ismale` 1 and 0 |
| Looks (grey skin, scars, harness, holster, chevrons) | partial | `.pdtexture` dependencies named by the mesh materials |
| Maian animations | exists (human clips, Maian scale) | green blood for Maian-type mod bodies |
| Seats and get in/out | missing | new clips and seat code |
| Calmer idle | missing | a per-body idle choice |
| Simulant: Maian weapons, cover, range | partial | profile fields used by weapon scoring and distance modes; a `bot_pref` for the crystal pistol; cover for bots |
| Ally drives or guns | missing | AI vehicle control and the turret seat |
| In-game tool | missing | a Characters tab on `modAuthorRuntimeSave` |

## 3. Plan

Each step lands with tests and, where it changes the game, a game run;
commit and push per step; no merges to dev. Each mod is made by the in-game
mod author from a template plus edits (a recipe and `--mod-author` for runs,
a Modding Hub tab for players), and the tools grow with the mods.

### Vehicle (first: everything else on it depends on step V1)

- V1. Mod vehicles in the game: a private model slot and runtime index for a
  mod vehicle (as mod weapon meshes get), so the hoverbike code finds its
  physics by model; `--debug-swap-vehicle <mod vehicle>` puts it in place of a
  stage's hoverbikes (CI training, Crash Site, Infiltration, Escape) until
  levels can place vehicles. Proof: drive it in CI training.
  - BUILT 2026-11-15 (804dd3a2, ac535d95, cb07beb7). The scanner's
    `registerVehicleModel` binds a mod vehicle's embedded `.pdmesh` through
    the shared mesh binder, forced private (`meshSourcePreparePrivate`: a
    copy of the base hoverbike still names FILE_PHOVBIKE, which the base
    model owns), and the vehicle row takes that model number; both scan
    paths, in a scan transaction (`PDVEHICLE.MODEL`, `PDVEHICLE.MODEL.REJECT`).
    Base vehicles keep MODEL_HOVBIKE; a vehicle whose model is a loose glTF
    stays unplaceable. `--debug-swap-vehicle <id>` (`vehicle_debug.c`) gives
    each hoverbike a stage creates that model and loads the vehicle for the
    stage. Found on the way, B-1157: a custom model slot has no world scale
    (objects made from one get scale 0), so the swap gives the mod vehicle
    the replaced hoverbike's slot scale (409/4096); a declared scale is still
    needed for levels. Proof is on Escape, the stage `vehicle_flow` already
    drives: `mod_vehicle_drive_smoke` (the base hoverbike saved as a
    local-only mod). RUN: unit cases `[vehicle-model-slot]`, full pd-tests
    1917 cases at ac535d95; the smoke is queued.
  - PROVEN in game 2026-11-16 09:24 UTC on shared base 7 (client e23f6ea7 with 22a2126b):
    `mod_vehicle_drive_smoke` 32/32 (the base hoverbike saved as a local-only mod, put in
    place by the swap, mounted, driven and steered on Escape).
- V2. Stand-in model: run Mike's October vehicle builder headless in Blender
  (read only, export into this worktree), convert to the native mesh with
  named parts for the turret yaw and pitch pivots, the 18 turret crystal
  seats and the boost crystals. The final model is art (see open questions).
  - FIRST CUT 2026-11-15 (7aa6d8c2): `dev-mods/maian_hover_vehicle/`. The
    in-game mod author builds `mod:vehicle_maian_hover` from original parts
    only: the example vehicle (`tri_vehicle`, its hoverbike physics for now)
    plus a typed mesh on the example mesh (`tri_mesh`) whose members are a
    stand-in generated from the October study (`blender_dump_vehicle.py` in
    a headless Blender, then `make_standin_mesh.py`). V4 is the source: V5
    fails its own rider-clearance check. 3.10 m long, 1.69 m wide, 2.16 m
    tall to the turret; hoverbike model units, native axes, the hoverbike's
    node layout, riders left out. Not yet: named turret, seat and crystal
    parts (the study's turret is pistol-sized with six crystals), the cowl.
    Mod tools change made for it: the archive rebuild keeps a manifest's copy
    of a descriptor value in step when an override changes it, so a creator
    can point a vehicle at a different model. RUN: `[maian-vehicle]`,
    `[clone]`, `[author]`, full pd-tests 1919 cases; `maian_vehicle_drive_smoke`
    is queued.
  - Both smokes first crashed on B-1158 (`--debug-mount-bike` ran before Player 0's body
    model existed; fixed in 0503fe0a); rerun queued with `vehicle_flow` as the control.
  - FINAL MODEL in progress (Mike, 2026-11-15: use Tripo). The `pd-game-model` skill
    (`.agents/skills/pd-game-model/`) took Mike's description to concept v10 (approved)
    and a textured Tripo model, 6,220 triangles; sources, renders, prompts and credits in
    `D:\Game Dev\PD2-Assets\maian-hover-vehicle\` (README there). Sharpened edges (a
    chine round the hull, a nose spine) are with Mike at two strengths. Next: Blender
    clean-up (thicker cannon neck, alcove opening, the turret split into a yaw part on
    its ring and a cannon pitching at its neck, decimate to about 4,000 triangles, bake
    128 px textures, crystal pieces in full, regrowing and black states), then the
    `.pdmesh` replaces the stand-in in `dev-mods/maian_hover_vehicle/`.
  - FINAL MODEL IN THE MOD 2026-11-16 (98f9125a, 635ef82e): Mike picked the strong creases; the
    stand-in is replaced by `build_vehicle_mesh.py`'s OBJ mesh (vertex colours, no texture:
    PD images stop at 255 px and a 128 px atlas smeared the vehicle). 3.10 m long, 4,085
    triangles of hull, gunner's pod and cannon plus 19 crystals; parts 1 turret yaw, 2 cannon
    pitch, 50 muzzle, 110-127 ring seats, 128 muzzle seat, toggles 140 (seafoam crystals) and
    141 (black, overheat, inside the seafoam ones). Focused pd-tests pass: [maian-vehicle] 49 assertions, [clone] 1,668, [author] 1,023, [vehicle-model-slot] 56. In-game proof waits on the smoke turn
    (the vehicle smokes run on Escape, which starts in grab mode, B-1159, not a bug, so
    the smokes drop the hover bed with one direct activate, ACTION_VEHICLE_USE, before
    the mount. Correction 2026-11-16 07:04 UTC: the mount hook never fired on CI
    training or Escape because B-1158's first fix waited for Player 0's body model,
    which a solo player outside a cutscene never has; it was not the hangar, the
    hook's walk covers paused props too. Fixed in chrMoveToPos, see B-1158).
  - PROVEN in game 2026-11-16 09:27 UTC on base 7: `maian_vehicle_drive_smoke` 34/34 (the
    final model, built by the in-game mod author, put in place, mounted, driven and
    steered on Escape; screenshots in receipt
    `20261116T092732426616Z-maian_vehicle_drive_smoke-b9176021`). The driver's eye is the
    original's fixed seat offset, so the hull's deck fills the lower half of the view; a
    seat position from the model belongs to V4's camera per seat.
- V3. Boost and heat: a boost action; ramp to full over about 1 s; after
  about 4 s the crystals turn black, boost stays above base below full,
  steering is limited briefly and the turret is locked out until the crystals
  recover; recharge faster when stopped, scaled by speed. The tunables are
  node variables in a vehicle behavior graph; the schema and runtime are
  CORE's, so MODS asks CORE for `pd.vehicle_graph.v1` and builds against the
  answer. CORE answered 2026-11-15 19:15 UTC: MODS drafts it for CORE's
  review. Draft 2026-11-16: `vehicle-graph-v1.md` (events, gates and
  `og.vehicle.*` modules on the mission graph runner's rules, the base
  hoverbike's graph, the Maian graph and variables, five questions for CORE).
  Boost input DECIDED by Mike 2026-11-16: a new mappable action, defaults
  Shift on keyboard and A on an Xbox controller; the vehicle handbrake's
  controller default moves from A to B (it keeps Space on keyboard).
  WRITTEN 2026-11-16, built and proven with the next build and smoke turn:
  `ACTION_VEHICLE_BOOST` (123 since the merge of dev, whose gun roll actions hold 121 and
  122; saved name `VehicleBoost`, Controls menu row,
  vehicle layer; actions 105-120 got their saved names too, the names
  `week-pd/crystal-pistol` gave them for B-1184). A vehicle whose archive
  has `behavior/variables.json` (`pd.vehicle_variables.v1`, the drafted
  graph's values, read by `asset_runtime.c`) boosts with
  `vehicle_boost_heat.c` instead of the original full-throttle bonus; its
  state is per vehicle (`vehicle_boost_runtime.c`, cleared at stage load in
  `lv.c`); steering is limited after overheating; `cool_crystal_part` (140
  for the Maian vehicle) is hidden while the crystals are black; phase
  changes log `VEHICLE.BOOST:` with `turret_locked=` until the turret (V5)
  reads it. The conformance tool allows and checks the file. A file with a
  bad value refuses the vehicle. Tests: `[vehicle-boost-heat]` (state
  machine, per-vehicle table, archive reader) and
  `tests/test_vehicle_variables_conformance.py`.
  - PROVEN in game 2026-11-16 09:31 UTC on base 7, through its v1 graph (Maian 0.4.0):
    `maian_vehicle_boost_smoke` 46/46. Boost pressed; full 1.0 s later with the turret
    locked; overheated 4.0 s after the press (part 140 hidden, so the black crystals
    show); on release the boost is capped at 0.47; cool 2.3 s later (part 140 shown,
    turret unlocked). Not shown by that run: the driver's view does not see the crystal
    ring (it is round the gunner's pod, behind the driver), and the log's `speed` is the
    non-boost velocity only, so the speed boost adds was not measured. Each phase line
    now gives `peak_ground` (the fastest the vehicle moved over the ground in the phase
    that ended) and `top` (21 here, the drag-limited top speed without boost).
  - BOOST SPEED MEASURED in game 2026-11-16 10:41 UTC (base 7, client 3aca4167): 14.73 in
    the half second of throttle before boost, 24.93 in the ramp (19 % over the top speed
    of 21, which drag holds without boost), then about 0: on Escape the vehicle meets the
    hangar's back wall about 1.4 s after it starts, so the full phase (and the four
    screenshots) are spent against it. The boost smoke now requires 22.5 or more in the
    ramp or full phase (5f2e4548). A longer straight, for the full phase and screenshots
    in motion, needs a route on Escape or another stage.
- V4. Two seats: driver and gunner, mount and dismount per seat, a camera per
  seat; the seat on the wire is MULTIPLAYER's message.
- V5. Turret: gunner aim, fire and lock/zoom on the turret input layer; yaw
  and pitch parts; fires the vehicle's embedded `.pdweapon`: 18 larger
  crystals that regrow per shot (the crystal pistol's regrow ammo), full
  regrow = interval x 18 plus about 10 % scaled by speed, fire rate lowered by
  normalised non-boost speed; locked out at full boost.
- V6. Lock-on at vehicles only: shots track until they hit, the gunner
  releases, or boost ramps; the boost curve weakens tracking; a shot that
  loses tracking keeps its heading.
- V7. The gunner's armoured cowl: hits on the gunner count only through the
  front opening of about 70 degrees.
- V8. AI: the single-player ally drives or guns (no AI touches vehicles
  today; the largest single step).
- V9. The Vehicles tab in the Modding Hub on `modAuthorRuntimeSave`.

### V4 design (decided by Mike 2026-11-16; the wire is MULTIPLAYER's)

READ, by a read-only survey of `week-pd/mods` at 452ad5bb (file:line as it
reported them). Today a vehicle has one rider:

- The rider lives on the player: `player->hoverbike` (the vehicle prop),
  `bondmovemode == MOVEMODE_BIKE`, `bondvehiclemode` (off, engine start,
  running; dismount only when running), `bondvehicleoffset` (the seat, a fixed
  (0, 80, -50) over the model scale) and the `bondenter*` camera blend.
- The vehicle's only occupancy state is `OBJHFLAG_MOUNTED` in `obj->hidden`,
  which also travels in `SVC_PROP_DAMAGE` and `SVC_PROP_RESYNC`. One rider is
  assumed by the owner loops (`currentPlayerHoverbikeOwner`, `...OwnedByOther`;
  `currentPlayerCanApplyHoverbikeState` refuses a second player), by every
  rider running the full bike tick (physics, engine sound with one shared
  static, perimeter), by the idle tick's skip while mounted and by the death
  path (`bondtankexplode`).
- Camera: the eye is the seat offset through the bike's matrix; yaw is the
  bike's (`vv_theta`) and looking sideways steers.
- Input: `LAYER_VEHICLE_DRIVER` with `g_ImcVehicle`. `LAYER_VEHICLE_TURRET` and
  `SCENE_VEHICLE_KIND_TURRET` exist as stubs (no action set, no IMC); the
  boarding check is the driver's only.
- Wire: `SVC_PROP_VEHICLE_STATE` (0x39, 20 bytes: state, actor client,
  playernum, prop syncid, type, objtype, stage epoch, sequence) has no seat;
  one transition is reserved at a time; mount and exit intents are UCMD bits.
- The vehicle graph already has both seats (`gate.vehicle.seat_free`,
  `og.vehicle.mount_seat` and `dismount_seat` with `"seat"`); the game's host
  refuses the gunner and every caller asks for the driver seat.

Mike's decisions (2026-11-16):

- "The seat you take depends on where you entered from and where you're
  looking."
- "If no one is in the other seat, double-tapping the enter/exit vehicle button
  switches seats instead of exiting."
- Turret control defaults are MODS's to pick, and stay mappable.

Design:

1. Seats belong to the graph. A gunner seat exists only on a vehicle whose v1
   graph mounts one; the base hoverbike keeps one seat. The occupants (a player
   number or none, per seat) live in the graph runtime's per-vehicle record,
   keyed like the instance by the prop and its `lifecycle_generation`, and
   `gate.vehicle.seat_free` reads them.
2. `OBJHFLAG_MOUNTED` keeps meaning "the driver seat is taken", so the physics,
   the idle tick, doors, pushing and the network flag carriage stay as they
   are.
3. Both seats keep `player->hoverbike` and `MOVEMODE_BIKE`; the player gains a
   seat. The driver-only parts branch on it: the physics and its input, the
   engine sound, the perimeter, the dismount momentum and the death explosion.
4. Which seat a mount asks for. Each seat has a mount point on the model: the
   driver's is the original seat offset, the gunner's is the pod (the turret's
   yaw part). The request is for the seat whose mount point the player is
   looking at and standing nearest to: within the mount range, the seat with
   the smaller angle between the player's view and the direction to its mount
   point wins, and distance breaks a near tie. If that seat is taken and the
   other is free and also in range, the request is for the other; otherwise
   it fails as a mount does today. The graph then decides as before
   (`event.vehicle.mount_requested` with the seat, `gate.vehicle.seat_free`,
   `og.vehicle.mount_seat`).
5. Seat switch. The exit button, pressed twice within 0.3 s while the other
   seat is free, fires `event.vehicle.switch_seat_requested`; its
   `og.vehicle.switch_seat` moves the rider to the other seat. One press, with
   no second inside the window, exits as today. So only a vehicle with a free
   second seat waits the 0.3 s before an exit; a one-seat vehicle, or a
   vehicle whose other seat is taken, exits at once. The window is a tuning
   constant beside the input read.
6. The gunner. The gunner's prop follows the pod each frame, with free yaw and
   pitch (V5 makes them the turret's aim), no bike physics and no drive input.
   Boarding the gunner seat pushes `LAYER_VEHICLE_TURRET`
   (`SCENE_VEHICLE_KIND_TURRET`) with its own action set and
   `g_ImcVehicleTurret`. The driver's eye also moves to a seat position read
   from the model, so the Maian hull no longer fills the lower half of the
   driver's view.
7. Turret control defaults, all mappable in the Controls menu under the vehicle
   group: mouse aims, left button fires, right button zooms and locks, F exits
   (twice to switch seat); on a controller the right stick aims, RT fires, LT
   zooms and locks, X exits (twice to switch seat). Fire and zoom do nothing
   until the turret (V5) reads them.
8. Network, MULTIPLAYER's: a seat byte in `SVC_PROP_VEHICLE_STATE` (a wire
   bump), a seat on the mount intent, and the gunner's aim and fire in the move
   message. MODS gives the local prepare, apply and commit a seat parameter so
   the wire can carry it, and does not change the wire.

Build order:

- V4a. Seat records in the graph runtime, the host's `seat_free` for both
  seats, `event.vehicle.switch_seat_requested` and `og.vehicle.switch_seat` in
  the runner and conformance, with pd-tests.
  - WRITTEN 2026-11-16 (0611ac20 on `week-pd/mods-v4`), not compiled yet. As
    built: a switch request names the seat asked for, like a mount;
    `gate.vehicle.seat` tests the seat of any of the three seat requests; one
    event may reach one mount, one dismount and one switch per seat;
    `vehicleGraphProgramEventSeats` tells the game which seats a mount, a
    dismount or a switch can act on (so "has a gunner seat" and "has a seat to
    switch to" are read from the graph); the record keeps one player per seat
    and one seat per player (`vehicleGraphRuntimeSeatOccupant`,
    `SetSeatOccupant`, `SeatOfPlayer`) and keeps the riders when a program is
    freed and compiled again. The driver seat's truth stays `OBJHFLAG_MOUNTED`
    (it covers a remote driver); the gunner seat's is the record, which is the
    call MULTIPLAYER's wire will make for a remote gunner. Tests: a two-seat
    graph in `[vehicle-graph]` and in the conformance cases (10 pass). Schema:
    `vehicle-graph-v1.md`, "Seats".
- V4b. The seat a mount asks for (mount points, view and distance), the
  gunner's ride (follow the pod, free look, no physics), the turret layer and
  its input context, the double-press switch, and the Maian graph's gunner
  nodes.
- V4c. A solo smoke: walk up looking at the cockpit and mount the driver seat,
  press exit twice (turret layer on top, the player at the pod), twice again
  (driver), drive, press once and dismount.

#### V4b step plan (read-only survey at ee3809a9, 2026-11-16)

READ by a planning survey; file:line as it reported them, not re-read line by
line. Nothing here is built.

Where things go:

- The rider's seat is read from the record
  (`vehicleGraphRuntimeSeatOfPlayer`), with no new field on the player. Only
  the gunner is recorded; a rider who is not the recorded gunner is the driver,
  so a one-seat or v2 vehicle makes no record and takes today's path. Helpers
  in `bondbike.c`: `bbikeSeatOfPlayer(playernum)`, `bbikeCurrentSeat()`.
- Pure helpers with unit cases, `port/src/vehicle_seats.c`
  (`[vehicle-seats]`): the view angle to a seat, the seat choice from each
  seat's free, in range, angle and distance, and the exit button's press
  machine (`vehicleExitPressStep`).
- Mount points in `vehiclegraph.c` (`hoverbikeSeatWorldPos`): the driver's is
  the seat offset (0, 80, -50) over the model scale (`bondbike.c:297`); the
  gunner's is model part 1 through `modelNodeGetModelRelativePosition`
  (`model.c:549`), not the render matrices (they exist only for a prop drawn
  that frame). Range is the native check on the mount point
  (`propobj.c:18555`); the view angle is the interact cone's formula
  (`propobj.c:18331`).
- The seat action takes the seat (`hoverbike_seat_action_fn(context, seat)`)
  and also serves the switch callback. The native mount and both apply
  functions keep their text (pinned by `test_vehicle_observer_layer.cpp`); a
  gunner mount is a new function, solo only (`g_NetMode == NETMODE_NONE`)
  until the wire carries a seat.
- `bbikeInit` and `bbikeExitLocal` split into what both seats do and what only
  the driver does (the scene kind, the projectile speed handoff,
  `objFreeEmbedmentOrProjectile`, `OBJHFLAG_MOUNTED`, momentum, the engine
  sounds). The gunner branch of the exit must come before the flag's early
  return (`bondbike.c:145`), and `bbikeCanDismount` and `bbikeTryDismount`
  have the same early return (`:197`, `:227`).
- `bbikeTick` branches to a gunner tick: no physics, engine sound, collision
  move or bike camera; the seat transform with the pod's offset, free yaw at
  the walk rate, the head through `bmoveUpdateHead`. `bbikeApplyMoveData`
  returns early for the gunner after the shared exit read (no drive, no boost).
  The bike's perimeter and the death explosion stay the driver's.
- Exit and switch: the press is read in `bbikeApplyMoveData`
  (`bondbike.c:580`). Per-player state beside `s_VehicleExitIntentPending`,
  stamped with `g_Vars.lvframe60`, window `TICKS(18)` (0.3 s). A switch fires
  `event.vehicle.switch_seat_requested`; its action swaps the driver half of
  the exit or the init, fires the scene's dismount and then board for the new
  kind (board does nothing while a vehicle handle exists, `scene.c:208`) and
  restarts the camera blend.
- Input: `g_LayerVehicleTurret` is a stub today (`inputlayer.c:292`: no action
  set, no context), so every gameplay action would pass, the hand gun included.
  It needs its action set (exit, look, aim axes, pause, and two new actions),
  a `g_ImcVehicleTurret` context registered everywhere the driver's is
  (`actionmap.cpp:923`, `1253`, `2309`, `2923`, `3557`, `3603`), defaults
  (mouse buttons, the triggers, F and X), and Controls menu rows
  (`pdgui_menu_mainmenu.cpp:1862`, `2009`, `2033`). New actions
  `ACTION_TURRET_FIRE` (124) and `ACTION_TURRET_ZOOM` (125), appended.
- Smoke: `--debug-mount-seat <driver|gunner>` beside `--debug-mount-bike`
  (`main.c:3112`), two exit taps 150 ms apart from the harness, a readiness
  condition for the gunner, and `VEHICLE.SEAT:` lines for the choice, each
  mount, each switch and each dismount.

Order: (1) the pure helpers and their cases; (2) the actions, the turret
context, layer and menu rows; (3) `vehiclegraph.c`: the seat-carrying action,
the switch callback, the mount points, the three pins; (4) `bondbike.c` and
`player.c`: the seat helper, gunner init, exit, tick, move data, the dismount
guards; (5) `propobj.c` and `main.c`: the gunner mount, the seat choice, the
debug seat; (6) the press machine and the switch action; (7) the Maian graph's
gunner nodes (the smokes' `nodes=15 exec_edges=11` lines change); (8) the
readiness condition and the V4c smoke.

Status on `week-pd/mods-v4` (2026-11-17):

- Step 1 WRITTEN and compiled in hosted CI (b1b53fe2, 17c12dd4): the seat
  choice and the exit press machine, `port/src/vehicle_seats.c`,
  `[vehicle-seats]`.
- Step 2 WRITTEN, not compiled locally: `ACTION_TURRET_FIRE` (124) and
  `ACTION_TURRET_ZOOM` (125), saved names `TurretFire` and `TurretZoom`; the
  context `g_ImcVehicleTurret` ("vehicle_turret", priority 5, last in
  `s_AllImcs`) with its defaults (left mouse button or right trigger fires,
  right mouse button or left trigger zooms and locks, F or X exits, the right
  stick and the arrow keys look, the mouse aims through the shared aim axes);
  `imcVehicleTurretMount` and `imcVehicleTurretDismount`; the turret layer's
  action set and its push, pop and abort hooks (`inputlayer.c`), so every
  other gameplay action is closed in the gunner's seat; a "Vehicle Turret"
  group of seven rows under the Controls menu's Vehicle tab, reset with it.
  Nothing pushes the turret layer yet (step 4 does), so play is unchanged.

Risks the survey found, to settle before or in step 5:

- The two mount points nearly coincide on the Maian model: pod part 1 is 11.6
  world units from the driver's seat offset, so "where you look" cannot tell
  them apart, and from the debug hook's spot the pod wins on both angle and
  distance. The model needs a driver seat part of its own (in
  `build_vehicle_mesh.py`), placed at the cockpit.
- `objTestForInteract` aims the interact cone at the vehicle's origin
  (`propobj.c:18313`), so looking at a seat far from the centre may not
  interact at all.
- Bindings files: `actionmap_profile.cpp:324` wanted exactly contexts times
  actions rows, so two new actions and a thirteenth context would have made
  every existing `input-bindings.ini` invalid and blocked saving (`:425`).
  SETTLED 2026-11-17: B-1233 (on `week-pd/mods`) reads a complete file from a
  build with fewer actions, and the side branch extends the same rule to a
  build with fewer input contexts (`[upgrade]` cases; not compiled locally
  yet): the bindings are kept, new actions and contexts take their defaults
  and the file is written back complete. The rule holds only while new actions
  are appended to the action list and new contexts to the end of `s_AllImcs`
  (`actionmap.cpp`), so the turret context goes last there, after
  `g_ImcTextInput`.
- A driverless vehicle slides as a projectile in the prop pass
  (`propobj.c:8052`) and excludes only its owner prop from collision, so it may
  hit its own gunner, and the gunner's eye may lag a frame. Unconfirmed in
  play.
- Whether the gunner mount should keep the driver mount's side gate
  (`propobj.c:18594`) is not decided.

### Veteran

- C1. The Characters tab: character, body and head mods from templates with
  name, gender, head policy and simulant profile, through
  `modAuthorRuntimeSave`.
  - SURVEYED 2026-11-16 (read only, `week-pd/mods` at 3c4e3f06; file:line as reported).
    Nothing of the tab exists. The mod author already saves character, body and head
    mods (the kind follows the template's extension; `mods/Characters/<kind>_<slug>/`,
    `mod:<kind>_<slug>`), but no game run has saved one yet, and descriptor values on
    those kinds are untested. The hub's ten tools are parallel hand-kept tables in
    `pdgui_menu_moddinghub.cpp` (the activate switch, child ids, the render chain, the
    descriptions, the 0..9 clamp, LB/RB cycling) plus the main menu's `MODHUB_TOOL_*`
    mirror, which already stops before Weapons. The Weapons tool is the model to follow:
    a catalog browser, "Use as Template", a creator view with inner tabs, a save modal,
    `modAuthorRuntimeSave`, enable and catalog refresh, `WEAPONMOD.SAVE.<STAGE>` lines and
    a `--debug-weapon-tool-create` hook; its name preview and output path are hard-coded
    to weapons.
  - What the tab needs that is missing: an eleventh tool in each table; a name preview
    and save path for any kind; browser and creator state for characters, bodies and
    heads; gender (there is no gender key: `ismale` with `type`, DEFAULT, FEMALE,
    FEMALEGUARD or MAIAN, kept consistent; for a character it lives in its embedded
    body and head, whose member names differ between base and example templates); head
    policy (fixed, random_gender or integrated: switching away from fixed removes the
    embedded head, and integrated needs a complete body); and fixes so the result shows
    in play (the Room's character list skips characters without both a body and a head
    id, mod heads are left out of the random-gender pools, head display names are not
    stored). Simulant profiles are a separate kind the mod author does not make yet (the
    game reads `profile.json`, `pd2.botprofile.v2`), and nothing links a character to a
    profile: bots take a body and a random head. Proof: a
    `--debug-character-tool-create` hook, a recipe fixture and a smoke on
    `MODAUTHOR.SAVE.OK kind=character` with a typed load.
  - Proposed first slice (for Mike): the Characters tool with a browser of characters,
    bodies and heads, "Use as Template", a creator with name and creator for all three
    and gender and height for bodies and heads, the generic save modal, and the debug
    hook and smoke. Head policy, the Room and pool fixes and simulant profiles (C4)
    follow. A character made from a base template embeds base body and head archives,
    so it is local only (BYOR), as the base hoverbike's copy is.
  - BACK END TESTED 2026-11-16 (`[characters-tab]` in `tests/test_mod_author.cpp`, 35
    assertions, pd-tests only): the mod author already does what that first slice
    saves. A body's or head's gender, type and height as descriptor values, with
    `rig_class` removed so the reader derives it from the type (`maian_tall_neck`,
    `human_female_neck_standard`), and a character's gender inside its own embedded
    body through a nested override (`<body archive>::body.ini`, the example id in it
    rewritten to the mod's id like the rest); the game's strict body and character
    readers accept all three, and the character keeps its fixed head.
  - FIRST SLICE WRITTEN 2026-11-16, approved by Mike the same day ("browser, Use as
    Template, name and gender/height editing, save"). Not compiled yet (written on
    `week-pd/mods-v4` while the vehicle landing waited for the game slot).
    - The rules are their own module, `port/src/character_tool.c` (`character_tool.h`),
      with `[characters-tool]` in pd-tests: what the tool lists, how a gender and a
      build become `ismale` and `type` (Human is DEFAULT when male and FEMALE when
      female, Guard is FEMALEGUARD and female, Maian is MAIAN and either, and
      "Template's own" keeps a type the builds cannot name, such as Cassandra's and
      Mr Blonde's rigs), and which descriptor values a save sends: only what the player
      changed, with `rig_class` removed when the type changes. A template saved as it
      is sends nothing. `[characters-tab]` gained the tool's whole path on the example
      body: read the template, set the form, save, read back.
    - The window is `port/fast3d/pdgui_menu_charactertool.cpp` (`pdgui_charactertool.h`):
      a browser with Characters, Bodies and Heads lists (the base game has 63, 68 and
      84), the selected entry's gender, build, height and neck read from its archive by
      the game's own reader, "Use as Template", a creator (name; gender, build and
      height for a body or a head; what the save will be called and what it changes;
      a note that a mod made from a base entry holds the player's own game data and is
      not to be shared), and the save window (creator, display name, Create + Enable).
      Every step logs a `CHARMOD.*` line.
    - The hub: the Characters tool is its eleventh. The hub's parallel tool tables (two
      name lists, the child ids, the footer lines, the count and the index clamp) are
      now one table, `kHubTools`, and the tool bar's buttons narrow to fit.
    - Proof to run: `--debug-character-tool-create` with `-name`, `-gender`, `-build`
      and `-height` (`port/src/main.c`), and `character_tool_create_smoke` (the base A51
      airman body saved as a male Maian body of height 106, then loaded through the
      typed catalog loader in the same launch).
    - Not in this slice: a character's own gender (it lives in its embedded body; the
      back end is tested), head policy, a 3D preview, the Room and random-head-pool
      fixes, simulant profiles (C4).
- C2. Models: original male and female Maian meshes bound to the 14 body
  parts, `HEADBODYTYPE_MAIAN`; looks as textures. A clone of a base Maian mesh
  is only a local stand-in (BYOR: it embeds ROM-derived geometry and must not
  be shared).
- C3. Green blood for Maian-type mod bodies.
  - WRITTEN 2026-11-16: `chrGetBloodColour` (`src/game/chr.c`) keeps the original's
    per-body list (Elvis, The King and Elvis in a waistcoat green; the robots, Mr Blonde
    and the Skedar their own; everyone else red) and then asks `bodyBloodForCatalogBody`
    (`port/src/body_blood.c`): a body outside the `base:` namespace whose type is
    `HEADBODYTYPE_MAIAN` bleeds the same Maian green. Base bodies keep the original's
    colours, so the base Maian soldier still bleeds red as in the original. The type is
    read only for a body the catalog knows (no catalog miss from an unknown slot).
    Tests: `[body-blood]` (the rule, and a source pin on the order in
    `chrGetBloodColour`). In-game proof comes with the first Maian-type mod body (C2).
- C4. Simulant: profile fields for weapon preference (Maian weapons),
  engagement range and cover, used by the bot weapon scoring and distance
  modes; a `bot_pref` for the crystal pistol so bots pick it at all.
- C5. A calmer idle per body; seat poses and get in/out for the vehicle.
- C6. The single-player ally as the vehicle's driver or gunner (with V8).

### Crystal pistol

The weapon mod is the crystal pistol session's (`week-pd/crystal-pistol`).
Continuing it here (its `bot_pref`, charge for bots, the turret's regrowing
crystals) needs that branch's engine work on this branch; that branch also
carries MULTIPLAYER's week. Mike decides how it comes in (open question 1).

### Level

Waits for Forge mode (Mike, 2026-11-15); not in this plan.

### Order and what fits this week

V1, V2, V3 first (a drivable Maian vehicle with boost and heat), then V4 and
V5, C1 and C4 alongside. V6, V7, V8 and C2 depend on answers below and are
unlikely to finish this week; they stay in this plan.

### Open questions

1. Crystal pistol engine work on this branch: merge `week-pd/crystal-pistol`
   (it brings MULTIPLAYER's week too), or have its engine commits published
   as a shared branch without MULTIPLAYER's week, or wait for the week-end
   merge (then the turret and the pistol follow-ups wait too).
2. Art for the final vehicle and the two veterans: Mike's asset session, or
   MODS stand-ins (vehicle from the October study; veterans need original
   meshes, since the October veteran is a human sergeant).
   Vehicle answered 2026-11-15: MODS makes it with Tripo (the `pd-game-model`
   skill). Veterans answered by Mike 2026-11-16: MODS makes both (male and
   female) with Tripo, the same way as the vehicle.
3. CORE: a vehicle behavior graph schema for the node variables (asked in the
   week note). Answered: MODS drafts it; the draft is `vehicle-graph-v1.md`,
   waiting for CORE's review.
