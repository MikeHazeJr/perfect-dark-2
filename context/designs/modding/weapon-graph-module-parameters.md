# Weapon Graph Module Parameters

Status: module parameter slice for Kanban `c3814-s8`.

This document turns the base weapon audit into named graph modules for `.pdweapon`, `.pdprojectile`, and `.pdentity`. It is the last design boundary before scanner/emitter/runtime implementation tasks are split.

## Common Parameter Rules

- Use explicit units for every time value: `ticks60`, `ticks240`, `centiseconds`, or `rpm`.
- Use game-world units for distance, speed, radius, and offset values.
- Use catalog IDs for cross-asset references: weapon, projectile, entity, model, animation, audio, effect, and UI references.
- Use named enums for current C flags instead of exposing raw bit fields to tools.
- Keep random behavior deterministic by passing a named random stream and authored range instead of arbitrary callback code.
- Any owner/team/target policy must be authored data so netplay and mods do not inherit hidden C branches.
- Runtime may keep optimized lists, pools, and prop internals, but graph-visible behavior must be represented by named modules and parameters.

## `.pdweapon` Modules

These modules live in `behavior.graph.json` inside `.pdweapon` archives.

### Shared Context

Decision 2026-05-22: primary and secondary behavior are modular subgraphs inside one `.pdweapon`, with explicit shared context entries for cross-mode and spawned-object state. The initial shared context set is:

| Context | Scope | Purpose |
| --- | --- | --- |
| `owner_player` | player | The player currently owning, equipping, or deploying the weapon. |
| `owner_team` | player | Team-aware targeting and friendly/hostile filtering. |
| `weapon_instance` | weapon | State shared between primary/secondary modules for this held weapon. |
| `damage_credit_player` | projectile | Damage dealer attribution for spawned projectiles and effects. |
| `projectile_owner` | projectile | Projectile ownership/lifecycle handoff from weapon to projectile. |
| `deployed_entity_set` | entity | Owned deployed objects such as remote mines or Laptop Gun autoguns. |
| `detonator_link_group` | weapon/entity | Links detonator actions to owned remote mine entities. |
| `target_policy_override` | entity | Runtime retargeting policy used by hacking/reprogramming tools. |
| `hacked_by_player` | player | The player applying a hacking override to another deployed entity. |

| Module | Key parameters | Base users | Notes |
| --- | --- | --- | --- |
| `event.trigger_pressed` | `mode`, `hand`, `requires_equipped`, `consume_press` | all weapons | Starts primary/secondary action graphs. |
| `event.trigger_held` | `mode`, `hold_time_out`, `repeat_policy` | auto fire, beams, Mauler charge, grenade hold | Feeds continuous actions without scripting. |
| `event.trigger_released` | `mode`, `min_hold`, `max_hold` | Mauler charge, throw release, beams | Needed for charge/release and primed throw behavior. |
| `gate.ammo_available` | `ammo_slot`, `minimum_loaded`, `minimum_reserve`, `allow_noreserve` | all ammo weapons | Mauler secondary needs `minimum_loaded = 2` while charging. |
| `gate.cooldown_ready` | `cooldown`, `recovery_time`, `animation_lock_policy` | single shot, melee, special | Preserves `recoverytime60` behavior. |
| `gate.target_lock` | `source`, `required`, `target_filter`, `lost_behavior` | homing, Farsight, threat detector | Converts target tracking into data. |
| `ammo.consume` | `ammo_slot`, `amount`, `when`, `allow_partial`, `consume_from_loaded_first` | all weapons | Mauler charge consumes one round per charge step after the first. |
| `ammo.reserve_transfer` | `from_weapon`, `to_entity`, `cap`, `cheat_policy` | Laptop autogun | Handles Laptop Gun ammo transfer into deployed entity reserve. |
| `fire.hitscan` | `damage`, `spread`, `duration`, `penetration`, `impact_force`, `noise`, `shot_sound`, `recoil`, `special_hit_effects` | pistols, automatics, sniper, Farsight, Laser | Current `weaponfunc_shoot` and `weaponfunc_shootauto` data lands here. |
| `fire.auto_cadence` | `initial_rpm`, `max_rpm`, `spinup`, `turret_accel`, `turret_decel`, `tick_policy` | automatics, Laptop autogun | Laptop autogun uses entity-side cadence but the parameter shape is shared. |
| `fire.burst` | `count`, `spacing`, `burst_refire_policy`, `consume_per_shot` | AR34, MagSec, Cyclone, Shotgun, Reaper | Maps `BURST2`, `BURST3`, `BURST5`, and `BURST50`. |
| `fire.charge_release` | `charge_min`, `charge_max`, `charge_rate`, `decay_rate`, `ammo_per_step`, `release_damage_curve`, `sound`, `sound_pitch_curve` | Mauler secondary | Captures `matmot1` charge behavior and the charge sound pitch curve. |
| `fire.beam_tick` | `damage_per_tick`, `tick_rate`, `range`, `beam_visual`, `sound_loop`, `ammo_tick_policy` | Laser, Watch Laser | Separates beam fire from normal hitscan. |
| `spawn.fired_projectile` | `projectile_ref`, `origin`, `muzzle_offset`, `aim_source`, `trajectory_correction`, `inherit_owner_velocity`, `launch_sound` | rockets, Slayer, Crossbow, SuperDragon, Devastator | Bridges `.pdweapon` to `.pdprojectile`. |
| `spawn.thrown_physical` | `payload_ref`, `origin`, `throw_speed`, `vertical_boost`, `trajectory_correction`, `rotation_policy`, `discard_weapon`, `pickup_lockout` | grenades, mines, thrown knife, Dragon, Laptop | Payload can be `.pdprojectile` or `.pdentity` with projectile-carrier behavior. |
| `melee.strike` | `damage`, `range`, `hit_effects`, `uncloak_policy`, `disarm_policy`, `stun_policy` | punches, pistol whip, knife, Reaper | Maps melee flags into authored data. |
| `special.remote_detonator` | `owner_filter`, `mine_entity_type`, `broadcast_mask_source`, `animation`, `sound`, `recovery` | Remote Mine secondary | Sets owned remote mines to detonate without hard-coded weapon checks. |
| `special.combat_boost` | `action`, `duration`, `time_scale`, `sound`, `recovery` | Combat Boost | Covers boost and revert boost. |
| `special.weapon_state` | `action`, `target_state`, `sound`, `recovery` | crouch, RCP120 cloak, Data Uplink | Covers special actions that change weapon/player state. |
| `device.activate` | `device_type`, `toggle_policy`, `overlay_ref`, `energy_policy`, `sound`, `recovery` | scanners, cloak, EyeSpy, suicide pill | Device behavior stays first-class, not a projectile/entity unless it deploys one. |
| `presentation.weapon_visibility` | `part_id`, `condition`, `ammo_slot`, `thresholds`, `visibility_state` | Laptop Gun, Farsight, ammo-visible weapons | Replaces hidden gunvis/part visibility behavior. |
| `presentation.reticle_overlay_camera` | `reticle_ref`, `overlay_ref`, `zoom_fovs`, `camera_effect`, `vision_mode` | sniper, Farsight, Horizon Scanner, Slayer | Makes view effects editable. |

## `.pdprojectile` Modules

These modules live in `.pdprojectile` behavior or are referenced by a `.pdweapon` spawn node.

| Module | Key parameters | Base users | Notes |
| --- | --- | --- | --- |
| `projectile.spawn_state` | `model_ref`, `scale`, `initial_timer`, `owner_ref_policy`, `hidden_owner_bits`, `dangerous_on_spawn`, `pickup_timer` | all fired/thrown physicals | Normalizes launch-time setup. |
| `projectile.motion` | `motion_kind`, `initial_speed`, `initial_vector`, `inherits_owner_velocity`, `gravity`, `lightweight_gravity`, `powered`, `power_limit`, `deceleration` | rockets, bolts, grenades, mines | Covers powered rockets and ballistic thrown objects. |
| `projectile.trajectory_correction` | `enabled`, `aim_source`, `max_angle`, `solve_velocity`, `fallback_vector` | throwables, bolts, grenades | Maps `CALCULATETRAJECTORY` and the separate fired/thrown angle clamps. |
| `projectile.homing` | `target_source`, `target_filter`, `steering_gain`, `steering_damping`, `lost_target_behavior`, `retarget_policy` | homing rocket | Current steering constants need named parameters during cutover. |
| `projectile.fly_by_wire` | `control_source`, `bot_route_policy`, `turn_rate`, `acceleration`, `enemy_proximity_radius`, `lost_target_timeout`, `max_altitude`, `smoke_interval`, `owner_death_behavior` | Slayer secondary | Covers player controlled rocket plus bot route fallback. |
| `projectile.wall_hugger` | `stick_surface_filter`, `stick_timer`, `fall_threshold`, `fall_vector`, `post_fall_timer`, `explosion_ref` | Devastator secondary | Captures stick, wait, fall, explode sequence. |
| `projectile.sticky_attach` | `surface_filter`, `prop_filter`, `allow_background`, `allow_char`, `allow_obj`, `embed_policy`, `on_attach` | mines, bolts, knives, sticky devices, Laptop | Laptop permits background stick but not prop stick. |
| `projectile.bounce_slide` | `reflect_angle`, `bounce_limit`, `first_bounce_boost`, `rest_speed`, `slide_friction`, `randomize_rotation` | grenades, N-Bomb, grenade rounds | Covers normal bounce and grenade secondary pinball behavior. |
| `projectile.timer` | `timer`, `timer_unit`, `timer_starts`, `on_expire` | grenades, N-Bomb, Devastator, mines, rockets | `on_expire` can explode, create storm, fall, or delete. |
| `projectile.impact` | `impact_filter`, `damage`, `explosion_ref`, `spark_ref`, `hit_sound`, `consume_on_hit`, `stick_on_hit` | rockets, bolts, knives, grenade rounds | Separates direct impact from timer expiry. |
| `projectile.trail` | `trail_type`, `interval`, `spawn_offset`, `condition` | rockets, homing rockets, grenade rounds | Covers rocket smoke and grenade-round trails. |
| `projectile.transition_to_entity` | `entity_ref`, `when`, `transfer_owner`, `transfer_ammo`, `transfer_position`, `delete_carrier` | Laptop, mines, sticky devices | Lets implementation instantiate early while graph remains carrier -> entity. |
| `projectile.pickup_recover` | `pickup_timer`, `allowed_owner`, `recover_weapon_ref`, `recover_ammo_policy`, `sound` | thrown Laptop, thrown weapons | Needed for deployed Laptop pickup/recover path. |

## `.pdentity` Modules

These modules live in `.pdentity` behavior.

| Module | Key parameters | Base users | Notes |
| --- | --- | --- | --- |
| `entity.armed_explosive` | `arm_delay`, `detonation_policy`, `explosion_ref`, `owner_filter`, `damage_response`, `delete_on_detonate` | timed mine, remote mine, proxy mine, Dragon proxy | Shared base for mine-like objects. |
| `entity.proxy_trigger` | `radius`, `radius_overrides`, `poll_policy`, `target_filter`, `team_filter`, `owner_filter`, `line_of_sight`, `on_trigger` | proximity mine, Dragon, grenade secondary, N-Bomb secondary | Dragon uses a larger trigger radius in one runtime path. |
| `entity.remote_detonatable` | `detonator_ref`, `owner_slot_source`, `coop_policy`, `anti_policy`, `self_attached_policy`, `on_remote_signal` | Remote Mine | Replaces global-mask-only hidden behavior with authored policy. |
| `entity.timed_detonatable` | `timer`, `starts_when`, `on_expire`, `pause_policy` | Timed Mine, Grenade | Used when the armed entity, not just projectile carrier, owns the timer. |
| `entity.nbomb_storm` | `storm_ref`, `owner_transfer`, `activation_policy`, `delete_carrier` | N-Bomb | Storm creation is an entity/effect trigger, not just explosion damage. |
| `entity.autogun` | `target_filter`, `team_policy`, `aim_distance`, `turn_speed`, `fire_cadence`, `alternate_muzzles`, `beam_interval`, `ammo_reserve`, `net_authority`, `friendly_fire_suppression`, `pickup_recover` | deployed Laptop Gun | Server fires in netplay; MP damage halves; friendly line-of-fire suppresses visible firing. |
| `entity.sticky_device` | `attachment_filter`, `mission_behavior_ref`, `pickup_policy`, `disable_policy`, `visible_state` | ECM Mine, Comms Rider, Tracer Bug, Target Amplifier | Mission-specific details still need runtime extraction during implementation. |
| `entity.owner_cleanup` | `owner_lost_behavior`, `owner_death_behavior`, `replace_existing_policy`, `max_active_per_owner` | Laptop, Slayer rocket, mines | Laptop allows one deployed autogun per owner and explodes/replaces old one. |
| `entity.interaction` | `interact_filter`, `action`, `prompt_ref`, `sound`, `transfer_payload` | Laptop pickup/recover, sticky devices | Keeps pickup and recover rules editable. |

## Base Mapping Decisions

These are locked enough for implementation planning:

- `base:laptopgun` secondary uses `spawn.thrown_physical` with `projectile.transition_to_entity` targeting `entity.autogun`.
- `base:dragon` secondary uses `spawn.thrown_physical` plus `entity.proxy_trigger` and `entity.armed_explosive`.
- `base:remotemine` primary uses `spawn.thrown_physical` plus `entity.remote_detonatable`; secondary uses `special.remote_detonator`.
- `base:proximitymine` uses `entity.proxy_trigger`.
- `base:timedmine` uses `entity.timed_detonatable`.
- `base:grenade` primary uses `projectile.timer`; secondary uses `projectile.bounce_slide` plus `entity.proxy_trigger` style activation.
- `base:nbomb` uses `entity.nbomb_storm` for timed and proxy activation.
- `base:slayer` secondary uses `projectile.fly_by_wire`; primary uses powered rocket motion.
- `base:rocketlauncher` secondary uses `projectile.homing`.
- `base:devastator` secondary uses `projectile.wall_hugger`.
- `base:mauler` secondary uses `fire.charge_release`.
- `base:laser` and `base:watchlaser` use `fire.beam_tick`.

## Remaining Runtime Extraction Risks

These do not block schema naming, but they should be checked while implementing:

- Farsight auto-seek and wall-penetration behavior needs a focused pass before runtime parity tests.
- Sticky mission devices need final mission behavior ownership before `.pdentity` activation is considered complete.
- Homing rocket steering constants should be extracted with readable names during implementation, not left as `unk` fields.
- Projectile bounce/slide internals use several anonymous runtime fields; the conversion should name them by behavior, not by struct field.
- Laptop Gun runtime can continue instantiating the autogun early as long as the graph-facing model remains throw carrier -> deployed autogun.
