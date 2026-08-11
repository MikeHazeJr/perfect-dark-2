# Tests

> Catch2 v2 single-header. `pd-tests` CMake target alongside `pd` and `pd-updater`; standalone `pd-server` / `PerfectDarkServer.exe` is removed. Self-contained: no SDL, no GL, no ImGui, no ENet. Pure-C mirrors keep the test binary globals-free. Static source-text checks pin architectural invariants. Per-session isolated build wrapper + scope aliases for targeted runs.

---

## What it is

The test framework is a third compile target. Sits alongside the production targets, shares declarations through the same headers, but compiles a small, focused source list selected for tests rather than the full game tree.

Code:

- Test framework: [port/include/catch.hpp](../../port/include/catch.hpp) (Catch2 v2 single-header).
- Test main: [tests/main.cpp](../../tests/main.cpp) (`#define CATCH_CONFIG_MAIN` at line 12).
- Test inventory: 60 `tests/test_*.cpp` files plus `test_versions_pin.c`.
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
- B-353 is closed for the new B-352 machine gate: Swarm slots now prove a live prop/backlink before teardown, death polling, movement intent, and GPU handoff. The normal `swarm_gpu_b352_stress_smoke` runner passed twice after the fix (`results-20260519T194220Z.json`, `results-20260519T194503Z.json`), each with 24/24 assertions and exit 0.
- `mission_escape_hoverbed_intro.json` is the B-345 rejected-card object gate: direct-launches Area 51 - Escape (`stagenum=0x19`) and asserts the mission-start setup path loads the Elvis hoverbed model (`file 214 (PhoverbedZ) loaded`) without crash/timeout. First verified `results-20260519T201545Z.json` with 13/13 assertions and exit 0.

### Per-pillar coverage matrix (Phase-2 complete 2026-05-14/15)

- **catalog** -- `boot_smoke.json` asserts `LOADER.UNIVERSAL.OK: kind=weapon scanned=86 registered=86 envelope_failures=0 register_failures=0` plus the universal-summary line. Commits: `9d22eb59` (tighten).
- **input** -- `full_sdl_pipeline_smoke.json` drives real `key`-type events (Return / Down / Up / Escape) through SDL -> ImGui -> actionmap -> menugraph and asserts `MENU.GRAPH.FIRE source=main_menu edge=close trigger=25`. Commit `9d380f41`.
- **modding** -- `mod_load_smoke.json` + 500-byte `test_smoke_skin.pdmod` fixture. Asserts modmgr scan / parse / discovery + SHA-256 surface for a `.pdmod` staged into `<install>/mods/`. Commit `a9e531b5`.
- **physics-collision** -- `physics_capsule_basic_smoke.json` uses `--debug-spawn-at 637.0,360.0,923.0,16` (canonical CITRAINING spawn point). Chain-of-evidence (no AV + bgunTickGameplay >= 30 ticks) because `src/lib/capsule.c` has zero `sysLogPrintf` sites today. Commit `a12327a7` (depends on harness extensions `fc9645aa`).
- **save-wire-format** -- `save_init_smoke.json` + v2 `agent_smoke.json` fixture. Booted with `--portable` so `saveDir = install dir`. Asserts `SAVEMIGRATE: Initialized ...` + `SAVE: initialized -- save dir:`. Deeper `saveLoadAgent` path deferred (Agent Select UI nav not yet harnessable). Commit `9a3f306e`.
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
- **Save-pillar deeper paths not exercised.** `saveLoadAgent` requires scripted Agent Select UI nav (blocked by post-Combat-Sim crash class in `mp_room_flow`'s deeper Room sub-screens); `saveLoadSystem` is not auto-called at boot; v1->v2 migration lives in `mpsetupfileLoadWad` only.
- **Deeper peer-flow smoke coverage.** The runner now supports two-process listen-host/client coverage through the `processes` array and separate `pd-host.log` / `pd-client.log` handling, including early host logs emitted before normal `sysInit()`. `listen_host_peer_smoke` proves stack init + ENet auth, and c3845's `listen_host_match_smoke` now covers the full match lifecycle through **match end + endscreen** (the `MATCH:` channel, both player slots, time-limit end). Remaining future extensions on the same harness: room settings mutation, countdown cancel, killfeed, and Return to Room assertions. NOTE: `listen_host_match_smoke` is authored but NOT YET RUN -- Mike compiles/runs; the new `--host-autostart` / `--match-timelimit-sec` flags, `MATCH:` log lines, and JSON have not been build-verified.

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
