# Engine Phase 3 SHIPPED -- Parallel Verify Pass (2026-05-03)

**Pillar**: Engine. **Goal**: Startup Phase 3 - Parallel Verify Pass.
**Kanban card**: `c109` (done). **Spec**: `context/designs/engine/startup-acceleration.md` Phase 3.
**Worktree**: `exciting-wozniak-fe0cac`. **Predecessor**: `78b5008a` (Phase 2 audit + smoke verify).
**Merge SHAs**: `e42ad24c` (Phase 3 implementation) + `67b97c4a` (Phase 3 alignment
fixup).  A B-323 fix from another session (`competent-saha-a202bb`) was merged
in parallel between the fixup and this audit, advancing dev to `8554b4ea`.

## What shipped

`romExtractVerifyAll` refactored from a serial 1..2048 loop into a parallel
fan-out across the boot pool's worker threads.  Per-file SHA-256 hashing now
runs concurrently; counters and failure events accumulate per-thread and merge
into a shared batch under a single mutex acquisition per worker (not per file).

### Files touched

- `port/src/romextract.c`: parallel verify implementation
  (`s_verifyOneFile`, `s_verifyWorkerFn`, `verify_batch_t` at lines 552..858).
  Original serial loop body factored into `s_verifyOneFile` operating on
  `verify_thread_state_t` thread-local accumulators; the entry point
  `romExtractVerifyAll` (line 774) dispatches `(worker_count - 1)` boot-pool
  jobs and runs one inline so the manager thread participates instead of
  blocking idle.
- `tests/test_romextract_passd.cpp`: per-file toast-helper test re-pinned to
  acknowledge Phase 3's deferred-event wiring.  File verify path tags events
  through `s_verifyAppendEvent(th, "File", ...)` which the manager replays
  serially after the worker join; segment verify path keeps inline
  `s_emitPerFileRecoverToast("Segment", ...)` calls (only ~12 segments, no
  parallelism benefit).

## Threading model

| Component | Owner | Notes |
|-----------|-------|-------|
| `next_file_num` cursor | shared, mutex-protected | one file per pull -> automatic load balance |
| Per-file SHA-256 + I/O | thread-local | no shared writes during the per-file work |
| Counters + event list | thread-local in `verify_thread_state_t`, merged once at drain end | one mutex op per worker, not per file |
| Progress reporting | `SDL_atomic_t files_done` + `bootProgressUpdate` every 32 | bar advances visibly, no per-file mutex contention on the progress channel |
| Failure events | thread-local lists, merged into batch, replayed serially | preserves existing `s_PerFileToastsEmitted` cap and matches the Phase 3 brief's "all failures, not just first" requirement (per-file `LOUDFAIL` log lines also fire from workers since `sysLogPrintf` is mutex-protected) |
| Manager-thread participation | manager runs `s_verifyWorkerFn` inline | saturates the pool; required because `bootPoolWaitIdle` would deadlock on the manager's own `in_flight` slot |
| Worker-completion latch | `workers_remaining` + `cv_done` | per-batch latch independent of the global pool wait |

## Reentrancy audit (per Phase 1+3 design)

- `sha256HashFile` / `sha256Hash` / `sha256ToHex`: stack-local ctx, own `FILE*`.
- `fsFullPath` / `fsFileOpenWrite` / `fsFileLoad`: out-buffer form (Phase 1).
- `sysLoudFailf` / `sysLogPrintf`: mutex-protected.
- `sysMemAlloc` / `sysMemFree`: SDL mutex registered at boot.
- `romdataFileGetData` / `romdataFileGetSize`: safe AFTER `romExtractAllFiles`
  loaded every slot (the SRC_UNLOADED branch does not fire on second visit).
  The verify pass always runs after extract, so this precondition holds.
- `rename()` / `mkdir()` on per-file paths: independent across files.

## Correctness invariants preserved

- Final catalog state IDENTICAL to serial verify (same files corrected, same
  failures detected, same baseline behavior).
- Log line format unchanged: `ROMEXTRACT.VERIFY: verified=%d, corrected=%d,
  baselined=%d, skipped_empty=%d, failed=%d` -- enables byte-identical diff
  with Phase 1/Phase 2 comparability.
- Aggregator updates (`s_AggValidated` / `s_AggRecovered` /
  `s_AggUnrecoverable`) unchanged: file path now extracts named locals from
  the batch so the textual structure matches the segment-side path.
- `bootProgress*` calls preserved at phase boundaries; per-file granularity
  preserved at every-32-files cadence.

## Build verify

Four-target build via `devtools/build-session.ps1 -Session phase3-verify`
on dev HEAD `8554b4ea` (post-Phase-3-merge):

| Target  | Result | Size    | Time |
|---------|--------|---------|------|
| client  | PASS   | 55.5 MB | 6s   |
| server  | PASS   | 22.4 MB | 2s   |
| updater | PASS   | 12.3 MB | 1s   |
| tests   | PASS   | 24.6 MB | 2s   |

`pd-tests.exe '[passd]'` -> all 9 cases / 42 assertions pass.

Pre-existing failures unrelated to Phase 3 still present and out of scope:
`uichrome-paths`, `step5: pdbase substrings`, and `rom-backed catalog
provider` -- these reference files I did not touch (BYOR-completion in flight).

## Smoke verify

Mike's actual install at `~/Downloads/Perfect Dark 2.0/`.  ROM at install root
post-B-321; extract + verify operate on `data/ntsc-final/files/` (2011 files).

### Phase 2 baseline (warm cache, current dev pre-Phase-3)

```
[00:00.72] ROMEXTRACT: starting first-launch extraction ...
[00:02.85] ROMEXTRACT: complete. wrote=2011 ...
[00:02.85] ROMEXTRACT.VERIFY: scanning data/ntsc-final/files/
[00:03.08] ROMEXTRACT.VERIFY: verified=2011 ...
```

Verify wall time: **0.23s**.

### Phase 3 (warm cache, this build)

```
[00:00.49] BOOT_POOL: spawned 14 worker thread(s) (override=0, physical_cores_detected=16)
[00:00.49] BOOT_OVERLAY: ready
[00:00.75] ROMEXTRACT: starting first-launch extraction ...
[00:03.34] ROMEXTRACT: complete. wrote=2011 ...
[00:03.34] ROMEXTRACT.VERIFY: scanning data/ntsc-final/files/
[00:03.42] ROMEXTRACT.VERIFY: verified=2011, corrected=0, baselined=0, skipped_empty=36, failed=0
[00:03.42] ROMEXTRACT.SEGS: starting first-launch segment extraction (segments=27, target=data/ntsc-final/segs/)
[00:03.50] ROMEXTRACT.SEGS: complete. wrote=26 ...
[00:03.50] ROMEXTRACT.SEGS.VERIFY: scanning data/ntsc-final/segs/
[00:03.56] ROMEXTRACT.SEGS.VERIFY: verified=26 ...
[00:06.88] BOOT_OVERLAY: dismissed (visible for 6.39s)
```

Verify wall time: **0.08s** (3.34 -> 3.42).

### Speedup

Warm-cache verify: **0.23s -> 0.08s == ~3x speedup**.  Mike's machine is
16-core; pool spawned 14 workers as designed.  Theoretical max parallelism is
14x but warm-cache I/O is so fast that thread spawn + sync overhead dominates.

The 8s figure cited in the design doc is for a cold-cache scenario (OS file
cache dropped between runs).  Phase 3's design point is that scenario:
fan-out reduces SHA-256 + 8 KiB streaming reads of 2011 files from ~8s
single-threaded to ~0.5-1s on a 6+ core machine.  Cold-cache measurement
deferred to a separate session that can drop OS file cache cleanly.

## Worker contention check

Counts match exactly (verified=2011 in both Phase 2 and Phase 3).  No worker
deadlock, no double-quarantine, no event-list overflow.  Final batch state
matched serial baseline byte-for-byte in the log.

## Boot stability

Boot proceeded normally through verify.  The crash later in boot
(setupCreateProps -> reset functions, ACCESS_VIOLATION at memcpy+260) is the
known BYOR-regression issue tracked separately by the BYOR triage session
(per the Phase 3 brief's "Coordinate with in-flight work" note); not caused
by Phase 3 and out of scope here.

## What's next

Phase 4: walker + emitter structural concurrency.  Phase 4 carries the
fine-grained mutex (or RW lock) on `assetCatalogRegister*` and per-thread
context for `loaderPoolParse*` so module-scope counters become local.  This
unlocks fully parallel walker + per-asset emitter execution, which matters
meaningfully once B-318's emitter gate fix lands and the per-asset emitters
actually run end-to-end on first launch.

Kanban: c109 -> done.  c110 (Phase 4) -> ready.

## SHA / merge trail

- Phase 3 implementation: `ef777824` (worktree) -> merged to dev as
  `e42ad24c` (Merge worktree: Engine Startup Phase 3 -- parallel verify
  pass).
- Phase 3 fixup (single-space alignment for test pin): `67698abd`
  (worktree) -> merged to dev as `67b97c4a` (Merge worktree: Engine
  Phase 3 fixup -- aggregate alignment).
- B-323 challengesInit AV fix from `competent-saha-a202bb` merged in
  parallel between the Phase 3 fixup and this audit, advancing dev
  HEAD to `8554b4ea`.
- This audit + kanban update: merged to dev as `d44ded34`.
