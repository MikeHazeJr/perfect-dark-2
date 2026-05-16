# Sprint Report — GPU Swarm Capacity + Collision Parity

**Date**: 2026-05-15 21:05 UTC
**Branch**: dev
**Commit**: `2be3aa26` — Benchmarking - c029: GPU swarm parity (B-309 + B-310)
**Parent**: `e5771cda` (Engine - c3738: Slice 5 surface_up raycast for wallrun)

## Dispatch

Two parity items on the GPU swarm pipeline:

1. **B-309** (collision constraints): port the CPU swarm's half-radius treatment + perim-disable lock to the GPU spawn path.
2. **B-310** (spawn upgrades + capacity): port the volume picker / 20% retry / streaming respawn to GPU mode, or bump `SWARM_GPU_MAX` past 256 if the SSBO grows cleanly.

## Headline Findings

**Both B-309 and B-310 were already structurally fixed at the spawn-helper level.** Audit reread of `port/src/swarm_test.c` confirmed:

- `spawn_one_skedar()` at line 538 is the SHARED spawn helper for both `SWARM_METHOD_CPU` and `SWARM_METHOD_GPU`. The per-bot radius/height scaling (lines 598-605) and the perim-disable lock (lines 623-624) apply UNCONDITIONALLY before the method-branch at line 626. So B-309 collision parity has been correct since commit `7f1b4e55` (2026-05-01).
- `respawn_swarm()` dispatches via `s_SpawnStrategy` to `respawn_ring()` or `respawn_volume()`; both call `spawn_one_skedar()` regardless of method. `respawn_volume()` uses `swarm_pick_volume_with_retry()` for the 1.0x then 1.2x grown retry. `respawn_slot()` feeds both the streaming-fill empty-slot path and the kill-respawn path. `death_poll_and_respawn()` caps refill at `SWARM_REFILL_PER_FRAME = 16` per frame. **None of these are method-gated.** So B-310 spawn upgrades have applied to GPU mode since the S594h-Unit-A/B/C consolidations (2026-05-01 to 2026-05-02).
- The B-309 and B-310 bugs.md entries were filed 2026-05-02 (S593h-followup-3) referencing the **pre-consolidation** GPU code path. The consolidation (commits `ffd1fc68` 2026-05-01 + `7f1b4e55` 2026-05-01) pre-dated the bug files.

**The only actionable gap was the GPU SSBO ceiling** `SWARM_GPU_MAX = 256`, which clamped the GPU compute dispatch to 256 active bots even when the cycler ladder set the target above. After A1a (CHRVTX pool bumped to 4096) and the c029 SAFE_MAX retirement (B-311 fix in `715424c4`), the SSBO cap is the GPU swarm's only remaining ceiling.

## Change Set

`port/fast3d/swarm_gpu.cpp`:
- Bumped `SWARM_GPU_MAX` from `256` to `TESTSCEN_SWARM_MAX_COUNT` (= 4096).
- Rewrote the comment block above the define to explain the capacity math + cross-reference the shared `spawn_one_skedar()` helper for the B-309 + B-310 parity context.
- Updated the file-header docblock to reflect the 48-byte boid record (was "32") and the 4096 capacity (was "256").

`context/bugs.md`:
- B-309: marked `FIXED-PENDING-PLAYTEST 2026-05-15 (c029, dev)` with full root-cause explanation (already-shared spawn helper).
- B-310: marked `FIXED-PENDING-PLAYTEST 2026-05-15 (c029, dev)` with full root-cause explanation + capacity math.

**Files NOT touched in this dispatch (pre-existing in-flight changes from other dispatches):**
- `port/fast3d/gfx_pc.cpp` — opcode-mask hardening
- `port/src/main.c` / `port/src/pdmain.c` — `--launch-load-agent` smoke fast-path
- `.claude/smoke-verify-install/*` — install state from prior smoke runs
- `C:\Users\mikeh\Perfect-Dark-2\bugs.md` (parent copy) — divergent (has B-324/B-325/B-326 not in canonical); cross-checkout sync deferred

## Capacity Math (4096-ceiling)

- **SSBO**: 4096 × `sizeof(boid_record=48)` = **196,608 B (~192 KB)** once at first dispatch via `glBufferData(...)`. OpenGL 4.3 spec requires `GL_MAX_SHADER_STORAGE_BLOCK_SIZE >= 128 MB`; nVidia/AMD typically advertise GB-class limits. Trivial.
- **`s_BoidScratch` BSS**: 4096 × 48 = ~192 KB. Static, no allocation.
- **Per-frame upload**: `glBufferSubData` writes `sizeof(boid_record) * count` (NOT × `SWARM_GPU_MAX`); scales with active bots, not the cap.
- **Dispatch groups**: `ceil(count / SWARM_GPU_LOCAL_X=64)`. At 4096 that's 64 groups, far under `GL_MAX_COMPUTE_WORK_GROUP_COUNT` minimum (65535).
- **CPU-side raycast**: `chrSurfaceLocoSampleFloorNormal` runs once per active bot per frame. At 4096 it's lighter than the CPU swarm path's full AI tick on 4096 chrs.
- **memset cost**: `memset(s_BoidScratch, 0, sizeof(boid_record) * SWARM_GPU_MAX)` is now 192 KB per frame. Sub-millisecond on modern hardware; left as-is (could be tightened to `max(count, prev_count)` but that's gold-plating).

## Build Verify — PASS

`devtools/build-headless.ps1` four-target build, all green:

```
[PASS  ]  CLIENT       3s   -> PerfectDark.exe (55.5 MB)
[PASS  ]  UPDATER      1s   -> Updater.exe (12.3 MB)
[PASS  ]  SERVER       2s   -> PerfectDarkServer.exe (22.4 MB)
[PASS  ]  TESTS        0s   -> pd-tests.exe (24.9 MB)
```

No new warnings. `swarm_gpu.cpp` recompiled cleanly with the new `SWARM_GPU_MAX = TESTSCEN_SWARM_MAX_COUNT` constant; `TESTSCEN_SWARM_MAX_COUNT` is visible because `testscenarios.h` is included at line 47, before the `SWARM_GPU_MAX` define at line 126.

## Smoke Verify — Deferred

The smoke runner is invoked via `tools/smoke-verify/run.ps1` (PowerShell). The current session has a deny-rule that blocks all PowerShell invocations (including `pwsh -File ...`), so `swarm_gpu_smoke` and `swarm_cpu_smoke` could not be re-run in this dispatch.

Mitigation: the spawn-helper logic is structurally unchanged (only the SSBO cap moved), so the smoke tests' assertions about "spawn happens / ladder cycles / no FATAL through 128" remain valid. `swarm_gpu_smoke` last-PASS 2026-05-14 reached count=128; the cap bump only affects what happens ABOVE the smoke's tested ladder, which is in playtest scope.

**Recommended follow-up** (Mike or next dispatch): run `tools/smoke-verify/run.ps1 -Test swarm_gpu_smoke -Test swarm_cpu_smoke` against the new commit `2be3aa26` and verify both still PASS. The smoke tests' ladder is 4 -> 128, well below the new 4096 ceiling.

## Playtest Coverage (Manual)

The B-309 + B-310 bug entries describe the validation steps in detail. Summary:

- **GPU swarm at 256/512/1024/2048/4096**: cycle ladder beyond the old 256 cap; expect each step to log `BENCHMARK.SWARM.GPU: SUMMARY count=N` and the post-cycle `target=N actual=~N` ratios to match CPU mode at equivalent counts.
- **Per-bot scale visualization**: confirm bots range from tiny (0.2x) to medium (0.6x) per the squared-rand bias; small bots have small hitboxes.
- **Phase-through**: bots overlap and pass through each other (perim disabled).
- **New crash class at higher counts**: if a FATAL or AV surfaces above 256, file as B-311c (NOT mask with another clamp — the dispatch hard-rules say "document and triage").

## Notes

- `gpu_fallback_seek_tick` (the no-GL-4.3 fallback) iterates `s_SwarmCount` directly without bounding by `SWARM_GPU_MAX` — it's already 4096-ready and benefits from the cap raise transparently.
- The per-frame perim-disable re-assertion in `swarmTestTick` (lines 1468-1515) is CPU-mode-only. This is fine for collision correctness because `chrSetPerimEnabled` in `src/game/chr.c:235` honors the `0x00040000` swarm-lock marker permanently — once set at spawn, the engine cannot re-enable perim. The per-frame loop is for the LOS / targeting short-circuit, not for collision parity.
- The dispatch said "Default Option α (data-plumbing only) unless reading reveals an existing `swarmSpawnOne(chr, mode)` helper that's already 80% there." Reading revealed that the helper is **100% there** — `spawn_one_skedar()` is the shared entry point, and the dispatch's premise that the GPU spawn path was distinct from CPU's was incorrect for the current code base. The fix collapses to Option C: bump the SSBO cap.

## Refs

- Commit: `2be3aa26`
- Kanban card: `c029` (Skedar Benchmark phase)
- Bugs: B-309 (FIXED), B-310 (FIXED), B-311 (already FIXED in `715424c4`)
- Pre-existing in-flight changes (NOT in this commit): `gfx_pc.cpp` opcode mask, `main.c` / `pdmain.c` `--launch-load-agent` smoke fast-path
