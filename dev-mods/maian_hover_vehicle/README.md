# Maian hover vehicle (mod source)

The source of the custom vehicle mod `mod:vehicle_maian_hover`. The in-game mod
author builds the mod from `recipe.json` (`--mod-author <recipe.json>`, as the
smokes below do). Nothing here is a finished archive, and nothing comes from
the base game. This folder is the recipe, not a mod folder; what the recipe
builds is `dev-mods/vehicle_maian_hover/`, the bundled sample listed in
`dev-mods.json` (pd-tests `"[maian-vehicle][bundled]"` fails if the two differ).

## Files

| File | What it is |
|---|---|
| `recipe.json` | What the mod author does: the template vehicle, the name, and the members it replaces. |
| `behavior.graph.json` | The vehicle's behavior as a node graph (`pd.vehicle_graph.v1`). Every node carries a comment. |
| `behavior/variables.json` | The numbers the graph uses as `$name`. Change one here and the graph takes it. |
| `physics.json` | Handling (`pd2.vehicle.physics.v3`): thrust, drag, steering and hover. It holds no boost numbers; those are the graph's. |
| `meshes/vehicle/` | The model's members: an OBJ with vertex colours and its nodes, parts, faces and render files. Made by `build_vehicle_mesh.py`. |
| `template/` | The example vehicle and mesh archives the recipe starts from. |
| `build_vehicle_mesh.py` | Makes `meshes/vehicle/` from the cleaned-up Tripo model. |
| `make_standin_mesh.py`, `blender_dump_vehicle.py` | The earlier stand-in model. Superseded: the recipe does not use their output. Kept as the worked example of the glTF route that the `pd-game-model` skill points to. |

## What the graph does

| When | Then |
|---|---|
| A player asks for a seat | The game names the seat from where the player stands and looks. The driver's seat, if nobody is driving: the original mount. The gunner's seat, if the pod is empty: the gunner's mount (the turret's controls, the vehicle stays put). |
| A rider asks to get off (the exit key once) | The original dismount, for the driver or for the gunner. |
| A rider asks for the other seat (the exit key twice quickly) | If that seat is empty: the rider moves to it without getting off. A driver who moves to the pod leaves the vehicle drifting. |
| Every update | Hover and take the driver's inputs. Run boost and heat. |
| The heat phase changes | Overheated or cooling: hide the seafoam crystals (part 140), so the black ones show. Any other phase: show them. Full boost, overheated or cooling: lock the turret. Any other phase: unlock it. |

The heat phases are cool, ramping, full, overheated and recovering. Held boost
ramps to full in 1 s and overheats after 4 s; boost is then capped at half and
the steering is limited for 0.75 s; cooling takes 2 s when stopped and up to
4 s at full speed. Those numbers are `behavior/variables.json`.

## Model parts

| Part | What |
|---|---|
| 1, 2 | Turret yaw, cannon pitch |
| 50 | Muzzle |
| 110 to 127 | The 18 ring seats for the turret's crystals |
| 128 | The muzzle crystal's seat |
| 140 | Seafoam crystals (a toggle part) |
| 141 | Black crystals, inside the seafoam ones (a toggle part) |
| 160 | The driver's eye: in the well ahead of the turret deck (no geometry) |
| 161 | The gunner's eye: above the cannon, turning with the turret (no geometry) |

Parts 160 and 161 are the seat parts any vehicle model may carry: the game puts
the rider's camera there. A model without part 160 gets the base hoverbike's
driver position (80 units up, 50 back), which on this vehicle is inside the
cannon.

## Proof

- `pd-tests "[maian-vehicle]"` builds the mod the way the game does and compiles its graph.
- `tools/smoke-verify/tests/maian_vehicle_drive_smoke.json` and
  `maian_vehicle_boost_smoke.json` build it in the game, put it in place of
  Escape's hoverbike, and drive and boost it.

Design: `context/designs/modding/vehicle-graph-v1.md` (the graph format) and
`context/designs/modding/maian-vehicle-and-veteran.md` (the plan).
