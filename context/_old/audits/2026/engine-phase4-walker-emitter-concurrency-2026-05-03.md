# Engine Phase 4 audit -- walker + emitter structural concurrency (2026-05-03)

> **Status**: SHIPPED at dev `55392850` (worktree `hardcore-leavitt-20fefd`).
> **Pillar**: Engine.
> **Phases shipped previously**: Phase 1 path-buffer refactor `19053489`; Phase 2 thread pool + progress channel + boot overlay `78b5008a`; Phase 3 parallel verify pass `f1d670e3`.
> **Inheritance**: `context/designs/engine/startup-acceleration.md` Phase 4 spec; Mike Q4 2026-05-03 ("parallel, built appropriately"); Phase 3 reference patterns in `port/src/romextract.c::romExtractVerifyAll`.

---

## What shipped

The remaining structural concurrency: per-asset emitters and the universal walker now fan out across the boot pool. Three pieces:

1. **Generic per-index fan-out helper** (`bootPoolForRangeBlocking`).
2. **Catalog + loader_pool mutexes** so concurrent walker callbacks serialize correctly into the shared hash table + arenas.
3. **Walker scaffold + 12 emitter inner loops** parallelized via the helper.

Boot pool topology unchanged: `(physical_cores - 2)` workers, manager runs inline on the catalog work thread, main thread free for SDL pump + overlay render.

---

## File changes

### `port/include/boot_pool.h` + `port/src/boot_pool.c`

`bootPoolForRangeBlocking(int begin, int end, boot_pool_range_fn fn, void *ctx)`. Generic shared-cursor pattern lifted from Phase 3:

- Shared mutex-protected `next_index` cursor; workers pull one index at a time.
- Worker count = `min(boot pool worker count, end - begin)`.
- Manager (caller's thread) participates inline; (worker_count - 1) others are dispatched via `bootPoolEnqueue`.
- `cv_done` signals when last worker decrements `workers_remaining` to zero.
- `begin >= end` or NULL fn = no-op.
- 1-worker (minimal hardware) executes fully serial on the calling thread.

The helper hides the verify-pass plumbing so each call site drops a per-index callback + a context pointer and gets fan-out for free.

### `port/src/assetcatalog.c` + `port/include/assetcatalog.h`

Single `s_CatalogMutex` (SDL_mutex) lazy-created in `assetCatalogInit`. `CATALOG_LOCK` / `CATALOG_UNLOCK` macros wrap every public hot path that touches `s_HashTable` or `s_EntryPool`:

- `assetCatalogRegister*` (base + 18 typed wrappers) -- locked end-to-end so the entry pointer + type-specific field fill stay valid across concurrent realloc.
- The base `assetCatalogRegister` body extracted into a static `s_registerLocked` helper that the typed wrappers call directly under one critical section, avoiding non-recursive double-lock.
- `assetCatalogResolve`, `assetCatalogGetMutable`, `assetCatalogResolveByNetHash`, `assetCatalogResolveBodyIndex`, `assetCatalogResolveStageIndex`. Resolve body extracted into `s_resolveLocked` for the same reason.
- `assetCatalogGetCount`, `assetCatalogGetCountByType`, `assetCatalogGetByIndex`.
- `assetCatalogIterateByType`, `assetCatalogIterateByTypeIncludingDisabled`, `assetCatalogIterateByCategory`, `assetCatalogGetSkinsForTarget`. Lock held throughout iteration; callbacks must NOT re-enter the catalog (no current caller does).
- `assetCatalogHasEntry`, `assetCatalogIsEnabled`, `assetCatalogGetLoadState`, `assetCatalogSetLoadState`, `assetCatalogSetEnabled`, `assetCatalogGetUniqueCategories`.
- `assetCatalogClear`, `assetCatalogClearMods`.

New public helper `assetCatalogSetCategoryById(const char *id, const char *category)` for walker callbacks (anim / font / lang) that need to fill `entry->category` after register. Re-resolves under-lock so the field write is safe even if another worker triggered a realloc between the register call and the field fill.

Leaf mutators that take an `asset_entry_t *` (catalogSetBodyDisplayName, catalogSetBodyRigClass, catalogSetPrimary*, etc.) are intentionally NOT locked. Callers from inside a typed register wrapper hold the lock; callers during single-threaded boot (`assetCatalogRegisterBaseGame`) hold a stable pointer because no other thread is registering.

### `port/src/loader_pool.c`

Single `s_PoolMutex` (SDL_mutex), lazy-created via `s_poolEnsureMutex` on first use. Wraps every parser entry point + reset/finalize:

- `loaderPoolReset`, `loaderPoolFinalize`.
- `loaderPoolParseWeaponJson`, `loaderPoolParseHeadJson`, `loaderPoolParseBodyJson`, `loaderPoolParseArenaJson`, `loaderPoolParseAnimationJson`.

Parsers consume shared arena counters (`s_GuncmdsUsed`, `s_AmmosUsed`, etc.) and write into shared pool slots. The lock serializes parses so concurrent walker callbacks for weapons/heads/bodies/arenas/anims still produce correct pool state.

Reader accessors (`loaderPoolGetWeapon`, `loaderPoolGetHead`, etc.) stay lock-free: they're called only AFTER `loaderPoolFinalize` flips the active flags, by which point the walker phase has completed and pool contents are stable.

### `port/src/loader_walker_common.c::loaderWalkerScanKind`

Restructured into two phases:

1. **Phase 1 (single-thread)**: `opendir` + `readdir` collects every matching `.pd<ext>` filename into a heap array. POSIX `readdir` is not reentrant on the same DIR* and the directory listing is small (1208 entries for chr anims is the largest, all others < 100), so this stays serial.
2. **Phase 2 (parallel via boot pool)**: `bootPoolForRangeBlocking(0, count, s_walkerOneFile, &ctx)` fans the per-file work. Each worker:
   - Loads the manifest (plain JSON or ZIP envelope) via `s_loadManifest`.
   - Parses the envelope's `pd_kind` + `id` fields.
   - Dispatches to the per-kind register callback.
   - Merges per-file counters into a batch mutex-guarded set.

Progress is pushed every 16 files via `SDL_AtomicAdd` on a shared `processed` counter so the boot overlay bar moves visibly during big walks (chr anims, sfx, voice).

### `port/src/loader_walker_anim.c`, `port/src/loader_walker_font.c`, `port/src/loader_walker_lang.c`

Walker callbacks that fill `entry->category` after register switched from direct field write to `assetCatalogSetCategoryById(id, category)`. Comment in each callsite calls out the rationale. Font callsite gained a stack `category[CATALOG_CATEGORY_LEN]` and `snprintf("font:%s", face)` build.

### 12 emitters: per-asset fan-out via `bootPoolForRangeBlocking`

Each emitter's outer setup (find segments, ensure dir, parent-dir create) stays single-threaded. The inner per-asset loop becomes a callback. Counters are `SDL_atomic_t` so there is no merge step.

| Emitter | Asset count | Per-asset work | Notes |
|--------|-------------:|----------------|-------|
| `romextract_pdwpn.c` | 86 | manifest synth + ZIP write per weapon | walks `g_WeaponData[]` authoring table |
| `romextract_pdmesh.c` | ~512 unique filenums | manifest + raw geometry + SHA sidecar per ZIP | two-phase: collect-and-dedup upstream, then fan out |
| `romextract_pdanim.c` | `g_AnimDataCount` (110) | gunscript opcode emit per anim | walks authoring table |
| `romextract_pdanim_chr.c` | up to 1208 | manifest + frames.bin + SHA sidecar per ZIP | walks the chr animation segment |
| `romextract_pdhead.c` | `g_HeadDataCount` (84) | manifest synth per ZIP | walks authoring table |
| `romextract_pdbody.c` | `g_BodyDataCount` (68) | manifest synth per ZIP | walks authoring table |
| `romextract_pdarena.c` | `g_ArenaDataCount` (47) | dual emit: `.pdarena` + `.pdscenario` per record | per-i iteration writes two unique slugs |
| `romextract_pdsfx.c` | 1545 | per-sound manifest + sample.bin + SHA sidecar | shared bank walker; fan-out type at file scope so the worker can be `static` |
| (pdvoice uses the same walker) | 1545 filtered | same | one-bit `want_voice` flips the filter |
| `romextract_pdsong.c` | ~119 | per-song manifest + bank slice + SHA sidecar | walks the sequences segment |
| `romextract_pdfont.c` | 10 | manifest + raw face bytes per ZIP | gated on `!PD_SERVER`; fan-out helper guarded the same way |
| `romextract_pdlang.c` | 68 | manifest + raw bank bytes per ZIP | gated on `!PD_SERVER`; bank index is 1-based, idx 0 -> bank 1 |

The pdmesh dedup is the only emitter that needed structural change beyond a callback. Dedup moved upstream into a single-threaded collect phase building `pdmesh_work_t jobs[]`; `s_emitOneMesh` lost its dedup body (caller already deduped). Cap is unchanged at `ROMEXTRACT_PDMESH_SEEN_CAP = 512`.

`romextract_pdui.c` is intentionally NOT parallelized: the .pdui emit fires from the GL render-loop trigger inside `pdguiThemeCheckExtract` (post-mainProc), not from the catalog work thread, so it stays on the GL/main thread.

---

## Concurrency model summary

| Phase | Model | Workers see | Manager / main |
|-------|-------|-------------|-----------------|
| Verify pass (Phase 3) | Embarrassingly parallel | One file each via cursor | Renders progress, manager runs inline |
| Walker per kind | Phase 1 collect single-thread; Phase 2 fan-out | One filename each via cursor | Manager runs inline |
| Catalog register/resolve | Single mutex serializes writers + readers | One at a time | Reader contention only during walker |
| Loader pool parse | Single mutex serializes parsers | One parse at a time | Pool slot writes serialized |
| Per-asset emitters (12) | Embarrassingly parallel | One asset each via cursor | Manager runs inline |
| `pdui` emitter | Serial (GL thread) | n/a | Render-loop hook in `pdguiThemeCheckExtract` |

---

## Build verify

Clean four-target via `devtools\build-session.ps1 -Session phase4 -Target ...`:

- Client (`pd`, `PerfectDark.exe`): **PASS, 55.5 MB (31s)**
- Updater (`pd-updater`, `Updater.exe`): **PASS, 12.3 MB (1s)**
- Server (`pd-server`, `PerfectDarkServer.exe`): **PASS, 22.4 MB (9s)**
- Tests (`pd-tests`, `pd-tests.exe`): **PASS, 24.6 MB (18s)**

No new compile warnings in client / updater / server / tests.

---

## What is now possible

- **Phase 5 (polish + telemetry)** unblocked. Spec items: per-launch weight caching for `boot_progress`, pool-tuning verification on Mike's 16-core box, overlay polish if discoverable, optional `Boot.Telemetry` flag for diagnostic counters.
- **Walker concurrency safety net** for any future kind-walker addition: per-file callback runs concurrently across workers, catalog + loader_pool mutexes already serialize writes. New walkers just call `loaderWalkerScanKind` with their `_register` callback.
- **Per-asset emitter speedup grows with asset count**. Audio (1545 + 1545) is the headliner; chr anims (1208), heads/bodies/arenas/weapons collectively (~285) get free parallelism. First-launch cost drops hardest where pool depth is highest.

Cold-cache wall-time measurement is deferred to Phase 5 (with telemetry hooks in place) so the before/after numbers come from real cache-flushed runs rather than warm-cache approximations.

---

## Where to look

- Boot pool fan-out helper: `port/src/boot_pool.c::bootPoolForRangeBlocking`.
- Catalog mutex: `port/src/assetcatalog.c::s_CatalogMutex` + `CATALOG_LOCK` / `CATALOG_UNLOCK` macros.
- Loader pool mutex: `port/src/loader_pool.c::s_PoolMutex` + `POOL_LOCK` / `POOL_UNLOCK` macros.
- Walker scaffold: `port/src/loader_walker_common.c::loaderWalkerScanKind` + `s_walkerOneFile`.
- Per-asset fan-out worker pattern: `port/src/romextract_pdsfx.c::s_pdsfxWork` is the reference shape.
- Pre-existing Phase 3 reference: `port/src/romextract.c::romExtractVerifyAll` and `s_verifyWorkerFn`.
