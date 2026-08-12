# Unreleased Changes

> Running release-note source for the next GitHub release.
> Keep bullets short, player-facing where possible, and specific to what changed.

## Highlights

- Made real peers distribute, verify, admit, and play an editable Needler
  package, including its gameplay effect, sound, and world presentation.
- Kept listen-host and client stage identity exact through arena and random
  selections, with one concrete nonzero session shared by both peers.
- Made editable Needler effect colors flow unchanged from public source through
  gameplay/presentation staging into the production world renderer.
- Fixed Mod Manager archive sizing on Windows and added a real same-process
  disable/re-enable path for active custom weapon dependencies.
- Made recursively nested weapon effects publish their editable sound,
  material, and texture dependencies atomically before gameplay activation.
- Made keyboard aim actions steer the real gameplay aim axes and let custom
  weapons retain their selected primary or secondary function during play.
- Added an editable catalog-backed Needler impact sound and opt-in live effect
  transaction diagnostics for installed-client validation.
- Made custom weapon activation hydrate the real held-weapon adapter from
  public `.pdweapon` source and preserve its catalog runtime identity in matches.
- Emitted editable `.pdvoice` archives for direct mission MP3 references and
  aligned Needler creator effect fields with strict runtime conformance.
- Made generic public effect graphs drive real gameplay, catalog audio, screen
  and world presentation, timelines, targets, attachments, and cleanup through
  one rollback-safe transaction.
- Rejected unsupported or inert effect fields at load time instead of silently
  retaining creator settings the game would not use.
- Kept received mod files transactional through catalog admission, restoring
  the prior installed content when a typed asset is rejected after extraction.
- Kept live typed-effect replacement transactional across catalog invalidation,
  filesystem rollback, pending-owner reload, and manifest component checks.
- Balanced overlapping custom-weapon/effect/sound owners through release and
  Mod Manager disable, and fixed the Apply modal's ImGui style-stack imbalance.
- Added fail-closed typed scheduling for every accepted public v1 effect node,
  rejecting incomplete schedules or missing handlers before partial execution.
- Made asset disable and catalog reset transactional across every typed family,
  clearing stale runtime data and rebuilding catalog-ID caches before reuse.
- Made public `.pdeffect` explosion, spark, smoke, and typed SFX dependencies
  drive native gameplay tables, with missing selected sources failing closed.
- Removed the 64-asset ceiling from nested weapon UI, sound, animation, and
  effect ingestion while preserving all-or-nothing registration and rollback.
- Removed the separate 64-sound playback ceiling for creator content and
  restored ordinary custom-weapon allocation after the full base catalog.
- Made selected and nested public effects fail closed when missing, corrupt,
  disabled, wrong-type, incomplete, or conflicting instead of silently using
  a built-in explosion, spark, smoke, or sound.
- Made effect ownership and dependency teardown survive shared parents,
  repeated references, large recursive closures, disable/re-enable, and catalog
  reset without leaving stale effect programs or dependency edges.
- Made active effects and weapons retire immediately when a selected sound,
  material, texture, or nested effect is disabled or replaced, then reload only
  after the complete edited dependency closure passes again.
- Kept configured explosion sounds playable when their editable audio source
  is voice-backed, without allowing ordinary voice or music in effect slots.
- Preserved complete editable effect graphs, timelines, and native profile
  libraries at game load, with growable mod-effect and custom-spark capacity.
- Kept live keyboard/controller action hints inside themed menu panels across
  resolution, UI scale, wrapping, and custom fonts.
- Kept stage transitions from releasing mod assets owned by active themes,
  menus, editors, manifests, or other explicit typed-asset lifecycles.
- Released active theme UI, font, sound, and music references cleanly during
  normal game shutdown.
- Made saved custom themes activate after real catalog discovery, retain full
  nested font paths, and compile nested `.pdsong` sources at ordinary menu use.
- Made repeated theme startup activation idempotent and transactional across
  rejection, replacement, reapply, and shutdown.
- Made Theme Editor save real atomic self-contained `.pdtheme` archives (and
  embed them in `.pdmod`) with selected UI, vector font, sound, and music assets.
- Made theme backgrounds, chrome, inline effects, fonts, menu sounds, music,
  colors, and glyph styling update together from public `.pdtheme` source;
  broken declared content now rejects the theme instead of silently falling back.
- Made embedded theme UI, font, sound, and music archives typed catalog
  dependencies that reject the whole theme when invalid; removed the inert
  standalone effect-archive slot in favor of inline theme effects.
- Removed unsupported weapon material/grip binding files from `.pdweapon` v1;
  old declarations now fail clearly instead of appearing to work while ignored.
- Removed the unused duplicate `.pdmission` briefing payload so creator edits
  flow through the nested public scenario source the game actually loads.
- Preserved full nested `.pdui` archive-chain paths so embedded weapon reticles
  no longer fail merely because the installed game path exceeds 128 bytes.
- Replaced Audio Mods Voice loose-file output with a validated, atomic,
  self-contained localized `.pdvoice` creator that hot-registers through the
  production catalog path and retains keyboard/controller action glyphs.
- Fixed public `.pdvoice` subtitle JSON decoding for non-BMP Unicode while
  rejecting lone, reversed, or mismatched surrogate escapes.
- Made public `.pdtheme` files strict and authoritative, rejecting malformed,
  mismatched, duplicate, unknown, out-of-range, or raw-path theme source instead
  of silently substituting private metadata or built-in theme data.
- Made creator `.pdweapon` archives render embedded `.pdui` reticles in the
  production sight HUD, with broken or wrong-type declared sources rejected.
- Made localized `.pdvoice` audio and subtitle JSON drive production playback
  and dialogue text, including locale fallback and fail-closed declared source.
- Made public `.pdprop` behavior graphs execute on live Forge props instead of
  being preserved but ignored.
- Registered nested weapon animation and audio archives before production
  weapon parsing and carried them through local and network dependency paths.
- Made nested weapon media registration transactional, rejected unresolved
  animation sounds, and balanced direct plus manifest-driven load ownership.
- Made creator `.pdcharacter` body/head identity, names, and optional portraits
  drive the production character picker and lobby roster, with declared portrait
  failures refusing silent generated fallback.
- Made public `.pdvoice` actor, transcript, and language metadata feed live
  dialogue subtitles when the authored locale matches the active language.
- Pinned the local project Workbench to the canonical checkout, exposed its
  repository identity, and rejected linked-worktree or conflicting-port
  startups that could silently split roadmap truth.
- Replaced the legacy Kanban with a repo-local Workbench that keeps permanent
  typed IDs, audited atomic roadmap mutations, append-only feedback/activity,
  dependency/timeline/decision/asset/proof views, and a separate live
  coordination/queue hub.
- Fixed the end credits rendering dust motes and text as solid opaque squares, and fixed glow/gradient textures that rendered as opaque black boxes: extracted textures now decode with N64-accurate alpha and channel order (I4/I8 intensity-alpha, IA16 byte order, RGBA32 channel order). Existing installs re-extract affected textures and scenes once automatically.
- Fixed a bug that silently re-extracted all 87 level scenarios on every game launch (a binary-file probe always reported them stale). Warm boots now skip straight past extraction, cutting roughly 40 seconds off every launch after the first.
- Dev Window v2 and v3 now open the Workbench and treat its roadmap, notes,
  changelog, Codex memory mirror, and release notes as live project state.
- Git Pull in Dev Window v2 now fetches remote commits first and lets the operator choose the exact commit to fast-forward to.
- Release notes now come from this running list instead of the old dedicated-server placeholder text.
- Settings now has a simplified Input tab for profiles, connected devices, bindings, and tuning.
- Dev Window v2 Run Tests now avoids blocking Windows `lib*.dll` system error popups by staging the test runtime DLL and suppressing loader modal dialogs.
- Automated smoke runs now keep the game crash handler enabled, suppress native `PerfectDark.exe` application-error popups, and reap smoke-owned `PerfectDark` / `WerFault` leftovers so failed verification runs report through logs instead of blocking the desktop.
- Automated smoke runs now exit cleanly after scripted shutdown instead of reporting a Windows heap-corruption exit from video display-mode cleanup.
- Smoke firewall setup now reports non-admin permission failures as a controlled warning instead of a raw PowerShell error after successful tests.

## Added

- Hardened `.pdweapon` source activation so invalid graphs, incomplete loose
  sources, unknown settings, and unsupported presentation fields reject instead
  of remaining enabled or being silently ignored.
- Hardened the public asset pipeline so extractor-only startup reports every
  failed family and exits nonzero, archive updates are atomic and preserve
  untouched members/comments, descriptor editing works through catalog/VFS
  paths, and oversized public INI descriptors fail loudly instead of silently
  dropping fields.
- Made selected public game-mode, bot-profile, theme, texture, font, animation,
  sequence, SFX, voice, and MP3 sources fail closed when their catalog binding
  or authored source is missing, removing residual ROM, loose-file, native, and
  built-in runtime fallback paths.
- Restored creator-facing OBJ source fixtures that the blanket object-file
  ignore rule had silently excluded, and strengthened the native-source guard
  so debug-gated or direct runtime fallback paths cannot satisfy the public
  archive contract.
- Preserved custom `.pdbotprofile` catalog identity through both multiplayer
  menu systems, match/runtime state, scenario and setup saves, manifests, and
  network match start; older binary setups migrate to the new profile-aware
  format and mixed v51/v52 peers are rejected.
- Made multiplayer setup saves catalog-native and fail-loud: JSON no longer
  writes a numeric weapon mirror that can overwrite creator weapon IDs, and
  binary setup files reject partial reads/writes, unsupported versions, and
  invalid setup indices.
- Extracted character animations now preserve root-motion (translation + facing) and cutscene camera channels in the public `.pdanim`. 439 root-motion and 98 camera animations that previously survived only in the private runtime cache now round-trip losslessly, so modded and extracted anims keep their movement and camera data.
- Custom weapon behavior authored in `.pdweapon`/`.pdprojectile`/`.pdentity` graphs now actually drives gameplay (behind the developer graph-runtime toggle): homing steering gains, fly-by-wire tuning, trajectory clamps, wall-hugger and sticky behavior, bounce tuning, fuse timers, impact filters/sounds/sparks/explosions, smoke trails, carrier-to-turret transitions, proxy/remote/timed mine policies with detonator pairing, deployed-autogun cadence and muzzle behavior, owner-death cleanup, pickup/recover rules, weapon settings/variables with `$name` substitution, and an x-ray camera effect. Base-game behavior is bit-identical with the toggle off (and on, for base weapons).
- Custom visual effects now have a runtime: `.pdeffect` graphs compile and drive the existing explosion/spark/smoke machinery, including custom-tinted spark types (the Needler's pink burst), with effects nested inside weapon archives now correctly discovered.
- Weapon graph runtime is now on by default; the old debug toggle and transient MP option are retired, and mixed v50/v51 network builds are refused at connection time.
- Custom `.pdweapon` archives can now render installed first-person model source directly from nested `.pdmesh` dependencies inside `.pdmod` packages, proven by the Needler visual smoke.
- Fixed three latent bugs found by static analysis: base Timed Mines would have detonated instantly with the graph runtime enabled; weapon archive shared-context/settings/variables files were silently ignored; and a freed target could leave homing projectiles pointing at stale memory.
- Bot profiles, game modes, and UI themes now read their authored archive data through the catalog runtime (with loud fallback to built-ins), the first of the meta asset families to do so.
- Boot now verifies every texture archive binding with a cheap stat pass and reports missing archives loudly instead of crashing at first use; the texture filename slug logic is unified so the emitter and catalog can never drift apart.
- Cleaned the Kanban Active lane so the current asset-parity completion path is the only active critical work, with unrelated cards deferred or marked done.
- Refreshed the context system so stale audit files are archived out of the hot folder and preserved non-`c3844` card workspaces are explicitly historical or deferred.
- Strengthened the asset-archive conformance check so numeric or legacy asset references (for example `model_catalog_id = 42` or a `MODEL_*` symbol) in JSON catalog-ID fields are now rejected like they already were in delimited tables, closing a gap in the primary public source format. Intra-archive member and dependency paths stay allowed.
- Added a first-class `probe` build target and a repeatable scenario-scene CPU sweep so the source scene.glb path can be validated for every level without launching the game, GPU, or audio. The full extracted scenario set passes 87/87.
- Catalog asset misses are now surfaced at a stage-load health checkpoint instead of being silently tolerated after a one-time log, and hard-fail under source-only enforcement so a missing extracted asset cannot quietly fall back to a default.
- Custom body/head archives now have the catalog-owned private-slot foundation and live render proof. Direct body/head registration assigns private runtime slots, `mod.json` rows no longer overwrite them, and the custom body/head smoke now proves `example:tri_body` reaches `MODASSET.RENDER` from public source.
- Removed legacy numeric bridge fields from mod authoring templates and external scan/distribution paths so custom assets use catalog IDs and source files publicly, with integer slots kept as private catalog-owned runtime bridges.
- Removed private bridge/provenance fields from generated public texture, language, and MP3 voice descriptors; retained archives keep needed provenance under `_meta` and strict conformance now rejects those fields in public INI files.
- Removed private mesh file-symbol provenance from generated public `.pdmesh::mesh.ini`; retained archives keep it under `_meta`, and strict conformance now rejects that bridge field in public mesh descriptors.
- Fixed menu preview bridge fallbacks so MP head and weapon previews clear with a catalog warning instead of loading raw legacy file numbers when provider-backed source resolution fails.
- Tightened `.pdmod` packaging and loading so public `.tsv` payloads are rejected like `.bin` payloads, including direct installed archives and Modding Hub pack output.
- Replaced the active Modding Hub pack/import workflow with strict `.pdmod` import/install validation, sharing the same `.bin`, public `.tsv`, and nested typed-archive checks used by received Public Mods.
- Fixed received Public Mods delivery so inbox `.pdmod` downloads validate, install, enable, and load through the same strict archive/runtime path as local installed mods.
- Fixed installed `.pdmod` source-only workflow proof so custom typed geometry, texture/material, animation, audio, scenario/map, mission, and weapon assets load or register through catalog/provider paths without public TSV/bin, numeric public IDs, or ROM fallback.
- Completed the c3844 live asset-source audit across Scenario geometry/textures, weapons/hands, custom body/head, props/vehicles/effects/material/UI/font/lang/theme, audio, and animation smoke paths with scoped logging and no runtime fallback signatures.
- Fixed clean source-only audit startup so `.pdui` can resolve `.pdtexture` sources, base catalog probes wait for emitted archives, and generated private caches create parent folders with shorter Windows-safe names.
- Fixed the final B-801 source-only readiness smokes so audio and animation probes wait for the right boot/catalog state, prove source-backed loads, and exit cleanly through the smoke harness.
- Closed c3844 at 100% after the final all-family regression sweep passed source guards, retained asset conformance, CPU audio/mesh/animation validators, focused Public Mods and `.pdmod` tests, live smoke matrix proof, and an isolated all-target build.
- Fixed source-backed `.pdsong` runtime compilation for extracted music sequences whose loops close at track end, preventing music fallback to legacy ROM sequences.
- Fixed the custom head private-slot handoff so slot 152 no longer narrows through signed 8-bit `chrdata.headnum`; live render audit now preserves `head=152` through `chrRender-modelRender`.
- Fixed MP manifest ownership for the custom-body live proof so the old stage diff no longer unloads manifest-owned custom body/head/weapon assets during base stage load, and body/head manager cached generated modeldefs are cleared before catalog unload.
- Fixed sparse custom body/head loader-pool slots so unparsed base rows still fall back to authored base body/head data instead of looking like empty registered records.
- Fixed typed body/head examples and walkers so nested `.pdmesh` archives carry material, hierarchy, render, face, and part sidecars and resolve OBJ, GLTF, or GLB model source.
- Fixed hierarchy-generated body modeldefs so explicit wildcard mesh groups render GLTF triangles, body sources get character skeleton/root defaults, and generated-source body recognition no longer depends on zero parts.
- Fixed a generated custom-body render crash by preparing character model matrices on demand before `modelRender` when a debug-placed/source-backed bot reaches render before the normal matrix allocation path.
- Fixed the host "Start Match" button: when you host a game in the client (listen host), Combat Simulator, Campaign co-op, and Counter-Operative matches now actually start. Previously only a remote client could trigger the start.
- Dev mods now ship from a git-tracked `dev-mods/` folder that is copied into the game's `mods/` on every build, so they survive clean builds, and builds/releases can include or exclude specific mods.
- Fixed source-backed menu model previews so custom provider handles avoid the character-preview sentinel and weapon previews receive visual model source instead of split graph source.
- Fixed unresolved character body source so it no longer renders as the wrong built-in body.
- Fixed catalog-backed body/head selector identity so custom rows beyond the old base body/head counts keep their catalog IDs through selection, default-head, bot, and config paths.
- Fixed custom body/head manager lifetime so mod-scanned `.pdbody` and `.pdhead` slots survive boot initialization through match setup and bot allocation.
- Fixed body/head archive walking so user-authored meshes without private runtime slots no longer masquerade as built-in slot zero.
- Added a CPU-only audio source verifier for `.pdsfx`, `.pdvoice`, and `.pdsong` archives so timing metadata, MP3/Vorbis headers, sequence events, loops, and pitch buckets can be checked before live audio tests.
- Added a CPU-only mesh source verifier for `.pdmesh` archives so OBJ/GLTF/GLB geometry, integer-native quantization, hierarchy JSON, render-command coverage, and declared counts can be checked before live render tests.
- Added a Dev Window v2 Assets tab and `devtools/pdxxx-asset-tool.ps1` so developers can crawl typed `.pdxxx` data archives, filter by type, extract selected assets to ignored `.pdxxx-dev-extracts/`, and open the output for Blender visibility checks without converting the stored archive data.
- Added an all-family typed asset workflow verifier that packages the modder example set as `.pdmod`, unpacks it, and proves editable `.pdxxx` source archives round-trip unchanged.
- Fixed the production `.pdmod` packer so the all-family typed source example packages through the same shared path as Modding Hub and sharing, including scenario-backed arenas and MIDI/source-backed music archives.
- Fixed the Dev Window v2 Assets tab so `.pdxxx` rows bind as individual assets instead of `System.Object[]`, and moved archive crawling off the WPF UI thread to avoid hard freezes while changing filters.
- Added CPU/static coverage proving custom body/head private slots stay source-backed through walker parsing, catalog lookup, public archive model conversion, and character assembly before live render tests.
- Smoke tests can now use named log-channel masks, and the high-risk asset parity smokes use scoped logging instead of enabling every log channel.
- Smoke tests that request a queued isolated build now install the freshly built session client instead of accidentally falling back to a stale shared build output.
- Added a bounded live source-only audio smoke proving representative SFX, voice, and sequence music start from public typed archives without ROM fallback.
- Added active-stage Scenario graph proof so `lvTick` asserts public `level.graph.json` global settings before logging the level tick source.
- Fixed Scenario AI list extraction so every declared AI opcode gets a semantic `opcode_name` instead of collapsing to generic `"command"`, and strict conformance now rejects stale flattened Scenario AI rows.
- Added an extractor-only boot mode that runs catalog extraction, archive walking, and runtime-cache generation, skips network/audio/window/UI/input startup, and exits before gameplay so stale archives can be regenerated safely without opening an OpenGL window.
- Fixed Scenario archive regeneration currentness so rebuilt scenarios no longer flatten AI opcode names and the generated Scenario tree passes conformance.
- Fixed Scenario level graphs so every AI command row now has a matching `scenario.ai.command` graph node and link from `scenario.ai.lists`; regenerated base Scenario archives now validate 75,920 command nodes and links.
- Fixed Scenario normal-play stage loading so setup, pads, tiles, background geometry, and level graph activation fail closed after public `.pdscenario` source failure instead of falling back to legacy ROM payloads.
- Fixed extractor-only UI archive regeneration so `.pdui` texture decode can find public `.pdtexture::texture.png` source before the normal catalog reverse index exists.
- Refreshed retained generated assets so the full `Build\data\ntsc-final` archive tree now passes strict all-family conformance.
- Raised the config registry capacity so current boot settings no longer hit the old 512-entry cap during extraction-only startup.
- Fixed retained all-family/archive-walker source-gate smokes so they request the current extracted font archive and wait long enough for clean extraction before checking catalog source activation.
- Fixed asset fallback telemetry so first-launch extraction bootstrap reads are not counted as runtime ROM fallback after extraction.
- Added clearer generated-cache failure diagnostics for mod asset compilation, including expanded cache paths and OS error codes.
- Extended high-risk source-gate smoke dwell windows so cold extraction has longer to finish before proof assertions run.
- Fixed focused pd-tests linkage for weapon-graph archive VFS fallback coverage with an inert test `fsFileLoad` stub.
- Fixed Forge prop, door, and weapon-pad model spawns so source-backed visual models use catalog/provider handles and `.pdmesh` inner model source instead of requiring positive legacy file numbers.
- Fixed Training and Hangar model previews so source-backed weapon/vehicle rows use catalog/provider handles before legacy file-number fallback.
- Fixed Map Import so it stages editable map source layouts or typed map archives instead of accepting native `.bg`/`.bin` map dumps.
- Fixed source-backed weapon, vehicle, and prop previews so custom catalog rows no longer require a legacy ROM file number.
- Fixed the Modding Hub model scale tool so it reads public `.pdmesh::mesh.ini` source metadata instead of editing native model bytes.
- Fixed walked prop archives so behavior graphs cannot replace prop/model source as the selected provider handle.
- Fixed base and walked vehicle archives so `physics.json` cannot replace model or behavior source as the selected provider handle.
- Fixed theme archive activation so UI/font/audio/music/effect dependencies cannot replace the required `theme.json` source.
- Fixed material archive activation so texture/effect dependencies cannot replace the required `material.json` source.
- Fixed skin archive activation so material/texture dependencies cannot replace required skin source such as `skin.json`, texture, or swatches data.
- Fixed HUD archive activation so `texture_file` cannot replace the required `layout.json` source.
- Fixed UI archive activation so layout and nine-slice metadata cannot replace the required texture source.
- Fixed font and language archive activation so primary paths, font metrics, or bankless strings cannot replace required authored source.
- Fixed mission archive activation so Scenario/objective/briefing members cannot replace the required complete mission source bundle.
- Fixed projectile/entity archive activation so model files cannot replace required behavior graph source.
- Fixed character/body/head archive activation so portraits, hand meshes, legacy model paths, or selected primary paths cannot replace required body/mesh source.
- Fixed weapon archive activation so model files, selected primary paths, legacy single graphs, or partial graph settings cannot replace required split held source.
- Fixed prop archive activation so behavior graphs or selected primary paths cannot replace required prop/model source.
- Fixed Scenario archive activation so selected primary paths or partial source bundles cannot replace required scene, setup, navigation, and level graph source.
- Fixed Arena archive activation so legacy geometry paths or selected primary paths cannot replace the required Scenario archive source.
- Fixed checked-in typed material examples so `.pdmaterial` archives and nested copies carry editable `material.json` source.
- Fixed runtime bindings so arena, body, head, prop, weapon, projectile, and entity archives keep display/name and unlock metadata after activation.
- Added a shared no-public-TSV release gate so typed archives reject TSV payloads and require semantic JSON/INI/graph or standard editable source files instead.
- Tightened typed archive conformance so public manifests and text metadata reject stale TSV references, not only TSV archive members.
- Tightened the native-source guard so TSV no longer counts as a public text-source suffix; old TSV member names are retained only as stale-archive rejection fingerprints.
- Removed dead Scenario TSV diagnostic ledgers from extraction and updated the Chicago source smoke so runtime proof requires JSON Scenario and mission source paths.
- Fixed source-built Scenario room-list validation so a full valid room list with its terminator in the reserved final slot no longer logs as malformed during Chicago source-scene gameplay.
- Fixed Scenario source-scene texture alpha so public `scene.glb` materials mark masked textures and the native source renderer no longer draws transparent texels as solid dark pixels.
- Fixed Scenario source-scene camera delivery so public `scene.glb` maps still render when the VI camera FOV is temporarily zero during stage startup/cutscene state.
- Added Scenario `scene.glb` material extras so public map geometry preserves texture-command, sampler, and secondary-texture metadata instead of flattening materials to one base texture.
- Added a non-visual Scenario `scene.glb` verifier that reports material-extras and secondary-texture coverage without launching the game.
- Added a CPU-only Scenario source-renderer probe so `scene.glb` build failures can be separated from live camera/GPU render failures without launching the game.
- Fixed Scenario source-scene material parsing so extracted levels build renderer CPU data quickly instead of stalling on real material/texture tables.
- Fixed Scenario source-scene rendering so valid `scene.glb` maps draw against the active game framebuffer dimensions and do not leak viewport state into later display-list rendering.
- Fixed Scenario source-scene material sampling so DCC UVs, runtime-repeat UVs, and secondary texture UV bindings are preserved instead of flattening all texture layers onto one coordinate set.
- Fixed Scenario source-scene shader diagnostics so compile/link failures log the exact renderer failure instead of looking like bad extracted geometry.
- Tightened Scenario `scene.glb` validation so material extras must include native integer tile/LOD fields and correct secondary runtime-UV bindings.
- Fixed configured SFX speech alias extraction so MP3-backed voice lines no longer fail `.pdsfx` extraction as out-of-bank sounds.
- Added typed `.pdvoice::sample.mp3` source archives for MP3-backed speech aliases so playback and duration no longer depend on loose extracted binary files.
- Fixed MP3-backed configured speech playback so voice aliases use the correct audio config row for volume, pan, filtering, and response behavior.
- Fixed MP3-backed `.pdvoice` playback so public `sample.mp3` voice archives use the source-backed SFX/voice path instead of failing WAV-only playback.
- Added offline repair tools for stale retained generated `.pdanim`, metadata, mission, weapon, projectile, and entity archives whose public source already matches the current contract.
- Fixed source-backed SFX/voice playback so native key-volume table changes affect public WAV handles instead of being flattened by the file mixer.
- Fixed source-backed SFX/voice playback so later pan and volume changes preserve native sample pan and sample volume metadata.
- Fixed source-backed SFX/voice playback so native FX mix and FX bus state drive a bounded audible send/return path for public WAV handles.
- Fixed source-backed `.pdsong` playback so public stream and sequence sources are tried before legacy sequence ROM-address validation.
- Fixed source-only `.pdsong` resolution so valid public song sources are not misclassified as missing source, while failed public compile/stream still refuses ROM fallback.
- Fixed source-only `.pdtexture` loading so non-image public source paths cannot fall back into the legacy compressed texture loader.
- Fixed source-only `.pdanim` loading so failed or non-GLTF public animation sources cannot fall back to native animation bytes.
- Fixed OGG-backed source audio decoding so stereo OGG files keep their full decoded sample length instead of truncating during runtime conversion.
- Recorded the current custom `.pdsong` integer-slot limitation so fully user-created songs stay tracked until runtime selection can use catalog IDs or catalog-owned music slots.
- Fixed custom sequence-backed `.pdsong` selection so catalog-ID songs can use private virtual sequence slots without exposing integer slots to modders.
- Fixed Scenario path archive loading so sparse or duplicate native path IDs are preserved instead of forcing `navigation/paths.json` rows to match row numbers.
- Fixed `.pdmesh` extraction so real model archives with unresolved native part-table links are preserved instead of skipped.
- Fixed `.pdmesh` hierarchy rebuilds so unresolved native part rows are preserved as editable metadata without breaking generated modeldef construction.
- Fixed editable `.pdmesh` archives so manifest metadata points at the actual GLTF/OBJ model source before registration.
- Recorded the current mesh/runtime integer-only geometry boundary so extracted and imported meshes preserve or quantize into native fixed-point semantics instead of float-only visual geometry.
- Tightened `.pdmesh` import validation so custom geometry rejects coordinates/UVs that cannot quantize into the current native integer mesh domain.
- Tightened `.pdmesh` GLTF/GLB archive validation so custom mesh sources fail before runtime if they cannot quantize into the native integer mesh domain.
- Fixed custom `.pdweapon` private MP slots so spawn defaults derive ammo/model data from parsed weapon source instead of empty slot rows.
- Fixed custom `.pdweapon` private slots so removed mod weapons no longer leave stale runtime/MP slot reservations after a catalog rebuild.
- Fixed model-less base `.pdweapon` archives so they no longer advertise a missing held mesh source member.
- Fixed the checked-in custom `.pdweapon` example so its graph source compiles and registers through the held weapon runtime.
- Tightened Scenario and Weapon typed archive manifests so loader-facing source keys and optional weapon dependencies stay visible to manifest-driven tools.
- Tightened direct `.pdprop` and `.pdvehicle` model-source validation so OBJ/GLTF/GLB sources must match the same native integer mesh boundary as `.pdmesh` before runtime load.
- Fixed another source-built mesh usage path so generated modeldefs use direct display-list pointers during character/object hit tests instead of being treated like segmented ROM model bytes.
- Fixed source-backed body/head activation so embedded `.pdmesh` archives compile their public model source instead of being handed to native model loading as archive bytes.
- Fixed source-backed body hand activation so `.pdbody::hand.pdmesh` feeds first-person hand model loading through the catalog instead of remaining archive-only source data.
- Fixed source-backed `.pdanim` rebuilds so authored glTF timing and `STEP`/`LINEAR` sampler behavior are preserved when clips are converted back into runtime animation frames.
- Tightened `.pdanim` validation so glTF sampler interpolation and accessor types must match the runtime animation compiler.
- Fixed editable `.pdanim` archives so manifest metadata names the actual GLTF/command source consumed by animation registration.
- Fixed local and network `.pdanim` command animations so `commands.json` is parsed into the weapon animation pool instead of only being registered as a file path.
- Fixed character `.pdanim` extraction currentness so stale GLTF payloads cannot be skipped just because manifest metadata looks current.
- Fixed network-distributed typed archives so shared `.pdanim` and `.pdtexture` assets keep the same animation metadata and source texture override data as locally scanned archives.
- Fixed network-distributed typed archives so shared assets keep the same dependency closure as locally scanned archives.
- Fixed network-distributed model and legacy audio descriptors so shared `.pdmesh`/model rows and `audio.ini` metadata match locally scanned archives.
- Fixed network-distributed map and Scenario descriptors so readable mode strings like `mp|solo` match locally scanned archives.
- Fixed network-distributed weapon descriptors so shared `.pdweapon` rows preserve the same existing weapon defaults as locally scanned archives.
- Fixed network distribution for large custom typed-archive packs so catalog advertisement, missing-asset reports, and transfer queues no longer truncate at old fixed integer caps.
- Fixed runtime bindings so large typed archive sets no longer drop source-backed runtime visibility after the old 256-entry cap.
- Fixed catalog dependency graph coverage so large typed archive dependency closure is tested past the old 256-entry initial allocation.
- Recorded the all-family integer-only runtime limitation so remaining numeric slots stay private bridge/cache details instead of becoming modder-authored archive fields.
- Fixed gamemode and bot-profile runtime bindings so selector metadata stays available by name after archive activation instead of flattening into generic fields.
- Fixed effect runtime activation so shader metadata cannot stand in for missing authored effect graph or timeline source.
- Fixed effect archive activation so an outer archive path cannot replace the required effect graph or timeline source.
- Fixed vehicle archive activation so `physics.json` cannot stand in for model/behavior source and model/behavior source cannot load without required physics data.
- Fixed gamemode and bot-profile activation so outer archive paths cannot replace required `rules.json` or `profile.json` source.
- Fixed metadata archive source walking so vehicle, mission, HUD, material, and theme dependency members stay distinct after boot-time registration.
- Fixed extracted `.pdmission` archives so mission graph, objectives, briefing, and embedded Scenario source members are all declared in manifest metadata and survive boot-time registration.
- Fixed editable `.pdhud` archives so HUD texture and layout source members are declared in manifest metadata before boot-time registration.
- Fixed editable `.pdtexture` archives so texture image source members are declared in manifest metadata before registration.
- Fixed runtime adapter bindings so skin, HUD, and mission archives keep distinct authored and dependency files after activation.
- Fixed Scenario runtime bindings so public scene, collision, setup, navigation, objective, and graph source members stay visible after activation.
- Fixed Scenario portal source propagation so `portals.json` remains an explicit catalog/runtime member after scanning, sharing, and activation.
- Tightened WAV-backed `.pdsfx` and `.pdvoice` archives so keymap, loop, envelope, pan, and volume metadata cannot be omitted from editable audio source.
- Tightened WAV-backed `.pdsfx` and `.pdvoice` validation so metadata must match the actual PCM16 mono `sample.wav` header.
- Fixed sequence-backed `.pdsong` archives so `sequence.mid` and `sequence.json` bind as public music source during local scan and network delivery.
- Fixed bitmap `.pdfont` archives so glyph atlases and metrics JSON survive scanning, sharing, boot walking, and runtime activation together.
- Fixed `.pdlang` archives so extractor-style `source_bank`, locale, category, string count, and `strings.json` survive scanning, sharing, and boot walking together.
- Fixed `.pdlang` runtime activation so public strings source and bank metadata remain visible through the runtime binding layer.
- Fixed `.pdui` archives so layout and nine-slice metadata survive scanning, sharing, boot walking, and runtime activation with the texture source.
- Fixed `.pdprop` archives so behavior graph source survives scanning, sharing, boot walking, and runtime activation with the prop model source.
- Fixed `.pdprojectile` and `.pdentity` archives so graph, binding, and composition source members are declared in manifest metadata before runtime registration.
- Fixed `.pdmaterial` examples and validation so embedded material effect archives stay declared as public source dependencies.
- Fixed `.pdeffect` archives so timeline source survives scanning, sharing, boot walking, and runtime activation with the effect graph source.
- Tightened `.pdanim` archive conformance so stale generated character animation GLTF and malformed weapon command sources fail the normal all-family archive gate.
- Fixed Scenario navigation source propagation so waypoint, waygroup, cover, and path JSON members remain explicit catalog/runtime members after scanning, sharing, and activation.
- Fixed Scenario folder-to-`.pdmod` templates so new user-created Scenario folders start with semantic JSON sidecars instead of TSV-shaped `pads.json` and include the required portal, setup, AI, and navigation source members.
- Fixed `.pdskin` archives so editable swatch source survives scanning, sharing, boot walking, and runtime activation.
- Fixed `.pdskin` archives so typed material and texture dependency archives survive scanning, sharing, boot walking, and runtime activation.
- Fixed `.pdtheme` archives so UI, font, audio, music, and effect dependency archives survive scanning, sharing, boot walking, packaging, and runtime activation.
- Fixed `.pdcharacter` archives so body, head, and portrait source members survive scanning, sharing, packaging, boot walking, and runtime activation.
- Fixed `.pdbody` and `.pdhead` archives so body mesh, hand mesh, and head mesh source members survive scanning, sharing, packaging, boot walking, and runtime activation.
- Fixed `.pdarena` archives so embedded Scenario source and catalog links survive scanning, sharing, boot walking, and runtime activation.
- Fixed `.pdgamemode` and `.pdbotprofile` archives so rules/profile metadata and bot target/tuning fields survive scanning, sharing, boot walking, and runtime activation.
- Fixed base metadata and weapon archive regeneration checks so stale `.pdgamemode`, `.pdbotprofile`, `.pdmission`, and `.pdweapon` archives no longer survive after source-manifest contract changes.
- Fixed Mod Manager component-state persistence so disabled custom arenas, bodies, heads, meshes, and animations stay disabled after restart.
- Fixed direct typed-archive scanning so strict source members such as Scenario links, mesh archives, weapon graph files, UI layout/nine-slice files, and theme dependencies resolve as archive-member paths instead of loose relative files.
- Fixed `.pdweapon` catalog lifecycle activation so archive-backed held weapon graphs compile and release through the held-weapon runtime instead of being skipped by the projectile/entity graph path.
- Fixed `.pdweapon` manifests so base weapon walking preserves model, graph, settings, variables, and shared-context source members instead of flattening them to the outer archive.
- Fixed `.pdweapon` source propagation so held graph members survive local scan, network delivery, base walking, runtime bindings, and loose-source activation instead of only working inside archive compiler reads.
- Fixed `.pdweapon` runtime bindings so model source and existing weapon defaults stay visible after held graph activation.
- Added catalog-owned private custom weapon slots so fully custom `.pdweapon` archives can enter match selection, spawn manifests, and held-weapon graph registration without exposing numeric weapon IDs to modders.
- Recorded remaining custom weapon base-equivalence work around authored gameplay defaults and direct catalog-ID weapon consumers after the private-slot bridge.
- Fixed `.pdprojectile` and `.pdentity` runtime bindings so behavior graph, model source, transition target, and archetype metadata stay visible after graph activation.
- Fixed extracted audio archive walking so SFX, voice, and music durations survive public-source registration instead of being reset to unknown.
- Fixed source-backed SFX and voice repitching so keymap base pitch stays applied after gameplay pitch changes.
- Fixed network catalog-info advertisement so custom arenas, bodies, heads, meshes, animations, and tools are visible for distribution.
- Fixed mod pack export/import and authoring UI lists so custom arenas, bodies, heads, meshes, and animations are manageable as first-class assets.
- Fixed `.pdcharacter` source walking so body and head archive members stay distinct after boot-time public-source registration.
- Fixed clean-boot UI archive loading so base `.pdui` textures emit, register, and reload from public source after `texReset()` instead of being first repaired by the render loop.
- Fixed `.pdui` catalog source retention so UI textures loaded by the theme also resolve through typed source-only catalog loads.
- Added native envelope/release metadata for `.pdsfx` and `.pdvoice` so source-backed WAV sounds preserve readable attack, decay, release, and key velocity fields.
- Removed stale TSV source handling from the Modding Hub weapon template path and Scenario source matrix proof so weapon bindings and Scenario runtime assertions now use the semantic JSON archive members.
- Added semantic Scenario AI-list source with `.pdscenario::ai/ailists.json`, replacing the public `ai/ailists.tsv` command table in extraction, runtime AI-list compilation, generated-nav fixtures, examples, validation, and source-only Scenario graph proof.
- Added semantic Scenario setup-field source with `.pdscenario::setup.fields.json`, replacing the public `setup.fields.tsv` table in extraction, runtime setup compilation, setup-link proof, examples, validation, and source-only Scenario graph proof.
- Added semantic Scenario object source with `.pdscenario::objects.json`, replacing the public `objects.tsv` table in extraction, runtime setup object loading, examples, validation, and source-only Scenario graph proof.
- Added semantic Scenario portal source with `.pdscenario::portals.json`, replacing the public `portals.tsv` table in extraction, runtime portal-table compilation, generated-nav hashes, examples, validation, and source-only Scenario graph proof.
- Added semantic Scenario objective source with `.pdscenario::objectives.json`, replacing the public `objectives.tsv` table in extraction, mission provenance, templates, examples, validation, and source-matrix summaries.
- Added semantic Scenario spawn source with `.pdscenario::spawns.json`, replacing the public `spawns.tsv` table in extraction, templates, examples, validation, and runtime setup compilation.
- Added semantic Scenario trigger-volume source with `.pdscenario::volumes.json`, replacing the public `volumes.tsv` table in extraction, templates, examples, validation, generated-nav hashes, and runtime graph activation.
- Added semantic Scenario pad source with `.pdscenario::pads.json`, replacing the public `pads.tsv` table in extraction, runtime padfile compilation, generated-nav hashes, examples, validation, and source-only Scenario graph proof.
- Added semantic Scenario navigation path source with `.pdscenario::navigation/paths.json`, replacing the public `navigation/paths.tsv` table in extraction, generated-nav metadata, examples, validation, source-matrix proof, and runtime path compilation.
- Added semantic Scenario navigation table source with `.pdscenario::navigation/waypoints.json`, `navigation/waygroups.json`, and `navigation/covers.json`, replacing public waypoint/waygroup/cover TSV tables in extraction, runtime padfile compilation, generated-nav metadata, examples, validation, and source-matrix proof.
- Added JSON-only source contracts for shared archive hash metadata, `.pdskin` swatches, `.pdvoice` subtitles, and `.pdmission` objective/briefing source so those public typed archives no longer rely on TSV sidecars.
- Added one-install source matrix verification so Scenario and all-family asset checks can reuse a frozen extracted content set when extraction itself did not change.
- Added file-backed non-Scenario asset matrix loading so thousands of source-only catalog IDs are read from a generated `type=id` list instead of the command line.
- Completed the c3844 asset-pipeline runtime source migration sweep, closing the remaining Scenario vehicle path, route-to-target, cover/navigation, optional actor/object/path, MP participant, and setup-record source-proof gaps found by the final matrices.
- Added source-native `.pdsong` sequence playback by proving public `sequence.mid` plus semantic `sequence.json` before compiling editable sequence source for the legacy sequencer.
- Added semantic `.pdmesh` hierarchy, part, and face JSON source files so extracted mesh archives no longer expose public TSV sidecars for model reconstruction.
- Added a stricter `.pdui` source contract so optional UI layout data uses semantic `layout.json` instead of public TSV tables.
- Added source-native `.pdtexture` image loading so source-built meshes decode public texture archives into runtime RGBA32 texture data instead of falling back to ROM/static compressed texture bytes.
- Added Scenario `scene.glb` room-shading payloads so public map geometry carries `COLOR_0` alongside authoring/runtime UVs and renders with archived room shade data instead of flat texture-only output.
- Added semantic GLTF extraction for character/cutscene `.pdanim` archives so public animation source uses editable transform channels and repeat/cut-skip metadata instead of header/frame byte tables.
- Fixed zero-frame `.pdanim` extraction so reserved native animation entries become valid no-op GLTF clips instead of invalid zero-sample channels.
- Improved `.pdanim` source verification so stale generated animation archives are grouped and capped instead of flooding full-tree audit output.
- Hardened Scenario animation actions so live character, camera, object, preset, natural, and special-death animation use requires compiled public `.pdanim` source clips instead of catalog names alone.
- Hardened weapon/inventory `.pdanim` command playback so the loader consumes public `commands.json` archive members directly and the gun runtime logs the same source script when starting weapon animations.
- Preserved generated mesh relation nodes during extraction so distance, reorder, and headspot model hierarchy rows are no longer flattened into bogus position nodes.
- Added a catalog-provider guard so body/head model catalog validation uses handle-based source IDs instead of raw source-filenum reverse lookup.
- Added source-native MP3 speech playback and duration sizing from extracted public source files before ROM/static fallback.
- Added source-native `.pdsong` track-audio playback for public `track.wav`, `track.ogg`, and `track.mp3` sources before legacy sequence fallback.
- Added a native-source guard so file-source SFX/voice playback failures refuse ROM/static fallback in source-only audio verification.
- Added native SFX/voice keymap extraction so public `.pdsfx` and `.pdvoice` WAV playback preserves original base pitch, sample volume, and pan.
- Added live file-backed SFX/voice handles so public `.pdsfx` and `.pdvoice` WAV playback preserves loop points and can be stopped, repanned, and repitched by gameplay code.
- Preserved native SFX/voice fxmix and fxbus state on source-backed `.pdsfx` and `.pdvoice` handles.
- Fixed custom `.pdmesh` OBJ material imports so standard `map_Kd` texture refs resolve through typed `.pdtexture` catalog dependencies.
- Added a native-source guard so MP3 prop-sound duration estimates refuse ROM/static file-size fallback in source-only audio verification.
- Added a native-source guard so MP3 speech playback refuses ROM/static file playback in source-only audio verification.
- Added archive-backed configured SFX aliases and catalog-form weapon animation command refs so `.pdanim` command source no longer depends on local animation names or runtime-only sound aliases.
- Added configured SFX alias resolution for weapon command audio and converted `.pdweapon` binding sidecars from public TSV to JSON.
- Added a native-source guard so sequenced music refuses ROM/static fallback in source-only audio verification until public `.pdsong` runtime playback is source-native.
- Added a native-source guard so body/head manager modeldef fallbacks refuse non-public family handles in source-only verification.
- Expanded the generated-nav Scenario smoke to prove live quadrant AI action/condition execution from public `ai/ailists.json`, `pads.json`, and generated navigation tables.
- Added a native-source guard so setup modeldef loads refuse non-public model handles in source-only verification.
- Added a native-source guard so Forge prop, door, and weapon-pad model spawns refuse non-public family handles in source-only verification.
- Added a native-source guard so bundled metadata preloads refuse activation without public editable source during source-only verification.
- Added a native-source guard so body/head model catalog validation refuses ROM/static handles before provider size checks.
- Added native-source guards so menu preview, title, and player weapon model sizing refuse ROM/static model handles in source-only verification.
- Added a native-source guard so queued held weapon model streaming refuses ROM/static fallback in source-only model verification.
- Added a native-source guard so Scenario speech audio can use ROM filename metadata only after extracted public speech source has been proven present.
- Expanded Duel Scenario source-matrix proof to require live player auto-walk actions from public AI and pad source.
- Expanded Extra16 Scenario source-matrix proof to require live object, lift, lighting, room, distance, and target-movement actions from public source.
- Hardened Scenario retained-route proof for optional live actors, target-prop movement, MP3 speech, and Airbase/Extra16/Skedar Ruins route families from public Scenario source.
- Hardened Scenario setup behavior links so target rows and expected object types are validated from public setup source before live registration.
- Hardened Scenario object possession, equipped-weapon, and give-object graph paths so absent runtime selectors branch or no-op instead of aborting source-only runs.
- Expanded Rescue Scenario source-matrix proof to require stable live AI actions from public path, pad, object, lighting, environment, speech/audio, and inventory sources.
- Hardened Scenario setup-link matrix proof so retained smokes require each authored behavior-link kind they exercise.
- Refreshed CI Training Scenario matrix proof to match source-resolved AI logs and avoid non-deterministic branch-only route checks.
- Added a native-source guard for Scenario AI-list runtime lookups so `ailistFindById()` stays behind public `ai/ailists.json` proof.
- Added a Scenario external-global inventory guard so new non-`g_Vars` runtime globals fail until explicitly audited.
- Hardened the Scenario scene-room guard so room table pointer and room-count reads stay inside source-built room helpers.
- Hardened the Scenario quip asset guard so bare quip-bank pointer selection is guarded alongside indexed table reads.
- Hardened the Scenario audio alias guard so both the legacy mapping table and its count stay inside catalog-resolved audio normalization.
- Added a native-source inventory guard for Scenario runtime globals so non-interpreter `g_Vars.*` access cannot grow outside audited source-boundary scanners.
- Added a native-source guard for Scenario active-character pointer reads so `g_Vars.chrdata` stays inside source-aware proof helpers.
- Added a native-source guard for Scenario current-player-number routing so player switches and HUD/speech fallbacks validate player slots before current-player state access.
- Added a native-source guard for Scenario stage-number special cases so `g_Vars.stagenum` reads stay in source-proven graph handlers.
- Added a native-source guard for Scenario Bond/Co-op player-number checks so identity globals stay in source/player-proven handlers.
- Added a native-source guard for Scenario Anti-player team checks so `g_Vars.antiplayernum` stays behind `chr_set_team` source proof.
- Added a native-source guard for Scenario player-autowalk state so `g_Vars.tickmode` reads stay behind player-slot proof.
- Added a native-source guard for Scenario runtime-mode globals so co-op checks cannot read MP/buddy state outside graph-source proof.
- Added a native-source guard for Scenario kill-count checks so `g_Vars.killcount` reads stay behind mission/global graph proof.
- Added a native-source guard for Scenario frame-state timing so frame globals stay behind graph/source proof.
- Added a native-source guard for Scenario autocut state so cutscene globals stay behind graph-source proof.
- Added a native-source guard for Scenario lift-number validation so `g_Lifts` bounds checks stay behind public `pads.json` proof.
- Added a native-source guard for Scenario teleport sound priority so audio-manager priority changes stay behind catalog-resolved graph audio proof.
- Added a native-source guard for Scenario cutscene animation timing so frame-overrun reads stay behind animation source proof.
- Added a native-source guard for Scenario environment globals so wind speed and tinted glass state stay behind graph-source proof.
- Added a native-source guard for Scenario player invincibility so `g_PlayerInvincible` reads and writes stay behind player-slot proof.
- Added a native-source guard for Scenario mission/music mode globals so co-op mode and music-queue reads stay behind graph-source proof.
- Added a native-source guard for Scenario portal globals so portal table pointer/count reads stay behind public `portals.json` proof.
- Added a native-source guard for Scenario source wide-pad offsets so large public `pads.json` runtime offset caches stay inside source padfile helpers.
- Added a native-source guard for Scenario setup-tag globals so `g_TagsLinkedList` stays inside the source-gated setup-tag count helper.
- Added a native-source guard for Scenario `g_StageSetup` table access so source-derived setup, path, and navigation tables stay behind source-gated helpers.
- Added a native-source guard for Scenario padfile count globals so `g_PadsFile->...` reads stay inside source-gated count helpers.
- Added a native-source guard for Scenario cutscene visibility slot scans so `g_ChrSlots[]` preflight stays source-proven before visibility changes.
- Added a native-source guard for Scenario quip asset tables so legacy quip audio/text rows stay inside catalog-resolved graph actions.
- Added a native-source guard for Scenario special-death animation rows so `g_SpecialDieAnims[]` stays behind animation catalog proof.
- Added a native-source guard for Scenario audio alias mappings so `g_AudioRussMappings[]` can only feed catalog-resolved Scenario audio IDs.
- Added a native-source guard for Scenario player-control writes so revoke/grant control must prove player slots before `g_PlayersWithControl[]` changes.
- Added a native-source guard for Scenario room and portal table mutations so `g_Rooms[]` and `g_BgPortals[]` writes stay behind public scene/portal source proof.
- Added a native-source guard for Scenario literal character scans so squad/team `chrFindByLiteralId()` results must be runtime-character proven before live state use.
- Added a native-source guard for Scenario character lookups so `chrFindById()` stays inside source-aware character-reference helpers.
- Added a native-source guard for Scenario suspicious-item room prop scans so `roomGetProps()` results stay behind spatial object-source and character proof.
- Added a native-source guard for Scenario prop-preset table access so `remove_object_at_prop_preset` and height checks keep public object-source and character proof before `g_Vars.props` reads.
- Added a native-source guard for Scenario target-index derivation so `propGetIndexByChrId()` stays behind character and target proof.
- Added a native-source guard for Scenario prop-index derivation so `remove_references_to_chr` proves the current character before clearing prop references.
- Added a native-source guard for Scenario hovercar branch helpers so stop, attack, and LOS graph paths prove public `objects.json` vehicle source before chopper helper calls.
- Added a native-source guard for Scenario hovercar timer access so timer graph actions prove public `objects.json` vehicle source before chopper timer reads or writes.
- Added a native-source guard for Scenario hovercar target mutation so `set_target` proves public `objects.json` vehicle source before `chopperSetTarget`.
- Added a native-source guard for Scenario hovercar target reads so `if_y` proves public `objects.json` vehicle source before chopper target lookup.
- Added a native-source guard for Scenario Bond/Co-op pointer snapshots so global player identity stays behind proven local player pointers or validated player slots.
- Added a native-source guard for Scenario current-player pointer snapshots so current-player graph paths must carry proof through local player pointers.
- Added a native-source guard for Scenario setup/path lookup APIs so direct runtime tag, object, and path lookups stay behind source-checked helpers.
- Added a native-source guard for Scenario Bond/Co-op global access so object-activation and P1/P2 ownership graph paths use source-proven local player pointers.
- Added a native-source guard for Scenario mission-global player writes so `kill_bond` mutates only the source-proven Bond player pointer.
- Added a native-source guard for Scenario cutscene visibility so show/hide actions mutate only source-proven local character slot pointers.
- Added a native-source guard for Scenario setup/equipment access so equipment and tag-255 object animation actions use source-proven active character state.
- Added a native-source guard for Scenario camera sleep state so `set_camera_animation` uses the source-proven active character-state pointer before yielding.
- Added a native-source guard for Scenario hovercopter fire so `hovercopter_fire_rocket` uses the source-proven local chopper pointer before rocket playback.
- Added a native-source guard for Scenario quip access so `say_quip` uses the source-proven active character pointer for quip state, audio, and subtitle routing.
- Added a native-source guard for Scenario list-control access so return-list and shot-list actions use source-proven local actor pointers after public `ai/ailists.json` and `objects.json` proof.
- Added a native-source guard for Scenario vehicle-motion access so truck, hovercar, and heli motion actions use source-proven local vehicle pointers after public `objects.json` proof.
- Added a native-source guard for Scenario player-state access so direct current-player and player-table reads stay behind proof helpers.
- Added a native-source guard for Scenario runtime row-count helpers so future table counts must stay behind public-source proof.
- Hardened Scenario logging-only pad row counts so graph diagnostics no longer count runtime pads unless a real public `pads.json` operand is required.
- Hardened Scenario character and vehicle row-source validation so graph helpers require public setup/spawn/object TSVs before runtime row counts.
- Hardened Scenario cover/navigation table source validation so graph helpers require public navigation TSVs before runtime table access.
- Hardened Scenario path-table source validation so graph path helpers require public `navigation/paths.json` before runtime path table access.
- Hardened Scenario setup-tag source validation so graph setup-tag lookups require public `setup.fields.json` before runtime setup tag access.
- Hardened Scenario object-tag source validation so graph object lookups require public `objects.json` before runtime setup object access.
- Hardened Scenario pad-source validation so graph pad lookups require public `pads.json` before runtime pad access.
- Hardened Scenario path-source validation so graph path lookups require public `navigation/paths.json` before runtime path lookup.
- Added Scenario Anti-player team validation so `chr_set_team` proves player slots before Anti team mutation.
- Added Scenario G5 Eyespy room-condition validation so `if_chr_in_room` proves player slots before Eyespy distance reads.
- Added Scenario portal-distance validation so `if_player_chr_portal_distance_less_than` proves current-player state before portal distance reads.
- Added Scenario clear-inventory validation so `clear_inventory` clears device state through the proven player slot.
- Added Scenario grab-object validation so `chr_grab_object` reads move/crouch state through the proven player slot before grab helpers.
- Added Scenario Eyespy object-enable validation so `enable_obj` carries the proven current-player pointer through Eyespy initialization checks.
- Added Scenario animation player-ground validation so `chr_do_animation` proves player slots before animation rewind updates player ground state.
- Added Scenario player-facing misc/audio/HUD validation so explosions, speech, and HUD message graph paths prove player slots before current-player state access.
- Added Scenario player-autowalk validation so autowalk action and completion checks prove player slots before player-navigation state access.
- Added Scenario room/object weapon-condition validation so object possession and equipped-weapon checks prove player slots before inventory or weapon-state reads.
- Added Scenario inventory/ammo validation so drop/give-object and ammo-check graph paths prove player slots before inventory or ammo state access.
- Added Scenario fade/invincibility validation so player fade and invincibility graph actions prove player slots before player-state side effects.
- Added Scenario player-control validation so revoke/grant control graph actions prove player slots before player-control side effects.
- Added Scenario camera-cutscene player validation so camera animation graph actions prove current-player state before player body-state reads.
- Added Scenario device-state validation so player-device graph checks prove player slots before reading device state.
- Added Scenario teleport player-state validation so teleport graph actions prove player slots before teleport state reads or writes.
- Added Scenario health-condition validation so player-character health checks prove player slots before reading player health.
- Added Scenario player-object validation so looking-at-object and grab-object graph paths prove player slots before player-object state reads or grab helpers.
- Added Scenario player-weapon validation so weapon draw and forced-speed graph actions prove player slots before player weapon or movement state changes.
- Added Scenario colour-fade validation so fade-complete graph checks prove player slots before reading fade timers.
- Added Scenario quip/death player-state validation so quip player routing and death-animation checks prove player slots before death-state reads.
- Added Scenario Eyespy target validation so Eyespy target-selection graph actions prove player slots before Eyespy state reads.
- Added Scenario P1/P2 ownership validation so toggle and explicit P1/P2 graph actions prove player state before ownership side effects.
- Added Scenario player-state validation so global fade, inventory, Eyespy object-enable, and Bond kill graph actions prove player slots before player side effects.
- Added Scenario camera-cutscene state validation so camera animation graph actions prove source-owned active character state before cutscene start or sleep-state mutation.
- Added Scenario drop-object character validation so object-drop graph actions prove the parent character before dropped-item flag mutation.
- Added Scenario active list-control validation so AI list setters and return-list reads prove the active character before touching live list state.
- Added Scenario room-mutation validation so room flag/environment graph actions prove the source-built scene room table and skip sparse public room refs instead of writing outside `g_Rooms[]` or falling back.
- Hardened Scenario lighting proof so inactive actor slots preserve the no-room behavior while live actors still prove source ancestry before room resolution.
- Added Scenario cutscene visibility validation so global show/hide character-slot actions prove source-backed or graph-spawned characters before flag mutation.
- Added Scenario lift/lighting/target-distance actor validation so lift-use, room-light, and distance-to-target graph paths prove source-backed actors before live helper calls.
- Added Scenario misc branch/effect validation so natural-animation, sound-timer, target-height, melee, eyespy, pounce, avoid, and hovercopter rocket graph paths prove live actors or vehicles before state access.
- Added Scenario intent/status validation so active orders, listening, and action predicates prove source-backed actors before live state reads.
- Added Scenario order/action validation so team-order flag reads and attack-amount graph actions prove source-backed actors before live state access.
- Added Scenario explicit-character flag validation so targeted flag graph actions prove source-backed characters before live flag reads or mutation.
- Added Scenario active-character flag validation so direct flag graph actions prove source-backed actors before live flag reads or mutation.
- Added Scenario active-character tuning validation so morale, alertness, health, shield, and rating graph actions prove source-backed actors before live state mutation.
- Added Scenario vehicle list-control validation so vehicle return-list actions prove source-built vehicle rows before live return-list reads or mutation.
- Added Scenario vehicle-motion validation so truck, hovercar, chopper, and heli graph actions prove source-built vehicle rows before live path, speed, rotor, or hoverbot next-step state access.
- Added Scenario chopper weapon-state validation so helicopter armed checks and arm/unarm graph actions prove source-built vehicle rows before live weapon-state reads or mutation.
- Added Scenario prop-preset/target character validation so prop preset, target assignment, near-character, and dangerous-object graph paths prove active actors before live state access.
- Added an exhaustive non-Scenario typed-archive source matrix runner that can generate source-only catalog-load smokes for every extracted public `.pdxxx` archive, with Scenario kept on its separate stage-load matrix.
- Added a sequential Scenario source-only matrix runner that generates per-stage smokes from extracted `.pdscenario` manifests and proves public scene, collision, portal, pad, and graph source loading.
- Added Scenario AI graph proof so source-only stage smokes must show state/order, environment, audio/music, object/player, inventory, timer/HUD, and late utility modules from public Scenario graph source.
- Added Scenario source-matrix proof for native `scene.glb` background activation and header-only generated navigation from public source rows.
- Added campaign mission graph proof so source-only Scenario smokes must show public mission graph activation, phase source startup, and mission objective runtime proof for matching `.pdmission` archives.
- Added Scenario setup-link matrix proof so source-only smokes retain evidence that public `setup.fields.json` link rows feed graph-owned live registration.
- Added Scenario source-matrix proof that public navigation path flag counts survive into runtime setup data.
- Added Scenario source-matrix proof that live AI path commands validate requested path ids against public `navigation/paths.json` runtime data.
- Added Scenario vehicle path graph validation against public `navigation/paths.json` runtime data.
- Added Scenario player autowalk graph validation against public `pads.json` runtime data.
- Added Scenario graph source-contract preflight before live object-tag lookup on object-backed room/object/weapon predicates.
- Added Scenario door graph source-object type validation before door-only runtime access.
- Added Scenario source-object type validation before lift, monitor image, and autogun graph runtime access.
- Added Scenario setup-tag table validation before Investigation terminal, Skedar Ruins pillar, Pelagic switch, and warp-to-tag graph placement access.
- Added CI Training Scenario source-matrix proof for live AI runtime actions from public `ai/ailists.json`.
- Added CI Training Scenario source-matrix proof for live AI runtime conditions from public `ai/ailists.json`.
- Added CI Training Scenario source-matrix proof for more live AI object, environment, combat, list, audio, and player-state actions.
- Added CI Training Scenario source-matrix proof for live AI cutscene, timer, setup, lift, lighting, flag, and state actions.
- Added CI Training Scenario source-matrix proof for remaining live AI flag, debug/no-op, cutscene reorient, and list-return setup actions.
- Added Scenario source-matrix proof that explicit public navigation waypoint and waygroup neighbour refs survive into runtime padfile data.
- Added Scenario generated-nav fixture proof that public path edges survive into final runtime navigation neighbour refs.
- Added Scenario matrix proof that generated public navigation path edges are counted at the final runtime padfile boundary.
- Added Scenario source-matrix catalog-id proof so retained smokes treat `.pdscenario` catalog ids and archive members as source identity while isolating legacy `--boot-stage` numbers as a temporary boot bridge.
- Added Scenario AI stage-ID condition proof that resolves compared runtime stage numbers to catalog stage IDs and forbids numeric `stagenum` predicate logs in retained source smokes.
- Added Scenario AI cutscene-weapon proof that resolves real weapon operands to catalog IDs and forbids numeric weapon predicate/action logs in retained source smokes.
- Added Scenario cutscene-weapon model proof so derived weapon models resolve to catalog mesh IDs before weapon creation.
- Hardened Scenario cutscene-weapon model proof so no-weapon sentinels skip legacy model lookup.
- Added Scenario character-entity audio validation so `play_sound_from_entity` proves source-built character rows or explicit player actors before binding positional sound.
- Added Scenario AI audio proof that resolves live SFX/voice operands to catalog IDs and forbids numeric audio/sound proof logs in retained source smokes.
- Added Scenario AI music-track proof that resolves live music operands to catalog IDs and forbids numeric track proof logs in retained source smokes.
- Added Scenario AI animation proof that resolves live animation operands to catalog IDs and forbids numeric animation proof logs in retained source smokes.
- Added Scenario preset-animation proof so hardcoded AI animation presets resolve to catalog animation IDs before playback.
- Added Scenario teleport sound proof so hardcoded teleport fade SFX resolve to catalog audio IDs before playback.
- Added Scenario AI body, head, model, and weapon proof so spawn/equipment/inventory graph actions resolve asset operands to catalog IDs instead of numeric runtime ids.
- Added source-only body/head fallback hardening so player character model failures fail loudly instead of substituting fallback bodies during asset-family verification.
- Added Scenario catalog-ID boot support so source-only Scenario smokes boot through `.pdscenario` identity instead of a numeric stage bridge.
- Added Scenario teleport/cutscene pad validation so warp-orbit and teleport graph actions prove source-built `pads.json` rows before writing player warp state.
- Added Scenario repeating pad-sound validation so pad-backed audio graph actions prove source-built `pads.json` rows before creating spatial sound.
- Added Scenario near-pad target validation so character-preset searches prove source-built `pads.json` rows before live near-pad selection.
- Added Scenario room-predicate pad validation so room checks prove source-built `pads.json` rows before deriving rooms from pad operands.
- Added Scenario pad-preset copy validation so copied AI pad presets prove source-built `pads.json` rows before runtime mutation.
- Added Scenario cover-selection validation so AI-selected cover ids prove source-built `navigation/covers.json` rows before movement/proof.
- Added Scenario quadrant pad-preset validation so AI-selected quadrant pads prove source-built `pads.json` rows before branch proof.
- Added Scenario waypoint-quadrant condition validation so successful quadrant checks prove their selected pad rows before branching.
- Added Scenario character-copy pad validation so duplicated/copied character pad presets prove source-built `pads.json` rows before mutation.
- Added Scenario communication/quip character validation so not-talking checks and quip nearby scans prove source-backed characters before live sound/talking state reads.
- Added Scenario combat active-actor validation so combat helpers prove source-backed characters before live attack and movement state reads.
- Added Scenario duplicate-character asset validation so cloned bodies, held weapons, and hats resolve to catalog IDs before clone creation.
- Added Scenario duplicate-character source validation so cloned characters prove their source character rows before clone creation.
- Added Scenario character lifecycle validation so `enable_chr` and `disable_chr` prove target character rows before prop activation or delisting.
- Added Scenario character property copy validation so `chr_copy_properties` proves source character rows before copying live character state.
- Added Scenario character pad-preset copy validation so `chr_copy_pad_preset` proves source and target character rows before copying pad preset state.
- Added Scenario player autowalk character validation so autowalk state changes and checks prove source-built character rows first.
- Added Scenario character state validation so team, damage, kill, grab-object, P1/P2, and cloak actions prove source character refs before mutation.
- Added Scenario character condition validation so lifecycle, health, shield, injury, and alertness predicates prove source character refs before branching.
- Added Scenario character combat validation so removal and direct damage actions prove source character refs before mutation.
- Added Scenario object/perception character validation so room, inventory, activation, drop, and transfer paths prove source character refs first.
- Added Scenario speech/player/cutscene character validation so speech, weapon, control/fade, and player perception paths prove source character refs before live state access.
- Added Scenario animation/list-control validation so target characters prove source refs before animation or AI-list mutation.
- Added Scenario intent/target character validation so listening, target, ammo, race, and target-assignment paths prove source character refs first.
- Added Scenario preset-team validation so legacy Bond fallback is preserved but the final team-compare character must prove source first.
- Added Scenario safety/misc-effect character validation so target movement, Y checks, explosions, and motion blur prove source character refs first.
- Added Scenario quip/misc-effect character validation so spark, Dr. Caroll image, quip, and CI staff quip paths prove source character refs before live effect/audio state.
- Added Scenario stat-mutation character validation so morale, alertness, and max-damage actions prove source character refs before live character state changes.
- Added Scenario character flag validation so chr/hidden flag actions prove source character refs, source-spawned characters, or explicit engine-owned selectors before mutation or branching.
- Added Scenario cover/retreat character validation so cover movement, orbit, squadron, and release actions prove active actors before live cover or target state access.
- Added Scenario cutscene/teleport character validation so player-device, teleport, cutscene-weapon, HUD-piece, and cutscene-firing paths prove source character refs before live player or cutscene state access.
- Added Scenario HUD/model/death character validation so same-floor distance checks, model-part toggles, special-death assignment, and player-targeted HUD messages prove source character refs before live state access.
- Added Scenario room/timer character-state validation so room-search and character timer actions prove prop-backed, setup-backed, source-spawned, or public graph-owned character state before live reads or mutation.
- Added Scenario move-to-pad character validation so moving actors and mode-88 character targets prove source before live movement state access.
- Added Scenario order/preset character validation so preset and action mutations prove source-backed character state first.
- Added Scenario squadron/team character validation so team-order, teammate-preset, squadron condition/count, and squadron-alertness loops prove source-backed characters before live reads or mutation.
- Added Scenario safety/detection character validation so nearby/enemy team scans prove source-backed characters before live state reads or target assignment.
- Added Scenario target-movement validation so movement-to-target helpers prove source-backed active and target characters before live movement calls.
- Added Scenario perception/alarm validation so hearing, sight, patrol, and route-to-target checks prove source-backed actors before live perception reads.
- Added Scenario spatial-perception validation so LOS, screen/FOV, suspicious-item, and target-distance checks prove source-backed actors before live spatial reads.
- Added Scenario distance-perception validation so character-to-pad, character-to-character, near-self, and target-to-pad checks prove source-backed actors before live distance reads.
- Added Scenario active-state/gun validation so random, actor-state, pouncebits, reposition, portal-distance, and current-gun paths prove source-backed actors before live state reads or mutation.
- Added Scenario basic-motion/surprise validation so stop, kneel, surrender, fade-out, and surprised actions prove source-backed active actors before live action helpers.
- Added Scenario pad/path validation so alarm, movement, path, patrol, and pad-preset actions prove source-backed characters before live path helpers.
- Added Scenario spawn-at-character validation so `spawn_chr_at_chr` proves target characters come from source-built setup rows before live spawn creation.
- Added Scenario weapon-predicate validation so thrown/equipped weapon checks resolve catalog IDs before live runtime checks.
- Added Scenario blocked-path setup-link validation so waypoint refs prove source-built navigation rows before live path toggling.
- Added Scenario path-pad validation so AI path and vehicle path commands prove source-built pad sequences before live path mutation.
- Added Scenario hoverbot next-step validation so vehicle path state proves source-built path and pad rows before live step comparisons.
- Added Scenario current-gun validation so gun interaction AI proves weapon/model catalog source before live gun movement, distance, and recovery behavior.
- Added Scenario no-gun condition validation so non-null current-gun state proves weapon/model catalog source before false-branch proof.
- Added Scenario special-death validation so explicit special-death modes prove catalog animation source before assignment.
- Added Scenario language-text validation so speech, quip, and HUD text actions prove public `.pdlang` source before display.
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
- Added scenario AI graph execution for character kill and inventory weapon-removal commands, with extracted AI tables carrying catalog weapon IDs for inventory references.
- Added scenario AI graph execution for clear-inventory, release-object, and grab-object commands, tied to public AI, object, and scene source.
- Added scenario AI graph execution for player assignment, cloak, and autogun target-team commands, tied to public AI and object source.
- Added scenario AI graph execution for target-movement branch commands, including run-from-target, target-prop movement, cover-prop movement, and character-to-character movement from public AI source.
- Added scenario AI graph execution for pouncebits, Training PC hologram, and player-device branch conditions.
- Added scenario AI graph execution for music-event queue and co-op mode branch conditions.
- Added scenario AI graph execution for same-floor pad-distance and reference-cleanup commands, tied to public AI and pad source.
- Added scenario AI graph execution for music track playback and stop commands, tied to public AI source.
- Added scenario AI graph execution for entity lifecycle and motion commands, including clone, enable/disable, move-to-pad, team, damage, preset animation, and related branch checks.
- Added scenario AI graph execution for setup, spawn, and equipment behavior, including character spawning, weapon/hat equip, monitor image updates, object animation, and direct door-open state.
- Added scenario AI graph execution for character intent/status branch checks, including talking, listening, orders, squadron action, injured-target latch, and action predicates.
- Added scenario AI graph execution for prop-preset and target behavior, including visibility/height checks, target assignment/comparison, and nearby-character preset selection.
- Added scenario AI graph execution for vehicle and Investigation terminal behavior, including dangerous-object checks, hovercar weapon/step state, terminal shuffling, terminal pad mapping, and heli arm state.
- Added scenario AI graph execution for safety, floor-aware enemy detection, CMP/AR34 player-weapon, and target-motion branch behavior from public AI, scene, and character-state source.
- Added scenario AI graph execution for miscellaneous branch conditions, including squadron dead/count checks, unconditional branches, natural animation checks, Y-position checks, sound timer checks, and target height-difference checks.
- Added scenario AI graph execution for player weapon-state commands, tied to public AI source.
- Added scenario AI graph execution for object-room branch conditions, tied to public AI, object, and scene source.
- Added scenario AI graph execution for player autowalk navigation commands, tied to public AI and pad source.
- Added scenario AI graph execution for debug print and no-op commands, tied to public AI source.
- Added scenario AI graph execution for spatial perception branches, including LOS, screen/room visibility, target aim, near-miss, suspicious-item, FOV, and target-distance checks from public AI, pad, and object source.
- Added a native-source guard that tracks remaining scenario AI graph parity debt explicitly while keeping generated scenario `scene.glb` texture-scale verification as a hard handoff gate.
- Added scenario AI graph execution for player weapon/cover state behavior, including character weapon deletion, trigger-shot-list latches, cover release, and attack amount selection from public AI and cover source.
- Added scenario AI graph execution for player cutscene, warp, control, and fade behavior, tied to public AI, pad, setup, and object source.
- Added named weapon graph parity modules so current OG-backed held, projectile, and deployed-entity behavior families are explicit before future retirement cuts.
- Added strict typed asset archive conformance validation for every `.pdxxx` family, including recursive embedded archive checks and catalog-ID-only public references.
- Added base extraction and native catalog/provider loading for the remaining `.pdmaterial`, `.pdtexture`, `.pdskin`, `.pdeffect`, `.pdprop`, `.pdvehicle`, `.pdmission`, `.pdgamemode`, `.pdbotprofile`, `.pdhud`, and `.pdtheme` archive families.
- Added a Settings > Debug asset source gate so one typed archive family at a time can be forced to use extracted/generated FileProvider source during playtest.
- Added static coverage that keeps the Settings > Debug source gate aligned with every current public typed archive family.
- Added public Scenario `portals.json` archives and native runtime portal-table compilation from extracted `.pdscenario` source.
- Added a Scenario source-ground smoke guard so invalid Chicago player-body ground sync warnings fail verification instead of passing unnoticed.
- Added exhaustive Scenario source-only matrix coverage for all 87 current `.pdscenario` archives.
- Added Scenario runtime validation for public `navigation.ini` and generated navmesh metadata so source-only stage loads prove their navigation source/cache binding.
- Added Scenario navmesh source-count validation so generated nav cache metadata must match public navigation tables before source-only stage loads continue.
- Added Scenario navmesh source-hash validation so stale generated nav cache metadata is rejected when public navigation inputs change.
- Added Scenario path-source smoke proof so source-only stage loads must show public `navigation/paths.json` graph binding.
- Added Scenario setup path-table smoke proof so source-only stage loads must show public `navigation/paths.json` compiled into runtime setup data.
- Added strict Scenario navigation-table runtime proof for public waypoint, waygroup, and cover TSV source.
- Added Scenario nav behavior graph proof for source-bound path, cover, player-navigation, quadrant, and vehicle path surfaces.
- Added deterministic Scenario navigation-table generation from public pads when waypoint, waygroup, and cover TSV sources are intentionally header-only, including valid zero-pad scenarios and path-source ordering when `navigation/paths.json` is present.
- Added a Scenario generated-nav fixture smoke so nonzero header-only nav generation must consume public `pads.json` ordered by `navigation/paths.json`, and stale generated-nav metadata cannot pass Scenario extraction fast-cache checks.
- Added Scenario generated-nav path topology so header-only navigation tables build waypoint neighbor edges from public `navigation/paths.json` rows instead of a generic pad chain.
- Added Scenario generated-nav waygroup derivation so disconnected public path components become separate runtime waygroups.
- Added Scenario generated-nav circular path handling so public circular path rows close the generated waypoint loop.
- Added Scenario setup path-flag proof so circular and flying public path rows are smoke-verified in runtime setup data.
- Added Scenario generated-nav waypoint segment flags so public `navigation/paths.json` directional `outward`/`inward` path tokens survive into generated runtime neighbour links.
- Added strict Scenario generated-nav movement capability proof so public `navigation.ini`, generated navmesh metadata, and source-only stage smokes agree on walk/jump/drop/wall/ceiling support.
- Added Scenario trigger/global graph proof so source-only stage smokes must show trigger-volume nodes and mission/global AI actions from public graph source.
- Added Scenario graph-source surface proof so source-only stage smokes must show global settings, volume table, pad table, and AI list source activation from public archive members.
- Added Scenario core AI graph proof so source-only stage smokes must show lifecycle, combat, target movement, perception, object interaction, animation, random, debug/no-op, and list-control modules from public AI graph source.

## Changed

- All-family asset source matrix failures now retain batch manifests and report missing debug-load evidence clearly before blaming an asset family.
- The all-family asset source gate now separately proves SFX, voice, song, and first-launch UI archives through public FileProvider source.
- Source-only Defense startup no longer crashes on roofguns; public `.pdmesh` autogun modeldefs now rebuild the required runtime parts and invalidate stale generated model caches.
- Source-only Air Force One, Defection, and Investigation startup no longer crashes while initializing public-source model geometry; generated model parts are type-checked before auto floor/wall collision use.
- Scenario objective graph startup seeding now waits for the player prop before querying inventory-held object state, avoiding early source-only mission startup crashes.
- Source-only Villa startup no longer hangs when public Scenario source has no legacy dynamic-light table.
- Source-only War startup no longer crashes when graph object destruction hits embedded setup objects.
- Source-only Scenario startup no longer leaks raw spawn-ground sentinel warnings on MP/test stages with empty intro spawns or tiny public collision meshes.
- Scenario quip and sound distance checks now fall back cleanly when source-built stages have no legacy light-transfer table.
- Fixed Scenario source-only matrix booting for Extra25/Extra26 by removing the stale `--boot-stage` 0x5d cap; both stages now load their public `.pdscenario` sources in source-only validation.
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
- Scenario ground checks can now use native world mesh floors built from public `.pdscenario` source when room-local tile lookup has no floor.
- C-3838 runtime bindings now cover all approved file-backed `.pdxxx` families, including `.pdprop`, with type/id/target/kind lookups plus primary-file accessibility and load validation.
- Catalog-generated base asset IDs and typed archive references now use readable names instead of legacy numeric handles.
- Weapon extraction now writes zero/no-sound fire SFX fields as `0` instead of fake `SFX_0000` references, and strict archive conformance rejects stale weapon manifests that reintroduce them.
- First-launch extraction now skips intentionally empty ROM file slots without logging false `romdataFileGetSize` errors.
- Scenario archive extraction no longer writes raw setup/mpsetup/visual word dumps as public payloads, and typed archive guards now reject numeric or legacy-symbol asset references in authoring files.
- Scenario and mission archives now use source-first public payloads: `.pdscenario` emits `scene.glb`, decoded catalog-ID setup tables, navigation inputs, level graph JSON, and generated collision/navmesh metadata, while `.pdmission` carries `mission.graph.json`.
- Scenario extraction now backfills 87 standalone `.pdscenario` archives and keeps `scene.glb` DCC texture UVs bounded while preserving sampled runtime-repeat UV parity.
- Scenario AI room flag behavior now executes through public level graph source, with Chicago `scene.glb` texture scale verified for Blender/3DS Max authoring UVs.
- Scenario AI cutscene character visibility now executes through public level graph source, with Chicago `scene.glb` texture scale verified for Blender/3DS Max authoring UVs.
- Scenario AI environment/global room behavior now executes through public level graph source, with Chicago `scene.glb` texture scale verified for Blender/3DS Max authoring UVs.
- Scenario AI target-distance conditions now execute through public level graph source, with Chicago `scene.glb` texture scale verified for Blender/3DS Max authoring UVs.
- Scenario AI speech, sound playback, channel state, object/entity/pad audio, and temporary primary music now execute through public level graph source, with generated `scene.glb` texture scale verified for Blender/3DS Max authoring UVs.
- Scenario AI same-floor pad-distance and reference-cleanup behavior now executes through public level graph source, with Chicago `scene.glb` texture scale verified for Blender/3DS Max authoring UVs.
- Scenario AI music track playback and stop behavior now executes through public level graph source, with Chicago `scene.glb` texture scale verified for Blender/3DS Max authoring UVs.
- Scenario AI player weapon equip, forced movement, invincibility, and no-gun condition behavior now executes through public level graph source, with Chicago `scene.glb` texture scale verified for Blender/3DS Max authoring UVs.
- Scenario AI object-room branch behavior now executes through public level graph source, with Chicago `scene.glb` texture scale verified for Blender/3DS Max authoring UVs.
- Scenario AI gun interaction behavior now executes through public level graph source, with Chicago `scene.glb` visible texture UV ranges guarded against tiny tiled DCC imports.
- Scenario AI character property copy now executes through public level graph source, with Chicago `scene.glb` visible texture UV ranges guarded against tiny tiled DCC imports.
- Scenario AI player autowalk navigation now executes through public level graph source, with fresh Chicago `scene.glb` texture scale verified for Blender/3DS Max authoring UVs.
- Scenario AI inventory clearing and object carrying now execute through public level graph source, with generated scenario texture-scale verification kept as a handoff gate.
- Scenario AI player assignment, cloak, and autogun target-team behavior now executes through public level graph source, with generated scenario and arena-nested GLB texture-scale verification kept as a handoff gate.
- Scenario AI state/device branch behavior now executes through public level graph source, with generated scenario texture-scale verification kept as a hard handoff gate.
- Scenario AI fade, passive mode, HUD-piece, cutscene firing, and portal flag behavior now executes through public level graph source, with generated scenario and arena-nested GLB texture-scale verification kept as a handoff gate.
- Scenario AI debug print and no-op commands now execute through public level graph source, with generated scenario and arena-nested GLB texture-scale verification kept as a handoff gate.
- Base weapon archives now use authored primary/secondary graph files directly at runtime instead of shipping a generated public `behavior/runtime.graph.json` duplicate.
- Weapon graph runtime records now retain the named `og.*` parity module selected by each held, projectile, or entity graph record.
- Gamemode and bot profile archives now require their public rule/profile source files for runtime activation.
- Replaced the old Settings Controls surface with a single actionmap-backed Input binding table.
- Main Menu now presents Play instead of Solo Play and removes the old Online Play direct-connect entry; online friend play now routes through Social invites/joins.
- Social friend invites are available even when a friend row is showing a stale Offline state.
- The Modding Hub weapon template flow now drills into an in-place tabbed weapon-creation view with auto-populated template refs, primary/secondary graph tabs, and an opaque mesh picker with live preview.
- Typed asset archive emitters now write machine metadata and hash sidecars under `_meta/`, while migration readers still accept legacy root metadata during the cleanup window.
- `.pdlang` base extraction now emits editable `strings.json` language source, and runtime language banks compile that JSON through the same catalog/FileProvider path used by mods.
- `.pdfont` base extraction now emits bitmap font metrics and kerning as semantic `font.metrics.json` source instead of public TSV tables.
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
- Scenario pads now compile from public `.pdscenario` `pads.json` during stage load instead of relying on the legacy pad payload when public source is available.
- Scenario archives now include decoded public waypoint, waygroup, and cover tables, and stage load compiles those tables with public `pads.json` instead of disabling navigation.
- Scenario `scene.glb` validation now fails generated archives whose visible materials use runtime-repeat UVs instead of Blender/3DS Max authoring UVs.
- Scenario AI list-control opcodes now execute through required `.pdscenario` graph nodes backed by public `ai/ailists.json`, and stale `scene.glb` archives are rejected before fast-cache reuse if they lack the current DCC/runtime UV stamp.
- Scenario level graphs now emit required AI morale/alertness action nodes, including cross-character and squadron alertness mutations, and runtime routes those state changes through graph-owned public `ai/ailists.json` source before the parity routine runs.
- Scenario AI graph execution now covers distance/preset branch conditions, including character-to-pad, character-to-character, nearby-character preset, and target-to-pad distance checks from public AI and pad source.
- Scenario AI graph execution now covers room/object/weapon conditions, including room checks, object possession, thrown/equipped weapon state, gun claim state, and object health from public AI, pad, and setup source.
- Scenario AI graph execution now covers object interaction actions, including activation latches, object interaction/destruction, character/object drops, object handoff, and object-to-pad movement from public AI, object, and pad source.
- Scenario AI graph execution now covers quadrant pad-preset behavior, including waypoint quadrant branching and target-quadrant pad preset selection from public AI, pad, and navigation source.
- Scenario AI graph execution now covers the final guarded quip/shuffle commands, so every non-interpreter AI command routine has an explicit graph runtime path before broader trigger/global/phase and fallback closure continues.
- Scenario level graphs now emit required AI action/order state nodes, and runtime routes `set_action`, `set_team_orders`, `retreat`, `set_squadron`, and `chr_set_listening` through graph-owned public `ai/ailists.json` source.
- Scenario level graphs now emit required AI team-maintenance nodes, and runtime routes teammate preset selection plus team/squadron rebuilds through graph-owned public `ai/ailists.json` source.
- Scenario level graphs now emit required AI cover/danger nodes, and runtime routes `orbit_target`, `face_cover`, and `danger_cover` through graph-owned public `ai/ailists.json` plus `navigation/covers.json` source.
- Scenario level graphs now emit required AI cover-search/navigation nodes, and runtime routes `find_cover`, distance-filtered cover search, `go_to_cover`, and cover line-of-sight checks through graph-owned public `navigation/covers.json` source.
- Scenario level graphs now emit required AI tuning/stat nodes, and runtime routes hearing/view distance, grenade probability, chr number, health/shield/damage, accuracy, reaction/recovery, and dodge ratings through graph-owned public `ai/ailists.json` source.
- Scenario level graphs now emit required AI alarm nodes, and runtime routes try-start, activate, and deactivate alarm opcodes through graph-owned public `ai/ailists.json` plus `pads.json` source.
- Scenario level graphs now emit required AI flag and stage-flag nodes, and runtime routes chr flag, target-chr flag, and stage-flag opcodes through graph-owned public `ai/ailists.json` source.
- Scenario level graphs now emit required AI savefile-flag nodes, and runtime routes savefile flag set, unset, and branch opcodes through graph-owned public `ai/ailists.json` source.
- Scenario level graphs now emit required AI HUD message nodes, and runtime routes HUD message, subtitle, middle-message, and removal opcodes through graph-owned public `ai/ailists.json` source.
- Scenario level graphs now emit required AI vehicle motion nodes, and runtime routes hovercar path startup, truck/hovercar speed, and rotor speed through graph-owned public `ai/ailists.json` plus `navigation/paths.json` source.
- Scenario level graphs now emit required AI door/object-control nodes, and runtime routes door open/close, door state tests, object-is-door tests, and door lock/unlock opcodes through graph-owned public `ai/ailists.json` plus `objects.json` source.
- Scenario level graphs now emit required AI lift nodes, and runtime routes lift stationary checks, go-to-stop, at-stop checks, lift activation, and using-lift branches through graph-owned public `ai/ailists.json`, `objects.json`, and `pads.json` source.
- Scenario level graphs now emit required AI weather/global nodes, and runtime routes rain/snow configuration through graph-owned public `ai/ailists.json` plus `scenario.ini` source.
- Scenario level graphs now emit required AI sky/wind nodes, and runtime routes alternate-sky transition plus sky wind speed through graph-owned public `ai/ailists.json` source.
- Scenario level graphs now emit the required AI room-lighting node, and runtime routes light on/off and timed light operations through graph-owned public `ai/ailists.json` plus `pads.json` source.
- Scenario level graphs now emit required AI model-part visibility nodes, and runtime routes character/object model-part visibility through graph-owned public `ai/ailists.json` plus `objects.json` source.
- Scenario level graphs now emit required AI object-health nodes, and runtime routes object health checks and mutations through graph-owned public `ai/ailists.json` plus `objects.json` source.
- Scenario level graphs now emit the required AI room-search node, and runtime routes target-room search assignment through graph-owned public `ai/ailists.json` plus source-derived scene room data.
- Scenario level graphs now emit the required AI special-death node, and runtime routes character special-death animation assignment through graph-owned public `ai/ailists.json` source.
- Scenario `scene.glb` texture-scale verification now has a direct checker for `.glb` files and `.pdscenario::scene.glb`, catching stale Chicago archives that still expose tiny-tiled runtime UVs to Blender/3DS Max.
- Scenario archives now feed public patrol paths and AI `set_path` / `start_patrol` graph nodes into runtime AI path behavior.
- Generated scenario GLBs now expose normalized `TEXCOORD_0` UVs for Blender/3DS Max while preserving renderer-repeat UVs in `TEXCOORD_1` for runtime parity.
- Strict scenario archive validation now parses public `scene.glb` UVs, rejects stale tiny-tiled DCC exports, and requires generated scenarios to retain runtime parity UVs.
- Scenario and Mission graph sources now activate during stage load from public archive members, proving `level.graph.json` and `mission.graph.json` are accessible to the runtime before behavior parity cutover.
- Scenario level graph table refs now select the public setup, pads, objective, and navigation source members consumed by runtime loaders.
- Mission archives now generate objective source and parity-backend graph nodes instead of empty graph placeholders or `original_perfect_dark_setup` pointers.
- Mission archives now generate per-objective and per-criteria graph nodes from decoded scenario objective rows, and runtime objective checks validate against the active mission graph before returning the current parity result.
- Mission objective Enter Room, Throw In Room, and Holograph criteria now keep their mutable status in graph-owned runtime state instead of reading legacy criteria status during graph evaluation.
- Mission objective completion/fail flag criteria now read graph-owned mission flag state instead of consulting the legacy stage-flag helper during graph evaluation.
- Mission objective Destroy, Collect, Throw, and Holograph criteria now read tagged object present/healthy/held state from graph-owned runtime state instead of querying object and inventory state during graph evaluation.
- Scenario setup behavior links now validate against graph-owned source records before live registration, covering linked guns, lift-door links, safe-item/padlock links, conditional scenery, and blocked paths.
- Scenario level graphs now bind and parse public `volumes.json` into graph-owned trigger-volume source rows during stage load.
- Enter Room and Throw In Room objective criteria now use graph-owned trigger-volume rows when a level graph is active, with the legacy room check kept only as the inactive-graph fallback.
- Scenario level graphs now emit explicit trigger-volume graph nodes, and stage activation validates those nodes against public `volumes.json` rows before trigger-volume source can be used.
- Scenario level graphs now emit a required global-settings source node, and stage activation validates it from public `.pdscenario::level.graph.json` before accepting the level graph.
- Scenario source activation now derives the matching `.pdscenario` catalog row from the active stage catalog ID when no explicit scenario ref is present.
- Mission graphs now emit required phase source nodes, and runtime records mission `load`/`active`/`complete`/`failed`/`end` transitions through the active `.pdmission` graph source.
- Scenario archives now include public `navigation/paths.json`, and stage setup compiles those patrol paths from the archive source so AI patrol startup does not depend on ROM setup data.
- Scenario level graphs now emit required AI pad movement action nodes, and runtime routes `jog_to_pad` / `go_to_pad_preset` / `walk_to_pad` / `run_to_pad` through graph-owned public `pads.json` source before the parity movement routine runs.
- Scenario level graphs now emit required AI pad-preset action nodes, and runtime routes `set_pad_preset`, `chr_set_pad_preset`, and `chr_copy_pad_preset` through graph-owned public source before the parity preset routine runs.
- Scenario level graphs now emit required AI chr-preset action nodes, and runtime routes `set_chr_preset` and `set_chr_target` through graph-owned public AI-list source before the parity routine runs.
- Source-gate smokes now fail closed on missing explicit source binaries and refresh the shared install binary on every explicit run, preventing stale clients from regenerating stale asset archives.
- Scenario source-only playtest now refuses legacy background-geometry reads directly at `bgLoadFile()`, exposing the remaining native `scene.glb` runtime replacement work instead of silently masking it.
- Scenario startup now renders public `.pdscenario::scene.glb` through a native source renderer, uses runtime-repeat `TEXCOORD_1` while preserving Blender/3DS Max authoring-scale `TEXCOORD_0`, builds source-derived room tables, and passes the Chicago source-only smoke without legacy BG `RomProvider` fallback.
- Scenario archives now include public `collision.obj` as the explicit collision override, and runtime source compilation preserves OBJ `room_<n>` tags so Chicago collision/tile data loads from `.pdscenario::collision.obj`.
- CI Training now extracts and loads a standalone `base_scenario_citraining.pdscenario`; forced Scenario source-only boot passes through CI Training without ROM/RomProvider tile or setup fallback, and normal client boot remains usable with `Build\pd.ini` restored to `AssetSourceOnlyType=0`.
- All top-level non-Scenario public archive families now pass a source-only runtime load gate, including metadata families, audio archive members, and `.pdprop` metadata activation.

## Fixed

- Fixed source-built `.pdmesh` rendering by exporting and compiling public `model.render.json` command source, preserving matrix-stack, material, and triangle order without exposing the render stream as TSV.
- Fixed public `.pdmesh` source-built models so extracted OBJ UVs and material/texture catalog bindings reach generated runtime display lists, with smoke verification proving the textured DY357 first-person mesh loads, builds, and renders from public source.
- Fixed Scenario source matrix validation so padded stage hex and stages that complete naturally before scripted exit do not produce false failures.
- Fixed Scenario source-only loading for minimal stages with intentionally empty public `pads.json` or `portals.json`, so those files are treated as authoritative empty source instead of triggering ROM fallback.
- Fixed generated one-test smoke folders so source-only matrix smokes do not fail strict mode before launching the client.
- Fixed normal `.pdmod` discovery logs so base-mod inheritance no longer appears as an asset-chain `fallback=` line.
- Fixed clean-install mod startup so a missing optional `mods-enabled.json` is treated as normal no-mod state instead of logging a false file-load error.
- Fixed base UI chrome so `base:ui_chrome_frame` is emitted and loaded from `ui_chrome_frame.pdui` instead of a generated loose `mods/base-game/ui-chrome` side folder.
- Fixed bundled `pd-modern-ui.pdmod` discovery so its `mod.json` declares a stable `pd-modern-ui` id instead of relying on filename fallback.
- Fixed random arena metadata extraction so the MP Random arena tokens no longer call the scenario stage-table lookup or log false `stageGetIndex(0x02/0x03)` warnings.
- Fixed scenario scene extraction so valid empty BG-room records no longer emit `convertRoomGfxData` pointer-clamp warnings during regenerated `.pdscenario` archives.
- Fixed generated `.pdmesh` door modeldefs so public OBJ sources provide source-derived bbox metadata, removing door setup/runtime bbox warnings without ROM fallback.
- Fixed smoke/release verification hangs from Windows loader/error dialogs by sourcing the canonical build environment, suppressing modal loader popups in smoke runs, and copying runtime DLLs into smoke installs.
- Fixed a title-sequence crash when generated catalog model sources lack legacy Rare-logo toggle parts.
- Fixed scenario path-table extraction so MP/test/Skedar Ruins `.pdscenario` archives with empty path tables are generated correctly and arena/mission dependency warnings are gone.
- Fixed generated logo `.pdmesh` modeldefs so public-source PDTWO title logo models expose the runtime-required front/morph parts and no longer skip or crash the intro.
- Fixed more scenario AI graph parity: character state/count conditions now execute through public `.pdscenario` graph source backed by `ai/ailists.json`, while generated scenario, mission-embedded, and arena-nested archives keep Chicago-style tiny tiled `scene.glb` imports gated by the texture-scale verifier.
- Fixed more scenario AI graph parity: character animation and surprised reaction actions now execute through public `.pdscenario` graph source backed by `ai/ailists.json`, with strict regenerated archive conformance and Chicago `scene.glb` DCC texture-scale verification kept in the handoff path.
- Fixed more scenario AI graph parity: random assignment and random branch conditions now execute through public `.pdscenario` graph source backed by `ai/ailists.json`, with generated archives and Chicago-scale `scene.glb` texture checks kept in the release gate.
- Fixed more scenario AI graph parity: debug print and no-op commands now execute through public `.pdscenario` graph source backed by `ai/ailists.json`, with generated archives and Chicago-scale `scene.glb` texture checks kept in the release gate.
- Fixed more scenario AI graph parity: mission/global branch conditions now execute through public `.pdscenario` graph source backed by `ai/ailists.json` and mission graph source, while the generated scene GLB texture-scale verifier keeps Chicago-style DCC UV regressions gated.
- Fixed more scenario AI graph parity: teleport and cutscene-weapon commands now execute through public `.pdscenario` graph source backed by `ai/ailists.json` and `pads.json`, with the generated scene GLB texture-scale verifier proving DCC authoring UVs across generated and arena-nested scenes.
- Fixed scenario GLB texture-scale verification so the focused checker and native-source guard scan generated scenario folders plus arena-nested scenario archives; stale Chicago-style tiny-tiled GLBs now fail in both smoke install and `Build\data`.
- Fixed more scenario AI graph parity: player assignment, cloak, and autogun target-team actions now execute through public `.pdscenario` graph source backed by `ai/ailists.json` and `objects.json`.
- Fixed generated scenario archives so stale shared `Build\data` scene GLBs with tiny tiled DCC textures fail the native-source guard; refreshed `Build\data\ntsc-final` now conforms strictly, including nested arena scenario dependencies.
- Fixed more scenario AI graph parity: player/object perception and target-is-player conditions now execute through public `.pdscenario` graph source backed by `ai/ailists.json`, `objects.json`, and `scene.glb`.
- Fixed more scenario AI graph parity: HUD message actions now execute through public `.pdscenario` graph source backed by `ai/ailists.json`, with the Chicago `scene.glb` texture-scale guard kept in the verification path.
- Fixed more scenario AI graph parity: vehicle motion actions now execute through public `.pdscenario` graph source backed by `ai/ailists.json` and `navigation/paths.json`, with the Chicago `scene.glb` texture-scale verifier proving fresh archives use DCC UVs.
- Fixed more scenario AI graph parity: door and object-control actions now execute through public `.pdscenario` graph source backed by `ai/ailists.json` and `objects.json`, with the Chicago `scene.glb` texture-scale verifier proving fresh archives use DCC UVs.
- Fixed more scenario AI graph parity: timer and countdown actions now execute through public `.pdscenario` graph source backed by `ai/ailists.json`, with the Chicago `scene.glb` texture-scale guard kept in the verification path.
- Fixed more scenario AI graph parity: chrflag, hidden-flag, and object-flag actions now execute through public `.pdscenario` graph source backed by `ai/ailists.json` and `objects.json`, while the Chicago `scene.glb` texture-scale guard remains pinned to DCC `TEXCOORD_0` plus runtime `TEXCOORD_1`.
- Fixed more scenario AI graph parity: idle/stopped/dead/knocked-out/can-see-target branch conditions now execute through public `.pdscenario` graph source backed by `ai/ailists.json`, with current generated scenario and arena GLBs texture-checked for DCC scale and stale installed Chicago archives identified as missing `scene.glb`.
- Fixed more scenario AI graph parity: stop and kneel basic-motion actions now execute through public `.pdscenario` graph source backed by `ai/ailists.json`, with regenerated `Build\data` and smoke-install archives retaining DCC-scale `scene.glb` textures for Blender/3DS Max.
- Fixed more scenario AI graph parity: combat movement and attack-state actions now execute through public `.pdscenario` graph source backed by `ai/ailists.json`, with restored `Build\data` and smoke-install archives retaining DCC-scale `scene.glb` textures for Blender/3DS Max.
- Fixed a CI startup render crash from legacy texture tuples by normalizing palette-backed RGBA/IA texture declarations before import/cache and using a transparent fallback for zero-sized uploads.
- Fixed the menu HUD-piece model source chain so `base:model_hudpiece_menu` is emitted as a public `.pdmesh` archive and loads from `.pdmesh::model.obj` instead of falling back to ROM.
- Fixed generated scenario `scene.glb` texture scale so Chicago and other extracted maps open in DCC tools with authoring-scale textures while preserving original repeated texture coordinates for runtime parity.
- Fixed source-gate smoke verification so a newer shared-install timestamp cannot cause smokes to run an older client than the build being validated.
- Fixed `.pdmesh` skeleton metadata export/loading so legacy small skeleton IDs like `SKEL_HEAD` are not treated as pointers, allowing all-model extraction and source-gated weapon matches to complete from public archive sources.
- Fixed held weapon model loading so modeldefs resolve through catalog/FileProvider `.pdmesh::model.obj` sources and refuse ROM fallback under the asset-source contract.
- Scenario setup overlays now compile from public `.pdscenario` setup source in source-gated match startup, and base `.pdmesh` extraction now emits every catalog model archive so scenario model references resolve under strict conformance.
- Scenario stage load now uses public `.pdscenario` `scene.glb` or an optional collision override as the source-derived world mesh when available.
- Scenario archives now include `setup.fields.json` as a named per-command setup source table, avoiding raw setup word dumps while preserving a complete authoring path for setup parity.
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
- Fixed Dev Window v2 game/test launches so missing MinGW `lib*.dll` helper dependencies no longer block behind Windows system error dialogs; current client/updater builds still import only system DLLs.
- Fixed Scenario navigation source loading so decoded waypoint, waygroup, and cover TSV files build valid runtime navigation tables from `.pdscenario` archives.
- Fixed Scenario graph-source runtime coverage so Chicago stage load validates `base_scenario_chicago.pdscenario::level.graph.json` and `base_mission_chicago.pdmission::mission.graph.json` through real archive-member file loads.
- Fixed Scenario graph-source selection so Chicago stage load binds `level.graph.json` table refs before compiling public pads/navigation source.
- Fixed Scenario generated-navmesh cache validation so public `portals.json` is hash-bound with the rest of the navigation source inputs.
- Fixed Scenario generated-navmesh cache validation so public `scene.glb` and `collision.obj` changes also invalidate stale nav metadata.
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
- Fixed custom `.pdweapon` dependency activation so embedded projectile/entity archives register their runtime graph payloads from public source during normal weapon archive activation.
- Fixed custom `.pdweapon` dependency cleanup so embedded projectile/entity runtimes are released when the owning weapon is removed or replaced, without clearing independently registered projectile/entity assets.
- Fixed a match-start crash when a saved `.pdweapon` mod was selected in Combat Simulator custom weapon slots.
- Fixed jump collision follow-through so airborne horizontal movement is clamped against rendered wall, ceiling, and corner geometry before the player can clip into it.
- Fixed generated scenario GLB texture export so Blender/3DS Max receive renderer-matched texture scaling, tile shifts, and per-material wrap, mirror, and clamp sampler modes.
- Fixed extracted SFX/voice parity so `.pdsfx` and `.pdvoice` archives preserve native keymap pitch metadata and file-backed playback applies base pitch, gameplay pitch, sample volume, and sample pan instead of playing every public WAV at default speed.
- Fixed weapon/inventory animation archives so `.pdanim` uses semantic `commands.json` source instead of public opcode or TSV-style tables, while the loader rebuilds native weapon animation commands from that editable source.
- Fixed generated `.pdmesh` runtime display-list handling so source-backed modeldefs use their real rebuilt pointers in character render and object hit-test paths instead of being treated like native segmented ROM addresses.
- Fixed scenario parent archive regeneration so `.pdarena` and `.pdmission` archives cannot keep stale embedded `.pdscenario` graph source after a scenario graph/cache update; parent descriptors now declare `scenario_graph_cache` and strict conformance validates it.
- Fixed more scenario AI graph parity: surrender, fade-out, remove-character, surprised-surrender, and kill-Bond actions now execute through public `.pdscenario` graph source backed by `ai/ailists.json` and mission graph source.
- Fixed more scenario AI graph parity: face-entity, direct damage, character-to-character damage, grenade-decision, and drop-item behavior now execute through public `.pdscenario` graph source backed by `ai/ailists.json`.
- Fixed more scenario AI graph parity: alarm/gas/hearing/sight predicates, patrol-state checks, recent target memory, injury/death observation, and route-to-target pad preset behavior now execute through public `.pdscenario` graph source backed by `ai/ailists.json`, pads, and navigation path source.
- Fixed more scenario AI graph parity: player ammo quantity, character target, chr-preset team, human, and skedar branch checks now execute through public `.pdscenario` graph source backed by `ai/ailists.json`.
- Fixed more scenario AI graph parity: miscellaneous effect and branch actions including character explosions, tinted glass, hovercopter rockets, motion blur, melee, Eyespy targeting, mini-Skedar pounce, object-to-pad distance, avoid, title mode/exit, sparks, and Dr. Caroll image state now execute through public `.pdscenario` graph source backed by `ai/ailists.json`, pads, and setup object source where needed.
- Fixed large extracted Scenario pad tables such as Maians SOS so public `pads.json` compiles through native source-owned wide offsets instead of failing and trying to fall back to ROM pads.
- Fixed Scenario AI cover actions so graph-backed cover search, movement, sight checks, facing, danger-cover, release, and retreat paths validate the source-built runtime cover table before using cover state, with `cover_rows=<n>` diagnostics tied to public `navigation/covers.json`.
- Fixed Scenario quadrant pad-preset actions so graph-backed waypoint quadrant checks validate source-built pad, waypoint, and waygroup runtime rows before using navigation helpers, with row-count diagnostics tied to public Scenario source.
- Fixed live Scenario pad-backed AI actions so character move-to-pad and lighting commands validate resolved runtime pad rows before using pad helpers, with retained source-matrix proof of `pad_rows=<n>` from public `pads.json`.
- Fixed Scenario pad-backed distance predicates so graph-backed character/target-to-pad checks validate runtime pad rows before calling distance helpers, with retained source-matrix proof tied to public `pads.json`.
- Fixed more Scenario pad-backed AI commands so pad preset, go-to-preset, warp/spawn, object-distance, and same-floor distance paths validate runtime pad rows from public `pads.json`, with retained Extra25 source-matrix proof for live go-to-preset and spawn commands.
- Fixed Scenario direct pad operands so object-to-pad movement, alarm start pads, Investigation terminal pad presets, and `PAD_PRESET` distance operands validate resolved source-built pad rows, with retained Extra16 proof for object movement and target-pad distance.
- Fixed Scenario lift AI commands so lift stop pads, current/aim lift pads, and lift numbers validate against source-built `pads.json` rows before legacy lift helpers run, with retained CI Training proof for live lift activation.
- Fixed Scenario route-to-target pad preset selection so graph-backed route helpers validate source-built pad, waypoint, and waygroup rows, then validate the selected pad preset before continuing.
- Fixed Scenario object-tag AI commands so object interaction, object flags, doors, model-part visibility, and object-health paths validate source-built setup object rows before using tag lookups.
- Fixed more Scenario object-tag AI commands so room/object/weapon predicates, Investigation terminal selection, lift activators, object-backed audio, setup image/animation/door actions, object lifecycle, object-room/perception, inventory grab, and autogun target-team paths validate source-built setup object rows before using tag lookups.
- Fixed Scenario setup-tag placement copying so Investigation terminal shuffling uses the guarded source-built setup-tag helper instead of the old raw tag-copy path.
- Fixed Scenario setup-link registration so source graph failures stop linked guns, lift doors, safe items, padlocked doors, conditional scenery, and blocked paths before live link creation.
- Fixed custom `.pdweapon` first-person held-model loading so custom weapon IDs resolve their source-owned `.pdmesh` model instead of falling back to a legacy weapon model.
- Fixed the Needler sample mod to ship a distinct held weapon source model separate from the projectile needle mesh.
- Fixed the weapon archive source smoke timing so cold source extraction/loading owns the c3844 pass/fail result instead of an early scripted exit.
