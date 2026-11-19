# Pickups and doors as level events

Status: CORE, 2026-11-16. Proposal for the week note's "Pickups" row
("touch event, can-pick-up gate, collect action") and "Doors, lifts, glass"
row ("an archetype graph at the open, activate and lift entry points"),
under decision 1. Slices 1 to 3 written on `week-pd/core` (f63e4248,
5233cbb7, 4881fc32; see "Slices 1-3 as written"), slice 4 not started.
Facts from a read-only survey of `week-pd/core` (c0410f15).

## Where it stands

- Pickups: every frame `prop.c` asks `objTestForPickup` (`propobj.c`) about a
  prop the player touches. It decides alone (collectable flags, projectile
  pickup timers, armed mines and grenades, safes, a weapon the player
  already carries full, dual weapons, the unlimited-ammo cheats) and then
  calls `propPickupByPlayer`, which applies the effect by object type (a key:
  sound, message, into the inventory; an ammo crate: ammo, then freed; a
  weapon: the inventory and ammo; and so on). The AI command that gives an
  object to the player and the use interaction call the same effect.
- Doors: `doorTestForInteract` (the use button near a door), `doorIsUnlocked`
  (keys, padlocks, the lock flags), `doorsActivate` and `doorsRequestMode`
  (open or close, with lifts), `doorStartOpen` and `doorStartClose`. The AI
  commands that open, close, lock and unlock doors already run as graph
  twins.
- Graph runtimes today: the mission graph runner (events, gates and native
  modules; objective, phase, end and, as of today, trigger-volume events)
  runs only for missions; the prop graph runtime runs mod props' own
  spawn and tick programs. Nothing fires an event when the player picks
  something up or opens a door.
- Forge's logic system (forge-level-editor.md section 7) wants exactly
  these: "On Item Pickup", "On Switch Activated", conditions such as "Has
  Item", actions such as "Open / Close Door" and "Lock / Unlock Door".

## Proposal

1. **One runner for every level.** The runner the missions use is generic
   (events, gates, `og.*` modules, compiled once, each node at most once per
   event). A scenario's `level.graph.json` gains the same executable part,
   so arenas and missions both have one; a mission graph stays as it is.
2. **Events at the native seams, after the native decision:**
   `event.pickup.collected` (the object's tag and type, the player) from
   `propPickupByPlayer` once the effect applied; `event.door.opened` and
   `event.door.closed` (the door's tag) from the mode change;
   `event.object.used` (the tag, the player) from the use interaction.
   Record-only at first: the base game's behavior does not change, and an
   author can already react ("open the vault when the key is picked up").
3. **Gates that can refuse, in front of the native rules:**
   `gate.pickup.allowed` and `gate.door.unlocked` are asked by
   `objTestForPickup` and `doorIsUnlocked` before their own rules; with no
   gate wired they pass, so the native rule decides exactly as today. With
   one wired, the author's condition decides first (a team filter, "has
   item", a channel), then the native rule.
4. **Actions as modules:** `og.door.open`, `og.door.close`, `og.door.lock`,
   `og.door.unlock`, `og.object.enable` and `og.object.disable` call the
   functions the AI twins already use; `og.pickup.give` gives an object as
   the AI command does.
5. **The base game from source.** The native decision and effect become the
   named modules `og.pickup.native_rules` and `og.pickup.collect` (and the
   door equivalents), and every extracted level graph wires them behind the
   events, so a level that rewires them changes the behavior; a level
   without the wiring refuses at activation (fails closed), as the mission
   graph does.
6. **Proof.** The campaigns and the arena smokes with the wiring in place
   play unchanged; a parity probe counts native and graph decisions as the
   objective probe does.

## Object identity (decided, CORE, 2026-11-16)

- An event, gate or module names an object by its `objects.json`
  `record_id` (`"setup_0123"`). Every setup record has one, and it is the
  setup command index the game keeps for each object both ways
  (`setupGetCmdIndexByProp`, `setupGetObjByCmdIndex` in `setuputils.c`),
  so a native seam can name the object it acts on and a module can find
  the object it is given. A Forge object gets a record id the same way.
- The payload also carries what a graph may filter by: the object's tag
  (the id of the `tag` record that names it, -1 when none does, as the AI
  commands address objects), its `kind` (`weapon_pickup`, `door`,
  `ammo_crate`, ...) and, when it has one, its catalog id (the weapon or
  the model). A gate or filter may use any of them; a module that acts on
  an object takes the record id.

## Slices

1. The level graph's executable part and the runner on arenas (no events
   yet); kind move.
2. The events, record-only, with campaign counts (every pickup and door the
   campaigns cause appears once).
3. The gates and action modules (pd-tests; in-game proof needs a level that
   wires them, after Forge).
4. The native rules and effects as modules wired by the extractor; the
   parity probe; the rows tick.

## Slice 1 plan (CORE, 2026-11-16)

- A level graph today: up to 6,887 nodes (Deep Sea), all `scenario.*` data
  but one inert `event.*` node, and `links` for its structure; no `edges`.
  Its executable part will be its `event.*`, `gate.*` and `og.*` nodes and
  an `edges` array of execution edges, as in a mission graph.
- The runner compiles a level graph's program from those nodes alone (a
  compile option that skips every other node, so the data nodes neither
  count toward the 4,096-node limit nor become program nodes); the
  mission graph keeps compiling as today.
- The runtime compiles the level program at scenario activation for every
  stage (arenas and missions), logs it once
  (`SCENARIO.GRAPH: level program '...' nodes=... exec_edges=... events=...
  modules=...`) and refuses a stage whose level program does not compile.
  Base level graphs have no program nodes, so every base stage compiles an
  empty program: no kind move, no extraction change.
- Trigger-volume events go to the level program too (bound to the stage's
  `volumes.json`), so an arena can react to a box; objective and phase
  events stay the mission's.
- Conformance checks a level graph's program part as it checks a mission
  graph's.

## Slice 2 plan (CORE, 2026-11-16)

- Event kinds in the runner: `event.pickup.collected`,
  `event.door.opened`, `event.door.closed` and `event.object.used`. An
  event node may filter one object (`"object": "setup_0123"`; absent or
  `"any"` for every one). The event carries `object` (the record id),
  `tag` (-1 when no tag record names it), `object_kind` (the objects.json
  kind), `catalog_id` (the weapon of a pickup, else the object's model; may
  be absent) and `player` (-1 when no player caused it).
- Seams, each after the native decision and effect (game code builds the
  payload, as the objectives check builds the players' positions):
  `propPickupByPlayer` once the pickup applied (the current player);
  `doorStartOpen` and `doorStartClose` for every start, whoever caused it
  (player -1); `propobjInteract` for the use button on an object (the
  current player). The record id comes from `setupGetCmdIndexByProp`, the
  tag from `objGetTagNum`.
- Delivered to the level program on every stage and to the mission
  program on a mission, on the host only (a networked client fires none;
  replication stays MULTIPLAYER's).
- Record-only: no base graph wires them. The runtime counts each kind per
  stage and logs the counts once (`SCENARIO.GRAPH: object events '...'
  pickup.collected=... door.opened=... door.closed=... object.used=...`);
  the campaign fixtures require doors and pickups on the missions that
  have them, so every base mission shows its events arriving.

## Slice 3 plan (CORE, 2026-11-16)

- **Node parameters.** The runner keeps each program node's scalar fields
  (strings, numbers, booleans) as its parameters, and the module callback
  receives them (`missionGraphParamText(params, "object")`), so a module
  knows what to act on. The existing modules ignore them.
- **Native gates.** A `gate.*` kind the runner does not evaluate itself is
  a native gate when the module table knows it: the same callback runs it
  and its result is pass (1) or fail (0). First ones:
  `gate.player.has_item` (`"item"`: a weapon or item catalog id the
  event's player carries) and `gate.object.kind` (`"kind"`: the event's
  object kind).
- **Query events, before the native rule.** `event.pickup.requested` from
  `objTestForPickup` (a player touches something it could collect) and
  `event.door.open_requested` from the player's door use
  (`doorTestForInteract`), each with the object payload and the player.
  If the walk runs `og.pickup.refuse` or `og.door.refuse`, the native seam
  refuses (no pickup; the door stays shut, as a locked one does); if
  nothing refuses, the native rule decides exactly as today. A program
  without such handlers changes nothing.
- **Action modules on objects.** `og.door.open`, `og.door.close`,
  `og.door.lock` and `og.door.unlock` (on `"object"`, through the same
  functions the AI door twins use; lock and unlock take `"bits"`, the key
  flags), and `og.pickup.give` (gives `"object"` to the event's player, as
  the AI command does). Each finds its object with `setupGetObjByCmdIndex`
  from the record id and refuses when there is none. `og.object.enable`
  and `og.object.disable` wait: the game has no single native switch for
  an object's collision, visibility and use to mirror yet.
- `gate.object.kind` takes `"object_kind"` (the node's own `"kind"` is the
  gate's).
- Proof: pd-tests (the runner's parameters, native gates and query
  refusals; the modules' table and seams pinned). In game, a level that
  wires them: Forge's first levels, or a test arena whose level program
  refuses one pickup and opens a door when another is collected.

## Slice 4 plan (CORE, 2026-11-17)

The base game's own pickup and door rules move into the graph, so the
Pickups and Doors rows tick (decision 1).

- **Pickups.** `objTestForPickup` keeps its range and line-of-sight test
  and then fires `event.pickup.touched` (the object payload, the player)
  instead of deciding. Its rule part (collectable flags, projectile pickup
  timers, armed throwables, safes, a weapon carried full, dual weapons, the
  unlimited-ammo cheats) becomes a predicate the native gate
  `gate.pickup.native_rules` runs, and the effect (`propPickupByPlayer`
  and its tick operation) the module `og.pickup.collect`, whose result the
  seam returns. Every extracted level graph wires
  `event.pickup.touched` -> `gate.pickup.native_rules` (pass) ->
  `og.pickup.collect`; an author puts a gate of their own on that path, or
  rewires it.
- **Doors.** `propdoorInteract` fires `event.door.use`; every level graph
  wires it -> `gate.door.native_unlocked` (`doorIsUnlocked`: keys, lock
  flags, front and back) -> pass `og.door.activate` (swing direction and
  `doorsActivate`), fail `og.door.locked_message` (the original locked
  message and sound). The automatic door check fires `event.door.approach`
  wired the same way to `og.door.activate_automatic`.
- **The queries fold in.** `event.pickup.requested` and
  `event.door.open_requested` (slice 3) are the same moments; once the
  rules are in the graph an author's gate on the rule path does what
  `og.pickup.refuse` and `og.door.refuse` did, so slice 4 retires them
  rather than keep two ways.
- **Fails closed.** A scenario-source level graph without the wiring (no
  handler for `event.pickup.touched` or `event.door.use`) refuses at
  activation, as the mission graph does; the extractor writes the wiring,
  so the scenario kind moves (`_ruleevents`; `objrules` already names the object rules row).
- **Proof.** A parity probe in the campaign runner computes the native
  decision beside the graph's at every pickup and door use and counts
  differences per mission; the campaign fixtures pin `differ=0`, as the
  outcome and AI edge probes do. Then the Pickups and Doors rows tick
  (lifts and glass stay their own events, after).

## Slice 4 design (CORE, 2026-11-17)

The plan above, made exact. On a stage built from scenario source the
seams below decide nothing: the level program does, through a gate and
modules that are the original routines. A ROM stage keeps the native code.

**Rule events.** Delivered to the level program only (a mission graph that
names one is refused). Each carries the object payload of the object events
(record id, tag, kind, catalog id, player) and may filter one object.

| Event | Kind | Fired by | When |
|---|---|---|---|
| `event.pickup.touched` | 11 | `objTestForPickup` | the player touches a pickup: an object its flags make collectable (`func0f085194` objects unless `OBJFLAG_UNCOLLECTABLE`, others with `OBJFLAG_COLLECTABLE`) is in pickup range (the magnet's and the Elvis shelf's included) and in line of sight; every tick it holds |
| `event.door.use` | 12 | `propdoorInteract` | the player (or the eyespy) presses use on a door |
| `event.door.approach` | 13 | `doorsCheckAutomatic` | the player is in range of an automatic door, in front of it, while the door is closed or closing |

**Gates and modules.** Each runs only inside its own rule events and
refuses anywhere else.

| Node | Does |
|---|---|
| `gate.pickup.native_rules` | the original pickup rules (`objPickupRulesAllow`): thrown laptops, projectile pickup timers, safes, armed throwables and mines, projectiles in flight, a weapon carried full and the dual-wield rules, full ammo crates, the shield amount, the briefcase in Hold the Briefcase, looking down without the magnet |
| `og.pickup.collect` | collects the object (`propPickupByPlayer`, with its HUD message); the seam returns its tick operation; once per touch |
| `gate.door.native_unlocked` | `doorIsUnlocked` for the player (or the eyespy): key flags, locked front or back, padlocks |
| `og.door.activate` | the door opens or closes for the user (swing direction, `doorsActivate`) |
| `og.door.locked_message` | the original refusal (`doorShowLocked`): the locked message when the door is shut (an override's text, the training message or "This door is locked."), who tried, the pending activation flag |
| `og.door.activate_automatic` | an automatic door opens (`doorsRequestMode`, the doors-opened statistic) |

**Wiring.** The extractor writes it into every level graph (scenario kind
`_ruleevents`), and the mod packer's level graph template carries it; node
ids `object.rules.*`:

```
event.pickup.touched -> gate.pickup.native_rules -pass-> og.pickup.collect
event.door.use -> gate.door.native_unlocked -pass-> og.door.activate
                                            -fail-> og.door.locked_message
event.door.approach -> gate.door.native_unlocked -pass-> og.door.activate_automatic
```

An author changes a rule by changing the wiring: a gate of their own on
the path (`gate.object.kind`, `gate.player.has_item`), another module, or
an event filtered to one object beside the general one.

**Fails closed.** A scenario-source level program that handles none of a
rule event (no event node of that kind with an execution edge) is refused
at activation, and conformance refuses the archive.

**The queries fold in.** `event.pickup.requested`, `event.door.open_requested`,
`og.pickup.refuse` and `og.door.refuse` (slice 3) are removed.

**Parity probe.** At each rule event the seam also computes the native
decision first (`objPickupRulesAllow`, `doorIsUnlocked`) and compares it
with what the graph did (collected or not; activated or the locked
message). The runtime counts `compared` and `differ` per stage; the
campaign runner logs `object rule parity stage_id='...' compared=N
differ=M` at each mission's end and fails the run on a difference; the
campaign fixtures pin it.

## Slices 1-3 as written (CORE, 2026-11-16)

- Slice 1 (f63e4248): `missionGraphProgramCompileLevel` compiles a level
  graph's program from its event, gate and module nodes only; the runtime
  compiles it at scenario activation for every stage (a base level graph
  gives an empty program: its inert `event.scenario.load`), refuses the
  level graph when it does not compile or names a volume that is no box of
  `volumes.json`, logs `SCENARIO.GRAPH: level program '...'`, and delivers
  its trigger-volume events from `lvTick` after `scenarioTick` on the host.
  Volume binding and delivery are shared with the mission program.
- Slice 2 (5233cbb7): the four object events from `propPickupByPlayer`,
  `doorStartOpen`, `doorStartClose` and `propobjInteract`, to the level
  program and (on a mission) the mission program, host only; per stage the
  first event of each kind is logged with its payload and the totals once
  (`SCENARIO.GRAPH: object events '...' pickup.collected=...`). The
  objects.json kind comes from the setup loader's own table.
- Slice 3 (4881fc32): node parameters for modules, native gates through
  the module table, the two queries before the native rule (a refused door
  behaves as a locked one, at the use button and at an automatic door),
  the door and pickup modules, `gate.object.kind` and
  `gate.player.has_item`; object events nest at most four deep.
- Proof so far: pd-tests (`tests/test_level_graph_program.cpp`); in game,
  the next campaign turn shows every stage's level program and its object
  event totals. The gates and modules need a level that wires them.
- In game 2026-11-17 (q_20261117000144009_bab5471d, client caf00d7c):
  every stage compiled its level program and recorded its object events;
  every payload's record id was empty, B-1259 (`setupGetCmdIndexByProp`
  read the N64 word offset), fixed in 4cbcd3da.

## Slice 4 as written (CORE, 2026-11-17)

- ca42b3af on `week-pd/core-s4`: the rule events (runner kinds 11 to 13;
  a mission graph naming one is refused when it compiles), the rule
  context (`mission_graph_object_rule_t`, `missionGraphModulesRuleBegin`
  and `End`) and the six rule gates and modules, `objTestForPickup` split
  into the candidate test, `objPickupRulesAllow` and the touch,
  `doorShowLocked` and `doorOpenAutomatic` out of `propdoorInteract` and
  `doorsCheckAutomatic`, the wiring in the extractor (kind `_ruleevents`,
  plus a freshness marker), the mod packer's template and the typed example
  builder (examples regenerated), the fail-closed check in the runtime and
  conformance, the parity probe and its fixture pins; slice 3's queries
  removed.
- 7326bbe8: the harness's `use_door` and `mp_arena_door_rules` (Felicity),
  since no campaign smoke presses use on a door.
- Not routed through the rules, on purpose: the AI's `give_object_to_chr`
  (an AI command with its own graph twin), the thrown laptop gun's
  recovery by the use button (the weapon graph's deployed entity decides
  it: `pickup_recover`, `interact_filter`), simulants' pickups (bot AI,
  MULTIPLAYER's) and a network client's door prediction (the host
  decides).

## Slice 5: lifts and glass (CORE, 2026-11-17)

The rest of the Doors, lifts and glass row, in slice 4's pattern: on a
scenario-source stage the seams fire rule events into the level program and
decide nothing; the original routines are the gates and modules; every
level graph wires them (fails closed); the parity probe compares the
original decision, computed first, with the graph's.

| Event | Kind | Fired by | Wiring |
|---|---|---|---|
| `event.lift.called` | 14 | `doorCallLift`: a linked lift door is activated (the use rule's `og.door.activate`, an AI, a script) and is not an open door that should just close | -> `gate.lift.native_vacant` (no player and no chr rides it) -pass-> `og.lift.go_to_stop` (the door's stop) |
| `event.lift.idle` | 15 | `liftTick` at the lift's aim stop, every tick it waits | -> `gate.lift.native_ready` (no door, or its door closed and unlocked) -pass-> `og.lift.go_to_next_stop` |
| `event.lift.arrived` | 16 | `liftTick` when the lift reaches its aim stop | -> `gate.lift.native_door_unlocked` (the stop's door has no key flags) -pass-> `og.lift.open_door` |
| `event.glass.damaged` | 17 | `objDamage` on glass or tinted glass, after the damage | -> `gate.glass.native_broken` (damage at its limit) -pass-> `og.glass.break` (`glassDestroy`) |
| `event.glass.broken` | 18 | `glassDestroy`, after the glass breaks | record only, for authors |

The rule events are the level graph's (a mission graph naming one is
refused) and carry the object payload; a lift event's stop travels in the
rule context. `og.lift.go_to_stop` also works outside its rule event for
authors: then `"object"` (a lift's record id) and `"stop"` (0 to 3) name
the lift and the stop. A network client keeps the native rules for its own
prediction and for the damage the host sends it (the host decides).

Node ids `object.rules.lift.*` and `object.rules.glass.*`; the scenario
kind moves to `_liftglass`; the level program is 23 nodes, 15 execution
edges and 8 modules. Proof: `mission_lift_glass_rules` (Defection: the
harness's `use_lift_door` calls a lift through a closed lift door,
`damage_glass` breaks the two nearest glass objects; the lifts' own waits
and arrivals run meanwhile), and the campaign fixtures' parity lines.

Left native, on purpose: a lift's door closing before it leaves (it cannot
move with the door open), the lift trigger that disables a lift after one
trip (`OBJFLAG_LIFT_TRIGGERDISABLE`), the glass in a windowed door (part of
the door's damage) and the AI's lift command (`liftGoToStop` behind its
graph twin).

### Levels authored before slice 5 (CORE, 2026-11-19)

Forge and the Grid landed on dev while this slice waited for its proof, with
three stored levels (the Blank Room, the Model Room, the Grid's two rooms)
and every level a player saves from them. Their graphs wire the pickup and
door rules and name the scenario kind before `_liftglass`. As first written
this slice refused them all: the activation check and the validator both
asked for the seven rules and the new kind. They keep loading, and they keep
the lift and glass behavior they were made with:

- **The game** requires the pickup and door rules of every level graph
  (slice 4, unchanged) and no longer requires the lift and glass rules. Each
  of the four seams asks whether the level program handles its event
  (`scenarioSourceGraphObjectRuleHandled`): when it does, the level program
  decides, as above; when it does not, the game's own rule decides, the same
  routine the gate and module wrap (`liftIsVacant` and `liftGoToStop`, the
  idle lift's next stop, the arrival door, `glassDestroy` at the damage
  limit). One log line names the rules a level leaves to the game
  (`level program '...' wires no event.lift.called, ...`). Forge can place
  glass, so making an unwired rule do nothing would have left glass
  unbreakable in every level saved before this slice.
- **The validator** accepts, for an arena a mod authored, the current kind
  or an older kind the game still loads
  (`ARENA_SCENARIO_GRAPH_CACHE_KINDS_STILL_LOADED`: the kind before
  `_liftglass`), and holds the level graph to what its kind promised: an
  older kind needs the pickup and door rules and everything else of its
  kind (the AI edges and typed operands), not the lift and glass rules. A
  level of the current kind needs all seven. A base arena names the current
  kind (an install re-extracts it otherwise).
- **New levels** get all seven: the packer copies the rule wiring from the
  example level, which is of the current kind.

An author changes a level's lift or glass behavior by wiring the event: once
the level program handles it, the nodes decide and the game's rule no longer
runs. The three stored levels can move to the current kind whenever their
owners rebuild them with this slice's packer (the Grid room builder pins its
template's hash, so that is the Grid lane's change), and Forge can add the
four rules when it saves a level that lacks them; neither is needed for a
level to keep working. Test: `tools/tests/test_level_rules_conformance.py`
(the validator) and the pins in `tests/test_level_graph_program.cpp` (the
seams).

## Not in scope

- Multiplayer's pickup replication (MULTIPLAYER): events fire on the host.
