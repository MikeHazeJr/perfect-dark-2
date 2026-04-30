# Visual Scripting Node Type Taxonomy

> **Version**: v0.5.0+ | **Date**: 2026-04-09
> **Depends on**: Studio Platform Design (`studio-platform-design.md`), Asset Catalog (`assetcatalog.h`), Weapon System (`bondgun.c`), Prop System (`propobj.c`)
> **Scope**: Complete node taxonomy for the PD2 visual scripting system — every node type, every pin, every data type, plus proof graphs reconstructing base-game weapons and a full mission flow.

---

## Data Type System

Every pin on every node has a typed connection. The type system is small and strict — no implicit coercion except Number <-> Integer truncation.

| Type | C Backing | Wire Color | Description |
|------|-----------|------------|-------------|
| `FlowSignal` | `void` (execution) | White | Execution flow — one-shot pulse, not data |
| `Number` | `f32` | Green | 32-bit float |
| `Integer` | `s32` | Cyan | 32-bit signed integer |
| `Boolean` | `s32` (0/1) | Red | True/false |
| `String` | `char[256]` | Yellow | Text, including catalog IDs when untyped |
| `CatalogID` | `char[64]` | Gold | Asset catalog ID: `"namespace:name"` format |
| `Entity` | `s32` (chrnum) | Blue | Single entity reference (chr slot index) |
| `EntityList` | `s32[64]` + count | Blue (dashed) | Up to 64 entity refs |
| `Vector3` | `f32[3]` | Purple | 3D position or direction |
| `AudioRef` | `CatalogID` | Orange | Alias for CatalogID, audio-typed for picker |
| `ModelRef` | `CatalogID` | Pink | Alias for CatalogID, model-typed for picker |
| `Curve` | `f32[16]` (keyframes) | White (dashed) | 16-point piecewise-linear curve, t=[0,1] |

**Pin Notation**: `pin_name:Type` — e.g., `damage:Number`, `target:Entity`, `exec_out:FlowSignal`

---

## Category 1: TRIGGERS

Triggers have no input flow pins. They fire their output `exec:FlowSignal` when the event occurs. All triggers also emit relevant context data as output pins.

### 1.1 `on_fire`

Fires when this weapon's trigger is pulled.

| Pin | Direction | Type | Description |
|-----|-----------|------|-------------|
| `exec` | OUT | FlowSignal | Fires each time weapon fires |
| `hand` | OUT | Integer | 0=right, 1=left (dual wield) |
| `owner` | OUT | Entity | Chr who fired |
| `aim_dir` | OUT | Vector3 | Normalized aim direction |
| `aim_origin` | OUT | Vector3 | Muzzle world position |
| `ammo_remaining` | OUT | Integer | Ammo after this shot |
| `is_ads` | OUT | Boolean | True if aim-down-sights active |
| `fire_mode` | OUT | Integer | 0=primary, 1=secondary |

**Usage**: Entry point for all weapon behavior. Every weapon graph starts here.

---

### 1.2 `on_hit`

Fires when a projectile from this weapon hits something.

| Pin | Direction | Type | Description |
|-----|-----------|------|-------------|
| `exec` | OUT | FlowSignal | |
| `hit_entity` | OUT | Entity | Chr hit (-1 if world) |
| `hit_position` | OUT | Vector3 | World-space impact point |
| `hit_normal` | OUT | Vector3 | Surface normal at impact |
| `hit_is_chr` | OUT | Boolean | True if hit a character |
| `hit_is_world` | OUT | Boolean | True if hit world geometry |
| `hit_distance` | OUT | Number | Distance from muzzle to impact |
| `projectile_id` | OUT | Integer | Pool index of the projectile that hit |
| `owner` | OUT | Entity | Chr who owns this weapon |

**Usage**: Damage application, impact effects, accumulation checks.

---

### 1.3 `on_pickup`

Fires when a player picks up this weapon.

| Pin | Direction | Type | Description |
|-----|-----------|------|-------------|
| `exec` | OUT | FlowSignal | |
| `player` | OUT | Entity | Chr who picked up |
| `weapon_id` | OUT | CatalogID | Weapon catalog ID |
| `ammo_given` | OUT | Integer | Ammo received |
| `had_weapon` | OUT | Boolean | True if player already had this weapon |

---

### 1.4 `on_death`

Fires when the weapon's owner dies.

| Pin | Direction | Type | Description |
|-----|-----------|------|-------------|
| `exec` | OUT | FlowSignal | |
| `victim` | OUT | Entity | The dying chr |
| `killer` | OUT | Entity | Who killed them (-1 if self/world) |
| `was_holding` | OUT | Boolean | True if this weapon was active at death |
| `death_position` | OUT | Vector3 | Position of death |

**Usage**: Drop weapon, detonate accumulated needles, trigger death effects.

---

### 1.5 `on_timer`

Fires after a specified delay from when the timer was started via `timer_start`.

| Pin | Direction | Type | Description |
|-----|-----------|------|-------------|
| `exec` | OUT | FlowSignal | |
| `timer_id` | OUT | Integer | Which timer slot fired |
| `elapsed` | OUT | Number | Actual elapsed seconds |

---

### 1.6 `on_objective_complete`

Fires when a mission objective is completed.

| Pin | Direction | Type | Description |
|-----|-----------|------|-------------|
| `exec` | OUT | FlowSignal | |
| `objective_index` | OUT | Integer | Which objective (0-based) |
| `objective_text` | OUT | String | Objective description |
| `player` | OUT | Entity | Who completed it |

---

### 1.7 `on_proximity`

Fires when an entity enters a radius around a position. Checks each tick.

| Pin | Direction | Type | Description |
|-----|-----------|------|-------------|
| `position` | IN | Vector3 | Center point to monitor |
| `radius` | IN | Number | Detection radius (default 250, matching mine proximity) |
| `team_filter` | IN | Integer | -1=all, 0+=specific team |
| `exec` | OUT | FlowSignal | Fires once per entity entering |
| `entity` | OUT | Entity | Who entered the radius |
| `distance` | OUT | Number | Exact distance when triggered |

**Notes**: Mirrors the proximity mine system in `propobj.c` (lines 4657-4676: `xdist*xdist + ydist*ydist + zdist*zdist < 250*250`). Re-arms after entity leaves radius.

---

### 1.8 `on_match_start`

Fires once when a multiplayer match begins.

| Pin | Direction | Type | Description |
|-----|-----------|------|-------------|
| `exec` | OUT | FlowSignal | |
| `stage_id` | OUT | CatalogID | Map catalog ID |
| `player_count` | OUT | Integer | Number of players |
| `bot_count` | OUT | Integer | Number of bots |
| `scenario_id` | OUT | CatalogID | Game mode |

---

### 1.9 `on_trigger_held`

Fires every tick while the weapon trigger is held. Distinct from `on_fire` which fires on trigger pull.

| Pin | Direction | Type | Description |
|-----|-----------|------|-------------|
| `exec` | OUT | FlowSignal | Every tick while held |
| `hold_time` | OUT | Number | Seconds since trigger down |
| `owner` | OUT | Entity | |
| `fire_mode` | OUT | Integer | 0=primary, 1=secondary |

**Usage**: Charge weapons (Mauler), spin-up (Reaper), guided rockets (Slayer).

---

### 1.10 `on_trigger_released`

Fires once when the weapon trigger is released.

| Pin | Direction | Type | Description |
|-----|-----------|------|-------------|
| `exec` | OUT | FlowSignal | |
| `hold_duration` | OUT | Number | Total seconds held |
| `owner` | OUT | Entity | |
| `fire_mode` | OUT | Integer | |

**Usage**: Release charged shot, stop spin-up, release guided missile control.

---

### 1.11 `on_deploy`

Fires when this weapon is deployed as a prop (thrown mine, laptop turret).

| Pin | Direction | Type | Description |
|-----|-----------|------|-------------|
| `exec` | OUT | FlowSignal | |
| `deploy_position` | OUT | Vector3 | Where deployed |
| `deploy_normal` | OUT | Vector3 | Surface normal (for wall-mounted) |
| `owner` | OUT | Entity | Who deployed |

---

### 1.12 `on_secondary_activate`

Fires when secondary fire button is pressed while holding this weapon.

| Pin | Direction | Type | Description |
|-----|-----------|------|-------------|
| `exec` | OUT | FlowSignal | |
| `owner` | OUT | Entity | |

**Usage**: Remote mine detonation, Dragon mine deploy, Laptop turret recall.

---

## Category 2: CONDITIONS

Condition nodes have one input `exec:FlowSignal` and branch to `true:FlowSignal` or `false:FlowSignal`.

### 2.1 `if_ammo_above`

| Pin | Direction | Type | Description |
|-----|-----------|------|-------------|
| `exec` | IN | FlowSignal | |
| `entity` | IN | Entity | Chr to check |
| `amount` | IN | Integer | Threshold |
| `true` | OUT | FlowSignal | Ammo > amount |
| `false` | OUT | FlowSignal | Ammo <= amount |
| `current_ammo` | OUT | Integer | Actual ammo count |

---

### 2.2 `if_health_below`

| Pin | Direction | Type | Description |
|-----|-----------|------|-------------|
| `exec` | IN | FlowSignal | |
| `entity` | IN | Entity | |
| `threshold` | IN | Number | Health value (0.0 - 1.0 normalized) |
| `true` | OUT | FlowSignal | Health < threshold |
| `false` | OUT | FlowSignal | Health >= threshold |
| `current_health` | OUT | Number | Actual health ratio |

---

### 2.3 `if_count_exceeds`

Tests a counter variable against a threshold. Used for accumulation (needler).

| Pin | Direction | Type | Description |
|-----|-----------|------|-------------|
| `exec` | IN | FlowSignal | |
| `value` | IN | Integer | Current count |
| `threshold` | IN | Integer | Limit |
| `true` | OUT | FlowSignal | value >= threshold |
| `false` | OUT | FlowSignal | value < threshold |

---

### 2.4 `if_distance_within`

| Pin | Direction | Type | Description |
|-----|-----------|------|-------------|
| `exec` | IN | FlowSignal | |
| `point_a` | IN | Vector3 | |
| `point_b` | IN | Vector3 | |
| `max_distance` | IN | Number | |
| `true` | OUT | FlowSignal | |
| `false` | OUT | FlowSignal | |
| `actual_distance` | OUT | Number | Computed distance |

---

### 2.5 `if_has_item`

| Pin | Direction | Type | Description |
|-----|-----------|------|-------------|
| `exec` | IN | FlowSignal | |
| `entity` | IN | Entity | |
| `item_id` | IN | CatalogID | Weapon or item |
| `true` | OUT | FlowSignal | Has item |
| `false` | OUT | FlowSignal | Does not have |

---

### 2.6 `if_is_player`

| Pin | Direction | Type | Description |
|-----|-----------|------|-------------|
| `exec` | IN | FlowSignal | |
| `entity` | IN | Entity | |
| `true` | OUT | FlowSignal | Entity is human player |
| `false` | OUT | FlowSignal | Entity is bot or NPC |

---

### 2.7 `if_line_of_sight`

Raycast visibility check between two points (uses `cdTestLos05` with `GEOFLAG_BLOCK_SIGHT`).

| Pin | Direction | Type | Description |
|-----|-----------|------|-------------|
| `exec` | IN | FlowSignal | |
| `from` | IN | Vector3 | |
| `to` | IN | Vector3 | |
| `true` | OUT | FlowSignal | Clear line of sight |
| `false` | OUT | FlowSignal | Blocked |

---

### 2.8 `if_on_team`

| Pin | Direction | Type | Description |
|-----|-----------|------|-------------|
| `exec` | IN | FlowSignal | |
| `entity` | IN | Entity | |
| `team` | IN | Integer | Team index |
| `true` | OUT | FlowSignal | Same team |
| `false` | OUT | FlowSignal | Different team |

---

## Category 3: ACTIONS

Action nodes consume a `FlowSignal` and perform a side effect. Most have an `exec_out:FlowSignal` for chaining.

### 3.1 `fire_projectile`

Spawns a projectile from the Studio projectile system (`studio_projectile.h`).

| Pin | Direction | Type | Description |
|-----|-----------|------|-------------|
| `exec` | IN | FlowSignal | |
| `origin` | IN | Vector3 | Spawn position |
| `direction` | IN | Vector3 | Normalized fire direction |
| `owner` | IN | Entity | Who fired |
| `speed` | IN | Number | Units per second |
| `damage` | IN | Number | Per-hit damage |
| `lifetime` | IN | Number | Seconds before despawn |
| `gravity` | IN | Number | Gravity scale (0=none, 1=normal) |
| `radius` | IN | Number | Collision radius |
| `model` | IN | ModelRef | Projectile model (empty=invisible) |
| `exec_out` | OUT | FlowSignal | After spawn |
| `projectile_id` | OUT | Integer | Pool index of spawned projectile |

**Notes**: Core projectile spawn. For tracking, accumulation, bounce — chain with weapon nodes (Category 7).

---

### 3.2 `play_sound`

| Pin | Direction | Type | Description |
|-----|-----------|------|-------------|
| `exec` | IN | FlowSignal | |
| `sound` | IN | AudioRef | Catalog ID of audio asset |
| `volume` | IN | Number | 0.0-1.0 (default 1.0) |
| `pitch` | IN | Number | 0.5-2.0 (default 1.0) |
| `exec_out` | OUT | FlowSignal | |

**Notes**: 2D sound — no spatialization. For 3D positional audio, use `play_sound_3d` (Category 8).

---

### 3.3 `give_weapon`

| Pin | Direction | Type | Description |
|-----|-----------|------|-------------|
| `exec` | IN | FlowSignal | |
| `entity` | IN | Entity | Recipient |
| `weapon_id` | IN | CatalogID | Weapon to give |
| `ammo` | IN | Integer | Ammo to include |
| `switch_to` | IN | Boolean | Auto-switch to this weapon |
| `exec_out` | OUT | FlowSignal | |

---

### 3.4 `damage_entity`

| Pin | Direction | Type | Description |
|-----|-----------|------|-------------|
| `exec` | IN | FlowSignal | |
| `target` | IN | Entity | Who to damage |
| `amount` | IN | Number | Damage value |
| `attacker` | IN | Entity | Credit kills to this chr |
| `damage_type` | IN | Integer | 0=bullet, 1=explosion, 2=melee, 3=fire |
| `exec_out` | OUT | FlowSignal | |
| `target_died` | OUT | Boolean | True if this killed them |

---

### 3.5 `damage_radius`

Explosion-style area damage.

| Pin | Direction | Type | Description |
|-----|-----------|------|-------------|
| `exec` | IN | FlowSignal | |
| `center` | IN | Vector3 | Explosion center |
| `radius` | IN | Number | Blast radius |
| `damage` | IN | Number | Max damage at center |
| `falloff` | IN | Number | 0=uniform, 1=linear falloff |
| `attacker` | IN | Entity | Kill credit |
| `exec_out` | OUT | FlowSignal | |
| `hits` | OUT | Integer | Number of entities hit |

---

### 3.6 `spawn_prop`

Deploy a prop into the world (mine, turret, etc.).

| Pin | Direction | Type | Description |
|-----|-----------|------|-------------|
| `exec` | IN | FlowSignal | |
| `model` | IN | ModelRef | Prop model |
| `position` | IN | Vector3 | Spawn position |
| `rotation` | IN | Vector3 | Euler angles |
| `owner` | IN | Entity | Who owns this prop |
| `has_collision` | IN | Boolean | Generate collision (default true) |
| `health` | IN | Number | Prop health (0=indestructible) |
| `exec_out` | OUT | FlowSignal | |
| `prop_id` | OUT | Integer | Runtime prop handle |

---

### 3.7 `set_objective`

| Pin | Direction | Type | Description |
|-----|-----------|------|-------------|
| `exec` | IN | FlowSignal | |
| `index` | IN | Integer | Objective slot (0-based) |
| `text` | IN | String | Objective description |
| `is_complete` | IN | Boolean | Mark as done |
| `exec_out` | OUT | FlowSignal | |

---

### 3.8 `open_door`

| Pin | Direction | Type | Description |
|-----|-----------|------|-------------|
| `exec` | IN | FlowSignal | |
| `door_tag` | IN | String | Door identifier in map |
| `open` | IN | Boolean | True=open, false=close |
| `exec_out` | OUT | FlowSignal | |

---

### 3.9 `destroy_prop`

| Pin | Direction | Type | Description |
|-----|-----------|------|-------------|
| `exec` | IN | FlowSignal | |
| `prop_id` | IN | Integer | Prop handle from `spawn_prop` |
| `explosion` | IN | Boolean | Show explosion effect |
| `exec_out` | OUT | FlowSignal | |

---

### 3.10 `teleport_entity`

| Pin | Direction | Type | Description |
|-----|-----------|------|-------------|
| `exec` | IN | FlowSignal | |
| `entity` | IN | Entity | Who to teleport |
| `position` | IN | Vector3 | Destination |
| `yaw` | IN | Number | Facing direction (degrees) |
| `exec_out` | OUT | FlowSignal | |

---

### 3.11 `remove_weapon`

| Pin | Direction | Type | Description |
|-----|-----------|------|-------------|
| `exec` | IN | FlowSignal | |
| `entity` | IN | Entity | From whom |
| `weapon_id` | IN | CatalogID | Which weapon |
| `exec_out` | OUT | FlowSignal | |

---

### 3.12 `hud_message`

| Pin | Direction | Type | Description |
|-----|-----------|------|-------------|
| `exec` | IN | FlowSignal | |
| `text` | IN | String | Message text |
| `duration` | IN | Number | Seconds to display (default 3.0) |
| `color` | IN | Integer | 0=white, 1=green, 2=red, 3=yellow |
| `player` | IN | Entity | -1 for all players |
| `exec_out` | OUT | FlowSignal | |

---

## Category 4: FLOW CONTROL

### 4.1 `sequence`

Execute multiple output pins in order.

| Pin | Direction | Type | Description |
|-----|-----------|------|-------------|
| `exec` | IN | FlowSignal | |
| `then_0` | OUT | FlowSignal | First action |
| `then_1` | OUT | FlowSignal | Second action |
| `then_2` | OUT | FlowSignal | Third action |
| `then_3` | OUT | FlowSignal | Fourth action |

**Notes**: Executes all connected outputs in order, same tick. Up to 4 outputs (add more in editor).

---

### 4.2 `branch_if`

| Pin | Direction | Type | Description |
|-----|-----------|------|-------------|
| `exec` | IN | FlowSignal | |
| `condition` | IN | Boolean | |
| `true` | OUT | FlowSignal | |
| `false` | OUT | FlowSignal | |

---

### 4.3 `loop_count`

Execute body N times in one tick.

| Pin | Direction | Type | Description |
|-----|-----------|------|-------------|
| `exec` | IN | FlowSignal | |
| `count` | IN | Integer | Number of iterations |
| `body` | OUT | FlowSignal | Fires N times |
| `index` | OUT | Integer | Current iteration (0-based) |
| `completed` | OUT | FlowSignal | After all iterations |

**Usage**: Shotgun spread (fire 8 projectiles in one tick), burst fire.

---

### 4.4 `delay`

Defers execution by a number of seconds.

| Pin | Direction | Type | Description |
|-----|-----------|------|-------------|
| `exec` | IN | FlowSignal | |
| `seconds` | IN | Number | Delay duration |
| `exec_out` | OUT | FlowSignal | Fires after delay |

**Notes**: Registers a pending timer internally. Only one delay per instance active at a time (re-trigger resets).

---

### 4.5 `delay_curve`

Defers execution with a duration sampled from a curve — useful for accelerating fire rates.

| Pin | Direction | Type | Description |
|-----|-----------|------|-------------|
| `exec` | IN | FlowSignal | |
| `curve` | IN | Curve | Time curve: X=trigger count (normalized), Y=delay seconds |
| `progress` | IN | Number | Where to sample on the curve (0.0-1.0) |
| `exec_out` | OUT | FlowSignal | Fires after sampled delay |
| `sampled_delay` | OUT | Number | Actual delay used |

**Usage**: Accelerating fire rate (Needler, Reaper spin-up). Maps `initialrpm` → `maxrpm` ramp from `bondgun.c` lines 1896-1922.

---

### 4.6 `random_branch`

Randomly pick one of several outputs.

| Pin | Direction | Type | Description |
|-----|-----------|------|-------------|
| `exec` | IN | FlowSignal | |
| `weight_0` | IN | Number | Weight for output 0 (default 1.0) |
| `weight_1` | IN | Number | Weight for output 1 (default 1.0) |
| `weight_2` | IN | Number | Weight for output 2 (default 0.0) |
| `out_0` | OUT | FlowSignal | |
| `out_1` | OUT | FlowSignal | |
| `out_2` | OUT | FlowSignal | |
| `chosen` | OUT | Integer | Which branch was taken |

---

### 4.7 `gate_cooldown`

Passes flow through only if enough time has elapsed since last pass. Implements fire rate.

| Pin | Direction | Type | Description |
|-----|-----------|------|-------------|
| `exec` | IN | FlowSignal | |
| `cooldown` | IN | Number | Minimum seconds between passes |
| `exec_out` | OUT | FlowSignal | Only fires if cooldown elapsed |
| `blocked` | OUT | FlowSignal | Fires if still in cooldown |
| `remaining` | OUT | Number | Seconds until next allowed pass |

**Usage**: Semi-auto fire rate limiting (Falcon 2: one shot per trigger pull, cooldown prevents rapid re-fire).

---

### 4.8 `gate_toggle`

Alternates between two states each time triggered.

| Pin | Direction | Type | Description |
|-----|-----------|------|-------------|
| `exec` | IN | FlowSignal | |
| `on` | OUT | FlowSignal | First trigger, third, fifth... |
| `off` | OUT | FlowSignal | Second trigger, fourth, sixth... |
| `is_on` | OUT | Boolean | Current state after toggle |

**Usage**: Deploy/recall laptop gun, arm/disarm.

---

### 4.9 `for_each_entity`

Iterate over an EntityList.

| Pin | Direction | Type | Description |
|-----|-----------|------|-------------|
| `exec` | IN | FlowSignal | |
| `list` | IN | EntityList | Entities to iterate |
| `body` | OUT | FlowSignal | Fires per entity |
| `current` | OUT | Entity | Current entity |
| `index` | OUT | Integer | Current index |
| `completed` | OUT | FlowSignal | After all entities |

---

## Category 5: MATH

Math nodes are pure — no `FlowSignal`, just data in → data out. Evaluated lazily when a downstream node reads the output.

### 5.1 `add`

| Pin | Direction | Type | Description |
|-----|-----------|------|-------------|
| `a` | IN | Number | |
| `b` | IN | Number | |
| `result` | OUT | Number | a + b |

---

### 5.2 `multiply`

| Pin | Direction | Type | Description |
|-----|-----------|------|-------------|
| `a` | IN | Number | |
| `b` | IN | Number | |
| `result` | OUT | Number | a * b |

---

### 5.3 `clamp`

| Pin | Direction | Type | Description |
|-----|-----------|------|-------------|
| `value` | IN | Number | |
| `min` | IN | Number | |
| `max` | IN | Number | |
| `result` | OUT | Number | clamped value |

---

### 5.4 `lerp`

| Pin | Direction | Type | Description |
|-----|-----------|------|-------------|
| `a` | IN | Number | Start value |
| `b` | IN | Number | End value |
| `t` | IN | Number | Interpolation factor 0-1 |
| `result` | OUT | Number | a + (b - a) * t |

---

### 5.5 `curve_sample`

Sample a piecewise-linear curve at a given t value.

| Pin | Direction | Type | Description |
|-----|-----------|------|-------------|
| `curve` | IN | Curve | 16-point curve |
| `t` | IN | Number | Sample position 0.0-1.0 |
| `result` | OUT | Number | Interpolated value |

**Usage**: Fire rate ramps, damage falloff curves, charge-to-damage mapping.

---

### 5.6 `random_range`

| Pin | Direction | Type | Description |
|-----|-----------|------|-------------|
| `min` | IN | Number | |
| `max` | IN | Number | |
| `result` | OUT | Number | Random value in [min, max] |

**Notes**: Uses game RNG (`rng.h`) for deterministic replay.

---

### 5.7 `distance`

| Pin | Direction | Type | Description |
|-----|-----------|------|-------------|
| `a` | IN | Vector3 | |
| `b` | IN | Vector3 | |
| `result` | OUT | Number | Euclidean distance |

---

### 5.8 `vector_add`

| Pin | Direction | Type | Description |
|-----|-----------|------|-------------|
| `a` | IN | Vector3 | |
| `b` | IN | Vector3 | |
| `result` | OUT | Vector3 | Component-wise sum |

---

### 5.9 `vector_scale`

| Pin | Direction | Type | Description |
|-----|-----------|------|-------------|
| `vec` | IN | Vector3 | |
| `scalar` | IN | Number | |
| `result` | OUT | Vector3 | vec * scalar |

---

### 5.10 `vector_normalize`

| Pin | Direction | Type | Description |
|-----|-----------|------|-------------|
| `vec` | IN | Vector3 | |
| `result` | OUT | Vector3 | Unit length direction |
| `length` | OUT | Number | Original magnitude |

---

### 5.11 `direction_to`

| Pin | Direction | Type | Description |
|-----|-----------|------|-------------|
| `from` | IN | Vector3 | |
| `to` | IN | Vector3 | |
| `direction` | OUT | Vector3 | Normalized direction from→to |
| `distance` | OUT | Number | Distance between |

---

### 5.12 `subtract`

| Pin | Direction | Type | Description |
|-----|-----------|------|-------------|
| `a` | IN | Number | |
| `b` | IN | Number | |
| `result` | OUT | Number | a - b |

---

### 5.13 `divide`

| Pin | Direction | Type | Description |
|-----|-----------|------|-------------|
| `a` | IN | Number | |
| `b` | IN | Number | |
| `result` | OUT | Number | a / b (safe: returns 0 if b==0) |

---

### 5.14 `compare`

| Pin | Direction | Type | Description |
|-----|-----------|------|-------------|
| `a` | IN | Number | |
| `b` | IN | Number | |
| `equal` | OUT | Boolean | |abs(a-b)| < 0.001 |
| `greater` | OUT | Boolean | a > b |
| `less` | OUT | Boolean | a < b |

---

### 5.15 `random_direction_in_cone`

Generate a random direction within a cone — for weapon spread.

| Pin | Direction | Type | Description |
|-----|-----------|------|-------------|
| `base_dir` | IN | Vector3 | Center of cone |
| `half_angle` | IN | Number | Cone half-angle in degrees |
| `result` | OUT | Vector3 | Random direction within cone |

---

## Category 6: ENTITY REFERENCE

### 6.1 `get_self`

Returns the owner of this weapon/script.

| Pin | Direction | Type | Description |
|-----|-----------|------|-------------|
| `entity` | OUT | Entity | Weapon owner chr |
| `position` | OUT | Vector3 | Owner's current position |
| `health` | OUT | Number | Owner's health ratio 0-1 |
| `team` | OUT | Integer | Owner's team index |

---

### 6.2 `get_attacker`

Returns the chr that last damaged the owner. Only valid inside `on_hit` or `on_death` context.

| Pin | Direction | Type | Description |
|-----|-----------|------|-------------|
| `entity` | OUT | Entity | Last attacker (-1 if none) |
| `position` | OUT | Vector3 | Attacker's position |

---

### 6.3 `get_nearest_enemy`

Find closest hostile chr to a position.

| Pin | Direction | Type | Description |
|-----|-----------|------|-------------|
| `position` | IN | Vector3 | Search origin |
| `max_range` | IN | Number | Max search radius |
| `require_los` | IN | Boolean | Must have line of sight |
| `entity` | OUT | Entity | Nearest enemy (-1 if none) |
| `distance` | OUT | Number | Distance to nearest |
| `direction` | OUT | Vector3 | Direction toward nearest |

**Notes**: Uses team-filtered chr iteration + optional `cdTestLos05`. Mirrors the laptop gun targeting loop (`propobj.c` lines 8785-8850).

---

### 6.4 `get_all_in_radius`

| Pin | Direction | Type | Description |
|-----|-----------|------|-------------|
| `position` | IN | Vector3 | Center |
| `radius` | IN | Number | Search radius |
| `include_friendly` | IN | Boolean | Include same-team |
| `entities` | OUT | EntityList | All chrs in radius |
| `count` | OUT | Integer | Number found |

---

### 6.5 `filter_by_team`

| Pin | Direction | Type | Description |
|-----|-----------|------|-------------|
| `list` | IN | EntityList | Input entities |
| `team` | IN | Integer | Team to keep |
| `invert` | IN | Boolean | True = exclude this team |
| `result` | OUT | EntityList | Filtered list |
| `count` | OUT | Integer | |

---

### 6.6 `get_entity_position`

| Pin | Direction | Type | Description |
|-----|-----------|------|-------------|
| `entity` | IN | Entity | |
| `position` | OUT | Vector3 | World position |
| `forward` | OUT | Vector3 | Facing direction |
| `velocity` | OUT | Vector3 | Movement velocity |

---

### 6.7 `get_entity_health`

| Pin | Direction | Type | Description |
|-----|-----------|------|-------------|
| `entity` | IN | Entity | |
| `health` | OUT | Number | 0.0-1.0 normalized |
| `health_raw` | OUT | Number | Absolute HP |
| `is_alive` | OUT | Boolean | |

---

### 6.8 `get_projectile_position`

| Pin | Direction | Type | Description |
|-----|-----------|------|-------------|
| `projectile_id` | IN | Integer | Pool index |
| `position` | OUT | Vector3 | Current position |
| `velocity` | OUT | Vector3 | Current velocity |
| `age` | OUT | Number | Seconds since spawn |

---

## Category 7: WEAPON

Specialized nodes for weapon behaviors that map to the systems in `bondgun.c` and `propobj.c`.

### 7.1 `burst_fire`

Fires N projectiles in rapid succession from a single trigger pull. Models the burst system from `bondgun.c` (FUNCFLAG_BURST2/BURST3/BURST5).

| Pin | Direction | Type | Description |
|-----|-----------|------|-------------|
| `exec` | IN | FlowSignal | |
| `count` | IN | Integer | Shots per burst (2, 3, 5, etc.) |
| `interval` | IN | Number | Seconds between burst shots |
| `origin` | IN | Vector3 | Muzzle position |
| `direction` | IN | Vector3 | Aim direction |
| `spread_per_shot` | IN | Number | Added spread per successive shot (degrees) |
| `per_shot` | OUT | FlowSignal | Fires for each shot in burst |
| `shot_index` | OUT | Integer | 0-based index in burst |
| `shot_direction` | OUT | Vector3 | Direction with spread applied |
| `burst_complete` | OUT | FlowSignal | After all shots fired |

---

### 7.2 `charge_hold`

Accumulates charge while trigger is held, outputs charge level. Models Mauler charge (`bondgun.c` lines 8124-8199: `hand->matmot1` 0-5 range, +0.05/frame, costs ammo per level).

| Pin | Direction | Type | Description |
|-----|-----------|------|-------------|
| `exec` | IN | FlowSignal | Call every tick while held |
| `charge_rate` | IN | Number | Units per second (Mauler: 3.0) |
| `max_charge` | IN | Number | Maximum charge level (Mauler: 5.0) |
| `ammo_per_level` | IN | Integer | Ammo consumed per charge level |
| `decay_rate` | IN | Number | Discharge rate when not held (Mauler: 0.3/sec) |
| `exec_out` | OUT | FlowSignal | Every tick during charge |
| `charge_level` | OUT | Number | Current charge 0 to max_charge |
| `charge_normalized` | OUT | Number | 0.0-1.0 |
| `fully_charged` | OUT | Boolean | |

---

### 7.3 `deploy_as_prop`

Deploy current weapon as a world prop (like Dragon mine or Laptop turret). Models `laptopDeploy()` in `propobj.c` line 18680.

| Pin | Direction | Type | Description |
|-----|-----------|------|-------------|
| `exec` | IN | FlowSignal | |
| `model` | IN | ModelRef | Deployed prop model |
| `position` | IN | Vector3 | Deploy position |
| `normal` | IN | Vector3 | Surface normal for orientation |
| `owner` | IN | Entity | |
| `health` | IN | Number | Prop HP (0=indestructible) |
| `remove_from_inventory` | IN | Boolean | Take weapon from player |
| `exec_out` | OUT | FlowSignal | |
| `prop_id` | OUT | Integer | Deployed prop handle |

---

### 7.4 `turret_aim`

Make a deployed prop track and fire at enemies. Models laptop gun turret (`propobj.c` lines 8760-9008).

| Pin | Direction | Type | Description |
|-----|-----------|------|-------------|
| `exec` | IN | FlowSignal | Call every tick |
| `prop_id` | IN | Integer | Turret prop handle |
| `aim_range` | IN | Number | Max targeting distance |
| `turn_speed` | IN | Number | Degrees per second |
| `team` | IN | Integer | Owner's team (-1=attack all non-owner) |
| `require_los` | IN | Boolean | Line-of-sight required |
| `exec_out` | OUT | FlowSignal | Every tick |
| `has_target` | OUT | Boolean | Currently tracking someone |
| `target` | OUT | Entity | Current target chr |
| `aim_direction` | OUT | Vector3 | Where turret is aiming |
| `on_target` | OUT | FlowSignal | Fires when aim converges on target |

---

### 7.5 `scope_zoom`

Activate weapon scope/zoom. Models the zoom system from `bondgun.c` lines 5507-5510, plus Farsight X-ray vision.

| Pin | Direction | Type | Description |
|-----|-----------|------|-------------|
| `exec` | IN | FlowSignal | |
| `zoom_level` | IN | Number | FOV divisor (1=none, 4=sniper, 8=Farsight) |
| `zoom_speed` | IN | Number | Seconds to reach full zoom |
| `vision_mode` | IN | Integer | 0=normal, 1=xray, 2=thermal |
| `exec_out` | OUT | FlowSignal | |
| `current_zoom` | OUT | Number | Actual current zoom factor |

---

### 7.6 `tracking_seek`

Apply homing behavior to an active projectile. Steers velocity toward target. Models Slayer rocket guidance (`propobj.c` lines 6906-6930).

| Pin | Direction | Type | Description |
|-----|-----------|------|-------------|
| `exec` | IN | FlowSignal | Call every tick for active projectile |
| `projectile_id` | IN | Integer | Which projectile to steer |
| `target` | IN | Entity | Target to home toward |
| `turn_rate` | IN | Number | Max degrees per second |
| `strength` | IN | Number | 0-1 tracking aggression |
| `exec_out` | OUT | FlowSignal | |
| `angle_to_target` | OUT | Number | Current angle offset (degrees) |

---

### 7.7 `accumulate_on_target`

Register a hit as an accumulated projectile on a target chr. Models the needler system from `studio-platform-design.md` Layer 3.

| Pin | Direction | Type | Description |
|-----|-----------|------|-------------|
| `exec` | IN | FlowSignal | |
| `target` | IN | Entity | Chr to accumulate on |
| `weapon_slot` | IN | Integer | Weapon identifier for grouping |
| `exec_out` | OUT | FlowSignal | |
| `new_count` | OUT | Integer | Total accumulated on this target |

---

### 7.8 `detonate_accumulated`

Explode all accumulated projectiles on a target. Removes them and applies area damage.

| Pin | Direction | Type | Description |
|-----|-----------|------|-------------|
| `exec` | IN | FlowSignal | |
| `target` | IN | Entity | Target chr |
| `weapon_slot` | IN | Integer | Which weapon's accumulants |
| `damage_per` | IN | Number | Damage per accumulated projectile |
| `radius` | IN | Number | Explosion radius |
| `effect` | IN | CatalogID | Explosion VFX |
| `exec_out` | OUT | FlowSignal | |
| `total_damage` | OUT | Number | Actual damage dealt |

---

### 7.9 `penetrate`

Fire a hitscan ray that passes through walls and entities. Models Farsight wall-penetration (uses `weaponfunc_shoot.penetration` field from `types.h` line 2952).

| Pin | Direction | Type | Description |
|-----|-----------|------|-------------|
| `exec` | IN | FlowSignal | |
| `origin` | IN | Vector3 | Ray start |
| `direction` | IN | Vector3 | Ray direction |
| `range` | IN | Number | Max ray distance |
| `damage` | IN | Number | Damage to each entity hit |
| `damage_falloff` | IN | Number | Multiplier per wall/entity penetrated |
| `max_penetrations` | IN | Integer | Max walls/entities to pass through |
| `exec_out` | OUT | FlowSignal | |
| `hit_count` | OUT | Integer | Total entities hit |
| `first_hit` | OUT | Entity | First entity in ray |

---

### 7.10 `guided_control`

Give player direct steering control of an active projectile. Models the Slayer fly-by-wire: `VISIONMODE_SLAYERROCKET` (line 2261).

| Pin | Direction | Type | Description |
|-----|-----------|------|-------------|
| `exec` | IN | FlowSignal | |
| `projectile_id` | IN | Integer | Projectile to control |
| `steer_speed` | IN | Number | Degrees per second of player control |
| `camera_follow` | IN | Boolean | Switch camera to projectile POV |
| `exec_out` | OUT | FlowSignal | Fires when control starts |
| `on_impact` | OUT | FlowSignal | Fires when projectile hits |

---

### 7.11 `spin_up`

Reaper-style spin-up: fire rate ramps while trigger is held. Models `turretaccel`/`turretdecel` from `weaponfunc_shootauto` and `gs_float1` ramp in `bondgun.c` lines 1807-1876.

| Pin | Direction | Type | Description |
|-----|-----------|------|-------------|
| `exec` | IN | FlowSignal | Call every tick while held |
| `accel_time` | IN | Number | Seconds to reach max RPM |
| `decel_time` | IN | Number | Seconds to spin down |
| `initial_rpm` | IN | Number | Starting fire rate |
| `max_rpm` | IN | Number | Peak fire rate |
| `exec_out` | OUT | FlowSignal | |
| `current_rpm` | OUT | Number | Current fire rate |
| `spin_fraction` | OUT | Number | 0.0-1.0 ramp progress |
| `should_fire` | OUT | Boolean | True when a shot should fire this tick |

---

### 7.12 `consume_ammo`

Deduct ammo from the weapon. Separate node so charge weapons can consume variable amounts.

| Pin | Direction | Type | Description |
|-----|-----------|------|-------------|
| `exec` | IN | FlowSignal | |
| `amount` | IN | Integer | Rounds to consume |
| `exec_out` | OUT | FlowSignal | |
| `remaining` | OUT | Integer | Ammo after consumption |
| `depleted` | OUT | Boolean | True if ammo hit 0 |

---

## Category 8: AUDIO/VISUAL

### 8.1 `play_sound_3d`

Positional 3D audio.

| Pin | Direction | Type | Description |
|-----|-----------|------|-------------|
| `exec` | IN | FlowSignal | |
| `sound` | IN | AudioRef | Catalog ID |
| `position` | IN | Vector3 | World position |
| `volume` | IN | Number | 0.0-1.0 |
| `pitch` | IN | Number | 0.5-2.0 |
| `loop` | IN | Boolean | Looping sound |
| `exec_out` | OUT | FlowSignal | |
| `handle` | OUT | Integer | Audio handle for later stop |

---

### 8.2 `stop_sound`

| Pin | Direction | Type | Description |
|-----|-----------|------|-------------|
| `exec` | IN | FlowSignal | |
| `handle` | IN | Integer | From play_sound_3d |
| `exec_out` | OUT | FlowSignal | |

---

### 8.3 `camera_shake`

| Pin | Direction | Type | Description |
|-----|-----------|------|-------------|
| `exec` | IN | FlowSignal | |
| `intensity` | IN | Number | Shake strength 0-1 |
| `duration` | IN | Number | Seconds |
| `player` | IN | Entity | -1 for all nearby players |
| `falloff_radius` | IN | Number | 0 = no distance falloff |
| `exec_out` | OUT | FlowSignal | |

---

### 8.4 `spawn_effect`

Spawn a visual effect (explosion, sparks, smoke trail).

| Pin | Direction | Type | Description |
|-----|-----------|------|-------------|
| `exec` | IN | FlowSignal | |
| `effect_id` | IN | CatalogID | Effect asset |
| `position` | IN | Vector3 | World position |
| `normal` | IN | Vector3 | Effect orientation |
| `scale` | IN | Number | Size multiplier |
| `exec_out` | OUT | FlowSignal | |

---

### 8.5 `play_animation`

Play an animation on an entity's model.

| Pin | Direction | Type | Description |
|-----|-----------|------|-------------|
| `exec` | IN | FlowSignal | |
| `entity` | IN | Entity | Target chr |
| `anim_id` | IN | CatalogID | Animation asset |
| `speed` | IN | Number | Playback speed (1.0 = normal) |
| `loop` | IN | Boolean | |
| `exec_out` | OUT | FlowSignal | |

---

### 8.6 `spawn_decal`

| Pin | Direction | Type | Description |
|-----|-----------|------|-------------|
| `exec` | IN | FlowSignal | |
| `texture` | IN | CatalogID | Decal texture |
| `position` | IN | Vector3 | |
| `normal` | IN | Vector3 | Surface normal |
| `size` | IN | Number | Decal radius |
| `lifetime` | IN | Number | Seconds before fade |
| `exec_out` | OUT | FlowSignal | |

---

### 8.7 `controller_vibrate`

| Pin | Direction | Type | Description |
|-----|-----------|------|-------------|
| `exec` | IN | FlowSignal | |
| `player` | IN | Entity | |
| `intensity` | IN | Number | 0-1 |
| `duration` | IN | Number | Seconds |
| `exec_out` | OUT | FlowSignal | |

---

## Category 9: MISSION FLOW

Nodes for scripting single-player and co-op mission logic.

### 9.1 `start_mission`

Initialize a mission with objectives.

| Pin | Direction | Type | Description |
|-----|-----------|------|-------------|
| `exec` | IN | FlowSignal | |
| `mission_name` | IN | String | Display name |
| `briefing` | IN | String | Briefing text |
| `objective_count` | IN | Integer | Number of objectives (1-6) |
| `exec_out` | OUT | FlowSignal | |

---

### 9.2 `set_objective_text`

| Pin | Direction | Type | Description |
|-----|-----------|------|-------------|
| `exec` | IN | FlowSignal | |
| `index` | IN | Integer | Objective slot |
| `text` | IN | String | Objective description |
| `show_notification` | IN | Boolean | Flash on-screen |
| `exec_out` | OUT | FlowSignal | |

---

### 9.3 `mark_objective_complete`

| Pin | Direction | Type | Description |
|-----|-----------|------|-------------|
| `exec` | IN | FlowSignal | |
| `index` | IN | Integer | Objective slot |
| `exec_out` | OUT | FlowSignal | |
| `all_complete` | OUT | Boolean | True if all objectives done |

---

### 9.4 `fail_mission`

| Pin | Direction | Type | Description |
|-----|-----------|------|-------------|
| `exec` | IN | FlowSignal | |
| `reason` | IN | String | Failure message |

**Notes**: Terminal node. No output flow — mission ends.

---

### 9.5 `complete_mission`

| Pin | Direction | Type | Description |
|-----|-----------|------|-------------|
| `exec` | IN | FlowSignal | |
| `rating` | IN | Integer | 0=no rating, 1-5 stars |
| `unlock_id` | IN | CatalogID | Asset to unlock (empty=none) |

---

### 9.6 `start_cutscene`

| Pin | Direction | Type | Description |
|-----|-----------|------|-------------|
| `exec` | IN | FlowSignal | |
| `camera_path` | IN | CatalogID | Camera animation |
| `duration` | IN | Number | Seconds |
| `skip_allowed` | IN | Boolean | Player can press skip |
| `exec_out` | OUT | FlowSignal | After cutscene ends/skipped |

---

### 9.7 `advance_stage`

Load a new stage/level.

| Pin | Direction | Type | Description |
|-----|-----------|------|-------------|
| `exec` | IN | FlowSignal | |
| `stage_id` | IN | CatalogID | Target stage catalog ID |
| `exec_out` | OUT | FlowSignal | Never fires (stage change resets graph) |

**Notes**: Uses `catalogResolveStage()` + `mainChangeToStage()` per catalog-first constraint.

---

### 9.8 `transfer_persistent_data`

Save data that persists across stage transitions within a mission.

| Pin | Direction | Type | Description |
|-----|-----------|------|-------------|
| `exec` | IN | FlowSignal | |
| `key` | IN | String | Variable name |
| `value` | IN | Number | Value to persist |
| `exec_out` | OUT | FlowSignal | |

---

### 9.9 `spawn_chr`

Spawn an NPC or enemy character.

| Pin | Direction | Type | Description |
|-----|-----------|------|-------------|
| `exec` | IN | FlowSignal | |
| `body_id` | IN | CatalogID | Character body |
| `head_id` | IN | CatalogID | Character head |
| `position` | IN | Vector3 | Spawn location |
| `yaw` | IN | Number | Facing direction |
| `team` | IN | Integer | Team assignment |
| `weapon_id` | IN | CatalogID | Starting weapon (empty=unarmed) |
| `health` | IN | Number | Health multiplier (1.0=normal) |
| `exec_out` | OUT | FlowSignal | |
| `entity` | OUT | Entity | Spawned chr |

---

### 9.10 `set_chr_behavior`

Assign AI behavior to an NPC.

| Pin | Direction | Type | Description |
|-----|-----------|------|-------------|
| `exec` | IN | FlowSignal | |
| `entity` | IN | Entity | Target NPC |
| `behavior` | IN | Integer | 0=idle, 1=patrol, 2=guard, 3=attack, 4=flee |
| `target` | IN | Entity | Who to guard/attack (-1 for auto) |
| `exec_out` | OUT | FlowSignal | |

---

## Category 10: VARIABLES

### 10.1 `set_local_var`

Set a named variable in the graph's local scope.

| Pin | Direction | Type | Description |
|-----|-----------|------|-------------|
| `exec` | IN | FlowSignal | |
| `name` | IN | String | Variable name |
| `value` | IN | Number | Value to store |
| `exec_out` | OUT | FlowSignal | |

---

### 10.2 `get_local_var`

Read a local variable. Pure data node (no flow).

| Pin | Direction | Type | Description |
|-----|-----------|------|-------------|
| `name` | IN | String | Variable name |
| `value` | OUT | Number | Current value (0.0 if unset) |

---

### 10.3 `set_entity_var`

Store a variable on a specific entity (persists with that chr).

| Pin | Direction | Type | Description |
|-----|-----------|------|-------------|
| `exec` | IN | FlowSignal | |
| `entity` | IN | Entity | Target chr |
| `name` | IN | String | Variable name |
| `value` | IN | Number | Value |
| `exec_out` | OUT | FlowSignal | |

---

### 10.4 `get_entity_var`

| Pin | Direction | Type | Description |
|-----|-----------|------|-------------|
| `entity` | IN | Entity | Target chr |
| `name` | IN | String | Variable name |
| `value` | OUT | Number | Stored value (0.0 if unset) |

---

### 10.5 `increment_counter`

Atomic increment-and-read for a named counter.

| Pin | Direction | Type | Description |
|-----|-----------|------|-------------|
| `exec` | IN | FlowSignal | |
| `name` | IN | String | Counter name |
| `amount` | IN | Integer | Increment by (default 1) |
| `exec_out` | OUT | FlowSignal | |
| `new_value` | OUT | Integer | Value after increment |

---

### 10.6 `timer_start`

Start a named timer that fires `on_timer` after duration.

| Pin | Direction | Type | Description |
|-----|-----------|------|-------------|
| `exec` | IN | FlowSignal | |
| `timer_id` | IN | Integer | Timer slot (0-7) |
| `duration` | IN | Number | Seconds until fire |
| `repeat` | IN | Boolean | Auto-restart after fire |
| `exec_out` | OUT | FlowSignal | |

---

### 10.7 `timer_cancel`

| Pin | Direction | Type | Description |
|-----|-----------|------|-------------|
| `exec` | IN | FlowSignal | |
| `timer_id` | IN | Integer | Timer slot to cancel |
| `exec_out` | OUT | FlowSignal | |

---

### 10.8 `set_local_var_string`

| Pin | Direction | Type | Description |
|-----|-----------|------|-------------|
| `exec` | IN | FlowSignal | |
| `name` | IN | String | Variable name |
| `value` | IN | String | String value |
| `exec_out` | OUT | FlowSignal | |

---

### 10.9 `get_local_var_string`

| Pin | Direction | Type | Description |
|-----|-----------|------|-------------|
| `name` | IN | String | Variable name |
| `value` | OUT | String | Current value ("" if unset) |

---

## Proof Graphs

### Falcon 2 — Semi-Automatic Pistol

Single shot per trigger pull, fixed damage, no special behavior.

```
[on_fire] ──exec──> [gate_cooldown]
  |aim_dir              |cooldown: 0.15 (400 RPM)
  |aim_origin           |
  |owner                |exec_out──> [fire_projectile]
  └──────────────────────────────>     |origin: <aim_origin>
                                       |direction: <aim_dir>
                                       |speed: 3000
                                       |damage: 8.0
                                       |lifetime: 0.5
                                       |gravity: 0.0
                                       |radius: 0.0
                                       |model: (empty = hitscan)
                                       |
                                       |exec_out──> [play_sound]
                                       |              |sound: "base:falcon2_fire"
                                       |              |
                                       |              |exec_out──> [consume_ammo]
                                       |                            |amount: 1
                                       |
                                       └projectile_id

[on_hit] ──exec──> [branch_if]
  |hit_is_chr            |condition: <hit_is_chr>
  |hit_entity            |
  |hit_position          |true──> [damage_entity]
  |hit_normal            |          |target: <hit_entity>
                         |          |amount: 8.0
                         |          |attacker: <owner>
                         |
                         |false──> [spawn_effect]
                                    |effect_id: "base:bullet_impact"
                                    |position: <hit_position>
                                    |normal: <hit_normal>
```

### CMP-150 — Automatic + Lock-On Secondary

Primary: automatic fire with RPM ramp. Secondary: lock-on tracking burst.

```
=== PRIMARY FIRE MODE ===

[on_fire] ──exec──> [branch_if]
  |fire_mode             |condition: (fire_mode == 0)
  |aim_dir               |
  |aim_origin            |true──> [spin_up]
  |owner                 |          |accel_time: 0.5
                         |          |decel_time: 0.3
                         |          |initial_rpm: 600
                         |          |max_rpm: 900
                         |          |
                         |          |should_fire──> [branch_if]
                         |                           |condition: <should_fire>
                         |                           |true──> [random_direction_in_cone]
                         |                           |          |base_dir: <aim_dir>
                         |                           |          |half_angle: 2.0
                         |                           |          |
                         |                           |          |result──> [fire_projectile]
                         |                           |                      |speed: 3000
                         |                           |                      |damage: 4.0
                         |                           |                      |exec_out──> [sequence]
                         |                           |                                    |then_0──> [play_sound] "base:cmp150_fire"
                         |                           |                                    |then_1──> [consume_ammo] amount:1
                         |                           |                                    |then_2──> [controller_vibrate] intensity:0.2
                         |
                         |false──> [SECONDARY: LOCK-ON BURST — see below]

=== SECONDARY FIRE MODE (fire_mode == 1) ===

[on_fire (mode 1)] ──exec──> [get_nearest_enemy]
                                |position: <aim_origin>
                                |max_range: 2000
                                |require_los: true
                                |
                                |entity──> [burst_fire]
                                |            |count: 5
                                |            |interval: 0.05
                                |            |direction: <aim_dir>
                                |            |
                                |            |per_shot──> [direction_to]
                                |            |              |from: <aim_origin>
                                |            |              |to: <get_entity_position(target).position>
                                |            |              |
                                |            |              |direction──> [fire_projectile]
                                |            |                            |speed: 2500
                                |            |                            |damage: 3.0
                                |            |
                                |            |burst_complete──> [consume_ammo] amount:5
```

### Dragon — Automatic + Proximity Mine Deploy

Primary: standard automatic. Secondary: throw as proximity mine.

```
=== PRIMARY: AUTOMATIC ===

[on_fire (mode 0)] ──exec──> [spin_up]
                               |initial_rpm: 600
                               |max_rpm: 900
                               |should_fire──> [fire_projectile]
                                                |speed: 3000, damage: 5.0
                                                |exec_out──> [consume_ammo] amount:1

=== SECONDARY: PROXIMITY MINE ===

[on_fire (mode 1)] ──exec──> [deploy_as_prop]
                               |model: "base:dragon_mine_model"
                               |position: <aim_origin> + <aim_dir> * 50
                               |owner: <owner>
                               |health: 10.0
                               |remove_from_inventory: true
                               |
                               |prop_id──> [set_local_var] name:"mine_id", value:<prop_id>

[on_proximity]  <──position: <deploy_position>
  |radius: 250            (matches propobj.c mine check)
  |team_filter: -1
  |
  |exec──> [sequence]
            |then_0──> [damage_radius]
            |            |center: <deploy_position>
            |            |radius: 400
            |            |damage: 100.0
            |            |attacker: <owner>
            |
            |then_1──> [spawn_effect] "base:explosion_mine"
            |then_2──> [play_sound_3d] "base:mine_explode"
            |then_3──> [destroy_prop] prop_id:<mine_id>
```

### Laptop Gun — Automatic + Turret Deploy

Primary: automatic fire. Secondary: throw as auto-targeting turret.

```
=== PRIMARY: AUTOMATIC ===
(Same as Dragon primary with different RPM values)

=== SECONDARY: TURRET DEPLOY ===

[on_secondary_activate] ──exec──> [gate_toggle]
                                    |on──> [DEPLOY TURRET]
                                    |off──> [RECALL TURRET]

--- DEPLOY ---
[deploy_as_prop]
  |model: "base:laptop_turret_model"
  |position: <aim_origin> + <aim_dir> * 80
  |health: 50.0
  |remove_from_inventory: true
  |prop_id──> [set_local_var] "turret_id"

--- TURRET TICK (runs every frame while deployed) ---
[on_timer] (timer_id:0, repeat:true, duration: 0.016)
  ──exec──> [turret_aim]
              |prop_id: <get_local_var "turret_id">
              |aim_range: 2000
              |turn_speed: 90
              |team: <owner team>
              |
              |on_target──> [gate_cooldown]
                              |cooldown: 0.066 (900 RPM)
                              |exec_out──> [fire_projectile]
                                            |origin: <turret position>
                                            |direction: <aim_direction>
                                            |speed: 3000, damage: 3.0
                                            |exec_out──> [play_sound_3d]
                                                          |sound: "base:laptop_fire"
                                                          |position: <turret position>

--- RECALL ---
[destroy_prop] prop_id: <get_local_var "turret_id">
[give_weapon] entity:<owner>, weapon_id:"base:laptop_gun"
```

### Farsight — Wall-Penetrating Sniper

Primary: penetrating hitscan that goes through walls. Secondary: X-ray scope.

```
=== SECONDARY: X-RAY SCOPE (toggle) ===

[on_fire (mode 1)] ──exec──> [scope_zoom]
                               |zoom_level: 8.0
                               |zoom_speed: 0.3
                               |vision_mode: 1  (xray)

=== PRIMARY: WALL-PENETRATING SHOT ===

[on_fire (mode 0)] ──exec──> [gate_cooldown]
                               |cooldown: 2.0 (slow fire rate)
                               |
                               |exec_out──> [penetrate]
                               |              |origin: <aim_origin>
                               |              |direction: <aim_dir>
                               |              |range: 10000
                               |              |damage: 80.0
                               |              |damage_falloff: 0.7 (30% per wall)
                               |              |max_penetrations: 5
                               |              |
                               |              |exec_out──> [sequence]
                               |                            |then_0──> [play_sound] "base:farsight_fire"
                               |                            |then_1──> [consume_ammo] amount:1
                               |                            |then_2──> [camera_shake] intensity:0.8, duration:0.3
```

### Slayer — Guided Rocket

Primary: standard homing rocket. Secondary: fly-by-wire guided missile.

```
=== PRIMARY: HOMING ROCKET ===

[on_fire (mode 0)] ──exec──> [fire_projectile]
                               |speed: 500
                               |damage: 0 (damage on impact via on_hit)
                               |lifetime: 5.0
                               |gravity: 0
                               |model: "base:rocket_model"
                               |
                               |projectile_id──> [set_local_var] "rocket_id"

--- Tracking tick (every frame while projectile alive) ---
[on_timer] (repeat, 0.016s)
  ──exec──> [get_nearest_enemy]
              |position: <get_projectile_position(<rocket_id>).position>
              |max_range: 2000
              |
              |entity──> [tracking_seek]
                           |projectile_id: <rocket_id>
                           |target: <entity>
                           |turn_rate: 90
                           |strength: 0.6

[on_hit] ──exec──> [sequence]
                    |then_0──> [damage_radius]
                    |            |center: <hit_position>, radius: 300, damage: 100
                    |then_1──> [spawn_effect] "base:rocket_explosion"
                    |then_2──> [play_sound_3d] "base:rocket_explode"
                    |then_3──> [camera_shake] intensity: 1.0, duration: 0.5

=== SECONDARY: FLY-BY-WIRE ===

[on_fire (mode 1)] ──exec──> [fire_projectile]
                               |speed: 300 (slower for controllability)
                               |damage: 0
                               |lifetime: 8.0
                               |model: "base:rocket_model"
                               |
                               |projectile_id──> [guided_control]
                                                   |steer_speed: 120
                                                   |camera_follow: true
                                                   |
                                                   |on_impact──> [damage_radius]
                                                                  |center: <hit_pos>
                                                                  |radius: 300
                                                                  |damage: 120
```

### Needler — Tracking + Accumulation + Detonation + Accelerating Fire Rate

The most complex weapon — demonstrates tracking projectiles, accumulation system, threshold detonation, and fire rate that increases the longer you hold the trigger.

```
=== FIRE RATE CURVE ===

Curve "fire_rate_curve": { (0.0, 0.15), (0.3, 0.12), (0.6, 0.08), (1.0, 0.05) }
  // Starts at ~400 RPM, ramps to ~1200 RPM over ~3 seconds of sustained fire

=== PRIMARY: TRACKING NEEDLES ===

[on_trigger_held] ──exec──> [increment_counter] name:"fire_ticks"
                              |new_value──> [divide] a:<new_value>, b:180 (3 sec * 60 Hz)
                                             |result──> [clamp] min:0, max:1
                                                         |result = <ramp_t>

[on_fire (mode 0)] ──exec──> [delay_curve]
                               |curve: <fire_rate_curve>
                               |progress: <ramp_t>
                               |
                               |exec_out──> [get_nearest_enemy]
                               |              |position: <aim_origin>
                               |              |max_range: 500
                               |              |require_los: true
                               |              |
                               |              |entity = <target>

                               [random_direction_in_cone]
                                 |base_dir: <aim_dir>
                                 |half_angle: 3.0
                                 |result──> [fire_projectile]
                                             |speed: 800
                                             |damage: 5.0
                                             |lifetime: 3.0
                                             |gravity: 0
                                             |radius: 5.0
                                             |model: "mymod:needle_proj"
                                             |
                                             |projectile_id──> [tracking_seek]
                                             |                   |target: <target>
                                             |                   |turn_rate: 180
                                             |                   |strength: 0.8
                                             |
                                             |exec_out──> [sequence]
                                                           |then_0──> [play_sound_3d] "mymod:needle_fire"
                                                           |then_1──> [consume_ammo] amount:1

[on_trigger_released] ──exec──> [set_local_var] name:"fire_ticks", value:0
                                 (reset fire rate ramp)

=== ACCUMULATION ON HIT ===

[on_hit] ──exec──> [branch_if]
                    |condition: <hit_is_chr>
                    |
                    |true──> [accumulate_on_target]
                    |          |target: <hit_entity>
                    |          |weapon_slot: 128 (STUDIO_WEAPON_BASE)
                    |          |
                    |          |new_count──> [if_count_exceeds]
                    |          |              |value: <new_count>
                    |          |              |threshold: 7
                    |          |              |
                    |          |              |true──> [DETONATE]
                    |          |              |false──> [play_sound_3d]
                    |          |                        |sound: "mymod:needle_stick"
                    |          |                        |position: <hit_position>
                    |
                    |false──> [spawn_effect] "mymod:needle_impact"

=== DETONATE (threshold reached) ===

[detonate_accumulated]
  |target: <hit_entity>
  |weapon_slot: 128
  |damage_per: 15.0  (7 * 15 = 105 total)
  |radius: 200
  |effect: "mymod:needle_explode"
  |
  |exec_out──> [sequence]
                |then_0──> [play_sound_3d] "mymod:needler_detonate"
                |then_1──> [camera_shake] intensity:0.6, duration:0.3

=== SECONDARY: THROW + DETONATE ALL ===

[on_fire (mode 1)] ──exec──> [get_all_in_radius]
                               |position: <aim_origin>
                               |radius: 5000
                               |
                               |entities──> [for_each_entity]
                                             |body──> [detonate_accumulated]
                                                       |target: <current>
                                                       |weapon_slot: 128
                                                       |damage_per: 15.0
                                                       |radius: 200
```

### Full Mission Flow — "Extraction Protocol"

A complete single-player mission demonstrating mission flow nodes.

```
=== MISSION INITIALIZATION ===

[on_match_start] ──exec──> [start_mission]
                             |mission_name: "Extraction Protocol"
                             |briefing: "Infiltrate the dataDyne lab and extract Dr. Carroll"
                             |objective_count: 3
                             |
                             |exec_out──> [sequence]
                                           |then_0──> [set_objective_text]
                                           |            |index: 0
                                           |            |text: "Disable the security grid"
                                           |
                                           |then_1──> [set_objective_text]
                                           |            |index: 1
                                           |            |text: "Locate Dr. Carroll"
                                           |
                                           |then_2──> [set_objective_text]
                                           |            |index: 2
                                           |            |text: "Escape via the rooftop helipad"
                                           |
                                           |then_3──> [spawn_chr]
                                                       |body_id: "base:guard_combat"
                                                       |head_id: "base:head_guard_1"
                                                       |position: [1000, 0, 500]
                                                       |team: 1  (enemies)
                                                       |weapon_id: "base:cmp150"
                                                       |entity──> [set_chr_behavior]
                                                                    |behavior: 2  (guard)

=== OBJECTIVE 1: DISABLE SECURITY ===

[on_proximity]
  |position: [2000, 100, 1500]  (security console)
  |radius: 150
  |
  |exec──> [branch_if]
            |condition: <if_has_item player, "base:ecm_mine">
            |
            |true──> [sequence]
            |          |then_0──> [mark_objective_complete] index:0
            |          |then_1──> [hud_message] "Security grid disabled"
            |          |then_2──> [open_door] door_tag:"lab_door"
            |          |then_3──> [play_sound] "base:objective_complete"
            |
            |false──> [hud_message] "You need the ECM Mine to disable this"

=== OBJECTIVE 2: LOCATE DR. CARROLL ===

[on_proximity]
  |position: [4000, -200, 3000]  (Dr. Carroll's location)
  |radius: 200
  |
  |exec──> [sequence]
            |then_0──> [mark_objective_complete] index:1
            |then_1──> [start_cutscene]
            |            |duration: 5.0
            |            |skip_allowed: true
            |            |exec_out──> [spawn_chr]
            |                          |body_id: "base:dr_carroll"
            |                          |position: [4000, -200, 3000]
            |                          |team: 0  (friendly)
            |                          |entity──> [set_chr_behavior]
            |                                      |behavior: 0  (idle — follows player)
            |
            |then_2──> [hud_message] "Dr. Carroll located. Get to the helipad!"

=== OBJECTIVE 3: ESCAPE ===

[on_proximity]
  |position: [6000, 500, 0]  (helipad)
  |radius: 300
  |
  |exec──> [branch_if]
            |condition: <if_distance_within
            |             player_pos, carroll_pos, 500>
            |
            |true──> [mark_objective_complete] index:2
            |          |all_complete──> [branch_if]
            |                            |condition: <all_complete>
            |                            |true──> [complete_mission]
            |                                      |rating: 3
            |
            |false──> [hud_message] "Dr. Carroll must be nearby to extract"

=== FAILURE CONDITION: DR. CARROLL DIES ===

[on_death]
  |victim: <dr_carroll_entity>
  |
  |exec──> [fail_mission]
            |reason: "Dr. Carroll was killed"
```

---

## Runtime Specification

### Graph Serialization (JSON)

Visual script graphs are stored as JSON inside mod directories:

```json
{
  "graph_id": "mymod:needler_behavior",
  "graph_type": "weapon",
  "version": 1,
  
  "nodes": [
    {
      "id": 1,
      "type": "on_fire",
      "position": [100, 200],
      "properties": {}
    },
    {
      "id": 2,
      "type": "gate_cooldown",
      "position": [300, 200],
      "properties": {
        "cooldown": 0.1
      }
    },
    {
      "id": 3,
      "type": "fire_projectile",
      "position": [500, 200],
      "properties": {
        "speed": 800.0,
        "damage": 5.0,
        "lifetime": 3.0,
        "gravity": 0.0,
        "radius": 5.0
      }
    }
  ],
  
  "connections": [
    { "from_node": 1, "from_pin": "exec",       "to_node": 2, "to_pin": "exec" },
    { "from_node": 2, "from_pin": "exec_out",   "to_node": 3, "to_pin": "exec" },
    { "from_node": 1, "from_pin": "aim_origin", "to_node": 3, "to_pin": "origin" },
    { "from_node": 1, "from_pin": "aim_dir",    "to_node": 3, "to_pin": "direction" },
    { "from_node": 1, "from_pin": "owner",      "to_node": 3, "to_pin": "owner" }
  ],
  
  "variables": [
    { "name": "fire_ticks", "type": "Number", "default": 0.0 },
    { "name": "mine_id",    "type": "Integer", "default": -1 }
  ]
}
```

### Graph Types

| Type | Triggers Available | Context |
|------|-------------------|---------|
| `weapon` | on_fire, on_hit, on_pickup, on_death, on_trigger_held/released, on_deploy, on_secondary_activate, on_timer | Attached to a weapon definition |
| `mission` | on_match_start, on_objective_complete, on_proximity, on_timer, on_death | Attached to a map/mission |
| `gamemode` | on_match_start, on_death, on_timer, on_pickup | Custom MP game modes |

### Loading

```c
/* In port/src/studio/studio_graph.c */

typedef struct vs_graph {
    char id[64];                  /* catalog ID */
    char type[16];                /* "weapon", "mission", "gamemode" */
    vs_node_t *nodes;             /* heap array */
    s32 num_nodes;
    vs_connection_t *connections;  /* heap array */
    s32 num_connections;
    vs_variable_t *variables;     /* heap array */
    s32 num_variables;
    
    /* Runtime state */
    f32 *var_values;              /* current variable values */
    vs_timer_t timers[8];         /* timer slots */
    vs_delay_t delays[16];        /* pending delay callbacks */
    s32 num_active_delays;
} vs_graph_t;

/* Parse JSON and populate graph struct */
vs_graph_t *vsGraphLoad(const char *json_path);

/* Free graph */
void vsGraphFree(vs_graph_t *graph);
```

### Evaluation Model

The graph evaluator uses two modes:

**Event-driven (triggers)**: When a game event occurs (fire, hit, death, timer), the engine calls `vsGraphFireTrigger()`. This finds matching trigger nodes and executes their output flow chains synchronously, depth-first.

**Continuous (per-tick)**: Nodes like `on_trigger_held`, `spin_up`, `turret_aim` need per-frame evaluation. The evaluator maintains an "active continuous nodes" set. Each frame, `vsGraphTick()` evaluates all active continuous nodes.

```c
/* Fire a trigger event on a graph */
void vsGraphFireTrigger(vs_graph_t *graph, const char *trigger_type,
                         vs_trigger_context_t *ctx);

/* Per-frame tick for continuous nodes */
void vsGraphTick(vs_graph_t *graph, f32 dt);

/* Execution context passed through the flow chain */
typedef struct vs_exec_ctx {
    vs_graph_t *graph;
    s32 current_node;
    f32 pin_values[32];      /* evaluated data pin cache for current node */
    s32 recursion_depth;      /* safety: max 64 deep */
    s32 nodes_executed;       /* safety: max 1024 per trigger */
} vs_exec_ctx_t;
```

**Data pin evaluation**: Data pins (Math, Entity, Variable nodes) are evaluated lazily — pulled when a downstream flow node reads an input. Values are cached per-trigger-fire to avoid redundant computation.

**Flow pin execution**: Flow is push-based. When a node's output flow pin fires, the connected node's `exec` pin handler runs immediately (depth-first). `sequence` nodes fire outputs in order. `delay` nodes register a pending callback and return (evaluation continues past the delay).

### Performance Budget

| Metric | Budget | Notes |
|--------|--------|-------|
| Max nodes per graph | 256 | Enough for any weapon; missions may need up to 256 |
| Max connections per graph | 1024 | ~4 connections per node average |
| Max nodes evaluated per trigger | 1024 | Safety cap — prevent infinite loops |
| Max recursion depth | 64 | Prevents stack overflow from circular flows |
| Max active graphs per match | 32 | One per unique custom weapon + mission + gamemode |
| Trigger evaluation budget | 0.5 ms per trigger | At 60 Hz with 8 players firing = ~4 ms/frame worst case |
| Per-tick evaluation budget | 1.0 ms total | All continuous nodes across all graphs |
| Memory per graph | ~16 KB | Nodes + connections + variables + runtime state |
| Total VS memory budget | 512 KB | 32 graphs * 16 KB |

**Optimization strategy**: The evaluator pre-computes a topological sort of each flow chain at load time, producing a flat instruction array. At runtime, trigger dispatch is an array walk — no pointer chasing through node/connection graphs.

### Network Determinism

Visual script graphs must produce identical results on all clients:

1. **RNG**: All `random_range` and `random_direction_in_cone` nodes use the game's deterministic RNG (`rng.h`) seeded identically on all clients at match start.
2. **Float determinism**: All math uses standard C `f32` operations — same binary on all clients (MinGW/GCC x86_64).
3. **Event ordering**: Triggers fire in deterministic order (sorted by entity chrnum, then trigger type).
4. **Authority**: Damage, spawns, and scoring are server-authoritative. Client-side graphs produce visual/audio effects locally but all gameplay state changes are validated by the server.

### Integration with Studio Platform

Visual script graphs are referenced from weapon JSON definitions (Layer 2 of `studio-platform-design.md`):

```json
{
  "id": "mymod:needler",
  "name": "Needler",
  "behavior_graph": "mymod:needler_behavior",
  ...
}
```

And from map definitions (Layer 5):

```json
{
  "id": "mymod:arena_custom",
  "mission_graph": "mymod:extraction_protocol",
  ...
}
```

Graphs are registered in the catalog as a new asset type:

```c
/* Add to asset_type_e in assetcatalog.h */
ASSET_VSCRIPT,   /* visual script graph (.pdgraph JSON) */
```

Graphs are distributed via `netdistrib.c` as part of the mod PDCA archive — no special handling needed.

---

## Node Count Summary

| Category | Count | Key Nodes |
|----------|-------|-----------|
| 1. Triggers | 12 | on_fire, on_hit, on_trigger_held/released, on_deploy |
| 2. Conditions | 8 | if_ammo_above, if_count_exceeds, if_line_of_sight |
| 3. Actions | 12 | fire_projectile, damage_entity, damage_radius, spawn_prop |
| 4. Flow Control | 9 | sequence, branch_if, delay, delay_curve, gate_cooldown |
| 5. Math | 15 | add/multiply/clamp/lerp, curve_sample, distance, random_direction_in_cone |
| 6. Entity Ref | 8 | get_self, get_nearest_enemy, get_all_in_radius |
| 7. Weapon | 12 | burst_fire, charge_hold, tracking_seek, accumulate, penetrate, guided_control |
| 8. Audio/Visual | 7 | play_sound_3d, camera_shake, spawn_effect, spawn_decal |
| 9. Mission Flow | 10 | start_mission, set_objective, mark_complete, fail_mission, spawn_chr |
| 10. Variables | 9 | set/get local/entity var, increment_counter, timer_start/cancel |
| **TOTAL** | **102** | |
