# Tasks

> Live punch list only. Completed implementation narratives belong in
> [session-log.md](session-log.md). Historical card and bug detail remains in the
> Kanban card history and bug ledger.

Last updated: 2026-06-10

---

## Active Critical Path

| Card | Status | Purpose |
|------|--------|---------|
| `c3844` | Active | Finish all-asset source/runtime parity to 100%. |
| `c3848` | Active | Custom-model runtime-slot allocator for embedded weapon meshes (B-911), under the c3844 umbrella. |
| `c3849` | Active (Waves 1-6 DONE; Wave 7 staged to B-801) | Catalog 100% utilization program, under the c3844 umbrella. |

**c3849 status (2026-06-10 evening):** Waves 1-6 are SHIPPED and build-verified
(telemetry; the four allocators; FONT consumer; texture emitter + Slice B bind
verification/slug unification; ALL Wave-5 dead-IR consumers per binding specs
B1-B8; Wave-6a meta-family consumers botprofile/gamemode/theme; Wave-6b full
.pdeffect runtime + pink-spark registry). Everything gameplay-facing is dormant
behind `Debug.WeaponGraphRuntime`. Wave 7 is the ONLY remainder and is
deliberately staged to the B-801 live gate (Mike's call): toggle default flip,
per-family fatal cutover (ASSET.FALLBACK telemetry is the instrument), strict MP
mismatch refusal (protocol bump), toggle retirement. Maps + binding specs:
`context/designs/catalog/c3849-wave-implementation-maps.md`. Bug ledger:
B-915/916/917/918 fixed, B-919 open (verify-before-fix decomp quirk).

`c3844` has been retitled **Asset Pipeline: 100% source/runtime parity closure**.
This is no longer a broad archive-format migration. The remaining work is to prove
and close the final runtime, stale-output, and tool workflow gates.

`c3848` (opened 2026-06-10 from the Needler proof-of-need) is parity work surfaced by
B-911: a custom `.pdweapon`'s embedded `.pdmesh` referenced by a catalog-ID `model_ref`
never reached the catalog and had no runtime slot, so custom weapon meshes did not
render. Slices 1-3 are IMPLEMENTED and BUILD-VERIFIED (2026-06-10: client `-Target all`
PASS, empty err; `[catalog][model][slots]` + `[modding][pdxxx][model_slots]` PASS 119
assertions / 10 cases; regression band 173/178 with the 5 failures pre-existing pins on
files untouched since `23914afb`): foundation `9063004c` (slot allocator + additive
`g_ModelStates` growth), ingest consumer (pure embedded-mesh scan + dependency-walk
wiring binding a multi-level `::` source chain -- `fs.c` resolves N levels for loose
on-disk archives, so no disk extraction), and the CPU pins. The related Needler gameplay
halves (B-914 homing widen, B-912 impact first slice, spawn functions bridge, c3847)
are also build-verified. Remaining: only s4, the B-801-gated live render proof (enable
`Debug.WeaponGraphRuntime` at live-test time).

---

## c3844 Completion Gates

1. **B-801 safe live visual/audio proof**
   - Build or use bounded CPU-safe probes first because prior live tests made the PC unresponsive.
   - Then run narrow live proof for Scenario visuals, character/body/head visuals, weapons/props, textures/materials, animations, SFX/voice/music.
   - Enable only relevant logging and inspect the logs after each run.
   - 2026-06-09: Scenario CPU pre-flight is now repeatable. `build-session.ps1 -Target probe` builds `scenario-scene-probe`; `devtools/scenario-scene-probe-sweep.ps1 -Session <id>` probes every `.pdscenario` (87/87 green on the fresh tree) with no window/GPU/audio. Remaining CPU pre-flight gaps before any live pass: standalone anim/mesh/audio decode probes for the non-scenario families. The narrow live pass itself remains the only live-blocked step.

2. **Custom body/head full runtime equivalence**
   - B-904, B-905, and B-906 removed wrong slot-zero and selector-cache failures.
   - Remaining body/head work is the final slot-indexed runtime boundary: add direct catalog-ID consumers or catalog-owned private body/head runtime allocation.
   - Do not expose numeric body/head slots to modders.
   - 2026-06-09: FULLY DESIGNED. See `context/designs/modding/body-head-private-slot-allocator.md`.
   - 2026-06-10 (B-909): IMPLEMENTED per Option B. `CATALOG_MGR_{BODY,HEAD}_CUSTOM_*`/`_TOTAL` constants (manager + pure headers, `_Static_assert` lockstep); arrays/bounds grown to TOTAL while population/random-gender loops stay at base 152; new `assetcatalog_body_head_slots.c` allocator (dedup by id, loud `CATALOG.{BODY,HEAD}.CUSTOM_SLOT_FAIL` on exhaustion); `loaderPoolParse{Body,Head}JsonForSlot` forced-slot parse; walker wiring (allocate slot + set `runtime_index` + forced-slot parse); reset beside the weapon-slot reset. Verified: client `-Target all` PASS (26s); `[catalog][bodyhead][slots]` + manager bound tests PASS (438 assertions / 51 cases); `[catalog][provider][static]` no regression; guard + examples conformance PASS. The ONLY remaining step is the live custom-body render proof, which is part of the B-801 live-proof gate (subtask 1), not a separate implementation gap. A custom-body CPU probe (character assembly) could be added before the live pass.

3. **Scenario AI graph/runtime fallback closure**
   - Continue retiring remaining Scenario AI command graph/fallback debt module by module.
   - Public Scenario JSON/GLB source remains the runtime source.
   - Fallback to native/ROM behavior after extraction must fail loudly.
   - 2026-06-09: producer side is fully wired (all `scenarioSourceAiGraphExecute*` reach the loud-fail guard). Hardened `scenarioSourceAiGraphExecuteChrDoAnimation` to handle the require-node tri-state explicitly (was correct only by accident of a downstream NULL-resolve).
   - 2026-06-09 (VERIFIED correction of the gate audit): the objectives and trigger-volume consumers are ALREADY at the correct enforced-under-debug posture. `objectiveCheckGraphSource` routes every mismatch through `scenarioSourceObjectiveGraphReportRuntimeMismatch` -> `s_missionObjectiveGraphRuntimeFailure`, which `sysFatalError`s under `ASSET_MISSION` source-only enforcement; `s_levelGraphVolumeRuntimeFailure` likewise `sysFatalError`s under `ASSET_SCENARIO`. So under enforcement the OG fallback never runs (it fataled first). The OG fallback in `objectiveCheck`/`objectiveCheckRoomEntered`/`objectiveCheckThrowInRoom` only executes in NORMAL play, where it is the SAFE parity behavior; flipping it to always-fatal there would brick campaign stages whose graph node set is not yet proven complete. Do NOT make that flip without a per-stage graph-completeness proof. Genuine remaining gate-3 work: (a) prove graph node-set completeness across all campaign stages so the normal-play flip is safe, and (b) make `lvTick` global level behavior graph-sourced (record-only today). Both are large and need live verification (paused under B-801).

4. **Regenerate and validate stale extracted output**
   - Run a safe regeneration or repair pass for retained generated installs.
   - Known stale focus: old `.pdanim` v3 archives and older metadata archives.
   - Then run whole-tree strict conformance before calling the archive set current.

5. **Private integer bridge closure audit**
   - Audit remaining modelnums, filenums, sound slots, texture numbers, body/head slots, music slots, and weapon slots.
   - Each bridge must be catalog-owned and loud-failing, or replaced with direct catalog-ID runtime consumption.
   - Integer bridge details remain private migration debt, never public archive identity.
   - 2026-06-09 (B-907): closed a conformance gap where the JSON ref scanner accepted numeric/legacy values in catalog-ID keys (`model_catalog_id`, etc.) that the delimited scanner already rejected; the public-archive numeric-leak guard is now symmetric across JSON and delimited source. `tools/asset_archive_conformance.py --selftest` pins it.
   - 2026-06-09 (B-908): closed the write-only `g_CatalogFailure` flag. The catalog `*ByIndex`/modelnum load helpers set it on a miss but nothing consumed it (silent default substitution). Added `catalogAssertHealthy()` (consumer, fatal-under-enforcement via pure `catalogHealthShouldFatal`), wired at `lv.c` stage-load entry.
   - 2026-06-10 (B-908 follow-up DONE): the 10 in-range-unregistered body/head field accessors now route through `s_bodyFieldRecordChecked`/`s_headFieldRecordChecked` and set `g_CatalogFailure` on a genuine miss (discriminator `filenum!=0 OR catalog_id set`, so B-909 custom assets are not mis-flagged). Verified: client build PASS; `[catalog][checked]` PASS. Remaining gate-5: weapon/music slot exhaustion already loud + leak no identity (acceptable private debt); fatal-under-enforcement wants a source-gate smoke confirm.

6. **End-to-end modder workflow proof**
   - Prove user-created external geometry, audio, animation, materials, and gameplay assets can import, validate, package, distribute, load, render/play/animate, and be edited again.
   - Base content and mods must use the same public archive contract and runtime rules.

---

## Deferred From Active

The following cards were removed from the Active lane on 2026-06-09 because they are
not part of the current all-asset 100% completion path. They remain on the board.

Moved to Done because their subtasks were complete:

- `c3836` - Kanban: remote phone board and card sessions
- `c3816` - Input: custom/accessibility controller class + glyph/social foundation
- `c3819` - Input: Settings Input tab rebuild and controller profile assignment
- `c133` - B-345: campaign mission starts to black screen after menu preview
- `c134` - B-346: credits particles/text and fog planes render as solid squares
- `c3829` - B-365: Infiltration robot attack exception
- `c3830` - B-366: Falcon 2 OBJ and in-game barrel stretch
- `c3831` - B-368: Match restart exception after leaving

Moved to Backlog / deferred:

- `c3840` - Weapon Graph: modularize OG behavior routines after parity
- `c3815` - B-356: Combat Sim post-match screen visible and interactive
- `c136` - B-350: airborne side-entry through overhead collision blockers
- `c083` - B-316: Combat Sim start crash in botJumpDecide
- `c3823` - Startup extraction fast-cache and centered progress modal
- `cmpbhfdir2lxo` - Full Campaign Auto-Runner with Unlocks and Social Syncing
- `c3746` - Player init architectural fixes
- `c029` - Benchmarking: speed tune + B-307 256-bot crash
- `c3807` - GPU swarm benchmark debug tab (Phase 2)
- `c3738` - Skedar surface-normal locomotion (Slices 4-5)
- `c028` - Pre-existing test failure triage
- `c031` - Benchmarking: GPU bot behavior pipeline
- `c3826` - B-361: Main Menu Esc/title X root close reopens
- `c3827` - B-362: Falcon 2 mission-start beam stretches to center

---

## Required Verification Pattern

For code changes under `c3844`:

1. Run `python tools\asset_native_source_guard.py`.
2. Run focused tests for the touched asset family or runtime surface.
3. Run relevant conformance or smoke/matrix checks.
4. Run an isolated build with `.\devtools\build-session.ps1 -Session <id> -Target all`.
5. Remove the isolated build directory with `.\devtools\build-session.ps1 -Remove -Session <id>`.
6. Update this file, the relevant pillar doc, `context/session-log.md`, and `UNRELEASED.md` if behavior changed.

For context/Kanban-only changes, JSON parse plus targeted diff/consistency checks are sufficient.

---

## Current Guardrails

- Public typed archives are the game-facing source.
- Base content and mods use the same contract.
- No public TSV files.
- No public `.bin` or native dump payloads as modder-facing source.
- No numeric asset identity in public archive fields, manifests, saves, config, wire, UI, or mod tools.
- Runtime ROM/RomProvider fallback after extraction is an asset-chain failure.
- Private renderer/GPU/audio/collision/animation/graph products are source-hashed cache only.
- Integer-only runtime limitations are private migration debt and must not leak into authored source.
