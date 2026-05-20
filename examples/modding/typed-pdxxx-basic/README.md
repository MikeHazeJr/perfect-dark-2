# Typed PDXXX Basic Examples

This folder is a permanent modder-facing sample set. The content units are the typed `*.pdxxx` files:

- `heads/tri_head.pdhead`
- `arenas/tri_arena.pdarena`
- `animations/weapon_idle.pdanim`
- `animations/character_skeletal.pdanim`

Each typed file points at normal editable sidecars in a same-name folder. The GLTF, OBJ, and INI files are the authored data. The game may generate private readable cache under `$S/mod-cache`, but that cache is internal, deleteable, and not part of this example.

`.pdmod` is not the authoring format for this folder. Wrap this folder as `.pdmod` only when testing Public Mods, online delivery, or share transport. Authored `*.bin` files do not belong in these examples.
