# Maian crystal pistol: brief and handoff (week of 2026-11-15)

Status: NOT STARTED as a build. Mike set this as the weapon mod on 2026-11-14
and then ordered the work: finish the mod tools first (the tools plan, the
Weapons creator's root-layout fix, the mod concepts), then build the pistol.
This note exists so a fresh session can pick the pistol up from the repo
alone. Owner: MODS week session, branch `week-pd/mods`.

Everything below is marked READ (seen in source or files), RUN (proven by a
run named here) or OPEN (not known yet).

## 1. Mike's brief (2026-11-14, his words, lightly condensed)

"Our custom weapon mod is the Maian crystal pistol. Please make it the weapon
you build with the mod tools, as a .pdweapon."

- Primary: select-fires single crystals like a pistol. Crystals stick where
  they land. When 3 landed, active crystals are within proximity of each
  other, they detonate.
- Secondary: charges all loaded crystals. A charge sound plays and the
  crystals shift to a brighter neon/fluorescent glow. Once charged, every
  crystal launches out of its own socket along the top spine and becomes a
  proximity mine wherever it lands or sticks. The spread follows the gun's
  orientation, so the player aims and rolls the pistol on its side to shape it
  (vertical fan upright, horizontal fan rolled). The charge can't be
  cancelled; if the wielder dies while charging, the release still happens
  when the charge completes.
- Reload: crystals regrow in their sockets (visible growth), not a magazine
  swap.
- Look: teal organic body, dark blue hooked grip, blue inlay along the side,
  green crystals in a row of sockets on top plus one at the muzzle.
- The model is being built separately in Blender as a parametric script with
  glTF/FBX exports, named socket points (muzzle, one per crystal socket with
  launch direction, grip) and crystal states (full, regrowing, charged,
  empty). It will be handed over with a spec.
- Until then: build the behavior graph and the `.pdweapon` entry against
  placeholder meshes using those socket names, and treat damage, radii,
  charge time and regrow time as tunable parameters.
- Record what the `.pdweapon` layout needs from the model so the handoff
  converts cleanly.
- Rules unchanged: no merges to dev or main without Mike's OK; clean up
  worktrees and caches.

Follow-up the same day: "don't drop what you were already doing for it.
Finish your current work first (the mod tools plan, the weapon creator's
root-layout fix for the .pdweapon validator, and the mod concepts), then do
the crystal pistol. Both need to get done. If your context runs low before
the pistol, write a handoff note in the repo that covers the pistol brief so
a fresh session can pick it up."

## 2. Earlier model work on disk (READ, not in git)

`C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike\.claude\pd-maian-models-20261013\weapon\`
holds the October artist pipeline for this pistol: `builder_v1.py` to
`builder_v6.py` (parametric Blender builders), `facet-plan-v6.md`,
`mechanism-contract-v6.json`, `handoff-v6.json`, `source-freeze-v6.json`.
V6 is "source and design only": never executed in Blender, never rendered,
units uncalibrated, hand fit unqualified. The same folder tree has `veteran`
(a Maian character) and `vehicle` (a Maian vehicle) work; a "three-asset
joint rework plan" is under `deep-rework-plan-20261013/`. Do not edit or
delete anything there; it is Mike's.

Facts from `mechanism-contract-v6.json` worth keeping in step with:

- Five crystal slots, ids `crystal_slot_0` to `crystal_slot_4`, in a row on
  top, front to back. Each has a centre, a base pivot and an axis
  (0.22, 0, 0.975 in artist coordinates): the direction a crystal rises,
  retracts and would launch along.
- Artist axes: forward is -X, up is +Z. Units are uncalibrated.
- Separate parts: five rigid crystals, a static "diamond emitter" at the
  muzzle (the "one at the muzzle" in Mike's look), two side lenses (the blue
  inlay), a formed hook grip; one connected casing.
- Proposed datums: `muzzle_axis` (-1, 0, 0), `emitter_tip`, `grip_mount`,
  `reload_order` (slot 0 to 4).
- Crystal motion is rigid travel along the slot axis ("collapse" 0 to 1), no
  morphing; reload is a sequential rise; `apply_pose(..., charge_fraction)`
  drives the charged look.
- Secondary, already fixed in October: an accepted start is irrevocable, no
  cancellable wind-up, and an owner death must still release once at the
  deadline. Open in October: ammo projection, numeric thresholds, damage,
  charge and arming times, caps, lifetimes, radii, team and credit rules,
  disconnect, pickup, save, pause and end-of-match behaviour.

The model Mike mentions on 2026-11-14 is "being built separately" and will
come with its own spec. Treat the October names as the likely vocabulary, not
as final.

## 3. Format rules the pistol must respect (READ)

- Clean `.pdweapon` (`context/designs/modding/weapon-archive-clean-format.md`,
  `tools/asset_archive_conformance.py` `SCHEMAS[".pdweapon"]`): a CLOSED member
  list. Required: `weapon.ini`, `behavior/primary.graph.json`,
  `behavior/secondary.graph.json`, `behavior/settings.json`,
  `behavior/variables.json`, `behavior/shared-context.json`. Allowed:
  `bindings/animations.json`, `bindings/audio.json`,
  `bindings/presentation.json`, and typed dependencies under
  `dependencies/assets/{models,animations,audio,ui,effects,projectiles,entities}/`.
- There is NO weapon-level sockets file. `bindings/grip-sockets.json` and
  `grip_sockets_file` were retired and are rejected by the validator and by
  the scanner (`assetcatalog_scanner.c` about 2098). The clean format says the
  nested `.pdmesh` owns "geometry, hierarchy, sockets, LODs, UVs, skinning".
  So the named socket points must arrive as named nodes in the held mesh's
  own hierarchy, and anything the graphs need from them is a graph schema
  question for CORE, not a new weapon member. Adding a weapon-level sockets
  member would change a locked layout: ask Mike first.
- `.pdprojectile`: `projectile.ini`, `behavior.graph.json`, optional typed
  dependencies (models, materials, textures, audio, effects, entities).
  `.pdentity`: `entity.ini`, `bindings.json`, `behavior.graph.json`, optional
  `composition.json`.
- Every embedded archive must share the weapon's namespace. The mod author
  does this (`assetArchiveRebuildFile`): embedded ids become
  `mod:<weapon slug>__<kind>_<name>`.
- BYOR: the pistol is committed under `dev-mods/` and may be shared, so it
  must contain original assets only. Do NOT build it from a base weapon
  template: a mod made from `base:falcon2` carries copies of the Falcon's
  extracted meshes, sounds and animations (RUN: `mod_author_weapon_create_smoke`
  shows 13 embedded base archives re-identified). Start from an original
  template (the example `tri_weapon.pdweapon` or the Needler) and replace its
  members.
- Graphs are CORE's. Use the schema as it stands and put change requests in
  the "Graph schema change requests" table of
  `context/week-pd-coordination.md` (canonical checkout, untracked).

## 4. Behavior graph vocabulary as it stands

Source: a read-only survey of the runtime on 2026-11-14 (subagent, file and
line as it reported them), with three claims re-read by the session itself:
the mode projection rule (`weapon_graph_runtime.c` 3627-3649 and 4044-4073),
the ammo clamp (`bondgun.c` 2286-2300) and the entity transition gate
(`bondgun.c` 5560-5578). Abbreviations: WGR `port/src/weapon_graph_runtime.c`,
WGA `port/src/weapon_graph_archive.c`, LP `port/src/loader_pool.c`, MC
`port/src/modasset_compiler.c`, BG `src/game/bondgun.c`, PO
`src/game/propobj.c`.

- v1 weapon graphs are parameter sheets. For each mode exactly ONE node
  reaches gameplay: the node exported as `primary` or `secondary` if its kind
  is a function kind, otherwise the first function-kind node whose `mode`
  param names the slot. Edges, shared contexts and event, gate and ammo nodes
  never reach gameplay. A graph that yields no function at all is rejected.
- v1 weapon function kinds: `function.empty`, `fire.hitscan`,
  `fire.auto_cadence`, `fire.burst`, `fire.charge_release`, `fire.beam_tick`,
  `spawn.fired_projectile`, `spawn.thrown_physical`, `melee.strike`,
  `special.*`, `device.activate`, `presentation.*`. The kind itself is ignored
  except for melee and auto cadence: `fire.charge_release` behaves like
  `fire.hitscan`. Other v1 kinds (`event.trigger_pressed/held/released`,
  `gate.*`, `ammo.consume`, `ammo.reserve_transfer`) compile and do nothing.
  `event.tick` and `event.spawn` are prop-graph kinds and
  `event.native_ready_while_held` and `state.bool_set` are v2 only: all four
  are compile errors in a v1 weapon graph.
- Params read from that one node (WGR 3651-3854): `mode`, `function_type`
  (shoot_single, shoot_automatic, shoot_projectile, throw, melee, special,
  device; a missing or `none` type rejects the whole weapon, so leave a mode
  out rather than use `function.empty`), `ammo_slot` (default -1), `flags` (a
  raw FUNCFLAG mask), damage, spread, recoil fields, `duration_ticks60`,
  `penetration`, shoot and projectile sound ids, rpm and turret fields,
  `recoverytime_ticks60`, `projectile_ref`, `entity_ref`, `payload_ref`,
  `scale`, `speed`, `travel_distance`, `timer_ticks60`, `reflect_angle`,
  `activation_time_ticks60`, `recovery_time_ticks60`, `range`, `specialfunc`,
  `device`, sight, zoom, `reticle_ref`, `camera_effect`. `trigger_policy`,
  `burst_count` and `overlay_ref` are parsed and unused. Time-like keys must
  carry a unit suffix (`timer_ticks60`, not `timer`) or the compile fails.
- `ammo_slot`: -1 means no ammo accounting and unlimited fire. 0 or 1 with no
  ammo definition (the mod adapter sets none): a hitscan function fires zero
  shots, projectile, throw and special functions still spawn, and reload can
  never start. RUN: this is why the Falcon copy in
  `mod_author_weapon_play_smoke` logged no shot.
- `behavior/settings.json` accepts only `fire_cadence`, `spread`, `zoom_fov`,
  `sight`; any other key rejects the weapon. `bindings/presentation.json`:
  `crosshair`, `sight`, `zoom_fov`. `behavior/variables.json` is only used
  for `$name` substitution into weapon-graph params; it does not reach
  projectile or entity graphs, and `ammo_clip` / `ammo_reserve` are never
  read.
- Burst: flags BURST2 0x1000, BURST3 0x2, BURST5 0x20000, BURST50 0x20 give N
  shots in sequence from the same muzzle and aim. There is no simultaneous
  multi-spawn (the shotgun's pellets are hardcoded). The Mauler's charge is
  hardcoded to its weapon number; the Reaper's spin-up (rpm ramp) is usable.
- Projectile graph (`pd.projectile_graph.v1`): every node is folded into one
  record, topology ignored, applied for custom weapon slots only.
  `spawn_state` (model_ref, scale, damage, flags), `motion` (speed,
  travel_distance, timer60, reflect_angle, powered, lightweight,
  calculate_trajectory), `bounce_slide`, `trail`, `homing` (gains only),
  `fly_by_wire`, `trajectory_correction`, `pickup_recover`, `timer` (start
  on_spawn, on_impact or on_attach; on expiry a fixed rocket-class blast or a
  delete), `impact` (consume_on_hit: a character takes spawn_state.damage,
  then an explosion from `explosion_ref`), `sticky_attach`
  (allow_background, allow_char, allow_obj). Sticking works on world, props
  and characters and the stuck object stays alive; on a character it embeds
  only while that character is on screen, otherwise the object is deleted;
  sticking deals no damage. `transition_to_entity` only runs on the thrown
  path and only when the entity is a laptop-gun type; on a fired projectile
  it does nothing.
- Entity graph (`pd.entity_graph.v1`): the entity record comes from the held
  function's `entity_ref` / `payload_ref`. `proxy_trigger` (radius, default
  250) is checked against the local human only; bots, NPCs and moving objects
  set proxies off through a separate check with a fixed 250 radius.
  `timed_detonatable` counts down only for primary-fired objects.
  `armed_explosive` supplies the explosion. `owner_cleanup` on owner death:
  explode, delete or persist (default persist); the death signal is raised
  for players only. A `.pdeffect` explosion maps a class word (tiny to
  massive) to a native explosion type, so blast radius and damage are that
  native type's.
- Limits: 100 weapon objects in the world (recycled when full), 120 proxies
  (silent failure when full), 160 embedded objects, 200 projectiles; 128
  projectile and 128 entity graph records; 64 nested payloads per archive.
- Graph v2 executes one profile (`held_single_shot.v1`: single-shot hitscan,
  offline human only, no projectiles, refuses network, Theater and bot
  owners). It cannot express this weapon. CORE's ruling for the week: mods
  stay on v1.
- `weapon.ini` keys the weapon descriptor reader takes (WGA 401-489):
  `catalog_id`, `primary_graph`, `secondary_graph`, `shared_context_file`,
  `settings_file`, `variables_file`, `presentation_file`, `manifest`,
  `nested_payloads`, `model_file`, `muzzlez`, `posx`, `posy`, `posz`,
  `track_type` (none, default, beta_scanner, rocket_launcher, follow_lock_on,
  threat_detector). `name` and `dual_wieldable` are read by the catalog
  scanner. Embedded archives are found by scanning the zip, not through
  descriptor keys.
- Working examples to copy shapes from: `dev-mods/needler/needler.pdweapon`
  and `examples/modding/typed-pdxxx-basic/weapons/tri_weapon.pdweapon`.

## 5. What the mod tools can do today (RUN unless marked)

- `modAuthorSave` / `modAuthorPlan` (`port/src/mod_author.c`): a template
  archive, a display name and member overrides become
  `mods/Weapons/weapon_<slug>/` with `weapon_<slug>.pdweapon`
  (`mod:weapon_<slug>`) and a `mod.json`. "Maian Crystal Pistol" gives
  `mod:weapon_maian_crystal_pistol` and `mod.weapon_maian_crystal_pistol`.
  An override path like
  `dependencies/assets/models/weapon.pdmesh::model.gltf` replaces a member
  inside an embedded archive, so placeholder geometry can be swapped into a
  template's mesh archive without building a `.pdmesh` by hand.
- Recipe files (`port/src/mod_author_recipe.cpp`,
  schema `pd2.mod_author.recipe.v1`) and the launch flag
  `--mod-author <recipe.json>` (`port/src/mod_author_runtime.c`): the game
  itself saves, rescans, enables and applies. This is the reproducible way to
  build the pistol "with the mod tools".
- Smokes: `mod_author_weapon_create_smoke` PASS 22/22 (create from
  `base:falcon2`, enable, typed load). `mod_author_weapon_play_smoke` 20/21:
  the created weapon spawns in the player's hand in a Combat Simulator match
  but does not fire. Receipts are named in the coordination note.
- What that play run showed about ANY mod weapon on the public adapter
  (`loader_pool.c` `loaderPoolInstallPublicWeaponAdapter`, READ):
  - view position comes only from `posx`, `posy`, `posz`, `muzzlez` in
    `weapon.ini`; without them the gun is drawn at the camera origin (the
    screenshots show it filling the screen). The Needler declares
    `posx = 30.0`, `posy = -18.0`, `posz = -45.0`, `muzzlez = 3.0`,
    `track_type = rocket_launcher`. The pistol's `weapon.ini` must carry these.
  - the adapter sets no weapon name, so the HUD name is empty
    (`SPAWN.WEAPON ... name=0`).
  - the adapter sets no ammo definition. A function with `ammo_slot` 0 or 1
    then fires zero hitscan shots (section 4); the Falcon copy logged no shot
    for that reason. Use `ammo_slot` -1 (unlimited) until ammo exists for mod
    weapons.
  - no low-detail model and no equip, reload or fire animations are wired by
    the adapter, and it cannot toggle model parts.
- Run commands, hub ids and the runner are in the coordination note
  (`context/week-pd-coordination.md`, Step 0 gate) and in
  `context/designs/modding/in-game-creators-2026-11.md`.

## 6. Requirement by requirement (from the 2026-11-14 survey)

| Requirement | Today on v1 | How, or what is missing |
|---|---|---|
| Crystal sticks where it lands | Partly | `spawn.fired_projectile` (function_type shoot_projectile) with a projectile graph that has `projectile.sticky_attach` (allow_background, allow_char, allow_obj). Missing: a crystal stuck to a character that leaves the screen is deleted. |
| Three landed crystals near each other detonate | No | Nothing counts same-kind objects within a radius. Needs a new entity or projectile module (count, radius, same asset, owner scope) with a group detonation. The nearest thing today is a proxy that pops when any object enters 250 units. |
| Charge-up secondary | No | No held charge phase exists for mods. The Mauler's charge is hardcoded to its weapon number. |
| Charge cannot be cancelled and survives death | No | Needs a pending action owned by the match, not by the hand state. |
| One launch per socket, fan follows gun roll | No | One activation spawns one projectile, from the muzzle, along the camera aim. Needs a list of launch points (model parts), a count taken from the loaded crystals, and a direction from each point's matrix, in the player, bot and network spawn paths. |
| Launched crystal becomes a proximity mine | Partly | The held function's `entity_ref` with `entity.proxy_trigger` and `entity.armed_explosive`. Missing: arming on landing (today the timer starts at launch) and a radius or filter for bots and objects (fixed 250 today). |
| Ammo is the loaded crystals | No | The mod adapter sets no ammo definition. Needs a v1 ammo block (clip size, flags) feeding the weapon's ammo and the HUD. |
| Reload is regrowth | No | Needs ammo regeneration over time with no reserve and no reload animation. |
| Per-crystal states (full, regrowing, charged, empty) | No | The mesh can declare toggle parts, but the mod adapter passes no part visibility. The base game's quantity-driven part visibility only gives full or empty. Needs a v1 binding for part visibility and state looks. |
| Charge sound and brighter glow | No | Effects only spawn at explosion and spark sites. Needs a charge sound and an effect attached to the gun or a socket. |
| Tunables (damage, radii, charge time, regrow time) | Partly | Contact and hitscan damage and the proxy radius (local player only) can be set in the graphs. Blast size is a class word. No charge or regrow time exists. `$variables` reach weapon-graph params only. `settings.json` takes four fixed keys. |

So with the schema as it stands the pistol can be a pistol that fires single
sticky crystals with unlimited ammo, plus a secondary that throws or fires one
proximity mine. Everything that makes it the crystal pistol is a CORE ask;
the asks are filed in the coordination note's schema table (2026-11-14).

## 7. What the `.pdweapon` needs from the model

From the survey of the mesh compiler (MC) and the Needler builder:

1. Source format: OBJ plus MTL is the format that keeps the most. It carries
   per-vertex colour (`v x y z r g b a`, 0 to 255), `vt` UVs, groups (`g` /
   `o`) and `usemtl`; the MTL may name a texture (`map_Kd` or
   `pd_texture_catalog`) and a render class (opaque, xlu, tex_edge). A glTF
   `.gltf` is accepted too, but only positions, indices and
   `baseColorFactor` (turned into vertex colour) survive; UVs, vertex
   colours, normals, skins, textures and node names are dropped. FBX is not a
   `.pdmesh` source. So ask for OBJ plus MTL, or glTF with flat-colour
   materials if textures are not needed yet. No loose `.bin`.
2. Named parts and attachment points are NOT taken from node names. They
   come from four sidecar files that must be supplied together inside the
   `.pdmesh`: `model.nodes.json`, `model.parts.json`, `model.faces.json`,
   `model.render.json` (the extractor writes these for base guns; see any
   base weapon's `held_hi.pdmesh`). OBJ group names choose which display list
   a triangle goes into. The game looks points up by PART NUMBER, never by
   name: the muzzle is model part 0x32 if the mesh has it, otherwise the gun
   origin. So the handoff must give, for each socket (muzzle, each crystal
   socket, grip): a name, a position and a direction, and the converter turns
   them into position parts with agreed part numbers. Proposed numbering to
   agree with CORE: muzzle 0x32 (existing), crystal sockets as one contiguous
   range.
3. Each crystal as its own OBJ group, so it becomes its own toggle part. The
   base game's quantity-driven visibility shows parts 100 + j while j is below
   the loaded count; using that numbering for crystals 0 to N-1 keeps the door
   open to the existing mechanism.
4. The four crystal states. Today nothing can switch them for a mod weapon.
   Deliver each state as a separate group per crystal (full, regrowing,
   charged; empty is "hidden") so either part ranges per state or a later
   tint binding can drive them.
5. Size, axes and limits: forward is -Z in the Needler builder; author about
   five times real size because first person applies a 0.1 scale; the view
   position is `posx`, `posy`, `posz` in `weapon.ini` (the Needler uses 30,
   -18, -45 and `muzzlez` 3). Coordinates become 16-bit integers, UVs are
   16-bit at 32 units per texel, at most 32,767 vertices per node and 64
   distinct vertex colours per model. The October source is in uncalibrated
   artist units with forward -X and up +Z, so the spec must state the
   conversion.
6. Materials as named slots (teal body, dark blue grip, blue inlay, green
   crystal, charged crystal). Emissive and normals are ignored by the
   compiler, so "glow" has to be a brighter flat colour or a texture until a
   glow effect exists.
7. A separate small crystal model for the fired projectile and the mine
   (`model_ref` of the projectile graph).
8. Sounds as source audio for `.pdsfx` (fire, charge, launch, detonate,
   regrow), or base sounds referenced by catalog id for now.
9. Not usable yet for any mod weapon: equip, reload and fire animations (the
   mod adapter plays none), a low-detail model, a HUD name.

## 8. Suggested order when the tools are done

1. Schema asks are filed with CORE (coordination note, 2026-11-14). Watch
   that table for answers; build against what exists meanwhile.
2. Write the recipe folder under `dev-mods/maian_crystal_pistol/`
   (`recipe.json`, graphs, settings, variables, presentation, `weapon.ini`
   with view position, placeholder mesh sources with the socket nodes).
3. Build it through `modAuthorSave` in a unit test and through
   `--mod-author` in a smoke; validate with the in-game Validate and
   `tools/asset_archive_conformance.py`.
4. Primary first (single sticky crystals), with whatever v1 can do; then the
   secondary as CORE's support lands.
5. When the real model arrives, swap the placeholder mesh members through
   overrides; nothing else in the recipe should need to change if section 7
   was followed.
