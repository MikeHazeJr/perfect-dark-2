# Phase 3 Pass C -- RomProvider drop (g_RomFile release)

> **Date**: 2026-05-02 PM
> **Session**: catalog-slice12-passc (worktree)
> **Predecessor**: Slice 12 SFX residual close-out `f52cf660`.
> **Plan reference**: `catalog-rom-once-phase3-plan-2026-05-02.md` Pass C.
> **Architectural finish line** for the catalog migration: after this
> commit, the runtime never reads from the in-memory ROM mapping.

---

## What lands

A new boot-time hook, `romdataReleaseRom()`, runs once after segment
extraction + verify completes.  It:

1. Reloads every `SRC_ROM` segment whose `seg->data` still points into
   `g_RomFile + ofs` from `data/<romid>/segs/<name>.bin`.  Uses
   `romExtractSegmentRelPath` + `fsFileLoad`; the disk image was just
   written by `romExtractAllSegments` and SHA-verified by
   `romExtractVerifyAllSegments` earlier in boot, so the read is
   guaranteed to succeed in steady state.
2. Normalises any `SRC_ROM` segment whose preprocess function had
   already produced a heap buffer (data pointer outside the
   `g_RomFile` range).  These segments don't dangle when the ROM is
   freed; we just flip their source flag to `SRC_EXTERNAL` so post-
   release callers see uniform state.
3. Walks `fileSlots[1..ROMDATA_MAX_FILES]` and NULLs the data pointer
   for every slot whose pointer falls inside `[g_RomFile,
   g_RomFile+size)`.  These are the lazy "ROM offset" pointers
   `romdataInitFiles` set up; the next `romdataFileLoad` re-resolves
   them through the per-romid extracted disk path.
4. Frees `g_RomFile`, sets it `NULL`, zeroes `g_RomFileSize`.

LOUD-FAIL via `sysFatalError` if any segment in `SRC_ROM` cannot
reload from disk -- we never half-release.  Pass A.4 + the Pass B
segment verify give us a strong invariant: every consumed segment is
on disk before Pass C runs.

`romdataFileLoad` gains a new fallback branch BEFORE the legacy
`SRC_ROM` set: try `data/<romid>/files/<name>.bin` via
`romExtractRelPathForFilenum` + `fsFileLoad`.  If that hits, the slot
adopts the heap buffer as `SRC_EXTERNAL`.  `numpatches` stays at its
original value because the extracted bytes are pre-patch raw ROM,
matching the ROM-served bytes the patch logic was designed for.  If
the per-romid path also misses and `g_RomFile` is `NULL`, we
`sysFatalError` ("LOAD.PASSC: ... reinstall data/<romid>/files/").

`romdataResetFile` now NULLs the data pointer when `g_RomFile` is
`NULL` instead of repointing to `g_RomFile + ofs`.  The next
`romdataFileLoad` takes the disk path.

`main.c`'s "rom file at %p - %p" boot log now branches: if `g_RomFile`
is `NULL` post-release, log "rom file released (Phase 3 Pass C):
runtime reads disk-only" instead.

## Why now

After Pass B Slices 1-13 closed (model classes, scenes, segs, voice,
music, UI chrome, SFX residual ACCEPTED LIMIT), every byte the
runtime fetches from `g_RomFile` is also resident on disk under
`data/<romid>/`.  Holding onto the 32 MB ROM buffer past the
extraction phase is dead weight.  Pass C is the architectural finish
line for the catalog migration: with `g_RomFile` released, the
runtime is provably disk-backed for all asset reads.

## Boot ordering

```
romdataInit()                       // line 275: load g_RomFile, populate fileSlots + segs
catalogCacheVerifyRom(...)          // line 281: ROM hash cache (no pointer capture)
romExtractAllFiles()                // line 296: dump fileSlots -> data/<romid>/files/
romExtractVerifyAll()               // line 303: SHA-verify + self-heal files
romExtractAllSegments()             // line 319: dump romSegs[] -> data/<romid>/segs/
romExtractVerifyAllSegments()       // line 320: SHA-verify + self-heal segs
romdataReleaseRom()                 // NEW: migrate SRC_ROM segs + free g_RomFile
netInit()                           // line 322: post-release boot continues
```

`romdataReleaseRom` is the only call that drops `g_RomFile`.  It
must run AFTER both verify passes so the disk image is guaranteed
intact, and BEFORE any consumer that might still capture ROM-relative
pointers (currently none after Pass B closure).

## Server build

`g_RomFile` is `NULL` server-side.  `romdataReleaseRom` early-returns
0 with a single `LOG_NOTE`.  Same pattern as `romExtractAllFiles`,
`romExtractAllSegments`, and the verify variants.

## Test surface

This change has no test pin because the assertion is dynamic (live
post-release boot state, not a static text check).  Verification is
runtime:

- `LOG_NOTE`: `ROMRELEASE: g_RomFile released. segs migrated=N
  normalised=M skipped=K, fileSlots cleared=J` -- emitted exactly
  once per boot, after segment extract verify completes.
- `LOG_NOTE`: `rom  file released (Phase 3 Pass C): runtime reads
  disk-only` -- emitted by the boot startup banner instead of the
  pre-Pass-C `rom  file at <ptr> - <ptr>` line.
- LOUD-FAIL `LOAD.PASSC` if any segment / fileSlot can't migrate.

## Pass D (queued, separate session)

`romdataReleaseRom` LOUD-FAILs if a segment is missing on disk.  Pass
D layers self-heal on top: catch the missing-segment case, re-derive
from a recovery source (e.g. legacy `data/segs/<name>` mod-override
path), and re-extract on the fly so the next boot is clean.  Pass D
is held until Pass C ships and bakes; the brief lives in a parallel
worktree.

## Files touched

- `port/include/romdata.h`: add `romdataReleaseRom` declaration +
  docblock describing post-release invariants.
- `port/src/romdata.c`:
  - Include `romextract.h` for `romExtractRelPathForFilenum` /
    `romExtractSegmentRelPath`.
  - Add `romdataPtrInRom` helper + `romdataReleaseRom` definition
    after `romdataInit`.
  - Insert per-romid disk fallback branch in `romdataFileLoad` before
    the legacy `SRC_ROM` set; LOUD-FAIL on missing fallback when
    `g_RomFile == NULL`.
  - Update `romdataResetFile` to NULL the data pointer when
    `g_RomFile` has been released.
- `port/src/main.c`:
  - Wire `romdataReleaseRom()` after `romExtractVerifyAllSegments()`.
  - Branch the "rom file at %p - %p" boot log on `g_RomFile != NULL`.
- `context/audits/catalog-phase3-passc-romprovider-drop-2026-05-02.md`:
  this audit.

## Pass B + Pass C summary

| Pass | Status | Lane |
|---|---|---|
| Pass A.1 | shipped | data/<romid>/ tier accessors |
| Pass A.2 | shipped (`0983b47c`) | first-launch ROM file extraction |
| Pass A.3 | shipped (BYOR populated by user) | SHA-256 known-good gate |
| Pass A.4 | shipped (`0983b47c`) | hash-verify-on-launch self-heal |
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
| Pass C | THIS COMMIT | RomProvider drop, g_RomFile freed |
| Pass D | queued (parallel session) | self-heal hardening |

After Pass C lands, Pass D is the only remaining catalog-track work.
