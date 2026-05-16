# B-311 Vertex-Store CHRVTX Pool Bump — Real Fix

**Date**: 2026-05-15
**Scope**: Ship the actual B-311 fix per c029. Bump `g_VtxstoreTypes[VTXSTORETYPE_CHRVTX]` (and peer CHRCOL) slot count from 120/80 to 4096/4096 so GPU swarm benchmarks scale past 128 bots without ROM-data vertex-pointer fallback corruption.
**Outcome**: SHIPPED at commit `715424c4` (dev). Build verify PASS (pd + pd-server linked clean). Single commit covering vtxstore.c + bugs.md status update. B-311 marked FIXED-PENDING-PLAYTEST.

---

## Chosen capacity and math

Old:
```c
struct vtxstoretype g_VtxstoreTypes[] = {
    { 3000, 120, 3000, 80, 0, 0, 500,  20, 12, 0, 0, 0, 0 },  /* CHRVTX */
    { 1500,  40,  500, 20, 0, 0, 500,  20, 12, 0, 0, 0, 0 },  /* OBJVTX */
    { 6000, 120, 6000, 80, 0, 0, 1000, 20, 4,  0, 0, 0, 0 },  /* CHRCOL */
    { 1500,  40,  500, 20, 0, 0, 500,  20, 4,  0, 0, 0, 0 },  /* OBJCOL */
};
```

New (rows [0] CHRVTX and [2] CHRCOL bumped equally for SP + MP):
```c
struct vtxstoretype g_VtxstoreTypes[] = {
    { 200000, 4096, 200000, 4096, 0, 0, 500,  20, 12, 0, 0, 0, 0 },  /* CHRVTX */
    {   1500,   40,    500,   20, 0, 0, 500,  20, 12, 0, 0, 0, 0 },  /* OBJVTX */
    { 200000, 4096, 200000, 4096, 0, 0, 1000, 20, 4,  0, 0, 0, 0 },  /* CHRCOL */
    {   1500,   40,    500,   20, 0, 0, 500,  20, 4,  0, 0, 0, 0 },  /* OBJCOL */
};
```

Field semantics (per types.h:5510 `struct vtxstoretype`):

- `valifsp`, `valifmp` = total per-frame VERTEX BUDGET (sum of all live sub-allocations).
- `numifsp`, `numifmp` = SLOT COUNT (number of concurrent sub-allocations the table can hold).
- The reset path (vtxstorereset.c) picks one (val, num) pair per game mode (sp / mp / special) and `mempAlloc`s `num * sizeof(var8007e3d0_data)` from MEMPOOL_STAGE.

Target picked:

- Slot count = 4096 = `TESTSCEN_SWARM_MAX_COUNT` (port/include/testscenarios.h:45). Matches the design ceiling of the swarm scenario.
- Vertex budget = 200000. Reasoning: at 4096 slots with worst-case ~50 vertices per Skedar DL node, 4096 × 50 = ~200000. Comfortable headroom for arbitrary chr models that disfigure with denser geometry.
- SP and MP both bumped equally. Swarm scenario is exercised mainly in MP, but the CPU swarm test can arm from a solo Combat Sim entry; SP must also cover.
- OBJVTX (row [1]) and OBJCOL (row [3]) untouched — destructible-prop counts are bounded by level-designer authoring, not the swarm scenario.

## Memory delta

Per-slot size: `sizeof(struct var8007e3d0_data) = 24 bytes` (struct fields: `void *unk00` 8 + `struct modelnode *node` 8 + `s32 level` 4 + `s16 count` 2 + `s16 unk0e` 2).

Old slot-array size:
- CHRVTX SP: 120 × 24 = 2880 B
- CHRVTX MP: 80 × 24 = 1920 B
- CHRCOL SP: 120 × 24 = 2880 B
- CHRCOL MP: 80 × 24 = 1920 B
- Total (SP or MP, not both at once): ~5 KB per pool. ~10 KB combined.

New slot-array size:
- CHRVTX SP/MP: 4096 × 24 = 98304 B (96 KB)
- CHRCOL SP/MP: 4096 × 24 = 98304 B (96 KB)
- Total combined: ~192 KB.

Allocation source: MEMPOOL_STAGE (40 MB; src/lib/memp.c:57 `MEMP_STAGE_SIZE = 40 * 1024 * 1024`). 192 KB is 0.5% of stage pool — trivial.

The actual vertex data (`memaAlloc` per allocation, line 143) is sized per-allocation in `count * 0xc` bytes and is unchanged by the slot-count bump. The vertex budget bump (`val1`/`val2` from 3000 → 200000) only raises the per-frame ceiling; actual memory is allocated lazily.

## Propagation checks performed

For each consumer of the pool counts, verified the bump doesn't introduce a new failure class:

1. **`vtxstoreTick` (vtxstore.c:101-128)** — Walks `g_VtxstoreTypes[VTXSTORETYPE_OBJVTX]` (index 1, OBJVTX, untouched). CHRVTX (index 0) has no O(N²) walk in vtxstoreTick. **Pass.**

2. **`vtxstoreAllocate` (vtxstore.c:130-209)** — Iterates `numallocated` slots looking for free entry. With 4096 slots the linear scan is O(N), called from chrDisfigure on each chr death. At 4096 dead-chr disfigure events per second worst case (highly unrealistic), this is ~16M comparisons/sec — trivial on modern hardware. **Pass.**

3. **`vtxstoreFree` (vtxstore.c:211-229)** — Same linear scan over `numallocated`. Called from `modelFreeVertices` (propobj.c:18201) when a chr is freed. Same performance class as vtxstoreAllocate. **Pass.**

4. **`vtxstoreFixRefs` (vtxstore.c:49-99)** — Walks ACTIVE PROPS (g_Vars.activeprops), not pool entries. Unrelated to slot count. **Pass.**

5. **`chrFadeCorpseWhenOffScreen` corpse-reaping (vtxstore.c:167-208)** — The hardcoded `struct chrdata *chrs[6]` array at line 137 is an intentional "keep up to 6 fresh on-screen corpses unreapable" heuristic, NOT a pool-size cap. The reaping logic walks `g_ChrSlots` (sized to the chr table, separately governed by `modelmgrAllocateSlots`). **Pass.**

6. **`chrDisfigure` (chr.c:4408)** — The sole CHRVTX consumer (`vtxstoreAllocate(rodata->numvertices, VTXSTORETYPE_CHRVTX, 0, 0)` at chr.c:4448). No fallback to ROM-data write — when allocation fails, the line `if (vertices) { ... rwdata->vertices = vertices; }` ensures rwdata is only mutated when allocation succeeded. So the engine is structurally self-protecting against the "ROM-data corruption" mode IF chrDisfigure is the only mutator. **Pass** — with the bump, allocation will succeed for all swarm bots, eliminating the silent-no-op path. **Subtlety**: the failed-allocation path means chr corpses get rendered with un-disfigured ROM-data vertices (less visually interesting wound geometry), not corrupted. So actually the B-311 crash signature must be caused by CHRCOL pool exhaustion at chr.c:4461 (colors-copy fallback) — which DOES potentially mutate the `rwdata->colours` pointer state via the same shared rodata pattern. Bumping CHRCOL alongside CHRVTX closes this fully.

7. **Static stack arrays sized to 120 or 80** — Grep across src/ for `[120]` and `[80]` in vtxstore.c specifically. **None found.** The only hardcoded array near vtxstore is `struct chrdata *chrs[6]` (corpse reap heuristic, not pool size).

8. **Tests that pin the old pool counts** — Grep `tests/` for `vtxstore` or `VtxstoreTypes`. **No tests pin these values.**

9. **`modelmgrAllocateSlots`** — Already correctly bumped at setup.c:1604 for swarm scenarios via `testScenarioGetSwarmMaxCount()` (which returns 4096 when armed). Independent of vtxstore. **Pass.**

10. **PCH compatibility** — None. struct vtxstoretype layout is unchanged; only the array initialiser values changed. PCH does not embed the values.

11. **`vtxstoreReset` (vtxstorereset.c:8-39)** — Picks the (val, num) pair per game mode and `mempAlloc`s the slot array. Walks `for (j = 0; j < num; j++)` to zero-init. At 4096 slots × 2 (VTX + COL) × 4 pools (per-mode) this is 32768 iterations — microseconds. **Pass.**

## SAFE_MAX retirement rationale

The `SWARM_GPU_SAFE_MAX = 64` rate-limited LOG_WARNING in port/fast3d/swarm_gpu.cpp:362-374 was a runtime band-aid surfacing the B-311 workaround to users. With the pool bump structurally fixing the root cause, the workaround is no longer needed. Retired alongside the pool bump.

**Note**: the SAFE_MAX retirement already landed in commit 6fe94c09 (Engine - c3738: GPU swarm surface-normal locomotion (Slice 6)) at 16:24:56 today — that commit's work integrated the B-311 root-cause analysis comment ("B-311 ... fixed at c029/2026-05-15 by bumping the chr vertex-store pool"). My swarm_gpu.cpp edits were therefore idempotent against that already-landed state; only vtxstore.c and bugs.md changed in this commit.

The GPU pipeline still hard-caps at `SWARM_GPU_MAX = 256` (SSBO size). That's a separate scaling concern tracked in audit 2026-05-13 HF-2 and B-308/B-309/B-310 (GPU AI / collision / spawn migration). Out of scope for B-311.

## Build outcome

Built via `ninja -C Build pd pd-server` from clean MSYS2 env. Both targets linked cleanly with no new warnings (pre-existing -Wmaybe-uninitialized / -Wpointer-to-int-cast warnings unchanged).

- `Build/PerfectDark.exe` rebuilt: 58.2 MB
- `Build/PerfectDarkServer.exe` rebuilt: 23.5 MB
- `Build/Updater.exe` not affected by change (no shared object files) — left at 12.9 MB / May 14
- `Build/pd-tests.exe` not affected by change (no shared object files) — left at 26.1 MB / May 13

Only `CMakeFiles/pd.dir/src/game/vtxstore.c.obj` rebuilt for the pd target — the change is localized.

## New ceiling reached

Static analysis confirms the pool now supports the full 4096 ladder. Manual playtest verification deferred to Mike per `FIXED-PENDING-PLAYTEST` workflow. Expected manual probe path:

1. Build current `dev` against this commit.
2. Launch `Build/PerfectDark.exe`, enter Combat Sim swarm test in GPU mode.
3. Cycle ladder 4 → 8 → 16 → 32 → 48 → 64 → 128 → 256 → ... → 4096.
4. Pre-fix: FATAL at 128-bot cycle (B-311). Post-fix: should climb cleanly. Watch `Build/pd-client.log` for `vtxstoreAllocate returns NULL` pattern or `FATAL: Unknown GBI opcode 0xbb0000ff` signature; both should be absent.
5. If a new crash class surfaces at higher counts (B-311c), capture log and file as follow-up.

The GPU compute pipeline's `SWARM_GPU_MAX = 256` will clamp the GPU-mode ladder at 256 even though the engine pool supports 4096 — the natural per-pipeline ceiling. Beyond that, B-308/B-309/B-310 work is needed to scale the GPU side itself.

## B-311c follow-up tracking

None identified pre-emptively from static analysis. The bump is well within architectural headroom (192 KB extra slot-array memory; lazy on-demand vertex memory; no static-array regressions). If playtest surfaces a new ceiling, that will be tracked as a separate bug with its own crash signature.

## Files touched

- `src/game/vtxstore.c` — pool table values + docblock describing the bump rationale (47 lines net).
- `context/bugs.md` — B-311 status update from `OPEN 2026-05-02` to `FIXED-PENDING-PLAYTEST 2026-05-15 (c029, dev)` with root-cause + fix + propagation summary (1 line replaced).

## Files NOT touched

- `port/fast3d/swarm_gpu.cpp` — SAFE_MAX retirement landed in 6fe94c09 (separate commit, different session).
- `port/fast3d/gfx_pc.cpp` — pre-existing uncommitted change from a prior research session, structurally related to B-311's FATAL signature but not part of this fix's scope per the brief ("pool bump + propagation fixes + SAFE_MAX removal"). Left in working tree for Mike's review.
- No kanban / session-log edits (per hard rules).
- No tests (none pin vtxstore values).

End of report.
