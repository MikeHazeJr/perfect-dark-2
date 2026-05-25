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
- Replaced the old Settings Controls surface with a single actionmap-backed Input binding table.
- Main Menu now presents Play instead of Solo Play and removes the old Online Play direct-connect entry; online friend play now routes through Social invites/joins.
- Social friend invites are available even when a friend row is showing a stale Offline state.
- The Modding Hub weapon template flow now drills into an in-place tabbed weapon-creation view with auto-populated template refs, primary/secondary graph tabs, and an opaque mesh picker with live preview.
- Typed asset archive emitters now write machine metadata and hash sidecars under `_meta/`, while migration readers still accept legacy root metadata during the cleanup window.
- `.pdlang` base extraction now uses the shared typed archive writer and emits standardized `_meta` inventory, hashes, provenance, validation, and source-handle metadata around editable `strings.tsv`.
- Typed archive policy, scanners, packers, Mod Manager lists, hot-distribution registration, and debug tooling now recognize approved first-class asset families through `.pdtheme`; `.pdfont` is `ASSET_FONT`, `.pdscenario` is `ASSET_SCENARIO`, and `.pdtool` remains deferred.
- Match manifests now carry approved typed dependency families through a generic asset entry, and `_meta/manifest.json` dependency records are validated for embedded archives or explicit base fallback reasons.
- Typed archive public files are now validated through mounted `.pdmod` transport paths, including `archive.pdxxx::file` access through VFS, `fs`, and FileProvider-style loaders.
- Saved skin mods now declare their `texture.tga` payload in `skin.ini` and register that texture through the catalog/FileProvider path.
- Typed asset archive validation now recursively checks descriptor, GLTF, OBJ/MTL, JSON, and embedded typed-archive references so clean `.pdxxx` packages fail when their authored closure is incomplete.
- Typed `.pdxxx` examples now cover every frozen family, including projectile, entity, material, texture, and character samples, with `_meta/manifest.json` and the frozen `.pdmesh` `mesh.ini` descriptor.
- Release packaging now fails stale typed asset outputs before zipping if they are `.pdwpn`, non-zip, descriptor-less, legacy `.pdmesh` `model.ini`, root-metadata, or `.bin`-backed archives.
- Saving a weapon mod now opens a confirmation modal with Creator and Display Name, derives the custom weapon catalog name as `mod:weapon_<name>`, and enables the new mod immediately.
- Cutscene skipping now uses a held button with a contextual radial progress prompt instead of an accidental tap.
- Startup asset extraction now reuses validated per-family cache stamps and shows a centered progress modal with smoother time-weighted progress.
- Tiny Mode now makes spoken character voice lines play at a more comical high pitch, and the actual tripled tiny enemies now speak even higher and slightly louder.
- Tiny Mode now triples ordinary non-unique enemy spawns into spaced-apart tiny enemies, with those tiny generic enemies moving about 30% faster than before, while keeping named/story characters single.
- Tiny Mode now leaves the player at normal size, camera height, movement scale, shadow size, and shelf-pickup range.
- Tiny Mode is now the only small-character cheat shown in Cheats; the old Small Characters slot is hidden as a legacy id.

## Fixed

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
