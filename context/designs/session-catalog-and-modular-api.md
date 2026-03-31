# Session Catalog and Modular Catalog API — Implementation Plan

**Status**: Design — awaiting game director approval
**Date**: 2026-03-31
**Author**: Session S89 (architect synthesis)

**Unifies**: ADR-003 (catalog core), catalog-loading-plan.md (C-4 through C-9), match-startup-pipeline.md (manifest phase), B-12 Phase 3 (chrslots removal), D3R-5 (callsite migration), D3R-9 (network distribution)

**Dependencies**:
- Asset Catalog core (ADR-003 — done, `assetcatalog.c` exists)
- Match Startup Pipeline Phase A–F (done, `netmanifest.c` exists)
- Participant pool B-12 Phase 2 (done, S47b)
- Catalog Activation C-0 through C-4 (done, S74)

**Cross-references**:
  [ADR-003-asset-catalog-core.md](../ADR-003-asset-catalog-core.md) — hash table, FNV-1a + CRC32, entry struct
  [catalog-loading-plan.md](../catalog-loading-plan.md) — C-series loading intercept plan
  [designs/match-startup-pipeline.md](match-startup-pipeline.md) — 7-phase match pipeline
  [b12-participant-system.md](../b12-participant-system.md) — chrslots removal
  [component-mod-architecture.md](../component-mod-architecture.md) — component filesystem, D3R series

---

## 1. Problem Statement

The asset catalog exists and works (`assetcatalog.c`). The match manifest pipeline exists and works (`netmanifest.c`). But these two systems don't yet speak the same language, and neither fully replaces the legacy numeric index system at interface boundaries.

Three problems remain:

**Problem 1 — Raw indices at every interface boundary.** The audit found ~180 call sites using raw numeric indices for asset identity:

| Site Class | Count | Location |
|------------|-------|----------|
| `g_HeadsAndBodies[n].filenum` in model load calls | ~30 | `body.c`, `player.c`, `menu.c`, `setup.c` |
| `netclient.settings.bodynum/headnum` as raw wire integers | ~15 | net messages across `net.c`, `netmsg.c` |
| Save files storing raw body/head/stage integers | ~12 | `savefile.c`, `scenario_save.c` |
| Stage file IDs accessed raw (`bgfileid`, `padsfileid`, `setupfileid`) | ~10 | `bg.c`, `setup.c` |
| `WEAPON_*` enums in net messages without catalog mediation | ~10 | net message handlers |
| Prop type indices for model lookup | ~20 | prop spawning, setup code |
| `netmanifest.c` constructing synthetic IDs that miss the catalog | 3 | `netmanifest.c` |
| Player body/head as raw `u8` in `CLC_SETTINGS`/`SVC_STAGE_START` | 8 | net handlers |

Every one of these is a violation of the "name-based asset resolution only" constraint (S27) and each one is a potential index-shift bug when mods change the underlying arrays.

**Problem 2 — No compact wire format for assets.** The manifest pipeline uses 32-byte CRC32 net hashes to identify assets in SVC_MATCH_MANIFEST. That works for the one-time manifest exchange. But in-game net messages still use raw numeric indices. We need a compact session-scoped integer that is both small on the wire and unambiguous.

**Problem 3 — No diff-based asset lifecycle for SP.** The catalog-loading-plan.md C-9 (`catalogComputeStageDiff`) exists for MP stage transitions, but there is no equivalent for SP campaign mission loading. Every SP mission transition loads/unloads assets without the catalog's knowledge. This leads to redundant reloads of shared assets (e.g., Joanna's model between consecutive Carrington Institute missions) and blocks SP mod support.

---

## 2. Solution Overview

Three interconnected systems solve these problems:

```
┌─────────────────────────────────────────────────────────────────────┐
│                    MODULAR CATALOG API LAYER                        │
│                                                                     │
│  catalogResolveBody()  catalogResolveHead()  catalogResolveStage()  │
│  catalogResolveWeapon()  catalogResolveProp()                       │
│  catalogWriteAssetRef()  catalogReadAssetRef()                      │
│                                                                     │
│  ← 180 call sites migrate here; one interception point per type    │
└──────────────────────────┬──────────────────────────────────────────┘
                           │
               ┌───────────┴───────────┐
               ▼                       ▼
┌──────────────────────┐  ┌────────────────────────────────────────┐
│  SESSION CATALOG     │  │           LOAD MANIFEST                │
│                      │  │                                        │
│  Built at match      │  │  MP: match manifest doubles as         │
│  start during        │  │      load manifest (diff + load delta) │
│  manifest phase.     │  │                                        │
│                      │  │  SP: mission manifest built from       │
│  Maps string IDs →   │  │      mission asset requirements        │
│  u16 session IDs     │  │      (same diff infrastructure)        │
│                      │  │                                        │
│  In-game net msgs    │  │  Shared assets between consecutive     │
│  use u16 only        │  │  missions stay loaded (no redundant    │
│  (2 bytes per ref)   │  │  unload/reload cycle)                  │
└──────────────────────┘  └────────────────────────────────────────┘
```

---

## 3. Design Principles

These principles are non-negotiable. They override any local engineering preference.

**1. The catalog is the single source of truth.**
It replaces the entire legacy N64 loading pipeline, not just networking. `g_HeadsAndBodies[]`, `g_Stages[]`, `g_ModBodies[]` — all of these are private implementation details that exist behind the catalog. Nothing outside the catalog may use them for asset identity.

**2. No raw indices at interface boundaries.**
Legacy N64 array indices are private implementation details. They never appear:
- On the wire (any net message)
- In save files
- In identity profiles
- In any public API return type

The catalog returns `runtime_index` internally for load calls. That internal value is ephemeral and invisible to callers.

**3. Mods and base content are peers.**
The game does not distinguish between `base:joanna_dark` and `gf64_bond` at the resolution layer. They resolve identically. The `bundled`, `temporary`, and `category` fields on `asset_entry_t` exist for management surfaces (Mod Manager, UI grouping) only.

**4. No silent fallbacks.**
If validation passed (required before any match transition), everything exists. Missing at load time = hard error = pipeline bug. `catalogGetSafeBodyPaired()` is a defense-in-depth guard that should never trigger. If it does, log a `[CATALOG-ASSERT]` warning and investigate.

**5. Clean lifecycle.**
- Session catalog: match lifetime. Built during manifest negotiation, destroyed on match end.
- Load manifest: transition-scoped. Built before each mission/match load, destroyed after load completes.
- Catalog entries: until mod toggle or shutdown.

**6. Full string IDs for persistence and debugging. Session IDs for real-time wire efficiency.**
- Disk (save files, identity profiles): catalog string IDs (e.g., `"base:joanna_dark"`)
- Debug logs: catalog string IDs + session ID annotation
- Network wire (in-game messages): session IDs (u16, 2 bytes)
- Network wire (manifest exchange): string IDs (one-time, negligible bandwidth)

---

## 4. System 1 — Modular Catalog API Layer

### 4.1 Purpose

One function per asset type, called at every point where game code needs to resolve an asset for loading or referencing. Every call site that currently does `g_HeadsAndBodies[bodynum].filenum` or `netclient.settings.bodynum` migrates to one of these functions.

This is the **single interception point** for:
- Mod override injection
- Validation and error logging
- Session ID lookup (for wire marshaling)
- Metrics / debug tracing

### 4.2 API

```c
// ── Resolution structs ──────────────────────────────────────────────

typedef struct {
    const asset_entry_t *entry;   // full catalog entry (may be NULL on failure)
    s32   filenum;                // runtime filenum for model load calls
    f32   model_scale;            // from catalog entry
    const char *display_name;     // from catalog entry
    u32   net_hash;               // CRC32 for manifest checks
    u16   session_id;             // assigned during session catalog build (0 = not assigned)
} catalog_body_result_t;

typedef catalog_body_result_t catalog_head_result_t; // same fields

typedef struct {
    const asset_entry_t *entry;
    s32   bgfileid;
    s32   padsfileid;
    s32   setupfileid;
    s32   stagenum;               // logical stage ID (e.g. 0x5e)
    u32   net_hash;
    u16   session_id;
} catalog_stage_result_t;

typedef struct {
    const asset_entry_t *entry;
    s32   filenum;                // weapon model file
    s32   weapon_num;             // runtime WEAPON_* enum value
    u32   net_hash;
    u16   session_id;
} catalog_weapon_result_t;

typedef struct {
    const asset_entry_t *entry;
    s32   filenum;                // prop model file
    s32   prop_type;              // runtime PROPTYPE_* value
    u32   net_hash;
    u16   session_id;
} catalog_prop_result_t;

// ── Resolution by catalog string ID ────────────────────────────────

bool catalogResolveBody(const char *id, catalog_body_result_t *out);
bool catalogResolveHead(const char *id, catalog_head_result_t *out);
bool catalogResolveStage(const char *id, catalog_stage_result_t *out);
bool catalogResolveWeapon(const char *id, catalog_weapon_result_t *out);
bool catalogResolveProp(const char *id, catalog_prop_result_t *out);

// ── Resolution by session ID (in-game, after session catalog built) ─

bool catalogResolveBodyBySession(u16 session_id, catalog_body_result_t *out);
bool catalogResolveHeadBySession(u16 session_id, catalog_head_result_t *out);
bool catalogResolveStageBySession(u16 session_id, catalog_stage_result_t *out);
bool catalogResolveWeaponBySession(u16 session_id, catalog_weapon_result_t *out);
bool catalogResolvePropBySession(u16 session_id, catalog_prop_result_t *out);

// ── Resolution by net_hash (manifest check phase) ──────────────────

const asset_entry_t *catalogResolveByNetHash(u32 net_hash); // linear scan, O(n), use sparingly

// ── Wire helpers (the ONLY functions that may write/read asset refs) ─

void catalogWriteAssetRef(netbuf_t *buf, u16 session_id);    // writes 2 bytes
u16  catalogReadAssetRef(netbuf_t *buf);                      // reads 2 bytes
```

### 4.3 Return convention

All `catalogResolveX()` functions return `true` on success, `false` on failure. On failure, `out` is zeroed. Callers must check the return value. Failure is a hard error — log it at `[CATALOG-ERROR]` level with the ID that failed, then return NULL/safe fallback.

### 4.4 File placement

```
port/include/assetcatalog.h        ← extend with new API types + declarations
port/src/assetcatalog.c            ← implement resolution functions
  (or port/src/assetcatalog_api.c  ← if assetcatalog.c is already large)
```

### 4.5 Call site migration

Migration is incremental — old sites continue working via legacy accessors during the migration window. New code must use the new API. Compiler deprecation attributes on legacy accessors after Phase 5 completes.

**Before (legacy pattern):**
```c
// Typical pre-migration body resolution:
s32 filenum = g_HeadsAndBodies[bodynum].filenum;
f32 scale   = g_HeadsAndBodies[bodynum].modelScale;
fileLoadToNew(filenum, MEMPOOL_STAGE);
```

**After (catalog API):**
```c
catalog_body_result_t body;
if (!catalogResolveBody(netclient.settings.body_id, &body)) {
    logPrintf("[CATALOG-ERROR] failed to resolve body '%s'\n", netclient.settings.body_id);
    return;
}
fileLoadToNew(body.filenum, MEMPOOL_STAGE);
// body.model_scale, body.display_name, body.session_id also available
```

---

## 5. System 2 — Session Catalog (Network Translation Layer)

### 5.1 Purpose

A compact, ephemeral lookup table negotiated at match start. Maps full catalog string IDs to small u16 integers for the duration of a match. All in-game net messages use u16 session IDs instead of strings or CRC32 hashes.

The session catalog is **not** a new catalog — it is a translation layer on top of the existing asset catalog. The asset catalog is the source of truth; the session catalog is a per-match index into it.

### 5.2 Design rationale

| Approach | Wire cost per ref | Determinism | Mod-safe |
|----------|-------------------|-------------|----------|
| Raw numeric index (`u8`) | 1 byte | No (breaks on mod) | No |
| CRC32 net_hash (`u32`) | 4 bytes | Yes | Yes |
| Full string ID | 32+ bytes | Yes | Yes |
| **Session ID (u16)** | **2 bytes** | **Yes (negotiated)** | **Yes** |

Session IDs give CRC32-level correctness at near-raw-index wire cost. The one-time mapping table is sent at match start (negligible bandwidth, sent once on NETCHAN_CONTROL reliable).

### 5.3 Architecture

```
MATCH START
     │
     ▼ (Phase 2 manifest build — existing)
manifestBuild() builds match_manifest_t
     │
     ▼ (NEW — Phase 1 of this design)
sessionCatalogBuild(manifest)
  ├── Iterates manifest entries (bodies, heads, stages, weapons, props)
  ├── Assigns sequential u16 session IDs starting at 1 (0 = "no asset")
  └── Populates s_SessionCatalog[] lookup table
     │
     ▼
sessionCatalogBroadcast()
  └── SVC_SESSION_CATALOG → all room clients (reliable, once)
     │                         │
     │                         ▼ (per client)
     │                  sessionCatalogReceive()
     │                    ├── Parses SVC_SESSION_CATALOG
     │                    ├── For each entry: catalogResolveByNetHash() → local entry
     │                    └── Builds s_LocalTranslation[session_id → local entry]
     │
     ▼ GAMEPLAY
All net messages use catalogWriteAssetRef(buf, session_id)
                     catalogReadAssetRef(buf) → session_id
                     catalogResolveBodyBySession(session_id, &result)
     │
     ▼ MATCH END
sessionCatalogTeardown()
  └── Zeros s_SessionCatalog[], frees any dynamic allocations
```

### 5.4 Data structures

```c
// Session catalog entry (server-side: ID → session mapping)
typedef struct {
    u16         session_id;
    asset_type_e type;
    char        asset_id[CATALOG_ID_LEN];  // full string ID
    u32         net_hash;                  // CRC32 for verification
} session_entry_t;

// Server-side catalog
#define SESSION_CATALOG_MAX 256  // max unique assets in a single match
typedef struct {
    session_entry_t entries[SESSION_CATALOG_MAX];
    u16             count;
    bool            active;   // true for match duration
} session_catalog_t;

// Client-side translation table
typedef struct {
    u16                   session_id;
    const asset_entry_t  *local_entry;   // pointer into local assetcatalog pool
    bool                  resolved;      // false = missing from local catalog
} session_translation_t;
```

### 5.5 Wire format: SVC_SESSION_CATALOG

```
Opcode: 0x67 (next available in SVC 0x60 lobby range, after SVC_MATCH_COUNTDOWN 0x63)
Channel: NETCHAN_CONTROL (reliable, ordered)
Sent: once, after manifest broadcast, before SVC_STAGE_START

Wire format:
  u8   opcode             = 0x67
  u16  num_entries        = total assets in session catalog
  [for each entry]:
    u16  session_id       = assigned session ID (1-based, sequential)
    u8   asset_type       = ASSET_CHARACTER/ASSET_MAP/ASSET_WEAPON/ASSET_PROP/...
    u32  net_hash         = CRC32 of asset_id (verification)
    u16  id_len           = length of asset_id string (including null terminator)
    char asset_id[]       = full catalog string ID (e.g., "base:joanna_dark")
```

**Estimated size**: A 8-player match with 8 unique bodies + 8 unique heads + 1 stage + 6 weapons + 20 props ≈ 43 entries. Average ID length ~32 bytes. Total: 43 × (2+1+4+2+32) ≈ 1.8 KB. One-time, reliable — negligible.

### 5.6 Wire format: In-game asset references

```
Per-reference wire cost: 2 bytes (u16 session_id)

Example: SVC_STAGE_START v24 player participant (after migration):
  [before] u32 body_hash + u32 head_hash  = 8 bytes
  [after]  u16 body_session + u16 head_session = 4 bytes  (50% savings)
```

### 5.7 Session ID 0

Session ID 0 is reserved as "no asset" / "not applicable." It is never assigned to a real entry. Receiving session ID 0 in a context that requires a valid asset is a hard protocol error.

### 5.8 Client resolution failure

If a client receives `SVC_SESSION_CATALOG` and cannot resolve an entry via `catalogResolveByNetHash()` (asset not in local catalog after manifest + transfer phases), that entry gets `resolved = false`. The ready gate (Phase E of match pipeline) should have caught this — a `resolved = false` at this point means the pipeline has a bug. Log `[SESSION-CATALOG-ASSERT]` and report to server.

### 5.9 File placement

```
port/include/net/sessioncatalog.h   ← new
port/src/net/sessioncatalog.c       ← new
```

Public API:

```c
// Server-side
void     sessionCatalogBuild(const match_manifest_t *manifest);
void     sessionCatalogBroadcast(void);                        // sends SVC_SESSION_CATALOG
void     sessionCatalogTeardown(void);
u16      sessionCatalogGetId(const char *asset_id);            // 0 if not found
u16      sessionCatalogGetIdByHash(u32 net_hash);              // 0 if not found

// Client-side
void     sessionCatalogReceive(netbuf_t *buf);                 // reads SVC_SESSION_CATALOG
const asset_entry_t *sessionCatalogLocalResolve(u16 session_id);
bool     sessionCatalogLocalIsResolved(u16 session_id);

// Lifecycle
bool     sessionCatalogIsActive(void);
void     sessionCatalogLogMapping(void);                       // debug: log full table at match start
```

---

## 6. System 3 — Load Manifest

### 6.1 Purpose

Manage asset lifecycle (load/unload/keep) for both multiplayer and singleplayer, using the catalog as the source of truth about what's needed. The match manifest (`match_manifest_t`) already exists; this system makes it the driver of all load decisions.

### 6.2 Manifest diff operation

```c
typedef struct {
    asset_entry_t **to_load;     // assets needed but not currently loaded
    s32             to_load_count;
    asset_entry_t **to_unload;   // assets currently loaded but not needed
    s32             to_unload_count;
    asset_entry_t **to_keep;     // assets needed AND currently loaded
    s32             to_keep_count;
} manifest_diff_t;

// Diff current loaded assets against a needed set.
// Returns a manifest_diff_t that drives the load/unload sequence.
manifest_diff_t manifestDiff(
    const match_manifest_t *currently_loaded,
    const match_manifest_t *needed
);

void manifestDiffFree(manifest_diff_t *diff);

// Apply a diff: unload to_unload[], load to_load[], skip to_keep[].
// Calls catalogResolveX() + assetCatalogLoad/Unload() internally.
void manifestApplyDiff(const manifest_diff_t *diff);
```

### 6.3 MP: match manifest as load manifest

```
CLC_ROOM_START received
     │
     ▼
manifestBuild(room, matchconfig)    ← existing Phase B
     │
     ▼
sessionCatalogBuild(manifest)       ← NEW (§5)
sessionCatalogBroadcast()
     │
     ▼
SVC_SESSION_CATALOG → all clients
     │
     ▼  (all clients READY — existing Phase E)
     │
     ▼
diff = manifestDiff(g_CurrentLoadedManifest, manifest)
manifestApplyDiff(diff)             ← loads only what's new, unloads only what's gone
     │
     ▼
SVC_STAGE_START v24
     │
     ▼ GAMEPLAY
```

`g_CurrentLoadedManifest` tracks what's currently loaded. On server startup it's empty. After each match it reflects the just-completed match's manifest.

### 6.4 SP: mission manifest

For singleplayer, a mission manifest is built from the mission's asset requirements before each mission loads.

```c
// Build a manifest from a singleplayer mission.
// Queries catalog for all assets used in the given stage.
match_manifest_t *manifestBuildMission(s32 stagenum);

// SP mission transition:
match_manifest_t *needed = manifestBuildMission(next_stagenum);
manifest_diff_t diff = manifestDiff(g_CurrentLoadedManifest, needed);
manifestApplyDiff(diff);
manifestFree(needed);
```

Benefit: Joanna's model loaded in dataDyne Central stays loaded through consecutive dataDyne missions. Only unique assets for each mission are loaded/unloaded. Groundwork for SP mod support (custom mission asset overrides, total conversion campaigns).

### 6.5 Manifest tracking state

```c
// Global: what's currently loaded (updated by manifestApplyDiff)
extern match_manifest_t *g_CurrentLoadedManifest;
```

Initialized to `NULL` (nothing loaded). Updated at the end of each `manifestApplyDiff()` call.

---

## 7. Integration Notes

### 7.1 How this extends match-startup-pipeline.md

The existing match pipeline has 7 phases (Gather → Manifest → Check → Transfer → Ready Gate → Load → Sync). This design inserts **Session Catalog build** between Phase 2 (Manifest) and Phase 6 (Load):

```
Phase 2: MANIFEST     ← existing: server builds match_manifest_t
     │
Phase 2.5: SESSION CATALOG ← NEW: sessionCatalogBuild() + SVC_SESSION_CATALOG
     │                              clients receive + resolve locally
     ▼
Phase 3: CHECK        ← existing: clients check manifest vs local catalog
...
Phase 6: LOAD         ← enhanced: manifestDiff() + manifestApplyDiff()
                                   instead of "unload everything, load everything"
```

Phase E (Ready Gate) already exists. The session catalog is broadcast as part of the manifest distribution, so no additional round-trip is added.

### 7.2 How this relates to ADR-003

ADR-003 establishes the hash table, `asset_entry_t`, FNV-1a + CRC32 dual hashes, and `catalogResolve()`. This design builds directly on that foundation:

- `catalogResolveBody()` etc. are thin wrappers around `catalogResolve()` from ADR-003
- The session catalog uses `net_hash` (CRC32) from `asset_entry_t` for wire verification
- `catalogResolveByNetHash()` is the existing ADR-003 `catalogResolveByHash()` renamed for clarity
- The session catalog's `LOCAL_SESSION_CATALOG` maps session IDs to `const asset_entry_t *` pointers from the existing pool

### 7.3 How this relates to catalog-loading-plan.md (C-4 through C-9)

The catalog-loading-plan.md C-series describes intercepting the 4 gateway functions (fileLoadToNew, texLoad, animLoadFrame, sndStart). Phase 5 of this design (§8.5) is exactly those C-series items, reframed as catalog API migration. C-4 through C-7 become subsets of Phase 5. C-9 (stage diff) is now `manifestDiff()` in §6.

### 7.4 Protocol version

SVC_SESSION_CATALOG (opcode 0x67) requires a protocol bump. However, Phase A of match-startup-pipeline.md already bumped to v24. SVC_SESSION_CATALOG is additive within v24 — it can be added before the protocol bump to v25.

The wire protocol migration (Phase 3 here) that changes `CLC_SETTINGS` and `SVC_STAGE_START` to use session IDs **will** require a bump to v25.

### 7.5 Impact on B-12 participant system

B-12 Phase 3 removes the chrslots bitmask from SVC_STAGE_START. The session catalog migration (Phase 3 here) also modifies SVC_STAGE_START — replacing `u32 body_hash + u32 head_hash` with `u16 body_session + u16 head_session`. These two changes should be bundled into the same protocol version bump (v25) to avoid a second disruptive break.

Dependency: B-12 Phase 3 must complete before or simultaneously with Phase 3 of this design.

### 7.6 Impact on save files and identity profiles

Identity profiles (`identity.c`) and save files (`savefile.c`, `scenario_save.c`) currently store raw integers. Phase 4 migrates these to catalog string IDs. For backward compatibility, a legacy integer fallback reader converts old saves on first load, then re-saves in the new format.

---

## 8. Migration Phases

### Phase 1: Session Catalog Infrastructure
**New files only. Zero changes to existing code.**
**Depends on**: match-startup-pipeline.md Phase B (manifestBuild — done, S85)

Files to create:
- `port/include/net/sessioncatalog.h`
- `port/src/net/sessioncatalog.c`

Implementation checklist:
- [ ] Define `session_catalog_t`, `session_translation_t` structs in `sessioncatalog.h`
- [ ] `sessionCatalogBuild(manifest)` — iterate manifest entries, assign sequential u16 IDs
- [ ] `sessionCatalogGetId(asset_id)` — O(1) lookup in s_SessionCatalog[]
- [ ] `sessionCatalogGetIdByHash(net_hash)` — O(n) fallback
- [ ] `sessionCatalogBroadcast()` — write SVC_SESSION_CATALOG wire format to netbuf, send reliable
- [ ] Register SVC_SESSION_CATALOG = 0x67 in `netmsg.h`
- [ ] `sessionCatalogReceive(buf)` — parse wire format, populate s_LocalTranslation[]
- [ ] `sessionCatalogLocalResolve(session_id)` — O(1) array lookup
- [ ] `sessionCatalogLocalIsResolved(session_id)` — safety check
- [ ] `sessionCatalogTeardown()` — zero all state
- [ ] `sessionCatalogLogMapping()` — debug: print full table at INFO level at match start
- [ ] Wire `sessionCatalogBuild` + `sessionCatalogBroadcast` into `manifestBuild()` call site in `netmsgClcLobbyStartRead`
- [ ] Wire `sessionCatalogReceive` handler into `net.c` dispatch table
- [ ] Wire `sessionCatalogTeardown` into match end path in `hub.c`
- [ ] Test: log shows session catalog at match start, all entries resolve on clients

---

### Phase 2: Modular API Layer
**New functions only. Wraps existing legacy lookups. Zero call site changes.**
**Depends on**: Phase 1 (session IDs available in results)

Files to modify:
- `port/include/assetcatalog.h` — add new types and declarations
- `port/src/assetcatalog.c` (or new `port/src/assetcatalog_api.c`)

Implementation checklist:
- [ ] Define `catalog_body_result_t`, `catalog_head_result_t`, `catalog_stage_result_t`, `catalog_weapon_result_t`, `catalog_prop_result_t`
- [ ] `catalogResolveBody(id, out)` — calls `catalogResolve(id)`, fills struct, queries session catalog for session_id
- [ ] `catalogResolveHead(id, out)` — same pattern
- [ ] `catalogResolveStage(id, out)` — resolves bgfileid, padsfileid, setupfileid from stage entry
- [ ] `catalogResolveWeapon(id, out)` — resolves weapon model filenum + weapon_num
- [ ] `catalogResolveProp(id, out)` — resolves prop model filenum + prop_type
- [ ] `catalogResolveBodyBySession(session_id, out)` — via `sessionCatalogLocalResolve()` then fills struct
- [ ] `catalogResolveHeadBySession(session_id, out)` — same
- [ ] `catalogResolveStageBySession(session_id, out)` — same
- [ ] `catalogResolveWeaponBySession(session_id, out)` — same
- [ ] `catalogResolvePropBySession(session_id, out)` — same
- [ ] `catalogWriteAssetRef(buf, session_id)` — writes 2-byte u16 to netbuf
- [ ] `catalogReadAssetRef(buf)` — reads 2-byte u16 from netbuf
- [ ] Test: call each function with known IDs, verify all struct fields populate correctly

---

### Phase 3: Wire Protocol Migration (HIGH priority)
**Modifies net message read/write. Requires protocol version bump to v25.**
**Depends on**: Phase 1 + Phase 2, B-12 Phase 3

Scope: Replace all raw indices in net messages with session IDs.

Files to modify:
- `port/src/net/netmsg.c` — all affected message readers/writers
- `port/src/net/net.c` — dispatch + CLC_SETTINGS handler
- `port/src/net/matchsetup.c` — SVC_STAGE_START builder
- `port/include/net/netmsg.h` — NET_PROTOCOL_VER bump

Implementation checklist:
- [ ] Bump `NET_PROTOCOL_VER` to 25
- [ ] `CLC_SETTINGS`: change `u8 bodynum` + `u8 headnum` → `u16 body_session` + `u16 head_session`
- [ ] `SVC_STAGE_START` v25: change `u32 body_hash + u32 head_hash` per participant → `u16 body_session + u16 head_session` (coordinate with B-12 Phase 3 chrslots removal in same message)
- [ ] `SVC_STAGE_START` v25: change `u8 stagenum` → `u16 stage_session` (session ID for stage)
- [ ] `SVC_STAGE_START` v25: change `u8 weapons[6]` raw WEAPON_* → `u16 weapon_session[6]`
- [ ] All `netbufWriteU8(buf, settings->bodynum)` patterns → `catalogWriteAssetRef(buf, sessionId)` using `catalogReadAssetRef(buf)` in reverse
- [ ] Server-side: populate session IDs from `sessionCatalogGetId()` when building messages
- [ ] Client-side: call `catalogResolveBodyBySession()` on received session IDs
- [ ] Remove `netclient.settings.bodynum` / `headnum` integer fields; replace with `body_id[CATALOG_ID_LEN]` + `head_id[CATALOG_ID_LEN]`
- [ ] Fix 3 synthetic ID construction sites in `netmanifest.c`
- [ ] Fix 8 raw u8 body/head read sites in net handlers
- [ ] Test: full match start with mods — verify no character mismatch, no index-shift bugs

---

### Phase 4: Persistence Migration (MEDIUM priority)
**Modifies save format. Requires save version bump.**
**Depends on**: Phase 2

Scope: Replace raw integers in save files and identity profiles with catalog string IDs.

Files to modify:
- `port/src/savefile.c`
- `port/src/scenario_save.c`
- `port/src/identity.c`
- `port/include/savefile.h`

Implementation checklist:
- [ ] Bump `SAVE_VERSION` in `savefile.h`
- [ ] Identity profiles: replace `u8 bodynum` + `u8 headnum` with `char body_id[CATALOG_ID_LEN]` + `char head_id[CATALOG_ID_LEN]`
- [ ] Save files: replace raw body/head/stage integers with string IDs
- [ ] Scenario presets (`scenario_save.c`): replace stage integer with stage string ID
- [ ] Add legacy integer fallback reader: on load, if version < new SAVE_VERSION, read integers, call `catalogResolveByRuntimeIndex()` to get string ID, re-save in new format
- [ ] `catalogResolveByRuntimeIndex(type, runtime_index)` — reverse lookup (O(n) scan, used only during migration)
- [ ] Test: load an old save, verify migration runs, new save uses string IDs, reload still works

---

### Phase 5: Load Path Migration (MEDIUM priority)
**Completes C-4 through C-9 from catalog-loading-plan.md.**
**Depends on**: Phase 2

Scope: All ~180 `g_HeadsAndBodies[]`, `g_Stages[]`, direct filenum access sites → catalog API calls. This is the largest phase by call site count.

Subsystem groupings (can be done in any order, independently shippable):

**5a — Body/head model load sites (~30 sites in body.c, player.c, menu.c, setup.c):**
- [ ] Identify all `g_HeadsAndBodies[n].filenum` in model load calls
- [ ] Replace with `catalogResolveBody(id, &result)` + `result.filenum`
- [ ] Where `n` comes from a raw integer local, trace its origin and replace with ID

**5b — Stage file ID sites (~10 sites in bg.c, setup.c):**
- [ ] Identify all `stage->bgfileid`, `stage->padsfileid`, `stage->setupfileid` access
- [ ] Replace with `catalogResolveStage(id, &result)` + `result.bgfileid` etc.

**5c — Prop model lookup sites (~20 sites in prop spawning, setup code):**
- [ ] Identify all prop type → model filenum lookups
- [ ] Replace with `catalogResolveProp(id, &result)` + `result.filenum`

**5d — Weapon model sites (~10 sites in weapon code):**
- [ ] Identify all `WEAPON_*` enum → model filenum lookups
- [ ] Replace with `catalogResolveWeapon(id, &result)` + `result.filenum`

**5e — fileLoadToNew / fileLoadToAddr intercept (C-4 complement):**
- [ ] Verify `catalogGetFileOverride` intercept in `romdataFileLoad()` covers all body/head sites after 5a
- [ ] Complete C-5 (`catalogGetTextureOverride` in `texLoad()`) — coded, needs wiring confirmation
- [ ] Complete C-6 (`catalogGetAnimOverride` in `animLoadFrame/Header()`)

**5f — Deprecation pass:**
- [ ] Add `__attribute__((deprecated))` to legacy numeric accessors (`catalogGetEntry(index)`, `catalogGetSafeBody(n)`, etc.)
- [ ] Fix resulting compiler warnings (= complete the migration)
- [ ] Remove deprecation attributes once all sites are clean

---

### Phase 6: Load Manifest for SP (LOWER priority)
**New infrastructure only. No existing code removed.**
**Depends on**: Phase 5 (catalog must be the load path for this to be meaningful)

Scope: Mission manifest builder + diff-based load/unload between SP missions.

Files to create/modify:
- `port/src/net/netmanifest.c` — add `manifestBuildMission()`, `manifestDiff()`, `manifestApplyDiff()`
- `port/include/net/netmanifest.h` — add new function declarations
- `port/src/pdmain.c` or mission load site — wire `manifestBuildMission` before mission load

Implementation checklist:
- [ ] Define `manifest_diff_t` struct
- [ ] `manifestBuildMission(stagenum)` — query catalog for all assets used by stage (tiles, bg, pads, setup, characters from spawn list)
- [ ] `manifestDiff(currently_loaded, needed)` — produces to_load/to_unload/to_keep lists
- [ ] `manifestDiffFree(diff)` — frees to_load/to_unload/to_keep arrays
- [ ] `manifestApplyDiff(diff)` — calls `assetCatalogUnload()` for to_unload, `assetCatalogLoad()` for to_load
- [ ] `g_CurrentLoadedManifest` global — updated by `manifestApplyDiff`
- [ ] Wire into SP mission load path (the equivalent of `matchStart()` for singleplayer)
- [ ] Test: run two consecutive missions sharing Joanna — verify she stays loaded (to_keep), mission-unique assets cycle correctly

---

### Phase 7: Consolidation
**Cleanup phase. No new functionality.**
**Depends on**: Phases 3, 4, 5 complete and verified in playtest

Implementation checklist:
- [ ] `modelcatalog.c` — absorb into `assetcatalog.c` OR retain as thin facade (see ADR-003 §Phase 4 note). Decision criteria: if all VEH/SIGSEGV model validation logic is in `modelcatalog.c` and not duplicated elsewhere, keep as facade. If it can be cleanly extracted, absorb.
- [ ] `modmgr.c` — remove shadow arrays `g_ModBodies[]`, `g_ModHeads[]`, `g_ModArenas[]` (already removed per constraints.md 2026-03-24 — verify no new ones crept back)
- [ ] `modelcatalog.c` legacy numeric accessors — remove (they're now deprecated and unwrapped wrappers)
- [ ] `netmanifest.c` synthetic ID construction sites (3) — verify all removed by Phase 3
- [ ] Audit: grep for any remaining `g_HeadsAndBodies\[` that aren't inside assetcatalog.c or modelcatalog.c
- [ ] Audit: grep for any remaining `netclient.settings.bodynum` / `headnum` raw integer usage
- [ ] Update CLAUDE.md: document that `catalogWriteAssetRef` / `catalogReadAssetRef` are the only permitted net message asset reference functions

---

## 9. Phase Dependency Graph

```
              ┌─────────────────────────────────┐
              │    ADR-003 asset catalog core    │
              │    (assetcatalog.c — DONE)       │
              └──────────────┬──────────────────┘
                             │
              ┌──────────────┴──────────────────┐
              │   match-startup-pipeline Phase B  │
              │   (manifestBuild — DONE, S85)    │
              └──────────────┬──────────────────┘
                             │
               ┌─────────────▼──────────────┐
               │  PHASE 1: Session Catalog   │
               │  Infrastructure             │
               │  (new files only)           │
               └──────┬──────────────────────┘
                      │
         ┌────────────▼────────────┐
         │  PHASE 2: Modular API   │
         │  Layer                  │
         │  (new wrappers only)    │
         └──┬────────────┬─────────┘
            │            │
    ┌───────▼──┐    ┌────▼─────────────────┐
    │ PHASE 3  │    │ PHASE 4              │
    │ Wire     │    │ Persistence          │
    │ Protocol │    │ Migration            │
    │ (v25)    │    │ (save version bump)  │
    │ +B12 P3  │    └──────────────────────┘
    └───┬──────┘
        │
   ┌────▼───────────────────────────────┐
   │  PHASE 5: Load Path Migration      │
   │  (~180 call sites, C-4 through C-9 │
   │  complement, multiple sub-phases)  │
   └──────────────┬──────────────────── ┘
                  │
        ┌─────────▼──────────┐
        │  PHASE 6: Load     │
        │  Manifest for SP   │
        │  (depends on P5    │
        │  for meaningful    │
        │  diff results)     │
        └─────────┬──────────┘
                  │
        ┌─────────▼──────────┐
        │  PHASE 7:          │
        │  Consolidation     │
        │  (cleanup,         │
        │  deprecation)      │
        └────────────────────┘
```

Phases 3 and 4 can run **in parallel** after Phase 2. Phase 5 can begin in parallel with Phase 4.

Phases 5a–5f are independent of each other — any subsystem can be migrated standalone.

---

## 10. Risk / Mitigation Table

| Risk | Impact | Likelihood | Mitigation |
|------|--------|------------|------------|
| Session ID 0 accidentally used as valid asset reference | Protocol corruption | Medium | Enforce "0 = no asset" in all read sites; assert on read if received 0 in an asset-required context |
| Client receives session ID not in its translation table | Crash or wrong model | Low (pipeline should catch) | `sessionCatalogLocalIsResolved()` guard before every `catalogResolveBodyBySession()` call; log `[SESSION-CATALOG-ASSERT]` |
| Phase 5 call site migration misses a site | Index-shift bugs on mod-heavy load | Medium | Grep audit after each sub-phase: `g_HeadsAndBodies\[` outside catalog files = failure; add to CI if possible |
| SVC_SESSION_CATALOG drops (packet loss) | Client never builds translation table | Low (sent on reliable channel) | Reliable send (NETCHAN_CONTROL); if client doesn't receive, the ready gate timeout fires |
| Two catalog systems confused during migration window | Developer error | Medium | Clear naming: `catalogResolve()` = new, `catalogGetEntry()` = old. Deprecation attributes on old API after Phase 5. Document in CLAUDE.md. |
| Phase 3 + B-12 Phase 3 coordination fail | Dual-breaking protocol changes conflict | Medium | Bundle both into single v25 bump. Define combined SVC_STAGE_START v25 format jointly. |
| Phase 4 save migration breaks old saves | User data loss | High impact, Low probability | SAVE_VERSION bump + legacy integer fallback reader + `catalogResolveByRuntimeIndex()`. Test with known old save before shipping. |
| Session catalog exceeds SESSION_CATALOG_MAX | Assert / overflow | Very Low (256 cap vs typical 43 entries) | SESSION_CATALOG_MAX = 256 is conservative; log warning if count > 200; increase cap if ever needed (trivially) |
| SP manifest diff incorrectly marks asset as "keep" when it needs reload | Stale model state | Low | `manifestDiff()` equality check uses `net_hash` (CRC32), not pointer identity. Robust to entry pool resizes. |

---

## 11. Detailed Wire Format Reference

### 11.1 SVC_SESSION_CATALOG (new, Phase 1)

```
Opcode: 0x67
Channel: NETCHAN_CONTROL (reliable, ordered)
Direction: Server → Clients (all room members)
When: After SVC_MATCH_MANIFEST, before SVC_STAGE_START

Byte layout:
  [0]       u8   opcode = 0x67
  [1-2]     u16  num_entries
  [for each entry, variable length]:
    u16  session_id          (sequential, 1-based)
    u8   asset_type          (ASSET_CHARACTER=1, ASSET_MAP=2, ASSET_WEAPON=3,
                               ASSET_PROP=4, ASSET_SKIN=5, ASSET_AUDIO=6, ...)
    u32  net_hash            (CRC32 of asset_id — for client-side verification)
    u16  id_len              (byte length of asset_id including null terminator)
    char asset_id[id_len]    (UTF-8 catalog string ID)
```

### 11.2 In-game asset reference (after Phase 3)

```
Every net message field that was previously a body/head/stage/weapon/prop identifier:

  [before Phase 3]  u8 bodynum          (1 byte, raw array index)
  [before Phase 3]  u32 body_hash       (4 bytes, CRC32 net hash)
  [after Phase 3]   u16 body_session_id (2 bytes, session catalog ID)

Wire helpers:
  catalogWriteAssetRef(buf, session_id)  → netbufWriteU16(buf, session_id)
  catalogReadAssetRef(buf)               → netbufReadU16(buf)
```

### 11.3 SVC_STAGE_START v25 participant block (Phase 3 + B-12 Phase 3)

```
[was v24, per participant]:
  u8   slot_index
  u8   type
  u8   team
  u32  body_hash      ← 4 bytes
  u32  head_hash      ← 4 bytes
  u8   bot_type
  u8   bot_difficulty
  u8   name_len
  char name[]

[v25, per participant — after Phase 3 + B-12 Phase 3]:
  u8   slot_index
  u8   type
  u8   team
  u16  body_session   ← 2 bytes (was 4)
  u16  head_session   ← 2 bytes (was 4)
  u8   bot_type
  u8   bot_difficulty
  u8   name_len
  char name[]

Per-participant savings: 4 bytes. For 8 players: 32 bytes saved.
```

---

## 12. Implementation Checklist Summary

For quick session-start reference:

```
PHASE 1 — Session Catalog Infrastructure
  [  ] sessioncatalog.h / sessioncatalog.c created
  [  ] SVC_SESSION_CATALOG 0x67 registered in netmsg.h
  [  ] sessionCatalogBuild wired after manifestBuild
  [  ] sessionCatalogReceive wired in net.c dispatch
  [  ] sessionCatalogTeardown wired in match end path
  [  ] Compile clean, log shows catalog at match start

PHASE 2 — Modular API Layer
  [  ] catalog_body_result_t etc. defined in assetcatalog.h
  [  ] All 5 catalogResolveX() functions implemented
  [  ] All 5 catalogResolveXBySession() functions implemented
  [  ] catalogWriteAssetRef / catalogReadAssetRef implemented
  [  ] Compile clean, unit-style test passes

PHASE 3 — Wire Protocol Migration (v25)
  [  ] NET_PROTOCOL_VER bumped to 25
  [  ] CLC_SETTINGS bodynum/headnum → session IDs
  [  ] SVC_STAGE_START v25 format (coordinate with B-12 Phase 3)
  [  ] Stage + weapon session IDs in SVC_STAGE_START
  [  ] 3 netmanifest.c synthetic ID sites fixed
  [  ] 8 raw u8 body/head read sites fixed
  [  ] Full networked match: no character mismatch

PHASE 4 — Persistence Migration
  [  ] SAVE_VERSION bumped
  [  ] Identity profiles use string IDs
  [  ] Save files use string IDs
  [  ] Legacy integer fallback reader implemented
  [  ] Old save loads and migrates correctly

PHASE 5 — Load Path Migration
  [  ] 5a: ~30 body/head model load sites
  [  ] 5b: ~10 stage file ID sites
  [  ] 5c: ~20 prop model lookup sites
  [  ] 5d: ~10 weapon model sites
  [  ] 5e: C-5/C-6 texture/anim override wiring
  [  ] 5f: deprecation attributes + warning sweep
  [  ] grep audit: zero g_HeadsAndBodies[ outside catalog files

PHASE 6 — Load Manifest for SP
  [  ] manifestBuildMission() implemented
  [  ] manifestDiff() / manifestApplyDiff() implemented
  [  ] g_CurrentLoadedManifest tracking
  [  ] Wired into SP mission load path
  [  ] Shared-asset keep test passes

PHASE 7 — Consolidation
  [  ] modelcatalog.c absorbed or confirmed as clean facade
  [  ] modmgr.c shadow arrays verified absent
  [  ] Legacy numeric accessors removed
  [  ] Final grep audit: clean
  [  ] CLAUDE.md updated with wire-format guidance
```

---

## 13. Estimated Effort

| Phase | Size | Can parallelize with |
|-------|------|---------------------|
| 1: Session Catalog Infrastructure | Medium (~200 LOC new) | Nothing (first) |
| 2: Modular API Layer | Small (~150 LOC new) | — |
| 3: Wire Protocol Migration | Medium-Large (180 sites, coordinated with B-12 P3) | Phase 4 |
| 4: Persistence Migration | Small-Medium (SAVE_VERSION + migration reader) | Phase 3 |
| 5a–5f: Load Path Migration | Large (180 sites, 6 sub-phases) | Phase 4, independently |
| 6: Load Manifest for SP | Medium (~200 LOC new + wiring) | Phases 3, 4 |
| 7: Consolidation | Small (cleanup, no new functionality) | Nothing (last) |

Phases 1 and 2 are straightforward sessions. Phase 3 is the highest-risk session (protocol break, must coordinate with B-12 Phase 3). Phases 5a–5f are individually small but numerous — suitable for incremental work across multiple sessions.
