# Pickups and doors as level events

Status: CORE, 2026-11-16. Proposal for the week note's "Pickups" row
("touch event, can-pick-up gate, collect action") and "Doors, lifts, glass"
row ("an archetype graph at the open, activate and lift entry points"),
under decision 1. Not built. Facts from a read-only survey of
`week-pd/core` (c0410f15).

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
  functions the AI door twins use), `og.object.enable` and
  `og.object.disable` (its collision, visibility and use), and
  `og.pickup.give` (gives `"object"` to the event's player, as the AI
  command does). Each finds its object with `setupGetObjByCmdIndex` from
  the record id and refuses when there is none.
- Proof: pd-tests (the runner's parameters, native gates and query
  refusals; the modules' table and seams pinned). In game, a level that
  wires them: Forge's first levels, or a test arena whose level program
  refuses one pickup and opens a door when another is collected.

## Not in scope

- Lifts' own stops and glass breaking (their own events after slice 4).
- Multiplayer's pickup replication (MULTIPLAYER): events fire on the host.
