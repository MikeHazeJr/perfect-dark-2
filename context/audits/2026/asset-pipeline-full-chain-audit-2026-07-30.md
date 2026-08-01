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
| A-ASSETS-005 | `.pdmaterial` | Canonical base material preset with editable base color, emissive, roughness and metallic | Metadata walker hydrates and validates `material.json`; invalid source deactivates the row | Character rendering consumes the bound material through skin appearance composition | Archive-aware descriptor editor plus readable JSON | **implemented (B-959 slice), validation pending** — focused source mutation/behavior, strict conformance and build pass; live edited render receipt remains |
| A-ASSETS-006 | `.pdtexture` | Editable image plus descriptor/manifest metadata | Texture walker, FileProvider, texture slot/compiler path | Runtime texture source/decoder path is live and now fails closed | Hub preview/import/editor paths | **implemented, validation pending** |
| A-ASSETS-007 | `.pdcharacter` | Body/head archive refs and portrait | Metadata walker + model activation | Body model source reaches model loading | Hub template/scale tools | **partial** — no complete production use was found for the character-level head and portrait contract |
| A-ASSETS-008 | `.pdhead` | Mesh source, rig class, selector metadata | Head walker, private slots, manager | MP selector, compatibility and model loading consume the row | Hub model/scale flow | **implemented, validation pending** |
| A-ASSETS-009 | `.pdbody` | Mesh, optional hand mesh, rig/default-head/selector metadata | Body walker, private slots, manager | MP selector, compatibility, body allocation and model loading consume the row | Hub model/scale flow | **implemented, validation pending** |
| A-ASSETS-010 | `.pdarena` | Arena wrapper plus embedded Scenario dependency | Arena walker/manager and Scenario dependency registration | MP arena selection and stage/Scenario loading consume it | Hub arena flow | **implemented, validation pending** |
| A-ASSETS-011 | `.pdscenario` | Scene, collision, rooms, portals, pads, spawns, volumes, objects, setup fields, AI, objectives and navigation source | Scenario walker, compiler, catalog lifecycle | Stage geometry/collision and `scenario_source_runtime` consume the public tables | Hub/Forge/source tools | **implemented, validation pending**; this is the strongest comprehensive family and remains the reference pattern |
| A-ASSETS-012 | `.pdmesh` | OBJ/MTL plus readable hierarchy/parts/faces/render JSON | Mesh walker, model compiler and private cache | Model and world rendering consume compiled public source | Hub mesh selection/scale/preview | **implemented; creator workflow live-smoke validated** — B-969 fixed the render-stream material/null-triangle crash; rebuilt all-family `.pdmod` smoke passes 58/58 |
| A-ASSETS-013 | `.pdanim` | Editable character animation or readable weapon commands | Animation walker/compiler, animation slots | Animation table/clip and weapon command playback consume it | Hub animation tools | **implemented, validation pending** |
| A-ASSETS-014 | `.pdsfx` | PCM source plus complete keymap, loop, envelope, pan/volume metadata | Audio walker/slots and FileProvider | Native sound start/mixer consumes file plus playback metadata | Hub audio tools | **implemented, validation pending**; B-956/B-960 fixed omission/fallback classes |
| A-ASSETS-015 | `.pdvoice` | Same audio envelope plus actor/transcript/language metadata | Voice walker/slots and FileProvider | Audio playback fields are live | Hub audio tools | **partial** — playback is implemented, but no production dialogue/localization consumer was identified for actor/transcript/language metadata |
| A-ASSETS-016 | `.pdsong` | Editable music/MIDI source and track metadata | Song walker/slots and FileProvider | Music selection, sequence compilation or streaming playback consume it | Hub audio tools | **implemented, validation pending** |
| A-ASSETS-017 | `.pdui` | Editable image, dimensions and nine-slice metadata | UI walker/catalog and theme UI iterator | ImGui/theme texture path consumes public UI assets | Theme/Hub preview and editor | **implemented, validation pending** |
| A-ASSETS-018 | `.pdfont` | Glyph atlas plus metrics/kerning source | Font walker and `fontCatalogBuildFace` | Text renderer builds segment-shaped runtime font from public source | Hub/font manager | **implemented, validation pending**; B-960 removed ROM fallback |
| A-ASSETS-019 | `.pdlang` | Editable `strings.json` bank | Language walker and `langManifest` | `langLoad` consumes catalog source and already fails closed | Hub language editing | **implemented, validation pending** |
| A-ASSETS-020 | `.pdskin` | Editable target-body material slots and swatches | Metadata walker hydrates `skin.json` plus `swatches.json`, validates catalog identity, and resolves the material binding | Character rendering composes material color and swatch alpha plus material shading controls | Skin editor plus archive-aware source editing | **implemented (B-959 slice), validation pending** — mutation/behavior and strict-schema proof pass; live body-appearance receipt remains |
| A-ASSETS-021 | `.pdeffect` | Effect graph/timeline and metadata | Effect walker and effect graph runtime | Effect graph references reach gameplay/render bridges | Hub effect source | **partial** — production bridges exist, but parity-backend/fallback paths and full shader/timeline semantics still require closure |
| A-ASSETS-022 | `.pdprop` | Editable v2 archetype, display name, health and flags plus model/behavior refs | Metadata walker hydrates and validates `prop.json`; model source can load | Forge door/prop dispatch, max damage and flags consume hydrated source and fail closed | Archive-aware descriptor/JSON editing | **partial (B-959)** — core state is production-connected with focused proof; arbitrary `behavior.graph.json` execution remains open |
| A-ASSETS-023 | `.pdvehicle` | Model plus complete named hoverbike movement, hover and mount/drive/dismount policy source | Metadata walker hydrates both source documents and rejects invalid/missing bindings | Real hoverbike movement, collision bob, hover tick, mount, drive and dismount paths consume the hydrated values and fail closed | Archive-aware descriptor/JSON editing | **implemented (B-959 slice), validation pending** — focused source mutation/behavior and compile proof pass; live tuned-vehicle receipt remains |
| A-ASSETS-024 | `.pdmission` | Scenario dependency, objectives/mission graph, plus a redundant placeholder briefing document | Mission metadata walker, `scenario_source_runtime` mission binding and Scenario dependency registration | The selected `ASSET_MISSION` graph drives mission activation, objective source rows, phase transitions and parity-backend dispatch; the nested Scenario reconstructs setup briefing/objective records consumed by `setupLoadBriefing` | Generic descriptor editor | **implemented for graph/objective/phase execution; partial public contract remains** — the duplicate `briefing.json` member is not the briefing authority and must not be presented as independently editable runtime text |
| A-ASSETS-025 | `.pdgamemode` | Editable v2 mode, display text, participant bounds, team requirement and unlock feature | Metadata walker hydrates/validates `rules.json` and rejects invalid rows | Production menus and selector consume hydrated source without descriptor/native fallback | Archive-aware descriptor/JSON editing | **implemented (B-953/B-959), validation pending** — focused mutation/runtime and compile proof pass; live MKB/controller selection receipt remains |
| A-ASSETS-026 | `.pdbotprofile` | Editable v2 bot type, difficulty, target body and unlock feature | Metadata walker hydrates/validates `profile.json`; permanent ID survives save/wire | Menus, bot creation, match state, saves, manifests and v52 wire consume identity and hydrated traits/body | Archive-aware descriptor/JSON editing | **implemented (B-959/B-961), validation pending** — focused mutation/runtime and compile proof pass; live custom-profile/network receipt remains |
| A-ASSETS-027 | `.pdhud` | Editable element identity, visibility and score/timer opacity | Metadata walker hydrates and validates `layout.json` | Ammo, crosshair, health, radar, score and timer production render paths consume the binding | Archive-aware descriptor/JSON editing | **implemented (B-959 slice), validation pending** — all six consumers are statically pinned and focused runtime hydration passes; live HUD edit receipt remains |
| A-ASSETS-028 | `.pdtheme` | Theme style/tokens plus optional dependencies | Theme walker/catalog and theme loader | Theme JSON and catalog UI assets drive ImGui styling | Theme editor, Hub, and corrected creator example | **partial** — B-971 fixed the example's ignored `accent`/`chrome` pseudo-schema; direct palette/style loading is live, but dependency archives are not yet all authoritative runtime inputs and missing bindings fall back |

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
  across metadata families. HUD/material/skin/vehicle are now implemented with
  strict structured schemas and production consumers; the remaining named
  families and live edited-source receipts stay open. Game mode and bot profile
  are now implemented from their JSON; prop core state is connected while its
  arbitrary behavior graph remains partial.
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
- **B-970:** the scheduled daily-flow entrypoint retained a callable retired
  tracker mutation/merge/push pipeline despite Workbench being the nominal
  main path; it is now a Workbench-only read-only briefing exporter.
- **B-971:** the creator theme example used plausible but ignored JSON keys;
  it now emits the exact production theme identity and palette schema.

## Required closure sequence

1. Preserve the verified archive-integrity and fail-closed fixes with full build,
   guard, and runtime receipts. Baseline gates are `V-001` and `V-002`; induced
   failure proof remains `V-006`.
2. Ultra closes weapon (`T-ASSETS-009`), effect (`T-ASSETS-012`), prop behavior
   (`T-ASSETS-013`), and mission briefing authority (`T-ASSETS-014`).
3. Lina closes character head/portrait (`T-ASSETS-010`), voice metadata
   (`T-ASSETS-011`), and theme dependencies (`T-ASSETS-015`).
4. Lina runs physical menu/input/glyph proof (`V-004`), live creator workflow
   proof (`V-007`), and custom game-mode/bot-profile save/listen-host proof
   (`V-008`).
5. Ultra completes the 27-family edited-source production matrix (`V-005`),
   folds the subordinate gates into aggregate runtime validation (`V-003`),
   and records cold/warm performance measurements and budgets (`P-001`).
6. Mark a family validated only after an edited public source demonstrably
   changes its production behavior and the evidence is stored durably.
