# Asset, modding, and executable graph goal: closure plan

Status: execution plan, 2026-09-23. This is the user's full asset goal, not the
narrower D-004 1.0 release sample. The authoritative task states remain in the
Workbench. Source and test evidence must be attached to those items as work
lands. D-006 option A is decided: authored graph events, connections, gates,
state, and parameters drive reusable native modules.

## Finish line

The goal is complete only when all of the following hold on one source-frozen
revision and a fresh portable install:

1. Every available base-game source record in the active ROM's authoritative
   tables has a public editable typed archive (including nested content and
   regional variants). An independent inventory and semantic round-trip report
   names every source, emitted archive, count/hash, accepted omission, and
   unmatched record. Zero unexplained omissions or lossy fields remain.
2. All 27 public asset families use their edited public source through ordinary
   catalog/provider loading. The V-005 matrix has one controlled source edit,
   observed production change, rollback/retirement check, and retained artifact
   per family. Runtime ROM/RomProvider fallback after extraction is zero.
3. Standard authoring inputs named in the public guide (JSON/INI, PNG, WAV,
   MP3/OGG, OBJ/MTL, glTF/GLB, and supported font formats) preserve the
   semantics the importer advertises. Unsupported standard features fail
   clearly; they are never silently dropped. Generated engine products are
   source-hashed cache only.
4. Base and custom weapon, projectile, and entity behavior use executable
   modular graphs in the production gameplay path. The source graph determines
   event routing, gates, selected native actions, state, and lifecycle. No
   weapon-number or custom-slot bypass silently replaces graph authority.
5. The requested creator pack works from the same public source path: Needler-
   like weapon, Ghost-like hover vehicle with alternating blasters, Warthog-
   like vehicle with driver/passenger/turret roles, and four distinct character
   archetypes (Master Chief, Sergeant Johnson, Brute, Grunt inspirations).
   It is editable, packageable, installable, and usable in an ordinary match.
6. Source, build, focused tests, fresh extraction, installed gameplay, network,
   graphics, audio, controller, and human visual evidence are reported as
   separate gates. A green source test does not stand in for play acceptance.

## Ordered work packages

| Package | Production work | Exit gate and Workbench owner |
| --- | --- | --- |
| 0. Settle and inventory | Freeze the current canonical source; reconcile retained work from other sessions; regenerate a machine-readable ledger of active-ROM files/segments, public archives, nested members, catalog entries, and production consumers. Mark every family/field as direct, converted, missing, or intentionally excluded with rationale. Keep the existing 27-family audit and V-005 as one matrix, not a new parallel tracker. | Inventory covers all active-ROM records and all 27 families; every gap has an owning Workbench item. T-ASSETS-001. |
| 1. Lossless extraction | Close field-level gaps in each family, not just archive-count gaps. Finish regional `.pdlang` and Japanese codec/glyph provenance, Japanese font sources, base MP3/voice/music reverse mappings, character/head/body identity, complete glTF materials/UV/normals/colors/skins/animation semantics, and remaining graph constants. Preserve original empty/null/variant semantics. Extractors resolve active-ROM identities, not assumed enum adjacency. | Fresh clean extraction plus independent source-to-public semantic parity and archive conformance. Every emitted source can be edited and re-extracted without destroying edits outside its owned fields. T-EXTRACTION-001, T-ASSETS-042/046/048/049, and new bounded gaps as discovered. |
| 2. One runtime authority | For each family, bind public source through provider/catalog prepare, validate, commit, rollback, replacement, and final detach. Keep source-generation leases until all users retire. Remove post-extraction ROM/provider escape paths and hidden native mirrors. Use source-hashed private caches for costly conversion. | V-005 controlled edit and negative/retirement proof for each of 27 families; V-003 can then be validated. T-RUNTIME-001 and V-006. |
| 3. Standard creator flow | Make the authoring guide and actual import/export/save/pack paths agree. Preserve nested archive members, archive-qualified dependency paths, and standard media source bytes. Finish font/glTF/audio import limits, stable semantic IDs, mod overlay priority, live reload, clear diagnostics, and atomic save. | Author creates, edits, packages, imports, enables, reloads, and disables representative nested assets in the ordinary Modding Hub without losing source. V-007 plus format-specific contracts. |
| 4. Graph runtime cutover | Connect existing v2 candidate/kernel to ordinary catalog activation, equipped per-hand binding, real input/admission/attack/copy/completion, and generation retirement. Start with a base single-shot plus edited branching mod graph. Then migrate automatic/burst/beam, charge/throw/melee/device, projectile motion/contact/timers, entity/deployment, and effect/context handoff in that order. Replace native branches one complete lifecycle at a time; retain explicit versioned v1 compatibility only until its parity replacement passes. | Production graph edits alter actual attack/result, false gates cause zero side effects, hands/players/objects isolate state, native parity holds for untouched base sources, and lifecycle cancellation leaves no stale subscriptions. T-MODDING-002, D-006, V-010. |
| 5. Requested pack | Build the seven requested showcase components as real editable assets using the completed source and graph contracts. Reuse the existing Needler source only after auditing it. Ghost alternation, Warthog three seats and turret, and four character identities must be declared through the public model/animation/vehicle/graph paths. | Installed ordinary-client match proves equip/fire/projectiles/effects, Ghost steering and alternating fire, Warthog enter/exit and simultaneous driver/passenger/turret roles, character selection and appearance, save/reload, package distribution and listen-host replication. T-MODDING-009. |
| 6. Closure campaign | Run one source-frozen full corpus, focused and broad tests, native source guard, clean install, Campaign and Combat Simulator paths, mod save/import/distribution, rollback, stage transitions, long portable path, GPU/render and audio-device checks, keyboard/controller and human visual review. Record exceptions explicitly instead of converting partial evidence into a pass. | All six finish-line conditions above pass; Workbench umbrellas and V-005/V-007/V-010 reflect actual evidence. |

Packages 1-3 can advance by family in bounded source-owned batches. Package 4
depends on the selected weapon/model/audio/animation generation lifetime from
package 2, and package 5 depends on the graph/vehicle/character pathways it
exercises. A batch is complete when its emitter, importer, catalog activation,
ordinary consumer, edit/rollback test, and documentation agree. Build/test only
after a coherent batch is source-frozen, as Mike requested.

## Immediate executable sequence

Progress 2026-09-23: the NTSC-final seven-locale Latin-1 slice of step 1
passed isolated build, focused tests, actual 476-archive extraction, and
independent 476/476 ROM-to-public semantic parity. Full extract-only boot is
blocked by 221 head/body/character nested-mesh source collisions. JPN-final,
glyphs, PC locale preference, and actual UI selection remain open. The graph
cutover in step 2 remains the next production critical path.

1. Settle the unverified T-ASSETS-042 worktree edits to
   `romextract_pdlang.c`, `langmanifest.c`, `lang_source.cpp/.h`,
   `langmanifest.h`, `lang.c`, `assetcatalog_load.c`, and focused tests. They currently attempt all
   seven active-name variants where the native codec is Latin-1 and
   locale ranking. The independent Python parity suite passed 17/17, the
   native-source guard passed, and conformance selftest passed. The first
   isolated all-target build stopped on a peer-owned menu declaration error;
   this language batch has **not** passed client/test build, extraction, or
   runtime gates.
   Correct active-name resolution, source-hash reuse, preload neutrality, and
   atomic multi-bank reload before calling this language unit implemented.
   JPN-final packed text, native/ImGui glyphs, and PC locale preference remain open.
2. Connect one graph v2 held single-shot to ordinary catalog/equipped gameplay.
   This is the critical path. Use the already implemented candidate, state,
   native-module and source-generation components; do not create a third graph
   format. Prove one extracted base gun and one creator branching gun through
   actual input, gate, shot, debit, sound, animation, and retirement.
3. Complete the 27-family matrix by filling real edited-source production
   witnesses, addressing every missing consumer in batches. Prioritize
   language/fonts, standard glTF visual and animation fidelity, base audio
   replacement, character identity, vehicle occupancy, and stage scenario
   semantics because the requested pack directly depends on them.
4. Expand the graph cutover across the remaining native lifecycles, then build
   the pack and run the closure campaign. No fixed calendar date is promised;
   each package has a measured acceptance gate and may reveal additional
   source-owned gaps.

## Coordination and evidence rules

The canonical checkout and Workbench remain shared. Each implementation batch
claims exact files/Workbench item, processes targeted notes, uses the
coordination FIFO for builds/tests/extraction/smoke, and records source hashes,
binary hashes, exact test selection, output artifacts, and negative receipts.
Only one session stages/commits/pushes at a time. `implemented` requires a
connected production path; `validated` requires the stated passing gate.
Update `context/tasks.md`, the relevant pillar, and `UNRELEASED.md` in the same
code commit. The public-source guard is mandatory after asset changes.

This plan consolidates the [asset audit](../../audits/2026/asset-source-runtime-contract-2026-09-05.md),
the [graph contract](../modding/executable-behavior-graphs-2026-09.md),
the [regional-language design](regional-language-source-and-runtime-2026-09.md),
and Workbench T-ASSETS-001, T-EXTRACTION-001, T-RUNTIME-001, T-MODDING-002,
T-MODDING-009, V-003, V-005, V-006, V-007, and V-010.
