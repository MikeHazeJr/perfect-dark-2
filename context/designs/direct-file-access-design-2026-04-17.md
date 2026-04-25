# Direct File Access — Asset Provider Architecture
## Design Document

> **Status**: Design (pre-implementation)
> **Session**: S318 — 2026-04-17
> **Author**: AI session (context from S89 catalog work, S300+ shipping history)
>
> **Builds on**:
> - [ADR-003-asset-catalog-core.md](../ADR-003-asset-catalog-core.md) — catalog hash table, entry struct
> - [designs/session-catalog-and-modular-api.md](session-catalog-and-modular-api.md) — resolution API, session IDs, manifest diff
> - [designs/manifest-architecture.md](manifest-architecture.md) — three manifest paths, lifecycle
> - [component-mod-architecture.md](../component-mod-architecture.md) — D3R mod filesystem layout
>
> **Goal**: Make the asset catalog the single source of truth for **where** assets come
> from, not just **what** they are. ROM byte offsets become an implementation detail of
> one provider among many. Mods become directories the catalog indexes. Multi-ROM-version
> support becomes a configuration choice, not a code fork.

---

## 1. Current State

### 1.1 ROM Binary as Monolithic Asset Store

On startup, `romdataInit()` (`port/src/romdata.c`) memory-maps the 32 MB z64 ROM file
into `g_RomFile`. All asset data for the base game lives inside this single binary blob.

**Segment table**: `romdataInitSegment()` walks a compile-time `romSegs[]` table. Each
entry has a hardcoded ROM byte offset (`seg->data`) for a named segment (`animations`,
`textures`, `models`, etc.). At init the offset is replaced with `g_RomFile + offset`,
producing a live in-memory pointer. The N64 linker symbols
`_animationsSegmentRomStart/_End` etc. are patched to match.

**File table** (`fileSlots[]`): `romdataInitFiles()` reads a big-endian `u32[]` offset
directory embedded in `romDataSeg` at `ROMDATA_FILES_OFS`. For each entry `i`:
```
fileSlots[i].data  = g_RomFile + PD_BE32(offsets[i])
fileSlots[i].size  = PD_BE32(offsets[i+1]) - PD_BE32(offsets[i])
fileSlots[i].name  = (from second offset table)
fileSlots[i].source = SRC_UNLOADED
```
`filenum` integers (`FILE_*` enum, range 1..~2016) are **direct indices** into
`fileSlots[]`. There is no hash, no indirection, no string lookup at this level.

### 1.2 Current Load Path (Catalog ID → Memory)

```
catalogGetBodyFilenumByIndex(bodynum)     ← catalog resolves ID → filenum integer
    │
    ▼
fileLoadToNew(filenum, method, loadtype)  ← src/game/file.c
    │
    ├─ fileGetInflatedSize() → size
    ├─ mempAlloc(size, MEMPOOL_STAGE)     ← 8 MB pool, N64 expansion pak size
    └─ fileLoad(dst, len, &g_FileTable[filenum], info)
           │
           └─ romdataFileLoad(filenum)    ← port/src/romdata.c
                  │
                  ├─ catalogResolveFile(filenum)  ← mod override seam
                  │     returns: {path, catalog_id, is_mod_override}
                  │
                  ├─ If is_mod_override → fsFileLoad(path) → SRC_EXTERNAL
                  ├─ Else if files/<name> on disk → SRC_EXTERNAL (AllInOneMods compat)
                  └─ Else → SRC_ROM (pointer directly into g_RomFile)
```

**`setupLoadModeldef`** (`src/game/setup.c`): Called during stage setup. Resolves prop
modelnum → filenum via `g_ModelInfo[]` table → calls `fileLoadToNew`. The modelnum is
an N64-era integer; the catalog has not yet intercepted this path fully (Phase 5c of the
session-catalog plan).

**`bodyAllocateModel`** (`src/game/body.c`): Resolves a catalog body entry to a filenum
via `catalogGetBodyFilenumByIndex()`, then calls `fileLoadToNew`. This path is partially
catalog-aware (post Phase 2 work) but still bottlenecks through filenum integers.

### 1.3 What the Override Seam Can and Cannot Do

**Can do today** (via `catalogResolveFile`):
- Redirect any existing `filenum` to a loose file on disk
- Override base-game ROM assets with mod files of the same type
- Serve the AllInOneMods external-file tree

**Cannot do today**:
- Serve **new** assets with no ROM counterpart (no filenum exists to anchor them)
- Declare assets by path without needing a filenum at all
- Support mod archives (zip/pak files) as a source
- Support multiple ROM versions with different file tables without code forking
- Declare resolution priority explicitly (the priority is implicit in code order)
- Allow the catalog to know whether a given asset comes from ROM, disk, or mod archive

### 1.4 Architectural Gap

The catalog knows **what** an asset is (`asset_entry_t`: id, type, net_hash, display
name). It does not know **where** the asset bytes come from. That knowledge is split
across `romdata.c`, `catalogResolveFile()`, and the AllInOneMods disk-scan. The catalog
cannot answer "what source provides `base:arena_felicity`?" — the only way to find out
is to call `romdataFileLoad()` and observe which branch it takes.

This gap:
- Blocks pure-file assets (mod stages, custom Forge levels)
- Blocks explicit priority control (can mod X shadow base for arena Y?)
- Blocks multi-ROM-version support (NTSC vs PAL have different offset tables)
- Makes the manifest diff semantically incomplete (it diffs asset IDs but cannot reason
  about source changes — a mod override of an existing ROM asset appears identical to
  an unmodded ROM asset from the manifest's perspective)

---

## 2. Target State

### 2.1 Vision

Every catalog entry carries a `source` descriptor that says **how to obtain its bytes**.
The ROM binary becomes one `AssetProvider` implementation (`RomProvider`). Loose mod
files become another (`FileProvider`). Mod archives are a third (`ArchiveProvider`).

The call site does not care which provider serves the bytes. `fileLoadToNew` (or its
successor) dispatches to whatever provider the catalog resolved:

```
catalogResolveBody(id, &result)
    result.data_handle → opaque handle for the provider
    result.provider    → pointer to provider vtable

providerLoad(result.provider, result.data_handle, &bytes, &size)
```

Mods are just directories (or archives) that the catalog indexes at startup. Their
entries have `FileProvider` or `ArchiveProvider` sources. Base-game assets have
`RomProvider` sources. Resolution priority (mod overrides file overrides ROM) is
declared in the catalog, not hardcoded in `romdataFileLoad`.

### 2.2 What Changes for the Caller

Before:
```c
s32 filenum = catalogGetBodyFilenumByIndex(bodynum);
fileLoadToNew(filenum, method, MEMPOOL_STAGE);
```

After:
```c
catalog_body_result_t result;
catalogResolveBody(body_id, &result);
assetLoad(result.data_handle, &buf, &size, MEMPOOL_STAGE);
```

The `filenum` integer becomes an **internal detail of RomProvider**, not a public API.
Mod assets never have filenums. The manifest, network protocol, and save files already
use catalog ID strings — they are unaffected.

### 2.3 What Does NOT Change

- Catalog ID strings (`"base:dark_combat"`, `"mods/Weapons/ak47:model"`) — unchanged
- The string-keyed catalog hash table — unchanged
- `asset_entry_t` fields that describe asset metadata — extended, not replaced
- Network wire format (catalog ID strings + session IDs) — unchanged
- Save file format (catalog ID strings) — unchanged
- The manifest system (diff + apply) — extended to carry source change detection
- mempAlloc / MEMPOOL_STAGE memory model — unchanged for now (Phase 3 async future)
- The 8MB pool (though it should be bumped separately per N64 legacy audit)

---

## 3. Asset Provider Abstraction

### 3.1 Provider Interface

```c
// ── Data handle (opaque — provider-defined interpretation) ──────────

typedef struct {
    void *provider;       // pointer to asset_provider_t
    u64   opaque[2];      // provider-private data (filenum, path offset, archive entry)
} asset_data_handle_t;

#define ASSET_HANDLE_NULL ((asset_data_handle_t){NULL, {0, 0}})
static inline bool assetHandleIsNull(asset_data_handle_t h) { return h.provider == NULL; }

// ── Provider vtable ─────────────────────────────────────────────────

typedef struct asset_provider_s {
    const char *name;     // "RomProvider", "FileProvider", "ArchiveProvider"

    // Resolve: given handle, produce a size estimate (may be 0 if unknown)
    s32  (*resolve_size)(const struct asset_provider_s *self,
                         asset_data_handle_t handle);

    // Load: allocate buf of size and fill with asset bytes.
    // Returns bytes written, or -1 on error.
    // buf is caller-allocated (mempAlloc or heap) — provider does not own it.
    s32  (*load)(const struct asset_provider_s *self,
                 asset_data_handle_t handle,
                 void *buf, s32 buf_size);

    // Unload: any provider-side cleanup (close handles, dec ref counts).
    // Called when manifest diff removes the asset. No-op for ROM.
    void (*unload)(const struct asset_provider_s *self,
                   asset_data_handle_t handle);

    // Optional: human-readable description of where this handle points.
    // Used in diagnostics / Catalog UI. May be NULL.
    const char *(*describe)(const struct asset_provider_s *self,
                             asset_data_handle_t handle, char *buf, s32 buf_size);
} asset_provider_t;
```

### 3.2 RomProvider

Wraps the existing `romdata.c` machinery. `opaque[0]` = filenum.

```c
static s32 rom_resolve_size(const asset_provider_t *self, asset_data_handle_t h) {
    s32 filenum = (s32)h.opaque[0];
    return fileGetInflatedSize(filenum);  // existing function
}

static s32 rom_load(const asset_provider_t *self, asset_data_handle_t h,
                    void *buf, s32 buf_size) {
    s32 filenum = (s32)h.opaque[0];
    return romdataFileLoadRaw(filenum, buf, buf_size);  // existing path, stripped of mod-override logic
}

static void rom_unload(const asset_provider_t *self, asset_data_handle_t h) {
    // ROM assets are always resident in g_RomFile — no action needed
}
```

The mod-override logic currently inside `romdataFileLoad` **moves** to the catalog
resolution phase (§5.2). `RomProvider.load` becomes a pure ROM read.

### 3.3 FileProvider

Serves loose files from disk. `opaque[0]` = byte offset into a global interned path
table (avoids per-handle malloc). `opaque[1]` = file size (cached from stat at
registration time, 0 = unknown until first load).

```c
static s32 file_resolve_size(const asset_provider_t *self, asset_data_handle_t h) {
    const char *path = providerFileGetPath(h.opaque[0]);
    struct stat st;
    return (stat(path, &st) == 0) ? (s32)st.st_size : 0;
}

static s32 file_load(const asset_provider_t *self, asset_data_handle_t h,
                     void *buf, s32 buf_size) {
    const char *path = providerFileGetPath(h.opaque[0]);
    return fsFileLoad(path, buf, buf_size);  // existing fsFileLoad
}

static void file_unload(const asset_provider_t *self, asset_data_handle_t h) {
    // no-op for loose files
}
```

FileProvider handles: SP mission stage files (`.bg`, `.pads`, `.setup`), mod character
models, skin textures, audio files, font files, Nine-Slice chrome images.

### 3.4 ArchiveProvider (Future / Phase 3)

Serves files from a zip/pak mod package. `opaque[0]` = archive handle index,
`opaque[1]` = entry offset within archive. Enables single-file mod distribution
(download `my_weapons_pack.pdmod`, drop in `mods/`, catalog discovers it).

Not implemented in Phase 1 or 2 — defined here so the interface is forward-compatible.

### 3.5 The `assetLoad` Dispatcher

Replaces `fileLoadToNew` as the catalog-aware entry point:

```c
// Load asset bytes into buf (caller-allocated from mempAlloc or heap).
// Returns bytes written on success, -1 on error.
s32 assetLoad(asset_data_handle_t handle, void *buf, s32 buf_size);

// Convenience: allocate from pool, load, return pointer. Returns NULL on error.
void *assetLoadToNew(asset_data_handle_t handle, mempool_t pool);

// Unload (provider-specific cleanup).
void assetUnload(asset_data_handle_t handle);

// Human-readable source description for diagnostics.
const char *assetDescribe(asset_data_handle_t handle, char *buf, s32 buf_size);
```

`assetLoadToNew` replaces the `fileLoadToNew` pattern at call sites (Phase 2 migration).
During the migration window both can coexist: `fileLoadToNew(filenum, ...)` is a thin
wrapper that creates a `RomProvider` handle on the fly and calls `assetLoadToNew`.

---

## 4. Catalog Integration

### 4.1 `asset_entry_t` Source Field

Extend `asset_entry_t` (in `port/include/assetcatalog.h`) with a source descriptor:

```c
typedef struct {
    asset_data_handle_t primary;   // canonical source (ROM, file, or archive)
    asset_data_handle_t override;  // active override (NULL if no override)
    u32                 flags;     // ASSET_SRC_PINNED | ASSET_SRC_MOD_LOCKED
} asset_source_t;

// In asset_entry_t:
asset_source_t source;
```

`primary` is set at catalog registration (base game → RomProvider handle with filenum;
mod asset → FileProvider handle with path). `override` is set when a mod declares it
overrides a base asset. `assetLoad` always dispatches to `override` if non-null,
`primary` otherwise.

### 4.2 Resolution Priority

Priority is declared at registration time, not hardcoded in loader:

1. **ArchiveProvider** — highest priority. A mod archive that explicitly overrides an
   asset wins over everything else.
2. **FileProvider (mod loose file)** — a mod's loose file override wins over ROM.
3. **FileProvider (base loose file)** — files in `files/` on disk (AllInOneMods compat).
4. **RomProvider** — base game ROM bytes. Always present for base game assets.

When the catalog registers an asset override (e.g., a mod declares `"base:arena_felicity"`
in its `mod.json`), it calls:
```c
catalogSetOverride(entry, new_handle);  // sets entry->source.override
```
When the mod is disabled, `catalogClearOverride(entry)` restores ROM behaviour.

This replaces the current implicit priority in `romdataFileLoad` (code order: mod
override → disk file → ROM) with an explicit field on the entry, visible to the Catalog
UI and debuggable without reading code.

### 4.3 New Entry Registration (Mod-Only Assets)

A mod stage or custom character has no ROM filenum. Registration:

```c
// From modmgr.c during mod scan:
asset_entry_t *entry = catalogRegister(
    "gf64_bond:arena_library",   // id
    ASSET_TYPE_MAP,              // type
    net_hash,                    // CRC32 of id
    "GoldenEye 007 — Library"    // display name
);
catalogSetPrimary(entry, fileProviderHandle("mods/GE64/maps/library.bg"));
```

There is no filenum. The entry's `source.primary` is a FileProvider handle. The rest of
the system (manifest, session catalog, network sync, The Grid) treats this entry
identically to a ROM-backed entry — it has the same catalog ID string, the same
session-ID allocation, the same SHA-256 hash in the mod distribution pipeline.

### 4.4 Multi-ROM-Version Support

Different ROM versions (NTSC 1.0, NTSC 1.1, PAL) have the same assets at different
byte offsets. Under the current design, the file table is rebuilt from the ROM at
startup (`romdataInitFiles`) so `fileSlots[n].data` already points to the correct
offset for whatever ROM was loaded.

With the provider model, ROM version divergence is isolated to `RomProvider`: it reads
`fileSlots[n].data` which is always the correct pointer for the loaded ROM. The catalog
entry, the catalog ID string, the session ID, and all calling code remain identical
across ROM versions. A PAL user and an NTSC user can connect to the same server — the
server uses catalog IDs on the wire, clients resolve them to their own ROM's bytes.

No code fork, no `#ifdef ROM_VERSION`. The catalog treats ROM version as a detail of the
provider layer, not an asset identity question.

### 4.5 Catalog UI Extensions (§14 of session-catalog design)

The existing Settings → Catalog page design should show `source.primary.provider->name`
and `source.override` (if active) per entry. This gives Mike and players full
transparency: "this character comes from ROM", "this arena is overridden by mod X".

---

## 5. Migration Strategy

### Phase 1: Formalize the Provider Interface (Low Risk)

**No behaviour change. New types only.**

- Define `asset_provider_t`, `asset_data_handle_t` in new `port/include/assetprovider.h`
- Implement `RomProvider` in `port/src/assetprovider_rom.c` — wraps existing
  `romdataFileLoad` logic exactly. `opaque[0]` = filenum.
- Implement `FileProvider` in `port/src/assetprovider_file.c` — wraps `fsFileLoad`.
  Path interning table: static `s_PathPool[4096]` char array + `s_PathOffsets[512]`.
- Implement `assetLoad` / `assetLoadToNew` / `assetUnload` / `assetDescribe` in
  `port/src/assetload.c`.
- `fileLoadToNew(filenum, method, pool)` becomes a wrapper:
  ```c
  void *fileLoadToNew(s32 filenum, s32 method, mempool_t pool) {
      asset_data_handle_t h = romProviderHandle(filenum);
      return assetLoadToNew(h, pool);
  }
  ```
  Zero callers change. Build clean. No runtime behaviour change.

### Phase 2: Catalog Source Field + Mod Override Migration

**Extends catalog. Replaces `catalogResolveFile` seam.**

- Add `asset_source_t source` to `asset_entry_t`.
- In `assetcatalog_base.c` registration, set `source.primary` to `romProviderHandle(filenum)`
  for every base-game entry.
- In `modmgr.c` when loading a mod that overrides a base asset, call `catalogSetOverride`
  instead of (or in addition to) populating the `modResolve` table.
- Move the priority logic from `romdataFileLoad` into `assetLoad`:
  ```c
  s32 assetLoad(asset_data_handle_t handle, void *buf, s32 buf_size) {
      // handle already carries provider selection from catalog resolution
      return handle.provider->load(handle.provider, handle, buf, buf_size);
  }
  ```
  `romdataFileLoad` loses its `catalogResolveFile` call — that seam is no longer needed.
- For new mod-only assets (no filenum), register with `fileProviderHandle(path)` as
  primary. The AllInOneMods `files/<name>` disk tree becomes a `FileProvider` backed
  by a pre-scanned directory → no startup disk cost for unloaded assets.

**Migration order** (independently shippable):
1. Body/head registrations in `assetcatalog_base.c` → RomProvider handles
2. Stage registrations → RomProvider handles for `.bg`/`.pads`/`.setup` per filenum
3. Mod override registrations in `modmgr.c` → FileProvider + `catalogSetOverride`
4. Validate: Catalog UI shows provider names; mod assets show "overrides ROM"

### Phase 3: Call Site Migration (Incremental, Parallels SA-5)

Replace `fileLoadToNew(filenum, ...)` at load sites with `assetLoadToNew(handle, ...)`:

```
Phase 3a — body.c / player.c (bodyAllocateModel, loadPlayerModel): ~12 sites
Phase 3b — setup.c setupLoadModeldef + prop loader: ~18 sites
Phase 3c — bg.c stage geometry loaders: ~8 sites
Phase 3d — weapon model loaders: ~10 sites
Phase 3e — texture/animation loaders (complement SA-5e): ~12 sites
```

After each sub-phase: grep for `fileLoadToNew` outside `assetload.c` — any remaining
hit is a site that hasn't been migrated. The wrapper ensures no regressions before
migration completes.

### Phase 4: Retire filenum as Public Currency

Once Phase 3 completes, `filenum` integers stop appearing at interface boundaries:
- `catalogGetBodyFilenumByIndex()` and siblings → deprecated → replaced by
  `catalogResolveBody()` which returns `result.data_handle` directly.
- `g_FileTable[filenum]` legacy pattern → no external consumers.
- `filenum` remains an internal `RomProvider` detail — it is never exposed in public
  APIs, net messages, save files, or catalog IDs.
- `fileSlots[]` and `g_FileTable[]` remain as ROM-internal arrays, not public state.

### Phase 5: Forge / Grid Stage Files (New Asset Registration)

The Grid (Forge) editor creates new stage maps with no ROM counterpart. Under this
model:

- Each saved Grid level is a directory: `mods/The Grid/<slug>/arena.bg`,
  `arena.pads`, `arena.setup`.
- `modmgr` registers these as catalog entries with FileProvider primaries.
- The catalog entry gets a generated ID: `"grid:<slug>"`.
- The session catalog, manifest, and network sync treat it identically to any other map.
- Clients who don't have the Grid level receive it via the existing mod distribution
  pipeline (SHA-256 check → download → `FileProvider`).

No special-casing needed in the loader. The Grid levels are just assets.

---

## 6. Impact on Existing Systems

### 6.1 Manifest Build and Diff

**Current**: `manifestBuildMission()` and `manifestBuild()` populate entries with
catalog IDs. `manifestDiff()` compares IDs. `manifestApplyDiff()` calls
`assetCatalogSetLoadState(entry, ASSET_STATE_LOADED)` — which today does not actually
load bytes (loading happens at model allocation time).

**After**: `manifestApplyDiff()` can optionally **prefetch** assets using
`assetLoadToNew` for assets in `to_load[]`. This is the foundation for async loading
(Phase future). The diff can also detect source changes: if `entry->source.override`
changes between sessions (mod enabled/disabled), the entry appears in `to_unload` +
`to_load` even if the catalog ID is the same. The manifest becomes source-aware.

**What stays the same**: The `match_manifest_t` wire format, `manifestClear` discipline,
the three manifest paths (MP / SP / Menu), and the SP two-phase (pre/post setup-scan).

### 6.2 Mod Distribution and SHA-256 Verification

**Current**: `modmgrVerifyModIntegrity()` computes SHA-256 of each loose file in the mod
directory. Clients verify they have matching files before accepting a match start.

**After**: No change to the verification algorithm. FileProvider handles carry path
info; `assetDescribe()` returns the path. The SHA-256 verifier reads the same path.
ArchiveProvider (Phase 3 future) would verify the archive entry's SHA-256 instead of
a loose file hash — same verification contract, different data source.

### 6.3 Network Sync

Catalog IDs and session IDs are already provider-agnostic. A client who resolves
`"gf64_bond:arena_library"` to `FileProvider("mods/GE64/maps/library.bg")` and one who
resolves it to `FileProvider("mods/GE64-v2/maps/library.bg")` produce the same session
ID on the wire. The only requirement is that their files produce the same SHA-256 —
enforced by the mod distribution check before the ready gate.

**Protocol version**: No bump required for Phases 1–3. Provider selection is purely
client-side. Phase 4 (deprecating `filenum` from public APIs) requires no wire changes
because `filenum` has already been removed from the wire in prior sessions.

### 6.4 The Grid (Forge Level Editor)

Grid-saved levels today are saved to disk but not yet wired into the catalog or served
as arena choices in the MP lobby. Phase 5 of this design provides the complete wire-up:
- Grid editor saves `.bg`/`.pads`/`.setup` → `modmgr` scan → catalog registration →
  `FileProvider` primary.
- Arena picker in Room screen lists `ASSET_TYPE_MAP` entries from catalog as before.
- No change to the Grid editor's UI or save logic.

### 6.5 Save Files and Identity Profiles

No change. These already use catalog ID strings (`"base:dark_combat"`). They never
referenced filenums, ROM offsets, or provider details. The provider is resolved at
load time from the catalog entry.

### 6.6 Skin Editor

The Skin Editor reads texture data via the current catalog + ROM path. After Phase 2,
texture entries carry `FileProvider` handles for modded skins and `RomProvider` for base
textures. The Skin Editor calls `assetLoadToNew` instead of `fileLoadToNew` — same
semantics, provider-aware call.

### 6.7 Audio Mod System

Audio mods are already served as loose files (`mods/Sounds/<slug>/*.ogg`). Under the
current system they are loaded by path without going through the ROM file table. After
Phase 2, these become `FileProvider` entries in the catalog — the same loading path,
formalized. The audio manifest entries (`ASSET_AUDIO`) gain `source` fields; the
audio loader uses `assetLoadToNew`.

### 6.8 ROM Hash Cache (`catalogCacheVerifyRom`)

The ROM hash check (S317 fix — `catalogCacheVerifyRom` calls `fsFullPath` before
`sha256HashFile`) verifies the ROM binary itself, not individual file slots. This is
unchanged: the ROM binary is still loaded, still hashed, and `RomProvider` still reads
from it. The provider model does not affect the ROM integrity check.

---

## 7. Performance Considerations

### 7.1 Memory-Mapped ROM (Current)

Assets loaded from ROM are a `memcpy` from the mmap'd `g_RomFile` into a `mempAlloc`
buffer. This is essentially a RAM-to-RAM copy — fast. The `dmaExec` name is vestigial
N64 terminology; on PC it is `memcpy`. Provider model adds negligible overhead: one
vtable dispatch per `assetLoad` call (a single indirect call, branch-predictor-friendly
since `RomProvider` dominates).

### 7.2 Loose File Loading (FileProvider)

`fsFileLoad` opens the file, reads it, closes it. On a modern NVMe SSD a 100 KB model
file loads in ~0.1 ms. For mods this is acceptable since mod assets are typically loaded
once per match start, not per frame.

**Caching consideration**: For mod assets that are referenced frequently (e.g., the
player skin in the Skin Editor preview), a simple LRU cache in `FileProvider` (keyed by
path hash, max 16 entries, ~4 MB total) would eliminate redundant disk reads. This is a
Phase 2 optional enhancement, not required for correctness.

### 7.3 Async Loading (Future)

The synchronous `assetLoad` interface is deliberately simple. The `load` vtable function
signature allows a future async extension:
```c
typedef void (*asset_load_cb_t)(void *buf, s32 size, void *userdata);
s32 (*load_async)(const asset_provider_t *self, asset_data_handle_t handle,
                  void *buf, s32 buf_size, asset_load_cb_t cb, void *userdata);
```
`RomProvider.load_async` = synchronous (ROM is always resident). `FileProvider.load_async`
= threaded read on a dedicated I/O thread. This enables loading a match's mod assets in
the background during the ready-gate countdown rather than the hard stall at stage start.

Not in scope for Phases 1–3. But the interface is designed so async can be added without
changing callers.

### 7.4 The 8MB Stage Pool

`MEMP_EXPANSION_POOL_SIZE = 8 MB` (N64 expansion pak size) is a separate concern
documented in the N64 legacy audit. Provider migration does not change the pool size.
However, once file-based assets can be loaded on-demand (rather than all-at-once at
stage start), it becomes feasible to load only what's currently needed and stream the
rest. That requires bumping the pool first (per the audit: → 64 MB). The provider
interface is compatible with a larger pool — `mempAlloc(size, MEMPOOL_STAGE)` just gets
a bigger backing allocator.

---

## 8. Risks and Mitigations

| Risk | Impact | Likelihood | Mitigation |
|------|--------|------------|------------|
| `RomProvider.load` diverges from old `romdataFileLoad` (behavior difference in edge cases) | Wrong bytes loaded, model corruption | Medium | Phase 1 wrapper: `fileLoadToNew` calls `RomProvider` via `assetLoad`. Compare outputs against old path in debug build using SHA-256 of loaded bytes for first 50 loads at stage start. |
| Mod override priority changes silently (catalogSetOverride replaces catalogResolveFile seam) | Mod assets stop overriding ROM | Medium | Phase 2 requires a per-mod smoke test: load a mod that overrides a base model, verify in Catalog UI that `source.override` is non-null and Catalog UI shows the mod name. |
| FileProvider path interning overflow (s_PathPool too small) | Assert / silent truncation | Low | Size s_PathPool to 4096 bytes (covers ~128 mod paths at avg 32 chars). Add overflow assert with log at registration. Easy to resize. |
| New catalog entries for Grid levels collide with base IDs | Asset resolution confusion | Low | Grid IDs use namespace prefix `"grid:"` — no overlap with `"base:"` or mod namespaces. `catalogRegister` already deduplicates by full ID string. |
| Phase 3 call site migration misses a site | Some assets still load via raw filenum after migration | Medium | Post-phase grep: `fileLoadToNew` outside `assetload.c` = missed site. Add to CI grep audit alongside the existing `g_HeadsAndBodies\[` audit from session-catalog plan §10. |
| Multi-provider assets (base + mod override) confuse manifest diff | An overridden asset gets double-counted | Low | `manifestDiff` compares catalog IDs, not handles. Same ID = same logical asset. Source changes (override added/removed) are captured by a separate `source_changed` flag on the diff entry — they trigger a reload of the same asset, not a double-load. |
| ArchiveProvider format incompatibility with existing mod distribution | Mod packs fail to distribute | Low (future) | ArchiveProvider is Phase 3 (future). The SHA-256 distribution channel verifies archive files by hash before any extraction. Format bugs are caught in the distribution verify step before load. |
| Rollback if Phase 1 breaks anything | Risk of disrupting stable load pipeline | Low | Phase 1 is additive only — zero existing code changes until the `fileLoadToNew` wrapper. The wrapper is a one-line change (`return assetLoadToNew(romProviderHandle(filenum), pool)`). Rollback = revert the wrapper line. |

### 8.1 Test Strategy

**Phase 1 verification**: CI Training loads cleanly. All models appear correctly.
No new log warnings. `assetDescribe()` returns `"RomProvider:filenum=NNN"` for every
load. Build size does not increase significantly.

**Phase 2 verification**: Install a mod that overrides one character model. Launch CI
Training. Open Settings → Catalog. Verify overridden model shows provider = mod name.
Disable mod. Verify same entry reverts to `"RomProvider"`. Run full match with mod
active — no character mismatch.

**Phase 3 verification**: After each sub-phase, grep audit for migrated call sites.
Run QC checklist (context/qc-tests.md): character select, model preview, stage load,
multiplayer match start with mixed ROM/mod assets.

**Rollback**: Each phase is independently reversible. Phase 1 wrapper can be removed
to restore the original `fileLoadToNew`. Phase 2 source field is additive to
`asset_entry_t` — removing it is a struct shrink, binary compatible. No network
protocol changes in any phase.

---

## 9. File Structure

### New files

```
port/include/assetprovider.h          ← provider vtable + handle types
port/include/assetload.h              ← assetLoad / assetLoadToNew / assetUnload / assetDescribe
port/src/assetprovider_rom.c          ← RomProvider implementation
port/src/assetprovider_file.c         ← FileProvider implementation + path interning
port/src/assetload.c                  ← dispatcher + fileLoadToNew wrapper
```

### Modified files

```
port/include/assetcatalog.h           ← add asset_source_t + source field to asset_entry_t
port/src/assetcatalog.c               ← add catalogSetPrimary / catalogSetOverride / catalogClearOverride
port/src/assetcatalog_base.c          ← set source.primary = romProviderHandle(filenum) at registration
port/src/modmgr.c                     ← catalogSetOverride on mod load; FileProvider for new mod assets
port/src/romdata.c                    ← remove catalogResolveFile call (moved to assetload.c Phase 2)
src/game/file.c                       ← fileLoadToNew wrapper (Phase 1); call sites migrate to assetLoadToNew (Phase 3)
```

---

## 10. Relationship to Existing Plans

| Plan | Relationship |
|------|-------------|
| session-catalog-and-modular-api.md Phase 5 (load path migration) | This design is the prerequisite: Phase 5 migrates **call sites** from filenum to catalog API; this design changes **what the catalog API resolves to** (provider handle vs filenum). Both can proceed in parallel — Phase 5 uses `catalogResolveBody()` which can return a handle instead of filenum once this design lands. |
| manifest-architecture.md | Unchanged. Provider details are below the manifest abstraction layer. |
| designs/forge-level-editor-2026-04-16.md | Phase 5 of this design enables Grid-saved levels to be served as catalog entries. The Grid editor's save format is unchanged. |
| component-mod-architecture.md D3R-9 (network distribution) | The distribution pipeline verifies SHA-256 of files. FileProvider paths are the same files the distribution pipeline downloads. No conflict. |
| n64-legacy-audit-2026-04-17.md §2.1 (MEMP pool bump) | Independent but complementary. Bump the pool first (one line) — provider migration benefits from the extra headroom for mod assets. |

---

## 11. Summary

The core insight is that an **override seam already exists** (`catalogResolveFile`), and
the catalog already uses string IDs everywhere important (wire, save, manifest). This
design doesn't invent a new architecture — it **formalizes the seam** into a typed
interface, **moves priority logic** from implicit code order to explicit catalog fields,
and **extends registration** to allow assets with no ROM filenum at all.

Phase 1 can ship in a single session with zero risk. Phases 2 and 3 are incremental
and independently reversible. The result is a catalog that can honestly answer:
**"Where do the bytes for `base:dark_combat` come from?"** — and that answer can change
per-user (ROM vs mod override) without touching any game logic.
