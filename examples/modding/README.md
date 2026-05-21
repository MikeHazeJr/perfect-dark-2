# Modding Examples

These examples use typed `*.pdxxx` asset archives as the authoring surface. Change a typed file's extension to `.zip` to inspect or edit the descriptor, GLTF/OBJ source files, textures, audio, TSV, fonts, and supporting INI files inside it.

`.pdmod` is only the transport wrapper for Public Mods, online-required delivery, and sharing. Do not use `.pdmod` archives as the primary sample format, and do not add authored `*.bin` payloads.

## Available Sets

- `typed-pdxxx-basic/` - zip-openable typed asset archives for every current `.pdxxx` family: weapon, head, body, arena, mesh/model, animation, SFX, voice, music, UI texture, font, language, and scenario. Each archive carries its descriptor plus the authored files it references internally.
