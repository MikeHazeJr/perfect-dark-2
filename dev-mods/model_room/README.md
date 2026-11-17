# Model Room

An original level made from a model: one room, 24 by 24 metres and 5 high,
with a platform 1.2 metres up at each end reached by a ramp, four pillars, two
low cover blocks and eight player starts on a ring (two of them on the
platforms). It is Forge step F5's example
(`context/designs/modding/forge-mode-scope.md`): the walls and floors come
from a model, the way a level made in Blender would, and Forge then places
starts, weapons and objects on it.

## Files

- `model_room.glb`: the model, in glTF's own terms (metres, +Y up). The west
  platform is the east one under a mirroring node.
- `make_model_room.py`: writes the model (it stands in for a modelling
  program) and builds the level from it.
- `model_room.pdarena`, `mod.json`: the built level and its manifest.

The level is made by `tools/build_level_from_model.py`, which reads any
`.glb` that way: meshes are drawn and collided with (a node named
`collision...` is collided with and not drawn, `nocollide...` the reverse),
faces that look down are not collided with, each material becomes a
textured, tinted part, and empties named `start...` are the player starts.
The rest of the archive comes from `tools/level_archive.py`, which the Blank
Room shares.

## Rebuilding

```
python dev-mods/model_room/make_model_room.py
python dev-mods/model_room/make_model_room.py --check
```

The first writes the model and builds, checks and writes the level. The
second builds in memory and says whether the files on disk are current.

It is a dev mod, off by default: include it with `-DevMods model_room`.
