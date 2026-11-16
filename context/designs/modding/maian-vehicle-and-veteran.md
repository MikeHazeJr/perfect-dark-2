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
    (both vehicle smokes now on CI training: B-1159 blocks Escape; they load the smoke
    agent through Agent Select and close the main menu first, since CI training forces
    Agent Select open until a game file is chosen there).
- V3. Boost and heat: a boost action; ramp to full over about 1 s; after
  about 4 s the crystals turn black, boost stays above base below full,
  steering is limited briefly and the turret is locked out until the crystals
  recover; recharge faster when stopped, scaled by speed. The tunables are
  node variables in a vehicle behavior graph; the schema and runtime are
  CORE's, so MODS asks CORE for `pd.vehicle_graph.v1` and builds against the
  answer.
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

### Veteran

- C1. The Characters tab: character, body and head mods from templates with
  name, gender, head policy and simulant profile, through
  `modAuthorRuntimeSave`.
- C2. Models: original male and female Maian meshes bound to the 14 body
  parts, `HEADBODYTYPE_MAIAN`; looks as textures. A clone of a base Maian mesh
  is only a local stand-in (BYOR: it embeds ROM-derived geometry and must not
  be shared).
- C3. Green blood for Maian-type mod bodies.
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
   skill); the veterans are still open.
3. CORE: a vehicle behavior graph schema for the node variables (asked in the
   week note).
