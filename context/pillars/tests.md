# Tests

> Catch2 v2 single-header. `pd-tests` CMake target alongside `pd` and `pd-server`. Self-contained: no SDL, no GL, no ImGui, no ENet. Pure-C mirrors keep the test binary globals-free. Static source-text checks pin architectural invariants. Per-session isolated build wrapper + scope aliases for targeted runs.

---

## What it is

The test framework is a third compile target. Sits alongside the production targets, shares declarations through the same headers, but compiles a small, focused source list selected for tests rather than the full game tree.

Code:

- Test framework: [port/include/catch.hpp](../../port/include/catch.hpp) (Catch2 v2 single-header).
- Test main: [tests/main.cpp](../../tests/main.cpp) (`#define CATCH_CONFIG_MAIN` at line 12).
- Test inventory: 35 `tests/test_*.cpp` files plus `test_versions_pin.c`.
- Pure-C mirrors: `tests/actionmap_pure.c`, `inputctx_pure.c`, `inputlayer_pure.c`, `manifest_pure.c`, `menupool_pure.c`, `savebuffer_pure.c`, `scene_pure.c`.
- Cherry-picked pure source files from `port/src/`: `netbuf.c`, `connectcode.c`, `bondgun_cache.c`, `catalog_checked.c`, `spawn_predicate.c`, `bodies_headcount.c`, `options_forced.c`, `catalog_mgr_weapons_pure.c`.
- Stubs for unreferenced symbols: [tests/stubs.c](../../tests/stubs.c).
- Build integration: [CMakeLists.txt:840-979](../../CMakeLists.txt:840).
- Run wrapper: [devtools/run-pd-tests.ps1](../../devtools/run-pd-tests.ps1).

---

## Compile target shape

`pd-tests` defines: `PD_TESTS=1`, `AVOID_UB=1`, `_LANGUAGE_C=1`, `PAL=0`, `VERSION=2`, `ROM_SIZE=32`, `PIRACYCHECKS=0`, `MATCHING=0`. Static linkage on Windows (`-Wl,-Bstatic -lwinpthread`).

The binary is self-contained: no SDL2, no OpenGL, no ImGui, no ENet linkage. Compile time stays low; tests run in seconds when built clean.

---

## Test inventory by domain

### Catalog
- `test_catalog_checked` - bounds + invariants on the catalog query API.
- `test_catalog_mgr_weapons_api` - 10 cases including bounds, EYESPY stage-to-variant mapping, mutual exclusion of flag masks, NULL-out guard.
- `test_catalog_provider_static` - source-wide static guards for typed identity helpers, FileProvider / RomProvider call boundaries.
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

## Active invariants

Per [constraints.md](../constraints.md), [procedures.md](../procedures.md), and the live test files:

- **Static-analysis tests pin architectural invariants.** Examples:
  - [test_weapon_direct_reads_audit.cpp](../../tests/test_weapon_direct_reads_audit.cpp) - per-file `REQUIRE` of zero `g_Weapons[` substrings.
  - [test_menu_graph.cpp:66-83](../../tests/test_menu_graph.cpp:66) - no raw `ImGui::IsKeyPressed` in menu files; menus use `pdgui_nav`.
  - [test_connectcode.cpp:193-266](../../tests/test_connectcode.cpp:193) - UI surfaces use `connectCodeDecodeWithPort`; no "Enter IP:port" labels.
  - [test_input_layer_stack.cpp:295-404](../../tests/test_input_layer_stack.cpp:295) - inputctx publishes the menu-layer bridge; every actionmap query goes through `actionLayerAllows`.
  - [test_save_migration.cpp:208-224](../../tests/test_save_migration.cpp:208) - live `mpsetupfileLoadWad` `if (version < 2)` block pinned via static text.
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
