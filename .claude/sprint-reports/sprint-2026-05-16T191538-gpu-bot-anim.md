# Sprint: GPU swarm spawn-time anim (POS_ONLY visual fix)

- **Date**: 2026-05-16
- **Card**: c3807 (Benchmarking)
- **Branch**: dev (no worktree per task rules)
- **Commit**: 43d5a3c8 -- "Benchmarking - c3807: GPU swarm spawn-time anim (POS_ONLY visual fix)"
- **Status**: FIXED-PENDING-PLAYTEST. swarm_gpu_smoke PASS 23/23; build verify clean across all 4 targets.

## Task framing (from caller triage)

Mike playtest: "GPU bots are just gliding towards me without any animation."

Triage walked the GPU path through three gates:
1. Default GPU mode at launch is `SWARM_METHOD_GPU_POS_ONLY`
   (`port/src/testscenarios.c:231-232` -- explicit per the B-308 first-slice
   sprint, 2026-05-15 -- to preserve a stable POS_ONLY baseline for the
   smoke tests).
2. POS_ONLY sets `do_ai = 0` in the compute dispatch
   (`port/fast3d/swarm_gpu.cpp:561`); the kernel's AI write-out block
   (lines 377-391) is skipped.
3. The CPU readback consumer short-circuits at the `if (do_ai)` gate
   (`port/fast3d/swarm_gpu.cpp:800`); `swarmTestApplyAiDecision` -- the
   only path that calls `modelSetAnimation` for GPU swarm bots -- never
   runs in POS_ONLY.

Additionally, `spawn_one_skedar` for GPU mode set `chr->myaction =
MA_NONE` (port/src/swarm_test.c:754) and made no `modelSetAnimation`
call. The chr starts with whatever bind/idle pose the Skedar body
shipped with, and `chrSetPos` then rewrites the prop position every
frame with no locomotion cycle ticking; the model translates but never
animates -- visually, sliding.

## Path picked: B (spawn-time anim)

Path A (flip default mode to GPU_FULL) was a single-line change at
`testscenarios.c:231-232`, but:
- GPU_FULL adds AI dispatch + per-frame readback consumer load on top
  of POS_ONLY's chrSetPos load.
- POS_ONLY is the documented "cheapest benchmark" baseline; flipping
  the default re-baselines the smoke + perf numbers.
- The smoke test's `SWARM_METHOD_GPU_POS_ONLY` expectation is wired
  into the swarm_gpu_smoke regex (max_speed_cap=5.0 in POS_ONLY).

Path B (spawn-time `modelSetAnimation` once, in the GPU branch of
`spawn_one_skedar`):
- Keeps POS_ONLY structurally identical for the kernel path.
- One `modelSetAnimation` call per spawn -- no per-frame cost.
- GPU_FULL is unaffected: `swarmTestApplyAiDecision` is transition-
  guarded (`cur_anim != desired_anim`) and either preserves the
  spawn-time anim (when GPU picks anim_key=1 / SKEDAR_RUNNING) or
  replaces it when AI selects stand/attack.

## The fix

`port/src/swarm_test.c::spawn_one_skedar` GPU branch only:

```c
} else {
    /* GPU mode: chr is passive prop. Position is driven externally
     * by swarmGpuStepAndApply. */
    chr->myaction = MA_NONE;

    /* c3807 (2026-05-16): kick a continuous running anim at spawn so
     * bots animate even in GPU_POS_ONLY where do_ai=0 and the GPU
     * AI write-out (anim_key) is skipped. Without this, bots inherit
     * whatever bind/idle pose the Skedar body shipped with and slide
     * across the floor as chrSetPos drives position each frame
     * without any locomotion cycle.
     *
     * GPU_FULL behaviour: swarmTestApplyAiDecision in
     * swarm_gpu.cpp's readback consumer is transition-guarded
     * (`cur_anim != desired_anim`), so this spawn-time anim is
     * either preserved (when GPU picks anim_key=1) or replaced when
     * the AI selects stand/attack. No conflict.
     */
    if (chr->model) {
        modelSetAnimation(chr->model, (s16)ANIM_SKEDAR_RUNNING,
            0, 0.0f, 0.5f, 16.0f);
    }
}
```

speed=0.5 and merge=16.0 mirror `swarmTestApplyAiDecision`'s running
branch (the canonical anim applier for GPU_FULL). `chr->model` guard
matches the dispatcher's NULL check at swarm_test.c:1507.

`bodyAllocateModel` + `chrAllocate` go through `modelmgrInstantiate*`
which calls `modelmgrInstantiateAnim` + `animInit` when `withanim` is
set (src/game/modelmgr.c:286-289), so `model->anim` is non-NULL by the
time we call `modelSetAnimation`. The dispatcher proves this works at
runtime (it's been live in GPU_FULL since c3807 first slice).

## Build verify

```
$ /c/msys64/usr/bin/bash Build/dobuild_all.sh
[542/543] Linking CXX executable PerfectDark.exe
NINJA_EXIT=0
```

All 4 targets clean:
- Build/PerfectDark.exe (58,236,803 bytes, 15:09)
- Build/PerfectDarkServer.exe (no source touched; up-to-date from 11:12)
- Build/Updater.exe (no source touched; up-to-date)
- Build/pd-tests.exe (no source touched; up-to-date)

Note on the build infra: `cc.exe` invoked through the default Bash
tool exits 1 with zero diagnostic output (stderr blackholed). The fix
was to spawn the build inside `/c/msys64/usr/bin/bash`, which uses the
right MSYS2 stdio shim. Documenting here for future workers; if the
default shell behaviour returns, run builds via the explicit msys2
bash path or via the powershell `build-headless.ps1` wrapper.

## Recursive test

```
$ powershell -NoProfile .\tools\smoke-verify\run.ps1 \
      -Test swarm_gpu_smoke -Install .claude\smoke-verify-install
...
PASS 23/23 assertions
exit code: 0
elapsed: 151.5s
```

Coverage:
- POS_ONLY path stays byte-for-byte identical for the kernel/readback
  pipeline (BENCHMARK.SWARM.GPU.VEL still fires at 5.00 unit/frame /
  300 cm/sec; ladder 4 -> 128 unchanged).
- Spawn-time `modelSetAnimation` call executes but emits no log line
  (modelSetAnimation is silent). Visual confirmation requires a
  playtest in front of the camera; the smoke test's assertion shape
  (anim-agnostic) can't catch sliding-vs-running directly.

What the smoke run confirms:
- No new crashes or warnings in 4 spawn cycles (4 -> 8 -> 16 -> 32 ->
  48 -> 64 -> 128 bots).
- spawn_one_skedar runs end-to-end without `modelSetAnimation`
  triggering an AV or log warning at any ladder step.
- Binary exits cleanly with code 0.

## Out of scope but observed

`src/game/chr.c` and `tools/smoke-verify/tests/swarm_gpu_smoke.json`
both had pending uncommitted edits when this session started -- they
are a parallel B-331 fix (chrInit myspecial-init class crash + smoke
ladder extension to 768). Left untouched; not part of this commit.

## Follow-ups

- (a) Playtest in front of a bot to confirm visual change. Smoke
  cannot prove "running anim ticking visibly"; only spawn + position
  pipelines.
- (b) If Mike wants GPU_FULL as the default eventually (Path A), it's
  a 1-line flip at `testscenarios.c:231-232`. Holding off until the
  per-frame readback consumer is promoted to a PBO + fence ring
  (filed as follow-up (c) in the prior c3807-finish sprint report).
- (c) The chrAllocate model path proves model->anim is always live;
  if a future caller bypasses modelmgrInstantiateAnim, this spawn-
  time call would silently no-op. modelSetAnimation already guards
  internally (model->anim NULL check at model.c:1891), so no AV risk
  -- just a quiet failure mode. Worth a note in pillars/server.md if
  a non-modelmgr chr alloc path ever lands.

## Files

- `port/src/swarm_test.c` -- spawn_one_skedar GPU branch (+25 lines)
