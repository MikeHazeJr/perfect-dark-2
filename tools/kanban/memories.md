# Task Group: PD2 Gameplay Audio Polish
scope: Cheat-driven voice/audio behavior, spoken-line routing, and focused verification for narrow gameplay polish changes.
applies_to: cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike; reuse_rule=reuse for PD2 audio/gameplay polish in this checkout when the request targets a specific runtime audio behavior rather than a broad audio-system redesign.

## Task 1: Raise Tiny Mode voice-line pitch slightly across both dialogue and character-talk playback, success

### rollout_summary_files

- rollout_summaries/2026-05-23T20-44-50-ot87-tiny_mode_voice_pitch.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\sessions\2026\05\23\rollout-2026-05-23T16-44-55-019e5695-5e5f-7232-9526-7e8946ce81ab.jsonl, updated_at=2026-05-23T21:02:18+00:00, thread_id=019e5695-5e5f-7232-9526-7e8946ce81ab, cheat-gated voice-pitch polish with focused static coverage)

### keywords

- CHEAT_SMALLJO, Tiny Mode, voice pitch, sndApplyTinyVoicePitch, sndStart, sndAdjust, PSTYPE_CHRTALK, propsnd, AL_SNDP_PITCH_EVT, [audio][voice][tiny][static], BuildTimeoutSeconds

## Task 2: Correct Tiny Mode so the player stays normal while only tiny generic enemies keep size, speed, and boosted voice treatment, success

### rollout_summary_files

- rollout_summaries/2026-05-23T17-11-59-Njc1-tiny_mode_enemy_voice_and_player_normalization.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\sessions\2026\05\23\rollout-2026-05-23T13-12-04-019e55d2-7fe3-72e1-9031-89e7b1ba963d.jsonl, updated_at=2026-05-23T23:55:36+00:00, thread_id=019e55d2-7fe3-72e1-9031-89e7b1ba963d, Tiny Mode contract correction for player-normal behavior and enemy-only voice boost)

### keywords

- Tiny Mode, CHEAT_SMALLJO, CHRCFLAG_TINYMODE_MOVESPEED, sndApplyTinyVoicePitchForProp, sndApplyTinyVoiceVolumeForProp, propsnd, player-normal, enemy tripling, voice pitch, volume boost, tests/test_tiny_mode_spawn_static.cpp, commit hook

## User preferences

- when the user asked: "make all voice lines play at a slightly higher pitch" -> scope similar requests narrowly to the named gameplay/audio behavior instead of broad audio-pipeline redesign [Task 1]
- when the user corrected the player-side effect with "I didn’t want the player to be tiny" -> keep Tiny Mode scoped to ordinary enemies, not player size, camera, movement, shadow, or pickup behavior [Task 2]
- when the user asked "Make the tiny guys voices higher pitched, slightly louder" -> apply follow-up audio polish to the actual tiny enemies and include the slight loudness lift the user asked for, not pitch alone [Task 2]

## Reusable knowledge

- `CHEAT_SMALLJO` is the Tiny Mode cheat gate for this behavior [Task 1]
- To catch all spoken lines, cover both the config-driven voice/dialogue path in `sndStart()` and `PSTYPE_CHRTALK` prop-sound playback, because some barks use plain SFX ids instead of the high-bit voice-config path [Task 1]
- A shared helper in `src/lib/snd.c` with a declaration in `src/include/lib/snd.h` is a workable central point when multiple call sites need the same voice-pitch rule [Task 1]
- `CHRCFLAG_TINYMODE_MOVESPEED` is the existing marker for the tripled tiny generic enemies and is now the prop-scoped signal for enemy-only Tiny Mode voice treatment too [Task 2]
- The final audio behavior is split into two layers: direct Tiny Mode voice-config starts keep the existing `1.35x` baseline, while actual tiny enemies get stronger prop-scoped treatment (`1.6x` pitch and `1.15x` volume) through `sndApplyTinyVoicePitchForProp()` and `sndApplyTinyVoiceVolumeForProp()` [Task 2]
- Player-side Tiny Mode effects were removed from `src/game/body.c`, `src/game/bondwalk.c`, `src/game/bondmove.c`, `src/game/bondgrab.c`, `src/game/chr.c`, and `src/game/propobj.c`; static coverage in `tests/test_tiny_mode_spawn_static.cpp` now guards the player-normal contract [Task 2]
- The focused static selector for this lane is `[audio][voice][tiny][static]`, and coverage now spans both `tests/test_audio_voice_retag.cpp` and `tests/test_tiny_mode_spawn_static.cpp` [Task 1][Task 2]

## Failures and how to do differently

- If a clean all-target `build-session` run gets killed by the wrapper's default 60-second active-build watchdog while the linker is still making progress, rerun the same session with a longer `-BuildTimeoutSeconds` before treating it as a code failure [Task 1]
- A global Tiny Mode voice-pitch helper was too broad once the user asked specifically about "the tiny guys"; future tiny-enemy audio changes should key off the enemy instance/prop marker rather than the cheat toggle alone [Task 2]
- Tiny Mode can leak into player-facing movement/view code; before shipping follow-up Tiny Mode edits, explicitly scan player-side `CHEAT_SMALLJO` uses in body, walk, move, grab, shadow, and pickup paths [Task 2]
- Commit message hooks enforce the pillar/card prefix convention in this repo; for this lane the accepted prefix was `Input - c036: ...` after an earlier mismatch was rejected [Task 2]

# Task Group: PD2 Shared Gameplay Node Editor Planning
scope: Reusable graph-editor foundation planning, active-card ordering, and context updates for modding workflows that extend beyond weapon-only graphs.
applies_to: cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike; reuse_rule=reuse for PD2 graph-editor/modding planning work in this checkout when the task is about card framing, node-editor scope, or board/context ordering.

## Task 1: Stage a reusable shared gameplay node-editor foundation card behind the Asset Pipeline decisions, success

### rollout_summary_files

- rollout_summaries/2026-05-22T03-51-04-lwD5-shared_gameplay_node_editor_kanban_card.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\sessions\2026\05\21\rollout-2026-05-21T23-51-04-019e4dce-e3c1-7183-b352-d4056d84700f.jsonl, updated_at=2026-05-23T16:50:57+00:00, thread_id=019e4dce-e3c1-7183-b352-d4056d84700f, added `c3835` and updated project context)

### keywords

- c3835, shared gameplay node editor foundation, node editor, imgui-node-editor, typed pins, wire colors, gameplay scripts, c3824, c3832, c3834, order 4000, context/pillars/modding.md

## User preferences

- when the user said the node editor should eventually serve gameplay scripts beyond weapon graphs -> frame future graph-editor work as a reusable foundation rather than a weapon-only feature [Task 1]
- when the user emphasized that wires should use the colors of the pins they connect -> treat wire color as a first-class UX rule, not a cosmetic afterthought [Task 1]
- when the new card was placed directly behind `c3824`, `c3832`, and `c3834` -> keep shared node-editor work ordered after archive-format and per-family utility-definition decisions [Task 1]

## Reusable knowledge

- The active modding ordering chain is now `c3824` umbrella archive migration -> `c3832` clean `.pdweapon` format -> `c3834` per-family mod utilities -> `c3835` shared gameplay node-editor foundation [Task 1]
- The new foundation card is intentionally broader than the current weapon graph UI: typed pins, wire colors derived from connected pin types, graph-model/render separation, shared contexts, inspector editing, validation UX, saved layouts, and reuse for future gameplay-script graphs [Task 1]
- When inserting a new active card ahead of existing items, update both `tools/kanban/state.json` and project context surfaces such as `context/tasks.md`, `context/pillars/modding.md`, and `context/session-log.md` so the priority is visible outside the board file [Task 1]

## Failures and how to do differently

- No technical failure occurred here, but future board-order changes should be reflected in both Kanban and project context files rather than only in `state.json` [Task 1]

# Task Group: PD2 Asset Pipeline: Typed pdxxx Asset Archives
scope: Native asset authoring, storage, sharing, validation, and runtime consumption for base-game and mod content.
applies_to: cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike; reuse_rule=reuse for PD2 asset-pipeline, base-content rebuild, mod content, Public Mods, and online-required-content work when the typed `*.pdxxx` contract is in scope.

## Task 1: Rebuild project memory around the unified asset-pipeline framing, success

### rollout_summary_files

- rollout_summaries/2026-05-20T21-10-40-xs8l-pd2_memory_rebuild_and_asset_pipeline_correction.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\sessions\2026\05\20\rollout-2026-05-20T17-10-40-019e4739-f3f8-71a3-be12-7d536b0b67fd.jsonl, updated_at=2026-05-20T21:17:38+00:00, thread_id=019e4739-f3f8-71a3-be12-7d536b0b67fd, corrected memory 1 from Mod Pipeline to Asset Pipeline)
- rollout_summaries/2026-05-19T19-50-11-GEOb-kanban_priority_pass_mod_input_collision_dropout_stability.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\sessions\2026\05\19\rollout-2026-05-19T15-50-11-019e41c9-e543-7201-b10b-84ae56099f87.jsonl, updated_at=2026-05-20T21:25:10+00:00, thread_id=019e41c9-e543-7201-b10b-84ae56099f87, confirms `c3812` as the active typed-archive repair card)

### keywords

- Asset Pipeline, typed pdxxx, .pdmod, c3812, memory-rebuild.md, base-game assets, mod assets, self-contained asset archives

## Task 2: Verify and expand self-contained typed archive coverage across every current asset family, success

### rollout_summary_files

- rollout_summaries/2026-05-20T21-25-27-TDyP-pdxxx_archive_repair_all_asset_families.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\sessions\2026\05\20\rollout-2026-05-20T17-25-27-019e4747-7bb7-7111-9c33-66edcee213f3.jsonl, updated_at=2026-05-20T23:43:26+00:00, thread_id=019e4747-7bb7-7111-9c33-66edcee213f3, expanded examples and dependency-closure tests for all current typed families)

### keywords

- c3812, typed-pdxxx-basic, self-contained archive, dependency closure, assetcatalog_scanner, model_file, hand_model_file, geometry_file, strings_tsv, c3809

## Task 3: Complete `.pdweapon` self-contained dependency closure and record the archive-contract cut line, success

### rollout_summary_files

- rollout_summaries/2026-05-21T00-04-19-UIZy-pdweapon_self_contained_closure.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\sessions\2026\05\20\rollout-2026-05-20T20-04-19-019e47d8-ecb6-7093-b728-38f6a580fd88.jsonl, updated_at=2026-05-21T19:04:37+00:00, thread_id=019e47d8-ecb6-7093-b728-38f6a580fd88, `.pdweapon` packaging/export now embeds authored dependency closure; runtime parity remains separate)

### keywords

- pdweapon, pdprojectile, pdentity, dependency_closure, embedded.v2, modVfs, fsDataPathFor, romextract_pdweapon, c3814, no .pdwpn

## Task 4: Compare clean self-contained archive layouts and stage the family-by-family cleanup card, success

### rollout_summary_files

- rollout_summaries/2026-05-21T19-30-37-c7Ve-asset_pipeline_clean_archive_layout_kanban_card.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\sessions\2026\05\21\rollout-2026-05-21T15-30-37-019e4c04-b49e-7453-9811-5e7a1a982c78.jsonl, updated_at=2026-05-21T19:46:41+00:00, thread_id=019e4c04-b49e-7453-9811-5e7a1a982c78, examples-vs-output audit and new `c3824` layout cleanup card)

### keywords

- typed-pdxxx, self-contained archive, examples.zip, _meta, c3824, clean layout, root descriptor, release tree audit

## Task 5: Fix archive-level misses exposed by the Combat Simulator audit run, success

### rollout_summary_files

- rollout_summaries/2026-05-21T14-36-44-FI4k-combat_sim_start_crash_and_extraction_fallback_fix.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\sessions\2026\05\21\rollout-2026-05-21T10-36-44-019e4af7-a56e-7883-9a83-543e185f5924.jsonl, updated_at=2026-05-21T16:00:50+00:00, thread_id=019e4af7-a56e-7883-9a83-543e185f5924, typed archive and fallback misses uncovered during Combat Simulator crash work)

### keywords

- pdanim, pdmesh, CATALOG.MISS, MANIFEST-SP: late-add, failed=0, typed archive, GDL decode, segment-4 vertex, base:sp_body_108

## Task 6: Define the clean `.pdweapon` archive format and make the broader rollout explicitly freeze formats before the sweep, success

### rollout_summary_files

- rollout_summaries/2026-05-22T12-51-20-xKwG-pdweapon_format_process_order_and_asset_pipeline_reprioritiz.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\sessions\2026\05\22\rollout-2026-05-22T08-51-25-019e4fbd-849e-7e92-8a0d-1042167f762a.jsonl, updated_at=2026-05-22T20:25:26+00:00, thread_id=019e4fbd-849e-7e92-8a0d-1042167f762a, `c3824`/`c3832` process-order clarification and clean `.pdweapon` format note)

### keywords

- c3824, c3832, pdweapon, freeze contracts, implementation sweep, _meta, weapon.ini, process order, asset family formats

## User preferences

- when project memory or docs describe the archive system, the user corrected: "it isn’t really the Mod Pipeline ... it is the Asset pipeline, and is intended to treat all asstes, base game or mod, natively and equally" -> use Asset Pipeline language and make base/mod parity explicit [Task 1]
- when the user explicitly asks to rebuild project memory from a source file "verbatim", treat that file as the source of truth instead of merging older memories back in [Task 1]
- when the user said "Each asset should serve as a fully self contained archive" -> default acceptance should be archive self-containment, not just descriptor presence [Task 2]
- when the user said "Run until all asset types pass their appropriate file and dependency file test" -> keep iterating until every family passes a dependency-aware gate [Task 2]
- when the user asked "List the asset types and the General file contents in each assert archive type" -> final reporting should be inventory-style and contents-specific [Task 2]
- when the user’s archive-contract clarification showed that opening one typed archive should expose "the descriptor plus the authored dependency closure needed to edit/clone/share/load it" -> fail packaging/export rather than silently emitting a reference-only archive when an embedded dependency cannot be resolved [Task 3]
- when the user asked for "a breakdown of the pros and cons" and then "How would we do that in the context of keeping it clean but fully functional and self contained?" -> compare conceptual layout tradeoffs before implementation, but keep closure/portability as non-negotiable [Task 4]
- when the user said "Make a kanban card with that sort of layout breakdown for each asset type" -> stage broad archive-contract cleanup as a family-by-family execution card instead of burying it in notes [Task 4]
- when archive validation found bad outputs even though the summary said `failed=0`, the user’s workflow favors fixing the actual archive/fallback contract instead of trusting the headline status [Task 5]
- when the user asked, "Does the card reflect the ongoing process, specifically, defining the format for each other asset type before implementing the full sweep?" -> make the freeze-first / rebuild-later sequence explicit in the board and docs, not just implied [Task 6]

## Reusable knowledge

- All game content should be saved per asset as the relevant typed `*.pdxxx` file, and each `.pdhead`, `.pdbody`, `.pdarena`, `.pdmesh`, `.pdanim`, `.pdsfx`, `.pdvoice`, `.pdsong`, `.pdui`, `.pdfont`, `.pdlang`, `.pdscenario`, and similar typed file is a self-contained archive for that asset [Task 1][Task 2]
- Base-game assets and mod assets should be treated natively and equally by the asset pipeline; mods are not a second-class or separate content model [Task 1]
- Changing a typed asset archive extension to `.zip` should expose the descriptor plus the authored source files needed to edit or clone that asset; authored files should stay modern and readable rather than opaque blobs [Task 1]
- The engine should use those files natively at the game-facing boundary. If native use requires conversion in either direction, build the two-way conversion pipeline rather than shipping opaque authored blobs [Task 1]
- `.pdmod` is transport only for Public Mods, sharing, and online-required delivery. It is not the primary authoring surface [Task 1]
- The active repair card for the current typed-archive contract is `c3812`, and the priority chain treats asset-pipeline repair as the front of the ship-critical path [Task 1]
- The archive gate is stronger than "file exists": tests should validate both required files and intra-archive dependency closure for descriptor paths, GLTF `uri`, OBJ `mtllib`, and MTL texture refs [Task 2]
- The scanner’s source-path keys are the contract surface for internal references: `model_file`, `hand_model_file`, `geometry_file`, `pads_file`, `setup_file`, `rooms_file`, `props_file`, `objectives_file`, `animation_file`, `texture_file`, `font_file`, `strings_file`, `strings_tsv`, `file_path`, and related keys in `assetcatalog_scanner` [Task 2]
- The current `typed-pdxxx-basic` examples cover every current non-weapon family plus the older `tri_weapon.pdwpn` fixture that existed during `c3812`; newer weapon-specific archive direction now lives in the `PD2 Weapon Graph Asset Archives` block because the weapon lane moved to `.pdweapon` [Task 2]
- `modVfs` supports nested `archive::entry` lookups, so authored files inside typed archives can be resolved without extraction when packaging/importing dependency closure [Task 3]
- `fsDataPathFor(rel, ...)` is the canonical helper for `data/<romid>/...` save/extractor paths in this lane [Task 3]
- For the weapon lane, dependency closure is now embedded in `.pdweapon` archives with `dependency_closure = embedded.v2`; remaining `c3814` work after this slice is runtime parity/adapters, not packaging/export [Task 3]
- `.pdwpn` remains explicitly unsupported/deprecated; the active weapon authoring format is `.pdweapon` [Task 3]
- The clean archive direction is a two-zone contract: authored/root files stay human-facing, while `_meta/` quarantines manifest, inventory, provenance, validation, SHA rows, and compatibility bookkeeping [Task 4]
- Keep duplication for portability/cloneability inside authored archives, then dedupe after ingestion by catalog identity / SHA-256 instead of trying to make the archive sparse at authoring time [Task 4]
- New emitters should write `_meta/`, scanners/loaders may accept older metadata roots temporarily during transition, and release validation should reject stale outputs once emitters are updated [Task 4]
- Archive correctness in this lane is stronger than a green summary footer: `.pdanim` outputs must be zip-openable typed archives with `animation.ini`, `manifest.json`, and `opcodes.json`, while `.pdmesh` extraction must honor runtime-aligned GDL / vertex decoding including low-bit marker masking and segment-4 vertex resolution [Task 5]
- Local Combat Simulator can still hit SP fallback paths such as `MANIFEST-SP: late-add` unless the MP manifest is prepared before stage change, and random bot body selection must exclude SP-only rows such as `base:sp_body_108` [Task 5]
- The current layout migration now has an explicit phased order: `c3824` is the umbrella migration-plan card, `c3832` is the concrete clean `.pdweapon` format card beneath it, and the broad extraction/examples/validators sweep belongs after asset-family formats are frozen [Task 6]
- The documented clean `.pdweapon` target is `weapon.ini` plus purpose folders such as `models/`, `materials/`, `textures/`, `animations/`, `sounds/`, `behavior/`, `projectiles/`, `entities/`, `ui/`, with `_meta/` holding manifest, inventory, provenance, validation, and hash material [Task 6]

## Failures and how to do differently

- Do not let a secondary generated `memory_summary.md` preserve stale pre-rebuild topics after a source-of-truth memory replacement; rewrite and verify both memory artifacts [Task 1]
- If a background process appears to rewrite the active memory copy, rewrite the intended source again and verify by task-group count and key corrected rules before treating the rebuild as finished [Task 1]
- A minimal "descriptor + one source file" check was insufficient once the user clarified the fully self-contained requirement [Task 2]
- If archive fixtures mix older simple OBJ faces with newer UV-indexed faces, make the shared assertion accept both forms instead of forcing one legacy fixture shape everywhere [Task 2]
- A sandboxed wrapper run that cannot find `pd-tests.exe` is not trustworthy verification for this lane; rerun from the real checkout/build output before treating the archive gate as passing [Task 2]
- If an embedded dependency cannot be resolved during packaging/export, do not emit a reference-only archive and hope runtime finds the missing file later [Task 3]
- Release/install trees can lag behind repo build output; compare both explicitly instead of assuming the example zip, build output, and installed release tree match [Task 4]
- Do not trust `failed=0` alone as proof that produced archives and fallback paths are sound; inspect the actual output archives and runtime-loaded assets before closing the lane [Task 5]
- If a raw HUD model filenum or similar catalog miss appears during runtime validation, treat it as a real provider/catalog gap instead of hand-waving it as a transient quirk [Task 5]
- If the card wording says "clean self-contained archive layouts" but does not make the sequence obvious, rewrite the board order and subtasks until the freeze-first / implement-later process is visible at a glance [Task 6]

# Task Group: PD2 Weapon Graph Asset Archives
scope: Weapon-specific authored behavior contracts, graph authoring surfaces, and Modding Hub workflow for weapon behavior data.
applies_to: cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike; reuse_rule=reuse for PD2 weapon-archive authoring, extractor/output, and card-handoff work when `.pdweapon`, nested payloads, or weapon behavior modularization are in scope.

## Task 1: Clarify weapon behavior modularization as authored asset data instead of new gameplay design, success

### rollout_summary_files

- rollout_summaries/2026-05-21T00-18-27-pZ9s-pdwpn_weapon_behavior_modularization.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\sessions\2026\05\20\rollout-2026-05-20T20-18-27-019e47e5-ddbf-72a1-8cf5-a2fe1c8d8276.jsonl, updated_at=2026-05-21T00:22:30+00:00, thread_id=019e47e5-ddbf-72a1-8cf5-a2fe1c8d8276, earlier clarification before the `.pdweapon` cutover)

### keywords

- weapon behavior, modularize existing behavior, authored asset format, c3812, trigger pulled, hold fire, charge release, zoom, reticle, ammo display

## Task 2: Add structured graph authoring to the Modding Hub Weapons tab instead of JSON-only editing, success

### rollout_summary_files

- rollout_summaries/2026-05-22T03-30-57-mpVE-weapon_graph_builder_ui.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\sessions\2026\05\21\rollout-2026-05-21T23-30-57-019e4dbc-784c-7d51-bcd3-c1dc37ce129a.jsonl, updated_at=2026-05-22T03:45:46+00:00, thread_id=019e4dbc-784c-7d51-bcd3-c1dc37ce129a, graph-builder UI landed as `c3814-s23`)

### keywords

- behavior.graph.json, Modding Hub, Weapons tab, pdgui_menu_moddinghub, c3814-s23, graph builder, presets, modules, exports, validation

## User preferences

- when the user said "All the behavior exists in the game already, we just need to modularize it and use it from our own asset files" -> treat weapon-behavior requests as modularization/export of existing behavior, not a request to invent new mechanics [Task 1]
- when behavior examples include "when trigger pulled", rapid fire, hold fire, charge/release, secondary modes, melee, zoom levels, reticle / overlay / zoom camera effects, and ammo-display behavior -> preserve those examples as required coverage when updating the weapon authored-data contract [Task 1]
- when the user said "The weapon mod menu should allow us to create weapon behavior graphs, not just look at their json" -> default the Weapons tab to a structured authoring workflow, with raw JSON kept as an advanced/fallback path rather than the main experience [Task 2]

## Reusable knowledge

- The earlier weapon-behavior clarification was about authored asset modularization, not a separate gameplay system: the target behavior layer should represent trigger-pulled cadence in centiseconds, custom projectile selection, looping/rapid fire, hold-fire beams, charge-and-release, secondary modes, melee, zoom, reticles, overlays, zoom-camera effects, and ammo-driven presentation [Task 1]
- `port/fast3d/pdgui_menu_moddinghub.cpp` is the right place for weapon-editor workflow changes; the Weapons tab already had preview, clone, import, and save plumbing, so graph-builder controls can be layered there instead of inventing a new screen [Task 2]
- The graph-builder UI now covers seed presets, named module insertion, editable node params, edge add/remove, primary/secondary export selection, validation, and a generated JSON view for `behavior.graph.json` [Task 2]
- The active `c3814` lane now records `c3814-s23` as done for the graph-builder UI, while deeper runtime execution remains in later subtasks [Task 2]

## Failures and how to do differently

- Avoid interpreting behavior requests as permission to invent a parallel gameplay system; the user explicitly corrected that framing [Task 1]
- Keep weapon-contract updates additive and scoped to the active archive lane instead of broad unrelated refactors when the repo already has many concurrent changes [Task 1]
- If new static coverage fails on the wrong generated-string shape, align the assertion with the literal source form instead of the rendered JSON you expected [Task 2]

# Task Group: PD2 Startup Asset Extraction and Boot Progress UI
scope: Startup extraction latency, typed-asset cache behavior, and boot-overlay progress presentation during initial content work.
applies_to: cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike; reuse_rule=reuse for PD2 boot/startup work in this checkout, but keep `.pdanim` caching claims conservative because its shared extension/directory shape breaks the per-family cache rule used elsewhere.

## Task 1: Add per-family typed-asset cache stamps so later boots skip unchanged extraction work, success

### rollout_summary_files

- rollout_summaries/2026-05-21T17-40-11-zo1h-boot_asset_fast_cache_progress_ui.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\sessions\2026\05\21\rollout-2026-05-21T13-40-11-019e4b9f-9b83-7ab1-9c2e-8bd421f882f7.jsonl, updated_at=2026-05-21T18:45:30+00:00, thread_id=019e4b9f-9b83-7ab1-9c2e-8bd421f882f7, fast-cache stamps for typed families and existing-install boot smoke)

### keywords

- bootfast, pdextract-cache, typed asset families, LOADER.UNIVERSAL.SUMMARY, boot_smoke, romextract_pd_cache, .pdanim

## Task 2: Rework the boot progress surface into a centered modal with smoother motion, success

### rollout_summary_files

- rollout_summaries/2026-05-21T17-40-11-zo1h-boot_asset_fast_cache_progress_ui.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\sessions\2026\05\21\rollout-2026-05-21T13-40-11-019e4b9f-9b83-7ab1-9c2e-8bd421f882f7.jsonl, updated_at=2026-05-21T18:45:30+00:00, thread_id=019e4b9f-9b83-7ab1-9c2e-8bd421f882f7, centered modal boot overlay and timing-based progress smoothing)

### keywords

- boot overlay, centered modal, progress smoothing, pdgui_bootoverlay, boot_progress, startup acceleration, c3823

## User preferences

- when the user asked to "speed up our startup" -> optimize startup wall time, not just one emitter in isolation [Task 1]
- when the user asked to show progress while catalog work is happening -> make startup work visibly progress during the expensive phase, not only after the fact [Task 1][Task 2]
- when the user asked for the load bar to be more accurate and centered -> prefer centered readability and smoother motion over a bottom-edge progress bar [Task 2]

## Reusable knowledge

- `.pdextract-cache` stamps typed-asset output families with schema, kind, file count, total bytes, and latest mtime so unchanged families can be skipped on later boots [Task 1]
- Cached boot logs showed skips for `.pdweapon`, `.pdmesh`, `.pdhead`, `.pdbody`, `.pdcharacter`, `.pdarena/.pdscenario`, `.pdsfx`, `.pdvoice`, `.pdsong`, `.pdfont`, and `.pdlang` after the cache landed [Task 1]
- `.pdanim` stays on its older per-archive validation path because weapon/inventory and character animation archives share the same extension/directory layout [Task 1]
- The boot overlay changed from a bottom bar to a centered modal-style panel with the status label above the bar and progress that blends item completion with conservative phase timing estimates [Task 2]
- Existing-install `boot_smoke` showed `LOADER.UNIVERSAL.SUMMARY` at 1.85s and `BOOT_OVERLAY: dismissed (visible for 1.51s)` after the fast-cache/progress work [Task 1][Task 2]

## Failures and how to do differently

- Do not assume every typed family can share one cache heuristic; `.pdanim` is the exception because its ownership is ambiguous across multiple archive lanes [Task 1]
- A config-entry-cap warning in the smoke log was not the real boot-cost driver here; avoid pivoting off a nearby warning until the timing data supports it [Task 1]
- If the repo is already dirty, keep boot/UI edits scoped to startup and tracking files instead of broadening the change set [Task 2]

# Task Group: PD2 Weapon Export and Beam Coordinate Diagnostics
scope: Weapon-model extraction, held-weapon transform handling, and live beam/effect coordinate-space diagnosis for visible weapon artifacts.
applies_to: cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike; reuse_rule=reuse for PD2 weapon OBJ export, held-weapon rendering, and laser/effect anchoring work when the symptom is a narrow visible weapon artifact rather than a broad model-system failure.

## Task 1: Diagnose Falcon 2 barrel stretch as split exporter and runtime coordinate-space bugs, success

### rollout_summary_files

- rollout_summaries/2026-05-22T01-37-19-RsXc-falcon_2_stretch_exporter_and_beam_space_fix.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\sessions\2026\05\21\rollout-2026-05-21T21-37-19-019e4d54-6de5-7542-9097-a9cb16c729ae.jsonl, updated_at=2026-05-22T05:42:02+00:00, thread_id=019e4d54-6de5-7542-9097-a9cb16c729ae, split Falcon 2 artifact into exporter-side matrix handling and runtime beam-space handling)

### keywords

- Falcon 2, pdmesh, pdweapon, OBJ export, G_MTX, G_POPMTX, SPSEGMENT_MODEL_MTX, bgunUpdateLasersight, beamfar, crosspos, rigged models, matrix stack

## Task 2: Track and verify the Falcon 2 export/runtime fix set, success

### rollout_summary_files

- rollout_summaries/2026-05-22T01-37-19-RsXc-falcon_2_stretch_exporter_and_beam_space_fix.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\sessions\2026\05\21\rollout-2026-05-21T21-37-19-019e4d54-6de5-7542-9097-a9cb16c729ae.jsonl, updated_at=2026-05-22T05:42:02+00:00, thread_id=019e4d54-6de5-7542-9097-a9cb16c729ae, verification and B-366 tracking updates for the split fix)

### keywords

- B-366, model_obj_mtx_v8, export_version.txt, boot_smoke, bondgun laser static, detached-group, context/pillars/modding.md, context/pillars/rendering.md, UNRELEASED.md

## User preferences

- when the user asks whether a visible weapon artifact is in "how we Re parsing model data" or "something to do with rigged models" -> test the parser/export path against the runtime effect/state path instead of answering with one broad cause [Task 1]
- when the user says the model is "mostly fine" but one visible piece stays fixed or stretches -> frame the diagnosis as a narrow coordinate-space or transform problem, not a blanket "model system is broken" claim [Task 1]
- when the user reports one concrete artifact, keep the work scoped to that artifact and avoid expanding into unrelated model-system cleanup [Task 2]

## Reusable knowledge

- The Falcon 2 OBJ issue was real in the export pipeline: weapon display lists use `G_MTX` / `G_POPMTX` against `SPSEGMENT_MODEL_MTX`, and the older `.pdmesh` OBJ path emitted vertices without applying those transforms [Task 1]
- The exporter-side fix now applies the held-weapon model-matrix stack and writes version markers (`model_obj_mtx_v8`, `export_version.txt`) so stale exports regenerate instead of silently persisting the old geometry [Task 1]
- The in-game Falcon 2 "tip stays fixed while the model moves" symptom was separate from the OBJ issue: the live laser-sight beam path had its far endpoint projected from the wrong space while the near endpoint followed the muzzle/root [Task 1]
- The durable diagnostic split is exporter-side matrix handling for held weapon meshes vs runtime beam/effect-space handling in `bgunUpdateLasersight()`; both can coexist for the same visible weapon symptom [Task 1][Task 2]
- Verification for this lane was intentionally split across extraction and runtime: focused mesh extractor, focused `.pdweapon`, focused `[bondgun][laser][static]`, isolated all-target builds, and `boot_smoke` regeneration of 303 `.pdmesh` plus 86 `.pdweapon` archives [Task 2]
- The all-weapon sweep reported 86 weapon archives, 152 nested weapon meshes, 303 standalone meshes, and no remaining severe-Y detached-group issues after the export fix [Task 2]

## Failures and how to do differently

- Do not collapse "OBJ artifact" and "in-game artifact" into one generic rigged-model parsing theory when the symptom can come from both exporter transforms and runtime beam/effect anchoring [Task 1][Task 2]
- When the symptom is "models mostly fine" but one endpoint or tip stays fixed while the model moves, inspect effect anchoring and coordinate spaces before blaming skeletal parsing [Task 1]
- Keep the exporter bug vs live effect bug distinction explicit in notes and tracking; collapsing them into one cause makes later verification ambiguous [Task 2]

# Task Group: PD2 F6 Debug Completion Semantics
scope: F6 debug hotkey behavior in Campaign and Combat Simulator, plus adjacent credits-routing debug entry points.
applies_to: cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike; reuse_rule=reuse for campaign/debug-hotkey/credits-routing work in this checkout, but keep Combat Simulator-only behavior separated from Campaign behavior.

## Task 1: Repurpose Campaign F6 into real mission completion while keeping Combat Simulator freeze behavior separate, success

### rollout_summary_files

- rollout_summaries/2026-05-19T03-40-15-eJeE-pd2_f6_campaign_complete_combat_sim_freeze_kanban.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\sessions\2026\05\19\rollout-2026-05-19T00-40-15-019e3e51-e573-7e01-9569-dc893c197510.jsonl, updated_at=2026-05-19T04:25:25+00:00, thread_id=019e3e51-e573-7e01-9569-dc893c197510)

### keywords

- F6, campaign complete, Combat Simulator freeze, mainEndStage, dev-build-only, debug hotkey

## Task 2: Fix final-campaign Credits routing so F6 stops on Credits instead of advancing into Challenges, success

### rollout_summary_files

- rollout_summaries/2026-05-19T18-33-07-wA5I-pd2_skedar_campaign_credits_and_b345_followup.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\sessions\2026\05\19\rollout-2026-05-19T14-33-07-019e4183-5914-7902-a0a5-ee804ed93a82.jsonl, updated_at=2026-05-19T20:21:13+00:00, thread_id=019e4183-5914-7902-a0a5-ee804ed93a82)

### keywords

- Credits, Skedar Ruins, endscreenContinue(2), f6credits, debug campaign complete hotkey

## Task 3: Add an offline-only Settings > Debug "Go to Credits" shortcut, success

### rollout_summary_files

- rollout_summaries/2026-05-19T04-25-30-bZCx-settings_debug_go_to_credits_shortcut.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\sessions\2026\05\19\rollout-2026-05-19T00-25-30-019e3e7b-547a-7100-bfd8-010a44de32d1.jsonl, updated_at=2026-05-19T04:35:43+00:00, thread_id=019e3e7b-547a-7100-bfd8-010a44de32d1)

### keywords

- Go to Credits, Settings Debug, GRID_STAGE_CREDITS, NETMODE_NONE, test_debug_credits_button

## User preferences

- when fixing final-campaign behavior, the user’s card note was that F6 should not go past Campaign into Challenges when Credits should play -> explicitly preserve the final Credits handoff instead of only marking objectives complete [Task 2]
- when the user asks for a small debug-menu entry such as "As an option to the debug tab in Settings to go to Credits", keep it localized instead of broadening into a larger menu rewrite [Task 3]

## Reusable knowledge

- F6 in Campaign and F6 in Combat Simulator are separate behaviors; Campaign F6 should behave like real mission completion, not like a loose objective/HUD shortcut, while Combat Simulator-only debug fallbacks remain separately gated [Task 1]
- Campaign completion must set the mission-completion flags and state, including difficulty-active objectives, death/abort state, end-stage flow, and the final-campaign Credits routing [Task 1][Task 2]
- The final Skedar Ruins branch should show `Credits` and route through `endscreenContinue(2)` before the normal next-mission path [Task 2]
- `renderSettingsDebug` in `port/fast3d/pdgui_menu_mainmenu.cpp` is the right insertion point for a small offline-only debug scene shortcut, and `g_NetMode == NETMODE_NONE` is the correct guard for it [Task 3]

## Failures and how to do differently

- Do not leave the final campaign path ambiguous between Credits and the normal next-mission/challenge flow; check the end-stage branch explicitly [Task 2]
- If Mike reserves runtime validation for himself, record the work as build/static verified with Mike-pending runtime playtest instead of overstating closure [Task 1]
- If the repo hook rejects a follow-up context commit, do not bypass it; record the remaining local context edits honestly [Task 3]

# Task Group: PD2 Press/Hold Input Semantics
scope: Press-vs-hold behavior across gameplay interaction, reload, hold-ring UI, and consumed-hold handling.
applies_to: cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike; reuse_rule=reuse for future action-map, interaction, firing, vehicle, weapon, and prompt/UI hold-progress work in PD2.

## Task 1: Correct ACTION_USE tap/hold semantics so tap reloads, hold interacts, and consumed hold-ring progress decays, success

### rollout_summary_files

- rollout_summaries/2026-05-18T20-35-32-kbzd-action_use_hold_only_interact_and_hold_ring_consumption.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\sessions\2026\05\18\rollout-2026-05-18T16-35-32-019e3ccd-0eee-7522-8457-566820dfc363.jsonl, updated_at=2026-05-18T20:48:19+00:00, thread_id=019e3ccd-0eee-7522-8457-566820dfc363)

### keywords

- ACTION_USE, tap reload only, hold interact only, hold_consumed, actionHoldProgress, press-hold, b340

## Task 2: Make cutscenes own input, require hold-to-skip, and suppress gameplay prompts, success

### rollout_summary_files

- rollout_summaries/2026-05-21T18-03-22-3G8c-main_menu_ci_camera_and_cutscene_hold_skip.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\sessions\2026\05\21\rollout-2026-05-21T14-03-22-019e4bb4-d4ea-7002-bc6b-965a7a79923c.jsonl, updated_at=2026-05-21T18:43:37+00:00, thread_id=019e4bb4-d4ea-7002-bc6b-965a7a79923c, cutscene hold-to-skip prompt and interact suppression)

### keywords

- cutscene, ACTION_SKIP_CUTSCENE_HOLD_THRESHOLD_MS, pdguiCutsceneSkipPromptRender, g_ImcCutscene, pdguiCiIntroBlocksInteractPrompt, actionConsumeHold

## Task 3: Fix PC Campaign weapon switching and stale weapon-function HUD text, success

### rollout_summary_files

- rollout_summaries/2026-05-21T19-59-06-o4xN-c3814_mkb_weapon_switch_function_hud_fix.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\sessions\2026\05\21\rollout-2026-05-21T15-59-06-019e4c1e-c747-7ce2-849d-b0acea9e4c1f.jsonl, updated_at=2026-05-21T20:49:32+00:00, thread_id=019e4c1e-c747-7ce2-849d-b0acea9e4c1f, `ACTION_WEAPON_NEXT` and HUD-label regression fix in the `c3814` lane)

### keywords

- ACTION_WEAPON_NEXT, VKL_Q, mouse wheel, up_time_ms, actionWasTap, bondmove.c, bondgun.c, weapon function HUD, c3814-s15, B-363

## User preferences

- when the user says "I can still interact by just tapping the input, instead of the required hold" -> treat tap-vs-hold as a strict gameplay contract, not a UI-only distinction [Task 1]
- when the user says the radial "should max out and then be consumed as though it were released" -> after a consumed hold, make the ring behave like release instead of staying full until physical button-up [Task 1]
- when cutscene/menu-transition behavior should wait for the scene to finish, prefer structural gating and held-input ownership over transient tap behavior during the transition [Task 2]
- when an input/display regression affects both weapon switching and the weapon-function HUD, treat it as a gameplay contract issue rather than UI polish and verify both the action path and the displayed state [Task 3]

## Reusable knowledge

- Press and hold are the same physical input until release timing or the held threshold decides the semantic result; releasing before threshold is a press, crossing threshold is a held input [Task 1]
- `actionWasTap`, `actionHeldForMs`, `actionConsumeHold`, and `actionHoldProgress` are the canonical press/hold primitives in this checkout [Task 1][Task 2][Task 3]
- In this lane, the real tap/hold contract crosses `src/game/bondmove.c`, `port/src/actionmap.cpp`, and the interact prompt renderer; fixing only the visible radial is incomplete [Task 1]
- `actionHoldProgress()` should return `0` for consumed holds after the brief full-ring pin, even if the button is still physically down [Task 1]
- `ACTION_SKIP_CUTSCENE_HOLD_THRESHOLD_MS` is the cutscene skip threshold constant, and `pdguiCutsceneSkipPromptRender()` is the dedicated hold-to-skip overlay path that uses the existing hold-ring rendering primitives [Task 2]
- `pdguiCiIntroBlocksInteractPrompt()` is the suppression hook for preventing gameplay interact prompts from bleeding through active cutscenes, and the glyph resolver must check `g_ImcCutscene` so the cutscene prompt shows the right binding first [Task 2]
- `ACTION_WEAPON_NEXT` is now the canonical PC Campaign next-weapon tap/hold surface in this checkout: tap cycles, hold opens the radial wheel, and `Q` plus mouse wheel feed the same action path [Task 3]
- Mouse-wheel weapon cycling depends on wheel-release timestamps refreshing `up_time_ms`; otherwise `actionWasTap()` can reject the gesture as stale [Task 3]
- The weapon-function HUD text cache must reset when weapon or function identity changes, or stale labels can leak across weapon transitions [Task 3]

## Failures and how to do differently

- Do not preserve stale alternate tap/double-tap interaction ideas once the current lane has been corrected to reload-only taps and hold-only interact [Task 1]
- If a patch misses because the file context moved, re-apply against the exact live block instead of forcing a brittle edit [Task 1]
- If a cutscene hold selector test becomes too literal after the implementation shifts to a reusable skip-action array, align the assertion with the actual source shape instead of the older exact string [Task 2]
- If an all-target wrapper prints a truncated JSON warning but the build itself completes, judge the build lane by the wrapper's final pass/fail state plus the focused selector evidence [Task 2]
- Do not stop at the input binding when the visible regression includes stale weapon-function text; display caches can hide gameplay-state fixes unless both surfaces are checked [Task 3]

# Task Group: PD2 Dev Window v2 CLI Launchers
scope: Dev Window v2 launcher behavior for Codex CLI, elevated Windows shells, and project-root landing behavior.
applies_to: cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike; reuse_rule=reuse for `devtools/dev-window-v2` launcher work in this checkout, especially when adding or correcting admin CLI entry points on Windows.

## Task 1: Add a direct Codex CLI admin launcher to the Dev Window v2 CLI tab, success

### rollout_summary_files

- rollout_summaries/2026-05-21T05-16-47-Vi9J-dev_window_codex_admin_launch_and_powershell_input_hardening.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\sessions\2026\05\21\rollout-2026-05-21T01-16-47-019e48f6-fff3-7863-a2c3-f42026af848b.jsonl, updated_at=2026-05-21T15:05:19+00:00, thread_id=019e48f6-fff3-7863-a2c3-f42026af848b)

### keywords

- Dev Window v2, Codex CLI, BtnCliLaunchCodex, Get-CliCodexExe, Invoke-CliLaunchCodexAdmin, WPF, XAML

## Task 2: Force the elevated Codex console to land in the repo root instead of `System32`, success

### rollout_summary_files

- rollout_summaries/2026-05-21T05-16-47-Vi9J-dev_window_codex_admin_launch_and_powershell_input_hardening.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\sessions\2026\05\21\rollout-2026-05-21T01-16-47-019e48f6-fff3-7863-a2c3-f42026af848b.jsonl, updated_at=2026-05-21T15:05:19+00:00, thread_id=019e48f6-fff3-7863-a2c3-f42026af848b, post-UAC landing correction)

### keywords

- System32, WorkingDirectory, cd /d, UAC, cmd.exe, project root, elevated console

## Task 3: Investigate broken PowerShell input symptoms and harden the Codex launcher without touching Windows-wide state, partial

### rollout_summary_files

- rollout_summaries/2026-05-21T05-16-47-Vi9J-dev_window_codex_admin_launch_and_powershell_input_hardening.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\sessions\2026\05\21\rollout-2026-05-21T01-16-47-019e48f6-fff3-7863-a2c3-f42026af848b.jsonl, updated_at=2026-05-21T15:05:19+00:00, thread_id=019e48f6-fff3-7863-a2c3-f42026af848b, profileless PowerShell host hardening after Backspace/Enter complaints)

### keywords

- PowerShell, PSReadLine, pwsh.exe, NoProfile, TERM, COLORTERM, EncodedCommand, ConsoleHost_history.txt, wt.exe, Backspace, Enter

## User preferences

- when the user says "I dont need prompting assistance, just a button to launch the CLI (as admin)" -> prefer a direct launcher rather than routing the request back through prompt-composition UI [Task 1]
- when the user says the elevated shell should open "at our project directory, not in System32" -> verify the visible post-UAC landing directory, not just the intended working-directory property [Task 2]
- when the user says they are not using Claude CLI for now -> keep the Claude panel intact, but do not make it the only CLI path when Codex is the requested tool [Task 1]
- when the user says "my powershell seems somewhat broken" and "Don't break Windows" -> investigate and harden the launcher first, but avoid registry edits, cache purges, or other Windows-wide changes unless strongly justified [Task 3]

## Reusable knowledge

- `devtools/dev-window-v2/dev-window-v2.ps1` already had a Claude-oriented CLI tab; the Codex launcher fit as a separate button in the existing launch row rather than a replacement workflow [Task 1]
- `Get-CliCodexExe` resolves Codex from common Windows install paths or `PATH`, and the launcher entry points added in this lane were `BtnCliLaunchCodex` and `Invoke-CliLaunchCodexAdmin` [Task 1]
- Under UAC elevation, `cmd.exe` can still open in `System32` even when `ProcessStartInfo.WorkingDirectory` is set; the reliable fix here was to launch `cd /d "C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike" && "codex.exe"` [Task 2]
- `wt.exe` was not installed/discoverable in this environment, so the launcher could not offload cleanly into Windows Terminal [Task 3]
- No PowerShell profile script was loaded in the investigated session, and PSReadLine key bindings for Enter / Backspace were normal, so the launcher hardening path focused on host isolation rather than profile surgery [Task 3]
- The hardened launcher now prefers `pwsh.exe`, uses `-NoProfile`, sets `TERM=xterm-256color` and `COLORTERM=truecolor`, routes PSReadLine history to a temp per-process path, and uses `-EncodedCommand` to avoid quoting/path fragility [Task 3]
- Focused verification for this surface was PowerShell AST parse plus XAML load, with the button name present and `git diff --check` clean after the final launcher command change [Task 1][Task 2][Task 3]

## Failures and how to do differently

- Do not assume an existing Claude prompt box should be reused when the user asked for a plain CLI launcher [Task 1]
- Keep elevated-launch code separate from prompt-generation code paths so the launcher remains reusable and easy to reason about [Task 1]
- `WorkingDirectory` alone was not enough for the elevated console; when the landing directory matters, enforce it inside the launched command with `cd /d` [Task 2]
- If the first hardening pass relies on a quoted `-Command` string, replace it with encoded-command startup before treating the launcher as robust [Task 3]
- When the user's symptom may come from a history/cache path but they did not ask for system repair, isolate the launched console from that state instead of modifying the user cache directly [Task 3]

# Task Group: PD2 Dev Window v2 Project State Sync and Release Closeout
scope: Git-tracked live project state, selectable pull behavior, and concise release-note generation in the Dev Window v2 / release tooling lane.
applies_to: cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike; reuse_rule=reuse for Dev Window v2 build/release/push/pull workflows and release-note upkeep in this checkout.

## Task 1: Mirror live project state into tracked files and commit it with code, success

### rollout_summary_files

- rollout_summaries/2026-05-21T14-50-10-i1ce-pd2_devwindow_live_state_git_sync_release_notes.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\sessions\2026\05\21\rollout-2026-05-21T10-50-10-019e4b03-f40c-7fd2-b463-0d97408cc3fa.jsonl, updated_at=2026-05-21T15:06:01+00:00, thread_id=019e4b03-f40c-7fd2-b463-0d97408cc3fa, Git-tracked Kanban/memory state sync before build/release/push)

### keywords

- project-state-sync, tools/kanban/memories.md, c3818, Sync-ProjectMindState, Get-ProjectStateCommitBody, live state, GitHub

## Task 2: Make Pull choose a remote commit instead of blindly advancing, success

### rollout_summary_files

- rollout_summaries/2026-05-21T14-50-10-i1ce-pd2_devwindow_live_state_git_sync_release_notes.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\sessions\2026\05\21\rollout-2026-05-21T10-50-10-019e4b03-f40c-7fd2-b463-0d97408cc3fa.jsonl, updated_at=2026-05-21T15:06:01+00:00, thread_id=019e4b03-f40c-7fd2-b463-0d97408cc3fa, fetch-first selectable pull flow)

### keywords

- selectable pull, git fetch, Show-GitPullCommitDialog, Start-GitPullMerge, fast-forward, remote commits

## Task 3: Replace stale release text with a running simplified change list, success

### rollout_summary_files

- rollout_summaries/2026-05-21T14-50-10-i1ce-pd2_devwindow_live_state_git_sync_release_notes.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\sessions\2026\05\21\rollout-2026-05-21T10-50-10-019e4b03-f40c-7fd2-b463-0d97408cc3fa.jsonl, updated_at=2026-05-21T15:06:01+00:00, thread_id=019e4b03-f40c-7fd2-b463-0d97408cc3fa, simplified `UNRELEASED.md` closeout flow)

### keywords

- UNRELEASED.md, simplified release notes, Highlights, Added, Changed, Fixed, stale notes

## User preferences

- when the user said they wanted to "also track our Kanban board and memories.md using GitHub" and "commit those as well" -> treat live board/memory state as part of the shippable project state, not a side note [Task 1]
- when the user described `memories.md` as "the active mind / perception / live state of the project at any given moment" -> mirror that state before build/release/push so code and project state stay synchronized [Task 1]
- when the user said "when I select Pull, it should let me choose a commit to pull" -> expose remote commit choice instead of defaulting to a blind pull [Task 2]
- when the user asked for a running list of "simplified" change descriptions and complained about the "same outdated commit message / description" -> keep release notes concise, current, and explicitly maintained during closeout [Task 3]

## Reusable knowledge

- `devtools/project-state-sync.ps1` is the reusable sync helper for this lane, with `Sync-ProjectMindState`, `Get-ProjectStateCommitBody`, and `Test-ReleaseNotesLookStale` coordinating live-state mirroring and release-note checks [Task 1][Task 3]
- The tracked mirror for Codex memory in this repo is `tools/kanban/memories.md`, which is fed from `C:\Users\mikeh\.codex\memories\MEMORY.md` before build/release/push flows [Task 1]
- In this workflow, release/build/push sync should mirror state first, then stage/commit, then derive commit/release text from the same staged live-state files [Task 1][Task 3]
- The safer pull UX here is fetch-first, choose from remote commits not yet local, then fast-forward to the selected commit or report that fast-forward is not possible [Task 2]
- `UNRELEASED.md` is the current release-notes source of truth, and a short `Highlights / Added / Changed / Fixed` running list is the accepted shape for user-facing release text [Task 3]

## Failures and how to do differently

- If a new tooling card ID collides with an existing live card, validate against the current `tools/kanban/state.json` list and move the new work to a fresh ID instead of renumbering the established card underneath it [Task 1]
- In a dirty tree, broad `git diff --check` or similar whole-tree validation can time out; scoped checks on the touched files are the reliable validation path for this lane [Task 1]
- The selectable-pull flow is intentionally fast-forward only; do not present it as a general divergent-history resolver [Task 2]
- If `UNRELEASED.md` still looks like an old baseline, rewrite it before release rather than letting stale placeholder text keep propagating into releases [Task 3]

# Task Group: PD2 Skedar Benchmark Parity and Surface Locomotion
scope: Skedar swarm benchmark behavior, CPU/GPU parity, and benchmark-local wall/jump surface locomotion.
applies_to: cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike; reuse_rule=reuse for Skedar benchmark and swarm parity work, but keep benchmark-specific fixes separate from normal Combat Simulator AI unless scope is widened explicitly.

## Task 1: Investigate why GPU Skedar benchmark bots diverged from CPU bots and why wall/jump behavior was missing, success

### rollout_summary_files

- rollout_summaries/2026-05-18T16-48-33-DSex-skedar_gpu_benchmark_parity_wall_jump_investigation.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\sessions\2026\05\18\rollout-2026-05-18T12-48-33-019e3bfd-4177-7621-bd15-d5e158a3e865.jsonl, updated_at=2026-05-18T16:53:57+00:00, thread_id=019e3bfd-4177-7621-bd15-d5e158a3e865)

### keywords

- Skedar, GPU_POS_ONLY, GPU_FULL, swarm_test.c, swarm_gpu.cpp, chrTrySkJump, surface_loco, investigate first and plan

## Task 2: Implement and verify benchmark-local CPU/GPU parity plus wall/jump support, success

### rollout_summary_files

- rollout_summaries/2026-05-18T16-16-37-8nu7-skedar_swarm_benchmark_parity_verified.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\sessions\2026\05\18\rollout-2026-05-18T12-16-37-019e3be0-05bb-7ee0-928a-f0ff9ce3f387.jsonl, updated_at=2026-05-18T17:39:24+00:00, thread_id=019e3be0-05bb-7ee0-928a-f0ff9ce3f387)

### keywords

- SWARM_METHOD_GPU_FULL, SWARM_METHOD_GPU_POS_ONLY, swarmTestApplyMovementIntent, chrSurfaceLocoApplyContactPos, chrStartSkJump, base:mp_skedar

## User preferences

- when the user said GPU benchmark bots should "act the same as the CPU benchmark bots" and wall walking/jumping should work in the benchmark, optimize for benchmark-scoped parity rather than a generic GPU approximation [Task 1][Task 2]
- when the user said "Investigate first and plan." -> split diagnosis from implementation for benchmark regressions before editing code [Task 1]
- when parity work is requested in the benchmark lane, keep the helper benchmark-local and do not silently widen normal Combat Sim bot AI [Task 1][Task 2]

## Reusable knowledge

- GPU swarm launches through `testScenarioLaunch()` and `matchStart()` for MP arenas; direct Grid/Forge handoff is only safe for Empty Map [Task 1]
- `testScenarioActiveMethod()` defaulted GPU swarm to `SWARM_METHOD_GPU_POS_ONLY`; the verified parity slice changed the default to `SWARM_METHOD_GPU_FULL` while keeping `GPU_POS_ONLY` as the diagnostic toggle [Task 1][Task 2]
- CPU swarm bots use `aibot` AI; GPU swarm bots in this lane do not, so CPU-owned side effects need an explicit benchmark bridge if parity is required [Task 1]
- The durable fix boundary is shared benchmark-local movement intent through `swarmTestApplyMovementIntent(...)`, with surface contact correction handled by `chrSurfaceLocoApplyContactPos(...)` instead of world-ground `chrSetPos` [Task 2]
- `chrTrySkJump` / `chrStartSkJump` are the Skedar wall-jump path, and `chrStartSkJump(...)` must null-check the target prop before measuring distance [Task 1][Task 2]

## Failures and how to do differently

- Do not assume GPU benchmark parity exists just because Skedars render and move; CPU-owned behavior must be bridged deliberately [Task 1]
- Do not assume wall-pin / surface-loco behavior is shared automatically across CPU and GPU swarm paths [Task 1]
- If wall-contact telemetry is sparse, extend the smoke duration or cycle density instead of concluding the locomotion helper is unused [Task 2]
- If the first isolated build hits the default watchdog, rerun with a longer build timeout rather than treating it as a source bug [Task 2]

# Task Group: PD2 Jump Collision and Bot Jump Verification
scope: Player and bot jump/collision behavior around rendered surfaces, overhead blockers, and capsule-sweep ownership.
applies_to: cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike; reuse_rule=reuse for PD2 physics-collision and jump fixes in this checkout, but keep bot-jump claims conservative unless directly verified.

## Task 1: Replace single-center Stage 2 with a rendered-surface capsule sweep and leave bot parity marked unverified pending playtest, success

### rollout_summary_files

- rollout_summaries/2026-05-18T16-57-48-kUb5-c038_jump_surface_collision_fix.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\sessions\2026\05\18\rollout-2026-05-18T12-57-48-019e3c05-ba16-71d1-b857-28e109fc2f9f.jsonl, updated_at=2026-05-18T18:42:27+00:00, thread_id=019e3c05-ba16-71d1-b857-28e109fc2f9f)

### keywords

- c038, Stage 2, multi-sample rendered capsule sweep, selfprop-aware, bot vertical paths, Mike playtest

## Task 2: Fix airborne side-entry through overhead blockers without breaking dynamic-object collision, success

### rollout_summary_files

- rollout_summaries/2026-05-19T17-44-51-b8St-pd2_b350_side_entry_fix_kanban_update.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\sessions\2026\05\19\rollout-2026-05-19T13-44-51-019e4157-293d-7201-a9d7-2eab0109fe09.jsonl, updated_at=2026-05-19T18:19:31+00:00, thread_id=019e4157-293d-7201-a9d7-2eab0109fe09)

### keywords

- B-350, bwalkClampAirborneSideEntry, capsuleSweep, overhead blockers, doorway sticking, moved couch, side-wall rendered-triangle

## Task 3: Move mission-start movement collision off render-owned prop/model state and onto collision-owned mesh data, success

### rollout_summary_files

- rollout_summaries/2026-05-18T19-44-21-PWIt-defection_perfect_crash_mesh_collision_ownership.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\sessions\2026\05\18\rollout-2026-05-18T15-44-21-019e3c9e-3502-79b3-a33f-1ac0eb03a9b5.jsonl, updated_at=2026-05-18T20:59:16+00:00, thread_id=019e3c9e-3502-79b3-a33f-1ac0eb03a9b5)
- rollout_summaries/2026-05-18T21-40-25-tPaL-pd2_meshcollision_extraction_hardening_loadscreen_crash.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\sessions\2026\05\18\rollout-2026-05-18T17-40-25-019e3d08-7840-75e1-a562-6bfd37078a5f.jsonl, updated_at=2026-05-19T01:36:35+00:00, thread_id=019e3d08-7840-75e1-a562-6bfd37078a5f, follow-on hardening of the meshcollision extraction boundary)

### keywords

- B-339, meshcollision, capsule Stage 2, rendered-prop floor acquisition, model_rodata_guard, ACCESS_VIOLATION, Defection Perfect

## Task 4: Clamp the remaining airborne wall/ceiling/corner entry path after B-350, success

### rollout_summary_files

- rollout_summaries/2026-05-22T03-28-30-jCbF-pd2_jump_wall_ceiling_corner_clamp_followup.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\sessions\2026\05\21\rollout-2026-05-21T23-28-30-019e4dba-38e0-7d63-9d0f-bd646671bd13.jsonl, updated_at=2026-05-22T03:45:26+00:00, thread_id=019e4dba-38e0-7d63-9d0f-bd646671bd13, B-367 airborne lateral clamp follow-up)

### keywords

- B-367, bwalkClampAirborneLateralEntry, wall clipping, ceiling clipping, corner clipping, bondwalk.c, [physics][jump], c136

## User preferences

- when the user reports collision regressions in concrete playtest terms such as "I could jump through the sides of overhead objects" and "I also seemed to get stuck above a doorway", preserve the exact symptom shape and look for the movement-order gap they described instead of broadening into unrelated movement work [Task 2]
- when dynamic objects are reported as still behaving correctly, preserve that behavior unless the evidence says otherwise; do not regress the couch/moving-prop path while fixing overhead blockers [Task 2]
- when Mike’s playtest is still the closure gate, keep the board/context distinction between build/test verified and Mike-pending runtime validation [Task 1][Task 2]
- when the user reports "jumping player clipping into walls ... ceilings or corners" -> stay tightly scoped to the named jump symptom and inspect horizontal-vs-vertical movement ordering before widening into broader collision refactors [Task 4]

## Reusable knowledge

- Player jump collision improved by replacing the single-center Stage 2 with a generic selfprop-aware multi-sample rendered capsule sweep, and both player and bot vertical paths were wired onto it; bot jumping itself remains unproven until direct runtime evidence says otherwise [Task 1]
- If a jump bug involves entering geometry from the side, a purely vertical sweep is insufficient; `bwalkClampAirborneSideEntry()` adds the needed upward diagonal `capsuleSweep()` before vertical resolution [Task 2]
- Movement collision should not depend on `func0f0849dc()`, `capsuleRenderedPropRayCast`, `model->matrices`, or other render-owned transient state; the stable ownership layer is `meshcollision` with `g_WorldMesh` and `prop->colmesh` [Task 3]
- `model_rodata_guard` is the right safety boundary for model rodata / GDL / Vtx extraction in this codebase, and fail-closed colmesh creation is acceptable if safe triangles cannot be extracted [Task 3]
- The remaining B-367 gap was the airborne horizontal half of the frame: `bwalkClampAirborneLateralEntry()` now runs immediately after horizontal movement and before speed correction, rolling X/Z back on wall/ceiling/corner hits while ignoring floor-class hits [Task 4]
- For jump regressions in this checkout, both the horizontal and vertical phases need to be checked; repairing only the vertical sweep can still leave the player inside geometry before correction runs [Task 2][Task 4]

## Failures and how to do differently

- Do not assume a rendered-surface or capsule bug is necessarily a mesh-ownership bug; the remaining gap can be movement ordering, especially a horizontal-first / vertical-second split [Task 2]
- Do not claim bot jump parity just because the shared vertical path changed; keep bot behavior marked unverified until there is focused runtime evidence [Task 1]
- When the first crash fix moves the failure forward into the next boundary, inspect the new stack instead of assuming the earlier architecture change fully closed the bug [Task 3]
- If the first airborne gate for a new clamp is too loose, tighten it so stray vertical velocity does not trigger collision rollback when the player is not genuinely airborne [Task 4]

# Task Group: Kanban Board Planning and Staleness Rules
scope: `tools/kanban` board behavior, review surfaces, active-card ordering, same-session tracking, and stale-card maintenance.
applies_to: cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike; reuse_rule=reuse for future Kanban UI, planning, and task-tracking work in this checkout.

## Task 1: Make pending-completion review rows title-first with details hidden behind an expander, success

### rollout_summary_files

- rollout_summaries/2026-05-18T17-11-11-GfT3-kanban_review_panel_title_first_details_expander.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\sessions\2026\05\18\rollout-2026-05-18T13-11-11-019e3c11-f859-7ce0-9c93-a00ceca35c01.jsonl, updated_at=2026-05-18T17:20:42+00:00, thread_id=019e3c11-f859-7ce0-9c93-a00ceca35c01)

### keywords

- pending-completion, Review panel, title-first, Review details, verify_notes, expected_artifacts

## Task 2: Rework Kanban navigation so status stays primary, Daily Flow is a peer tab, and star/priority become sort order instead of filter clutter, success

### rollout_summary_files

- rollout_summaries/2026-05-18T17-37-36-esL8-kanban_ui_tabs_daily_flow_sort_followups.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\sessions\2026\05\18\rollout-2026-05-18T13-37-36-019e3c2a-2a20-7f42-b8cb-671b342b451a.jsonl, updated_at=2026-05-18T18:52:42+00:00, thread_id=019e3c2a-2a20-7f42-b8cb-671b342b451a)

### keywords

- status tabs, Daily Flow, star sort, priority sort, top-level navigation, less filter clutter

## Task 3: Track same-session start/finish work explicitly on the board, success

### rollout_summary_files

- rollout_summaries/2026-05-19T15-07-45-irVP-kanban_same_session_task_tracking.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\sessions\2026\05\19\rollout-2026-05-19T11-07-45-019e40c7-54e6-7b50-a699-c5032d2e040d.jsonl, updated_at=2026-05-19T18:18:53+00:00, thread_id=019e40c7-54e6-7b50-a699-c5032d2e040d)
- rollout_summaries/2026-05-19T17-44-51-b8St-pd2_b350_side_entry_fix_kanban_update.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\sessions\2026\05\19\rollout-2026-05-19T13-44-51-019e4157-293d-7201-a9d7-2eab0109fe09.jsonl, updated_at=2026-05-19T18:19:31+00:00, thread_id=019e4157-293d-7201-a9d7-2eab0109fe09, B-350 card `c136`)
- rollout_summaries/2026-05-19T17-48-38-9OtF-pd2_combat_sim_room_watchdog_and_kanban_update.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\sessions\2026\05\19\rollout-2026-05-19T13-48-38-019e415a-a08d-76a2-b08e-20215bbc772f.jsonl, updated_at=2026-05-19T18:19:15+00:00, thread_id=019e415a-a08d-76a2-b08e-20215bbc772f, B-351 card `c135`)

### keywords

- same-session tracking, pending_completion, c136, c135, duplicate card IDs, Kanban board update

## Task 4: Reprioritize the active board and record the ship-critical dependency chain for compaction resilience, success

### rollout_summary_files

- rollout_summaries/2026-05-19T19-50-11-GEOb-kanban_priority_pass_mod_input_collision_dropout_stability.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\sessions\2026\05\19\rollout-2026-05-19T15-50-11-019e41c9-e543-7201-b10b-84ae56099f87.jsonl, updated_at=2026-05-20T21:25:10+00:00, thread_id=019e41c9-e543-7201-b10b-84ae56099f87)

### keywords

- c3812, c086, c136, c3813, c083, star priority order, compaction resilience, stale cards

## Task 5: Add a separate Memory Review tab and API for reviewing Codex memory groups before reset/rebuild, success

### rollout_summary_files

- rollout_summaries/2026-05-20T16-43-16-KKIN-kanban_memory_review_tab.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\sessions\2026\05\20\rollout-2026-05-20T12-43-16-019e4645-2352-7340-bb94-5bcba1d6d991.jsonl, updated_at=2026-05-20T18:29:49+00:00, thread_id=019e4645-2352-7340-bb94-5bcba1d6d991)

### keywords

- Memory Review, memory-review.json, /api/memory-review, review-only staging, own tab, check responses first

## Task 6: Make Memory Review live-source and writable, add durable bug deletion, and replace the old morning flow with Codex automations, success

### rollout_summary_files

- rollout_summaries/2026-05-21T19-56-56-IRQn-pd2_tooling_dynamic_memory_review_and_morning_automations.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\sessions\2026\05\21\rollout-2026-05-21T15-56-56-019e4c1c-cd71-78c1-95bd-242d957b6db3.jsonl, updated_at=2026-05-21T20:09:40+00:00, thread_id=019e4c1c-cd71-78c1-95bd-242d957b6db3, `c3825` live memory-review overlay, bug delete endpoint, and morning automations)

### keywords

- /api/bugs/delete, /api/memory-review/apply, memory-review.json, tools/bugs/state.json, live-source overlay, pd2-daily-flow, pd2-architecture-review, 5:00 AM, 6:00 AM, c3825

## User preferences

- when reviewing completion surfaces, the user said "I don't need to see the verbose details in the review unless I expand it, but rather the higher-level task that it represents." -> default review rows to concise title-first presentation and hide dense detail behind an expander [Task 1]
- when adjusting the main board, the user preferred top-level navigation with less filter clutter and wanted importance surfaced by ordering instead of more controls [Task 2]
- when the user said "Add to / Update the Kanban board, even if you both started and completed a task during this session it should still be tracked." -> always create or update a board record for same-session work instead of omitting it [Task 3]
- when the user warned that context would be compacted, preserve the current priority chain in durable board/context artifacts instead of relying on conversation memory [Task 4]
- when the user asked for a decision-response workflow to be "it's own tab" and that new sessions should "always check my responses unless directed specifically to do something else" -> surface repeated review workflows as first-class tabs and check that surface before choosing work unless the latest message overrides it [Task 5]
- when the user said Memory Review should be "dynamic and based on actual entries in memories.md" and they want to "modify at any time" with the actual memory file updated from markup -> treat review state as an overlay on live source, not as the source of truth itself [Task 6]
- when the user said they "can't delete them myself" about stale bug rows -> treat stale tracker rows as actionable cleanup and add a supported delete path instead of one-off file surgery [Task 6]
- when the user asked for a daily 5:00am flow plus a 6:00am architecture review -> default recurring maintenance to morning automations instead of manual orchestration [Task 6]

## Reusable knowledge

- Kanban status remains the primary navigation, pillars are scoped filters within status, Daily Flow is a peer top-level tab and starts collapsed, and star/priority should sort cards rather than becoming extra filter controls [Task 1][Task 2]
- Manual card order matters for agent planning; do not ignore user-arranged ordering [Task 2][Task 4]
- Same-session completed work still gets tracked on the board, typically through `pending_completion` when code/build work is done but a human retest remains [Task 3]
- Active sort is star -> priority -> order, so reprioritization requires updating the underlying `order`, `priority`, and sometimes `flag`/`flagged_at` fields [Task 4]
- After the priority pass, the explicit ship-critical chain was asset pipeline -> input -> collision -> gameplay stability / drop-in-drop-out, with `c3813` as the explicit new stability card and `c083` blocked until a fresh Combat Sim crash log exists [Task 4]
- The Memory Review staging surface is separate from `tools/kanban/state.json`; it is a review-only queue sourced from `MEMORY.md` and served by GET/PATCH `/api/memory-review` [Task 5]
- `tools/bugs/state.json` is the bug tracker source of truth, and `POST /api/bugs/delete` is now the supported permanent-delete path used by the UI `Del` action [Task 6]
- `GET /api/memory-review` now parses live `# Task Group:` blocks from `C:\Users\mikeh\.codex\memories\MEMORY.md`, while `tools/kanban/memory-review.json` stores only review markup keyed to those live rows [Task 6]
- `POST /api/memory-review/apply` applies `Remove` deletions and `Adjust` replacements back to the source file, but `Adjust` only fires when the note contains a full replacement block beginning with `# Task Group:` [Task 6]
- The accepted Codex automation IDs for this checkout are `pd2-daily-flow` at 5:00 AM and `pd2-architecture-review` at 6:00 AM [Task 6]

## Failures and how to do differently

- Browser automation may be unavailable locally if Playwright is missing; fall back to parse/runtime checks instead of claiming browser coverage [Task 1]
- Full unscoped `git status` timed out once during heavy board work; scoped status checks are safer in this repo for similar sessions [Task 4]
- This board already had overlapping card IDs during adjacent work; always validate uniqueness across the full card list after edits and avoid renumbering the same canonical card repeatedly [Task 3]
- Review/API smoke can create stray generated artifacts such as `NUL` or `__pycache__`; clean only the artifacts created by the current task and leave unrelated dirty state alone [Task 5]
- If automation creation rejects the first payload, mirror the accepted worktree/local-environment shape instead of assuming a simpler cron payload will work [Task 6]

# Task Group: PD2 Read-Only Assessments and Scope Expansion
scope: Read-only assessments, plan-first reviews, and architectural discovery runs where the user asks for diagnosis before code changes.
applies_to: cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike; reuse_rule=reuse when the user explicitly asks for read-only review, diagnosis, planning, or scope discovery before implementation.

## Task 1: Diagnose the second load-screen crash as an architectural issue, plan the fix first, then implement after approval, success

### rollout_summary_files

- rollout_summaries/2026-05-18T21-40-25-tPaL-pd2_meshcollision_extraction_hardening_loadscreen_crash.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\sessions\2026\05\18\rollout-2026-05-18T17-40-25-019e3d08-7840-75e1-a562-6bfd37078a5f.jsonl, updated_at=2026-05-19T01:36:35+00:00, thread_id=019e3d08-7840-75e1-a562-6bfd37078a5f)
- rollout_summaries/2026-05-18T16-48-33-DSex-skedar_gpu_benchmark_parity_wall_jump_investigation.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\sessions\2026\05\18\rollout-2026-05-18T12-48-33-019e3bfd-4177-7621-bd15-d5e158a3e865.jsonl, updated_at=2026-05-18T16:53:57+00:00, thread_id=019e3bfd-4177-7621-bd15-d5e158a3e865, investigation-first benchmark example)

### keywords

- read-only, plan a fix first, architectural problem, investigate first and plan, scope expansion

## Task 2: Rebuild Codex memory directly from the user-supplied source file instead of reinterpreting it, success

### rollout_summary_files

- rollout_summaries/2026-05-20T21-10-40-xs8l-pd2_memory_rebuild_and_asset_pipeline_correction.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\sessions\2026\05\20\rollout-2026-05-20T17-10-40-019e4739-f3f8-71a3-be12-7d536b0b67fd.jsonl, updated_at=2026-05-20T21:17:38+00:00, thread_id=019e4739-f3f8-71a3-be12-7d536b0b67fd, source-of-truth rebuild workflow)

### keywords

- source of truth, verbatim, memory-rebuild.md, update Codex memory, do not merge older memories

## User preferences

- when the user asks "Is this an architectural problem? plan a fix first, then we implement" or "Investigate first and plan." -> stay in diagnosis/planning mode first and separate subproblems before editing [Task 1]
- after the user approves the plan with wording like "PLEASE IMPLEMENT THIS PLAN", proceed directly with implementation rather than re-litigating the architecture [Task 1]
- when the user asks for a source-of-truth rebuild, preserve the provided artifact literally instead of summarizing or merging back older memory [Task 2]

## Reusable knowledge

- When Mike asks for a read-only assessment, keep it read-only and deliver the requested review or rubric before coding [Task 1]
- Architectural work often expands as the real scope is discovered; surface that scope decision rather than silently deferring important newly discovered work [Task 1]
- Use roadmap/context/tasks/session-log and the latest log or evidence file before broad source spelunking when the request is review/discovery oriented [Task 1]

## Failures and how to do differently

- Do not silently convert a plan-first request into a coding run just because the root cause looks obvious [Task 1]
- Do not restore older generated memories during a requested memory rebuild; confirm the new source replaced them [Task 2]

# Task Group: PD2 Queued Isolated Builds and Cleanup Discipline
scope: Queued isolated build/test workflow, wrapper caveats, and cleanup of temporary build sessions.
applies_to: cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike; reuse_rule=reuse for PD2 build verification in this checkout, especially when other sessions may also be building.

## Task 1: Verify controller menu-graph completion with isolated build sessions and remove the session afterward, success

### rollout_summary_files

- rollout_summaries/2026-05-19T19-58-49-VwGb-c036_controller_support_cohorts_menu_graph_complete.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\sessions\2026\05\19\rollout-2026-05-19T15-58-49-019e41d1-cf2a-7320-8405-7fbb17b7658f.jsonl, updated_at=2026-05-19T20:18:35+00:00, thread_id=019e41d1-cf2a-7320-8405-7fbb17b7658f)

### keywords

- build-session, isolated build, .claude/session-builds, c036done, wrapper summary, cleanup

## Task 2: Use queued isolated verification for press/hold and other focused lanes, success

### rollout_summary_files

- rollout_summaries/2026-05-23T20-44-50-ot87-tiny_mode_voice_pitch.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\sessions\2026\05\23\rollout-2026-05-23T16-44-55-019e5695-5e5f-7232-9526-7e8946ce81ab.jsonl, updated_at=2026-05-23T21:02:18+00:00, thread_id=019e5695-5e5f-7232-9526-7e8946ce81ab, longer timeout rerun after active-build watchdog stop)
- rollout_summaries/2026-05-18T20-35-32-kbzd-action_use_hold_only_interact_and_hold_ring_consumption.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\sessions\2026\05\18\rollout-2026-05-18T16-35-32-019e3ccd-0eee-7522-8457-566820dfc363.jsonl, updated_at=2026-05-18T20:48:19+00:00, thread_id=019e3ccd-0eee-7522-8457-566820dfc363)
- rollout_summaries/2026-05-18T16-16-37-8nu7-skedar_swarm_benchmark_parity_verified.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\sessions\2026\05\18\rollout-2026-05-18T12-16-37-019e3be0-05bb-7ee0-928a-f0ff9ce3f387.jsonl, updated_at=2026-05-18T17:39:24+00:00, thread_id=019e3be0-05bb-7ee0-928a-f0ff9ce3f387, watchdog timeout example)

### keywords

- hold340, skswarm, tinypitch, BuildTimeoutSeconds, queued isolated session, active-build watchdog, exit code 124

## User preferences

- when Mike says to skip tests, stop forcing verification and record the skipped or pending state honestly [Task 1][Task 2]
- when a task is verified in this repo, the user expects the temporary isolated build session to be cleaned up before the run is treated as complete [Task 1]

## Reusable knowledge

- Default to `devtools/build-session.ps1 -Session <short-id> -Target all` for queued isolated verification instead of the shared `Build/` output tree [Task 1][Task 2]
- Reuse the same session ID for reruns within one task, and locate binaries under `.claude/session-builds/<session-id>/` rather than assuming an older flat path [Task 1]
- If the all-target wrapper summary looks incomplete, verify client/updater, tests, and server separately before judging the build state [Task 1]
- If a clean all-target build is clearly still linking or otherwise progressing when the wrapper watchdog fires, rerun the same session with a longer `-BuildTimeoutSeconds` instead of assuming the code failed [Task 2]
- Clean up with `devtools/build-session.ps1 -Remove -Session <short-id>` when the task is done [Task 1]

## Failures and how to do differently

- Treat watchdog timeout exit code `124` as a hung-build failure unless current evidence proves the build is still making progress; increase timeout only when the lane is legitimately longer, such as a clean all-target link that is still active at the 60-second watchdog [Task 2]
- Do not assume the first `pd-tests.exe` path is still valid in this checkout; the isolated build layout has drifted from older flat-path examples [Task 1]

# Task Group: PD2 Targeted pd-tests Routing
scope: Focused `pd-tests` execution, Windows test-launch runtime, selector use, and keeping test/doc updates aligned with product rules.
applies_to: cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike; reuse_rule=reuse for targeted verification work in this checkout where a narrow suite can prove the relevant behavior.

## Task 1: Use focused selectors for menu-graph, F6, jump, and hold semantics verification, success

### rollout_summary_files

- rollout_summaries/2026-05-23T20-44-50-ot87-tiny_mode_voice_pitch.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\sessions\2026\05\23\rollout-2026-05-23T16-44-55-019e5695-5e5f-7232-9526-7e8946ce81ab.jsonl, updated_at=2026-05-23T21:02:18+00:00, thread_id=019e5695-5e5f-7232-9526-7e8946ce81ab, `[audio][voice][tiny][static]`)
- rollout_summaries/2026-05-19T19-58-49-VwGb-c036_controller_support_cohorts_menu_graph_complete.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\sessions\2026\05\19\rollout-2026-05-19T15-58-49-019e41d1-cf2a-7320-8405-7fbb17b7658f.jsonl, updated_at=2026-05-19T20:18:35+00:00, thread_id=019e41d1-cf2a-7320-8405-7fbb17b7658f, `[input][menu_graph]`)
- rollout_summaries/2026-05-19T18-33-07-wA5I-pd2_skedar_campaign_credits_and_b345_followup.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\sessions\2026\05\19\rollout-2026-05-19T14-33-07-019e4183-5914-7902-a0a5-ee804ed93a82.jsonl, updated_at=2026-05-19T20:21:13+00:00, thread_id=019e4183-5914-7902-a0a5-ee804ed93a82, `[debug][campaign][f6]`)
- rollout_summaries/2026-05-18T20-35-32-kbzd-action_use_hold_only_interact_and_hold_ring_consumption.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\sessions\2026\05\18\rollout-2026-05-18T16-35-32-019e3ccd-0eee-7522-8457-566820dfc363.jsonl, updated_at=2026-05-18T20:48:19+00:00, thread_id=019e3ccd-0eee-7522-8457-566820dfc363, `[press-hold]`)
- rollout_summaries/2026-05-19T17-44-51-b8St-pd2_b350_side_entry_fix_kanban_update.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\sessions\2026\05\19\rollout-2026-05-19T13-44-51-019e4157-293d-7201-a9d7-2eab0109fe09.jsonl, updated_at=2026-05-19T18:19:31+00:00, thread_id=019e4157-293d-7201-a9d7-2eab0109fe09, `[physics][jump]`)

### keywords

- run-pd-tests.ps1, -Scope, -Selector, [audio][voice][tiny][static], [input][menu_graph], [debug][campaign][f6], [press-hold], [physics][jump]

## Task 2: Fix the Windows `clock_gettime64` loader popup by making `pd-tests.exe` resolve `winpthread` locally, success

### rollout_summary_files

- rollout_summaries/2026-05-21T16-03-36-thOE-pd_tests_clock_gettime64_loader_popup_fix.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\sessions\2026\05\21\rollout-2026-05-21T12-03-36-019e4b47-2c05-7ab2-b44e-3467b5d91e80.jsonl, updated_at=2026-05-21T16:23:09+00:00, thread_id=019e4b47-2c05-7ab2-b44e-3467b5d91e80, local `libwinpthread-1.dll` copy and wrapper hardening)

### keywords

- clock_gettime64, libwinpthread-1.dll, objdump -p, WINPTHREAD_STATIC=1, pd-tests.exe, run-pd-tests-smoke.ps1, B-355

## User preferences

- when product rules change, the user expects focused verification tied to the named behavior instead of a broad undifferentiated test pass [Task 1]
- when the user says automated tests keep hitting a Windows loader popup, treat it as a real automation blocker and prefer a durable binary/environment fix over manual launch workarounds [Task 2]

## Reusable knowledge

- Prefer `devtools/run-pd-tests.ps1` with `-Scope` or `-Selector` for focused `pd-tests` lanes when applicable, and keep test/docs alignment in the same slice when product rules change [Task 1]
- The recent reliable focused selectors in this checkout were `[audio][voice][tiny][static]`, `[input][menu_graph]`, `[debug][campaign][f6]`, `[press-hold]`, and `[physics][jump]` [Task 1]
- For Windows `pd-tests.exe` popup issues, check the binary import table first (`objdump -p`) before assuming the wrapper is the only culprit [Task 2]
- The durable fix for B-355 was not removing the dependency entirely but copying the matching `libwinpthread-1.dll` beside `pd-tests.exe` so Windows resolves the local runtime before `PATH` [Task 2]
- `devtools/run-pd-tests.ps1`, Dev Window v2, and `tools/smoke-verify/run-pd-tests-smoke.ps1` all matter for this lane; a test-launch fix is incomplete if only one wrapper is hardened [Task 2]
- The smoke wrapper must accept both singular and plural Catch2 success footers when parsing output (`test case` / `test cases`) [Task 2]

## Failures and how to do differently

- Stale historical runner-failure details should not dominate future verification choices unless they recur in the current repo state [Task 1]
- If a first attempt to make `pd-tests` fully static still leaves `libwinpthread-1.dll` in the import table, stop assuming the static-link comment is true and validate the actual binary/runtime pair [Task 2]
- When `build-session.ps1` fails under sandbox Git ownership issues, rerun from the real checkout before treating the loader popup lane as a code failure [Task 2]

# Task Group: PD2 Client-Hosted Online and Connect-Code Contract
scope: Listen-host/client-hosted online, connect-code UI, NAT-aware handoffs, and trust/security hardening.
applies_to: cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike; reuse_rule=reuse for future PD2 online interoperability work in this checkout; treat dedicated-server productization as out of scope unless reopened explicitly.

## Task 1: Preserve the rebuilt online contract during the 2026-05-20 memory rebuild, success

### rollout_summary_files

- rollout_summaries/2026-05-20T21-10-40-xs8l-pd2_memory_rebuild_and_asset_pipeline_correction.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\sessions\2026\05\20\rollout-2026-05-20T17-10-40-019e4739-f3f8-71a3-be12-7d536b0b67fd.jsonl, updated_at=2026-05-20T21:17:38+00:00, thread_id=019e4739-f3f8-71a3-be12-7d536b0b67fd, rebuilt memory retained this task group)

### keywords

- client-hosted online, connect code, NAT traversal, dedicated server deferred, stale direct-IP UI

## Task 2: Fix social presence/invite handling, remove obsolete Main Menu Online Play, and leave the card pending live retest, partial

### rollout_summary_files

- rollout_summaries/2026-05-21T21-14-02-sdcU-c3828_social_presence_invites_main_menu_play.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\sessions\2026\05\21\rollout-2026-05-21T17-14-02-019e4c63-64df-7c13-9ff2-dd0dbce0f6d4.jsonl, updated_at=2026-05-21T23:59:24+00:00, thread_id=019e4c63-64df-7c13-9ff2-dd0dbce0f6d4, code/build verified but still pending Mike+Chris live retest)

### keywords

- c3828, B-364, social presence, invite, Play, Online Play, socialRebindToActiveAgent, UDP 27105, netStartClientWithHolePunch, pending_completion

## Task 3: Fix the second-pass existing-agent startup gap so sidecar-loaded agents actually come online, success

### rollout_summary_files

- rollout_summaries/2026-05-22T03-39-01-hpX4-existing_agent_presence_startup_second_pass.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\sessions\2026\05\21\rollout-2026-05-21T23-39-01-019e4dc3-d9ec-7e42-9f88-5b06acdf446a.jsonl, updated_at=2026-05-22T03:48:57+00:00, thread_id=019e4dc3-d9ec-7e42-9f88-5b06acdf446a, existing-agent social-hub startup fix after `c3828`)

### keywords

- prefsAgentLoad, socialHubBringOnline, presenceMarkAgentLoaded, existing agent, sidecar, c3828, B-364, Offline rows

## User preferences

- when the user reports that players can add each other by code but "aren’t appearing as online to one another" -> trace the live online path end-to-end instead of assuming the connect-code join path proves presence is healthy [Task 2][Task 3]
- when the user asks to make invites work even with stale Offline display -> keep recovery actions visible instead of hard-gating them behind presence badges [Task 2]
- when code/build verification passes but the user still needs a real two-client check, keep the board item pending completion instead of marking it simply done [Task 2]

## Reusable knowledge

- Current online scope is listen-host/client-hosted behavior; dedicated-server productization is deferred unless Mike explicitly revives it [Task 1]
- No raw IP should appear in player-facing UI surfaces; connect codes are the share/join mechanism [Task 1]
- Stale direct-IP UI, docs, or tests are bugs once product behavior is connect-code-only [Task 1]
- Player-facing remote handoffs should use the hole-punch-aware client path instead of bypassing NAT traversal [Task 1]
- Mod transfer integrity, malformed wire strings, and listen-host trust boundaries are security surfaces, not polish [Task 1]
- Per-agent connect codes in this checkout derive from `(pubkey || agent_name)`, so presence validation and social handle binding must account for the active save-slot agent name, not only a device-level identity [Task 2]
- Presence frames now carry agent-name and status fields separately, and invite/group-session/live-spectator handoffs should go through the NAT-aware `netStartClientWithHolePunch` path [Task 2]
- The main player-facing online entry is now the Social-based `Play` flow; the old Main Menu `Online Play` direct-connect surface is deprecated/removed [Task 2]
- `prefsAgentLoad()` has two meaningful paths, and both the no-sidecar and successful sidecar branches must call `socialHubBringOnline()` before `presenceMarkAgentLoaded()` or existing agents will stay Offline [Task 3]
- `socialHubBringOnline()` is the actual socket-start gate; `presenceMarkAgentLoaded()` only flips the presence-ready state [Task 3]

## Failures and how to do differently

- When working in a dirty tree on online lanes, categorize dirty files by lane and avoid sweeping unrelated changes into the online work [Task 1]
- Do not close presence/invite work after code/build verification alone; the common configured existing-agent path can still be broken until a real two-client retest says otherwise [Task 2][Task 3]
- If a first-pass presence fix only validates no-sidecar/bootstrap behavior, explicitly retest sidecar-loaded existing agents before declaring the lane finished [Task 3]

# Task Group: PD2 Mod Sharing and Public Mods Trust Rules
scope: Public Mods, direct mod sharing, online-required mod delivery, and received-mod enable policy.
applies_to: cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike; reuse_rule=reuse for future PD2 mod sharing and connected-player mod access work in this checkout.

## Task 1: Preserve the rebuilt mod-sharing trust rules while the asset-pipeline contract shifted to typed pdxxx content units, success

### rollout_summary_files

- rollout_summaries/2026-05-20T21-10-40-xs8l-pd2_memory_rebuild_and_asset_pipeline_correction.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\sessions\2026\05\20\rollout-2026-05-20T17-10-40-019e4739-f3f8-71a3-be12-7d536b0b67fd.jsonl, updated_at=2026-05-20T21:17:38+00:00, thread_id=019e4739-f3f8-71a3-be12-7d536b0b67fd)
- rollout_summaries/2026-05-19T19-50-11-GEOb-kanban_priority_pass_mod_input_collision_dropout_stability.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\sessions\2026\05\19\rollout-2026-05-19T15-50-11-019e41c9-e543-7201-b10b-84ae56099f87.jsonl, updated_at=2026-05-20T21:25:10+00:00, thread_id=019e41c9-e543-7201-b10b-84ae56099f87, confirms `.pdmod` is transport while typed archives are the content surface)

### keywords

- Public Mods, friend-sourced auto-enable, validation before install, safe IDs, .pdmod transport wrapper

## Reusable knowledge

- Mods should have their files validated for security before install/enable [Task 1]
- Mod sharing between connected players should be seamless and native once validated [Task 1]
- Friend-sourced direct/requested mod sharing should auto-accept and hot-enable after validation, while non-friend sources should prompt before enabling after download/install [Task 1]
- Sharing should use registered/known mods and safe IDs, not arbitrary peer-provided filesystem paths [Task 1]
- `.pdmod` remains the transport wrapper for sharing/Public Mods/online-required delivery, while typed `*.pdxxx` archives remain the content units [Task 1]

## Failures and how to do differently

- Do not regress into loose descriptor/sidecar thinking for the content contract; modder-facing content units are the typed archives, with `.pdmod` only as the delivery envelope [Task 1]

# Task Group: PD2 Menu Architecture Guidance
scope: Menu graph, menu transitions, watchdog behavior, and controller-facing menu architecture.
applies_to: cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike; reuse_rule=reuse for future PD2 menu cleanup and UI architecture work in this checkout.

## Task 1: Finish the active ImGui menu-graph migration and eliminate direct stack calls from active pdgui menu files, success

### rollout_summary_files

- rollout_summaries/2026-05-19T19-58-49-VwGb-c036_controller_support_cohorts_menu_graph_complete.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\sessions\2026\05\19\rollout-2026-05-19T15-58-49-019e41d1-cf2a-7320-8405-7fbb17b7658f.jsonl, updated_at=2026-05-19T20:18:35+00:00, thread_id=019e41d1-cf2a-7320-8405-7fbb17b7658f)

### keywords

- menu graph, EDGE_PUSH_ANY, menuGraphFireReplaceDialog, menuGraphFirePop, pdgui_menu_*.cpp, zero direct stack calls

## Task 2: Fix the Combat Simulator room watchdog by distinguishing leak cleanup from force-close cleanup, success

### rollout_summary_files

- rollout_summaries/2026-05-19T17-48-38-9OtF-pd2_combat_sim_room_watchdog_and_kanban_update.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\sessions\2026\05\19\rollout-2026-05-19T13-48-38-019e415a-a08d-76a2-b08e-20215bbc772f.jsonl, updated_at=2026-05-19T18:19:15+00:00, thread_id=019e415a-a08d-76a2-b08e-20215bbc772f)

### keywords

- menuPoolConsistencyCheck, MENU_TYPE_ROOM, standalone overlays, leak-only cleanup, menupoolReleaseAll

## Task 3: Fix Combat Simulator post-match endscreen interaction after the screen was already visible, success

### rollout_summary_files

- rollout_summaries/2026-05-21T03-59-33-ndop-pd2_combat_sim_postmatch_endscreen_interaction_fix.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\sessions\2026\05\20\rollout-2026-05-20T23-59-33-019e48b0-4bd3-7072-afa4-5b4da5e89925.jsonl, updated_at=2026-05-21T16:57:08+00:00, thread_id=019e48b0-4bd3-7072-afa4-5b4da5e89925, second-pass B-356 interaction fix for visible MP endscreen)

### keywords

- Combat Simulator, post-match screen, B-356, pdguiRenderConfirmModal, pdguiConsumeTitleClose, ImGuiFocusedFlags_RootAndChildWindows, confirm popup scope, title X

## Task 4: Fix Main Menu root-close so X/Escape actually exit instead of reopening the menu, success

### rollout_summary_files

- rollout_summaries/2026-05-21T20-21-40-siKb-pd2_main_menu_root_close_exits_cleanly.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\sessions\2026\05\21\rollout-2026-05-21T16-21-40-019e4c33-7086-7063-8e03-f0b1e3a1ecb7.jsonl, updated_at=2026-05-21T20:32:33+00:00, thread_id=019e4c33-7086-7063-8e03-f0b1e3a1ecb7, root-pop now closes both menu ownership layers)

### keywords

- menuClose, menupoolReleaseAll, POP_ROOT, X button, Escape double sound, Main Menu, B-361

## Task 5: Gate Main Menu entry on the Carrington Institute camera finishing across all entry paths, success

### rollout_summary_files

- rollout_summaries/2026-05-21T18-03-22-3G8c-main_menu_ci_camera_and_cutscene_hold_skip.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\sessions\2026\05\21\rollout-2026-05-21T14-03-22-019e4bb4-d4ea-7002-bc6b-965a7a79923c.jsonl, updated_at=2026-05-21T18:43:37+00:00, thread_id=019e4bb4-d4ea-7002-bc6b-965a7a79923c, CI camera readiness gate for cold boot and return flows)

### keywords

- Main Menu, Carrington Institute, ciReadyForMenuOpen, ciHoldMenuOpenUntilCameraReady, g_PostExitMainMenuView, var80087260, c3821

## User preferences

- when the correct fix is architectural, the user expects the architecture to be cleaned up rather than hidden behind small ad hoc stack-state patches [Task 1][Task 2]
- when the user reports that a Combat Simulator post-match screen is visible but the X or Quit/Disconnect confirm "does not respond" -> inspect popup scope and title-close focus routing, not just the renderer [Task 3]
- for controller-facing menu work, preserve real controller usability and transitions across the whole surface, not just a static migration [Task 1]
- when the user reports "I can't exit menus with the X button" and Escape plays the sound twice but keeps the menu open -> treat duplicate-close feedback as a split state/ownership bug, not just a binding issue [Task 4]
- when the user says Main Menu loading "should work for all entries ... including returning there from game matches or missions ending" -> cover cold boot and return paths together when menu-entry timing is the issue [Task 5]

## Reusable knowledge

- Prefer proper menu architecture over ad hoc push/pop or one-off state flags; the graph is now the controller-facing abstraction for the active ImGui menu surface [Task 1]
- `menuGraphFireReplaceDialog` is the pattern for pop-then-push transitions that should not expose raw stack calls, and `EDGE_PUSH_ANY` is the escape hatch for legacy/unregistered dialogdefs during migration [Task 1]
- The room screen is a valid menu-pool occupant even when the legacy dialog stack is empty; the watchdog fix boundary is "release only legacy-stack leaks" rather than "release less everywhere" [Task 2]
- A visible menu can still be effectively dead if confirm popups are opened from a child scope and rendered from the parent scope; defer the popup open until after the child closes, then render/open it from the parent endscreen scope [Task 3]
- `pdguiConsumeTitleClose()` should accept root-and-child focus when the title X must work after focus moves into nested action/content children [Task 3]
- Root-pop edges are not fully closed by `menupoolReleaseAll()` alone; if the legacy root dialog survives, `menuClose()` must run too or the Main Menu can re-open on the next frame [Task 4]
- The Main Menu camera-readiness gate lives in `src/game/menutick.c`; `ciReadyForMenuOpen()` / `ciHoldMenuOpenUntilCameraReady()` are the right enforcement point for cold boot plus `g_PostExitMainMenuView` / `var80087260` return paths [Task 5]

## Failures and how to do differently

- If the wrapper build summary is incomplete, do not assume the menu lane is verified; confirm client/updater, tests, and server separately [Task 1]
- Do not treat standalone pure-ImGui overlays as leaks just because the legacy stack is empty [Task 2]
- If a visible post-match screen still ignores clicks, check popup ownership/scope and title-close focus before assuming the renderer is dead [Task 3]
- Do not record a post-match interaction fix as advanced until the focused `[input][menu_graph]` selector and the all-target build both pass; move it to fixed-pending-playtest only after that evidence exists [Task 3]
- If closing a menu appears to work for one ownership layer but the surface re-opens immediately, inspect surviving legacy dialogs in addition to menu-pool/input-context state [Task 4]
- If the active worktree contains lots of generated smoke artifacts, keep the durable memory anchored on the verified source behavior and the context/Kanban updates instead of treating those artifacts as part of the feature [Task 5]

# Task Group: PD2 Log-First Runtime Diagnostics
scope: Runtime bug diagnosis for black screens, load failures, catalog misses, lifecycle cleanup, and scenario/debug-launch issues.
applies_to: cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike; reuse_rule=reuse for future gameplay/runtime crash, black-screen, or missing-object investigations in this checkout.

## Task 1: Diagnose and fix Combat Simulator restart-after-leave crash from the build log, success

### rollout_summary_files

- rollout_summaries/2026-05-22T04-29-11-jKNd-b368_combat_sim_restart_after_leave_crash_fix.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\sessions\2026\05\22\rollout-2026-05-22T00-29-11-019e4df1-c7b7-74d2-9f31-28e45e8505bb.jsonl, updated_at=2026-05-22T04:46:32+00:00, thread_id=019e4df1-c7b7-74d2-9f31-28e45e8505bb, B-368 restart crash traced through teardown and stale MP runtime state)

### keywords

- Build/logs/game client/pd-client.log, objFree, lvStop, chrmgrStop, chrRemove, mpClearRuntimeChrState, g_MpAllChrPtrs, g_MpNumChrs, pdguiEndscreenExitToMainMenu, pdguiEndscreenExitToRoom, B-368

## Task 2: Diagnose Defection Perfect mission-start crash from `Build\pd-client.log` and move the ownership boundary to collision-owned meshes, success

### rollout_summary_files

- rollout_summaries/2026-05-18T19-44-21-PWIt-defection_perfect_crash_mesh_collision_ownership.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\sessions\2026\05\18\rollout-2026-05-18T15-44-21-019e3c9e-3502-79b3-a33f-1ac0eb03a9b5.jsonl, updated_at=2026-05-18T20:59:16+00:00, thread_id=019e3c9e-3502-79b3-a33f-1ac0eb03a9b5)

### keywords

- Build\\pd-client.log, Defection Perfect, stage=0x30, CHR.TICK, symbolication, rendered-prop floor acquisition

## Task 3: Diagnose the second load-screen crash from the newest log, then harden mesh extraction instead of reverting the architecture, success

### rollout_summary_files

- rollout_summaries/2026-05-18T21-40-25-tPaL-pd2_meshcollision_extraction_hardening_loadscreen_crash.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\sessions\2026\05\18\rollout-2026-05-18T17-40-25-019e3d08-7840-75e1-a562-6bfd37078a5f.jsonl, updated_at=2026-05-19T01:36:35+00:00, thread_id=019e3d08-7840-75e1-a562-6bfd37078a5f)

### keywords

- second exception, setupCreateDoor, meshExtractFromModel, extractGfxTris, architectural problem, latest log first

## Task 4: Follow up on the rejected B-345 black-screen/missing-prop card with a narrower hoverbed smoke and a Chicrob metadata fix, success

### rollout_summary_files

- rollout_summaries/2026-05-19T18-33-07-wA5I-pd2_skedar_campaign_credits_and_b345_followup.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\sessions\2026\05\19\rollout-2026-05-19T14-33-07-019e4183-5914-7902-a0a5-ee804ed93a82.jsonl, updated_at=2026-05-19T20:21:13+00:00, thread_id=019e4183-5914-7902-a0a5-ee804ed93a82)
- rollout_summaries/2026-05-19T01-43-41-RxuK-mission_start_prop_mesh_detach_fix_and_smoke_cleanup.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\sessions\2026\05\19\rollout-2026-05-18T21-43-41-019e3de7-2d8c-7fc3-8e1c-519a15e5f5ce.jsonl, updated_at=2026-05-19T02:37:07+00:00, thread_id=019e3de7-2d8c-7fc3-8e1c-519a15e5f5ce, earlier mission-start lifecycle cleanup)

### keywords

- B-345, black screen, missing props, Elvis medical bed, PhoverbedZ, BODY_CHICROB, head_canon=NULL for headnum=0, auto_campaign_first_cycle

## Task 5: Fix the Infiltration robot-attack build exception with fail-closed guards and a stage-specific smoke replay, success

### rollout_summary_files

- rollout_summaries/2026-05-22T01-38-10-rIJM-infiltration_robot_attack_exception_fix.md (cwd=C:\Users\mikeh\Documents\Codex\2026-05-21\goal-i-playtested-in-build-and, rollout_path=C:\Users\mikeh\.codex\archived_sessions\rollout-2026-05-21T21-38-10-019e4d55-3467-7021-a558-89e0a6da73be.jsonl, updated_at=2026-05-22T02:04:26+00:00, thread_id=019e4d55-3467-7021-a558-89e0a6da73be, build-log-first B-365 robot attack exception fix in the PD2 repo)

### keywords

- chrTickRobotAttack, robotSetMuzzleFlash, ROBOT.ATTACK.GUARD, base:infiltration, auto_campaign_infiltration_robot_attack, [chraction][robot][static][b365], B-365

## Task 6: Diagnose the Combat Simulator start crash from the newest build log and surface the stale bot target-prop dereference, success

### rollout_summary_files

- rollout_summaries/2026-05-21T14-36-44-FI4k-combat_sim_start_crash_and_extraction_fallback_fix.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\sessions\2026\05\21\rollout-2026-05-21T10-36-44-019e4af7-a56e-7883-9a83-543e185f5924.jsonl, updated_at=2026-05-21T16:00:50+00:00, thread_id=019e4af7-a56e-7883-9a83-543e185f5924, `botJumpDecide` crash from stale target prop on Combat Sim start)

### keywords

- Combat Simulator, botJumpDecide, ACCESS_VIOLATION, attackpropnum, prop backlink, src/game/bot.c:3271, B-316

## Task 7: Diagnose and fix the F9 diagnostics overlay crash on solo pause resume from the latest log, success

### rollout_summary_files

- rollout_summaries/2026-05-21T17-49-44-P10d-pd2_f9_diagnostics_overlay_crash_solo_pause_resume.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\sessions\2026\05\21\rollout-2026-05-21T13-49-44-019e4ba8-5717-7561-b314-2ac54c77dbd9.jsonl, updated_at=2026-05-21T17:57:29+00:00, thread_id=019e4ba8-5717-7561-b314-2ac54c77dbd9, B-359 read-only overlay crash fix)

### keywords

- F9 diagnostics overlay, soloMenuTitlePauseStatus, menuResolveDialogTitle, pdguiDebugFormatLegacyMenuInfo, pdguiMenuStackOverlayRender, B-359

## Task 8: Record the user’s PD2 log-location shorthand for faster runtime diagnostics, success

### rollout_summary_files

- rollout_summaries/2026-05-21T00-39-23-o7K9-pd2_log_location_shorthand.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\sessions\2026\05\20\rollout-2026-05-20T20-39-23-019e47f9-0958-7f33-b2c2-099b71f53b99.jsonl, updated_at=2026-05-21T14:05:41+00:00, thread_id=019e47f9-0958-7f33-b2c2-099b71f53b99, durable shorthand note for build vs release client log folders)

### keywords

- in build, release folder, install directory, Build\\logs\\game client, Downloads\\Perfect Dark 2.0\\logs\\game client, log shorthand

## User preferences

- when the user says "Start match or mission, leave. Try to start a new one." -> treat it as a lifecycle/restart bug and inspect teardown plus re-entry instead of only the visible crash site [Task 1]
- when the user points at a runtime log or says "Log is in build folder" or "Check the log in build, I got an exception." -> start from the log rather than from prior issue state or source guesses [Task 1][Task 2][Task 3]
- when the user rejects a "fixed" card because some mission starts still look black or miss a visible prop, treat the complaint as a concrete runtime artifact problem and investigate the named object or mission start directly [Task 4]
- when a user names a specific missing object such as the Elvis medical bed, add a targeted smoke fixture for that exact object/path instead of relying only on a broad campaign run [Task 4]
- when the user reports a build/playtest exception and points at the build log, start from the newest runtime log and exact stage path before guessing from the symptom alone [Task 5]
- when the user says "Investigate and surface what the failure was." for a Combat Simulator crash, report the concrete failure from the live log rather than a speculative blocked hypothesis [Task 6]
- when the user says another session is already reviewing the extraction crash, stand down on that bug and record only the durable environment shorthand they asked for [Task 8]
- when the user defines phrases like "in build" or "release folder / install directory", treat them as stable path shorthand and map them directly to the exact log folders [Task 8]

## Reusable knowledge

- For black screens, HUD-only loads, restart-after-leave crashes, missing objects, or debug scenario failures, check launch-path ownership, load/manifest state, catalog/provider registration, and teardown lifecycle before guessing at rendering [Task 1][Task 2][Task 3][Task 4]
- The newest `Build/logs/game client/pd-client.log` was enough to isolate the B-368 crash path, and `addr2line` cleanly symbolicated it to `objFree()` in the teardown chain `lvStop -> chrmgrStop -> chrRemove -> objFree` [Task 1]
- `mpClearRuntimeChrState()` is now the central helper for clearing `g_MpNumChrs`, `g_MpAllChrPtrs`, `g_MpAllChrConfigPtrs`, and bots after MP endscreen exits; the endscreen exit paths plus the legacy MP fallback call it before the next stage starts [Task 1]
- `propobj.c` MP cleanup loops should null-check each `g_MpAllChrPtrs[i]` before dereferencing `chr->aibot`, because stale/null runtime chr slots can survive long enough to crash teardown on the next start [Task 1]
- Symbolication can help, but recent evidence showed the log breadcrumbs were often more useful than raw `addr2line` output when the backtrace came back `??` [Task 2]
- The mission-start lifecycle fix moved dynamic prop-mesh teardown into a live-prop cleanup pass before `MEMPOOL_STAGE` reset, and later smoke cleanup removed stale free-tail traversal artifacts [Task 4]
- The broad `auto_campaign_first_cycle` fixture can be useful for proving early mission-start health even if it times out before full campaign completion; judge it by the target assertions and named artifact loads [Task 4]
- `chrTickRobotAttack()` / `robotSetMuzzleFlash()` are the exact retrieval handles for the Infiltration robot-attack exception, and the durable fix is fail-closed validation of chr/model/prop/target/beam state before muzzle/beam writes proceed [Task 5]
- `ROBOT.ATTACK.GUARD:` is now the diagnostic string for invalid robot attack state, and `tools/smoke-verify/tests/auto_campaign_infiltration_robot_attack.json` is the narrow replay path for this crash class [Task 5]
- The Combat Sim start crash class in `botJumpDecide` came from dereferencing `tprop->chr` after `tprop = &g_Vars.props[chr->aibot->attackpropnum]`; validating only `attackpropnum >= 0` is insufficient without prop-pool range checks and `prop->chr` / `chr->prop` backlink validation [Task 6]
- The useful stack for that crash class was `botJumpDecide -> botJumpTickEval -> botTickUnpaused -> botTick -> propsTickPlayer -> lvRender -> mainTick`, with the faulting read at `src/game/bot.c:3271` [Task 6]
- Read-only diagnostics surfaces can still crash runtime: `pdguiDebugFormatLegacyMenuInfo()` should only resolve literal or lang-backed titles, while callback-backed legacy titles must render as placeholders such as `<dynamic title>` [Task 7]
- `soloMenuTitlePauseStatus()` must tolerate `curdialog == NULL` or `curdialog->definition == NULL` if the pause dialog is already released during overlay render [Task 7]
- The user’s shorthand log roots in this checkout are `C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike\Build\logs\game client` for "in build" and `C:\Users\mikeh\Downloads\Perfect Dark 2.0\logs\game client` for "release folder / install directory" [Task 8]

## Failures and how to do differently

- A broad status scan can time out in a dirty tree; for crash follow-up in this checkout, prefer focused file checks plus the exact regression selector over repeated whole-tree scans [Task 1]
- When adding a regression case in an already-busy test file, inspect the existing block and append with matching anchors instead of patching by assumption [Task 1]
- Do not assume the first crash fix closed the issue just because the crash moved; inspect the next stack and keep the architecture if the new failure is only at a still-unguarded boundary [Task 3]
- Do not over-trust broad smokes for closure when the useful signal is a named mission-start path or object load; add a narrow smoke fixture [Task 4]
- When cleaning up after runtime-smoke work, remove generated smoke artifacts from the current session but do not sweep unrelated dirty files [Task 4]
- If a crash occurs in a repeatable stage such as Infiltration, add a stage-specific smoke replay instead of waiting for a longer whole-campaign runner to reach the same point [Task 5]
- Once a fresh runtime log exists, stop treating the issue as a blocked-log mystery and update the memory to the concrete observed failure [Task 6]
- If a crash appears to happen during resume or gameplay teardown but the stack points into a read-only diagnostics overlay, follow the latest log and fix the actual callback/title path instead of the first suspected subsystem [Task 7]
- Do not keep pushing an extraction-crash investigation after the user says another session owns it; preserve the environment shorthand and hand off cleanly [Task 8]

# Task Group: PD2 Catalog as Asset Reference Authority
scope: Catalog-owned asset identity, lookup, provider, and reference behavior.
applies_to: cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike; reuse_rule=reuse for future asset, loader, mod, network-content, and gameplay reference work in this checkout.

## Task 1: Fix a mission-start body/head metadata mismatch and record the catalog-facing implication, success

### rollout_summary_files

- rollout_summaries/2026-05-19T18-33-07-wA5I-pd2_skedar_campaign_credits_and_b345_followup.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\sessions\2026\05\19\rollout-2026-05-19T14-33-07-019e4183-5914-7902-a0a5-ee804ed93a82.jsonl, updated_at=2026-05-19T20:21:13+00:00, thread_id=019e4183-5914-7902-a0a5-ee804ed93a82)
- rollout_summaries/2026-05-20T21-10-40-xs8l-pd2_memory_rebuild_and_asset_pipeline_correction.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\sessions\2026\05\20\rollout-2026-05-20T17-10-40-019e4739-f3f8-71a3-be12-7d536b0b67fd.jsonl, updated_at=2026-05-20T21:17:38+00:00, thread_id=019e4739-f3f8-71a3-be12-7d536b0b67fd, rebuilt memory preserved the catalog rule)

### keywords

- catalog, BODY_CHICROB, base:sp_body_118, CchicrobZ, provider registration, asset identity

## Reusable knowledge

- The catalog is the single source of truth for asset references in the game [Task 1]
- Gameplay and systems should ask the catalog by asset identity rather than guessing file paths, ROM file numbers, archive locations, provider internals, or fallback order [Task 1]
- Base content, mods, network-delivered content, validation, rebuild, load, unload, and dependency behavior should converge through the catalog identity layer [Task 1]
- Provider details and fallback order belong behind catalog APIs rather than being scattered through gameplay callsites [Task 1]
- The B-345 follow-up showed how a concrete asset-reference issue can surface as a metadata mismatch (`BODY_CHICROB` / `base:sp_body_118`) rather than a renderer-only failure [Task 1]

## Failures and how to do differently

- Do not collapse catalog/provider issues into generic black-screen notes; preserve the exact asset IDs and warning strings because they are the real retrieval handles [Task 1]

# Task Group: PD2 Layer-Aware Input Authority
scope: Input ownership across gameplay, menus, room overlays, held actions, analog values, and legacy pad mirrors.
applies_to: cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike; reuse_rule=reuse for future action-map, input routing, UI, gameplay control, and modal/menu ownership work in this checkout.

## Task 1: Fix ACTION_USE ownership so tap/hold semantics and hold-ring state agree across gameplay and UI, success

### rollout_summary_files

- rollout_summaries/2026-05-18T20-35-32-kbzd-action_use_hold_only_interact_and_hold_ring_consumption.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\sessions\2026\05\18\rollout-2026-05-18T16-35-32-019e3ccd-0eee-7522-8457-566820dfc363.jsonl, updated_at=2026-05-18T20:48:19+00:00, thread_id=019e3ccd-0eee-7522-8457-566820dfc363)

### keywords

- layer-aware input, ACTION_USE, held actions, action map, prompt renderer, ownership

## Task 2: Fix Combat Simulator room input snapping caused by watchdog ownership churn, success

### rollout_summary_files

- rollout_summaries/2026-05-19T17-48-38-9OtF-pd2_combat_sim_room_watchdog_and_kanban_update.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\sessions\2026\05\19\rollout-2026-05-19T13-48-38-019e415a-a08d-76a2-b08e-20215bbc772f.jsonl, updated_at=2026-05-19T18:19:15+00:00, thread_id=019e415a-a08d-76a2-b08e-20215bbc772f)

### keywords

- input authority, INPUTCTX, syncMouseMode, MENU_TYPE_ROOM, pure-ImGui overlay, ownership flip

## Reusable knowledge

- Input authority is layer-aware: gameplay actions, menu actions, held actions, analog values, and legacy pad mirrors should respect the active input layer [Task 1][Task 2]
- No surface should consume or mirror input that belongs to a higher-priority menu, modal, text capture, or UI layer [Task 1][Task 2]
- The Combat Simulator room bug was an ownership-flip example: the room overlay was valid, but the watchdog kept releasing it and re-acquiring it every frame, which looked like input being pulled back toward center [Task 2]
- Keep build-verification workflow details in the queued-build memory rather than duplicating them here [Task 1][Task 2]

## Failures and how to do differently

- Do not diagnose layer-aware input bugs as binding problems first when the symptom suggests context/ownership churn [Task 2]
- Do not fix only the gameplay half or only the prompt/UI half of an input contract; both surfaces must agree [Task 1]

# Task Group: PD2 Controller-First Interaction Surfaces
scope: Project-wide interaction design with controller-first expectations across menus, UI, gameplay, and tooling surfaces.
applies_to: cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike; reuse_rule=reuse for future PD2 UI, gameplay, menus, social, modding, tools, and interaction-surface work in this checkout.

## Task 1: Close the Controller Support Cohorts lane with controller-first menu-graph behavior, success

### rollout_summary_files

- rollout_summaries/2026-05-19T19-58-49-VwGb-c036_controller_support_cohorts_menu_graph_complete.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\sessions\2026\05\19\rollout-2026-05-19T15-58-49-019e41d1-cf2a-7320-8405-7fbb17b7658f.jsonl, updated_at=2026-05-19T20:18:35+00:00, thread_id=019e41d1-cf2a-7320-8405-7fbb17b7658f)

### keywords

- Controller Support Cohorts, Controller First Class Citizen, controller-facing menu transitions, c036, s036-08

## Task 2: Preserve controller-first expectations in the rebuilt memory set, success

### rollout_summary_files

- rollout_summaries/2026-05-20T21-10-40-xs8l-pd2_memory_rebuild_and_asset_pipeline_correction.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\sessions\2026\05\20\rollout-2026-05-20T17-10-40-019e4739-f3f8-71a3-be12-7d536b0b67fd.jsonl, updated_at=2026-05-20T21:17:38+00:00, thread_id=019e4739-f3f8-71a3-be12-7d536b0b67fd)

### keywords

- controller-first, first-class citizen, interaction surfaces, rebuilt memory

## User preferences

- when the user says "Controller. It is a First Class Citizen in this game." -> validate the whole controller-facing flow and preserve controller usability across the surface, not only a code-local invariant [Task 1]
- when the user asks to complete a controller-support card, treat the whole lane as the deliverable and close the tracking/context surfaces too [Task 1]

## Reusable knowledge

- Controller is a first-class citizen across the whole project, and every interaction surface should be designed, implemented, and verified with controller usability in mind rather than added as an afterthought [Task 1][Task 2]
- Menu/UI work should respect the ImGui/menu-pool/input-context architecture unless a new architecture is explicitly chosen [Task 1][Task 2]
- Trim surface-specific social-shell details unless the current task actually needs them; keep the memory focused on the controller-first boundary [Task 2]
- Related skill: skills/pd2-card-closeout/SKILL.md [Task 1]

## Failures and how to do differently

- Do not stop at a static migration or isolated helper change when controller-facing transitions are still awkward or incomplete [Task 1]

# Task Group: PD2 Onboarding and Major Project Prompts
scope: No-code onboarding, roadmap synthesis, and paste-ready session-start prompts.
applies_to: cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike; reuse_rule=reuse for planning-only or onboarding requests in this checkout; do not treat it as implementation guidance unless the user changes mode.

## Task 1: Preserve the rebuilt no-code onboarding and prompt-generation rules during the 2026-05-20 memory rebuild, success

### rollout_summary_files

- rollout_summaries/2026-05-20T21-10-40-xs8l-pd2_memory_rebuild_and_asset_pipeline_correction.md (cwd=C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, rollout_path=C:\Users\mikeh\.codex\sessions\2026\05\20\rollout-2026-05-20T17-10-40-019e4739-f3f8-71a3-be12-7d536b0b67fd.jsonl, updated_at=2026-05-20T21:17:38+00:00, thread_id=019e4739-f3f8-71a3-be12-7d536b0b67fd)

### keywords

- onboarding, no-code, roadmap synthesis, paste-ready prompts, full-release-roadmap-2026-04-27

## Reusable knowledge

- When Mike asks for no-code onboarding, major project lists, roadmap synthesis, or paste-ready prompts, stay in planning/prompt-generation mode and do not implement [Task 1]
- Organize major project prompts around top-level project pillars rather than a narrow bug list [Task 1]
- Use `context/designs/full-release-roadmap-2026-04-27.md` as the steering source when it is current, and read the project context before making claims about live state [Task 1]

## Failures and how to do differently

- Do not quietly drift from a planning-only onboarding request into code changes just because the repo context is familiar [Task 1]
