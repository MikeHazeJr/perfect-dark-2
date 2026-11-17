# Blank Room

A small level that holds nothing of the original game: one room, 24 by 24
metres and 4 high, with four pillars for cover, eight player starts on a ring
and no objects. It is the template Forge starts a level from when the level is
meant to be shared. A level cloned from one of the game's own arenas embeds
that arena's walls and floors and can only ever be used on the machine that
made it.

Everything in `blank_room.pdarena` is made by `build_blank_room.py`: the
geometry, its texture, the collision, the pads, starts and bounds, the level's
graph and the navigation record. Nothing is modelled by hand and nothing is
borrowed.

## Files

- `blank_room.pdarena`: the built level (an arena with its scenario inside).
- `mod.json`: the mod manifest.
- `build_blank_room.py`: the builder and its checks.

## Rebuilding

```
python dev-mods/blank_room/build_blank_room.py
python dev-mods/blank_room/build_blank_room.py --check
```

The first builds, checks and writes the two files. The second builds in
memory and says whether the files on disk are current.

The level graph's structure (the AI kind nodes and their fixed links, the
pickup and door rules, the cache kind `arena.ini` names) is read from the
repository's example level, `examples/modding/typed-pdxxx-basic/arenas/
tri_arena.pdarena`. When the level formats change, rebuild the example first
(`tools/build_typed_pdxxx_examples.py`), then this.

The builder refuses to write a level that breaks a rule the game enforces
(the rules, with the code that enforces each, are in
`context/designs/modding/forge-mode-scope.md`, "F5: the smallest level the
game accepts"), and then gives the archive to
`tools/asset_archive_conformance.py`.

## Using it

It is a dev mod that is off by default. Include it in a build with
`-DevMods blank_room`, enable it in the mod manager, and it appears as
"Blank Room" under the mod levels in The Grid and in match setup. Its ids are
`mod_blank_room:arena_blank_room` and `mod_blank_room:scenario_blank_room`.

## Status

Built and checked offline on 2026-11-17. Not yet run in the game: two smokes
are written for it (`forge_blank_room_smoke`, `forge_blank_room_edit_smoke`).
