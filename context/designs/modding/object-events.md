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

## Slices

1. The level graph's executable part and the runner on arenas (no events
   yet); kind move.
2. The events, record-only, with campaign counts (every pickup and door the
   campaigns cause appears once).
3. The gates and action modules (pd-tests; in-game proof needs a level that
   wires them, after Forge).
4. The native rules and effects as modules wired by the extractor; the
   parity probe; the rows tick.

## Not in scope

- Lifts' own stops and glass breaking (their own events after slice 4).
- Multiplayer's pickup replication (MULTIPLAYER): events fire on the host.
