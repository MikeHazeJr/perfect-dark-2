# ADR-003: Asset Catalog Core (D3R-2)

**Date**: 2026-03-23
**Status**: Proposed
**Scope**: String-keyed hash table, registration API, `catalogResolve()`, CRC32 identity
**Depends on**: INI format finalized (ADR-002 establishes this)
**Blocks**: D3R-3 (base game cataloging), D3R-4 (scanner), D3R-5 (callsite migration), D3R-9 (network distribution)
**Design doc**: [component-mod-architecture.md](component-mod-architecture.md) §5

---

## Context

The project has a **name-based asset resolution** constraint (S27, active in constraints.md): all asset references must use string IDs resolved through a central catalog. No numeric ROM addresses, table indices, or array offsets for asset identity.

Currently, two systems handle asset metadata:

1. **`modelcatalog.c`** — A numeric-indexed catalog of `g_HeadsAndBodies[152]`. It caches model metadata (scale, status, display name, thumbnail), provides safe accessors (`catalogGetSafeBody/Head`), and validates models at startup with exception handling. It's well-built infrastructure, but it catalogs by array index — `catalogGetEntry(42)` — not by name.

2. **`modmgr.c` shadow arrays** — `g_ModBodies[64]`, `g_ModHeads[64]`, `g_ModArenas[64]` extend the base arrays with mod content. Accessors like `modmgrGetBody(index)` route to the base or shadow array based on whether `index` is above or below the base count. Again, numeric.

The Asset Catalog (D3R-2) replaces both numeric lookup pathways with a single string-keyed resolution layer. `catalogResolve("gf64_bond")` returns runtime data regardless of whether the asset is base game or mod content.

This ADR covers the core data structure and API. It does **not** cover the scanner that populates the catalog (D3R-4) or the callsite migration that consumes it (D3R-5). Those are separate phases with their own decisions.

---

## Decision

### Architecture: Extend modelcatalog, don't replace it

The existing `modelcatalog.c` has battle-tested infrastructure we want to keep: safe model loading with VEH/SIGSEGV handlers, scale validation and clamping, thumbnail generation, display name resolution. Throwing that away to build a new catalog from scratch would be wasteful and risky.

Instead, we **extend** modelcatalog into the Asset Catalog by adding:

1. A string-keyed hash table that maps asset IDs to catalog entries
2. A new entry struct that generalizes beyond heads/bodies to all asset types
3. Registration functions for each asset type
4. A unified `catalogResolve()` API

The existing numeric accessors (`catalogGetEntry(index)`, `catalogGetSafeBody(bodynum)`) remain functional during the migration period. They become thin wrappers that resolve through the hash table internally once D3R-5 is complete. This lets us migrate callsites incrementally without a flag day.

### File placement

New files in `port/`:
- `port/include/assetcatalog.h` — Public API (resolve, register, iterate, query)
- `port/src/assetcatalog.c` — Hash table, registration, resolution logic

The existing `modelcatalog.h/c` remains unchanged initially. Once D3R-3 (base game cataloging) registers all heads/bodies through the new asset catalog, `modelcatalog.c`'s init path calls into `assetcatalog.c` to register its entries and delegates lookups. Eventually (D3R-11), modelcatalog becomes a thin facade or is absorbed entirely.

### Hash table implementation

**Open addressing with linear probing**, not chained buckets.

Rationale: The catalog is read-heavy (thousands of lookups per frame during menus, dozens during gameplay) and write-rare (populated once at startup, modified only on mod enable/disable). Open addressing gives better cache locality for lookups. The table is small enough (max ~1024 entries — 87 stages + 63 bodies + 76 heads + 30 weapons + mod content) that linear probing's worst-case clustering is negligible.

```c
#define CATALOG_HASH_INITIAL 2048    // power of 2, starting size
#define CATALOG_ENTRIES_INITIAL 512  // starting pool; grows dynamically

typedef struct asset_entry {
    // Identity
    char id[CATALOG_ID_LEN];           // "gf64_bond", "base:joanna_dark" (64 bytes)
    u32  id_hash;                      // FNV-1a of id string (hash table slot index)
    u32  net_hash;                     // CRC32 of id string (network identity)

    // Classification
    asset_type_e type;                 // ASSET_MAP, ASSET_CHARACTER, ASSET_SKIN, etc.
    char category[CATALOG_CATEGORY_LEN]; // "goldfinger64", "base" (64 bytes)

    // Filesystem
    char dirpath[FS_MAXPATH];          // absolute path to component folder

    // Common metadata
    f32  model_scale;                  // from .ini (default 1.0)
    bool enabled;                      // user toggle
    bool temporary;                    // session-only download
    bool bundled;                      // shipped with game

    // Runtime binding (set during registration, updated on reload)
    s32  runtime_index;                // index in the relevant runtime array
                                       // (g_Stages, g_HeadsAndBodies, etc.)

    // Type-specific extension (union keeps entry size bounded)
    union {
        struct {
            s32 stagenum;              // logical stage ID (e.g. 0x5e)
            s32 mode;                  // mp, solo, coop
            char music_file[FS_MAXPATH];
        } map;
        struct {
            char bodyfile[FS_MAXPATH];
            char headfile[FS_MAXPATH];
        } character;
        struct {
            char target_id[CATALOG_ID_LEN];  // soft reference
        } skin;
        struct {
            char base_type[32];        // "NormalSim", "DarkSim", etc.
            f32 accuracy;
            f32 reaction_time;
            f32 aggression;
        } bot_variant;
    } ext;

    // Catalog internals
    bool occupied;                     // hash table slot in use
} asset_entry_t;
```

### Hash functions: FNV-1a (table) + CRC32 (network)

Two hashes serve two different purposes:

**FNV-1a** computes the hash table slot index (`id_hash`). It's designed for hash table distribution — a single multiply-and-XOR per byte with strong avalanche properties. Small input changes spread uniformly across output bits, minimizing probe chain clustering in open addressing. Implementation is ~10 lines of C with no dependencies.

```c
static u32 fnv1a(const char *str) {
    u32 hash = 0x811c9dc5;  // FNV offset basis
    while (*str) {
        hash ^= (u8)*str++;
        hash *= 0x01000193; // FNV prime
    }
    return hash;
}
```

**CRC32** computes the network identity hash (`net_hash`), consistent with the existing `modmgrHashString()`. This is what gets sent over the wire in asset requirement messages (D3R-9). CRC32 is stable across platforms and already established in the protocol.

The hash table slot is `id_hash & (table_size - 1)` with linear probing on collision. Network comparisons use `net_hash`.

### ID namespace convention

| Prefix | Source | Example |
|--------|--------|---------|
| `base:` | Base game asset | `base:joanna_dark`, `base:laptop_gun` |
| (none) | Mod component | `gf64_bond`, `kakariko_village` |

The `base:` prefix is a namespace convention, not special-cased in code. The hash table treats `"base:joanna_dark"` as an opaque string like any other. This means mod content can theoretically override a base asset by registering the same ID — a deliberate design choice that enables total conversions.

**Override priority**: If two entries share an ID, the later registration wins. Base assets register first (D3R-3), mod assets register after (D3R-4 scanner). A mod registering `"base:joanna_dark"` would replace the base Joanna. This is powerful and intentional — the game itself is essentially a mod of the original, so total conversion capability is a first-class feature. The Mod Manager (D3R-6) should surface overrides for user visibility. (Director confirmed, S28.)

---

## API Design

### Registration

```c
// Register a single asset. Returns pointer to the entry, or NULL on failure.
// Overwrites existing entry with same ID (last-write-wins for overrides).
asset_entry_t *catalogRegister(const char *id, asset_type_e type);

// Convenience wrappers that set type-specific fields after registration:
asset_entry_t *catalogRegisterMap(const char *id, s32 stagenum, const char *dirpath);
asset_entry_t *catalogRegisterCharacter(const char *id, const char *bodyfile, const char *headfile);
asset_entry_t *catalogRegisterSkin(const char *id, const char *target_id);
// ... etc. for each asset type
```

### Resolution

```c
// Core lookup — returns entry or NULL if not found / not enabled.
const asset_entry_t *catalogResolve(const char *id);

// Type-specific convenience that also returns the runtime index:
s32 catalogResolveBodyIndex(const char *id);    // returns runtime_index for ASSET_CHARACTER
s32 catalogResolveStageIndex(const char *id);   // returns runtime_index for ASSET_MAP
s32 catalogResolveWeaponIndex(const char *id);  // etc.

// Hash-based lookup (for network sync — avoids string comparison):
const asset_entry_t *catalogResolveByHash(u32 id_hash);
```

### Iteration

```c
// Iterate all entries of a given type (for menus, mod manager, etc.)
typedef void (*catalog_iter_fn)(const asset_entry_t *entry, void *userdata);
void catalogIterateByType(asset_type_e type, catalog_iter_fn fn, void *userdata);

// Iterate all entries with a given category label
void catalogIterateByCategory(const char *category, catalog_iter_fn fn, void *userdata);
```

### Lifecycle

```c
void catalogInit(void);         // allocate hash table, zero state
void catalogClear(void);        // remove all entries (before full reload)
void catalogClearMods(void);    // remove non-base entries only (mod toggle reload)
s32  catalogGetCount(void);     // total registered entries
s32  catalogGetCountByType(asset_type_e type);
```

### Query

```c
// Dependency resolution
bool catalogHasEntry(const char *id);
bool catalogIsEnabled(const char *id);

// Skin queries (resolve soft references)
s32  catalogGetSkinsForTarget(const char *target_id, const asset_entry_t **out, s32 maxout);
```

---

## Options Considered

### Option 1: Extend modelcatalog (Recommended — chosen above)

Add the hash table and string-keyed API alongside the existing numeric catalog. Migrate incrementally.

**Pros**: Preserves VEH/SIGSEGV model validation, thumbnail caching, display name resolution. No flag day. Existing code keeps working during migration.

**Cons**: Two catalog systems coexist during the migration window (D3R-2 through D3R-11). Risk of lookup path confusion — "which catalog do I call?"

**Mitigation**: Clear naming convention. `catalogGetEntry(index)` = old numeric path. `catalogResolve("id")` = new string path. Compiler warnings if numeric accessors are used in new code (via deprecation attributes after D3R-5).

### Option 2: Replace modelcatalog entirely

Build a new `assetcatalog.c` from scratch. Reimplement model validation, thumbnail caching, etc. Delete `modelcatalog.c`.

**Pros**: Clean slate. No legacy path confusion.

**Cons**: Duplicates ~300 LOC of battle-tested infrastructure (VEH model loading, scale validation). Risk of introducing regressions in model handling that the existing catalog already solved. Larger changeset = harder to test incrementally.

### Option 3: Hash table as a separate utility, catalog calls into it

Build a generic hash table (`hashtable.h/c`) and use it inside the existing modelcatalog.

**Pros**: Reusable hash table for other systems.

**Cons**: Over-engineering. No other system currently needs a generic hash table. The catalog's requirements (fixed key type, known max size, read-heavy) make a specialized implementation simpler and faster than a generic one.

### Decision: Option 1

The migration cost is low (clear naming, incremental callsite migration), and the preserved infrastructure (model validation, thumbnails) is significant.

---

## Relationship to Existing modelcatalog.c

### Phase 1 (D3R-2, this ADR): Coexistence

`assetcatalog.c` is a new file. `modelcatalog.c` is untouched. They don't interact yet.

### Phase 2 (D3R-3): Base game registration

`catalogInit()` in `modelcatalog.c` is extended to also call `assetcatalogRegister()` for each head/body it processes. The asset catalog now has entries for all base assets. Both systems contain the same data, queryable by either index or name.

### Phase 3 (D3R-5): Callsite migration

New code calls `catalogResolve("base:joanna_dark")` instead of `catalogGetEntry(42)`. Old callsites are migrated subsystem by subsystem. During this phase, both paths work.

### Phase 4 (D3R-11): Consolidation

`modelcatalog.c`'s numeric accessors become thin wrappers around `assetcatalog.c`. The shadow arrays in `modmgr.c` are removed. Eventually `modelcatalog.c` may be absorbed into `assetcatalog.c` or kept as a facade for the model-validation-specific code (VEH loading, thumbnails).

---

## Network Implications

Each entry carries two hashes:

1. **`id_hash` (FNV-1a)**: Hash table slot resolution — fast local lookup
2. **`net_hash` (CRC32)**: Network identity — stable across machines, consistent with existing protocol

Network sync uses `net_hash`. This replaces the current `modmgrGetManifestHash()` (combined CRC of all enabled mod IDs) with per-asset granularity. A client missing one map doesn't need to download an entire mod — just the specific components.

**Wire format** (for SVC_ASSET_REQUIREMENTS, future D3R-9):
```
u16 count
[count × { u32 net_hash, u32 size_bytes }]
```

Client responds with a bitmask of which assets it has. Server compiles a delta pack of the missing ones.

`catalogResolveByHash(net_hash)` does a linear scan of the entry pool (not the hash table) to find matching `net_hash` values. This is an infrequent operation (connection time only), so O(n) is acceptable.

---

## Memory Budget & Dynamic Growth

The catalog uses **dynamic allocation** throughout — no hard caps. Both the hash table and the entry pool grow on demand.

| Component | Initial Size | Growth Strategy |
|-----------|-------------|-----------------|
| Hash table (slot array) | 2048 slots × 8 bytes = 16 KB | Doubles when load factor exceeds 70%. Rehash all entries. |
| Entry pool | 512 entries × ~768 bytes = 384 KB | `realloc` doubles capacity when full. Pointers into the pool are **not** stored externally — the hash table stores indices, not pointers, so realloc is safe. |
| **Initial total** | | **~400 KB** |

At the base game's ~256 assets + 5 bundled mods (~150 assets), the initial allocation is more than sufficient. A power user with 200+ mod components would trigger one pool growth (512 → 1024) and no hash table growth. Even 2000+ entries would only reach ~1.5 MB — negligible on modern hardware.

The existing `modelcatalog.c` uses a fixed `s_Catalog[256]` at ~200 bytes per entry = 50 KB. The new system is larger but covers all asset types (not just models), supports string-keyed lookup, and has no ceiling.

**Implementation detail**: The hash table stores entry pool indices (`s32`), not pointers. This means `realloc` on the entry pool doesn't invalidate hash table slots. External code receives `const asset_entry_t *` from `catalogResolve()` — these pointers are valid until the next `catalogClear()` or pool resize. Since the catalog is populated once at startup and only rebuilt on mod toggle (which returns to title screen), pointer lifetime isn't a practical concern.

---

## Risks and Mitigations

| Risk | Impact | Mitigation |
|------|--------|------------|
| Hash collisions causing silent wrong-asset resolution | Gameplay bugs | FNV-1a has strong avalanche properties; collision probability negligible at 300-500 entries. All lookups verify the full ID string after hash match — hash is only used for slot selection. Network `net_hash` (CRC32) lookups also verify full string. |
| Entry struct too large, wastes memory | Minor | Union for type-specific data keeps entries bounded. Paths are the dominant cost; `FS_MAXPATH` is typically 260 bytes. Dynamic pool means we only allocate what's used. Could switch to pooled string storage if needed — premature now. |
| Two-catalog confusion during migration | Developer error | Clear naming: `catalogResolve()` = new, `catalogGetEntry()` = old. Document in CLAUDE.md. |
| `base:` override by mods creates unexpected behavior | User confusion | Override is intentional (enables total conversions). Mod Manager (D3R-6) surfaces conflicts. |

---

## Success Criteria

1. `assetcatalog.h/c` compiles and links cleanly
2. Hash table insert/lookup works correctly (verified by unit-style test in a scratch main, or by registering known assets and resolving them)
3. API supports all operations needed by D3R-3 (base registration) and D3R-4 (scanner registration)
4. CRC32 hashes match between local computation and network-received values
5. No changes to existing `modelcatalog.c` or `modmgr.c` in this phase (coexistence, not replacement)
6. Dynamic growth works: inserting 2000+ entries triggers realloc and rehash without data loss

---

## Open Questions for Game Director

1. ~~**Entry cap**~~: **Resolved** — no cap. Dynamic allocation with growth on demand. Users with 200+ mods will trigger one realloc. (Director feedback, S28)

2. ~~**Override behavior**~~: **Resolved** — yes, mods can override `base:` assets. Total conversions are a first-class feature. The game itself is essentially a mod. Mod Manager (D3R-6) surfaces overrides for visibility. (Director confirmed, S28.)

3. ~~**CRC32 vs. other hash**~~: **Resolved** — use both. FNV-1a for hash table slot distribution (better avalanche, fewer probe chains). CRC32 for network identity (consistent with existing `modmgrHashString()` wire format). Each entry stores both hashes. No trade-off — they serve different purposes. (Director + engineering analysis, S28.)
