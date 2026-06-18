# Catalog universality: walker-after-emitters reorder + Dev Window ROM placement

**Date**: 2026-05-03
**Worktree**: gifted-bohr-62309b
**Scope**: B-324 / B-325 / B-326 (one coherent unit)
**Build**: pre-fix dev `232d05ea`

## TL;DR

The Step-5 + BYOR + post-pivot triage build deadlocks on a clean install at the catalog universality walker:

1. Walker scans `data/<romid>/<kind>/*.pd<ext>` BEFORE emitters write those files.
2. Walker registers 0 entries; loader_pool stays inactive (`s_LoaderActive == 0`).
3. Emitters then write 86 weapons, 84 heads, 68 bodies, etc. -- but the walker never re-runs.
4. `loaderPoolGetWeapon(idx)` returns NULL for every index because the active flag never flipped.
5. `weaponFindById(0) -> catalogManagerGetWeaponByIndex(0) -> loaderPoolGetWeapon(0) -> NULL`.
6. `bgunCalculateBlend` dereferences `weapon->sway` on a NULL pointer -- AV.

This audit captures the root cause, the structural fix, and a propagation check on the related Dev Window v2 / build-headless ROM-placement bug surfaced in the same playtest.

## B-324: bgunCalculateBlend AV

### Crash signature

`Build/pd-client.log` 2026-05-03 20:40 ET (line 2870):

```
[00:14.09] LOAD: setupCreateProps done, calling reset functions
FATAL: ACCESS_VIOLATION PC=00007ff74664410c (+0x2410c) CODE=0xc0000005
  chr_slot=-1 stack_watermark=0
```

Backtrace (resolved via `addr2line` against the dev `232d05ea` binary):

| # | Offset | Function | File |
|---|--------|----------|------|
| 0 | +0x2410c | `bgunCalculateBlend` | [src/game/bondgun.c:3526](../../src/game/bondgun.c:3526) |
| 1 | +0x3428d | `bgunReset` | [src/game/bondgunreset.c:231](../../src/game/bondgunreset.c:231) |
| 2 | +0xcadf7 | `lvReset` | [src/game/lv.c:618](../../src/game/lv.c:618) |
| 3 | +0x233e2d | `mainLoop` | [port/src/pdmain.c:617](../../port/src/pdmain.c:617) |
| 4 | +0x233edf | `mainProc` | [port/src/pdmain.c:436](../../port/src/pdmain.c:436) |
| 5 | +0x1f3f66 | `main` | [port/src/main.c:530](../../port/src/main.c:530) |

The call site at line 3525-3526 is unguarded:

```c
struct weapon *weapon = weaponFindById(bgunGetWeaponNum(handnum));
f32 sway = weapon->sway;   // line 3526 -- NULL deref
```

### Causal chain

1. `bgunReset` calls `bgunCalculateBlend(HAND_RIGHT)` x3 + `bgunCalculateBlend(HAND_LEFT)` x3.
2. `bgunGetWeaponNum(handnum)` returns `WEAPON_NONE` (= 0) when the player hand is freshly initialised (`hands[handnum].inuse == false`).
3. `weaponFindById(0)` is `catalogManagerGetWeaponByIndex(0)` which delegates to `loaderPoolGetWeapon(0)`.
4. `loaderPoolGetWeapon` early-returns NULL when `s_LoaderActive == 0`.
5. `s_LoaderActive` is flipped to 1 by `loaderPoolFinalize()` only when the universal walker registered >= 1 .pd<ext> file from disk during its scan.
6. On a fresh install the per-asset directories are empty (the emitters that write them have not run yet). Walker scanned 0 entries; pool stayed inactive.
7. `weapon` is NULL; `weapon->sway` AVs.

### Why now (not before BYOR)

Pre-BYOR-completion the loader pool was populated by the legacy `.pdbase` aggregate parser at startup. After Step 5 retired `.pdbase` (worktree `hungry-elgamal-e991de`, 2026-05-03), the universal walker became the sole pool population path. The walker reads from `data/<romid>/<kind>/*.pd<ext>` -- which is the same disk location the per-kind emitters write to. The emit-then-walk dependency was never wired in `bootRunCatalogWork`.

## B-325: Walker runs BEFORE emitters write

### Pre-fix order in `bootRunCatalogWork` ([port/src/main.c:154](../../port/src/main.c:154))

```
... catalog init ...
BOOT_PHASE_WALKER          <-- walker reads empty dirs (scanned=0, active=0)
BOOT_PHASE_EMIT_WPN        <-- emitter writes 86 weapons
BOOT_PHASE_EMIT_MESH       <-- emitter writes 256 meshes
... (12 more emitters) ...
BOOT_PHASE_EMIT_UI         <-- emitter writes UI placeholder (real emit on render thread)
BOOT_PHASE_BUILD_CACHES    <-- catalogBuildRuntimeCaches
```

Mike's pd-client.log (line 187 / 332):

```
[00:05.43] LOADER.UNIVERSAL.OK: walking data/ntsc-final/<kind>/*.pd<ext> for 13 universality kinds
[00:05.43] LOADER.UNIVERSAL.SUMMARY: scanned=0 registered=0 envelope_failures=0 register_failures=0 active=0
[00:05.47] romextract pdwpn: written=86 skipped=0 failed=0 total=86
[00:05.68] romextract pdmesh: written=256 skipped=0 failed=0 unique_filenums=256
... (13 emitters write 1545+1545+86+84+68+47+10+68+119+256+110+1208 files) ...
```

Walker scanned 0 entries. Emitters wrote everything. Pool never finalised.

### Fix

Reordered `bootRunCatalogWork` so the walker runs AFTER all emitters. The walker is now the LAST step before `catalogBuildRuntimeCaches`:

```
... catalog init ...
BOOT_PHASE_EMIT_WPN
BOOT_PHASE_EMIT_MESH
... (13 emitters) ...
BOOT_PHASE_EMIT_UI
BOOT_PHASE_WALKER          <-- walker now reads populated dirs
BOOT_PHASE_BUILD_CACHES    <-- caches read from the now-active loader_pool
```

`assetCatalogRegisterWeaponModelFiles()` (which iterates `catalogManagerGetWeaponByIndex` to register each weapon's hi/lo model file) stays inside the walker block, gated on `loaderPoolIsActive()`. It depends on the pool being populated, so its placement after the walker scan is correct.

### Boot progress side-effect (none)

`s_computeOverall_locked` ([port/src/boot_progress.c:162](../../port/src/boot_progress.c:162)) sums weights via `completed_mask` (a bitmask), not by enum-order traversal. The walker's contribution to the bar is its captured weight regardless of when it runs. Phase 5 per-launch weight caching just measures elapsed_ms per phase; ordering does not affect the cache.

### Propagation check

Searched for other `s_LoaderActive`-gated entry points outside `loader_pool.c`:

```
loaderPoolGetWeapon              (catalog_mgr_weapons.c)
loaderPoolGetHead/Body/Arena     (catalog_mgr_heads.c / bodies.c / arenas.c)
loaderPoolIsActive               (main.c -- post-walker gate)
```

All five consumers correctly NULL-check the pool result. The bug class is the WRITER side (walker order) not the READER side. No additional fixes needed in catalog_mgr_*.

`pdwpn` etc. emitters do NOT depend on `loaderPoolIsActive` -- they walk the binary-baked `g_*Data[]` authoring tables (BYOR completion 2c). Confirmed safe to move them before the walker.

The pdui emitter is special-cased (GL render-loop trigger via `pdguiThemeCheckExtract`). Its placeholder call in `bootRunCatalogWork` is structural -- the actual emit fires post-mainProc. Reordering does not affect this.

## B-326: Dev Window v2 + build-headless ROM placement at install root

### Background

B-321 (2026-05-03) flattened the install layout: `DEFAULT_BASEDIR_NAME = "."` so the binary's `fsFileLoad(g_RomName, ...)` searches at `$E` (EXE directory aka install root). Post-B-321 the binary expects `pd.<romid>.z64` at `<install_root>/pd.<romid>.z64`, NOT at `<install_root>/data/pd.<romid>.z64`.

`devtools/release.ps1` was updated in the B-321 ship to match this layout. `devtools/dev-window-v2/dev-window-v2.ps1::Copy-AddinFiles` and `devtools/build-headless.ps1` post-build addin copy were NOT updated. Both still copy `..\post-batch-addin\data\` -> `<BuildDir>\data\` recursively, which places `pd.<romid>.z64` (Mike's authoritative dev ROM) at `<BuildDir>\data\pd.<romid>.z64` -- the wrong location.

Mike's verbatim 2026-05-03: "the dev window v2 should place the rom file at the root instead of in data if that's where the client wants it."

### Fix

Both `dev-window-v2.ps1::Copy-AddinFiles` and `build-headless.ps1` post-build copy now:

1. Sweep `..\post-batch-addin\data\*.z64` (recursive). For each ROM file found, copy to `<BuildDir>\` (install root).
2. Mirror the rest of `..\post-batch-addin\data\` -> `<BuildDir>\data\`, excluding `*.z64` (so they do not duplicate at the wrong location).

robocopy path uses `/XF "*.z64"`; the no-robocopy fallback uses a Get-ChildItem filter.

`devtools/release.ps1` already correctly excludes `*.z64` from the data-copy loop (line 462) and writes `put_your_rom_here.txt` at install root (line 519). No change needed.

## Verification

### Build verify

`build-session.ps1 -Session b324 -Target all/server/tests` clean four-target:

- Client (PerfectDark.exe): PASS, 55.5 MB (32s)
- Updater (Updater.exe): PASS, 12.3 MB (1s)
- Server (PerfectDarkServer.exe): PASS, 22.4 MB (9s)
- Tests (pd-tests.exe): PASS, 24.6 MB (24s)

No new compile warnings.

### Smoke verify (Mike-runnable)

After fixes:

1. Run the binary on the install (existing data/<romid>/ may be present or wiped).
2. Boot log expected:
   - `LOADER.UNIVERSAL.OK: walking data/ntsc-final/<kind>/*.pd<ext> for 13 universality kinds` AFTER `romextract pdwpn: written=86 ...` block.
   - `LOADER.UNIVERSAL.SUMMARY: scanned=N registered=N ... active=1` (N > 0; active should flip to 1).
   - No `FATAL: ACCESS_VIOLATION` in `bgunCalculateBlend`.
3. Boot reaches title screen.

For B-326: a fresh Dev Window v2 build should place `pd.<romid>.z64` at `<BuildDir>\` (install root), not `<BuildDir>\data\`. `build-headless.ps1` after build verify should produce the same install layout.

## Files modified

- [port/src/main.c](../../port/src/main.c) (+16 net): walker block moved from line 205 to line 268, after BOOT_PHASE_EMIT_UI; comment + reasoning preserved.
- [devtools/dev-window-v2/dev-window-v2.ps1](../../devtools/dev-window-v2/dev-window-v2.ps1) (+30 net): `Copy-AddinFiles` rewritten to sweep `*.z64` to install root + exclude from data copy.
- [devtools/build-headless.ps1](../../devtools/build-headless.ps1) (+31 net): post-build addin copy rewritten with the same pattern.

## Where to look

- B-321 install layout: tasks.md section 2b, `port/src/fs.c::DEFAULT_BASEDIR_NAME`.
- BYOR completion: tasks.md section 2c, `port/src/*data_authored.c`.
- Loader pool: `port/src/loader_pool.c::s_LoaderActive`, `loaderPoolFinalize`.
- Walker: `port/src/loader_walker.c::loaderWalkerLoadAll`.
- Emitters: `port/src/romextract_pd*.c` (13 kinds).
- Boot orchestrator: `port/src/main.c::bootRunCatalogWork`.
