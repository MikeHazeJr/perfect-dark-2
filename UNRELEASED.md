# Unreleased Changes

> Running release-note source for the next GitHub release.
> Keep bullets short, player-facing where possible, and specific to what changed.

## Highlights

- Dev Window v2 now treats the Kanban board, Codex memory mirror, and release-note source as live project state that should commit with the code.
- Git Pull in Dev Window v2 now fetches remote commits first and lets the operator choose the exact commit to fast-forward to.
- Release notes now come from this running list instead of the old dedicated-server placeholder text.
- Settings now has a simplified Input tab for profiles, connected devices, bindings, and tuning.

## Added

- Added a tracked Codex memory mirror at `tools/kanban/memories.md` so GitHub can carry the project's active memory state alongside code and Kanban state.
- Added shared project-state sync tooling for Dev Window v2 and the release script.
- Added Codex daily automations for the morning maintenance flow and architecture review.
- Added Bug Tracker delete support in the Kanban browser.
- Added a reusable PD2 large-change sweep skill for auditing broad changes against runtime code, tests, context, Kanban, and release notes.
- Added Dev Window v2 `Start Kanban Server` and `Stop Kanban Server` buttons for remote Kanban phone access.
- Added a phone-first Kanban layout with filter modal, full-screen card editing, mobile card ordering controls, and card-scoped Codex session launch/status output.
- Added full-screen Kanban Codex session viewing with response-only transcripts, phone follow-up messaging, queued prompt steering, tap-to-answer plan-mode questions, and ad-hoc session launch without requiring a card.
- Added an Asset Decisions Kanban tab, including mobile portal access, for reviewing each `.pdxxx` archive recommendation, requested decision, status, and Mike's notes before implementation cards are updated.
- Added a card-specific Kanban Decisions workspace that stores selected-card progress, context, card memories, recommendations, Mike decisions, and next actions without deleting inactive-card notes.
- Added c3813 online lifecycle guards for listen-host/client smoke coverage and reconnect/drop-in/drop-out state restoration.
- Added named input profile slots and per-controller profile assignment, including custom/raw controller devices.
- Added Blender-ready map visual exports inside scenario/arena archives, including OBJ/MTL scenes, decoded TGA wall/floor textures, and material TSV ledgers.
- Added self-contained weapon archives that embed model, animation, audio, projectile, and entity payloads for editing and sharing.
- Added a Blueprint-style Modding Hub weapon graph node editor for modular primary/secondary subgraphs, shared owner/damage/detonator/targeting context, presets, draggable nodes, links, inspector editing, and JSON validation.
- Added an Asset Pipeline planning card for per-family mod utility flows behind the clean archive format decisions.
- Added frozen clean Asset Pipeline archive layout contracts for all current typed families, including `.pdui`, `.pdfont`, and `.pdlang`.
- Added a final Asset Pipeline extraction handoff card, modularization verification audit, and load/use closure matrix for all approved `.pdxxx` game-content archive families.
- Added a shared typed archive writer for the Asset Pipeline extraction sweep, covering root descriptors, `_meta` manifests, inventories, provenance, validation, source handles, hashes, and SHA sidecars.
- Added Codex and repo guard hooks for the Asset Pipeline native-source contract, including a pre-commit guard and focused `[modding][pdxxx][c3842]` static tests.
- Added shared per-family Asset Pipeline utility contracts for clean typed archives, including Modding Hub visibility and secure `.pdtool` policy.
- Added a reusable gameplay graph editor foundation with typed pins, pin-colored links, compatibility checks, and asset-family adapters.
- Added named weapon graph parity modules so current OG-backed held, projectile, and deployed-entity behavior families are explicit before future retirement cuts.
- Added strict typed asset archive conformance validation for every `.pdxxx` family, including recursive embedded archive checks and catalog-ID-only public references.
- Added base extraction and native catalog/provider loading for the remaining `.pdmaterial`, `.pdtexture`, `.pdskin`, `.pdeffect`, `.pdprop`, `.pdvehicle`, `.pdmission`, `.pdgamemode`, `.pdbotprofile`, `.pdhud`, and `.pdtheme` archive families.
- Added a Settings > Debug asset source gate so one typed archive family at a time can be forced to use extracted/generated FileProvider source during playtest.

## Changed

- Dev Window build, release, and push sync commits now describe the staged files and include live state paths in the commit body.
- The release script mirrors Codex memory before release commits and warns if the notes file looks stale.
- Memory Review now parses live memory task groups and stores review markup separately from the source memory file.
- Active Kanban now uses a numbered two-pane priority layout with manual reorder, docked card actions, and saved AI special notes.
- Kanban remote mode now uses token-gated access over a localhost-only Cloudflare tunnel, without changing system routing or proxy settings.
- Local `Open Kanban` now stays tokenless even when a remote token-gated Kanban tunnel exists, and remote startup no longer occupies the normal local board port.
- Kanban card sessions now launch with the selected card plus context about other active cards and running card sessions, and can be resumed from the phone UI for follow-up turns, queued steering prompts, or choice-question answers.
- The old Asset Decisions tab is now the Card Decisions workspace, while the prior asset archive decision data remains preserved on `c3824`.
- Asset Pipeline tracking now closes the completed migration/runtime cards and moves future mod utility and reusable node-editor work out of the active migration lane.
- Weapon behavior graphs now drive held, projectile, deployed-entity, and sight/zoom presentation runtime values behind the debug graph-runtime toggle.
- Projectile and deployed-entity behavior graphs now compile into runtime records from accessible graph files and feed the existing Perfect Dark execution paths for gameplay parity.
- C-3838 runtime bindings now cover all approved file-backed `.pdxxx` families, including `.pdprop`, with type/id/target/kind lookups plus primary-file accessibility and load validation.
- Catalog-generated base asset IDs and typed archive references now use readable names instead of legacy numeric handles.
- Scenario archive extraction no longer writes raw setup/mpsetup/visual word dumps as public payloads, and typed archive guards now reject numeric or legacy-symbol asset references in authoring files.
- Scenario and mission archives now use source-first public payloads: `.pdscenario` emits `scene.glb`, decoded catalog-ID setup tables, navigation inputs, level graph JSON, and generated collision/navmesh metadata, while `.pdmission` carries `mission.graph.json`.
- Base weapon archives now use authored primary/secondary graph files directly at runtime instead of shipping a generated public `behavior/runtime.graph.json` duplicate.
- Weapon graph runtime records now retain the named `og.*` parity module selected by each held, projectile, or entity graph record.
- Gamemode and bot profile archives now require their public rule/profile source files for runtime activation.
- Replaced the old Settings Controls surface with a single actionmap-backed Input binding table.
- Main Menu now presents Play instead of Solo Play and removes the old Online Play direct-connect entry; online friend play now routes through Social invites/joins.
- Social friend invites are available even when a friend row is showing a stale Offline state.
- The Modding Hub weapon template flow now drills into an in-place tabbed weapon-creation view with auto-populated template refs, primary/secondary graph tabs, and an opaque mesh picker with live preview.
- Typed asset archive emitters now write machine metadata and hash sidecars under `_meta/`, while migration readers still accept legacy root metadata during the cleanup window.
- `.pdlang` base extraction now uses the shared typed archive writer and emits standardized `_meta` inventory, hashes, provenance, validation, and source-handle metadata around editable `strings.tsv`.
- Typed archive policy, scanners, packers, Mod Manager lists, hot-distribution registration, and debug tooling now recognize approved first-class asset families through `.pdtheme`; `.pdfont` is `ASSET_FONT`, `.pdscenario` is `ASSET_SCENARIO`, and `.pdtool` remains deferred.
- Match manifests now carry approved typed dependency families through a generic asset entry, and `_meta/manifest.json` dependency records are validated for embedded archives or explicit base dependency records.
- Typed archive public files are now validated through mounted `.pdmod` transport paths, including `archive.pdxxx::file` access through VFS, `fs`, and FileProvider-style loaders.
- Saved skin mods now declare their `texture.tga` payload in `skin.ini` and register that texture through the catalog/FileProvider path.
- Typed asset archive validation now recursively checks descriptor, GLTF, OBJ/MTL, JSON, and embedded typed-archive references so clean `.pdxxx` packages fail when their authored closure is incomplete.
- Typed asset archive validation now enforces the full planned family schema, not just the presence of an authorable file; fresh extracted base archives now pass the strict checker.
- Typed asset archive validation now enforces definitive optional-slot contracts, including documented role/owner/load/absence/status entries for every optional public slot and `_meta/` whitelist entry.
- Typed asset archive validation now rejects catalog-looking references that do not match actual declared/root/nested catalog IDs, including public TSV/CSV table columns and `_meta/manifest.json` dependency records.
- Base extracted content now includes all 27 typed asset families as strict on-disk archives, and fresh extraction passes strict conformance with `--require-all-families`.
- Runtime ROM fallback after extraction is now tracked as an Asset Pipeline failure condition; `c3844` owns removal/fatal hardening of remaining runtime fallback paths.
- Source-only asset checks now reject raw extracted ROM cache paths such as `data/<romid>/files`, `data/<romid>/segments`, and `.bin` payloads instead of treating every FileProvider path as clean public source.
- Scenario pads now compile from public `.pdscenario` `pads.tsv` during stage load instead of relying on the legacy pad payload when public source is available.
- Scenario archives now include decoded public waypoint, waygroup, and cover tables, and stage load compiles those tables with public `pads.tsv` instead of disabling navigation.
- Scenario archives now feed public patrol paths and AI `set_path` / `start_patrol` graph nodes into runtime AI path behavior.
- Generated scenario GLBs now apply renderer-matched texture scale and per-material sampler wrap modes so Blender/3DS Max imports match the game more closely.
- Scenario and Mission graph sources now activate during stage load from public archive members, proving `level.graph.json` and `mission.graph.json` are accessible to the runtime before behavior parity cutover.
- Scenario level graph table refs now select the public setup, pads, objective, and navigation source members consumed by runtime loaders.
- Mission archives now generate objective source and parity-backend graph nodes instead of empty graph placeholders or `original_perfect_dark_setup` pointers.
- Mission archives now generate per-objective and per-criteria graph nodes from decoded scenario objective rows, and runtime objective checks validate against the active mission graph before returning the current parity result.
- Mission objective Enter Room, Throw In Room, and Holograph criteria now keep their mutable status in graph-owned runtime state instead of reading legacy criteria status during graph evaluation.
- Mission objective completion/fail flag criteria now read graph-owned mission flag state instead of consulting the legacy stage-flag helper during graph evaluation.
- Mission objective Destroy, Collect, Throw, and Holograph criteria now read tagged object present/healthy/held state from graph-owned runtime state instead of querying object and inventory state during graph evaluation.
- Scenario setup behavior links now validate against graph-owned source records before live registration, covering linked guns, lift-door links, safe-item/padlock links, conditional scenery, and blocked paths.
- Scenario level graphs now bind and parse public `volumes.tsv` into graph-owned trigger-volume source rows during stage load.
- Enter Room and Throw In Room objective criteria now use graph-owned trigger-volume rows when a level graph is active, with the legacy room check kept only as the inactive-graph fallback.
- Scenario level graphs now emit explicit trigger-volume graph nodes, and stage activation validates those nodes against public `volumes.tsv` rows before trigger-volume source can be used.
- Scenario level graphs now emit a required global-settings source node, and stage activation validates it from public `.pdscenario::level.graph.json` before accepting the level graph.
- Scenario source activation now derives the matching `.pdscenario` catalog row from the active stage catalog ID when no explicit scenario ref is present.
- Mission graphs now emit required phase source nodes, and runtime records mission `load`/`active`/`complete`/`failed`/`end` transitions through the active `.pdmission` graph source.
- Scenario archives now include public `navigation/paths.tsv`, and stage setup compiles those patrol paths from the archive source so AI patrol startup does not depend on ROM setup data.
- Scenario level graphs now emit required AI pad movement action nodes, and runtime routes `jog_to_pad` / `go_to_pad_preset` / `walk_to_pad` / `run_to_pad` through graph-owned public `pads.tsv` source before the parity movement routine runs.

## Fixed

- Fixed generated scenario `scene.glb` texture scale so Chicago and other extracted maps open in DCC tools with normalized UVs, renderer tile shifts, and sampler modes instead of stale tiny repeated tiling.
- Fixed `.pdmesh` skeleton metadata export/loading so legacy small skeleton IDs like `SKEL_HEAD` are not treated as pointers, allowing all-model extraction and source-gated weapon matches to complete from public archive sources.
- Fixed held weapon model loading so modeldefs resolve through catalog/FileProvider `.pdmesh::model.obj` sources and refuse ROM fallback under the asset-source contract.
- Scenario setup overlays now compile from public `.pdscenario` setup source in source-gated match startup, and base `.pdmesh` extraction now emits every catalog model archive so scenario model references resolve under strict conformance.
- Scenario stage load now uses public `.pdscenario` `scene.glb` or an optional collision override as the source-derived world mesh when available.
- Scenario archives now include `setup.fields.tsv` as a named per-command setup source table, avoiding raw setup word dumps while preserving a complete authoring path for setup parity.
- Public metadata-family typed archives now use named selector keys for game modes, bot profiles, HUD elements, effects, props, arenas, and scenarios instead of public numeric selector fields.
- Universal extracted archive walkers now bind game-facing catalog rows to public archive-member sources such as `archive.pdxxx::scene.glb`, so runtime activation uses the same user-editable files the archives expose.
- Weapon language IDs now preserve the full `L_GUN_*` range used by extracted weapon function labels, including DY-357 primary/secondary fire-mode text.
- Typed `.pdxxx` examples now cover every frozen family, including projectile, entity, material, texture, and character samples, with `_meta/manifest.json` and the frozen `.pdmesh` `mesh.ini` descriptor.
- Release packaging now fails stale typed asset outputs before zipping if they are `.pdwpn`, non-zip, descriptor-less, legacy `.pdmesh` `model.ini`, root-metadata, `.bin`-backed archives, typed-family `.zip` inspection copies, or loose extracted typed archive folders; arena/scenario extraction now also invalidates stale clean-shape caches with nested metadata clutter.
- Saving a weapon mod now opens a confirmation modal with Creator and Display Name, derives the custom weapon catalog name as `mod:weapon_<name>`, and enables the new mod immediately.
- Cutscene skipping now uses a held button with a contextual radial progress prompt instead of an accidental tap.
- Startup asset extraction now reuses validated per-family cache stamps and shows a centered progress modal with smoother time-weighted progress.
- Tiny Mode now makes spoken character voice lines play at a more comical high pitch, and the actual tripled tiny enemies now speak even higher and slightly louder.
- Tiny Mode now triples ordinary non-unique enemy spawns into spaced-apart tiny enemies, with those tiny generic enemies moving about 30% faster than before, while keeping named/story characters single.
- Tiny Mode now leaves the player at normal size, camera height, movement scale, shadow size, and shelf-pickup range.
- Tiny Mode is now the only small-character cheat shown in Cheats; the old Small Characters slot is hidden as a legacy id.

- Fixed base Falcon 2 Silencer/Scope weapon archives so extracted `.pdweapon` files bind to the same catalog IDs requested during mission loads, while preserving base MP weapon runtime bindings.
- Fixed weapon source-only loading so generated `.pdweapon` archives bind as FileProvider sources for canonical base weapon IDs such as `base:dy357`.
- Fixed DY-357 fire-mode labels under extracted weapon loading by restoring missing gun language entries and pinning `L_GUN_*` enum resolution.
- Fixed generated body/head public-source modeldefs so extracted archive source bodies can instantiate through the character model path without falling back to Dark Combat.
- Fixed metadata archive scanning and distribution registration so named public selector keys are consumed natively while stale numeric public selectors are rejected by conformance guards.
- Fixed scenario source activation so authored collision can be compiled directly from public `scene.glb` when no collision override is present, with degenerate source triangles skipped instead of forcing fallback.
- Fixed Scenario source-only playtests so setup, briefing setup, pads, and tiles now fail loudly if their stage handles still point at ROM/RomProvider instead of extracted FileProvider source.
- Fixed source-only validation so raw extracted ROM dump/cache files cannot satisfy the public typed-archive source requirement.
- Fixed Scenario navigation source loading so decoded waypoint, waygroup, and cover TSV files build valid runtime navigation tables from `.pdscenario` archives.
- Fixed Scenario graph-source runtime coverage so Chicago stage load validates `base_scenario_chicago.pdscenario::level.graph.json` and `base_mission_chicago.pdmission::mission.graph.json` through real archive-member file loads.
- Fixed Scenario graph-source selection so Chicago stage load binds `level.graph.json` table refs before compiling public pads/navigation source.
- Fixed source-gated Scenario startup so stage-owned scenario archives bind by catalog name instead of missing the public source row.
- Fixed stale mission archive regeneration and validation so empty `mission.graph.json` files and `original_perfect_dark_setup` objective rows are rejected.
- Fixed mission objective graph validation so generic placeholder objective nodes are rejected and Chicago smoke proves objective checks route through `base_mission_chicago.pdmission::objectives.tsv`.
- Fixed a source-gated Combat Simulator match crash by bounding MP setup AI-list sorting so it cannot overwrite source-compiled pad data.
- Fixed SP-in-MP transport setup handling under the source contract so missing public setup source skips the raw overlay instead of loading raw setup data.
- Fixed generated weapon graph/audio references so high-bit SFX aliases resolve to actual leaf `.pdsfx` catalog IDs before graph, binding, dependency, and manifest emission.
- Fixed Dev Window v2 release commits on Git Bash/MSYS Python by running the Asset Pipeline pre-commit guard through a repository-relative path and removing the automatic hook-bypass retry.
- Fixed typed asset extraction cleanup so arena/scenario/mesh `.zip` inspection copies do not survive fast-cache skips, and embedded arena scenario folders no longer grow duplicate nested `_meta` sidecar trees.
- Fixed first-run `.pdtexture` extraction by correcting the `texture.ini` format string so texture metadata generation no longer shifts its arguments and crashes.
- Fixed the sample `.pdmesh` archive so mesh textures are no longer loose public sidecars; mesh materials/textures now stay in typed dependencies or the model source.
- Removed the outdated v0.0.7 dedicated-server release notes that were being reused for new releases.
- Cleared stale Kanban Bug Tracker rows B-318 through B-326.
- Fixed Combat Simulator post-match endscreen X and Quit/Disconnect confirmation clicks so the visible results screen can be exited normally.
- Fixed a Combat Simulator restart exception after leaving a match by clearing stale MP runtime character slots before the next start.
- Fixed a client build break in graph-backed projectile setup by making the projectile runtime type visible to `projectileApplyGraphRuntime()` declarations.
- Fixed online post-match return-to-room resync so clients replay room assignment, match settings, and playlist state instead of returning with partial room state.
- Fixed friend presence so existing agents using per-agent client codes start the social hub, validate signed presence, appear online, and use invite/join handoffs through the NAT-aware path.
- Fixed failed online match asset transfers so active match prep declines the manifest and returns to lobby instead of repeatedly requesting the same failed content.
- Fixed listen-host logging during early startup so hosted smoke and diagnostics write to the host log before normal system init.
- Fixed Main Menu Escape/title-X closing so the menu actually exits instead of playing cancel and reopening.
- Fixed Main Menu entry timing so the Carrington Institute camera intro finishes before the menu opens and accepts input.
- Fixed gameplay interaction prompts so labels like Open door do not appear during cutscenes.
- Fixed Falcon 2 in-game barrel stretch by making the laser beam endpoint move with the muzzle instead of staying pinned to the crosshair during weapon-root animation.
- Fixed mouse/keyboard Campaign weapon switching: Q tap/hold, scroll wheel next/previous, number-key direct select, and equipped primary/secondary function labels now stay tied to the actual weapon instead of stale inventory or transition state.
- Fixed an Infiltration campaign exception by making robot muzzle flash and robot attack setup fail closed when robot model parts or target state are incomplete.
- Fixed weapon OBJ extraction so held meshes apply model matrices before being embedded in `.pdweapon` archives, and Falcon 2 exports no longer include the detached skewed effect group.
- Fixed custom weapon authoring so saved weapon mods no longer expose or write numeric weapon IDs; catalog names are used for author-facing references.
- Fixed weapon-mod saving diagnostics and template payload handling so shotgun-based dual-wield saves report each save stage in the log and preserve archive-local template model and animation files.
- Fixed Combat Simulator weapon and arena pickers so catalog entries are alphabetized, custom weapon mods appear by catalog name, and No Score Limit stores the runtime unlimited sentinel.
- Fixed Cheats menu controller activation for focused cheat rows and the Unlock All action.
- Fixed weapon behavior graph editing so template graphs load with connected trigger/action nodes, left/right docked pins, pin-colored wires, inspector link lists, larger exec sockets, and simple node-param controls.
- Fixed a match-start crash when a saved `.pdweapon` mod was selected in Combat Simulator custom weapon slots.
- Fixed jump collision follow-through so airborne horizontal movement is clamped against rendered wall, ceiling, and corner geometry before the player can clip into it.
- Fixed generated scenario GLB texture export so Blender/3DS Max receive renderer-matched texture scaling, tile shifts, and per-material wrap, mirror, and clamp sampler modes.
