# Typed PDXXX Basic Examples

This folder is a permanent modder-facing sample set. The content units are the typed `*.pdxxx` asset archives:

- `heads/tri_head.pdhead`
- `bodies/tri_body.pdbody`
- `arenas/tri_arena.pdarena`
- `meshes/tri_mesh.pdmesh`
- `weapons/tri_weapon.pdwpn`
- `animations/weapon_idle.pdanim`
- `animations/character_skeletal.pdanim`
- `audio/sfx/tri_click.pdsfx`
- `audio/voice/tri_voice.pdvoice`
- `audio/music/tri_song.pdsong`
- `ui/tri_reticle.pdui`
- `fonts/tri_font.pdfont`
- `lang/tri_lang.pdlang`
- `scenarios/tri_scenario.pdscenario`

Change any `.pdxxx` extension to `.zip` to inspect the archive. The GLTF, OBJ, and INI files inside each archive are the authored data. The game may generate private readable cache under `$S/mod-cache`, but that cache is internal, deleteable, and not part of this example.

The contract is strict: each asset archive should carry every authored dependency it needs internally, including model textures/UV material references, rig or mesh linkage, animation targets, weapon model/animation/audio relationships, and any other files referenced by descriptors or source formats.

`.pdmod` is not the authoring format for this folder. Wrap this folder as `.pdmod` only when testing Public Mods, online delivery, or share transport. Authored `*.bin` files do not belong in these examples.
