# Tasks

> Razor-thin punch list of what is open right now. Per-slice "Done this slice" narratives belong in [session-log.md](session-log.md), not here. Completed lanes get archived per [retention.md](retention.md).
>
> When in doubt: shorter is better. If a lane has stalled for > 14 days, ask whether it is genuinely active.

---

## Current critical path (per Mike, 2026-04-30)

The queue has three lanes after the context rebuild lands. Lane order is sequential; do not start the next until the previous is at a stable stopping point.

**Tooling note (2026-05-19)**: Kanban board status-tab refactor shipped. Active Kanban now displays one status at a time (`Backlogged`, `Active`, `Blocked`, `Done`) with a scoped dropdown for `All` and status-scoped pillars only. Cards flow in a responsive grid, sorted with starred cards first, then numbered priorities from highest to lowest, then the rest by board order. Daily Flow is now a top-level tab beside Bug Tracker and starts collapsed by default. Pending-completion Review panel title-focus polish remains shipped. B-336 release auto-commit hook compatibility is patched in `release.ps1` and Dev Window v2. B-337 v0.0.199 signature sidecar was re-signed/re-uploaded with the key embedded in the shipped updater; future sign attempts now fail if key/header mismatch. B-338 updater cleanup now protects root-level BYOR ROM files (`*.z64`, `*.v64`, `*.n64`) in both apply paths; already-published v0.0.199 and v0.0.200 predate the fix and should be superseded by a newer prerelease, then retired by the rolling prerelease prune. B-342 mission-start crash fix is build-verified and pending playtest: solo stage loads now route through an MP scenario stage-owner wrapper so Combat Simulator drop-in/drop-out is not gated on `normmplayerisrunning` alone. B-343 follow-up mission-start crash fix is now build/smoke-verified: dynamic prop collision meshes detach before stage-pool reset, the detach pass walks only live props, prop reset initializes `colmesh`, `mission_intro_flow` passes, and `auto_campaign_first_cycle` completes without crash/exception. B-344 F6 campaign-complete hotkey is second-pass build-verified: F6 completes all difficulty-active loaded solo campaign objectives, clears death/abort, calls `mainEndStage()`, and the final Skedar Ruins ImGui endscreen action is labeled Credits and routes through `endscreenContinue(2)` instead of Challenge/next-mission flow; the bot-freeze fallback is dev-build-only plus Combat Simulator-only.

**Tooling note (2026-05-20)**: Kanban card `c121` now has a dedicated Decision Requests top-level tab planned/implemented in this slice: it surfaces unresolved questions and Mike's answered responses from the same `open_questions[]` ledger. New sessions must check Mike's decision responses before choosing work unless the newest user message specifically overrides that flow; the CLI surface is `python tools/kanban_evaluator.py list-decision-requests`.

**Tooling note (2026-05-20, memory review)**: Kanban now has a separate `Memory Review` top-level tab backed by `tools/kanban/memory-review.json`. It seeds the 19 current Codex memory task groups from `C:\Users\mikeh\.codex\memories\MEMORY.md` and lets Mike tag each as `Keep`, `Adjust`, or `Remove`, with an adjustment note and expandable source text. This is review data only; the actual Codex memory files are untouched until Mike resets/rebuilds them from the revised set.

**Modding note (2026-05-20)**: External-format content plus `.pdmod` transport card `c3809` is done. Corrected packaging split is implemented: typed `*.pdxxx` files are the preferred content units for organization; `.pdmod` is the bundle/transport envelope for sharing, Public Mods, and online-required content delivery; authored `.bin` files are invalid in both content units and transport archives; generated engine-native cache is private, readable, rebuildable, and unshipped. Runtime validation is complete for legacy `.pd*` registration, loose typed `*.pdxxx` folder content, typed content wrapped in real `.pdmod` transport, and Public Mods installed `.pdmod` discovery/install. Final closure also fixed archive/folder digest comparison to root `mod.json` bytes and refreshes `g_ModRegistry` after received/distributed mod installs. Follow-up card `c3810` is done: request-download installs from friends hot-enable after validated install; non-friend downloads install disabled and queue a Social-shell modal to enable now or keep disabled. Focused Public Mods static tests and queued `modreq` all-target build passed.

**Modding sample/export gap (2026-05-20)**: Kanban `c3812` is active to repair c3811's wrong archive shape. Mike clarified the actual contract: each typed `*.pdxxx` file is itself the modder-facing asset archive. Changing `.pdhead`, `.pdarena`, `.pdanim`, etc. to `.zip` should reveal the descriptor, meshes, textures, INI/TSV, audio, and other authored source files needed to edit or clone that asset. `.pdmod` remains only the networking/Public Mods/online transport wrapper. Authored `.bin` payloads are invalid; runtime cache may be generated but must be readable, private, rebuildable, and unshipped. c3811's loose descriptor plus same-name folder wording is superseded by c3812.

**c3812 progress (2026-05-20)**: Scanner/runtime nested archive support is done. Folder mods and `.pdmod` transport archives scan typed `*.pdxxx` archive entries directly, and runtime paths such as `heads/tri_head.pdhead::model.gltf` resolve through VFS without extraction. `modVfsCanResolve`, `modVfsGetSize`, and `modVfsResolveAnyAlloc` now understand nested typed archive paths so preflight checks do not rewrite them as loose filesystem paths. Permanent examples are real zip-openable typed archives, and the docs/UI/context wording pass now presents `.pdxxx` as the editable asset archive and `.pdmod` as transport only. Mike clarified the strict bar: every asset archive must be fully self-contained, including model textures/UV material references, rig/mesh linkage, animation targets, weapon model/animation/audio relationships, and any other authored dependencies. Follow-up correction 2026-05-21: weapon behavior uses `.pdweapon` only; `.pdwpn` is fully deprecated, was never released, and must be removed rather than accepted, aliased, migrated, or supported for compatibility. The format must cover trigger-pulled fire cadence in centiseconds, custom projectiles, rapid/looping fire, hold beams, charge/release, secondary modes, melee, zoom, reticle/overlay/camera effects, and ammo-driven model or screen state. Automated archive coverage opens current example families, verifies required inner authored files, and checks descriptor/GLTF/OBJ references resolve inside the same archive; weapon coverage must move to `.pdweapon`. Focused c3812 all-asset archive tests and adjacent c3809 modding tests pass.

**c3812 non-weapon base archive slice (done 2026-05-21)**: Base `.pdarena`, `.pdhead`, and `.pdbody` extraction no longer emits plain JSON files with typed extensions. Those emitters now write zip-openable typed archives containing their root descriptor (`arena.ini`, `head.ini`, `body.ini`) plus compatibility `manifest.json` for the current universal walker. Existing legacy JSON outputs are treated as stale and rewritten even without a force flag. The already-zip non-weapon base emitters now include root descriptors too: `.pdmesh` -> `model.ini`, character `.pdanim` -> `animation.ini`, `.pdsfx` -> `sound.ini`, `.pdvoice` -> `voice.ini`, `.pdsong` -> `music.ini`, `.pdui` -> `ui.ini`, `.pdfont` -> `font.ini`, `.pdlang` -> `lang.ini`, and `.pdscenario` -> `scenario.ini`; descriptor-less existing zips are treated as stale. Follow-up extractor work added canonical `.pdcharacter` archives with `character.ini`, `manifest.json`, and nested `.pdbody`/`.pdhead` dependency archives; scanner, packer, and allowed typed-archive suffixes now recognize `.pdcharacter`. `.pdhead` and `.pdbody` remain lower-level dependency/runtime compatibility archives, not the top-level character asset name. The base `.pdsfx`/`.pdvoice` walker now decodes ALADPCM/RAW16 ROM sample data into accessible mono PCM16 `sample.wav` files with `sample.wav.sha256`; `sound.ini`/`voice.ini` and `manifest.json` point at `sample.wav` and retain `source_format` provenance. The `.pdlang` extractor now decodes raw ROM language offset tables into editable `strings.tsv` with `strings.tsv.sha256`; `lang.ini` and `manifest.json` point at `strings.tsv`, and old `data.bin`-only language archives are stale. The `.pdfont` extractor now decodes ROM font segments into `glyphs.pgm`, `metrics.tsv`, and `kerning.tsv` with SHA-256 sidecars; `font.ini` and `manifest.json` point at those files, and old `data.bin`-only font archives are stale. The `.pdsong` extractor now inflates RareZip N64 compressed-MIDI sequences, header-swaps them, and emits standard `sequence.mid` plus editable `sequence.tsv` event listings with SHA-256 sidecars; `music.ini` and `manifest.json` point at those files, and old `data.bin`-only music archives are stale. Character `.pdanim` archives now emit editable `header.tsv` and `frames.tsv` instead of `frames.bin`, with SHA-256 sidecars. `.pdmesh` archives now promote the source `PD_MODELDEF`, walk model display lists, and emit standard Wavefront `model.obj` plus `model.mtl` and SHA-256 sidecars instead of `geometry.bin` or a TSV byte table. `.pdscenario` now emits standard `rooms.obj` plus `scenario.mtl`, decoded `tiles.tsv`, decoded `pads.tsv`, setup/mpsetup word tables, and `visual_segments.tsv` provenance with SHA-256 sidecars instead of raw `geometry.bin`/`tiles.bin`/`pads.bin`/`setup.bin`/`mpsetup.bin`. Focused static coverage pins the non-weapon descriptor set, `.pdcharacter` wiring, WAV payload guard, TSV language guard, bitmap-font guard, MIDI/event song guard, character animation TSV guard, `.pdmesh` OBJ guard, and `.pdscenario` standard map/text payload guard. Verification: `pdxasset` client build passed; focused `[modding][pdxxx][base][static][c3812]` passed 212 assertions / 11 cases. Weapon graph archive work is handled by c3814.

**c3812 extractor crash hardening (done 2026-05-21)**: B-357 fixed the first-launch asset extractor crash class surfaced by `modelPromoteNodeOffsetsToPointers` / `modelPromoteOffsetsToPointers`. `.pdmesh` now RareZip-inflates ROM payloads before model/gun preprocessing, promotes skeleton type before offsets, validates promotable model offsets before the node walk, and handles low-bit display-list flags before OBJ traversal. `.pdarena` / `.pdscenario` stage payload extraction now also inflates before tiles/pads/setup preprocessing. Both extractors run their preprocessor-dependent work serially because those preprocessors use process-global marker/GBI scratch state. Verification: focused `[modding][pdxxx][base][static][c3812]` passed 229 assertions / 11 cases, queued `meshprom` all-target build passed, and `boot_smoke` passed from a clean smoke install.

**Weapon graph asset design/audit (2026-05-21)**: Kanban `c3814` is now the first active critical Modding card. The canonical schema design is written at [designs/modding/weapon-behavior-graph-assets.md](designs/modding/weapon-behavior-graph-assets.md), the source-backed behavior audit is written at [designs/modding/base-weapon-behavior-coverage.md](designs/modding/base-weapon-behavior-coverage.md), the current 86-weapon parameter matrix is written at [designs/modding/base-weapon-parameter-matrix.md](designs/modding/base-weapon-parameter-matrix.md), the module parameter spec is written at [designs/modding/weapon-graph-module-parameters.md](designs/modding/weapon-graph-module-parameters.md), and the runtime cutover split is written at [designs/modding/weapon-graph-runtime-cutover-plan.md](designs/modding/weapon-graph-runtime-cutover-plan.md). Implemented now: `.pdweapon`/`.pdprojectile`/`.pdentity` schema contract, archive layouts, graph node categories, IR boundary, validation gates, current 86-weapon function inventory, behavior-family mapping, physical payload candidates, named weapon/projectile/entity behavior modules, runtime mapping for Slayer rockets, grenades, proxy mines, mines, Dragon proxy behavior, thrown Laptop Gun, deployed Laptop Gun autogun behavior, concrete runtime slices from extension removal through parity closure, the `.pdweapon` code cutover, first-class `.pdprojectile`/`.pdentity` catalog/manifest metadata plumbing, shared graph archive helper APIs, base graph emitter, graph validator, deterministic runtime IR compiler, Debug Settings UI/config storage for the graph runtime toggle, the Mods > Weapons browser, and a partial template/save editor. The base weapon emitter now writes zip-openable `.pdweapon` archives with `weapon.ini`, `manifest.json`, `behavior.graph.json`, and `nested_payloads.json`; the graph is `base_weapon_graph_v1` with named modules (`fire.hitscan`, `fire.auto_cadence`, `fire.burst`, `fire.charge_release`, `fire.beam_tick`, `spawn.fired_projectile`, `spawn.thrown_physical`, `melee.strike`, specials, devices), and physical functions embed generated `.pdprojectile`/`.pdentity` payload archives under `projectiles/` and `entities/` with canonical SHA-256 inventory rows. `.pdprojectile` and `.pdentity` scan/register as catalog asset types, have descriptor templates, can be named by manifest/catalog distribution paths, and preserve projectile-to-entity catalog dependencies. `weapon_graph_archive` provides descriptor/text reads, root validation, derived nested projectile/entity IDs, duplicate-ID collision checks, canonical archive-content SHA-256 over sorted uncompressed entries, nested payload inventory scanning/formatting, and in-memory embedded-archive digest support through `modArchiveMemForEachEntry`. `weapon_graph_runtime` validates schema/module/unit/catalog/cycle rules and compiles graph JSON to stable opcodes, sorted parameter blocks, and source/IR SHA-256 digests. Active next: `c3814-s15` held weapon runtime adapter using the Debug Settings graph runtime toggle as the gameplay callsite gate. Partial now: held weapon IR registers during the weapon walker pass and feeds shared gameplay accessors plus first direct held shooting helpers; the Weapons tab can clone a selected weapon into a new `.pdweapon` mod with catalog refs, file imports, save-time graph validation, copied template payloads, `mod.json`, rescan, and enable. Not implemented yet: remaining held-weapon callsites, projectile runtime adapter, entity runtime adapter, true embedding of all selected catalog assets, projectile/entity file import affordances, saved custom weapon parity/hot-register coverage, and parity closure. `.pdwpn` was never released and must not be accepted, aliased, migrated, or supported for compatibility.

**c3814-s15 held adapter progress (2026-05-21)**: The `.pdweapon` walker now compiles/registers held IR during load, shared gameplay accessors read graph damage, impact force, fire-slot duration, numeric shoot sound, penetration, function flags, and max_rpm cadence, and direct held shooting helpers use graph-backed burst flags, ammo slot, spin-up/spin-down, muzzle flash flag, initial/max RPM, and ammo consumption behind the Debug Settings graph runtime toggle. Remaining for `c3814-s15`: cooldown, trigger state, full hitscan execution, charge/release, beam, melee, specials, devices, presentation, reticles, overlays, zoom, model visibility, and parity tests. Legacy behavior remains the default fallback until parity is proven.

**c3814-s20 Mods Weapons browser done (2026-05-21)**: The Modding Hub now has a Weapons tab that lists catalog weapon assets, marks Base vs Mod entries, shows catalog/model/runtime/archive details, and previews `weapon.ini`, `behavior.graph.json`, and `nested_payloads.json` when the `.pdweapon` archive is resolvable. The original browser slice was inspection-only for base entries; template/import/save work is now partially active under `c3814-s21` and `c3814-s22`.

**c3814-s21/s22 weapon template/save progress (2026-05-21)**: The Weapons tab now has a Template flow: `Use as Template` clones the selected base or mod weapon, catalog pickers select model/texture/animation/audio/projectile/entity refs, the in-engine file browser imports model/texture/animation/audio/graph files, edited graph JSON is validated before save, and `Save Weapon Mod` writes `mods/Weapons/<slug>/<slug>.pdweapon` with root descriptors, copied non-root template payloads, imported files, `mod.json`, mod rescan, and enable. Remaining: selected catalog assets that are not already copied from the template/import payload are stored as refs rather than embedded payloads; projectile/entity typed-archive file import buttons and stronger saved custom weapon hot-register/parity tests still need to land.

**Kanban priority pass (2026-05-21)**: Mike set the near-term necessity/weight order: finish the asset pipeline first, then Input, Collision, and gameplay stability in local and networked play including drop-in/drop-out. The active Kanban sort now reflects that: `c3814` weapon graph asset design first; `c3812` typed `*.pdxxx` archive repair second; `c086` Combat Sim input bindings/SkipUp walker third; `c136` airborne collision blocker retest fourth; `c3813` local/network gameplay stability and drop-in/drop-out fifth. Campaign/render/player-init stability stay high but below those five. Benchmark/GPU swarm work and vague failed-test cleanup are demoted behind the ship-critical chain. `c083` remains blocked but flagged critical once Mike provides a fresh Combat Sim start crash log.

**Menu/input note (2026-05-21)**: Kanban `c086` is code/build verified and pending Mike controller playtest. Combat Sim Room now removes the conflicting per-row Y multi-select path so Y stays undefined there, adds `pdguiMenuStartPressed()` so Start on the right panel jumps focus to Start Match, opens X/right-click context menus for bot rows, the local player row, and Add Bot through a shared request helper, adds Add Bot Fill/Remove All popup actions, and routes LT/RT to the focused panel. The left settings panel walker now moves Arena -> Scenario -> Limits -> Weapon Set -> Options, while player-list LT/RT remains team/page scoped. Arena/weapon/handicap group headers were converted to non-focusable text so panels/headers are not selectable while their contents remain selectable. Main Menu -> Combat Simulator -> Start Match and endscreen/leave return paths remain graph-routed and statically pinned; Theme Editor NavFlattened coverage is statically guarded. Verification: isolated `c086menu2` focused `[input][menu_graph]` passed, focused `[scroll],[nested-scroll]` passed, and queued all-target build passed. Manual retest remains: Solo Play -> Combat Simulator -> configure with controller + MKB -> Add Bot/context actions -> Start Match -> endscreen/Back to Menu, plus RS scroll feel.

**Combat Sim post-match note (2026-05-21)**: B-356 is build-verified and pending Mike playtest. Ending a local Combat Simulator match could appear to route toward the post-match screen but never show it, leaving the game stuck until force close. The MP game-over root was pushed, then the legacy NTSC save-player prompt could be pushed on top for local profiles without a file GUID; that prompt is intentionally suppressed as a no-op by the PC ImGui layer, so it could hide the real post-match UI while `MPPAUSEMODE_GAMEOVER` stayed active. `mpPushEndscreenDialog()` now marks `OPTION_ASKEDSAVEPLAYER` but does not push the suppressed save-player dialog; PC config save remains owned by the ImGui endscreen exit path. Verification: isolated `csend356` focused `[input][menu_graph]` passed (644 assertions / 33 cases) and queued all-target build passed. Manual retest: Combat Simulator local match -> end match -> post-match screen appears without force close -> Return to Room / Back to Menu works.

**Menu/input closure note (2026-05-21)**: Kanban `c087` and `c088` are code/build verified and pending Mike controller playtest. Main Menu and Pause Menu now poll `pdguiMenuTertiaryPressed()` at the screen level, open the Social shell, render the `ACTION_MENU_SOCIAL` glyph in the top-right chrome area, and prevent the parent menu from closing on B/Escape while Social owns input. Combat Sim Room still intentionally has no Y-Social binding per Q4, and the static menu graph guard pins that restriction. Verification: isolated `menuinput` focused `[input][menu_graph]` passed (662 assertions / 34 cases), focused `[press-hold]` passed (47 assertions / 13 cases), and queued all-target build passed after rerun with a 300s active-build watchdog.

**Press/hold closure note (2026-05-21)**: Kanban `c020` is done for the input/menu lane. The actionmap primitive treats press/tap and hold as the same physical input state resolved by threshold and consumption; `actionWasTap` is a release-time short-hold check, `actionHeldForMs` is the threshold gate, `actionConsumeHold` prevents later tap release, and `actionHoldProgress` decays after consumption. Current consumers covered by `[press-hold]`: `ACTION_USE`, `ACTION_WEAPON_NEXT`, and `ACTION_CROUCH`. Natural-stop cases are consumer-owned, not a new input primitive: future full-charge fire, beam/overheat cooldown, ammo-empty stop, and weapon-specific hold behavior are tracked under `c3814-s15` held-weapon runtime adapter work.

**Custom/accessibility controller note (2026-05-21)**: Kanban `c3816` is code/build verified and pending Mike hardware playtest. Raw non-SDL_GameController joysticks (HOTAS/HOSAS, button boxes, homemade HID, accessibility devices) now open beside standard controllers, bind capture accepts raw buttons and axes 0-5, actionmap dispatch maps those through existing JOY virtual keys, glyphs show `BtnN` / `AxisN+/-` labels for custom-class devices instead of Xbox A/B/X/Y labels, and Social presence/friend rows carry a privacy-safe input class only. Verification: isolated `c3816custom` focused `[input][custom-controller]` passed (44 assertions / 4 cases), adjacent `[input][menu_graph]` passed (662 assertions / 34 cases), `[press-hold]` passed (47 assertions / 13 cases), and queued all-target build passed. Final manual retest should use real non-standard hardware; follow-ups remain per-device profiles, glyph-pack textures, calibration/deadzones, and richer multi-axis semantics.

**Menu/input note (2026-05-19)**: B-351 is build-verified and pending Mike playtest. Opening Solo Play -> Combat Simulator enters the pure-ImGui room screen, but the menu-pool watchdog treated `MENU_TYPE_ROOM` as a stale leak because the legacy stack was empty, then released/reacquired it every frame. That repeatedly flipped mouse mode between gameplay-relative and ImGui-absolute, matching the reported input being pulled toward center. The watchdog now uses leak-only menupool helpers, preserving standalone pure-ImGui overlays while still releasing real legacy-stack leaks such as the stale `main_solo_view` slot shown in the log. Isolated `b351menu` tests target, focused `[input][menupool][static][b351]`, and all-target build passed; manual Combat Simulator room retest remains.

**Physics note (2026-05-19)**: B-350 is build-verified and pending Mike playtest. The prior c038/B-339 meshcollision work is active (`MESHCOL: world mesh finalized`, `MESHCOL: ENABLED`) and top/bottom jump blocking improved, but Mike's CI Training playtest showed airborne side-entry through overhead blockers because player motion split horizontal movement first and a vertical-only jump sweep second. `bondwalk.c` now runs an upward diagonal capsule sweep from `bondprevpos` to current X/Z plus vertical delta before the vertical-only sweep, rolls X/Z back to the safe fraction on any hit, and kills upward velocity when the combined sweep classifies a ceiling. Side-wall capsule fixture coverage is in `[physics][jump]`; isolated `b350side` tests target, focused `[physics][jump]`, and all-target build passed. Kanban card `c136` tracks the started-and-completed session slice with pending-completion review; manual retest remains for CI Training blockers/doorway and moved-couch dynamic collision.

**Skedar Swarm stress note (2026-05-19)**: B-352 is build-verified and pending Mike playtest. The release-log fingerprints were clear: GPU Swarm crashed during the 128 -> 256 cycle before `despawn_all freed ...`, CPU Swarm reached 4096 then hit `Unknown GBI opcode 0x80`, jump starts clustered into same-frame bursts, and volume placement could select unsafe/death-floor areas. `swarm_test.c` now invalidates GPU slot-keyed state before mass-free, avoids death-union writes for non-death actions, snaps/rejects spawn candidates against real non-`GEOFLAG_DIE` floor, and phases/budgets Skedar jump starts. `swarm_gpu.cpp` gives <=256-bot readback fences a short bounded wait to reduce `active=0` jitter. The B-353 follow-up is closed: Swarm slots now prove a live prop/backlink before teardown, death polling, movement intent, or GPU handoff, and stale slots are cleared/refilled instead of being passed into `chrRemove()`. Static/build verification passed in `skstress`, `sksmoke`, and `sklive`; focused `[testscenarios][static][b352]` now covers 38 assertions; normal `swarm_gpu_b352_stress_smoke` runner passed twice to 256 with 24/24 assertions and exit 0. Manual retest remains for GPU >128, CPU 4096, wall/surface movement, and spawn-volume safety.

**Dev Window release note (2026-05-19)**: B-348 is fixed pending smoke. Dev Window v2 now persists the Log tab stream to `devtools/dev-window-v2/dev-window-v2-console.log` with two rotated prior logs (`.1`, `.2`) so a freeze/crash during release leaves evidence. The generated console logs are ignored by git; the previously tracked log is removed from the index so logging cannot dirty Release's rebase. `release.ps1` now streams `gh release create` by inherited stdout/stderr plus wait heartbeats, avoids the unsafe async PowerShell event callback, and aborts Release if rebase fails instead of continuing toward push/GitHub publish.

**Dev Window CLI note (2026-05-21)**: Dev Window v2's CLI tab now has a separate `Codex CLI Admin` button. It bypasses the Claude prompt-composition flow, does not require prompt text, resolves `codex` from common Windows install paths or PATH, and opens an elevated interactive console in the project root.

**Updater/logging note (2026-05-19)**: B-349 is build-verified and pending Mike retest. Client logs now live under `logs/game client/`; standalone updater logs to `logs/updater/pd-updater.log`; both updater cleanup paths protect `logs` and record protected skips/stale deletions. This gives evidence for Mike's other-machine ROM deletion report while keeping the B-338 root-ROM protection in both apply paths. Release keeps `put_your_rom_here.txt` as the only ROM instruction file; root `README.txt` remains excluded.

**Mission-start note (2026-05-19)**: B-345 black-screen mission start is fixed pending playtest, with a second pass after Mike rejected the completion card. The original stale charpreview `INVMENU` ownership fix still holds: Defection reaches `bgunTickMasterLoad ... CARTS->LOADED newwpn=3` and `visible=1`. The rejected-card follow-up fixed a confirmed catalog miss from the campaign sweep: BODY_CHICROB/base:sp_body_118 is now self-contained, so Chicago/Skedar Ruins robot setup `headnum=0x00` no longer requests a phantom head. New smoke fixture `mission_escape_hoverbed_intro` verifies Area 51 - Escape loads the Elvis hoverbed model (`PhoverbedZ`) during mission start. Kanban card `c133` is back in pending-completion with second-pass evidence; manual UI visual retest remains the closure gate for any mission Mike still sees as black or missing props.

**Rendering note (2026-05-19)**: B-346 credits/fog solid-square artifact is in second-pass fixed-pending-playtest state. Mike's first `gfxalpha` playtest failed: characters/weapons became translucent, and credits particles/text were still solid colored squares. The second pass removes the bad broad fog-alpha output-alpha path (`G_BL_A_FOG` now drives color fog only, not material alpha), keeps normal translucent `MEM,1MA` alpha handling, adds a strict `G_RM_ADD` additive-fog exception for the exact `IN,FOG_ALPHA,MEM,1` blender tuple, and explicitly resets text texture enable/scale in `text0f153628()` for CI4 glyph masks. Isolated `gfxalpha` tests build, focused `[rendering][fast3d][fog][static][b346]` (41 assertions / 3 cases), focused `[rendering][credits][texture][static][b346]` (16 assertions / 2 cases), scoped `git diff --check`, and `-Target all` client/updater build passed after the additive-fog tightening. Mike will manually retest credits and fog/additive stage effects from `.claude/session-builds/gfxalpha/PerfectDark.exe`. Kanban card `c134` remains active/watch; the stale first-pass pending-completion marker was removed.

**Menus note (2026-05-19)**: Settings > Debug now has an offline-only "Go to Credits" scene shortcut for faster credits rendering playtests. Static coverage pins the button, `GRID_STAGE_CREDITS` handoff, and netplay disable guard. Isolated `dbgcred` tests build, focused `[debug][credits][menu][static]`, and all-target build passed; manual UI playtest remains.

**Physics/logging note (2026-05-19)**: B-347 capsule diagnostic flood is build-verified and pending restart/playtest. The running `Build\PerfectDark.exe`/`Build\pd-client.log` showed a dev `RelWithDebInfo` build (`PD_STABLE_RELEASE=OFF`) flooding `CAPSULE:` Stage 2 probe lines even with `Debug.JumpLogging=0`, which can look like a release hang through log I/O pressure. `CAPSULE_LOG` now honors `g_JumpLoggingEnabled`, and `CAPSULE:` routes through the game log channel. Isolated `caplog` tests/focused `[physics][jump]`/all-target build passed; manual log-flood confirmation needs a restarted rebuilt binary.

### 1. Catalog - Weapons F11-F13 data move - LANE CLOSED 2026-04-30

**Status**: F1-F10 (S484), F11+F12+F13 (S591) all shipped 2026-04-30. **Lane closed.** Mike's playtest verified F12's `LOADER.PDBASE.WEAPON.OK: parity check PASS (86 weapons)`; F13 retired Layer A on the back of that confirmation.

**Final state**:

- `base/weapons.pdbase` (12,823 lines) is the sole authoring source for weapon DATA. Generated by `devtools/extract_weapons_pdbase.py`; regeneration is byte-deterministic from invitems.c (now retired) so the script doubles as a regression tool.
- Loader [port/src/loader_pdbase.c](../../port/src/loader_pdbase.c) parses the JSON at startup, populates manager-owned typed pools (weapons, guncmd, gunviscmd, partvis, ammos, aim/noise/recoil settings, weaponfunc union, vibrations, anim name table, bot prefs).
- Catalog manager [port/src/catalog_mgr_weapons.c](../../port/src/catalog_mgr_weapons.c) is the sole accessor surface: `catalogManagerGetWeaponByIndex`, `catalogManagerGetWeaponById`, `catalogManagerGetWeaponBotPref`, `catalogManagerWeaponDefaultAimSettings`, `catalogManagerWeaponDefaultNoiseSettings`. All routes go through `loaderPdbaseGetWeapon*()`. No legacy fallback.
- `src/game/invitems.c` reduced from 5789 lines to ~50-line header comment. `g_AibotWeaponPreferences[]` removed from `src/game/botinv.c`. Externs removed from `inv.h`, `data.h`. F12's parity-period bridge + `loaderPdbaseRunParityCheck()` retired.
- F13 grep-guard test ([catalog-mgr-weapon][s484][f13], 4 cases / 18 assertions) ensures the symbols don't return. Helper `fileHasNonCommentOccurrence` allows comment mentions for grep-trail.
- Binary shrink: PerfectDark.exe 54.7 MB -> 54.5 MB (200 KB removed static data).

**OOB-read class structurally impossible**: the historical B-263 / `g_Weapons[254]` AV crash class is gone -- there is no `g_Weapons[]` to index out of bounds. The defensive guard at `modelmgrLoadProjectileModeldefs` is now belt-and-braces redundancy that stays for safety.

**F12 follow-ups (deferred, not blocking)**:

- Expand `s_BaseWeapons` from 41 MP-only to 86 entries with `pdbase_path` metadata. Per Mike's 2026-04-30 unlock-state clarification, registration is unconditional. Not in scope for the closed lane.

**Design ref**: [designs/catalog/catalog-full-pipeline-weapons.md](designs/catalog/catalog-full-pipeline-weapons.md) (F11 + F12 + F13 progress in Section J.4-J.10).
**Pillar ref**: [pillars/catalog.md](pillars/catalog.md).

### 1a. Texture deployment + extraction investigation (queued, post-F13)

Surfaced during F12 runtime debugging. Two parallel slices:

**Slice A: drop the legacy `mods/` build-time deploy.** Per Mike: "the copied mods don't conform to our new file format. We can probably remove that and just ensure we copy the data folder with the rom." Find the build step copying `mods/` -> `Build/data/mods/`, remove it. Ensure `Build/data/` ships alongside the ROM at release time.

**Slice B: client-side ROM texture extraction debug.** `pdguiThemeExtractRomTextures()` writes 14 UI textures from ROM data to `Build/data/mods/base-ui/textures/X.tga`. Latest playtest (Mike's 15:05 run) showed all 13 .tgas loading from mod file at correct dimensions (no procedural fallback). The "plagued for weeks" symptom Mike described was deployment fragility (clean build wipes Build/data/mods/, extract regenerates them at startup, but if that path failed the procedural fallback fired). After Slice A removes the legacy deploy, only the extraction path matters. Audit + fix the extraction path correctness; relocate output to source `base/ui/textures/` so they ship with the build (CMake POST_BUILD already covers `base/`).

### 2. Catalog - Gate 3 Migration + Phase 3 Rom-once disk-back

**Status**: LANE CLOSED 2026-05-02. Pass C (RomProvider drop) shipped at dev `b15cc701`. Pass D (self-heal hardening) shipped 2026-05-02 in worktree `sharp-zhukovsky-116f03` (S604). The Catalog migration is COMPLETE; Mike's 2026-05-01 ROM-once-then-disk directive is fully satisfied. The runtime never touches `g_RomFile` after extraction; corruption is detected via SHA-256 sidecars on every boot, recovered via re-extract from the in-memory ROM during verify, surfaced via LOUDFAIL.LOAD log channel + per-file UI toasts + aggregate boot integrity report.

**Heads** -- shipped 2026-05-01 as S596 at dev `a2ad421e`. F1-F13 landed: manager (`port/src/catalog_mgr_heads.c`), pure validators, `base/heads.pdbase` (84 records: 75 named + 9 SP fallback), loader integration, F2 catalogGetHead* routing, F3+F4 modeldef cache, F5 body.c modeldef NULL check via manager, F6 retire `g_MpMaleHeads` / `g_MpFemaleHeads`, F11 startup wiring (`catalogManagerHeadInit` + `loaderPdbaseBuildHeadManager`), F13 grep-guard test (12 cases / 96 assertions). Body reads keep the legacy `g_HeadsAndBodies` pattern until bodies migrates. S593g `head_canon=NULL` warning gate preserved.

**Bodies** -- shipped 2026-05-02 as S598 at dev `64af7e0c` (bodies F1-F13) + `47f837d5` (pd-server stubs close-out). F1-F13 landed: manager (`port/src/catalog_mgr_bodies.c` 305 lines), pure validators (25 lines), `base/bodies.pdbase` (890 lines, 68 records: 63 named + 5 SP fallback), loader integration, F2 catalogGetBody* + `_Checked` accessor routing, F3 modeldef accessor migration, F4 catalogResetAllModeldefs legacy walk removed, F5 body.c verification (S593g warning gate preserved via manager-routed `catalogGetBodyIsComplete`), F11 startup wiring, F12 parser, F13 grep-guard test (16 cases / 190 assertions; 21 cases / 246 assertions for `[gate3]` covering heads + bodies). Build clean across pd (54.8 MB) + pd-server (22.3 MB) + pd-tests (23.9 MB). pd-server stub fix added 5 client-only refs to `port/src/server_stubs.c`.

**Maps + Arenas** -- shipped 2026-05-02 as S599 at dev `6d3bf4c9`. F1-F13 Manager + `.pdbase` + grep-guard template applied; static arena metadata moved to `base/arenas.pdbase`.

**Phase 3 Pass A/B/C (Rom-once + disk-backed catalog)**:

- **Pass A.1** (data tier accessors), **A.2** (first-launch ROM extraction to `data/<romid>/files/`), **A.3** (BYOR SHA-256 known-good gate), **A.4** (hash-verify-on-launch self-heal): all shipped.
- **Pass B Slices 1-13**: per-class catalog migration from RomProvider to FileProvider + segment extraction to `data/<romid>/segs/`. All shipped 2026-05-02. Slice 12 (SFX residual ACCEPTED LIMIT) close-out at `f52cf660`. Alias-range SFX IDs (0x8000+) deliberately unregistered -- leaf-level overrides via the 1545 `ASSET_AUDIO` entries cover alias-IDed plays after the `snd.c::sndStart` decode.
- **Pass C** (RomProvider drop) shipped 2026-05-02 at `b15cc701`. `romdataReleaseRom()` migrates SRC_ROM segments to disk-backed copies after extract+verify, NULLs lazy fileSlot pointers into ROM range, frees `g_RomFile`. `romdataFileLoad` gains a per-romid disk fallback before the legacy SRC_ROM set; LOUD-FAIL `LOAD.PASSC` if both miss with `g_RomFile == NULL`. `romdataResetFile` handles the released-ROM case. Audit: [`audits/catalog-phase3-passc-romprovider-drop-2026-05-02.md`](audits/catalog-phase3-passc-romprovider-drop-2026-05-02.md).

**Pass D** -- shipped 2026-05-02 (S604) in worktree `sharp-zhukovsky-116f03`. Self-heal hardening on top of Pass A.4 + segment verify. Three surfaces:

1. Per-file system toasts on `corrected` / `failed` outcomes (5-cap to prevent queue flooding).
2. Aggregated boot integrity report: `LOG_NOTE: DATA INTEGRITY: V validated, R re-extracted, U unrecoverable` plus `LOG_WARNING` + danger toast if `U > 0` and info toast if `R > 0`.
3. Deferred toast queue + drain (toasts queued during boot replay with fresh timestamps after `gameInit` so the renderer sees them as new).

Plus: quarantine path migrated to user-visible `data/_quarantine/<romid>/<unixtime>_<basename>` (was `data/<romid>/.quarantine/`).

Test pin: 9 cases / ~25 assertions in `[catalog][passd]` (`tests/test_romextract_passd.cpp`). Audit: [`audits/catalog-phase3-passd-self-heal-2026-05-02.md`](audits/catalog-phase3-passd-self-heal-2026-05-02.md).

**Remaining catalog queue** (catalog migration COMPLETE; these are post-migration polish, not blocking the lane closure): scenarios / game modes, bot profiles + bot variants -- each gets a design pass + audit + migrate + retire Layer A when scope permits, but the architectural ROM-once-then-disk endpoint is achieved.

### 2a. Catalog Universality Pivot (in flight)

Per [designs/catalog/universality-pivot-schemas.md](designs/catalog/universality-pivot-schemas.md). Schema lock-down (Step 0) at dev `00fdb8b7`. Step 1 (weapons + meshes + weapon-anim emitters) at dev `7e0d0791`. Step 2 (heads + bodies + arenas + scenarios) shipped 2026-05-03 in worktree `stupefied-jemison-6f4e32`: emits `.pdhead` / `.pdbody` / `.pdarena` JSON plus the unified `.pdscenario` ZIP per Q-1 (one ZIP per arena's playable stage; bg + tiles + pads + setup + mpsetup + manifest). Step 3a (character animations) shipped 2026-05-03 in worktree `amazing-torvalds-eadac6`: emits one `.pdanim` ZIP compound per chr animation entry in `data/<romid>/segs/animations.bin`, each with manifest envelope (`category: "character_animation"`) + `frames.bin` (header + frame bytes) + SHA-256 sidecar. New files: `port/src/romextract_pdanim_chr.c`, `port/src/romextract_parity_pdanim_chr.c`. Boot wiring follows the Step 1/2 pattern in `port/src/main.c`. Reuses `loaderPdbaseNameForAnimEnum` reverse lookup over `k_AnimEnum` (1208 entries) to mint catalog IDs `base:anim_<symbolic_lower>` or `base:anim_chr_<NNNN>` for unnamed slots. Q-5 parity check verifies envelope + frame_count + bytes_per_frame + header_len + frames.bin size round-trip.

**Step 3a status**: 7 of 13 kinds emitted (`weapon` / `mesh` / `animation` (now complete with both categories) from Steps 1 + 3a; `head` / `body` / `arena` / `scenario` from Step 2). Build clean across all 4 targets (client 55.1 MB, updater 12.3 MB, server 22.4 MB, tests 24.9 MB). Step 3a closes Mike's Q-3 ruling.

**Step 3 audio half status (2026-05-03, worktree `frosty-antonelli-fd537f`)**: 10 of 13 kinds emitted. Step 3 audio half ships `.pdsfx` / `.pdvoice` / `.pdsong` ZIP compounds. New files: `port/src/romextract_pdsfx.c` (shared SFX-bank walker), `port/src/romextract_pdvoice.c` (wrapper), `port/src/romextract_pdsong.c`, and three matching `_parity_*.c` siblings, plus `port/src/romextract_pdaudio_internal.h` (private walker glue between sfx + voice). Voice classification reuses the Slice 10 predicate (audioconfig slot in `{1, 2, 3, 47, 48, 60, 62}`). Boot wiring lands in `port/src/main.c` after the Step 3a block. Q-5 parity verifies envelope + `id` + `source_index` + `data_size`/`binlen`/`ziplen` round-trip.

**Step 3b part 1 status (2026-05-03, same worktree continued)**: 12 of 13 kinds emitted. Step 3b part 1 ships `.pdfont` + `.pdlang` ZIP compounds. New files: `port/src/romextract_pdfont.c` (10 NTSC font face segments wrapped raw), `port/src/romextract_pdlang.c` (68 lang banks wrapped raw, English locale only for NTSC ship), and two matching `_parity_*.c` siblings. Boot wiring lands in `port/src/main.c` after the Step 3 audio block. Q-5 parity verifies envelope + `id` + `source_segment`/`source_bank` + `data_size` round-trip.

**Step 3b part 2 status (2026-05-03, worktree `gifted-benz-cad936`)**: **13 of 13 kinds emitted**. Step 3b part 2 ships `.pdui` ZIP compounds plus the theme reader migration. Catalog universality writer-side is COMPLETE. New files: `port/src/romextract_pdui.c` (thin C wrapper around the C++ emitter in `pdgui_theme.cpp`), `port/src/romextract_parity_pdui.c` (Q-5 envelope parity walking the canonical 14-texture mirror table). Modified: `port/fast3d/pdgui_theme.cpp` adds memory-variant TGA helpers (`s_writeTgaToMem` / `s_loadTgaFromMem`), the canonical `k_PduiEntries[]` table, the `pdguiThemeEmitPduiZips` extern "C" emitter, rewrites `pdguiThemeLateInit` to read `.pdui` ZIPs via `modArchiveOpen` + `modArchiveExtractAlloc("texture.tga")` + `s_loadTgaFromMem`, collapses `pdguiThemeExtractRomTextures` body to a single delegate call, and updates `pdguiThemeCheckExtract` to detect missing `.pdui` ZIPs at `data/<romid>/ui/<slug>.pdui`. Boot wiring lands in `port/src/main.c` Step 3b part 2 block (structural placeholder; texture system not ready until pdmain.c::mainInit, so the actual emit fires from the render-loop trigger inside `pdguiThemeCheckExtract` once GL is up). Q-5 parity verifies envelope + `id` + `texture_count` + `source_index` round-trip; missing files treated as skip on first launch.

**Step 4 status (2026-05-03, worktree `affectionate-hawking-f01503`)**: SHIPPED. Universal directory walker (`loaderWalkerLoadAll`) became the catalog row registration path. The pre-Step-5 aggregate parser remained in the boot flow as the parity-bounded fallback for the heavyweight pool until Step 5.

**Step 5 status (2026-05-03, worktree `hungry-elgamal-e991de`)**: SHIPPED. **Catalog Universality COMPLETE.** The legacy aggregate-archive tier retired entirely:

- `loader_pdbase.{h,c}` (parser, ~2400 lines) -> `loader_pool.{h,c}` (~1900 lines) with per-asset entry points (`loaderPoolReset` / `loaderPoolParse{Weapon,Head,Body,Arena,Animation}Json` / `loaderPoolFinalize`). The four `BuildManager` helpers + `loaderPdbaseScan` + arena parity check retired into the universal walker. `loader_pdbase_enums.{h,c}` -> `loader_enum_reverse.{h,c}` with renamed `loaderEnum{Resolve,NameFor}*` functions.
- `loader_walker_kind_desc_t` gained `always_invoke` flag; the four pool kinds + the animation kind set it so the scaffold calls per-kind register_fn even for IDs already in the catalog (so `loader_pool` populates the typed payload).
- `port/src/loader_walker.c::loaderWalkerLoadAll` brackets the per-kind scan with `loaderPoolReset` + `loaderPoolFinalize`.
- 4 deletions: `base/{weapons,heads,bodies,arenas}.pdbase` archives.
- 4 deletions: `devtools/extract_{weapons,heads,bodies,arenas}_pdbase.py`.
- 7 deletions: `port/src/romextract_parity_pd{wpn,head,body,arena,sfx,lang,anim_chr}.c`.
- 2 deletions: `tests/test_loader_pdbase_{arenas,scan}.cpp`.
- Drops: `bondgun.c` canary instrumentation block (~85 lines), `pdgui_theme.cpp` legacy writers (`s_writePng`, `s_writeNinesliceJson`, CRC32 helpers, ~200 lines), `asset_entry_t.ext.{weapon,head,body,arena}.pdbase_path/offset/size` fields (12 fields, ~70 KB row overhead).
- 1 addition: `tests/test_pdbase_retired_audit.cpp` Step 5 grep-guard. Walks `port/` + `src/game/` + `devtools/` and asserts no `pdbase` / `loaderPdbase` / `loader_pdbase` substring remains, plus checks no `base/*.pdbase` archive exists.
- `CMakeLists.txt`: dropped `pdbase_deploy` custom target; tests list updated; grep-guard added.
- `devtools/release.ps1`: dropped base/ copy section; release zip ships only client + updater + data/.
- Build verify clean four-target: pd 55.3 MB / pd-updater 12.3 MB / pd-server 22.4 MB / pd-tests 24.9 MB.

**Pillar ref**: [pillars/catalog.md](pillars/catalog.md). **Audit**: [audits/catalog-universality-pivot-plan-2026-05-02.md](audits/catalog-universality-pivot-plan-2026-05-02.md) Step 5 SHIPPED entry.

### 2b. Catalog universality post-pivot triage SHIPPED (2026-05-03, gallant-booth-6f996f)

Five-bug coherent ship from Mike's playtest of the Step-5 build (`pd-client.log` at install dir on 2026-05-03).

- **B-318** (HIGH) -- Walker-emitter chicken-and-egg deadlock at the gate level. Pool-dependent emitters (`pdwpn`, `pdmesh`, `pdanim`, `pdhead`, `pdbody`, `pdarena`) had `if (!loaderPoolIsActive()) return 0` early-returns inherited from the pre-Step-5 `.pdbase` flag, which now never flip true on clean install. Mike's option (c) applied: gates removed, emitters run unconditionally; inner loops gracefully handle NULL pool slots. Note: the upstream "empty pool on clean BYOR" issue is NOT solved by gate removal alone -- pool-dependent emitters still emit 0 files when the walker has nothing to populate the pool from. The proper fix (either pre-ship `.pd<ext>` files or add a ROM-direct extraction path) is acknowledged future work consistent with the Step 5 audit doc's "First-boot regression note (accepted)".
- **B-319** (MED) -- `fsCreateDir` semantics. Returned raw `_mkdir`/`mkdir` int (0=success) but most callers used `if (!fsCreateDir(x))` which interpreted SUCCESS as failure. Standardised on 1=success / 0=failure; updated 12 sites that used the raw POSIX pattern.
- **B-320** (HIGH) -- Audio emitter parent-dir creation. `pdsfx`, `pdvoice`, `pdsong` emit under `data/<romid>/audio/{sfx,voice,music}` but only created the leaf dir; the `audio/` parent missing on Windows produced 1545 / 1545 / 119 `modArchiveBegin` failures. Fix: each audio emitter now creates the `audio/` parent before its leaf subdir.
- **B-321** (MED) -- Install layout: `data/` and `mods/` flattened to install root. `DEFAULT_BASEDIR_NAME` changed from `"data"` to `"."` so base dir = EXE directory. Trailing `/.` stripped at fsInit. `release.ps1` data-copy loop refactored to use `$DistDir` directly (no nested `data/` wrapper); `base/` and `mod source files/` dropped from dist via exclusion match.
- **B-322** (LOW) -- Retire `data/README.txt` in favour of `put_your_rom_here.txt` at install root. Filename is the call-to-action; rich content covers ROM placement, region/format, first-launch, troubleshooting, install layout.

Build verify clean four-target via `build-session.ps1 -Session b318 -Target all/server/tests`: client 55.1 MB, updater 12.3 MB, server 22.4 MB, tests 24.6 MB. Audit: [audits/catalog-universality-pivot-plan-2026-05-02.md](audits/catalog-universality-pivot-plan-2026-05-02.md) "Post-pivot triage SHIPPED" section. Bug ledger entries B-318 through B-322 in [bugs.md](bugs.md).

**Followups (out of scope for this ship):**
- ~~Empty-pool-on-clean-BYOR root-cause fix~~ -- SHIPPED at section 2c (BYOR completion via Option B authoring tables).
- `pdvoice skipped=1545` investigation (every sound classified as is_voice=0 -- russ-table read issue independent of parent-dir).

### 2c. BYOR Completion SHIPPED (2026-05-03, focused-poitras-97c1d4)

Closes the "Empty-pool-on-clean-BYOR" followup from section 2b. Per Mike's hard constraint: BYOR is non-negotiable, never tradeoff. Ship as ONE coherent unit covering all metadata kinds via Option B (authoring source-of-truth files in `port/src/*data_authored.c`). No mixed pattern; no pre-shipped extracts.

**5 authoring files** (`port/src/*data_authored.c` + `port/include/*data_authored.h`): `weapondata` (86 weapons + 87 bot prefs + slug table, 6037 lines), `animdata` (110 anims iteration table), `headdata` (84 heads), `bodydata` (68 bodies), `arenadata` (47 arenas with slug + category + load_mode columns). Engine never includes these headers; reads route through the catalog managers.

**6 emitters refactored** to walk authoring tables: `pdwpn`, `pdanim` (non-chr), `pdhead`, `pdbody`, `pdarena` (also feeds `.pdscenario` ZIPs unchanged), and `pdmesh` extended to walk weapon hi/lo + head + body + body hand mesh refs (closes the schema 2.5 cross-ref coverage gap).

**6 catalog files migrated** (catalog internals only; perimeter outside catalog already routed): `assetcatalog_base.c` (registration loops + SP loop), `assetcatalog_api.c` (handfilenum sentinels), `assetcatalog_base_extended.c` (hand-model probe), `catalog_mgr_heads.c` + `catalog_mgr_bodies.c` (mirror init via lookup helpers), `modelcatalog.c` (validation-time data source).

**Engine retirements**: `g_HeadsAndBodies[152]` body retired in `src/game/modeldata/robot.c`; `g_MpArenas[47]` body retired in `src/game/mplayer/setup.c`; `data.h` externs retired; `server_stubs.c` mirrors retired (server links the head/body/arena authoring files explicitly via `SRC_SERVER` list update).

**Build verify clean four-target** via `build-session.ps1 -Session byor1`: client 55.2 MB, updater 12.3 MB, server PASS (7s), tests PASS (16s). No new compile warnings.

**Test pin updates**: `test_catalog_provider_static.cpp` hand-model probe pin updated to `g_BodyData[i].handfilenum`. `test_arena_direct_reads_audit.cpp` rewritten as positive pins on the new authoring file + walk pattern.

**Pipeline**: ROM -> disk segments (Pass A.2/B/C) + binary-baked `g_*Data[]` -> emitters write per-asset `.pd<ext>` -> walker registers in catalog -> catalog serves engine. No `.pdbase`, no pre-shipped extracts, no engine-side direct reads of authoring tables. BYOR contract holds end-to-end.

**Smoke verify (Mike-runnable)**: `rm -rf <install>/data/<romid>/`, run client, verify `LOADER.UNIVERSAL.SUMMARY: scanned > 0 registered > 0` for all 13 kinds, verify per-kind dirs populated with expected counts, verify stage load proceeds without AV.

**Audit**: [audits/catalog-universality-pivot-plan-2026-05-02.md](audits/catalog-universality-pivot-plan-2026-05-02.md) "BYOR Completion SHIPPED" section.

### 2e. Walker-after-emitters reorder + Dev Window ROM placement SHIPPED (2026-05-03, gifted-bohr-62309b)

Three-bug coherent ship from Mike's playtest of `dev 232d05ea` (BYOR completion + Phase 3 + B-323 build). The Step-5 + post-pivot universal walker order had a structural deadlock that B-318's gate removal could not fix on a clean install.

- **B-324** (CRITICAL) -- AV at boot: `bgunCalculateBlend` (bondgun.c:3526) dereferences NULL `weapon` returned by `weaponFindById(0)`. Stack via addr2line: `bgunCalculateBlend` -> `bgunReset` (bondgunreset.c:231) -> `lvReset` (lv.c:618) -> `mainLoop` -> `mainProc` -> `main`. Caused by B-325.
- **B-325** (HIGH) -- Walker ran BEFORE emitters in `bootRunCatalogWork` ([port/src/main.c:154](../../port/src/main.c:154)). On a clean install the per-asset directories were empty when the walker scanned them; the walker registered 0 entries; `s_LoaderActive` stayed 0; `loaderPoolGetWeapon` returned NULL for every index for the entire boot; `weaponFindById` returned NULL; `bgunCalculateBlend` AVed. Fix: reordered so all 13 emitters run BEFORE `BOOT_PHASE_WALKER`, which now sits between `BOOT_PHASE_EMIT_UI` and `BOOT_PHASE_BUILD_CACHES`. `assetCatalogRegisterWeaponModelFiles()` (which iterates the populated weapon pool) stays inside the walker block. Boot progress accumulator is order-independent (`completed_mask` bitmask). Phase 5 weight caching unaffected.
- **B-326** (MED) -- Dev Window v2 + `build-headless.ps1` post-build addin copy placed `pd.<romid>.z64` at `<BuildDir>\data\` instead of `<BuildDir>\` (install root). Post B-321 (`DEFAULT_BASEDIR_NAME = "."`) the binary's `fsFileLoad(g_RomName, ...)` searches at `$E` (install root). `release.ps1` was already correct; `dev-window-v2.ps1::Copy-AddinFiles` and `build-headless.ps1` were not. Both now sweep `..\post-batch-addin\data\*.z64` to install root first, then mirror the rest of `data\` to `<BuildDir>\data\` with `*.z64` excluded.

Build verify clean four-target via `build-session.ps1 -Session b324 -Target all/server/tests`: client 55.5 MB, updater 12.3 MB, server 22.4 MB, tests 24.6 MB. Audit: [audits/catalog-universality-walker-order-2026-05-03.md](audits/catalog-universality-walker-order-2026-05-03.md). Bug ledger entries B-324 / B-325 / B-326 in [bugs.md](bugs.md).

**Followups (out of scope for this ship):**
- Mike's existing dev install (`Build/`) already has `pd.ntsc-final.z64` at install root manually; the fix protects future fresh dev builds (and the smoke verify will confirm boot reaches title without AV on existing data).

### 2d. BYOR post-boot AV (B-323) SHIPPED (2026-05-03, competent-saha-a202bb)

Mike's playtest of the current install AVs at boot inside `challengesInit()` -- `bcopy/memcpy+146` from a `dmaExecWithAutoAlign` over-read in [src/game/challenge.c::challengeLoadConfig](../../src/game/challenge.c). Note: this AV exists in BOTH pre-BYOR and post-BYOR builds; the user's prompt framing that "BYOR completion was supposed to fully resolve" the AV was inaccurate. The actual ancestor is Pass C (S607, dev `b15cc701`, 2026-05-02), which moved each ROM segment to its own heap allocation -- exposing a long-latent N64-vs-PC struct-stride mismatch that pre-Pass-C had hidden inside the contiguous 32 MB g_RomFile.

**Root cause**: PC `MAX_BOTS` grew from 8 to 32 over the port's life. `sizeof(struct mpconfig)` and `sizeof(struct mpstrings)` both grew with it (4596 and 680 bytes per entry on PC). The on-disk segment layout is the unchanged N64 binary format, so per-entry stride is 320 bytes (mpstrings) or smaller (mpconfig). The PC code was iterating using PC sizeof for both stride and read length: `confignum * sizeof(struct mpconfig)`. With `g_MpChallenges[0].confignum = 14`, the very first iteration tried to memcpy 4596 bytes from segment offset `14 * 4596 = 64344`, ~60 KB past the 4576-byte segment. AV.

**Fix** ([src/game/challenge.c:374-469](../../src/game/challenge.c)):
1. Remove the mpconfigs `dmaExec` -- the data was always overwritten by `mpconfig->config = g_MpConfigs[confignum]` so the read was dead code. Buffer is now aligned via `ALIGN16((uintptr_t)buffer)` directly.
2. Replace the mpstrings `dmaExec` with a `bcopy` of N64-sized 320-byte chunks at N64-spaced offsets (`confignum * 320`) into the head of the PC mpstrings struct (description[200] + aibotnames[0..7] = 320 bytes -- matches N64 layout exactly), with `bzero` of the whole PC struct first to leave aibotnames[8..31] zeroed.

**Build verify clean four-target** via `build-session.ps1 -Session b323`: client 55.5 MB / updater 12.3 MB / server 22.4 MB / tests 24.6 MB. No new compile warnings.

**Smoke verify (Mike-runnable)**: launch a fresh build over the existing install (no need to wipe `data/<romid>/`); the AV in `challengesInit` should be gone. Boot log expected: `VERBOSE: INIT: challengesInit...` followed by `VERBOSE: INIT: utilsInit...` with no AV between. All 30 challenges should render their description text and the first 8 bot names per challenge.

**Audit**: [audits/catalog-universality-pivot-plan-2026-05-02.md](audits/catalog-universality-pivot-plan-2026-05-02.md) "BYOR post-boot AV triaged" section.

### 2f. Smoke Verify Gate Phase 1 SHIPPED (2026-05-11, agitated-franklin-7f16f3)

Per the 2026-05-06 super-audit's "single most valuable next move" recommendation. Project Stability 35/100 + Execution Quality 65/100 lift in one ship: every playtest blocker from the prior week (B-318 / B-324 / B-326 / Combat Sim modal-stuck / weapons-load broken / CS room Esc / build-tool ROM placement) would have been caught at build time if this gate had existed.

**Capability shipped**:

- `PerfectDark.exe --smoke <test.json>` -- new CLI flag on the existing client binary. Inert when absent. When present: parses the test JSON, applies `log_channel_mask` + `verbose` flags, schedules SDL keyboard events at `at_ms` offsets via `SDL_PushEvent`, force-exits on timeout with `SMOKE: result=...` marker. `sysFatalError` no longer blocks on a modal dialog when the harness is active. New module: `port/include/smoke_harness.h` + `port/src/smoke_harness.c` (~470 lines). Hooked at `port/src/main.c` (init pre-boot + tick inside the boot overlay pump loop) and `port/src/pdmain.c::mainTick` (per-frame).
- Runner at [`tools/smoke-verify/run.ps1`](../tools/smoke-verify/run.ps1) + `lib/Test-Assertions.ps1` + `lib/Install-Harness.ps1`. Discovers tests, filters by `-Tag` / `-Test` / `-AutoSelect` (path-of-interest glob match against `git diff --name-only <base>...HEAD`), builds per-test clean install under `.claude/smoke-verify-runs/<utc>-<test>/`, runs the binary with watchdog, applies assertions, returns 0 / 1 aggregate code.
- Three tests in [`tools/smoke-verify/tests/`](../tools/smoke-verify/tests/): `boot_smoke.json` (catches B-324 family), `stage_load_paradox.json` (catches B-323 family via `--boot-stage 0x26 --skip-intro`), `combat_sim_entry.json` (Phase 1 scaffolding for the menu-nav class; exact frame timings tune on first live run).

**Build verify clean four-target** via `build-session.ps1 -Session smoke-gate-1`: client 55.6 MB, updater 12.3 MB, server 22.4 MB, tests 24.6 MB. Pre-existing `test_pdbase_retired_audit` + `test_catalog_provider_static` source-grep failures unchanged.

**Design**: [`context/designs/engine/smoke-verify-gate.md`](designs/engine/smoke-verify-gate.md) -- architecture, schema reference, lifecycle, Phase 2 expansion plan.

**Phase 2 expansion targets** (schema accommodates already; harness module structured for them): online init smoke (lobby join + listen-host), mod load smoke (.pdmod scan + activation), skin editor smoke, Forge smoke, named-verb input grammar via actionmap (Confirm / Back / NextTab / NavUp etc.).

**Build-gate integration**: `tools/smoke-verify/run.ps1 -AutoSelect -MergeBase dev` is callable from `devtools/build-headless.ps1` post-build. Initial ship leaves this OFF by default so the first week is advisory; once the noise floor is confirmed low, the auto-merge step will block on it.

### 2j. Dev Window v2 CLI panel refinements SHIPPED (2026-05-12, adoring-turing-a53052, c127)

Mike feedback after using the c125 / archive-directive ship: LAUNCH was hidden by the always-docked Run Tests / Run Game bottom bar; the CLI tab had too many text inputs (prompt + bug-id + branch + preview pane) when one prompt textbox would do; action buttons could overwrite manually typed content; the panel did not fit at the default window size without scrolling.

**Capability shipped**:

- **Run Tests / Run Game moved off the always-docked bottom bar** and into the BUILD tab as a secondary hero pair right below the BUILD / RELEASE pair. They are no longer visible from CLI / LOG / DOCS tabs, which uncovers LAUNCH on the CLI tab.
- **Single canonical prompt textbox** on the CLI tab. `TxtCliBugId` / `TxtCliBranch` / `TxtCliPreview` are removed.
- **Body-preserving wrap-swap on action click (corrected design, 2026-05-12 same-day refinement of the initial c127 ship)**. The user's substance is extracted from the textbox at action-click time and re-wrapped with the new template: prefix + body + actionSuffix + cards block + standing rules. Switching between Goal / Plan / Investigate / Bug Fix / Review / Custom preserves the body verbatim and only swaps the syntactic frame around it. No destruction of typed content, no overwrite-confirmation modal. Extraction works by stripping the stored `CliWrapPrefix` and `CliWrapSuffix` strings from the current textbox content; if the user has nuked those markers (atypical), the whole textbox becomes the new body.
- **`Show-CliInputDialog`** small WPF modal (parented to the dev window) collects action-specific inputs: Bug Fix asks for B-NNN (required, last value remembered in `$script:CliBugIdMemory`); Review asks for an optional branch (last value remembered in `$script:CliBranchMemory`). Cancel aborts the action click with no panel change.
- **Active action button visuals** in PD cyan (`#0078A8` background, white foreground, `#005A80` border, thickness 2) via direct property override; inactive buttons fall back to ToolBtn defaults via `ClearValue` so their mouse-over trigger still works. `Update-CliActionButtonVisuals` runs after every `Set-CliAction` and on Reset / cold start.
- **No-scroll at default size**. Outer `ScrollViewer` removed from the CLI tab body. Layout is now a DockPanel with `LastChildFill="True"`; header + action row are docked Top, the launch row is docked Bottom, and the prompt+cards Grid is the LastChildFill. LAUNCH stays visible at the default `MinHeight="940"` window regardless of how tall the middle Grid grows.

**Removed in the same-day correction**: the originally-shipped dirty-tracking flow (`Update-CliPromptDirtyState`, `Confirm-CliOverwriteIfDirty`, `LblCliPromptDirty` `(custom)` badge, `CliLastAppliedTemplate` / `CliPromptDirty` state vars). All replaced by the substance-preserving model above.

**Verification**: three probes at `.claude/scratch/probe-cli-panel-*.ps1`. XAML probe asserts 17 named elements present + 4 removed names (incl. `LblCliPromptDirty`) absent + Run Tests/Run Game still present (PASS). Compose probe exercises body preservation across all 6 action swaps + body-extraction round-trip + fallback when wrap markers don't match + cards block + em-dash hygiene + archive-directive language (19 assertions, PASS). Launch probe smoke-starts dev-window-v2 for 8 s without crash (PASS). PowerShell AST parse clean. Em-dash count on every new/modified file = 0.

**Design ref**: [context/designs/devwindow-claude-cli-panel.md](designs/devwindow-claude-cli-panel.md) - UI map updated, "Body-preserving wrap-swap" section replaces the prior dirty-tracking section.

**Sprint report**: [.claude/sprint-reports/sprint-c127-cli-refinements.md](../../.claude/sprint-reports/sprint-c127-cli-refinements.md) per the c125 sprint-report contract.

### 2i. Dev Window v2 Claude CLI panel + sprint-report contract SHIPPED (2026-05-12, adoring-turing-a53052)

Per Mike's directive: "Add a Claude CLI button in the Dev window v2 ... a container that has a text box with buttons such as Goal, where I can then select card(s) and it will prompt the CLI with /goal and those as a prompt, as well as other useful functions ... The prompts should also include the requirement that they update our context and kanban system so you can easily catch back up after sprints. It should file a report specifically intended for you to do so, at which point you can interpret and dispose of the report ONLY, once you are clear and verified what it has done."

**Capability shipped**:

- New `CLI` tab in `devtools/dev-window-v2/dev-window-v2.ps1` placed after BUILD / LOG / DOCS. Header strip + action row (Goal, Plan, Investigate, Bug Fix, Review, Custom) + Bug ID + Branch row + 60/40 split of prompt textbox + multi-select card list + composed-prompt preview + mode radios + Launch / Copy Prompt / Reset.
- Card list pulled from `http://localhost:7531/api/state` (with `tools/kanban/state.json` file fallback when the kanban server is down). Filters to `active` + `backlog`; sorts active-first, then by priority then order. Live search filter on title or pillar. Selection is preserved across filter changes and action switches.
- Action-button wrapping logic produces well-formed prompts: Goal -> `/goal <text>`; Plan / Investigate -> instruction prefix; Bug Fix -> `Fix bug B-NNN: ...` plus regression-test write requirement at `tools/smoke-verify/tests/bugs/B-NNN.json`; Review -> branch or selected-cards scope; Custom -> verbatim. All wrappings append a standing-rules suffix (commit format, pre-allocated card range, mandatory sprint report path + schema).
- Two launch modes via radio buttons. Interactive (default): writes composed prompt to `$env:TEMP\pd2-cli-prompt-<UTC>.txt`, copies prompt to clipboard via `[System.Windows.Clipboard]::SetText`, opens a new `cmd.exe /K` window at project root running `claude` so Mike pastes with Ctrl+V and converses live. Headless: runs `claude --print --output-format text < <temp>` via `Start-AsyncPoolAction` on the background runspace pool, streams stdout to the Log tab on completion.
- Sprint-report contract: every launched session is mandated (by the standing-rules suffix) to write `.claude/sprint-reports/sprint-YYYY-MM-DDTHHMMSS.md` with sections Goal / Shipped / Decisions / Blockers / Follow-ups / Kanban Changes / Files Touched / Verification Notes. The Dispatch orchestrator's session-start routine scans this directory, cross-references against `tools/kanban/state.json` and `git log`, and ARCHIVES the report (moves it into `.claude/sprint-reports/archive/<same-basename>.md`) once verified. Reports are NEVER deleted - they have long-term reference value; the active directory is the orchestrator's inbox, the archive is the permanent record. The launcher creates both directories on startup so the layout is correct on a fresh checkout.

**Verification**: three probes at `.claude/scratch/probe-cli-panel-*.ps1` (gitignored). XAML probe loads the embedded markup with WPF's XamlReader and finds all 20 named CLI elements (PASS). Compose probe exercises every action wrapping with 16 assertions including em-dash hygiene (PASS). Launch probe spawns `dev-window-v2.ps1` in a background powershell process, holds 8s, confirms no startup crash, then closes cleanly (PASS). PowerShell AST parse on the modified `.ps1` is clean.

**Design**: [context/designs/devwindow-claude-cli-panel.md](designs/devwindow-claude-cli-panel.md) (326 lines, SENTINEL-terminated) - UI map, action wrapping rules, composed prompt structure, launch mechanism choice + rationale, sprint report consumption protocol (orchestrator contract), future extensions, verification procedure.

**Canonical sprint-report demo**: [.claude/sprint-reports/sprint-c125-demo.md](../../.claude/sprint-reports/sprint-c125-demo.md) - shows the full schema with Verification Notes specifically aimed at the orchestrator.

**Followups (out of scope for this ship, tracked as future extensions in the design doc)**:

- Orchestrator-side consumption logic (memory file `feedback_dispatch_orchestrator_workflow.md` update): scan-verify-archive sequence in the Dispatch session-start routine. Archive = move into `.claude/sprint-reports/archive/`, never delete.
- Saved prompt presets in `devtools/dev-window-v2/settings.json` (dropdown of last-N composed prompts).
- Sprint-report history pane inside the CLI tab so Mike can see what the orchestrator has yet to consume.
- Batch operations (queue multiple sprints in headless mode).
- Skill awareness: detect installed Claude Code skills from `~/.claude/skills/` and grey out action buttons referencing missing skills.

### 2h. Decision-Request Mechanism SHIPPED (2026-05-12, clever-swirles-d24f0c)

Per Mike's directive: "When surfacing something in a [card] that has open questions, allow the card to offer me multiple choices curated by you, or an alternate custom response from me, that gets interpreted, solidified, inquired further [if] needed, and put into action when a fresh session or current session reads the kanban board." Plus the design answers given 2026-05-11: badge + banner + animation + modal + side panel together; allow_custom always true; questions never auto-resolve; cards with open questions BLOCK active or upcoming work.

**Capability shipped**:

- Schema extension on `tools/kanban/state.json`: root gains `schema_version: 2` + `semantic_version: 0.2.0` + `x_extensibility_rule`; cards gain optional `open_questions[]` array with `{id (q-NNN), question, asked_by, asked_date, choices[{id,label,rationale,implication}], allow_custom=true, answer, answer_type, answered_date, custom_text, interpretation, interpretation_confirmed, follow_up_question_ids[]}`. Schema is additive-only with reserved `x_*` namespace.
- 6 server endpoints in `tools/kanban/server.py`: `POST /api/cards/<id>/questions` (add), `POST /api/cards/<id>/questions/<qid>/answer` (Mike answers choice or custom), `POST /api/cards/<id>/questions/<qid>/interpret` (orchestrator writes interpretation of custom answers), `POST /api/cards/<id>/questions/<qid>/confirm-interpretation` (Mike confirms or refines; refine spawns follow-up linked via id), `GET /api/open-questions` (list all unresolved), `GET /api/cards/<id>/blocked-status` (per-card). Atomic writes via temp + os.replace; CORS-permissive.
- UI surfaces in `tools/kanban/index.html`: yellow `?N` badge in card top-right corner with hover tooltip; subtle pulsing animation + light-yellow tint on cards with open questions; red `BLOCKED ON QUESTIONS (N)` strip on active-column blocked cards; top banner with Review + Dismiss; collapsible right-side panel listing every open question grouped by card with curated-choice buttons + custom textarea + skip + interpretation-pending surface; modal opens one question at a time with full rationale + implication for each choice, custom textarea, confirm + refine controls when interpretation present. Header `Questions <N>` toggle button appears when count > 0.
- Block-on-active gate: card.open_questions[] with any unresolved entry surfaces visually via the BLOCKED strip in active column; orchestrator gate enforced via the CLI evaluator.
- New CLI evaluator at `tools/kanban_evaluator.py` (parallel to `parked_evaluator.py`): subcommands `check-active-blocks` (list cards in active or priority-1 upcoming that are blocked), `interpret-pending` (list custom answers awaiting orchestrator interpretation), `cascade-on-answer --question-id q-NNN` (run parked-thread cascade when a question fully unblocks a card). Module-level helpers (`list_blocked_cards`, `list_pending_interpretations`, `find_question`) available as a library import.
- Park / unpark preserves open_questions (rides inside `x_archived_card_payload`); block-on-active fires again on unpark.

**Verification**: end-to-end probe at `.claude/scratch/probe-decision-request.py` (gitignored) walks every endpoint + CLI subcommand + the curated-answer + custom-answer + interpret + refine + confirm path. All 8 phases PASS. State restored to leave c121 q-001 unanswered so Mike sees the live UX when he opens the kanban browser.

**Design**: [context/designs/decision-request-mechanism.md](designs/decision-request-mechanism.md) (599 lines) - architecture, schema reference, full UI map, lifecycle diagrams for both curated + custom paths, block-on-active gate semantics, integration with parked threads + orchestrator + sessions, anti-patterns, file layout, explicit non-goals.

**Followups (out of scope for this ship)**:

- Daily-flow orchestrator Phase 2 hook: call `kanban_evaluator.py check-active-blocks` in step 1 audit; surface counts in briefing headlines; refuse to spawn worker sessions on blocked cards.
- Daily-flow orchestrator Phase 2 hook: call `kanban_evaluator.py interpret-pending` in step 2 state-sync; loop the pending list through Claude's reasoning to produce interpretation prose; POST back via the interpret endpoint.
- Question staleness: if a question has been open > 14 days, surface in daily briefing (same pattern as parked staleness). Not in this ship; daily-flow can layer on top.

### 2g. Daily-Flow Orchestrator SHIPPED (2026-05-11, vigilant-stonebraker-a0ef5b)

Per Mike's Spec v0.5. Automation layer that consumes the data layer (parked.json, bugs/state.json, parked_evaluator.py) + smoke-verify gate and turns them into a daily Claude-driven workflow.

**Capability shipped**:

- Scheduled daily at 06:00 America/New_York via `anthropic-skills:schedule` (task id `pd2-daily-flow`, cron `0 6 * * *`). Spawns a Claude Code session that reads `tools/daily_flow/orchestrator-prompt.md` and drives the seven-step pipeline.
- Pipeline mechanics in `tools/daily_flow/` (Python package): `lib/` (fsutil + gitutil + priority + templates + timefmt + decisions), `steps/` (one module per pipeline step), `orchestrator.py` (top-level entrypoint), `state/` (per-day audit + snapshot + cascade results, last-run breadcrumb, decision-id registry).
- Catch-up handling: orchestrator detects missed days via `state/last-run.json` and runs catch-up oldest-first before today's pass.
- Step 1 audit: git log + session-log parse + kanban diff vs yesterday snapshot + smoke-verify results -> persisted audit blob.
- Step 2 state sync: card flip (commit + scratch ref) and bug flip (linked_test smoke pass) and parked.last_checked bump. Idempotent.
- Step 3 cascade: invokes `parked_evaluator.py cascade --card-done` per flip plus `update-ready` and `update-stale` globally.
- Step 4 merge consolidation: claude/* branches ahead of dev, oldest-committerdate first, with conflict + worktree-lock skip, pre/post line-count verify, push.
- Step 5 priority sort: cards (flag desc, pillar weight, created asc), bugs (severity desc, filed_date asc), parked-ready (parked_date asc).
- Step 6 daily log: writes `context/daily-logs/YYYY-MM-DD.md` with template-enforced Decisions section (always present even when empty).
- Step 7 briefing: writes `tools/kanban/daily-briefing.json` consumed by kanban browser banner (`tools/kanban/index.html` + `/api/briefing` endpoint).
- Weekly rollup (Mondays): two-phase prepare + finalize. Decisions section preserved verbatim from dailies; narrative sections (Headline, Stalls and Blockers, Path Not Taken) written by orchestrator session. Compaction quality self-check floors at 80 lines, ceilings at 600; off-threshold flags `compaction_quality_review` and preserves dailies.
- Monthly rollup (first Monday of new month): same two-phase pattern. Floors 120 / ceilings 1200.
- Decision-ID registry: monotonic `dec-NNN` allocated at write time, threaded across daily -> weekly -> monthly logs. Sessions append `### dec-PROPOSED: <title>` to scratch files; orchestrator allocates real ids on next 6 AM run.

**Schemas**: every persisted JSON has `schema_version`, `semantic_version`, `x_extensibility_rule`. Additive-only, reserved `x_*` namespace.

**Idempotence + failure handling**: per-step audit-hash skip; partial daily log on step failure with `x_failure_point` surfaced in briefing; refuse-overwrite on state file corruption; one retry with backoff on push failure.

**Design**: [`context/designs/daily-flow-orchestrator.md`](designs/daily-flow-orchestrator.md) - architecture, schedule, pipeline diagram, rollup logic, failure modes, file layout.

**Smoke verify (Mike-runnable)**: `python -m tools.daily_flow.orchestrator --no-merge --no-push --force` from project root produces today's daily log + briefing without touching git. Output goes to `context/daily-logs/<today>.md` and `tools/kanban/daily-briefing.json`. Banner surfaces in kanban browser at http://localhost:7531/ when `tools/kanban/server.py` is running.

### 3. Input - Controller Support (Branch 2 Cohorts 5-8)

**Status**: c036 complete 2026-05-19. All 8 subtasks are DONE. Per [designs/input/input-universality-and-transitions.md](designs/input/input-universality-and-transitions.md), Cohorts 1-4 shipped earlier (layer types + scene events, layer push/pop, IMC ownership migration, per-player cutscene state); Cohorts 5-8 now close the controller-first actionmap, context gating, observer lifecycle, right-stick menu scroll, tap/hold gameplay affordances, vehicle controls, and active ImGui menu graph surface.

**Subtask state (2026-05-19, c036 closed)**:

- **s036-01 LAYER_GAMEPLAY / LAYER_MENU .imc metadata wire** -- DONE. Wired as DECLARATIVE only because g_ImcGameplay is the actionmap-init baseline (never deactivates) and g_ImcMenu is owned by the input context stack (inputctx.c), not the layer stack. [port/src/inputlayer.c](../../port/src/inputlayer.c) lines 202-220 / 240-257.
- **s036-02 10 F-key migration** -- DONE. Extended chord support (VK_CHORD_SHIFT_F1, VK_CHORD_SHIFT_F2, VK_CHORD_ALT_RETURN) + broadened [port/src/actionmap.cpp::chordVkForKeysym](../../port/src/actionmap.cpp) to detect SHIFT-only and ALT-only chords. Added 9 new ACTION_* enum entries (ids 107-115) with default bindings on g_ImcGameplay. Dispatch consumers in [port/src/pdsched.c::schedEndFrame](../../port/src/pdsched.c) with PD_DEV_BUILD gating on F6/F7/F12. All 10 raw F-key blocks + RS-click joy block removed from [port/fast3d/pdgui_backend.cpp::pdguiProcessEvent](../../port/fast3d/pdgui_backend.cpp).
- **s036-03 gfx_sdl2 raw hotkey migration** -- DONE. Backquote -> ACTION_CONSOLE_TOGGLE; F10 dead branch deleted; Alt+Enter -> ACTION_TOGGLE_FULLSCREEN via VK_CHORD_ALT_RETURN. gfxFullscreenToggle public C wrapper added in [port/fast3d/gfx_sdl2.cpp](../../port/fast3d/gfx_sdl2.cpp) (declared in [port/fast3d/gfx_sdl.h](../../port/fast3d/gfx_sdl.h)). All raw SDL_KEYDOWN handlers removed from gfx_sdl_handle_events.
- **s036-04 inputKeyPressed retire** -- DONE. Joy section deleted from [port/src/input.c::inputKeyPressed](../../port/src/input.c). After F-key + Alt+Enter migration, zero production callers pass VK_JOY_*. Function now handles keyboard + mouse only; returns 0 for joy VKs.
- **s036-05 WantCaptureKeyboard -> gameplayInputSuppressed** -- DONE. [port/fast3d/pdgui_spectator.cpp::handleKeyboard](../../port/fast3d/pdgui_spectator.cpp). The new gate folds menu push + focus loss + 50ms focus-settle into one predicate.
- **s036-06 Observer push/pop IMC symmetry** -- DONE. s_ObserverActiveSource tracks source at push; pop / abort only deactivate g_ImcObserver when source was SPECTATOR. Removed unconditional Forge / ForgeSession IMC deactivates from pop / abort (those are owned by [src/game/forgemode.c::forgeTransitionToInactive](../../src/game/forgemode.c)). Test pin in [tests/test_vehicle_observer_layer.cpp](../../tests/test_vehicle_observer_layer.cpp) updated.
- **s036-07 Right-stick scroll spec/runtime drift detector** -- DONE. Found genuine drift; synced test spec to runtime (deadzone 0.18, exp 2.0, max 1680 px/sec); added static-text TEST_CASE "right-stick scroll: runtime constants match spec" [scroll][static][link] that grep-locks the runtime constants in pdgui_backend.cpp.
- **s036-08 Menu graph completion** -- DONE 2026-05-19. The final slice moved remaining active ImGui menu push/pop transitions behind named graph edges and graph helpers. `menuGraphFirePopOp` now owns the actual pop/root release after a successful graph op, `menuGraphFireReplaceDialog` covers pop-then-push flows, and wildcard push edges preserve legacy/unregistered dialogdefs without raw renderer stack calls. Static coverage now asserts every active `port/fast3d/pdgui_menu_*.cpp` file has zero direct `menuPushDialog()` / `menuPopDialog()` calls. Verification: `c036done` client/updater build PASS, tests target PASS, focused `[input][menu_graph]` PASS (606 assertions / 31 cases), server target PASS. Legacy C/runtime stack calls remain outside this controller-facing card.

**Pillar refs**: [pillars/input.md](pillars/input.md), [pillars/menus.md](pillars/menus.md).

---

## Deferred to next session (from 2026-05-16 c3807 Tracks 2a/2c/2d + c029 crash class + c3738 wall-transition arc)

Follow-ups surfaced by the day's ship batch on `dev`. Tracked in detail in [session-log.md](session-log.md) (top-of-file entry) and bugs.md.

- **Skedar swarm behavior parity (2026-05-18)** -- VERIFIED in session `skswarm`. GPU swarm now defaults to GPU_FULL, GPU readback emits jump/surface requests, CPU/GPU paths share a benchmark-local movement-intent helper, and CPU/GPU behavior smokes on `base:mp_skedar` prove jump, surface transition, wall contact pin, and trace telemetry.
- **A4 (B-308) GPU bot AI parity** -- first slice + finish shipped earlier in the c3807 lane (commits `0edf90cb` + `6d67888d`), so the three-mode `swarm_method_t` enum + hybrid GPU-decides / CPU-executes architecture skeleton is live. Today's Tracks 2a / 2c / 2d expand the state-as-texture side-channel into a live ENet-replicated data plane (NET_PROTOCOL_VER 48). Next slices are 2e (client-side prediction via wire'd velocity extrapolation) and 2f (range-relative pos quantization for tighter wire load) per Track 2d's sprint report.

- **A5 (B-308 v1 limits) Track 2g GPU swarm dedicated-server Mode A**. `pd-server` has no GL context today; Mode A requires either CPU swarm compute on the dedicated path (broadcasting the same SVC_GPUSWARM_STATE 0x6c opcode) or a headless-GL path. Tracked as v1-limitation (a) under B-333.

- **G (c038) Jump surface collision fix** -- **IMPLEMENTED + BUILD/TEST VERIFIED 2026-05-18; pending playtest.** Stage-1 activation was not enough: the live Stage 2 path was a single center ray and still missed capsule skin/top/bottom contacts, rendered-only floors, angled ceilings, prop-model tops, and bot vertical movement. The first c038 slice implemented the generic `selfprop`-aware multi-sample Stage 2 solver and wired player/bot vertical paths. The B-339 architecture slice then moved that solver off render-owned prop/model matrices and onto `meshcollision`: stage-load world mesh from rendered room triangles, `prop->colmesh` dynamic meshes for movement-solid props, and stable transforms from `prop->pos` + `defaultobj.realrot`. Bug ledger: B-335 + B-339. Verification: queued isolated `jumpfix` client/tests/focused `[physics][jump]` PASS for the initial solver; queued isolated `meshcol` client/updater PASS, tests target PASS, focused `[physics][jump]` PASS (64 assertions / 4 cases), server PASS for the collision-owned mesh substrate. See `tools/kanban/state.json` c038.

- **B-329 / B-330 / B-331 / B-332 playtest verification** -- all four shipped 2026-05-16 as FIXED-PENDING-PLAYTEST. Recursive smoke verification covers the swarm ladder (swarm_gpu_smoke 4 -> 768 PASS 26/26, swarm_cpu_smoke 4 -> 512 PASS 22/22 with 30s dwell at 256). B-329 needs manual verification: launch Falcon-2 Airbase mission or Farsight Combat Sim and watch `bgunRender` for `animmode != 0` / `animnum > 0` on first FIRE state transition.

- **2026-05-13 audit ledger refresh** -- HF-3 (c029 B-307) FIXED-PENDING-PLAYTEST via B-330's root-cause fix; MF-3 (c3738 Slices 4-5) now includes today's Slice-4 wall-transition climb trigger; LF-1 (c036 s036-08 menu graph) CLOSED 2026-05-19 for the active ImGui/controller-facing surface.

---

## Active investigations / unresolved bugs

> Cross-track audit: [audits/incompleteness-sweep-input-context-extraction-jump-2026-05-13.md](audits/incompleteness-sweep-input-context-extraction-jump-2026-05-13.md) covers input pillar / context system / file extraction + external user accessibility / jump collision in one read. Proposes c132-c135 sprint sketches; awaiting Mike's prioritisation pass.

Latest crash fix awaiting Mike playtest:

- **B-341 load-screen crash before intro** -- FIXED-PENDING-PLAYTEST 2026-05-18. `Build\pd-client.log` first showed an AV immediately after `LOAD: bgBuildTables done` on CI/title load, then after that fix moved forward, another AV at `LOAD: calling setupCreateProps...` in `meshAttachModelToProp -> meshExtractFromModel -> extractGfxTris`. Root cause was the new B-339 `meshcollision` path trusting room gfxdata and model DL/GUNDL/Gfx/Vtx pointers too early. `meshWorldAddRenderedRoom()` now loads room gfxdata first and falls back to room geo; model extraction now uses `model_rodata_guard`, skips unsafe DL/GUNDL/Gfx/Vtx reads with diagnostics, and fails closed by attaching no dynamic `colmesh` if extraction yields no safe triangles. Verified isolated `loadcr` client/updater/tests/server/focused `[physics][jump]` PASS for the room-load fix, and `meshro` client/updater/tests/server/focused `[physics][jump]` PASS for final extraction hardening. Needs Mike playtest: normal launch should reach intro/main menu without `ACCESS_VIOLATION`; bounded `MODEL.RODATA.MISS:` / `MESHCOL:` skip logs are acceptable.

- **B-340 ACTION_USE hold-only interact regression** -- FIXED-PENDING-PLAYTEST 2026-05-18. Tap activation leaked through the PC `bondmove` release path and the hold ring stayed full after consumed holds. Current patch makes ACTION_USE tap reload-only, removes the hoverbike tap-mount release helper, keeps interaction dispatch hold-only, and makes consumed hold progress decay like release after the full-ring pin. Build verified in isolated session `hold340` (client/updater, tests, server) and focused `[press-hold]` passed. Needs Mike playtest at door/terminal/pickup targets.

- **B-339 Defection Perfect mission-start crash** -- ARCHITECTURAL-FIX-PENDING-PLAYTEST 2026-05-18. Immediate crash was an unsafe `model->matrices[mtxindex]` deref in `propobj.c::func0f0849dc()`, but the root class was movement collision reading render-owned transient matrices. Local guard remains defense-in-depth; real fix moved capsule Stage 2 to collision-owned `meshcollision` world/dynamic meshes and added a static guard proving `src/lib/capsule.c` no longer calls `func0f0849dc`, contains no `capsuleRenderedPropRayCast`, and does not read `model->matrices`. Verified isolated `meshcol` client/updater, tests target, focused `[physics][jump]`, and server; pending Mike playtest on Defection Perfect plus Grid/prop collision acceptance cases.

Open per [bugs.md](bugs.md). Latest entries (B-280 through B-290) are all FIXED-PENDING-BUILD as of S575 (2026-04-28); promote to FIXED with commit SHA when the build verification clears.

Older still-open bugs:

- **B-179, B-182, B-183**: torn-modeldef class. Critical/high crash + geometry issues. Modeldef defensive guards landed (S312); root-cause fix not yet identified. Track in `bugs.md`.

---

## Build infrastructure follow-ups

Per [audits/infrastructure-pillars-status-2026-04-27.md](audits/infrastructure-pillars-status-2026-04-27.md) Section 7:

- **Investigate the `pd_headers` 600s stall.** Likely Python tool or Git probe blocking under the wrapper's launcher policy. Surface real progress (which generator script is running) in heartbeat.
- **Factor SP-9 truncation guard** out of inline `build-headless.ps1:763-804` into a callable function in `version-util.ps1` (or new `_build-safety.ps1`) so it can be unit-tested.
- **Centralize worktree-redirect** logic into one helper called by every script (currently duplicated in 3 places).
- Add a smoke test that the smart-clean heuristic correctly forces a clean on generator change.

These do not block the critical path; pick up when context allows.

---

## Test framework follow-ups

Per [pillars/tests.md](pillars/tests.md) Known Gaps:

- CI-time drift check for each `*_pure.c` (currently hand-synced with manual `@SYNC` comments).
- F10 `.pdbase` test moves from shape-only to behavioral when F11 lands (covered by lane 1 above).
- Pin right-stick scroll runtime to its spec constants (covered by lane 3 above).
- Domain coverage extension: forge serializer round-trip, social public-mod registry, updater HTTP error paths, theme decode, menu graph completeness.

---

## Modding follow-ups

Per [pillars/modding.md](pillars/modding.md) Known Gaps:

- **DONE 2026-05-20: External-format content plus `.pdmod` transport pipeline (Kanban c3809).** Typed `*.pdxxx` files are the preferred content units; `.pdmod` is the bundle/transport envelope; root `mod.json` remains for transport/discovery; asset content uses standard editable files plus grouped `.ini` / `.tsv` metadata; authored `.bin` payloads are invalid in authored content and transport archives; engine-native data may exist only as private readable runtime cache generated from source files. Completed coverage includes the format contract, shared INI/archive scanner, metadata families, audio/music, UI/font/lang, models/maps/animations, packer/exporter integration, compatibility validation, Public Mods install, archive/folder digest alignment, and distributed-mod registry refresh. Verification passed with focused `[modding][pdmod][static][c3809]`, focused `[social][public_mods][static]`, `pdxxx_content_folder_smoke`, `pdxxx_content_transport_smoke`, `public_mods_pdmod_install_smoke`, and queued `modpipe` all build. Design: [designs/modding/external-format-pdmod-pipeline.md](designs/modding/external-format-pdmod-pipeline.md).
- **Audit and remove or wire `modmgrWriteManifest / modmgrReadManifest`** (dead legacy serializer at [modmgr.c:2456-2554](../../port/src/modmgr.c:2456)).
- Replace `manifest_pure.c` hand-sync with compile-boundary approach.

These are post-Catalog-Gate-3 candidates.

---

## Connectivity follow-ups

Per [pillars/connectivity.md](pillars/connectivity.md) Known Gaps. Not on the immediate critical path but tracked for the connectivity-focused phase that follows controller support:

- Wire `p2pIceAddPeerCandidate` from presence/invite delivery.
- Publish local STUN reflexive in presence pings.
- Replace placeholder kbps in `groupSessionRecomputeAuthority` with real measurement.
- Extend UPnP to map p2p probe ports (27102, 27103) in addition to ENet port.
- Add a configurable public TURN fallback when `bestRelayCand()` is NULL.
- Detect Opus via CMake `find_package` and auto-set `HAVE_OPUS`.
- Unify the hole-punch flow: `netStartClientWithHolePunch` should consume `p2pPairGetEndpoint`.

---

## Save / wire format follow-ups

Per [pillars/save-wire-format.md](pillars/save-wire-format.md) Known Gaps:

- Replace `tests/savebuffer_pure.c` and `tests/manifest_pure.c` hand-sync with compile-boundary approach.
- Add chained-migration behavioral test (set up `SAVE_VERSION = 4` fixture, register dummy 1->2, 2->3, 3->4 migrations, assert each ran in order).
- Co-locate version constants with cross-reference comment headers in each version-bearing file.

---

## Where to look

- For per-pillar live state and pillar-specific in-flight work: [pillars/](pillars/).
- For per-pillar active design references: [designs/](designs/).
- For active invariants that must be respected: [constraints.md](constraints.md).
- For build verify, git safety, isolated builds: [procedures.md](procedures.md).
- For the longer-term roadmap by gate / pillar / decision: [roadmap.md](roadmap.md).
