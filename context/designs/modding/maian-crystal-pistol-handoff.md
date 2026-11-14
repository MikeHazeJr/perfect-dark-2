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

## 4. Behavior graph vocabulary as it stands (READ: strings in the runtime sources)

- Weapon (`pd.weapon_graph.v1`): `event.trigger_pressed`,
  `event.trigger_held`, `event.trigger_released`,
  `event.native_ready_while_held`, `event.tick`, `event.spawn`,
  `function.empty`, `spawn.fired_projectile`, `spawn.thrown_physical`,
  `ammo.consume`, `ammo.reserve_transfer`, `state.bool_set`,
  `presentation.reticle_overlay_camera`, `presentation.weapon_visibility`.
- Projectile (`pd.projectile_graph.v1`): `projectile.spawn_state`, `motion`,
  `homing`, `impact` (has `stick_on_hit`, `consume_on_hit`, `explosion_ref`,
  `spark_ref`), `bounce_slide`, `fly_by_wire`, `pickup_recover`,
  `sticky_attach`, `timer`, `trail`, `trajectory_correction`,
  `transition_to_entity` (`entity_ref`, `when`, `transfer_owner`,
  `transfer_position`), `wall_hugger`.
- Entity (`pd.entity_graph.v1`): `entity.armed_explosive`, `autogun`,
  `interaction`, `nbomb_storm`, `owner_cleanup` (`max_active_per_owner`),
  `proxy_trigger`, `remote_detonatable`, `sticky_device`,
  `timed_detonatable`.
- Effect: `effect.explosion`, `glow`, `particle`, `screen`, `shimmer`,
  `smoke`, `spark`, `tint`, `darken`.
- Working examples to copy shapes from: `dev-mods/needler/needler.pdweapon`
  (two projectile graphs, an embedded effect) and
  `examples/modding/typed-pdxxx-basic/weapons/tri_weapon.pdweapon` (a
  projectile that turns into an entity at rest, an armed explosive entity).
- CORE's ruling for this week: mod weapons stay on v1 graphs. v1 graphs are
  parameter sheets (values are read, the topology is not executed).

What v1 is NOT expected to express (to be confirmed, then asked of CORE):
the three-crystal proximity rule, a committed charge that survives the
wielder's death, launching one projectile per named socket, ammo as loaded
crystals with regrowth, per-crystal visual states, and the charge glow. A
read-only survey of the runtime was started on 2026-11-14 to pin each of
these to file and line; its results belong in section 6.

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
  - the adapter sets no ammo definition. OPEN: whether a function with
    `ammo_slot` 0 can fire at all; the Falcon copy logged no shot.
  - no low-detail model, no equip, reload or fire animations are wired by the
    adapter (OPEN: confirm).
- Run commands, hub ids and the runner are in the coordination note
  (`context/week-pd-coordination.md`, Step 0 gate) and in
  `context/designs/modding/in-game-creators-2026-11.md`.

## 6. Requirement by requirement (fill in from the survey)

| Requirement | Expressible today? | How, or what is missing |
|---|---|---|
| Crystal sticks where it lands | OPEN (nodes exist: `projectile.impact` `stick_on_hit`, `projectile.sticky_attach`) | |
| Three landed crystals near each other detonate | OPEN (no node kind found for it) | |
| Charge-up secondary | OPEN | |
| Charge cannot be cancelled and survives death | OPEN | |
| One launch per socket, fan follows gun roll | OPEN | |
| Launched crystal becomes a proximity mine | OPEN (nodes exist: `projectile.transition_to_entity`, `entity.proxy_trigger`) | |
| Ammo is the loaded crystals | OPEN | |
| Reload is regrowth | OPEN | |
| Per-crystal states (full, regrowing, charged, empty) | OPEN | |
| Charge sound and brighter glow | OPEN | |
| Tunables (damage, radii, charge time, regrow time) | OPEN (`behavior/settings.json`, `behavior/variables.json` exist) | |

## 7. What the `.pdweapon` needs from the model (first cut, refine with the survey)

Write the final list here once the mesh importer's limits are confirmed. What
is already certain from the format rules:

1. One held model, delivered as a source the nested `.pdmesh` accepts (glTF
   `.gltf` with embedded buffers, or OBJ plus MTL; no loose `.bin`, which the
   archive policy forbids unless declared as a glTF buffer). FBX is not a
   `.pdmesh` source; the handoff should include glTF.
2. Named nodes in the mesh hierarchy for: the muzzle, each crystal socket
   (position plus launch axis), and the grip. Proposed names, matching the
   October contract where it has one: `muzzle`, `crystal_slot_0` to
   `crystal_slot_4` (or however many sockets the new model has), `grip`.
   State for each socket node which local axis is the launch direction.
3. Each crystal as its own named part, so it can be shown, hidden, moved
   along its slot axis and recoloured per state without touching the rest:
   `crystal_0` to `crystal_4`, plus the muzzle crystal as a static part.
4. The four crystal states as something the game can switch per crystal:
   either separate named parts per state or one part plus a material or
   emissive variant per state. Which of these the engine can drive is OPEN.
5. Forward and up axes, and the scale in game units, stated in the spec. The
   October source is uncalibrated; the Needler's mesh and `posx/posy/posz`
   are the working reference for size and view position.
6. Materials as named slots (teal body, dark blue grip, blue inlay, green
   crystal, charged crystal), with textures, if any, as files that can become
   `.pdtexture` dependencies of the mesh.
7. A separate small crystal model for the fired projectile and the mine.
8. Sounds as source audio for `.pdsfx` (fire, charge, launch, detonate,
   regrow), or a statement that base sounds are to be referenced by catalog
   id for now.

## 8. Suggested order when the tools are done

1. Finish section 6 from the survey; file the schema asks with CORE.
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
