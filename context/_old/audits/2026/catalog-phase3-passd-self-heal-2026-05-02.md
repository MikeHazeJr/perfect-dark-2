# Phase 3 Pass D -- self-heal hardening

> **Date**: 2026-05-02 PM
> **Session**: catalog-phase3-passd (worktree `sharp-zhukovsky-116f03`)
> **Predecessors**: Pass C RomProvider drop `dcfc5989`, Slice 12 SFX
> residual close-out `f52cf660`, Slice 13 UI chrome correction
> `e00927a2`.
> **Plan reference**: `catalog-rom-once-phase3-plan-2026-05-02.md` Pass D.
> **Catalog migration**: this is the closer.  After Pass D lands the
> Catalog lane is COMPLETE.

---

## Premise

Pass A.4 + the segment verify pair already do hash-verify-on-launch
with quarantine + re-extract on mismatch.  The infrastructure works:
LOUDFAIL.LOAD logs are emitted, corrupted files move to a quarantine
holding pen, and the in-memory ROM serves a fresh copy of the
corrupted asset.

What was missing for end-user trust:

1. **No UI signal.**  Corruption recovery happened silently in the log
   file.  A player whose `data/<romid>/` got partially wiped by an
   antivirus scan or a power-loss-during-write would never know the
   game self-healed unless they read the log.
2. **No aggregate report.**  The two verify passes each emitted a
   per-pass `LOG_NOTE` line.  Tooling (and humans) had to sum two
   counters across two log lines to know whether the boot was clean.
3. **Quarantine path was hidden.**  `data/<romid>/.quarantine/` lives
   under a per-romid tier and uses a leading dot that Windows file
   managers hide by default.  A user trying to find their quarantined
   bytes would not see them.

Pass D adds these surfaces while keeping the Pass A.4 self-heal
mechanism unchanged.  No new failure modes are introduced; the
existing mechanism gains UI + aggregate visibility.

---

## What lands

### A. Per-file system toasts on recovery / unrecoverable

`romExtractVerifyAll` and `romExtractVerifyAllSegments` now defer a
system-tier toast (`TOAST_CATEGORY_SYSTEM`) on each `corrected` /
`failed` outcome.  Title prefixes distinguish the asset class so the
player can tell what was recovered:

| Outcome           | Title                  | Body                                              |
|---|---|---|
| File recovered    | `File recovered`       | `Re-extracted <basename> from ROM after corruption.` |
| File unrecoverable | `File unrecoverable`   | `Could not recover <basename>. Verify your ROM.`    |
| Segment recovered | `Segment recovered`    | `Re-extracted <basename> from ROM after corruption.` |
| Segment unrecoverable | `Segment unrecoverable` | `Could not recover <basename>. Verify your ROM.`    |

The per-file emit is **capped at 5** via `ROMEXTRACT_TOAST_PERFILE_CAP`
so wholesale corruption (e.g. user wiped `data/` mid-session) does not
flood the 16-slot toast queue.  The aggregate boot-integrity toast
carries the totals beyond the cap.

### B. Aggregated boot integrity report

New public function `romExtractEmitBootIntegrityReport()`:

- Emits one `LOG_NOTE` line summarising both passes:
  `DATA INTEGRITY: <V> validated, <R> re-extracted, <U> unrecoverable`
- If `R > 0` defers an info system toast: `Boot data integrity
  recovered -- <R> asset(s) re-extracted from ROM after corruption.`
- If `U > 0` emits `LOG_WARNING` and defers a danger system toast:
  `ROM integrity check failed -- <U> asset(s) could not be recovered.
  Verify your ROM.`

The report is **idempotent**: subsequent calls within the same boot
short-circuit.  Counters are module-static and accumulated by the
verify funcs as they run.

Counter definitions:

| Counter        | Source                               | Semantic                          |
|---|---|---|
| `validated`    | verified + baselined per pass        | hashed clean + legacy sidecar baseline |
| `recovered`    | corrected per pass                   | sha-mismatch then re-extract succeeded |
| `unrecoverable` | failed per pass                     | re-extract write itself failed (disk full, etc.) |

### C. Deferred toast queue + drain

`pdguiToastInit` runs at `main.c:233`, well before the verify pair at
`main.c:303`/`320`, so the toast array is live during verify.
However, `pdguiToastTick` / `pdguiToastRender` don't run until the
main game loop kicks in (`pdmain.c::mainProc -> mainLoop`).

Direct `pdguiToastEnqueue` calls during boot would stamp `enqueued_ms`
with `SDL_GetTicks` at boot time.  By the time the render loop rolls
the toast hold (5 s) typically expires and the toast fades out before
the first frame.

Pass D queues toasts in a private buffer
(`s_BootToasts[ROMEXTRACT_TOAST_QUEUE_MAX]`) inside `romextract.c`.
`main.c` calls `romExtractToastDrain()` right before `mainProc()` to
replay the queue with fresh timestamps.  The drain is idempotent: a
second call drains an empty queue.

Server build: PD_SERVER guard around the queue infrastructure +
`pdguiToastEnqueue` call.  The queue compiles out and the drain is a
no-op.

### D. Quarantine path migration

Old: `data/<romid>/.quarantine/<unixtime>_<basename>`
New: `data/_quarantine/<romid>/<unixtime>_<basename>`

The top-level `data/_quarantine/` keeps quarantined files visible to
the user (no leading dot to hide on Windows file managers) and outside
any per-romid tree that gets wiped on a clean re-extract.  The
per-romid sub-tier preserves cross-version isolation so a JPN
quarantine never collides with an NTSC one.

Existing per-romid hidden-dir quarantines are not migrated by this
commit; they remain in place under `data/<romid>/.quarantine/`.  A
user who wants to inspect them can; the next corruption event lands
in the new tier.

---

## Why now

Pass C froze the architectural finish line: after `romdataReleaseRom`
the runtime never reads the ROM directly.  `data/<romid>/` becomes the
sole asset surface.  `data/` integrity becomes the critical surface.
The Pass A.4 self-heal already protects integrity at the bytes level;
Pass D layers the user-facing trust signal on top so corruption is
visible and recoverable, not a silent self-fix.

After Pass D lands, the catalog migration is COMPLETE.  Mike's
directive (2026-05-01) "we need our full catalog migrated properly to
confirm to our standard and utilization plan, where the ROM is an
initial asset source and then we use the extracted assets for loading,
sans ROM" is fully satisfied: ROM is consumed once on first launch,
extracted to disk, verified at every boot, self-heals on mismatch with
LOUDFAIL.LOAD + UI toast + aggregate report, and the runtime never
touches the ROM mapping after extraction.

---

## Boot ordering

```
videoInit()                           // line 244
pdguiInit(videoGetWindowHandle())     // line 245
pdguiToastInit()                       // line 233 (earlier in same batch)
...
romdataInit()                          // line 275: load g_RomFile + segs
catalogCacheVerifyRom(...)             // line 281: ROM hash cache
romExtractAllFiles()                   // line 296: dump fileSlots
romExtractVerifyAll()                  // line 303: SHA-verify files,
                                       //          updates s_Agg* counters,
                                       //          defers per-file toasts
romExtractAllSegments()                // line 319: dump romSegs[]
romExtractVerifyAllSegments()          // line 320: SHA-verify segs,
                                       //          updates s_Agg* counters,
                                       //          defers per-file toasts
romExtractEmitBootIntegrityReport()    // NEW: one-line summary +
                                       //      conditional aggregate toast
romdataReleaseRom()                    // line 332 (Pass C): free g_RomFile
gameInit()                             // line 347: ...
romExtractToastDrain()                 // NEW: replay deferred toasts with
                                       //      fresh timestamps
mainProc()                             // line 545: enter main game loop
```

`romExtractEmitBootIntegrityReport` runs BEFORE Pass C release because
the counters are pure reads from module-static aggregates already
populated by the verify pair; no ROM access is needed.

`romExtractToastDrain` runs AFTER `gameInit()` and right before
`mainProc()` so the toast renderer (kicks in once `mainProc` enters
its loop) sees fresh `enqueued_ms` timestamps and the toast fades in
naturally on the first rendered frame.

---

## Server build

`g_RomFile` is `NULL` server-side, so both verify funcs early-return
with no counter updates.  `s_Agg*` stay zero.
`romExtractEmitBootIntegrityReport` logs `0 validated, 0 re-extracted,
0 unrecoverable` and skips the toast block.  `romExtractToastDrain` is
PD_SERVER-guarded and is a no-op.

No new server stubs needed.  Verified via the post-merge `pd-server`
link.

---

## Test surface

Static-text grep pin at `tests/test_romextract_passd.cpp` (9 cases /
~25 assertions).  Pins:

1. `LOUDFAIL.LOAD` channel name used for hash-mismatch events (>= 6
   sites between file + segment paths).
2. `DATA INTEGRITY: %d validated, %d re-extracted, %d unrecoverable`
   format string is canonical.
3. Quarantine path migrated to `data/_quarantine/<romid>/`; old
   `/.quarantine/` is gone from production code.
4. PD_SERVER guards present around `pdgui_toast.h` include and the
   `pdguiToastEnqueue` call site.
5. Per-file toast cap (`ROMEXTRACT_TOAST_PERFILE_CAP 5`) and queue max
   (`ROMEXTRACT_TOAST_QUEUE_MAX 16`) constants pinned.
6. Per-file recover + fail toast helpers reach the verify paths (>= 2
   each).  Title prefixes `File` and `Segment` both present.
7. Aggregate counter updates appear in BOTH verify functions (== 2
   sites each: file + segment).
8. Public API exported through `romextract.h`: `romExtractToastDrain`,
   `romExtractEmitBootIntegrityReport`, `romExtractGetBootIntegrity`,
   plus the `Pass D (2026-05-02)` docblock marker.
9. `main.c` wiring: report fires AFTER verify pair AND BEFORE Pass C
   release; drain fires AFTER `gameInit` AND BEFORE `mainProc`.  Order
   asserted via `find` position comparison.

Behavioral coverage is intentionally minimal: the verify funcs depend
on `g_RomFile` + filesystem state which the test runner does not
provide.  The mechanism is exercised in vivo at boot.

---

## Files touched

- `port/include/romextract.h`: declare `romExtractToastDrain`,
  `romExtractEmitBootIntegrityReport`, `romExtractGetBootIntegrity` +
  Pass D docblock (~50 lines added).
- `port/src/romextract.c`:
  - PD_SERVER-guarded `#include "pdgui_toast.h"`.
  - Quarantine path migration in `romExtractQuarantine`.
  - Pass D state + helpers (`s_BootToasts`, `s_PerFileToastsEmitted`,
    `s_AggValidated/Recovered/Unrecoverable/ReportEmitted`,
    `romExtractBasename`, `s_deferBootToast`,
    `s_emitPerFileRecoverToast`, `s_emitPerFileFailToast`).
  - File verify path: per-file toast emit on `corrected` / `failed`
    branches; aggregate counter update at end.
  - Segment verify path: same treatment as file verify.
  - Public Pass D functions appended at end of file.
- `port/src/main.c`:
  - Wire `romExtractEmitBootIntegrityReport()` after the verify pair,
    before `romdataReleaseRom()`.
  - Wire `romExtractToastDrain()` after `gameInit()`, before
    `mainProc()`.
- `tests/test_romextract_passd.cpp` (new): 9 cases / ~25 assertions
  pinning the static contract.
- `CMakeLists.txt`: wire `tests/test_romextract_passd.cpp` into
  `SRC_TESTS`.

---

## Pass A through Pass D summary

| Pass | Status | Lane |
|---|---|---|
| Pass A.1 | shipped | `data/<romid>/` tier accessors |
| Pass A.2 | shipped (`0983b47c`) | first-launch ROM file extraction |
| Pass A.3 | shipped (BYOR populated by user) | SHA-256 known-good gate |
| Pass A.4 | shipped (`0983b47c`) | hash-verify-on-launch self-heal |
| Pass A.5 | shipped | LOUDFAIL channel convention |
| Pass B Slice 1 | shipped (`0983b47c`) | weapon models on disk |
| Pass B Slice 2 | shipped (`fb7331ce`) | sfxctl + sfxtbl on disk |
| Pass B Slice 3 | shipped (`0983b47c`) | lang banks on disk |
| Pass B Slice 4 | shipped (`0983b47c`) | character models on disk |
| Pass B Slice 5 | shipped (`fb7331ce`, via 2) | character sounds |
| Pass B Slice 6 | shipped (`fb7331ce`) | animations on disk |
| Pass B Slice 7 | shipped (`0983b47c`) | props on disk |
| Pass B Slice 8 | shipped (`fb7331ce`, via 2) | prop sounds |
| Pass B Slice 9 | shipped (`214518b9`) | stage scene files on disk |
| Pass B Slice 10 | shipped (`b2122749`) | voice retag (taxonomy) |
| Pass B Slice 11 | shipped (`fb7331ce`) | music sequences on disk |
| Pass B Slice 12 | shipped (`f52cf660`, doc) | SFX residual ACCEPTED LIMIT |
| Pass B Slice 13 | shipped (`e00927a2`, corrected) | UI chrome on disk |
| Pass C | shipped (`b15cc701`) | RomProvider drop, g_RomFile freed |
| **Pass D** | **THIS COMMIT** | **self-heal hardening** |

Catalog migration COMPLETE after Pass D lands.

---

## Hypotheses left open (low priority)

These are surfaces the audit considered but did not address; none
block Pass D's user-facing trust goal.

1. **Toast hold 5s vs unrecoverable severity.**  `pdgui_toast.cpp`
   uses fixed `TOAST_HOLD_MS = 5000`.  An unrecoverable boot is rare
   but worth keeping on screen longer.  Possibility: extend
   `pdgui_toast.cpp` with a `TOAST_FLAG_STICKY` that bypasses the hold
   timer.  Out of scope here -- the LOG_WARNING + the boot-integrity
   toast-on-replay-of-each-launch covers persistence.
2. **Quarantine retention.**  Quarantined files accumulate forever.
   A long-running install with many corruption events grows
   `data/_quarantine/` without bound.  Possibility: cap retention at
   N most-recent or M MB.  Out of scope; no current pressure.
3. **Mid-session corruption detection.**  Pass A.4 verifies at boot
   only.  An asset corrupted mid-session (rare; would require a
   filesystem-level event after boot) is not detected until next
   boot.  Possibility: re-verify on asset load when a model decode
   fails.  Out of scope; no current pressure.
4. **Re-extract verify loop.**  Current code re-extracts from
   `g_RomFile` and writes a new sidecar from the freshly-extracted
   bytes.  If `g_RomFile` is itself corrupted (e.g. user fed a
   damaged ROM that passed size/header check but has flipped bits in
   the data segment), the re-extract "succeeds" structurally but
   produces wrong bytes.  Detection requires comparing against an
   external known-good hash table; the SHA-256 known-good gate at
   Pass A.3 is the venue.  Out of scope here.

---

## Cross-references

- Pass C audit: `context/audits/catalog-phase3-passc-romprovider-drop-2026-05-02.md`
- Phase 3 plan: `context/designs/catalog/catalog-rom-once-phase3-plan-2026-05-02.md`
- ROM extraction architecture: `context/audits/rom-extraction-audit-2026-04-30.md`
- LOUDFAIL convention: `port/include/system.h:104-127`
- Toast surface: `port/include/pdgui_toast.h`,
  `port/fast3d/pdgui_toast.cpp`
- Catalog pillar: `context/pillars/catalog.md`
