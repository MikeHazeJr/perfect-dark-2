# Asset Pipeline Full-Chain Audit — 2026-07-30

Scope: ROM/native extraction, public typed `*.pdxxx` source archives,
descriptor/manifest fidelity, catalog/provider registration, runtime
activation, production gameplay/render/audio use, creator editing, and
failure behavior.

Truth rules:

- `implemented` means the public source is connected to a real production
  consumer.
- `validated` requires durable passing evidence beyond source presence.
- A catalog row, provider handle, preview, generic runtime binding, or private
  cache by itself is not production utilization.
- A selected public source may not fall through to ROM, a native mirror, a
  loose extracted cache, or an opaque authored payload.

## Current full-chain matrix

| Workbench | Family | Extracted public source | Catalog/provider | Production use | Creator flow | Current truth / open gap |
|---|---|---|---|---|---|---|
| A-ASSETS-002 | `.pdweapon` | Split held graphs, settings, variables, presentation, optional model/dependency closure | Weapon walker, catalog slots, FileProvider, weapon graph activation | Held graph execution and model/runtime slot integration are live | Hub graph/template/editor paths | **partial** — optional presentation/dependency fields and arbitrary settings still include stored-only/ignored cases; do not call the whole advertised contract complete |
| A-ASSETS-003 | `.pdprojectile` | Behavior graph, visual dependency, transition entity | Weapon/dependency walker and graph runtime | Custom projectile spawn, timers, impact, trail, pickup and related graph records feed `propobj.c` | Typed archive and graph templates | **implemented, validation pending** |
| A-ASSETS-004 | `.pdentity` | Behavior graph, archetype, visual dependency | Weapon/dependency walker and graph runtime | Deployed/sticky-device graph records feed weapon/prop lifecycle paths | Typed archive and graph templates | **implemented, validation pending** |
| A-ASSETS-005 | `.pdmaterial` | Synthetic material preset today | Metadata walker + generic binding | No standalone material binding consumer found | Generic descriptor editor only | **missing (B-959)** — emitted PBR-like values are not extracted native combiner/material state and do not drive rendering |
| A-ASSETS-006 | `.pdtexture` | Editable image plus descriptor/manifest metadata | Texture walker, FileProvider, texture slot/compiler path | Runtime texture source/decoder path is live and now fails closed | Hub preview/import/editor paths | **implemented, validation pending** |
| A-ASSETS-007 | `.pdcharacter` | Body/head archive refs and portrait | Metadata walker + model activation | Body model source reaches model loading | Hub template/scale tools | **partial** — no complete production use was found for the character-level head and portrait contract |
| A-ASSETS-008 | `.pdhead` | Mesh source, rig class, selector metadata | Head walker, private slots, manager | MP selector, compatibility and model loading consume the row | Hub model/scale flow | **implemented, validation pending** |
| A-ASSETS-009 | `.pdbody` | Mesh, optional hand mesh, rig/default-head/selector metadata | Body walker, private slots, manager | MP selector, compatibility, body allocation and model loading consume the row | Hub model/scale flow | **implemented, validation pending** |
| A-ASSETS-010 | `.pdarena` | Arena wrapper plus embedded Scenario dependency | Arena walker/manager and Scenario dependency registration | MP arena selection and stage/Scenario loading consume it | Hub arena flow | **implemented, validation pending** |
| A-ASSETS-011 | `.pdscenario` | Scene, collision, rooms, portals, pads, spawns, volumes, objects, setup fields, AI, objectives and navigation source | Scenario walker, compiler, catalog lifecycle | Stage geometry/collision and `scenario_source_runtime` consume the public tables | Hub/Forge/source tools | **implemented, validation pending**; this is the strongest comprehensive family and remains the reference pattern |
| A-ASSETS-012 | `.pdmesh` | OBJ/MTL plus readable hierarchy/parts/faces/render JSON | Mesh walker, model compiler and private cache | Model and world rendering consume compiled public source | Hub mesh selection/scale/preview | **implemented, validation pending** |
| A-ASSETS-013 | `.pdanim` | Editable character animation or readable weapon commands | Animation walker/compiler, animation slots | Animation table/clip and weapon command playback consume it | Hub animation tools | **implemented, validation pending** |
| A-ASSETS-014 | `.pdsfx` | PCM source plus complete keymap, loop, envelope, pan/volume metadata | Audio walker/slots and FileProvider | Native sound start/mixer consumes file plus playback metadata | Hub audio tools | **implemented, validation pending**; B-956/B-960 fixed omission/fallback classes |
| A-ASSETS-015 | `.pdvoice` | Same audio envelope plus actor/transcript/language metadata | Voice walker/slots and FileProvider | Audio playback fields are live | Hub audio tools | **partial** — playback is implemented, but no production dialogue/localization consumer was identified for actor/transcript/language metadata |
| A-ASSETS-016 | `.pdsong` | Editable music/MIDI source and track metadata | Song walker/slots and FileProvider | Music selection, sequence compilation or streaming playback consume it | Hub audio tools | **implemented, validation pending** |
| A-ASSETS-017 | `.pdui` | Editable image, dimensions and nine-slice metadata | UI walker/catalog and theme UI iterator | ImGui/theme texture path consumes public UI assets | Theme/Hub preview and editor | **implemented, validation pending** |
| A-ASSETS-018 | `.pdfont` | Glyph atlas plus metrics/kerning source | Font walker and `fontCatalogBuildFace` | Text renderer builds segment-shaped runtime font from public source | Hub/font manager | **implemented, validation pending**; B-960 removed ROM fallback |
| A-ASSETS-019 | `.pdlang` | Editable `strings.json` bank | Language walker and `langManifest` | `langLoad` consumes catalog source and already fails closed | Hub language editing | **implemented, validation pending** |
| A-ASSETS-020 | `.pdskin` | One synthetic default material slot and white swatch today | Metadata walker + generic binding; editor can save texture rows | No character/model renderer skin-binding consumer found | Skin editor exists | **missing (B-959)** — editor/catalog presence is not runtime skin utilization |
| A-ASSETS-021 | `.pdeffect` | Effect graph/timeline and metadata | Effect walker and effect graph runtime | Effect graph references reach gameplay/render bridges | Hub effect source | **partial** — production bridges exist, but parity-backend/fallback paths and full shader/timeline semantics still require closure |
| A-ASSETS-022 | `.pdprop` | Synthetic archetype JSON, model/behavior refs, flags/health | Metadata walker + generic binding; model source can load | Model path and selected type metadata have consumers | Generic descriptor editor | **partial (B-959)** — health, flags and behavior source do not form a complete standalone prop production adapter |
| A-ASSETS-023 | `.pdvehicle` | Model plus placeholder physics and three-node behavior graph | Metadata walker + generic binding; model source can load | Vehicle visual model can resolve | Generic descriptor editor | **missing (B-959)** — physics and behavior files are not executed by the hoverbike runtime |
| A-ASSETS-024 | `.pdmission` | Scenario dependency, objectives/mission graph, placeholder briefing | Metadata walker + generic binding | Scenario owns substantial mission execution, but no standalone `ASSET_MISSION` selector/runtime consumer was found | Generic descriptor editor | **partial (B-959)** — mission wrapper/briefing is not authoritative campaign source |
| A-ASSETS-025 | `.pdgamemode` | Selector metadata plus OG-backend rules wrapper | Metadata walker + generic binding | Team flag reaches selector | Generic descriptor editor | **partial (B-953/B-959)** — name/description, player bounds and rules source are not comprehensively consumed; missing bindings still fall back |
| A-ASSETS-026 | `.pdbotprofile` | Selector mirrors plus OG-backend profile wrapper | Metadata walker + generic binding | Permanent profile ID now reaches both menus, bot/match runtime, saves, manifests, and v52 wire; traits/body derive from the readable binding | Archive-aware descriptor editor; live interaction proof pending | **partial (B-959; B-961 implemented)** — identity no longer collapses to native indices, but `profile.json` is still not a comprehensive creator-authored behavior/tuning source |
| A-ASSETS-027 | `.pdhud` | Empty `slots` array plus renderer label today | Metadata walker + generic binding | No HUD renderer consumer of the binding/layout was found | Generic descriptor editor | **missing (B-959)** |
| A-ASSETS-028 | `.pdtheme` | Theme style/tokens plus optional dependencies | Theme walker/catalog and theme loader | Theme JSON and catalog UI assets drive ImGui styling | Theme editor and Hub | **partial** — direct style loading is live; dependency archives are not yet all authoritative runtime inputs and missing bindings fall back |

## Cross-cutting defects found in this audit

- **B-954:** the creator editor invented loose descriptor paths and could not
  edit standalone typed archives.
- **B-955:** archive replacement deleted the good destination before rename.
- **B-956:** the shared descriptor parser silently omitted fields beyond 32.
- **B-957:** boot discarded extraction/walker failure results and extractor-only
  always returned success.
- **B-958:** most typed emitters logged partial failures but still returned
  success.
- **B-959:** placeholder public source and generic-binding-only runtime claims
  across metadata families.
- **B-960:** runtime fallback correctness depended on optional debug mode.
- **B-961:** catalog-backed bot-profile selection discarded custom identity at
  `mp_index = -1`. The permanent ID now survives UI, runtime/match state,
  v3 setup migration, manifests, and v52 network reconstruction; live custom
  profile interaction/network/gameplay proof remains.
- **B-964:** MP setup JSON dual-wrote catalog weapon IDs and a later numeric
  mirror that overwrote them on load; writers now emit one catalog-native
  representation and old numbers are migration-only.
- **B-965:** binary MP setup IO accepted truncated, future-version, and invalid
  index state; exact IO and schema/index validation now reject partial files.

## Required closure sequence

1. Preserve the verified archive-integrity and fail-closed fixes with full build,
   guard, and runtime receipts.
2. Complete B-953 for game-mode metadata/rules without a native mirror fallback.
3. Split B-959 into per-family source-fidelity/runtime slices, starting with
   HUD, material/skin, vehicle/prop, mission, and bot profile.
4. Close residual partial fields in weapon, character, voice, effect and theme.
5. Run the menu/input/glyph sweep and live MKB/controller creator workflows.
6. Mark a family validated only after an edited public source demonstrably
   changes its production behavior and the evidence is stored durably.
