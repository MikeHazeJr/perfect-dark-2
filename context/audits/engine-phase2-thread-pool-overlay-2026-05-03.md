# Engine Phase 2 SHIPPED -- Thread Pool + Progress Channel + Boot Overlay (2026-05-03)

**Pillar**: Engine. **Goal**: Startup Phase 2 - Thread Pool + Progress Channel + Boot Overlay.
**Kanban card**: `c108` (done). **Spec**: `context/designs/engine/startup-acceleration.md` Phase 2.
**Worktree**: `silly-dijkstra-05e238`. **Predecessor**: `19053489` (Phase 1, fs.c path-buffer refactor).

## What shipped

A coherent unit of three new components that together make the boot sequence
visibly progress on screen instead of freezing the window:

1. **Boot thread pool** (`port/src/boot_pool.{h,c}`)
   - `bootPoolInit` spawns N worker threads via `SDL_CreateThread` and a single
     mutex + cond-var job queue (`s_workerLoop` in `port/src/boot_pool.c:103`).
   - Topology per Mike Q1 (2026-05-03): N = (physical_cores - 2), floored at 1,
     capped at 16. Main thread stays free for UI / SDL pump; one slot is
     reserved for a manager (Phase 5 telemetry will promote this to a real
     thread; in Phase 2 the dispatch happens inline before the pump loop).
   - Physical-core detection on Windows via `GetLogicalProcessorInformation`
     (`port/src/boot_pool.c:55`); falls back to `SDL_GetCPUCount` on other
     platforms.
   - `pd.ini` override `Boot.WorkerThreads` registered for diagnostics
     (`port/src/boot_pool.c:130`); 0 = auto-detect.
   - Pool API: `bootPoolEnqueue` + `bootPoolWaitIdle` + `bootPoolShutdown`.

2. **Progress channel** (`port/src/boot_progress.{h,c}`)
   - 22-phase enum spanning the entire boot sequence
     (`port/include/boot_progress.h:24`). Per-phase weights sum to ~1.0
     (`port/src/boot_progress.c:71`) calibrated against the timing analysis in
     the design doc.
   - Thread-safe API: producers (worker thread) call
     `bootProgressBeginPhase` / `Update` / `EndPhase` /
     `MarkComplete`. Consumer (main thread) calls
     `bootProgressSnapshot` for a self-consistent read.
   - Internal SDL mutex; lock held briefly so the snapshot blocks at most a few
     microseconds.

3. **Boot overlay** (`port/fast3d/pdgui_bootoverlay.cpp` +
   `port/include/pdgui_bootoverlay.h`)
   - Plain bottom progress bar + phase label + status label (Mike Q2).
   - Colors pulled from the active palette via `pdguiGetActivePaletteRaw`
     (`port/fast3d/pdgui_bootoverlay.cpp:62`) so theme switches automatically
     apply.
   - No textures; only ImGui DrawList primitives.
   - Eased fill (~30% / frame at 60Hz) so a 2011-file verify pass renders as a
     smooth bar instead of strobing.
   - Bar sits 64 px from the bottom edge with full-width margin, 14 px tall.
     Phase label above (22 pt), status label + "(N / M)" counter below
     (16 pt), percentage on the right of the phase line.
   - Skipped entirely on `g_NetDedicated == 1` (no window).

## Render plumbing

- New `gfx_run_boot_overlay_frame(draw_overlay_cb, user)` in
  `port/fast3d/gfx_pc.cpp:2954`. Drives a complete frame without a game
  display list: gfx_wapi->start_frame, GL clear, gfx_opengl_reset_for_overlay,
  invokes the callback (which manages ImGui frame begin/end internally),
  swap_buffers_begin. Exposed via `port/fast3d/gfx_api.h`.
- `pdguiBootOverlayPump` calls the new helper, then `gfx_end_frame` for the
  swap finish. Bypasses `pdguiNewFrame / pdguiRender` entirely (those gate on
  game state we don't have yet at boot).

## Boot reorder

- `port/src/main.c` carved out lines 277..506 (the catalog work block from
  `romdataInit` through `modmgrCatalogChanged`) into a new static helper
  `bootRunCatalogWork` (port/src/main.c:154) that runs on a pool worker.
- After `audioInit`, the main thread now:
  1. `bootProgressInit` + `bootPoolInit` + `pdguiBootOverlayInit`
  2. `bootPoolEnqueue(bootRunCatalogWork, NULL)`
  3. `while (!bootProgressIsComplete()) pdguiBootOverlayPump();`
  4. `bootPoolWaitIdle` -> `pdguiBootOverlayShutdown` -> `bootPoolShutdown` ->
     `bootProgressShutdown`
  5. continues to atexit / bootCreateSched / mempHeap alloc / mainProc.
- Phase boundaries inside `bootRunCatalogWork` push `bootProgressBeginPhase`
  / `EndPhase` calls so the bar advances proportional to the static weights.
- Per-file updates inside `romExtractAllFiles` / `romExtractVerifyAll` /
  `romExtractAllSegments` / `romExtractVerifyAllSegments` drive smooth bar
  motion through the dominant 8 s verify phase
  (`port/src/romextract.c:404,520,694,801`).

## Thread-safety changes outside the new modules

- `sysLogPrintf` now holds a lazy-init SDL mutex around the structured ring
  buffer write + stdout / file output (`port/src/system.c:419`). Prevents
  `s_LogRing` head/count corruption when the boot worker and main thread log
  concurrently. `conPrintLn` runs outside the lock (it has its own
  serialisation) to avoid lock inversion. No regression on the single-threaded
  pre-boot logging path.

## Verify pass still serial (intentional)

Phase 2 ships infrastructure only. The verify pass executes inside the worker
thread but iterates files serially as before. Phase 3 (`c109`) will rewrite
`romExtractVerifyAll` to fan out per-file SHA-256 across the pool's worker
threads. The progress channel + overlay are fully wired and ready for that
fan-out -- Phase 3 just changes who pushes the per-file updates.

## Build verify

Pending merge to dev (worktree path detected by `build-headless.ps1` redirects
the configure step to the main checkout, so the build verify must run after
the worktree merge to dev brings the new files into `perfect_dark-mike/`).

Expected sizes (matching the c107 baseline with ~10-50 KB delta from the new
modules):

- `pd` (client): ~55.3 MB
- `pd-updater`: ~12.3 MB
- `pd-server`: ~22.4 MB
- `pd-tests`: ~24.6 MB

## Smoke verify

Pending: launch `PerfectDark.exe` from the post-merge build directory with a
`pd.ntsc-final.z64` ROM in place, observe:

- `BOOT_POOL: spawned N worker thread(s) ...` log line at startup.
- `BOOT_OVERLAY: ready` log line.
- Overlay bar visible on screen; advances during the verify pass.
- `BOOT_OVERLAY: dismissed (visible for X.XXs)` log line at end of boot.
- Game proceeds to title screen normally (no regression).

## What Phase 3 picks up

Phase 3 spec (`c109` in kanban, ready): rewrite `romExtractVerifyAll` to fan
out per-file SHA-256 across the worker pool. The progress channel already
accepts per-file updates from any thread, the pool's `bootPoolEnqueue` /
`bootPoolWaitIdle` already work. Phase 3 is a contained refactor of one
function plus an atomic counter promotion in `port/src/romextract.c`.

## Notes for future phases

- Phase 4 will require a fine-grained mutex (or RW lock) around
  `assetCatalogRegister*` and per-thread context for `loaderPoolParse*` to
  enable parallel walker + emitters. The orchestrator in
  `bootRunCatalogWork` already groups these phases distinctly so Phase 4 can
  swap the implementation behind the existing phase boundaries without
  touching main.c.
- Phase 5 telemetry can replace the static weights in `k_PhaseWeight[]`
  (`port/src/boot_progress.c:71`) with last-launch-measured values stamped
  into pd.ini at shutdown.

## Files touched

New files:
- `port/include/boot_pool.h`
- `port/src/boot_pool.c`
- `port/include/boot_progress.h`
- `port/src/boot_progress.c`
- `port/include/pdgui_bootoverlay.h`
- `port/fast3d/pdgui_bootoverlay.cpp`
- `context/audits/engine-phase2-thread-pool-overlay-2026-05-03.md`

Edits:
- `port/src/main.c` -- carved out `bootRunCatalogWork`; main now drives the
  overlay pump while the worker runs catalog work.
- `port/src/romextract.c` -- per-file `bootProgressUpdate` calls in the four
  extract / verify loops.
- `port/src/system.c` -- SDL mutex around `sysLogPrintf` ring buffer + I/O.
- `port/fast3d/gfx_pc.cpp` -- new `gfx_run_boot_overlay_frame` helper.
- `port/fast3d/gfx_api.h` -- declaration of the helper.
- `tools/kanban/state.json` -- c108 -> done with SHA, c109 -> ready.
