# Tests

> Catch2 v2 single-header. `pd-tests` CMake target alongside `pd` and `pd-updater`; standalone `pd-server` / `PerfectDarkServer.exe` is removed. Self-contained: no SDL, no GL, no ImGui, no ENet. Pure-C mirrors keep the test binary globals-free. Static source-text checks pin architectural invariants. Per-session isolated build wrapper + scope aliases for targeted runs.

---

## What it is

The test framework is a third compile target. Sits alongside the production targets, shares declarations through the same headers, but compiles a small, focused source list selected for tests rather than the full game tree.

Code:

- Test framework: [port/include/catch.hpp](../../port/include/catch.hpp) (Catch2 v2 single-header).
- Test main: [tests/main.cpp](../../tests/main.cpp) (`#define CATCH_CONFIG_MAIN` at line 12).
- Test inventory: 118 `tests/test_*.cpp` files plus `test_versions_pin.c`.
- Pure-C mirrors: `tests/actionmap_pure.c`, `inputctx_pure.c`, `inputlayer_pure.c`, `manifest_pure.c`, `menupool_pure.c`, `savebuffer_pure.c`, `scene_pure.c`.
- Cherry-picked pure source files from `port/src/`: `netbuf.c`, `connectcode.c`, `bondgun_cache.c`, `catalog_checked.c`, `spawn_predicate.c`, `bodies_headcount.c`, `options_forced.c`, `catalog_mgr_weapons_pure.c`.
- Stubs for unreferenced symbols: [tests/stubs.c](../../tests/stubs.c).
- Build integration: [CMakeLists.txt:840-979](../../CMakeLists.txt:840).
- Run wrapper: [devtools/run-pd-tests.ps1](../../devtools/run-pd-tests.ps1).

---

## Compile target shape

`pd-tests` defines: `PD_TESTS=1`, `AVOID_UB=1`, `_LANGUAGE_C=1`, `PAL=0`, `VERSION=2`, `ROM_SIZE=32`, `PIRACYCHECKS=0`, `MATCHING=0`. Windows links static zlib/libgcc/libstdc++ where possible and copies the matching `C:/msys64/mingw64/bin/libwinpthread-1.dll` beside `pd-tests.exe`, because the Catch2/std::chrono path can still import `clock_gettime64` through MinGW's C++ runtime.

The binary is self-contained: no SDL2, no OpenGL, no ImGui, no ENet linkage. Compile time stays low; tests run in seconds when built clean.

---

## Test inventory by domain

### Catalog
- `test_catalog_checked` - bounds + invariants on the catalog query API.
- `test_assetcatalog_deps` - dependency graph growth, dedupe, bundled-skip behavior.
- `test_catalog_mgr_weapons_api` - 10 cases including bounds, EYESPY stage-to-variant mapping, mutual exclusion of flag masks, NULL-out guard.
- `test_catalog_provider_static` - source-wide static guards for typed identity helpers, FileProvider / RomProvider call boundaries, and audio source-decoder parity such as OGG per-channel sample counts.
- `test_asset_runtime_adapters` - runtime activation coverage for typed archive source members, adapter capacity, named metadata handoffs such as gamemode/bot-profile and arena/body/head/prop/weapon/projectile/entity selector fields, and source-required activation such as rejecting primary-only arenas, primary/partial Scenarios, primary/incomplete missions, primary/model/partial-source weapons, behavior-only props, primary/model-only projectiles and entities, primary/hand-only bodies, primary-only heads, primary/portrait-only characters, primary-path-only gamemodes/bot profiles, primary/metrics-only fonts, bankless language rows, layout/nine-slice/primary-only UI rows, shader-only or primary-path-only effects, physics-only or physics-missing vehicles, texture-only HUDs, dependency-only skins, dependency-only materials, and dependency-only themes.
- `test_mod_external_archive_static` - static source-contract coverage for archive writer, packer, scanner, network delivery, examples, and runtime load handoffs, including local/network `.pdanim` command-source parsing through the loader-pool path instead of catalog-primary-only registration.
- `test_loader_pdbase_scan` - 3 cases (currently shape-only static text; needs behavioral coverage when F11 lands).
- `test_weapon_findbyid_migrated` - F2 weapon-find migration pin.
- `test_weapon_direct_reads_audit` - source-wide `g_Weapons[` substring pin per migrated file.
- `test_bondgun_cache` - cache invariants for bondgun resolution.
- `test_bodies_headcount` - body/head iteration accuracy.
- `test_spawn_predicate` - spawn validator predicate coverage.
- `test_spawn_weapon_resolved` - exhaustive `spawnWeaponNumIsResolved` walk over 0..0xFF.
- `test_spawn_weapon_mode` - v45 SVC tail alignment, CLC handicap alignment, truncated tail failure, static field order.

### Input
- `test_actionmap_flush` - flush release-edge synthesis.
- `test_input_authority` - input context stack lifecycle, deferred-pop, S197a resurrect path.
- `test_input_layer_stack` - generation-numbered handle correctness, scene event routing, source guards on the menu-layer bridge.
- `test_right_stick_scroll` - scroll math spec (deadzone, exponent, max speed, monotonicity, sign, dt accumulation).
- `test_social_toggle_imc` - IMC activation on social shell open/close.
- `test_editor_tool_hotkeys` - Forge + Skin Editor synthetic chord coverage.

### Menu
- `test_menu_stack` - 9 lifecycle cases.
- `test_menu_reachability` - 6 synthetic trees specifying the flat-menu rule.
- `test_menu_graph` - static source-text guard (no raw `ImGui::IsKeyPressed` in menu files; menu files use `pdgui_nav` helpers).

### Scene / lifecycle
- `test_scene_dispatch` - scene event vocabulary + listener registration.
- `test_cutscene_layer` - cutscene layer push/pop + flush + per-player state.
- `test_vehicle_observer_layer` - vehicle and observer layer activation, source-guarded scene events.

### Network
- `test_netbuf` - cursor-based buffer write/read, error-sticky flag, length-prefixed strings, malformed-input safety.
- `test_net_lifecycle_static` - parse-before-commit ordering on every sensitive `CLC_*` and `SVC_*` handler. The static-text discipline that catches the "trust client byte before validating" bug class.
- `test_net_cutscene_authority` - exact v56 cutscene request/result codecs, authenticated-source planning, stable client-ID/runtime-slot remapping, generation rejection, and malformed or ambiguous roster fail-closed behavior.
- `test_net_reconnect` - v57-introduced stable-slot connect-data validation, timeout-only
  retry classification, and B-1092 server-intent resolution when ENet's
  sender-local event loses the remote disconnect datum.
- `test_connectcode` - encode/decode round-trip; UI-surface no-raw-IP static guard.
- `test_manifest` - manifest container, hash compare, append/rollback on parse error.

### Save / wire
- `test_savebuffer` - bit-pack primitive coverage (1-bit, 8-bit, 13-bit cross-byte, multi-field roundtrip, 64-bit, boundary widths, alternation).
- `test_save_migration` - v1->v2 migration helper + static-text pin on live `mpsetupfileLoadWad` `if (version < 2)` block.
- `test_versions` + `test_versions_pin.c` - reads `SAVE_VERSION`, `MPSETUP_VERSION`, `NET_PROTOCOL_VER` through public headers; pins live values.

### Modding social
- `test_public_mods_static` - public-mod registry static guards (S587 era).

### Misc
- `test_smoke` - basic sanity.
- `test_random_pool` - random pool primitive.
- `test_options_forced` - forced-option handling.
- `test_swarm_boid_sim` - GPU swarm benchmark sim correctness.

---

## Pure-C mirrors

Each mirror keeps the test binary globals-free. The mirror is a small (74-437 line) reimplementation of the live algorithm. Header comment in each mirror documents a manual `diff` command for drift audit.

| Mirror | Mirrors |
|--------|---------|
| `actionmap_pure.c` | actionmap dispatch + flush logic |
| `inputctx_pure.c` | input context stack push/pop/compact |
| `inputlayer_pure.c` | layer stack handle discipline |
| `manifest_pure.c` | manifest container + serialize/deserialize |
| `menupool_pure.c` | menu pool active-slot tracking + dedup |
| `savebuffer_pure.c` | bit-pack primitives |
| `scene_pure.c` | scene event dispatch |

Drift is a known gap: each mirror is hand-synced. Replacing with a CI-time `diff` check is queued but not done.

---

## Per-session isolated builds

`devtools/build-session.ps1 -Target tests` builds the `pd-tests` target into `.claude/session-builds/<session-id>/`. See [pillars/build-dev-tooling.md](build-dev-tooling.md) for the wrapper details.

`devtools/run-pd-tests.ps1` is the targeted runner over the session wrapper:

```powershell
.\devtools\run-pd-tests.ps1 -Session <id> -Selector "[catalog][provider][static]"
.\devtools\run-pd-tests.ps1 -Session <id> -Scope catalog-provider
.\devtools\run-pd-tests.ps1 -ListScopes
.\devtools\run-pd-tests.ps1 -Session <id>           # full suite
.\devtools\run-pd-tests.ps1 -Session <id> -ListTags -NoBuild
```

Do not invoke `.claude/session-builds/<id>/pd-tests.exe` directly during AI verification. Use the wrapper even for focused selectors. It dot-sources the canonical build environment and sets the Windows process error mode before launching the binary so loader failures do not become blocking GUI dialogs. The build must also keep `libwinpthread-1.dll` beside `pd-tests.exe`; that local copy takes precedence over PATH and prevents stale `clock_gettime64` loader popups.

### Scope aliases

[devtools/run-pd-tests.ps1:69-80](../../devtools/run-pd-tests.ps1:69):

```
catalog              [catalog]
catalog-provider     [catalog][provider][static]
catalog-identity     [catalog][identity][static]
input                [input]
manifest             [manifest]
save                 [save][migration]
netbuf               [netbuf]
connectcode          [connectcode]
network-lifecycle    [lifecycle]
spawn                [spawn-weapon]
```

Use `-Scope <alias>` instead of memorizing raw Catch2 selectors.

---

## Smoke verify gate (c115)

Per-binary smoke fixtures that exercise the production exes end-to-end. Distinct from the Catch2 unit suite above. Runner: `tools/smoke-verify/run.ps1`. Fixtures live under `tools/smoke-verify/tests/*.json` (declarative log-assertion schema with `required_lines`, `forbidden_patterns`, `required_counts`, `fixtures`, `boot_args`, optional `target`, optional `runtime_strategy`). Shared and per-test install modes managed by `tools/smoke-verify/lib/Install-Harness.ps1`. Tests-pillar meta-smoke at `tools/smoke-verify/run-pd-tests-smoke.ps1`.

### Recent hardening (2026-05-19)

- `swarm_gpu_b352_stress_smoke.json` focuses the B-352 GPU Swarm crash boundary: `base:mp_felicity`, GPU_FULL, 4 -> 8 -> 16 -> 32 -> 48 -> 64 -> 128 -> 256, asserts 256 respawn/readback evidence, forbids high-count `active=0`, and exits before the longer 512/768 ladder.
- `Get-SmokeLogPath` now prefers the current centralized client log path `logs/game client/<leaf>` and falls back to historical root-level logs. The install clear path also removes both locations, which prevents false `log_missing` failures after B-349 moved client logs under `logs/`.
- Smoke runs keep the in-game crash handler enabled by default so faults return through logs and exit codes instead of native Windows modal dialogs. `PD_SMOKE_DISABLE_CRASH_HANDLER=1` is diagnostic-only for raw fault behavior.
- 2026-06-11: crash-dialog suppression is enforced in the child process as well as the runner. `crashInit()` sets `SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX`, and `tools/smoke-verify/run.ps1` reaps smoke-owned `PerfectDark`, `PerfectDarkServer`, and `WerFault` processes before launch, after runs, and during teardown. B-928 fixed the separate clean-exit heap corruption by making `videoShutdown()` free only owned display-mode heap storage; fresh `boot_smoke` exits with OS code 0 and no `exit-code override`. Firewall-rule setup failures now route through the harness warning path when the runner is not elevated, instead of dumping a raw non-terminating PowerShell error after the smoke summary.
- 2026-06-17: full `pd-tests` is green at 804 cases / 41,191 assertions after fixing two stale test issues surfaced during c3849 Wave 7 verification. `test_input_layer_stack` now pops the temporary local layer definition before the next test reset can abort a dangling stack pointer, and `test_settings_input_tab_static` now pins the shared `renderBindTable` search filter instead of expecting `s_BindSearch` inside the wrapper slice. The focused Wave 7 selector is green at 24 cases / 937 assertions.
- 2026-06-17: `needler_graph_runtime_visual_smoke` is the current runtime proof for product-default weapon graph loading. The latest post-B-933 run passed 43/43 in `.claude/smoke-verify-runs/results-20260617T235837Z.json`, with log proof for nested `.pdweapon` / `.pdeffect` / `.pdmesh` ingestion, `BONDGUN.SOURCE` filenum 2016 for `mod_needler:needler_model`, `MODASSET.RENDER` with 300 vertices / 100 tris, and scripted exit. The two retained BMP screenshots in `.claude/smoke-verify-runs/screenshots/20260617T195640-needler_graph_runtime_visual_smoke/` were visually inspected and show the `NEEDLER SOURCE MODEL RENDERED` proof overlay.
- 2026-08-10: the current source-frozen Needler receipt is `.claude/smoke-verify-runs/results-20260811T030050Z.json`, PASS 43/43 with exit 0 after B-1018 through B-1022. It proves runtime 86 survives specific selection, the public loader adapter carries model file 2016/two held functions/track type 3, the nested executable effect is admitted, and the 300-vertex/100-triangle public model reaches generated render submission. The retained captures show the proof overlay but the heavily white frame does not make the weapon silhouette visually unambiguous; sound is disabled and no effect-output channel is asserted. Treat this as partial T-ASSETS-025/V-009 evidence, not visual/audio/effect validation.
- 2026-08-12: T-TESTS-002 is validated after D-005 option A closed B-1061 and B-1062 and the long-path cache repair closed B-1063. Exact v2 plus INI migration and restart passed 24/24 at `.claude/smoke-verify-runs/results-20260813T030531Z.json`; corrupt activation rollback plus active-delete rejection passed 21/21 at `results-20260813T030952Z.json`; ordinary Agent Select passed 20/20 at `results-20260813T031306Z.json`; and the final invalid CLI process passed 12/12 at `results-20260813T032640Z.json` with exit 2 before any plan or load. The rebuilt client SHA-256 `9BC837488A34B1D2A99171558E994E7E6CBDA3DE713D9518253A2079E7D10010` then passed the canonical 17-mission, live Credits, and second-client v3 reload gate 23/23 at `results-20260813T032847Z.json`. The final full suite passes 57,277 assertions in 1,070 cases, the focused D-005/B-1063 selector passes 1,475 in 29 cases, the native-source guard passes, and the 2,869-input source/harness fingerprint remained `89D6477D6D6BD95DABFB896DE73EAB92BC2916539F8E1C46437B91BA776C9281` across the final runtime receipts.
- 2026-08-13: T-ENGINE-004 B-1077/B-1080/B-1081 has one source-frozen Combat Simulator acceptance unit. The bounded generated-audit diagnostic passed 13/13 at `.claude/smoke-verify-runs/results-20260813T152854Z.json`, proving per-owner/per-stage render witnesses, a one-shot Needler witness, and a finite 2,048-record weapon-diagnostic budget plus one suppression record. The diagnostics-disabled two-cycle production fixture passed 56/56 at `.claude/smoke-verify-runs/results-20260813T153846Z.json`, with two starts, natural hits, stats commits, match ends, endscreens, Room re-entry/Play Again, clean exit, and no weapon/capsule/render-step diagnostic leakage, watchdog, rollback, or GDL rejection. The exact full suite passes 59,094 assertions in 1,117 cases and `tools/asset_native_source_guard.py` passes. These receipts close the three bugs as regression gates; they do not yet close T-ENGINE-004's remaining Campaign, transition, reconnect, friend-authority, listen-host, or future visual-capture gates.
- 2026-08-14: B-1089/B-1090 protocol-v56 remains partial with accepted automation and one accepted current authority role. First replacement freeze `80680321...` passed 60,722 assertions in 1,150 full cases and the native-source guard. `.claude/smoke-verify-runs/results-20260814T003933Z.json` passed initiator authority 211/211; invitee authority retained 169/215. That inverse run proved central END generation 1 then START generation 2 publication with no queue rejection, but player 0 stayed at zero cutscene progress because global scenario camera/condition work inherited a remote ambient player. Current unverified contracts require one unambiguous receiver-local pointer match, defer the in-client global tick when that identity is unavailable, preserve a separate dedicated simulation policy, route graph/fallback camera conditions and animation synchronization through explicit presentation APIs, move camera animation mutation after authority preflight, and advance AI only after a committed start. Both fixtures require local player 0 body-ready/committed/active/in-progress and forbid remote-local starts. One replacement source-frozen build/full/native-source batch plus both ordinary-client authority roles remains required before regression-gate status; prior D-003 rollback receipts remain accepted because this unit does not alter startup rollback.
- 2026-08-14: The first local-context full-suite attempt was correctly rejected at 55,472/55,474 assertions and 1,149/1,151 cases. Both failures were stale static contracts: the asset-native graph test still named the replaced ambient cutscene query, and the new dedicated-policy assertion searched only after the restore call even though the policy precedes it. Both contracts are corrected together; no production source changed, the guard and runtime were not run on the rejected candidate, and one rebuilt full batch remains pending.
- 2026-08-14: The second local-context full-suite attempt advanced to 56,059/56,060 assertions and 1,150/1,151 cases, then exposed a later assertion in the same asset-native case that still labeled the camera player `current`. The whole camera contract block was audited before correction: it now requires the `presentation` label and `playerStartCutsceneForPresentation`, while the two animation-overrun checks require the presentation query. Production source and client remain unchanged; the guard/runtime remain intentionally deferred until a full pass.
- 2026-08-14: The third consolidated local-context batch passed the full suite at 60,790 assertions in 1,151 cases, then the native-source guard correctly rejected one production access plus its two stale proof tokens. Graph `set_camera_animation` directly indexed `g_Vars.players[presentation_playernum]` instead of using the sole guarded slot boundary, and the animation-overrun guard still named the replaced ambient query. Current source routes the camera lookup through `s_aiGraphRequireRuntimePlayerSlot`, removes its obsolete current-player exception, and makes both overrun proofs require `playerPresentationCutsceneInProgress`. Because this correction changes production Scenario source, the prior client is superseded; one new source-frozen all/tests build, one full-suite/guard batch, and both authority-role smokes remain pending.
- 2026-08-14: Product freeze `cc8791c1...` built client `D1C91583...`; the full suite passed 60,824 assertions in 1,152 cases and the native-source guard passed. Initiator authority passes 214/214 at `.claude/smoke-verify-runs/results-20260814T020223Z.json`. The final assertion/parser/JSON/embedded-C# batch plus `[b1085]` passed 402 assertions in 13 cases with test binary `85349949...`. Inverse authority then passed 218/218 at `results-20260814T030245Z.json`, proving exact-PID GUI-thread focus, fresh source `focus GAINED -> LOST`, target focus retention, named post-loss sequences, whole-log pre-focus sequences, owned fire/effects, two scripted exits, and zero failures/leaks. Product/test/tool fingerprint `2784d121...` was unchanged before/after. This closes the B-1082 through B-1090 friend-play cluster as regression gates without closing broader T-ENGINE-004.
- 2026-08-14: B-1064's product-source fingerprint `3b6bcfe4...` builds client `07E2D24...` and updater cleanly after rejecting two superseded compiler candidates. Focused reconnect automation passes 335 assertions in 6 cases. The accepted test binary `1A6246ED...` passes the complete 61,241 assertions in 1,157 cases, and the native-source guard passes. Four full-suite failures in the first accepted-source attempt were stale structural guards that inspected wrapper bodies or substring-collided with internal helpers; all four production boundaries were audited and the exact entry/helper contracts were corrected together without changing product source. The ordinary two-client timeout/reconnect smoke remains pending, so B-1064 and T-ENGINE-004 remain partial.
- 2026-08-14: B-1093 replaces the smoke gate's hardcoded player-0 readiness projection with `playermgrGetLocalPlayerNum()` for every player, cutscene, and control fact, and statically rejects the old forms. Product/verification aggregates `cee25049...`/`eddd3c9...` remained unchanged through isolated client/updater/tests builds, focused B-1064/B-1092/B-1093 passes 510 assertions in 9 cases, the complete suite passes 61,302 assertions in 1,159 cases, and the native-source guard passes. One ordinary two-client reconnect receipt on exact client `FA70FC97...` remains before runtime acceptance.
- 2026-08-14: The exact `FA70FC97...` reconnect receipt at `.claude/smoke-verify-runs/results-20260814T071725Z.json` is rejected at 36/93. It proves B-1093 was not this run's blocker: the ordinary client really occupied runtime slot 0, loaded/spawned Felicity, published `CLC_STAGE_READY`, and ticked. B-1094 is the production root: the blanket client return in `playerEndCutscene()` trapped the client in the local MP opening swirl before the reconnect action. Current source delegates to the mode-aware setter, protects real client cutscene exits for the complete match authority lifetime, and permits `TICKMODE_MPSWIRL` to reach normal play. Product aggregate `9e1fff25...`, exact client `011132D1...`, and verification aggregate `656c22ba...` remained frozen through the accepted 527/10 focused run, complete 61,325/1,160 suite, and native-source guard; test binary is `5F6C0647...`. Only one ordinary reconnect smoke remains pending.
- 2026-08-14: Exact-client receipt `.claude/smoke-verify-runs/results-20260814T075829Z.json` is rejected at 23/93 but proves B-1094 reached `normal=1`. B-1095 is verifier causality: the host's boot-relative 150-second stage wait spent 83 seconds before listener publication, after which the runner launched a client that needed 72 seconds to reach connect—four seconds after the host had timed out. Current source adds a pure/typed `network_listen_ready` prerequisite and sequences the host stage deadline after it. Product `8f94b807...` / client `33C8FDF8...` and verification `e94a10d4...` / tests `DFCE2DFF...` remained unchanged while focused 537/11, complete 61,337/1,161, and the native-source guard passed. Only the exact runtime rerun remains.
- 2026-08-14: The exact `33C8FDF8...` B-1095 rerun is retained but rejected at 56/93 in `.claude/smoke-verify-runs/results-20260814T082045Z.json`. It proves listener readiness, ordinary gameplay, timeout reservation/cookie retention, reconnect auth/manifest/stage replay, post-load READY, and room reclaim before the authority's exact snapshot failed. B-1096 traces that failure to the common projectile initializer omitting the reverse object link on the disconnect death-drop CMP150. Current source restores the invariant, retains fail-closed world validation, returns typed first-failure ownership, rolls compound packet writes back atomically, and maps authority-local write/send failure as retryable instead of `DISCONNECT_FILES`. This is implementation evidence only; one source-frozen build, focused/full/native-guard batch, and ordinary reconnect receipt remain required.
- 2026-08-14: B-1096 automation is accepted on frozen product `701931d8...` / client `C497DD0F...` and verification `00ec5fb4...` / tests `61FAA307...`: isolated all/tests build passes, focused reconnect contracts pass 586/12, complete `pd-tests` passes 61,386/1,162, and the native-source guard passes. The unchanged-source ordinary receipt `.claude/smoke-verify-runs/results-20260814T090751Z.json` is retained but rejected at 55/93 with zero operational failures/leaks. It clears the projectile-owner boundary and proves typed atomic retry policy, then identifies the next authority defect as `world_prop_state` on prop 4 after 118 attempted bytes. Exact world/inventory PREPARE, commit, and resumed fire remain absent; audit and repair the prop-state lifecycle before one replacement frozen batch/run.
- 2026-08-14: The B-1096 prop-state audit maps prop 4 to the departing player's original post-start held weapon, already marked for terminal deletion before a separate replacement drop is allocated. Product `b3df3ed0...` / client `F2A25827...` and verification `38c558b2...` / tests `26861DA1...` remained frozen while the isolated all/tests build passed, focused reconnect coverage passed 623 assertions in 13 cases, the complete suite passed 61,423 assertions in 1,163 cases, and the native-source guard passed. Source treats terminal non-regenerating deletion as exact-set absence, preserves deleting regenerating setup objects, emits omission evidence in PREPARE, reports stable per-predicate prop diagnostics, validates reciprocal dual links, and retires a newly allocated drop if its common transition cannot commit. One ordinary reconnect smoke remains before production acceptance.
- 2026-08-14: The exact `F2A25827...` follow-up at `.claude/smoke-verify-runs/results-20260814T094551Z.json` is retained but rejected at 55/93 with no operational failures or leaks. The stable diagnostic identifies the live host Cyclone (`prop=4`, `parent=1`) as `attachment_pair`. B-1097/SP-61 is source-confirmed: `modeldefCloneForChr()` cloned only the `modelnode *` vector while `modelGetPart()` reads the packed sorted `s16` part-number sidecar immediately after it, so the right-hand lookup read beyond the clone and `chrEquipWeapon()` left one attachment pointer unset. Current source makes the complete modeldef/node/pointer/sidecar clone one allocation, requires the hand lookup before ownership publication, keeps reconnect fail-closed, and updates the smoke's PREPARE contract for B-1096 terminal-absence evidence. This remains implementation truth until one consolidated frozen build/focused/full/native-guard batch and one replacement ordinary smoke pass.
- 2026-08-14: B-1097 automation is accepted on unchanged product `10eab368...` (2,713 files) / client `6E4AC13...` and verifier `628d2c66...` (400 files) / tests `D839DA85...`. Both isolated all/tests builds pass with empty aggregate error logs, focused B-1064/B-1092 through B-1097 passes 655 assertions in 14 cases, the complete suite passes 61,455 assertions in 1,164 cases, and the native-source guard passes. Exactly one replacement ordinary reconnect smoke remains; no earlier D-003 smoke is being repeated.
- 2026-08-14: The B-1097 replacement at `.claude/smoke-verify-runs/results-20260814T101348Z.json` is retained but rejected at 14/57. It crashes before ENet listener publication while hiding a valid `base:head_christ` sunglasses toggle; symbolization and disassembly prove `modelGetNodeRwData()` returned a null attached-head base at `body.c:634`. B-1098/SP-62 corrected the shared mutable rwdata-index/topology records: the clone collects the canonical relation graph, privately copies/remaps topology-bearing relation rodata, drops foreign HEADSPOT children, and publishes nodes/relations/packed parts atomically. Frozen product `920a236d...` / client `2039d142...` and verifier `96601f67...` / tests `c3987ec6...` pass isolated builds, focused 1,541/16, full 63,731/1,165, and the native-source guard. The exact follow-up `.claude/smoke-verify-runs/results-20260814T110358Z.json` completes both stage loads and reconnect world/inventory commit without the former access violation.
- 2026-08-14: That follow-up remains rejected at 80/93 for separate B-1099/SP-63: after reconnect, per-player cutscene state and scene layers are clear but the ordinary client retains CI's global `TICKMODE_CUTSCENE=6`; the newly minted match latch blocks ordinary reset from leaving it. Current source adds an authenticated idle-authority stage-load boundary that enters GE_FADEIN through the authoritative setter while leaving the general `HasMatch()` guard unchanged. New focused contracts pin validated latch ordering, stage-load application before player reset, normal-only gameplay admission, exact `previous_tickmode=6` evidence, and the corrected reconnect world regex. Frozen product `7437d77c...` / client `0f377e3e...` and verifier `d5fd25c9...` / tests `ef5bdc72...` pass isolated builds, focused 1,746/19, full 63,936/1,168, and the native-source guard. One replacement ordinary smoke remains; accepted D-003 smokes are not repeated.
- 2026-08-14: The sole B-1099 replacement on exact client `0f377e3e...` proves the `6 -> 0` boundary, two NORMAL gameplay epochs, one endpoint-scoped reconnect, exact world/inventory commit, five real MagSec shots, host authority acceptance, scripted exits, and no leaks. Its immutable 92/95 receipt is retained as rejected for B-1100/SP-64 verifier defects. Corrected verifier `9f6c126b...` passes the exact retained aggregate/host/client logs 98/98 in `.claude/session-builds/v1m1engine004/b1099-retained-log-revalidation.json`; compiled `[b1100]` passes 61/1. Product `7437d77c...` is unchanged, so the accepted full 63,936/1,168 and native-source guard were not rerun.
- B-353 is closed for the new B-352 machine gate: Swarm slots now prove a live prop/backlink before teardown, death polling, movement intent, and GPU handoff. The normal `swarm_gpu_b352_stress_smoke` runner passed twice after the fix (`results-20260519T194220Z.json`, `results-20260519T194503Z.json`), each with 24/24 assertions and exit 0.
- `mission_escape_hoverbed_intro.json` is the B-345 rejected-card object gate: direct-launches Area 51 - Escape (`stagenum=0x19`) and asserts the mission-start setup path loads the Elvis hoverbed model (`file 214 (PhoverbedZ) loaded`) without crash/timeout. First verified `results-20260519T201545Z.json` with 13/13 assertions and exit 0.

### Per-pillar coverage matrix (Phase-2 complete 2026-05-14/15)

- **catalog** -- `boot_smoke.json` asserts `LOADER.UNIVERSAL.OK: kind=weapon scanned=86 registered=86 envelope_failures=0 register_failures=0` plus the universal-summary line. Commits: `9d22eb59` (tighten).
- **input** -- `full_sdl_pipeline_smoke.json` drives real `key`-type events (Return / Down / Up / Escape) through SDL -> ImGui -> actionmap -> menugraph and asserts `MENU.GRAPH.FIRE source=main_menu edge=close trigger=25`. Commit `9d380f41`.
- **modding** -- `mod_load_smoke.json` + 500-byte `test_smoke_skin.pdmod` fixture. Asserts modmgr scan / parse / discovery + SHA-256 surface for a `.pdmod` staged into `<install>/mods/`. Commit `a9e531b5`.
- **physics-collision** -- `physics_capsule_basic_smoke.json` uses `--debug-spawn-at 637.0,360.0,923.0,16` (canonical CITRAINING spawn point). Chain-of-evidence (no AV + bgunTickGameplay >= 30 ticks) because `src/lib/capsule.c` has zero `sysLogPrintf` sites today. Commit `a12327a7` (depends on harness extensions `fc9645aa`).
- **save-wire-format** -- `save_init_smoke.json` + v2 `agent_smoke.json` fixture pins save initialization. The campaign release runner exercises the JSON agent path at launch and after restart. `agent_select_json_load_smoke.json` uses a test-owned save directory and ordinary title/menu input to prove Agent Select reaches the same fail-closed Agent Session activation as CLI automation, independent of stale profiles in the shared smoke install. Commit `9a3f306e` plus the 2026-08-12 T-TESTS-002 receipts above.
- **connectivity** -- `listen_host_init_smoke.json` omits `--no-net`, pins ENet init + P2P.LAN UDP 27101 bind + PRESENCE UDP 27105 bind + presence-initialised. `listen_host_peer_smoke.json` uses the runner's multi-process `processes` array to start a listen host and a loopback client in one install directory, waits for the host bind barrier, and asserts the ENet auth handshake reaches `CLC_AUTH` / client-slot assignment. Path-pinned firewall allow rule covers the binds. Commit `605faf3f`; c3813 statically guards the peer fixture, runner orchestration, clean-install timing budget, and early `--host` log routing. **c3845 (2026-06-23): `listen_host_match_smoke.json` extends that two-process loopback from the join handshake to the FULL match lifecycle (start -> spawn both players -> play -> score -> end -> endscreen). The host seeds a Combat Sim on `base:arena_mp_felicity`/`base:combat` via `--launch-mp-room ... 0` (0 bots; two humans), then `--host-autostart` fires the real lobby-leader path (`netLobbyRequestStartWithSims`, the same call the Room "Start Match" button uses -- room-assign / manifest / ready-gate all run, NO Room-UI nav) once the joined client reaches `CLSTATE_LOBBY` + ~2s settle. `--match-timelimit-sec 35` forces a seconds-granularity `g_MpTimeLimit60` override at the lv.c time-limit gate (the wire timelimit is minutes-only) so the match ends deterministically. New `MATCH:` log channel asserts 8 milestones across the concatenated host+client logs; `MATCH: player spawned slot=0` (host) AND `slot=1` (client) both appear because the spawn log derives the slot from `player->client->id` (robust against `netPlayersAllocate`'s client-side local-player->index-0 swap). Kept as a sibling to `listen_host_peer_smoke` (the join-handshake regression, untouched).**
- **tests** (meta) -- `run-pd-tests-smoke.ps1` wraps the `pd-tests` Catch2 binary with an allowlist of 6 carry-over TEST_CASE names (`test_uichrome_paths_pin`, `test_pdbase_retired_audit`, `test_catalog_provider_static`) and soft-guards the pre-existing teardown segfault exit `0xC0000005`. Commit `367f6c16`.
- **server** -- The former `dedicated_server_boot_smoke.json` path is historical after the standalone `pd-server` target was removed. Current server-path coverage should focus on in-client listen-host initialization and match/lobby lifecycle smokes.
- **input (stage-verify propagation)** -- `mp_room_flow.json`, `swarm_cpu_smoke.json`, `swarm_gpu_smoke.json` got the `LOAD: lv.c entering stage load sequence for stagenum=0xNN` + `TICK: lvTick enter tick=N stagenum=0xNN` triplet pattern that `mission_intro_flow` introduced. mp_room stays at CITRAINING 0x26; swarm tests transition to Felicity 0x43. Commit `24117635` (also adds the 1000 ms `New-SmokeSharedInstall` settle delay).

### Harness extensions delivered this phase

- `fixtures: [{src, dst}]` array in test JSON, copied by `Copy-SmokeFixtures` after install seed and before binary launch. Pre-stages `.pdmod`, agent JSON, or any other file under the install root.
- `--debug-spawn-at x,y,z,room` boot-flag (port/src/main.c `bootApplyDebugSpawnAt` + `port/src/pdmain.c` mainTick wiring of `bootDebugSpawnAtTick`). Latches at boot, fires once at frame >= 4 via `chrMoveToPos(force=true)`. Deterministic player positioning without depending on AI-script triggers. Commit `fc9645aa`.
- `--host-autostart` boot-flag (port/src/main.c `bootHostAutostartTick` + pdmain.c mainTick wiring). On the listen host only, once a remote client reaches `CLSTATE_LOBBY` and a ~2s (120-frame) settle elapses, runs a 3-phase state machine EXACTLY ONCE (`g_BootHostAutostartFired` guard): (0) create the host's room + add every connected remote client to it, (1) ~15-frame spacing, (2) fire the lobby-leader Start Match path (`netLobbyRequestStartWithSims`). Seeded from `g_MatchConfig` (populated by `--launch-mp-room`). Replaces fragile Room-UI navigation for the two-process match smoke. c3845.
  - **Room-create + membership** (c3845): the listen host boots into the global lounge (`room_id == 0xFF`), so `CLC_LOBBY_START` was rejected ("not room creator", netmsg.c:5297). Two new C-linkage bridge fns in pdgui_bridge.c make the start work: `netLobbyRequestCreateRoom()` local-replays `CLC_ROOM_CREATE` through `netmsgClcRoomCreateRead` (host becomes creator+member -> branch (a) accepts; resets the host's 1/sec room-mutation bucket first), and `netLobbyHostAddRemoteClientsToRoom(roomId)` server-side-adds each connected remote client (`roomJoin` + `cl->room_id` + `SVC_ROOM_ASSIGN`, bypassing the rate limiter). **The match participant model is room-scoped**: SVC_STAGE_START dispatches via `netSendToRoom(g_NetMatchRoomId)` (net.c:1008/2345), and the ready gate (netmsg.c:5766) + CLSTATE_GAME transition (net.c:971-979) skip non-room clients — so the joined client (slot 1) must be a room member or it never receives the stage-start and `MATCH: player spawned slot=1` never fires. (The participant POOL at netmsg.c:5511 is all-clients/unfiltered, but moot since the gate+dispatch are room-scoped.)
  - **Listen-host ready-gate self-pre-ready** (c3845, netmsg.c ~5789): when the host is a room member it lands in its own `expected_mask`, but never sends itself a `CLC_MANIFEST_STATUS`, so the gate would only fire on the 30s timeout. The host is authoritative (it just built the manifest), so its local-client bit is marked READY immediately at gate entry — the gate now fires the 3s countdown once the single remote client answers READY. Dedicated servers (`g_NetLocalClient == NULL`) unaffected; remote clients still gate on real responses.
- `--match-timelimit-sec <n>` boot-flag (port/src/main.c parse + `bootGetMatchTimeLimitSec` accessor; consumed in `src/game/lv.c` at the MP time-limit gate). Test-only seconds-granularity override of `g_MpTimeLimit60 = SECSTOTIME60(n)` applied once when the latch is set (the wire/config `g_MpSetup.timelimit` is minutes-only, 6-bit). Lets a regression match end in ~35s instead of the 1-minute floor. c3845.
- `MATCH:` log channel (LOG_NOTE, grep-friendly): `MATCH: server stage start` (net.c, after SVC_STAGE_START broadcast), `MATCH: client stage start received` (netmsg.c `netmsgSvcStageStartRead` top, client side), `MATCH: player spawned slot=N playernum=N pos=(x,y,z)` (lv.c per local player, slot from `player->client->id`), `MATCH: scores replicated tick=N` (net.c periodic + stage-start resync scores broadcast), `MATCH: stage end reason=timelimit` (lv.c host time-limit gate before mainEndStage), `MATCH: endscreen shown` (mplayer.c `mpEndMatch` before `func0f0f820c(NULL,-6)`). c3845.
- Historical `target: "pd-server"` and `runtime_strategy: "timeout-kill"` support existed for the removed standalone server smoke. Current smoke work should target `pd` listen-host paths unless the dedicated-server track is explicitly reopened.
- `runtime_strategy: "harness"` remains the normal scripted-exit path for `pd` smoke fixtures.
- `New-SmokeSharedInstall` settle delay: module-scope counter; first call exempt, subsequent calls sleep 1000 ms before wipe / seed to absorb the prior `PerfectDark.exe` atexit log flush race. Commit `24117635`.

### Known gaps in the smoke gate

- **Cross-session install lock missing.** The settle delay is intra-session only; concurrent `run.ps1` invocations across two Claude sessions can still race on `.claude/smoke-verify-install/`. Future hardening: `.claude/smoke-verify-install/.lock` file lock.
- **Retired pd-server smoke docs still exist in older audits/designs.** The standalone `pd-server` target is removed; do not revive `SRC_SERVER` or `dedicated_server_boot_smoke.json` as routine coverage unless the dedicated-server track is explicitly reopened.
- **`capsule.c` has zero `sysLogPrintf` sites.** Physics-collision smoke coverage is chain-of-evidence (no AV + tick count) rather than capsule-sweep-direct. Instrumenting `capsuleSweep` entry / `cdTestVolume` early-out unlocks a real `wall_jump_capsule_smoke` sibling.
- **System-save startup coverage remains incomplete.** Agent Profile Store activation is now unified and production-proven, but `saveLoadSystem` is not auto-called at boot; v1 to v2 migration lives in `mpsetupfileLoadWad` only. Preserve this as separate save/system coverage rather than reopening the validated Agent Select boundary.
- **Deeper peer-flow smoke coverage.** The runner supports two-process listen-host/client coverage through the `processes` array and separate `pd-host.log` / `pd-client.log` handling, including early host logs emitted before normal `sysInit()`. `listen_host_peer_smoke` proves stack init + ENet auth, and c3845's production-verified `listen_host_match_smoke` covers the golden lifecycle through **match end + endscreen** (the `MATCH:` channel, both player slots, and time-limit end). B-1064's ordinary-client fixture now proves preserved identity/room/settings, post-load READY, and exact world/inventory commit on the current B-1098 binary; B-1099's replacement still must prove resumed normal gameplay and authoritative fire on the new source. Other 1.0 extensions remain real candidate/NAT paths, co-op continuity, room mutation, and clean return-to-room evidence.

---

## Active invariants

- **c3844 final sweep proof refreshed (2026-06-17).** The current all-family
  closure evidence is: native-source guard PASS; modder workflow verifier PASS
  with 27 families, 59 archives, 31 nested archives, and 229 public sources;
  archive conformance PASS with 8,093 root / 9,125 checked archives including
  retained `Build\data\ntsc-final`; audio/mesh/animation CPU verifiers PASS
  with 2,111 / 734 / 1,062 archives; focused Public Mods / `.pdmod` / c3844 /
  c3842 / weapon-graph tests PASS with 10,782 assertions / 77 cases; Needler
  source-render smoke PASS 43/43 in
  `.claude\smoke-verify-runs\results-20260617T235837Z.json`; full c3844 smoke
  matrix PASS 9/9 and 421/421 in
  `.claude\smoke-verify-runs\results-20260617T215555Z.json`; isolated
  `final3844fix` all-target build PASS; board JSON parse PASS; final process
  cleanup clean for `PerfectDark`, `PerfectDarkServer`, and `WerFault`. The
  Needler screenshot channel now has retained, visually inspected proof BMPs
  showing the source-render overlay.

Per [constraints.md](../constraints.md), [procedures.md](../procedures.md), and the live test files:

- **Static-analysis tests pin architectural invariants.** Examples:
  - [test_weapon_direct_reads_audit.cpp](../../tests/test_weapon_direct_reads_audit.cpp) - per-file `REQUIRE` of zero `g_Weapons[` substrings.
  - [test_menu_graph.cpp:66-83](../../tests/test_menu_graph.cpp:66) - no raw `ImGui::IsKeyPressed` in menu files; menus use `pdgui_nav`.
  - [test_connectcode.cpp:193-266](../../tests/test_connectcode.cpp:193) - UI surfaces use `connectCodeDecodeWithPort`; no "Enter IP:port" labels.
  - [test_input_layer_stack.cpp:295-404](../../tests/test_input_layer_stack.cpp:295) - inputctx publishes the menu-layer bridge; every actionmap query goes through `actionLayerAllows`.
  - [test_save_migration.cpp:208-224](../../tests/test_save_migration.cpp:208) - live `mpsetupfileLoadWad` `if (version < 2)` block pinned via static text.
  - [test_asset_native_source_contract.cpp](../../tests/test_asset_native_source_contract.cpp) - c3842 guard chain: Codex preflight, large-change skill gate, pre-commit hook, executable guard, Kanban/context source-of-truth, and numeric/legacy asset-reference rejection in public typed archive payloads.
- **Pure mirrors keep the test binary globals-free.** No SDL, no GL, no ImGui, no ENet in `pd-tests`.
- **Test cases land in the same commit as the invariant they enforce.** No "tests later" PRs.
- **Build verification uses `build-session.ps1`** with isolated session id; do not run shared `Build/`.

---

## What is done

Per [audits/infrastructure-pillars-status-2026-04-27.md](../audits/infrastructure-pillars-status-2026-04-27.md) Section 6:

- 35 test files covering catalog, input, menu, scene, network, save/wire, modding social, and misc.
- Pure-C mirrors for 7 critical algorithms.
- 8 cherry-picked pure source files from `port/src/` for direct testing.
- Self-contained `pd-tests` binary; sub-second compile when warm.
- Per-session isolated builds prevent concurrent-session conflicts.
- Scope aliases shorten friction for targeted runs.

---

## What is in flight

- **Cohorts 1+2** of the testing framework shipped per [designs/in-flight/testing-framework.md](../designs/in-flight/testing-framework.md) (the 2026-04-26 ADR). Cohort 3 (mission/mode/input mapping) deferred.
- Static-text guards expand each session as new invariants need pinning.
- **B-1067/B-1103/B-1104 protocol-v58 verification is accepted as one
  coherent production gate.** Pure fault-seam and static production contracts
  pin exact parsing, one-shot ownership, authenticated client-ID ordering,
  reverse rollback, mode-aware listen-host startup, NPC readiness and atomic
  resync, nonzero stage epochs, dedicated baseline queue ownership, and
  ACTIVE-only shared replication. Current product `03a65922...` and verifier
  `f5f9ba01...` pass isolated client/updater/tests builds, complete tests
  66,421/1,200, the native-source guard, and exact post-run manifest comparison.
  The focused transaction cluster passed 1,283/15 before its assertions were
  included in that complete suite.

  Exact client `5DA75BE6...` passes the seven-path ordinary-client matrix: co-op
  96/96, Counter-Op 98/98, later-player rollback 60/60, settings rollback 43/43,
  initiator authority 214/214, deterministic typed-Cyclone reconnect 99/99,
  and focus-independent invitee authority 170/170. The final route fixture's
  compiled route/reconnect contract passes 108 assertions in 2 cases. Its
  immutable raw receipt remains rejected at 169/170 with zero operational
  failures because a stale verifier regex rejected valid epoch 1; the corrected
  definition and separately hashed retained logs pass 170/170 without changing
  the product binary or logs. Final closure verifier `726a2c96...` spans 409
  files and exact tests binary `38C37DC4...`; the only pre-route changes are that
  corrected fixture and its static contract. They prove one elected listen authority, one
  signed typed match-server route, exactly one peer join, no probe/relay
  descriptor handoff, epoch-correct baseline/ACTIVE ordering, stable gameplay,
  and clean exits.

  The integrated invitee-authority B-1085/V-009 fixture remains unchanged at
  210/218 on this desktop because no foreground HWND exists for its independent
  SDL focus witness. It remains a focus/visual regression fixture and does not
  weaken or block the accepted D-003 route gate. Earlier rejected receipts are
  retained as diagnosis history in `context/session-log.md`; they are not the
  current verification state. B-1104 is a regression gate.

- **B-1076 ordinary Combat Simulator ownership is production-verified.** The
  source-frozen isolated client/updater/tests builds pass; the coherent focused
  and full batch passes 273/4 and 66,690/1,204, the post-fixture guard passes
  275/4, and the native-source guard passes. The first ordinary runtime receipt
  is retained/rejected at 51/55 because its definition used `pass` rather than
  the harness's emitted `satisfied`; both product cycles completed. The corrected
  same-client receipt `results-20260826T095854Z.json` passes 55/55 through
  ordinary Agent Select, Main Menu/Play graph, Room, two gameplay/endscreen
  cycles, and real Play Again with balanced menu owners and no watchdog. The
  unchanged invitee-authority regression `results-20260826T100235Z.json` passes
  170/170 with exactly one typed signed-route join. Static coverage pins
  `mpStartMatch` as the pre-publication cleanup owner and the exact `satisfied`
  receipt vocabulary. B-1076 is a regression gate.

- **B-1096/B-1097 corrected retirement proof is production-verified.** Exact
  client `BD0F9DAC...` and tests
  `51AD00D5...` retain the
  first accepted isolated builds, focused 468/15, complete 66,705/1,204, and
  native-source guard. The sole ordinary run at
  `results-20260826T104217Z.json` passes 101/105 and proves reciprocal Cyclone
  attachment, the real deleting/non-regenerating transition, timeout/reconnect,
  exact world/both inventories, one commit, resumed authority fire, and clean
  teardown. Its four misses correctly report a fully freed authority prop and
  pristine receiver. After Luna xhigh's bounded audit, the fixture now records
  the selected sync ID, reads the authority pool after normal cleanup, and
  requires that ID absent before auth, PREPARE `terminal_absent=0`, receiver
  `removed=0`, and `exact_set=1`. Static schema contracts reject fields on the
  fieldless absence event. No cleanup delay or reconnect wire/counter mutation
  is permitted. The first corrected automation batch passes 477 assertions/15
  cases, complete 66,714/1,204, and the native-source guard with zero freeze
  drift. Its sole runtime attempt at `results-20260826T110651Z.json` is retained
  and rejected before listener publication: the 34-character event name exposed
  a 31-character parser buffer and arrived truncated. Shared schema/harness
  source now uses a 64-byte capacity, behavior-tests the boundary, and rejects
  overflow before copying. The parser-safe refreeze passes isolated builds,
  focused 483/16, complete 66,720/1,205, the native-source guard, and unchanged
  2,734/410-file manifests on exact client `5AEC7918...` and tests
  `2674C22A...`. The replacement receipt
  `results-20260826T111558Z.json` remains rejected at 93/109: every retirement,
  absence, exact-world, inventory, and single-commit assertion passes before an
  access violation at `objTickPlayer` prevents restored gameplay/fire. The
  retained logs and symbolized binary localize one defect to reconnect snapshot
  death replay invoking live drop/prop side effects. Current source adds a pure
  planner matrix, static production-boundary coverage, and an ordinary-client
  assertion requiring one snapshot dead-state application with
  `live_side_effects=0`, `drops=0`, `score=0`, and `owner_cleanup=0` between
  exact inventories and commit. A bounded Luna xhigh audit found the companion
  topology defect: old spawn receive scheduled a prop before reparenting it
  through the same intrusive links. Pure placement tests cover attached/active/
  paused/invalid plans; static coverage pins off-list construction, local held-
  weapon pruning, map publication last, bounded scheduler membership, and the
  `topology=exclusive` transaction witness. A fresh tests-target build produced
  `A9B1ADAA...`; focused coverage passes 718/20, the complete suite passes
  66,960/1,209, and the native-source guard passes. Exact 2,734-file product
  `1C184C57...` and 410-file verifier `E0FA8E6A...` manifests have zero pre/post
  differences; exact client is `8262681E...`. The first attempted receipt is
  retained/rejected because `Target all` had left a stale tests binary. Exact
  unchanged client `8262681E...` passes the sole ordinary rerun 116/116 with
  the required retirement/absence, snapshot-side-effect, exclusive-topology,
  exact-restoration, one-commit, real-fire, clean-exit, and no-leak witnesses.
  B-1096/B-1097 are regression gates; accepted D-003 or Campaign receipts were
  not rerun. The complete section 9.10 evidence matrix is indexed at
  `context/evidence/2026-08-26-t-engine-004-closure.md`; T-ENGINE-004 is now
  validated. Milestone 1 moves to 3 validated, 7 partial, and 5 missing leaves.

---

## Known gaps

Per the audit Section 6:

- **Pure mirrors drift from production sources.** Each `*_pure.c` carries a manual `@SYNC` comment giving a `diff` command for audit. Drift is not enforced by CI. Replace with compile-boundary approach: factor algorithm into a TU that imports no globals, link both production and test against the same TU.
- **F10 `.pdbase` test pins shape only.** [test_loader_pdbase_scan.cpp](../../tests/test_loader_pdbase_scan.cpp) has 3 cases, all static-text checks; none call `loaderPdbaseScan` or assert returns. Tests would pass even if implementation were stripped. F11 needs behavioral coverage.
- **Right-stick scroll test is unrooted.** [test_right_stick_scroll.cpp:14-18](../../tests/test_right_stick_scroll.cpp:14) explicitly says runtime is `pdguiDriveImGuiNav` and the suite is the spec. If runtime drifts from deadzone 0.15 / max 1200 px/s / exponent 1.7, no test catches it.
- **`pd_headers` build watchdog stalls test runs.** Per [pillars/build-dev-tooling.md](build-dev-tooling.md), the 600s `pd_headers -j1 -v` step bottlenecks isolated test builds in some sessions.
- **No coverage for several visible runtime modules.** Forge editor (`port/src/forge/*.c`, 7 files), social shell, updater, voice, spectator, lobby portrait baking, theme decode, and the bulk of `pdgui_menu_*.cpp` have no test files. Add per-domain coverage as those modules see active changes.

---

## Active design references

- [designs/in-flight/testing-framework.md](../designs/in-flight/testing-framework.md) [TBD path; currently `designs/testing-framework-2026-04-26.md`] - ADR + execution plan.

---

## Where to look

- For build wrappers + isolated builds + queue: [pillars/build-dev-tooling.md](build-dev-tooling.md).
- For procedures around build verification: [procedures.md](../procedures.md).
- For systemic patterns the static guards defend against: [systemic-bugs.md](../systemic-bugs.md).
