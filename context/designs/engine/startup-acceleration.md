# Startup Acceleration + Boot Progress UI

> **Pillars**: Engine (threading + extract pipeline), UX (boot progress overlay).
> **Status**: APPROVED 2026-05-03 by Mike (Q1 to Q6 below). Phase 1 in flight. Phases 2 to 5 queued sequential.
> **Date**: 2026-05-03 (initial), 2026-05-03 (resolution pass).
> **Author**: bold-chaplygin-e3b96e session.
> **Related in-flight**: B-318 fix (gate deadlock so per-asset emitters actually run on first launch) is in another session at local SHA `local_d0edb241`. Phases 1 to 3 here proceed in parallel; Phase 4 measurements fold in B-318's emitter timing once it lands.

---

## Why this exists

Mike's ask, 2026-05-03 14:32 ET: "See what we can do about speeding up our startup (and display something on screen when catalog work is happening to show progress (maybe a load bar at the bottom and status label). Any reason we can't multi-thread the process?"

The boot today is silent for 8 to 16 seconds. The window exists but is unresponsive (Windows paints "Not Responding" on it). There is no progress feedback. Once B-318 lands, first-launch boot grows because the per-asset emitters will actually run end-to-end, so the cost of doing nothing is rising.

---

## What the boot does today

Source of truth: `port/src/main.c:172-590`. Sequence after `videoInit() + pdguiInit()` (window created, ImGui ready, no frames pumped) is:

| Stage | Function | Cost (clean install boot 1) | Cost (boot 2+) | Notes |
|-------|----------|----------------------------:|---------------:|-------|
| ROM file extract | `romExtractAllFiles` | ~7 s (writes 2011 files) | ~0.1 s (size-only skip) | Sequential `fwrite` of 2011 ROM file slots to `data/<romid>/files/<name>.bin` + per-file `.sha256` sidecar. |
| ROM file verify | `romExtractVerifyAll` | ~8 s | ~8 s | SHA-256 every file, compare to sidecar, quarantine + re-extract on mismatch. **Sequential, single-threaded SHA + 8 KiB streaming reads.** |
| Segment extract | `romExtractAllSegments` | ~0.2 s | <0.1 s | Walks `romdataSegmentCount()` (~12 segments). |
| Segment verify | `romExtractVerifyAllSegments` | ~0.3 s | ~0.3 s | Same SHA-on-disk pattern. |
| Boot integrity report | `romExtractEmitBootIntegrityReport` | <0.01 s | <0.01 s | Pure read of counters. |
| ROM mapping release | `romdataReleaseRom` | <0.1 s | <0.1 s | Migrates SRC_ROM segments to disk-backed copies. |
| Catalog scaffolding | `assetCatalogInit` -> `RegisterBaseGame` -> `RegisterStageSceneFiles` -> `ScanComponents` | ~0.3 s | ~0.3 s | In-memory hash table fill. |
| Manager mirrors | `catalogManagerHeadInit` + `Body` + `Arena` | <0.1 s | <0.1 s | Parallel-mirror walks. |
| **Universal walker** | `loaderWalkerLoadAll` | ~0.5 s today (1208 chr anims + 13 kinds, ZIP read + envelope parse + register) | ~0.5 s | Per-file `fsFileLoad` -> `modArchiveOpen` -> `manifest.json` extract -> envelope parse -> per-kind register callback. |
| **Per-asset emitters** | `romExtractAllPdwpn / Pdmesh / Pdanim / Pdhead / Pdbody / Pdarena / PdanimChr / Pdsfx / Pdvoice / Pdsong / Pdfont / Pdlang / Pdui` | post B-318: **est. 4 to 8 s on first boot** (write 13 kinds of `.pd*` ZIP + sidecar) | <0.5 s (size-skip) | Each emitter walks the loader_pool or seg table and writes one ZIP per asset. `pdui` defers to GL-ready hook. |
| Catalog runtime caches | `catalogBuildRuntimeCaches` | <0.1 s | <0.1 s | O(1) lookup tables. |
| `mainProc -> mainInit` | various | ~0.5 s | ~0.5 s | Heap alloc, modeldef validate, title screen prep. |

Boot 2+ floor today: roughly **8 to 9 s, dominated by the verify pass.** Boot 1 today is roughly **15 to 16 s, dominated by extract + verify.**
Post-B-318 boot 1 estimate: **20 to 24 s** (extract + verify + emitters). Mike's measured `~/Downloads/Perfect Dark 2.0/pd-client.log` confirms the 8-16 s range pre-B-318.

---

## Threading feasibility audit

Per the explore-agent investigation in this session. References below are `file:line`.

### Reentrant primitives (safe to call from worker threads, no changes)

- `sha256*` (`port/src/sha256.c`): ctx is stack-local, file streaming opens its own `FILE*`. Fully reentrant.
- `decodeRgba16/32/Ia16/Ia8/Ia4` (`port/fast3d/pdgui_theme.cpp:146-200`): pure functions, no globals.
- `mod_archive_t` reader/writer (`port/src/modarchive.c`): per-archive struct with own `FILE*`. Last-error is `__thread` storage. Two threads opening different archives is safe; same archive across threads is not (shared `fp`).
- `mempAlloc` and `sysMemAlloc` (`port/src/pdmain.c:109-111, 361-362`): SDL mutex registered at boot. Already thread-safe.

### Non-reentrant chokepoints (refactor or serialize)

- **`fs.c` static return buffers** (`port/src/fs.c:69, 470, 477`): `fsFullPath`, `fsDataDir`, `fsDataPathFor` all return `static char` buffers. **Two concurrent callers trash each other's path.** This is THE blocker for any file I/O parallelization. Wide but mechanical refactor: change all three to `(out, outSize)` form and update every call site.
- `assetCatalogRegister*` and `assetCatalogResolve` (`port/src/assetcatalog.c:47-49`): single global `s_HashTable`, zero mutex. Concurrent register would corrupt linear-probe. Either add a mutex around register + resolve, or use a producer-consumer pattern (parallel parse, serial apply).
- `loaderPoolParse*Json` (`port/src/loader_pool.c:26`): explicit single-thread invariant declared in the header doc. Module-scope `s_Weapons[]` etc. arrays + `s_*Used` counters. Same fix shape: producer-consumer.
- `audio.c` decoders: file-static `dev`, `nextBuf`, `nextSize`, volume globals (`audio.c:24-77`). The audio engine itself is not safe to drive from a worker. Per-asset audio extract reads ROM segments which are already on disk after Phase 3 Pass C, so the emitters don't need the runtime audio engine. But anything that calls `sndStart` / `snd*` mid-boot would not be parallel-safe.

### Existing thread primitives (reuse, don't reinvent)

- **No thread pool exists.** Each async op uses a fresh `SDL_CreateThread` plus a status struct guarded by `SDL_CreateMutex`.
- The cleanest existing pattern is `updaterCheckAsync` (`port/src/updater.c:1279-1322`): one thread, one mutex, status fields polled by the main thread. Reusable as the template.
- `crashCreateThread` (`port/src/crash.c:562`) and the STUN worker (`port/src/net/netstun.c:479-491`) follow the same pattern.
- For a 4-to-8-worker pool, `SDL_CreateThread` against a work queue with one mutex + one condition variable is sufficient. No need to pull in `<thread>` or a new dep.

### Window/UI timing finding

Between `pdguiInit()` (`main.c:247`) and `mainProc()` (`main.c:590`), **no `videoStartFrame` / `gfx_run` / `SDL_PumpEvents` is called**. The OS message pump is dormant for the full 8 to 16 s. The window is created and `SDL_ShowWindow`'d at `port/fast3d/gfx_sdl2.cpp:206, 229`, but Windows paints it as Not Responding.

This is a hard requirement to fix before a progress UI can render. Two options:

- **Option A: pump-and-render at phase boundaries.** Add `videoStartFrame() / pdguiRender() / videoEndFrame()` calls between each phase in `main()`. Cheap to implement, but the window still freezes mid-phase (Verify is the 8 s phase, so still 8 s of unresponsive).
- **Option B: extract on worker thread, main thread spins a render loop polling progress.** Cleaner, lets the load bar actually animate. Requires a tiny render loop that doesn't depend on `mempSetHeap` (which is called inside `mainInit` in `pdmain.c:359`). ImGui itself does not need the game heap; `pdgui` already initialized at line 247.

**Recommendation: Option B.** It is the only one that produces a smooth progress bar instead of a stuttering screen-jump per phase.

---

## Proposed architecture

### Layers

1. **Layer E1: Path-buffer refactor.** Replace `fsFullPath` and friends with `(out, outSize)` forms. Touches every call site that takes the return pointer. Mechanical. Prerequisite for all parallel work.
2. **Layer E2: Boot thread pool.** New `port/src/boot_pool.{h,c}` (~150 lines). 4 to 8 worker threads via `SDL_CreateThread`. Work queue is a ring of `boot_work_t` structs (function pointer + arg + completion counter). `bootPoolEnqueue` / `bootPoolWait` / `bootPoolShutdown`. SDL mutex + cond var for queue signaling.
3. **Layer E3: Progress channel.** New `port/include/boot_progress.h` + `port/src/boot_progress.c` (~120 lines). Thread-safe. Worker threads call `bootProgressIncrement(stage_id, delta)` and `bootProgressSetLabel(stage_id, "...")`. Main thread reads `bootProgressSnapshot()` for a consistent view. Atomic counters where possible, mutex for the label (variable-length string).
4. **Layer U1: Boot overlay renderer.** New `port/fast3d/pdgui_bootoverlay.cpp` (~150 lines). Renders a full-screen ImGui overlay with: PD2 logo / wordmark on top half, per-stage progress bar at bottom (full width, 6 px tall, PD-themed), status label below the bar (current operation in human-readable text). Uses ImGui calls that don't need the game heap.
5. **Layer E4: Boot orchestrator.** Modify `port/src/main.c` to enter a boot-render loop after `pdguiInit()`. Worker threads run the extract/verify/walker work. Main thread spins `videoStartFrame -> pdguiBootOverlayRender -> videoEndFrame -> SDL_PumpEvents` at ~30 Hz until `bootProgressIsComplete()`.
6. **Layer E5: Phase parallelization.** Stage by stage:
   - **E5a Verify pass.** Replace per-file sequential loop with `bootPoolEnqueue` per file. Counters `s_AggValidated` / `s_AggRecovered` / `s_AggUnrecoverable` become atomics or per-thread accumulator + reduce. Files are independent.
   - **E5b Walker.** Producer-consumer. N workers do `fsFileLoad + ZIP open + manifest extract + envelope parse` and push parsed envelopes into a queue. Main thread (or a single drain thread) pops envelopes and runs `assetCatalogRegister*` + `loaderPoolParse*` serially. The expensive work (ZIP unpack, JSON peek) is parallel; the unguarded write is serial.
   - **E5c Per-asset emitters.** Each kind walks an independent table. Within one kind, per-asset emit is independent. Either run multiple kinds in parallel (sfx + voice + song + font are all independent), or fan out the per-asset loop within one kind across workers.

### Concurrency model summary (post-Q4 resolution)

| Phase | Model | Workers see | Main thread |
|-------|-------|-------------|-------------|
| Verify pass | Embarrassingly parallel | One file each via work queue | Render progress, drain when done |
| Walker | Fully parallel after Phase 4 structural fix to `assetCatalog` + `loader_pool` | Parse envelope + register row + populate pool, all in parallel | Render progress |
| Per-asset emitters | Parallel within kind, kinds in parallel where decoders allow | One asset each | Render progress |
| `pdui` emitter | Stays on main / GL thread | n/a | Runs during render-loop hook in `pdguiThemeCheckExtract` (already gated post-`g_TexGeneralConfigs`) |

**Phase 4 carries the structural fix** that makes walker + emitters fully parallel: a fine-grained mutex (or RW lock) on `assetCatalogRegister*` in `port/src/assetcatalog.c`, and per-thread context for `loaderPoolParse*` so module-scope counters become local. Phase 2 keeps the walker serial (single worker enqueue) so the structural change is isolated to Phase 4. See Q4 resolution for rationale.

### Where the progress bar plugs in

`port/src/main.c` between `pdguiInit()` and `mainProc()`:

```
bootProgressInit();
bootProgressSetStage(STAGE_VERIFY_FILES, "Verifying assets", weight=0.45);
bootProgressSetStage(STAGE_VERIFY_SEGS,  "Verifying segments", weight=0.05);
bootProgressSetStage(STAGE_WALKER,       "Building catalog", weight=0.10);
bootProgressSetStage(STAGE_EMIT_WPN,     "Extracting weapons", weight=0.04);
... (one per emitter)

bootPoolInit(numWorkers=6);

bootKickoffWork();  // spawns the verify + walker + emit work into the pool

while (!bootProgressIsComplete()) {
    videoStartFrame();
    pdguiBootOverlayRender();   // reads bootProgressSnapshot()
    videoEndFrame();
    SDL_PumpEvents();
    SDL_Delay(33);  // ~30 Hz
}

bootPoolShutdown();
```

Ordering constraints carry through into `bootKickoffWork`: `assetCatalogInit / RegisterBaseGame / ScanComponents` must complete BEFORE the walker fires; the walker must complete BEFORE the per-asset emitters fire (emitters read loader_pool which the walker populates); `pdui` waits for GL.

### Non-goals

- Not changing `mainInit` or `mainLoop` rendering. The boot overlay is a pre-game overlay, then the existing pipeline takes over.
- Not parallelizing the single-threaded audio engine init.
- Not introducing a generic task framework. The pool is scoped to boot; if a future phase wants async work it can build on the same primitive.
- Not addressing dedicated server boot (`g_NetDedicated == 1`). Server has no window and no need for progress UI; threading the verify pass on server is still useful and falls out of the same `boot_pool` work, but the renderer hooks are gated `if (!g_NetDedicated)`.
- Not fixing the `mods/` build-time deploy or texture extraction debug (Slice A / Slice B in `tasks.md`); those are queued separately.

---

## Phased ship plan

Each phase is independently shippable, mergeable to dev, and testable.

### Phase 1: Path-buffer refactor (Layer E1)

- Rewrite `fsFullPath`, `fsDataDir`, `fsDataPathFor` to take `(out, outSize)`.
- Update every call site (grep `fsFullPath\|fsDataDir\|fsDataPathFor` across `src/` + `port/`).
- Build verify, smoke verify (run a clean boot, confirm same behavior).
- Auto-merge to dev.

**Wide change but no behavior delta.** This is the prerequisite. Estimated 200 to 400 call sites; expect 2 to 3 hours of mechanical edits.

### Phase 2: Thread pool + progress channel + boot overlay (Layers E2 + E3 + U1 + E4)

- Add `port/src/boot_pool.{h,c}`, `port/src/boot_progress.{h,c}`, `port/fast3d/pdgui_bootoverlay.cpp`.
- Wire main.c to use them with the verify pass STILL serial (single-worker enqueue). Verify the overlay renders, the progress bar advances per file, the window is responsive.
- Dedicated server: skip overlay render, keep pool active.
- Build verify, smoke verify (clean boot, confirm overlay shows + dismisses cleanly when boot done).
- Auto-merge to dev.

**This phase delivers the user-visible UX even before any speedup.** Mike sees a load bar; no more "Not Responding."

### Phase 3: Verify pass parallel (Layer E5a)

- Convert verify pass to fan out per file via the pool.
- Atomic counters or per-thread accumulator + reduce.
- Build verify, smoke verify on clean install (rm `data/<romid>/files/`) to force a full extract + verify cycle. Confirm progress UI advances smoothly and verify completes faster.
- Measure: target 2 to 3 s vs today's 8 s on a 6-core machine.
- Auto-merge to dev.

**Biggest single user-perceived speedup. Recurring boot drops from 8 s to 2 to 3 s.**

### Phase 4: Walker + emitter structural concurrency (Layer E5b + E5c)

- **Structural fix to `assetCatalog`**: add fine-grained mutex (or RW lock) around `assetCatalogRegister*`. Audit read paths during the walker phase; if reads happen concurrently with writes, RW lock is correct. If only single-writer-multiple-readers, a plain mutex on writes is sufficient. Decision lands in this phase based on the audit.
- **Structural fix to `loader_pool`**: refactor module-scope `s_*Used` counters and any shared scratch into per-thread arena or explicit `(ctx)` parameter. Rewrite the "Single-thread invariant" docblock at `loader_pool.c:26` to declare the new contract.
- Walker fully parallel: per-file parse + register + pool populate runs across all N - 2 workers concurrently.
- Per-asset emitter parallelization within one kind, then kinds in parallel where decoders allow.
- Audio decoders stay serial at the audio engine (1 kind at a time for sfx + voice + song); within a kind, per-asset is parallel.
- Build verify, smoke verify on clean install + post-B-318 to confirm round-trip emitter cost is brought down.
- Auto-merge to dev.

**This phase only matters meaningfully after B-318 lands.** Before that the emitters early-return, so the wall-clock impact is modest. The structural fix to `assetCatalog` + `loader_pool` is still worth landing because Phase 5 builds on it.

### Phase 5: Polish + measurements pass

- Wire timing telemetry into `boot_progress` so the next launch's weights come from the previous launch's actual timings.
- Tune worker count (test 4, 6, 8 on Mike's machine).
- Status label polish: human-readable IDs per `feedback_human_readable_ids`.
- Progress bar PD-theme polish per `pdgui_theme`.
- Add a simple log line on shutdown summarizing boot timing breakdown for telemetry.
- Auto-merge to dev.

---

## Risks and mitigations

### Path-buffer refactor blast radius

`fsFullPath` is called from a wide surface. Even with mechanical edits there is a real risk of subtle behavioral changes. Mitigation: keep the static-buffer fallback as a `[[deprecated]]` shim during the refactor so any missed call site still compiles, just emits a deprecation warning. Remove the shim once the build is clean.

### Catalog ordering

`assetCatalogRegisterBaseGame` MUST run before the walker. The walker's `loader_pool` MUST run before the per-asset emitters. The boot orchestrator must encode these as a dependency graph, not a flat queue. The simplest shape: serial dispatch at the kickoff point with each step internally fanning out to the pool. Verify with a smoke test that ordering is preserved.

### Producer-consumer drain pacing

If the parse workers outpace the catalog drain, the queue grows unbounded. Cap it (size = 64 entries, parsers block on full). If the drain outpaces parsers, drain idles cheaply on a cond var.

### Atomic counters on Windows + GCC

`stdatomic.h` on MinGW GCC works fine for the SHA verify counters. Sanity check the build flags.

### Windows message pump from a non-main thread

SDL requires the message pump on the thread that created the window. We keep the main thread on the render loop and SDL pump; workers do file I/O + hashing + ZIP only. They never call SDL.

### Dedicated server thread mode

Dedicated server doesn't have a window, so no overlay, but the pool is still useful for the verify pass (server boots faster too). The render-loop section is gated `if (!g_NetDedicated)`. Server just enqueues + waits.

### Crash handler interaction

`crashInit` runs at line 184 before pdguiInit. If a worker thread crashes during boot, the crash handler must be able to capture it. SDL_CreateThread inherits the process's crash handlers on Windows. Add a TLS marker to identify worker threads in stack dumps.

### Test coverage

- New `tests/test_boot_progress.cpp`: progress channel snapshot consistency, label updates, weight rollup.
- New `tests/test_boot_pool.cpp`: enqueue, drain, shutdown, cancellation.
- Smoke test: clean install boot vs cached boot, verify timing telemetry stays under thresholds.

---

## Resolutions (Mike, 2026-05-03)

All six open questions resolved. Each answer is canonical and shapes the implementation. Quotes are Mike's verbatim direction.

### Q1 - Worker count: scale to physical cores

> "Scale up based on number of physical cores, one thread as a manager, the rest are workers. Keep the main thread usable."

Topology:

- **Main thread** stays free for game / UI / render. Never participates in the worker pool.
- **One dedicated manager / coordinator thread** handles work distribution, progress aggregation, and dispatching to workers. Owns the work queue and the progress channel writer side.
- **N - 2 worker threads** where N is physical core count detected at boot.
- Use `SDL_GetCPUCount()` but verify it returns physical, not logical, cores (SDL doc says it's "logical CPUs"; if so, divide by 2 for hyperthreaded x86 or use a Windows-specific `GetLogicalProcessorInformation` query for accurate physical count).
- **Floor at 1 worker** if fewer than 3 physical cores are detected, so the architecture still works on minimal hardware. With 1 worker the manager thread can step in and run work itself if needed.
- **Cap at 16 workers** so heroic hardware does not spawn dozens of threads for a single boot.
- `pd.ini` override `Boot.WorkerThreads` exposed for diagnostics. Not the primary mechanism; auto-detect is the canonical path.

### Q2 - Overlay aesthetic: plain bar, PD-colored

> "Plain is fine, colored with PD colors"

Plain bottom progress bar plus status label. No animated decoration, no logo. Use PD2's signature palette via `pdgui_theme` color tokens so the overlay automatically picks up theme switches (e.g. accent for the bar fill, dim background for the empty track, primary text for the status label). No new color literals; query the active theme.

### Q3 - Phase 1 chunking: one big merge

> "One big refactor merge loaded with context"

Single coherent merge for the `fs.c` path-buffer refactor across all 200 to 400 call sites. No directory-cluster chunking. The commit message carries the rationale, the new contract, and common migration patterns; reviewability comes from the message, not file granularity. Diff is large but mechanical.

### Q4 - Walker concurrency: fully parallel, built appropriately

> "Parallel, built appropriately"

Not producer-consumer with serial register. Build the register path correctly for concurrent callers:

- Add a fine-grained mutex (or RW lock) around `assetCatalogRegister*` in `assetcatalog.c`. Considerations: hot path for many writes during boot, then read-only after; an RW lock is sensible if reads happen during the parallel walker phase.
- Audit `loaderPoolParse*` for thread safety. Refactor any global per-pool counter or shared scratch into per-thread arena or an explicit `(ctx)` parameter that callers create on the stack. The "Single-thread invariant" docblock at `loader_pool.c:26` gets rewritten or replaced.
- Per `feedback_no_half_measures`: structural correctness over speed of implementation. If the register path needs structural changes for concurrent safety, do them. Do not ship a tactical patch that "works for now."
- Per `feedback_migrate_first_then_fix_structurally`: this IS the structural fix that the threading work depends on.
- Land this as part of Phase 4. Phase 2 keeps walker serial; Phase 4 makes it correctly parallel.

### Q5 - B-318 sequencing: parallel

> "Parallel"

Do not wait for B-318 (`local_d0edb241`) to land. Phases 1 to 3 are independent of the gate fix. If B-318 lands while Phase 1 to 3 are in flight, fold the new emitter timing into Phase 4's measurements; do not replan earlier phases.

### Q6 - Phase 1 standalone, then sequential phases

> "Separate, but sequential"

- Phase 1 (`fs.c` refactor) ships as its own merge first.
- Phase 2 (thread pool + progress channel + boot overlay, verify still serial) ships after Phase 1 is on dev.
- Phase 3 (parallel verify) ships after Phase 2.
- Phase 4 (walker + emitter parallel, structural concurrency) ships after Phase 3.
- Phase 5 (polish + telemetry) ships after Phase 4.
- Each phase is a separate coherent unit shipped sequentially. Not bundled into one mega-ship; not parallel with each other.
- Matches `feedback_complete_unit_shipping` (each phase is a coherent testable unit) and gives Mike sequential review checkpoints.

---

## Status + next step

Approved 2026-05-03. Phase 1 is in flight in worktree `bold-chaplygin-e3b96e` immediately following the doc + kanban update commit. Subsequent phases ship sequentially per Q6.

Tracking in kanban under the Engine pillar: cards `c107` (Phase 1) through `c111` (Phase 5). Renumbered post-merge from c100-c104 because S614 catalog-triage took c100-c106 in parallel. Phase 1 in `done`; Phases 2 to 5 in `backlog`.

After Phase 3 lands, append `context/audits/startup-acceleration-<date>.md` with before / after timing on Mike's machine. Update this doc as each phase ships; move it to `_old/designs-shipped/` per `retention.md` once Phase 5 lands.

---

## Where to look

- Boot order: `port/src/main.c:172-590`.
- Verify pass: `port/src/romextract.c:488` (`romExtractVerifyAll`), `port/src/sha256.c:193` (`sha256HashFile`).
- Walker: `port/src/loader_walker.c:37`, `port/src/loader_walker_common.c:209`, single-thread invariant declared at `port/src/loader_pool.c:26`.
- File path chokepoint: `port/src/fs.c:67-120` (`fsFullPath` static buffer).
- Existing thread pattern to clone: `port/src/updater.c:1279-1322` (`updaterCheckAsync`).
- ImGui ready point: `port/fast3d/pdgui_backend.cpp:353` (`pdguiInit`). No frames render until `mainProc` (`port/src/pdmain.c:428`).
- Decoders confirmed pure: `port/fast3d/pdgui_theme.cpp:146-200` (`decodeRgba16/32/Ia*`).
- Audio engine NOT reentrant: `port/src/audio.c:24-77`.
- Architectural endpoint: catalog universality is COMPLETE per `context/tasks.md` Section 2a Step 5; the per-asset envelope is the canonical authoring format end-to-end. The walker is the SOLE catalog row + pool source.
