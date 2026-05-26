# Weapon Graph Runtime Cutover Plan

Status: closed for Kanban `c3814`; Slice 1 (`c3814-s10`) through Slice 9 (`c3814-s18`/`c3814-s19`) landed by 2026-05-25. The clean authored archive layout is owned by `c3832` and [weapon-archive-clean-format.md](weapon-archive-clean-format.md); the runtime cutover now uses graph-authored records as the behavior source while existing Perfect Dark routines remain the parity-preserving execution backend. Follow-up `c3840` owns any future routine replacement; its first safe slice names the current OG-backed opcode families through `weaponGraphParityModule` and records `parity_module` on held/projectile/entity runtime records.

This document turns the schema, base behavior audit, parameter matrix, and module parameter spec into sequenced runtime work. It is intentionally split so the pre-release `.pdwpn` removal can land cleanly before graph compiler and gameplay runtime work begins.

## Inputs

- [weapon-behavior-graph-assets.md](weapon-behavior-graph-assets.md) defines `.pdweapon`, `.pdprojectile`, `.pdentity`, graph JSON, nested catalog IDs, and canonical payload SHA-256 rules.
- [base-weapon-behavior-coverage.md](base-weapon-behavior-coverage.md) records the source-backed base weapon behavior audit.
- [base-weapon-parameter-matrix.md](base-weapon-parameter-matrix.md) records the current 86-weapon function parameter matrix.
- [weapon-graph-module-parameters.md](weapon-graph-module-parameters.md) names the v1 weapon, projectile, and entity behavior modules.

## Cutover Rules

- `.pdweapon` is the only weapon archive extension. `.pdwpn` is not an accepted input, alias, migration source, fallback, or compatibility path.
- `.pdprojectile` and `.pdentity` are first-class catalog asset types, not private sub-documents hidden inside weapon records.
- Embedded projectile/entity archives remain real catalog entries with derived IDs unless an explicit same-namespace ID is authored.
- Runtime may temporarily adapt graph IR into existing weapon structs, but the authored source of truth must be graph JSON.
- Base assets continue to regenerate through the boot/self-heal path.
- Each code slice must update Kanban and context as it lands. Code slices require build/test verification.

## Slice 1 Code Footprint

These were the first known pre-release weapon-extension paths. `c3814-s10` has now moved live code/tests/examples to `.pdweapon`; the old extension should not return outside historical notes.

| Area | Current file(s) | Required direction |
| --- | --- | --- |
| Base weapon emitter | `port/src/romextract_pdweapon.c`, `port/include/romextract_pd.h`, `port/src/main.c` | Emits `.pdweapon` zip-openable archives with `weapon.ini`, `manifest.json`, and `behavior.graph.json`. |
| Weapon walker | `port/src/loader_walker_weapon.c` | Scans `.pdweapon` only. |
| Scanner/packer mapping | `port/src/assetcatalog_scanner.c`, `port/src/modpack_pdmod.c`, `port/src/modmgr.c` | Maps `.pdweapon`, `.pdprojectile`, and `.pdentity` to first-class typed assets. |
| Loader pool comments/parser assumptions | `port/src/loader_pool.c`, `port/include/loader_pool.h`, `port/src/weapondata_authored.c`, `port/include/weapondata_authored.h` | Naming and parse-boundary comments now refer to `.pdweapon`. |
| Tests/examples | `tests/test_mod_external_archive_static.cpp`, examples under `examples/modding` | Fixtures use `.pdweapon`, include `behavior.graph.json`, and statically guard the live scanner/packer/walker surface. |

## Implementation Slices

### Slice 1: remove `.pdwpn`, emit `.pdweapon`

Kanban: `c3814-s10`. Status: done 2026-05-21.

Scope:

- Rename exposed comments/log labels/helper names only where useful for clarity; the important behavior is extension and archive shape.
- Teach base extraction to write `.pdweapon` archives containing `weapon.ini`, compatibility `manifest.json` if the current universal walker still needs it, and initial `behavior.graph.json`.
- Rewrite stale `.pdwpn` output rather than preserving it.
- Update loader walker, scanner, packer, examples, tests, and static guards to reject `.pdwpn`.

Exit gates:

- Fresh repo search shows `.pdwpn` only in historical context/audits/bug notes, not live code, tests, examples, or emitted archive mappings.
- Focused modding static tests pass.
- All-target build passes.

### Slice 2: catalog kinds and manifest metadata

Kanban: `c3814-s11`.
Status: done 2026-05-21.

Scope:

- Added catalog asset kinds for projectile and entity behavior assets.
- Added manifest/distribution metadata support for `.pdprojectile` and `.pdentity` entries.
- Kept `.pdentity` as behavior/archetype data and not an `ASSET_PROP` replacement.
- Added projectile `entity_ref` dependency registration and static coverage.

Exit gates:

- `.pdweapon`, `.pdprojectile`, and `.pdentity` resolve by catalog ID.
- Network manifest/catalog diff can name all three asset types without numeric identity leaks.
- Missing dependency diagnostics name the catalog ID.

### Slice 3: archive readers/writers and nested payload inventory

Kanban: `c3814-s12`.
Status: done 2026-05-21.

Scope:

- Production helper APIs now read archive text and descriptors for `weapon.ini`, `projectile.ini`, `entity.ini`, and graph roots.
- Base `.pdweapon` archives now include `nested_payloads.json` and the temporary legacy graph references it.
- Derived nested ID generation and duplicate-ID collision checks are implemented.
- Canonical archive-content SHA-256 is implemented over sorted uncompressed entry paths/bytes for disk archives and embedded in-memory archives.
- Nested payload inventory scan/format support is implemented; generated base payload archives move to Slice 4.

Exit gates:

- Helper-level archive inventory tests open `.pdweapon`, `.pdprojectile`, and `.pdentity` archives.
- Derived ID/collision helper tests pass.
- Canonical digest is stable across zip entry order and works for embedded archive bytes.

### Slice 4: base graph emitter

Kanban: `c3814-s13`.
Status: done 2026-05-21.

Scope:

- Convert current authored weapon data into initial graph JSON using the named v1 modules.
- Preserve exact timing units from current data instead of normalizing away source intent.
- Emit first nested payload ID list for Slayer rockets, rocket launcher homing rockets, Devastator rounds, grenades, mines, Dragon proxy, thrown Laptop Gun, and Laptop autogun.

Exit gates:

- All 86 base weapons emit a parseable `.pdweapon`.
- The graph contains no opaque C callback names except explicitly marked temporary adapters.
- The generated base graph inventory matches the audit matrix count.

### Slice 5: graph validator and deterministic IR compiler

Kanban: `c3814-s14`.
Status: done 2026-05-21.

Scope:

- Validate graph schema, pin names, module names, units, catalog references, and cycles.
- Compile graph JSON to deterministic runtime IR with stable opcode IDs and parameter blocks.
- Store dependency digests for cache invalidation.
- Reject arbitrary script/code text.

Exit gates:

- Validator accepts generated base assets and rejects malformed archive fixtures.
- Compiler output is deterministic across repeated runs.
- Runtime code does not parse editor-only graph affordances on the hot path.

Landed:

- Added `weapon_graph_runtime` validation for schema names, graph IDs, known module kinds, duplicate node IDs, edge references, cycles, catalog-style references, unsafe script-like modules, and explicit units for time-like parameters.
- Added deterministic IR compilation with stable opcode IDs, sorted parameter blocks, `source_sha256`, and `ir_sha256`.
- Added archive-file compilation through the shared graph archive reader for `.pdweapon`, `.pdprojectile`, and `.pdentity`.
- Added Debug Settings UI/config storage for the graph runtime toggle. Gameplay callsite use of that toggle is part of Slice 6 and later adapters.
- Verified focused compiler coverage, focused `[c3814]`, and all-target build logs under session `wpir`.

### Slice 6: held-weapon runtime adapter

Kanban: `c3814-s15`.
Status: done 2026-05-21; later projectile/entity/presentation parity slices closed 2026-05-25.

Scope:

- Feed existing held weapon behavior from compiled IR for non-physical modules first.
- Cover ammo gates/consume, cooldowns, hitscan, automatic cadence, burst, charge/release, beam tick, melee, specials, devices, reticles, overlays, zoom, animations, and ammo-driven model visibility.
- Keep legacy struct handoff only as the adapter boundary while callsites are converted.

Exit gates:

- Non-physical base weapon families can run from IR without behavior drift.
- Old authored weapon data is no longer the gameplay source of truth for converted modules.
- Focused parity tests cover fire cadence, ammo consumption, charge/release, and device toggles.

Landed so far:

- `.pdweapon` walker compiles/registers held IR from each weapon archive after the legacy pool payload is parsed.
- Shared gameplay accessors read graph-backed damage, impact force, fire-slot duration, numeric shoot sound, penetration, function flags, and max_rpm cadence when the Debug Settings graph runtime toggle is enabled.
- Direct held shooting helpers read graph-backed burst flags, ammo slot, spin-up/spin-down, muzzle flash flag, initial/max RPM, and ammo consumption when the toggle is enabled.
- Runtime adapter coverage now also captures symbolic SFX names, function type ids, recoil/recovery fields, throw activation/recovery, projectile/entity refs, projectile model refs, projectile scale/speed/distance/timer/reflect/sound fields, melee range, special function/recovery/sound, and device ids.
- Player held paths read graph-backed recoil/recovery, trigger dispatch type, throw/special/device state, fired/thrown projectile spawn parameters, sight/auto-aim type, and device toggles when the Debug Settings graph runtime toggle is enabled.
- The AI projectile launcher reads graph-backed projectile model, speed, distance, timer, flags, reflect angle, and launch sound for rocket/grenade/bolt launcher behavior.
- The old authored data is no longer the gameplay source for converted modules. Existing OG routines remain the execution backend for parity while graph-authored runtime records feed their values.

### Slice 7: projectile runtime adapter

Kanban: `c3814-s16`.
Status: done 2026-05-25.

Scope:

- Run fired/thrown physical behavior from `.pdprojectile` IR.
- Cover powered rockets, homing rockets, Slayer fly-by-wire, Devastator wall-hugger, grenade bounce/timer, N-Bomb flight, crossbow bolts, knives, mines, and thrown Laptop carrier behavior.
- Preserve current Laptop Gun early-instantiation detail if needed while keeping graph-facing carrier-to-entity semantics.

Exit gates:

- Projectile spawn, motion, guidance, sticky, bounce/slide, timer, impact, trail, and transition modules execute from IR.
- Slayer, grenade, mine, Dragon proxy, and thrown Laptop smoke/parity tests exist.

Landed:

- First runtime-record adapter layer landed 2026-05-24: `.pdprojectile` graph JSON registers from direct JSON, zip-openable `.pdprojectile` archives, or catalog-accessible graph files loaded through `fsFileLoad(source_path)`.
- Runtime records now capture spawn-state model/archive/source/function/scale/damage/flags, motion kind/speed/travel/timer/activation/recovery/reflect/powered/trajectory fields, homing, fly-by-wire, wall-hugger, sticky attach, bounce/slide, timer, impact, trail, transition-to-entity, and pickup/recover module data behind the Debug Settings graph runtime gate.
- Held graph projectile references now resolve at player fired-projectile, held-rocket, thrown-projectile, and AI projectile launch callsites.
- Projectile records drive model, scale, speed, travel distance, timer, reflect angle, powered/lightweight flags, trajectory, homing, fly-by-wire, pickup timer, and projectile object timer state.
- Existing OG projectile tick, guidance, impact, trail, and transition routines remain the execution backend so graph-authored values preserve original gameplay behavior.

### Slice 8: entity runtime adapter

Kanban: `c3814-s17`.
Status: done 2026-05-25.

Scope:

- Run deployed/armed entity behavior from `.pdentity` IR.
- Cover armed explosives, proxy triggers, remote detonatables, timed detonatables, N-Bomb storm creation, Laptop autogun, sticky mission devices, owner cleanup, and pickup/recover interaction.
- Keep runtime props as implementation objects; catalog entity assets own behavior/archetype data only.

Exit gates:

- Proxy mine, timed mine, remote mine, Dragon proxy, N-Bomb, deployed Laptop autogun, and sticky device behavior resolve through entity IR.
- Net authority and owner/team policy are authored data, not hidden hard-coded branches.

Landed:

- First runtime-record adapter layer landed 2026-05-24: `.pdentity` graph JSON registers from direct JSON, zip-openable `.pdentity` archives, or catalog-accessible graph files loaded through `fsFileLoad(source_path)`.
- Runtime records now capture shared archetype/model/source/timing/flags plus armed explosive, proxy trigger, remote detonatable, timed detonatable, N-Bomb storm, autogun, sticky device, owner cleanup, and interaction module data behind the Debug Settings graph runtime gate.
- Held/projectile entity references now resolve into thrown/deployed behavior paths.
- Entity records drive Laptop model, aim distance, ammo reserve, team policy, mine/proxy/timed arm timing, and proxy trigger radius.
- Existing OG mine, N-Bomb, Dragon, Laptop autogun, owner cleanup, and interaction routines remain the execution backend so graph-authored values preserve original gameplay behavior.

### Slice 9: parity and removal closure

Kanban: `c3814-s18`.
Status: done 2026-05-25.

Scope:

- Add static and runtime parity tests across weapon, projectile, and entity families.
- Add a hard static guard against reintroducing `.pdwpn` in live code/tests/examples.
- Record any temporary adapters with owner, removal condition, and test gap.
- Update docs and board when legacy data paths are fully retired.

Exit gates:

- Full build/test verification passes.
- Fresh `.pdwpn` search is clean outside historical context.
- Base weapon behavior coverage is marked runtime-complete, not just schema-complete.

Landed:

- Added runtime helper coverage for held-to-projectile/entity and projectile-to-entity reference resolution.
- Added static callsite guards for player, AI, projectile object-state, deployed entity, and sight/zoom presentation graph consumption.
- Added presentation capture for sight, zoom FOV, reticle, overlay, and camera-effect metadata; current sight/zoom accessors consume the graph fields.
- Verified focused `[modding][pdxxx][weapon_graph][runtime]`, focused `[c3814]`, and isolated all-target build under session `c3814`.

## Closure State

C-3814 is closed. Future weapon behavior work should continue to author behavior in graph records and feed the existing OG execution routines unless `c3840` or a later replacement card explicitly cuts over one behavior family with tests and playtest parity. `.pdwpn` remains unsupported outside historical notes.

## Sentinel

This document ends with the completed cutover order: implement archives, emit graphs, compile IR, adapt weapons, adapt projectiles, adapt entities, close parity.
