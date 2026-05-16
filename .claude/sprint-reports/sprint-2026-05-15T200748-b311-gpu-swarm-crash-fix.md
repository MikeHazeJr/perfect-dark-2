# B-311 GPU Swarm Crash Above 64 Bots — Diagnosis & Blocker Report

**Date**: 2026-05-15
**Author**: Child worker, dev branch (no commit, no file changes)
**Scope**: Investigate B-311 (GPU swarm crash above 64 bots), fix in scope of swarm_gpu.cpp, build verify, climb the SAFE_MAX ceiling.
**Outcome**: STOPPED at root-cause-analysis stage. The actual root cause is **NOT** in `port/fast3d/swarm_gpu.cpp`; it lives in the inherited engine vertex pool (`vtxstore`). No code changes shipped. Reporting blocker per the "structurally larger than a single dispatch" hard rule.

---

## Summary

The H1-H5 hypotheses in the task brief (memset bounds, SSBO size mismatch, display-list handoff buffer, shader count gate, memory barrier race) **all rule out** under code review. The compute pipeline in `swarm_gpu.cpp` is correct: SSBO sized to SWARM_GPU_MAX (256), kernel has explicit `if (i >= P.count) return;`, memory barrier present, readback uses `count` bytes, dispatch group count is `(count + 63) / 64`. There is no display-list buffer produced by the compute kernel — the only output path is `chrSetPos` writing `chr->prop->pos`, which goes through the normal chr render path.

The actual root cause is **vertex pool exhaustion in the engine-side vtxstore subsystem** at chr counts > ~120.

---

## Evidence

### swarm_gpu.cpp end-to-end review (port/fast3d/swarm_gpu.cpp)

- **Line 107** `#define SWARM_GPU_MAX 256` — hard cap, SSBO sized to this.
- **Line 132** `static boid_record s_BoidScratch[SWARM_GPU_MAX]` — 256-record scratch buffer.
- **Line 162** GLSL kernel: `if (i >= uint(P.count)) return;` — count-gate present, H4 ruled out.
- **Line 284** `glBufferData(... sizeof(boid_record) * SWARM_GPU_MAX, NULL, ...)` — SSBO allocated 256-wide once at first use.
- **Line 315** `if (count > SWARM_GPU_MAX) count = SWARM_GPU_MAX;` — caller's count clamped.
- **Line 337** `memset(s_BoidScratch, 0, sizeof(boid_record) * SWARM_GPU_MAX)` — full 256 cleared each tick. H1 ruled out: this is the LOCAL CPU scratch, not the SSBO.
- **Line 348** `glBufferSubData(... sizeof(boid_record) * count, ...)` — only first count records uploaded; SSBO indices `count..255` retain prior state, but the kernel's count-gate ensures the kernel never reads them.
- **Line 370** `groups = (count + 63) / 64` — workgroup math correct.
- **Line 372** `glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT)` — barrier present; followed by `glGetBufferSubData` which is a blocking call. H5 ruled out.
- **Line 376** readback uses `sizeof(boid_record) * count` — bounded correctly. H2 ruled out.
- **No display-list buffer in swarm_gpu.cpp** — H3 ruled out. The compute kernel writes positions/velocities only; rendering happens via `chrSetPos` -> `modelSetRootPosition` and the regular chr render pass.

### The actual root cause: `src/game/vtxstore.c`

```c
struct vtxstoretype g_VtxstoreTypes[] = {
    { 3000, 120, 3000, 80, 0, 0, 500,  20, 12, 0, 0, 0, 0 },  /* CHRVTX */
    ...
};
```

`numifsp = 120` and `numifmp = 80` are the chr-vertex sub-allocation slot counts. `vtxstoreAllocate` at `src/game/vtxstore.c:130` returns NULL when no free slot exists, or when the vertex budget (`val2`) is exhausted. The compatibility cycle is `4 -> 8 -> 16 -> 32 -> 48 -> 64 -> 128 -> 256 -> 512 -> ... -> 4096`. The pool slot count of 120 is the **exact threshold** where B-311 fires at 128.

When `vtxstoreAllocate` returns NULL, the chr model keeps the ROM-data vertex pointer (`rwdata->dl.vertices = rodata->dl.vertices`). This is generally safe to render, but if multiple chrs share the same modelnode's rwdata at the same time, lighting/colour updates and display-list mutations from one chr can corrupt the data another chr is mid-rendering. This produces the `fdbb0000ffff0000` corrupted display-list-word signature recorded in B-311.

### Smoke test record

`.claude/smoke-verify-runs/20260514T032045Z-swarm_gpu_smoke/pd-client.log` was inspected. The most recent swarm_gpu smoke run does NOT actually activate the swarm scenario (no `TESTSCEN.SWARM` or `BENCHMARK.SWARM.GPU` log lines emitted). The c115 smoke test description claims "B-311 does NOT trip" — that claim is based on the binary exiting cleanly, but the scenario was never reached in-game. The c115 smoke is a **false-positive** for B-311 closure.

### Bonus correctness finding (in scope of fast3d, NOT fixed here)

`port/fast3d/gfx_pc.cpp:2446`:
```c
uint32_t opcode = cmd->words.w0 >> 24;
```

`cmd->words.w0` is `uintptr_t` (64-bit on x86_64; see `include/PR/gbi.h:1559`). When the parser reads garbage data with non-zero high 32 bits, `w0 >> 24` produces a 64-bit value that truncates to a non-byte-sized `uint32_t`. The switch dispatch (with cases like `(uint8_t)G_TEXTURE` = 0xBB) FAILS to match, and the parser FATALs.

The correct fix is `uint8_t opcode = (uint8_t)(cmd->words.w0 >> 24);` or `uint32_t opcode = (cmd->words.w0 >> 24) & 0xFF;`. This is a real correctness bug in fast3d display-list parsing but is **not the root cause of B-311** — it only changes the FATAL signature to a silent skip-bad-opcode + render-garbage symptom. I did NOT ship this fix in scope of the swarm_gpu task; should be filed as its own ticket.

---

## Why I stopped

Per the hard rule:
> If you reach a point where the fix is structurally larger than a single dispatch... STOP and report the blocker concisely with the specific evidence — do not half-ship a fix that doesn't actually close B-311.

A meaningful fix to close B-311 requires:

1. **Bump `g_VtxstoreTypes[VTXSTORETYPE_CHRVTX].numifsp/numifmp`** from 120/80 to (at minimum) 4096 to match `TESTSCEN_SWARM_MAX_COUNT`. This requires bumping `valifsp/valifmp` (total vertex budget) proportionally to avoid budget exhaustion. The pool comes from `MEMPOOL_STAGE` (see `src/game/vtxstorereset.c:28`). 4096 × 24 bytes/entry = ~96KB; verify MEMPOOL_STAGE has the headroom.
2. **Audit `vtxstoreAllocate` allocation pattern at 4096-chr count** — `chrFadeCorpseWhenOffScreen` corpse-reaping logic (`vtxstore.c:170-208`) walks `g_ChrSlots` and may need tuning for 4096-slot tables.
3. **Audit other `[CHRVTX]`-indexed code paths** — `vtxstoreTick`, `vtxstoreFixRefs`, anything iterating `g_VtxstoreTypes[].unk24[]`.
4. **Audit `modelmgrAllocateSlots`** — already-correctly bumped at `src/game/setup.c:1604` for swarm scenarios via `testScenarioGetSwarmMaxCount()`, but other pool consumers (collision broadphase, prop draw queue, animation slots) may also need bumps.

Items 1-4 are an inherited-engine multi-system change. Not safe to ship in a single drive-by swarm_gpu commit. Risk: cascading allocation failures in MEMPOOL_STAGE, performance regressions from larger vertex budget, untested 4096-vertex-sub-allocation behavior.

---

## Files of interest (all paths absolute)

- `C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike\port\fast3d\swarm_gpu.cpp` — compute pipeline (correct)
- `C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike\port\src\swarm_test.c` — caller; cycle ladder at line 70 spans 4..4096
- `C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike\port\include\testscenarios.h:45` — `TESTSCEN_SWARM_MAX_COUNT = 4096`
- `C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike\src\game\vtxstore.c:38-43` — **the actual bottleneck** (pool size 120/80 for CHRVTX)
- `C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike\src\game\vtxstorereset.c` — pool sizing per game mode
- `C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike\src\game\chr.c:1487` — `modelFreeVertices(VTXSTORETYPE_CHRVTX, model)` on chr removal
- `C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike\port\fast3d\gfx_pc.cpp:2446` — bonus fast3d opcode-extraction bug (separate from B-311 root cause; should be filed as own ticket)

---

## What was NOT done (and why)

- **No code changes shipped** — root cause is structurally outside scope of swarm_gpu.cpp.
- **No SAFE_MAX clamp removed** — would un-mask the bug without fixing it.
- **No vtxstore pool bump** — multi-system change, requires owner review; not safe as a drive-by.
- **No fast3d opcode-extraction fix** — separate bug; would only mask the FATAL signature, not the underlying corruption.
- **No bugs.md update** — B-311 is still OPEN. Bumping it to FIXED-PENDING-PLAYTEST without a fix would be false reporting.
- **No commit** — nothing to commit.
- **No build verify** — no changes to verify.

---

## Recommended next steps (for the coordinator / Mike)

**Option A (recommended)**: Open a follow-up ticket "B-311a: bump CHRVTX vtxstore pool for 4096-bot swarm scenario" with the file/line evidence above. Scope: ~50 lines of code across `vtxstore.c`, `vtxstorereset.c`, and possibly `bss.h` / MEMPOOL sizing. Estimated effort: 1 child session with a thorough cross-system propagation check.

**Option B**: Open a follow-up ticket "B-311b: fast3d opcode extraction truncates incorrectly on 64-bit uintptr_t" with the gfx_pc.cpp:2446 evidence. Scope: 1-line fix in fast3d, plus a smoke test. Estimated effort: 30 minutes.

**Option C**: Leave B-311 OPEN; the SAFE_MAX=64 clamp in `port/fast3d/swarm_gpu.cpp:112` is the only safe workaround until A and B both land.

---

## Audit / propagation references

- `context/audits/2026-05-13-followup-and-migration-sweep.md` HF-2 (SWARM_GPU_MAX vs TESTSCEN_SWARM_MAX_COUNT drift; comment fixed but root cause not)
- `feedback_no_half_measures` — "Catalog / system foundations are full or broken. Defensive fallbacks to legacy hide registration gaps; strengthen the new system instead." Applies here: the SAFE_MAX=64 clamp is a half-measure; the right fix is in vtxstore.

---

End of report.
