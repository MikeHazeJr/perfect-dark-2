# PD2 build queue, Milestone 0: clean build timing

Measured 2026-11-22, 00:15 to 00:31 UTC, by claude-pd-integration-20261121 (T-TOOLING-020).

## Setup

- **Source:** dev `905ed513`, cloned into `C:\PD2\timing`. It is a local clone of the canonical repository. The three bgfx submodules came from that repository's own copies at the pinned commits.
- **Builds:** on C:, per Mike's 2026-11-21 word ("Builds on C:").
- **Exclusive slot:** each run held the coordination hub's `build` slot alone, so no other build ran at the same time. A scratch deletion was running on D: (hard disk) throughout; these builds do not use that disk.
- **Machine:**
  - CPU: Intel i9-12900KF, 16 cores and 24 threads.
  - Memory: 31.9 GB, 16 GB free.
  - C:: Kingston SNVS2000G NVMe, which holds the clone and every build folder.
  - D:: Seagate ST2000DM008 hard disk.
  - Defender real-time scanning is on. Its exclusions cannot be read without administrator rights.
  - The Windows Search service is running.
- **Bar:** Mike's 2:32 (152 s). That is a cold, clean, full rebuild of client, updater and tests, with the cache cleared.

## Runs

Each run starts from an empty build folder. ccache is off (`CCACHE_DISABLE=1`) unless stated.

| Run | What | Wall time |
|---|---|---|
| A | The wrapper as sessions build: `build-session.ps1 -Session t0 -Target all -Clean`, then `-Target tests` | **201.9 s** (95.2 + 106.7) |
| B | `build-headless.ps1` direct | Not run. The wrapper's own step files split run A (below). |
| C | Raw toolchain: CMake configure, then one Ninja build of `pd pd-updater pd-tests -j24` | **157.1 s** (configure 3.1, build 153.9) |
| D | Run C on the other drive | Not run. Builds are on C: now, and D: is the hard disk, busy with the deletion. |
| E | Run C with Defender exclusions | Not run. Exclusions are Mike's call, and nothing was changed. |
| F | Run C with ccache on and an empty private cache (the shared store untouched) | **179.1 s** (3.5 + 175.5; 0 of 1,471 hits) |
| G (experiment) | Run C with `-g0` for the test objects only (timing clone, not committed) | **114.5 s** (2.9 + 111.6) |

## Breakdown

### Run C (`.ninja_log`)

The build ran 1,891 edges using 3,417 CPU-seconds, at an average parallelism of 22.2 of 24. It is CPU-bound.

| Share | CPU-s |
|---|---|
| Test sources | 2,085 (61%) |
| `port/src` | 466 |
| `port/fast3d` | 273 |
| Links and other | 240 |
| `src/game` | 152 |
| bgfx-pd, bx and bimg | 146 (4%) |
| `src/lib` | 55 |

Links take 7.0 s for `PerfectDark.exe`, 9.0 s for `pd-tests.exe` and 2.6 s for `Updater.exe`. Ninja (1.13.2) starts most test objects after the game's, so the slowest test files start at about 85 s and set the tail.

### Run A (the wrapper's step files)

| Step | Time |
|---|---|
| Configure | 9 s |
| Generate headers | 18 s, run on its own |
| Client | 62 s |
| Updater | 2 s |
| Second call: configure | 1 s |
| Second call: tests | 104 s |

### Slowest compile units in run C (seconds)

| s | Unit |
|---|---|
| 44.3 | `pd-tests.dir/tests/test_asset_native_source_contract_stage_payloads_2.cpp.obj` |
| 35.4 | `pd-tests.dir/tests/test_mod_external_archive_static_2.cpp.obj` |
| 30.7 | `pd-tests.dir/tests/test_asset_native_source_contract_stage_payloads_1.cpp.obj` |
| 29.3 | `pd-catch-main.dir/tests/main.cpp.obj` |
| 29.2 | `pd-tests.dir/tests/test_mod_external_archive_static_3.cpp.obj` |
| 26.9 | `pd-tests.dir/tests/test_forge_level_doc.cpp.obj` |
| 26.6 | `pd-tests.dir/tests/test_mod_external_archive_static.cpp.obj` |
| 26.4 | `pd-tests.dir/tests/test_forge_level_doc_2.cpp.obj` |
| 22.7 | `pd-tests.dir/tests/test_asset_native_source_contract_stage_payloads_3.cpp.obj` |
| 21.1 | `pd-tests.dir/tests/test_mission_graph_runner.cpp.obj` |
| 20.2 | `pd-tests.dir/tests/test_asset_native_source_contract_walkers.cpp.obj` |
| 18.3 | `pd-tests.dir/tests/test_asset_native_source_contract.cpp.obj` |
| 18.2 | `pd-tests.dir/tests/test_catalog_provider_static.cpp.obj` |
| 15.7 | `pd-tests.dir/tests/test_vehicle_graph_runner.cpp.obj` |
| 15.3 | `pd-tests.dir/tests/test_asset_runtime_adapters.cpp.obj` |
| 15.1 | `pd-tests.dir/port/src/forge_level_doc.cpp.obj` |
| 14.9 | `pd-tests.dir/tests/test_asset_native_source_contract_families.cpp.obj` |
| 14.8 | `pd-tests.dir/tests/test_mod_author.cpp.obj` |
| 14.3 | `pd-tests.dir/tests/test_stage_rules.cpp.obj` |
| 14.3 | `pd-tests.dir/tests/test_menu_graph.cpp.obj` |

## Verdict and causes

Run A takes **201.9 s, over the 152 s bar**, so Milestone 0 does not pass as the wrapper builds today. Each cause is measured and has a fix:

1. **Two wrapper calls and a separate header step: 44.8 s** (A minus C). The client and updater build in one Ninja call and the tests in a second, so nothing overlaps the long test files or the 18 s header step.
   - Fix: build `pd`, `pd-updater` and `pd-tests` in one Ninja call. This is a build-script change, and the queue's job uses it.
2. **Debug info on test objects: 42.6 s** (C minus G). Catch2 reports each failure's file and line without debug info. The client keeps `-g` for its crash backtraces.
   - Fix: build `pd-tests` and `pd-catch-main` with `-g0`. This is a CMakeLists change.
3. **ccache on cold misses: 22.0 s** (F minus C). The queue builds with ccache off, as the spec says, so nothing changes here.
4. **Test sources are 61% of all compile work.** After fixes 1 and 2 the build is about 115 s (run G), well under 152 s. Splitting the long test files further is not needed for the bar.

With fixes 1 and 2, a cold, clean, full build of client, updater and tests takes **about 114.5 s, which is 1:55**. Both are build-script fixes, so they go in as a normal reviewed change before the queue goes live.

The timing clone is deleted after this report.

## After the fixes (proof, same clone, 00:35 to 00:38 UTC)

Fixes 1 and 2 were made on dev `905ed513`:
- `build-session.ps1` and `build-headless.ps1` gained `-Target full`, which builds client, updater and tests in one Ninja call. The header step also lost its `-j1`.
- `CMakeLists.txt` builds `pd-tests` and `pd-catch-main` with `-g0`.

The wrapper as sessions build, `build-session.ps1 -Session t1 -Target full -Clean` with ccache off, took **128 s** (compile 120 s) instead of 201.9 s. That is **under the 152 s bar**. `pd-tests.exe` from that build passed every test: 2,598 cases and 710,889 assertions in 25 s (`m0-proof-pdtests.log`). **Milestone 0 passes with the fixes.**
