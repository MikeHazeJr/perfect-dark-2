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

Clean four-target build via queued `devtools/build-session.ps1` after the
worktree merge to dev (the build script intentionally redirects worktree
paths to the main checkout, so the verify runs against dev's HEAD):

- `pd` (client): 55.5 MB (+0.3 MB vs c107 baseline for the new modules)
- `pd-updater`: 12.3 MB
- `pd-server`: 22.4 MB
- `pd-tests`: 24.6 MB

Two compile fixups landed during build verify and were merged as separate
follow-up commits:
- `62f1b002`: `pdgui_bootoverlay.cpp` was missing `#include <PR/gbi.h>`,
  which `gfx_api.h`'s `gfx_run(Gfx*)` declaration needs.
- `8f992873`: `gfx_api.h` lacked `extern "C"` guards, so C++ callers
  outside `gfx_pc.cpp` (the new boot overlay) saw mangled prototypes
  while the implementation symbols were C-linkage. Added the standard
  `#ifdef __cplusplus extern "C" { ... }` block.

## Smoke verify

Ran `PerfectDark.exe --no-update-check` against a clean install
(`pd.ntsc-final.z64` only, no extracted `data/<romid>/`). Boot log
confirms the full Phase 2 flow:

```
[00:00.47] BOOT_POOL: spawned 14 worker thread(s) (override=0, physical_cores_detected=16)
[00:00.47] BOOT_OVERLAY: ready
[00:00.72] ROMEXTRACT: starting first-launch extraction ...
[00:02.85] ROMEXTRACT: complete. wrote=2011 ...
[00:02.85] ROMEXTRACT.VERIFY: scanning data/ntsc-final/files/
[00:03.08] ROMEXTRACT.VERIFY: verified=2011 ...
[00:03.08] ROMEXTRACT.SEGS: starting ...
[00:03.16] ROMEXTRACT.SEGS: complete. wrote=26 ...
[00:03.22] ROMEXTRACT.SEGS.VERIFY: verified=26 ...
[00:06.44] BOOT_OVERLAY: dismissed (visible for 5.98s)
```

What this verifies:

- 14 worker threads spawned per Mike Q1 (16 physical cores - 2 reserved
  for main + manager), exactly as designed.
- Boot overlay is ready before any catalog work begins (so the first
  visible state is "Ready window with bar at 0%", not "Not Responding").
- The full extract / verify / segment / catalog work runs on the worker
  thread (5.98 s of overlay-visible time on a clean install).
- Overlay dismissed cleanly when `bootProgressMarkComplete` fires.
- No log corruption from concurrent main + worker logging
  (sysLogPrintf mutex holds).

### Pre-existing post-boot regression (UNRELATED to Phase 2)

After the boot overlay dismissed, mainProc -> setupCreateProps -> reset
functions hits a 0xc0000005 access violation. This crash:

- Reproduces against the **pre-Phase-2 BYOR completion** build (verified
  by running the binary built before Phase 2 merge against the same ROM
  + data layout: same crash signature at the same call site).
- Does **not** reproduce against pre-BYOR-completion binaries (e.g.
  `pivot-load` from 2026-05-03 13:00 boots cleanly to title and runs to
  the 24 s timeout with character preview activity).

The regression therefore lives in the BYOR completion changes
(commits 33bce92c..b73ab6b0, merged at ab0a6fe7) -- the per-asset
emitter refactor or the `g_HeadsAndBodies` retirement broke the
post-boot stage prop reset path. Phase 2's worker-thread reorder did
not introduce it; the crash sits behind the boot overlay's dismissal
in the existing main-thread code path that Phase 2 did not touch.

Action: file as a separate triage item (BYOR completion follow-up).
Phase 2's user-visible UX deliverable is intact -- the window is
responsive, the bar visibly progresses during the 6 s of catalog work,
and the overlay dismisses on completion. Phase 3's verify-pass
parallelization can proceed against this Phase 2 baseline.

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
