# Session Log (Active)

> Recent sessions only. Archives: [1-6](sessions-01-06.md) . [7-13](sessions-07-13.md) . [14-21](sessions-14-21.md) . [22-46](sessions-22-46.md) . [47-78](sessions-47-78.md) . [79-86](sessions-79-86.md)
> Back to [index](README.md)

## Session S96 -- 2026-03-31

**Focus**: SA-6 follow-up — manifest completeness (counter-op assets + runtime safety net)

### What Was Done

1. **`manifestBuildMission` counter-op scan** (`port/src/net/netmanifest.c`):
   - Added scan of `g_Vars.antibodynum` / `g_Vars.antiheadnum` when `g_Vars.antiplayernum >= 0`
   - These assets are not in the props spawn list — they were missing from the SP manifest in Counter-Operative mode
   - Adds `body_N`/`head_N` catalog entries with slot_index=1 (the anti player's slot)

2. **`manifestEnsureLoaded(catalog_id, asset_type)`** added to `netmanifest.c/h`:
   - Checks `g_CurrentLoadedManifest` by FNV-1a hash; if absent, resolves via `assetCatalogResolve()`, adds to manifest, advances catalog state to `ASSET_STATE_LOADED`
   - No-op when `g_CurrentLoadedManifest.num_entries == 0` (MP mode or pre-load) — safe to call unconditionally
   - Synthetic hash fallback with warning log when catalog can't resolve the ID (suppresses repeat spam)

3. **`bodyAllocateChr` wired** (`src/game/body.c`):
   - Added `#include "net/netmanifest.h"`
   - After bodynum/headnum are resolved (post-`bodyChooseHead`), calls `manifestEnsureLoaded("body_N", MANIFEST_TYPE_BODY)` and conditionally `manifestEnsureLoaded("head_N", MANIFEST_TYPE_HEAD)` (skips headnum == -55555 / built-into-body case)
   - Catches any CHR missed by the static pre-scan: deferred objective spawns, random-body resolution, etc.

### Build Status

27/27 clean. Both `PerfectDark.exe` and `PerfectDarkServer.exe` link clean. No new warnings. Commit `002a16a` pushed to `dev`.

### Key Decisions

- **Counter-op is the only gap in the static scan**: all other CHRs (including objective-deferred spawns) are in `g_StageSetup.props` and already covered by the SA-6 props walk. The "objective command lists" are interleaved in the same props stream — no separate structure.
- **`manifestEnsureLoaded` as safety net, not primary path**: the static scan is the primary mechanism. `bodyAllocateChr` wiring catches edge cases and provides a clear log line when the pre-scan missed something.
- **No MP interference**: the `num_entries == 0` guard means `manifestEnsureLoaded` is fully inert in MP mode where `g_CurrentLoadedManifest` is never populated.

### Next Steps

- SA-2: Modular catalog API layer — typed query functions (bodies, heads, stages, weapons, sounds)
- SP playtest for SA-6/S96: two consecutive missions, verify Joanna stays in `to_keep`, check Counter-Op mode adds anti-player assets to log

---

## Session S95 -- 2026-03-31

**Focus**: SA-7 — Session Catalog migration cleanup / consolidation

### What Was Done

1. **modelcatalog.c decision**: Kept as thin facade. Contains unique VEH/SIGSEGV model validation logic, lazy validation, scale clamping, and thumbnail queue system — none duplicated in assetcatalog.c. Absorbing would be a large risky refactor, not cleanup. Decision: **keep**.

2. **modmgr.c shadow arrays**: Confirmed absent. `g_ModBodies[]`, `g_ModHeads[]`, `g_ModArenas[]` do not exist anywhere in the codebase.

3. **Dead code removed** from modelcatalog.c and modelcatalog.h:
   - `catalogGetEntry(s32 index)` — zero external callers, removed
   - `catalogGetBodyByMpIndex(s32 mpIndex)` — zero external callers, removed
   - `catalogGetHeadByMpIndex(s32 mpIndex)` — zero external callers, removed
   - Kept: `catalogGetSafeBody`, `catalogGetSafeHead`, `catalogGetSafeBodyPaired` (4 call sites each in matchsetup.c + netmsg.c)

4. **Audit: netmanifest.c synthetic IDs**: Clean. `assetCatalogResolve("body_0")` and `assetCatalogResolve("head_0")` go through the catalog — not synthetic bypasses.

5. **Audit: g_HeadsAndBodies[** accesses**: Clean. All accesses confined to: `assetcatalog_base.c`, `assetcatalog_api.c`, `modelcatalog.c`, `server_stubs.c` (extern definition only). `netmsg.c` reference is comment-only.

6. **Audit: netclient.settings.bodynum/headnum**: These fields do not exist. Architecture already uses `body_id`/`head_id` string fields and session hashes.

7. **port/CLAUDE.md updated**: Added "Asset Catalog — Mandatory Rules (SA-7)" section documenting:
   - `catalogWriteAssetRef`/`catalogReadAssetRef` as only permitted net message asset reference functions
   - Catalog-first file resolution principle with permitted accessor list
   - Allowed `g_HeadsAndBodies[]` read sites

### Build Status

711/711 clean (fresh worktree configure + build). Both `PerfectDark.exe` and `PerfectDarkServer.exe` link clean.

### Key Decisions

- **modelcatalog.c stays**: Unique responsibility (platform exception handling for model validation). Not a candidate for absorption into assetcatalog.c without significant risk.
- **Deleted 3 dead functions**: SA-5f had already flagged these with zero-caller audit notes. SA-7 completes the removal.
- **All 6 audits passed**: No violations found. The catalog migration from SA-1 through SA-6 was clean.

### Next Steps

- SA-2: Modular catalog API layer — new typed query functions (bodies, heads, stages, weapons, sounds)
- SP playtest for SA-6: two consecutive missions, verify Joanna stays in `to_keep`

---

## Session S94 -- 2026-03-31

**Focus**: SA-6 — SP load manifest + diff-based asset lifecycle

### What Was Done

1. **`manifest_diff_t` struct** added to `port/include/net/netmanifest.h`:
   - `manifest_diff_entry_t` (id[64] + net_hash + type) as the per-entry type
   - `manifest_diff_t` with fixed `to_load[]`, `to_unload[]`, `to_keep[]` arrays (MANIFEST_MAX_ENTRIES each) + counts

2. **5 new functions** implemented in `port/src/net/netmanifest.c`:
   - `manifestBuildMission(stagenum, out)` — builds manifest for SP stage via `catalogResolveStage()` + Joanna (body_0/head_0); two TODOs for spawn-list characters and prop models
   - `manifestDiff(current, needed, out)` — two-pass O(n²) diff; pass 1 classifies needed entries into to_load/to_keep; pass 2 finds to_unload entries
   - `manifestDiffFree(diff)` — memset (fixed arrays, no heap); named for future MEM-2 swap-in
   - `manifestApplyDiff(needed, diff)` — calls `assetCatalogSetLoadState()` for unloads (→ENABLED) and loads (→LOADED); updates `g_CurrentLoadedManifest`
   - `manifestSPTransition(stagenum)` — convenience wrapper using module-static buffers to keep caller stack small

3. **`g_CurrentLoadedManifest` global** added (tracks current SP load state for diff baseline)

4. **`mainChangeToStage()` wired** in `port/src/pdmain.c`: added `#include "net/netmanifest.h"` + `STAGE_IS_GAMEPLAY()` guard + `manifestSPTransition(stagenum)` call before `g_MainChangeToStageNum` is set

### Build Status

711/711 clean. Both `PerfectDark.exe` and `PerfectDarkServer.exe` link clean. No new warnings from SA-6 code.

### Key Decisions

- **Fixed arrays, not malloc**: `manifest_diff_t` uses MANIFEST_MAX_ENTRIES-sized fixed arrays (~26 KB struct). Module statics in netmanifest.c avoid stack bloat in callers. `manifestDiffFree` is a memset stub — named for easy MEM-2 upgrade.
- **State-tracking only**: `manifestApplyDiff` calls `assetCatalogSetLoadState` to advance through the lifecycle but doesn't move memory yet. That's MEM-2's job. The value of SA-6 is the tracking infrastructure.
- **Joanna hardcoded for now**: `body_0`/`head_0` covers the current SP use case. Full spawn-list enumeration requires pre-loaded setup file data — deferred with TODO comments.
- **Wired in `mainChangeToStage()`**: This is the canonical SP stage transition call, equivalent to `matchStart()` for MP. Only fires for `STAGE_IS_GAMEPLAY()` stages (not title/credits/menus).

### Next Steps

- SP playtest: run two consecutive missions, check log for `MANIFEST-SP:` lines. Joanna should appear in `to_keep` on second mission; mission-unique stage should appear in `to_load`.
- SA-2: Modular catalog API layer (typed result struct functions replacing ad-hoc `catalogResolve()` calls) — this is the next major SA track

---

## Session S93 -- 2026-03-31

**Focus**: SA-5f — deprecation pass, final Phase 5 sub-phase

### What Was Done

1. **Full legacy accessor audit** across `src/` and `port/`:
   - Confirmed all load-path `.filenum` sites are already migrated (SA-5a through SA-5e)
   - Confirmed all `.bgfileid`/`.padsfileid`/`.setupfileid`/`.tilefileid`/`.mpsetupfileid` sites migrated (SA-5b)
   - Confirmed all `g_ModelStates[].fileid` load sites migrated (SA-5c)
   - Found 3 remaining raw `g_HeadsAndBodies[bodynum].filenum` in diagnostic LOG messages in `body.c` — annotated as intentional (`/* SA-5f: raw access for diagnostic log only */`)
   - Found 1 bug: `catalogGetBodyScaleByIndex` silently fell back to `g_HeadsAndBodies[bodynum].scale` on catalog miss — design doc flags this as wrong

2. **Bug fixed** (`port/src/assetcatalog_api.c`): `catalogGetBodyScaleByIndex` error path changed from silent legacy fallback to CATALOG-FATAL pattern (sets `g_CatalogFailure=1`, `g_CatalogFailureMsg`, returns `1.0f`). Now matches the pattern used by `catalogGetBodyFilenumByIndex`, `catalogGetHeadFilenumByIndex`, and `catalogGetStageResultByIndex`.

3. **Deprecated attribute audit** (`port/include/modelcatalog.h`):
   - Added `__attribute__((deprecated(...)))` to `catalogGetEntry`, `catalogGetBodyByMpIndex`, `catalogGetHeadByMpIndex`
   - Built → **zero new warnings** — confirmed these three functions have zero external callers
   - Removed deprecated attributes per SA-5f protocol (job done: compiler-enforced audit confirmed migration complete)
   - Replaced with SA-5f audit note in doc comments

4. **Functions left in place** (still actively used):
   - `catalogGetSafeBody` — internal to `catalogGetSafeBodyPaired` only
   - `catalogGetSafeHead` — 4 active call sites in matchsetup.c + netmsg.c (defense-in-depth guards)
   - `catalogGetSafeBodyPaired` — 4 active call sites; design doc says these are intentional safety nets
   - `g_HeadsAndBodies[bodynum].handfilenum` (bondgun.c:4029) — no catalog API for handfilenum yet; out of SA-5 scope

5. **Remaining legitimate raw array accesses** (not SA-5 scope):
   - `g_HeadsAndBodies[]` registration code in `assetcatalog_base.c` and `modelcatalog.c` — reads array to populate catalog, definitionally correct
   - `g_HeadsAndBodies[bodynum].animscale`, `.unk00_01`, `.canvaryheight`, `.type`, `.ismale`, `.height`, `.modeldef` — runtime behavioral properties, not asset identity; no catalog API for these
   - `bodyreset.c:29-30` — iterates to null modeldef pointers; legitimate
   - `chr.c:4887` — dead code (`PIRACYCHECKS=0` in CMakeLists.txt)

### Build Status

Both targets (client + server) build clean. Only 2 pre-existing `-Wcomment` warnings remain (auto-generated comment blocks in matchsetup.c:11 and modelcatalog.c:24, both pre-existing).

### Key Decision

`catalogGetBodyScaleByIndex` legacy fallback was a design doc-identified bug. Removal makes the function behave identically to the other `catalogGet*ByIndex` functions — CATALOG-FATAL on miss, no silent degradation.

### Next Steps

- SA-5 series COMPLETE (SA-5a through SA-5f all done)
- SA-2: Modular catalog API layer (next major infrastructure track — typed result structs, `catalogResolveBody/Head/Stage` functions replacing ad-hoc `catalogResolve()` calls)
- Playtest C-5/C-6 (texture + anim override mods)

---

## Session S92 -- 2026-03-31

**Focus**: SA-5e — fileLoadToNew / texLoad / animLoad intercept wiring

### What Was Done

1. **C-4 VERIFIED** (`romdataFileLoad`, `port/src/romdata.c:617-646`):
   - Intercept fully wired in `romdataFileLoad()`. Calls `catalogResolveFile(fileNum)` on every load.
   - Handles mod override (load from path), ROM path (catalog_id >= 0), and uncataloged (fall-through).
   - All callers of `fileLoadToNew`/`fileLoadToAddr`/`romdataFileGetData` benefit transparently.

2. **C-5 CONFIRMED** (`texLoad` → `modTextureLoad`, `src/game/texdecompress.c:2255` + `port/src/mod.c:38`):
   - Chain: `texLoad()` sets `g_TexNumToLoad` → calls `modTextureLoad(g_TexNumToLoad, alignedcompbuffer, 4096)`
   - `modTextureLoad()` calls `catalogResolveTexture()` → if mod override: `fsFileLoadTo(path, dst, dstSize)`
   - Single gateway for all 50+ texture call sites. No wiring changes needed.

3. **C-6 IMPLEMENTED** (`animLoadFrame` + `animLoadHeader`, `src/lib/anim.c`):
   - **Gap found**: `modAnimationLoadData()` already called `catalogResolveAnim()`, but only when `g_Anims[animnum].data == 0xffffffff` (pre-marked external animations). ROM-based animations never checked catalog.
   - **Fix**: Added `modAnimationTryCatalogOverride(u16 num)` in `port/src/mod.c` — catalog-only check, no sysFatalError, returns `NULL` when no override so caller falls through to ROM DMA.
   - Wired in both `animLoadFrame` (anim.c:~319) and `animLoadHeader` (anim.c:~378) in the `else` (ROM) branch: checks `g_AnimReplacements[animnum]` cache first, calls `modAnimationTryCatalogOverride` on first miss, uses same offset logic as external-animation path when override found.
   - Override file format: binary blob (header bytes at offset 0, then frame data) — same as `data == 0xffffffff` external animations.

### Build Status

Both targets (client + server) build clean. No new errors or warnings.

### Next Steps

- Playtest C-6: enable a mod that registers an anim override; verify the animation plays from mod file
- C-5 playtest: enable a texture mod; verify override textures appear in-game
- SA-2: Modular catalog API layer (next infrastructure track)

---

## Session S91 -- 2026-03-31

**Focus**: SA-1 — Session Catalog Infrastructure (Phase 1 of session-catalog-and-modular-api.md)

### What Was Done

1. **SA-1 COMPLETE — Session catalog infrastructure** (`port/include/net/sessioncatalog.h`, `port/src/net/sessioncatalog.c`):
   - Implemented full per-match translation layer: manifest entries → u16 session IDs
   - Server-side: `sessionCatalogBuild(manifest)` assigns sequential session IDs 1..n from manifest; `sessionCatalogBroadcast()` sends SVC_SESSION_CATALOG (opcode 0x67) reliable on NETCHAN_CONTROL; `sessionCatalogGetId()`/`sessionCatalogGetIdByHash()` for lookup
   - Client-side: `sessionCatalogReceive(buf)` parses wire, resolves each entry via `assetCatalogResolveByNetHash` + fallback `assetCatalogResolve`; logs `[SESSION-CATALOG-ASSERT]` for unresolved entries (= pipeline bug); `sessionCatalogLocalResolve(session_id)` for O(1) lookup
   - Lifecycle: `sessionCatalogTeardown()` zeros all state at match end; `sessionCatalogIsActive()` / `sessionCatalogLogMapping()` for debug
   - SESSION_CATALOG_MAX = 256; SESSION_ID_NONE = 0 (reserved)

2. **Wiring** (existing files modified):
   - `netmsg.h`: added `SVC_SESSION_CATALOG 0x67` opcode and `netmsgSvcSessionCatalogRead()` declaration
   - `netmsg.c`: added `#include "net/sessioncatalog.h"`; after `manifestBuild()` + `manifestLog()`: calls `sessionCatalogBuild()` + `sessionCatalogLogMapping()`; after SVC_MATCH_MANIFEST broadcast: calls `sessionCatalogBroadcast()`; in `netmsgSvcStageEndRead()`: calls `sessionCatalogTeardown()` (client-side match end); added `netmsgSvcSessionCatalogRead()` implementation delegating to `sessionCatalogReceive()`
   - `net.c`: added `#include "net/sessioncatalog.h"`; added `case SVC_SESSION_CATALOG` to client dispatch switch; added `sessionCatalogTeardown()` in `netServerStageEnd()` (server-side match end)
   - `CMakeLists.txt`: added `port/src/net/sessioncatalog.c` to `SRC_SERVER` explicit list (client auto-discovered via GLOB_RECURSE)

3. **Design adherence**:
   - No `bool`/`stdbool.h` — all booleans are `s32` as required
   - Wire format uses `netbufWriteStr`/`netbufReadStr` consistent with SVC_MATCH_MANIFEST pattern
   - Session IDs are 1-based; 0 = SESSION_ID_NONE (reserved)
   - Client translation table is directly indexed by session_id: O(1) resolve

### Build Status

Both targets (client + server) build clean (S91, tools/build.sh --target both, ~52s).

### Next Steps

- SA-2: Modular catalog API layer — typed result structs + `catalogResolveBody/Head/Stage/Weapon()` functions in assetcatalog.h/c
- Playtest: at match start, verify log output shows "SESSION-CATALOG: built N entries for match" + per-entry mapping table on server, and "SESSION-CATALOG: received N entries" on client
- C-5/C-6: Texture + anim override wiring (still pending)

---

## Session S90 -- 2026-03-31

**Focus**: Playtest bug triage (B-51/B-52/B-53), bot config transmission fix, codebase asset reference audit, session catalog design

### What Was Done

1. **B-51 FIXED — Bot configs not transmitted via SVC_STAGE_START**:
   - Root cause: `SVC_STAGE_START` did not include bot configuration (body/head/team/name) in the wire payload. Clients received no bot config — bots spawned with default/zero geometry, appearing invisible or stuck under the map.
   - Fix: Extended `SVC_STAGE_START` payload to transmit bot config block. Uses catalog net_hash values (not raw N64 body/head indices) on the wire. Clients resolve hash → local index via their catalog.
   - Protocol bumped to **v25**.

2. **B-52 FIXED — `scenarioInitProps` not called on clients**:
   - Root cause: Client-side stage load path (`SVC_STAGE_START` handler) did not call `scenarioInitProps()`. This function initializes all interactive props — weapon pickups, ammo, doors, keys, switches. Without it, all props were non-interactive.
   - Fix: Added `scenarioInitProps()` call to client-side `SVC_STAGE_START` handler.

3. **B-53 FIXED — End match hang / door non-interactive**:
   - Root cause: Same `scenarioInitProps()` omission as B-52. Interactive props (doors, keys) were not initialized on clients.
   - Fix: Covered by the B-52 fix.

4. **Full codebase asset reference audit**:
   - Audited ~180 call sites across ~20 patterns where raw N64 indices cross interface or protocol boundaries (raw `filenum`, `bodynum`, `headnum`, `stagenum`, `texnum`, `animnum` in wire messages, save files, public APIs).
   - Audit findings captured in design doc: `context/designs/session-catalog-and-modular-api.md`.

5. **Session catalog + modular API design doc created** (`context/designs/session-catalog-and-modular-api.md`):
   - Foundational architecture covering: modular catalog API (per-system typed query functions), network session catalog translation layer (catalog IDs ↔ wire net_hash), and load manifest system for both MP and SP.
   - **Key principle established**: The catalog replaces the **entire** legacy loading pipeline, not just networking. All asset references at interface boundaries use catalog IDs — never raw N64 indices.
   - Discussion of mobile mod creation webapp as a future community feature (deferred, no implementation).

6. **Context maintenance**: Constraints updated (v25 protocol, catalog-at-boundaries constraint). Bugs B-51/52/53 moved to Fixed. Session catalog track added as highest infrastructure priority.

### Key Decisions

- **Catalog replaces entire legacy pipeline**: Session catalog is not a networking-only concern. It is the authoritative identity system for all asset references at all interface boundaries.
- **Bot configs on wire use catalog hashes**: Raw N64 body/head indices never travel over the network. Wire format uses catalog net_hash — clients resolve to local index via their own catalog.
- **Session catalog is #1 infrastructure priority**: Match startup pipeline (Phases B–F), mod distribution (Phase D), and anti-cheat all require catalog-based identity at boundaries.

### Protocol

- **NET_PROTOCOL_VER bumped to 25**: SVC_STAGE_START now includes bot config block with catalog hashes.

### Build Status

Both targets build clean.

### Next Steps

- Implement session catalog + modular API (SA-1 through SA-5 — see design doc)
- C-5/C-6: Texture + anim override wiring
- R-2: Room lifecycle (expand hub slots, room_id, leader_client_id)
- Playtest B-51/B-52/B-53 fixes in live networked session to confirm

---

## Session S88 -- 2026-03-30

**Focus**: Match Startup Pipeline Phases D, E, F — completing the full pipeline

### What Was Done

1. **Phase D: Mod Transfer Gate** (`netmsg.c`):
   - In `netmsgClcManifestStatusRead()`, when client reports `NEED_ASSETS`: resolves each missing hash via asset catalog, queues component for chunked delivery via `netDistribServerHandleDiff`
   - No-op when all clients report READY (base game assets always present locally)

2. **Phase E: Ready Gate** (`netmsg.c`, `net.c`):
   - Added `s_ReadyGate` static struct: bitmask-based tracker (`expected_mask`, `ready_mask`, `declined_mask`, `deadline_ticks`, `stagenum`, `total_count`)
   - Three helpers: `readyGatePopcount()`, `readyGateBroadcastCountdown()`, `readyGateCheck()`
   - `netmsgClcLobbyStartRead()`: after manifest broadcast, transitions clients to `CLSTATE_PREPARING`, initializes ready gate with 30s timeout, enters `ROOM_STATE_PREPARING`
   - `netmsgClcManifestStatusRead()`: READY → marks bit + broadcasts countdown + checks gate; NEED_ASSETS → queues transfer; DECLINE → resets client to CLSTATE_LOBBY (spectator)
   - `netServerStageStart()` (net.c): now transitions CLSTATE_PREPARING → CLSTATE_GAME
   - Replaced fire-and-forget launch with proper handshake

3. **Phase F: Sync Launch Countdown** (`netmsg.c`, `netmsg.h`, `net.c`):
   - Extended `s_ReadyGate` with countdown fields (`countdown_active`, `countdown_next_tick`, `countdown_secs`)
   - Added `g_MatchCountdownState` global for client-side UI display
   - `readyGateCheck()`: when all ready, arms 3-second countdown instead of immediate launch
   - `readyGateTickCountdown()`: called in `netEndFrame()`, decrements at 60-tick intervals, broadcasts SVC_MATCH_COUNTDOWN with MANIFEST_PHASE_LOADING, fires stage start at 0
   - `netmsgSvcMatchCountdownRead()`: populates g_MatchCountdownState for room screen UI
   - Countdown sequence: 3→2→1→0 with broadcasts, then mainChangeToStage + netServerStageStart

### Build Status

Both targets build clean: client (31s) + server (9s).

### Next Steps

- Phase C.5 remaining: navmesh spawn for SP maps, unlock gating in selection UI, character picker categories
- UI: room screen reading g_MatchCountdownState to display "Match starting in 3..."
- Playtest: full pipeline end-to-end (manifest → check → ready gate → countdown → launch)

---

## Session S87 -- 2026-03-30

**Focus**: Phase C.5 — Full Game Catalog Registration (SP bodies/heads from g_HeadsAndBodies[152])

### What Was Done

1. **Full game catalog expansion** (`port/src/assetcatalog_base.c`):
   - Added SP body/head registration after the arena loop in `assetCatalogRegisterBaseGame()`
   - Builds a 152-entry boolean coverage mask from `g_MpBodies[0..62]` (bodynum) and `g_MpHeads[0..75]` (headnum)
   - Iterates all 152 `g_HeadsAndBodies[]` entries — skips covered entries, null sentinel (filenum==0), and BODY_TESTCHR (0x70)
   - Uses `unk00_01` to distinguish heads (==1) from bodies (==0)
   - Registers as `base:sp_head_N` / `base:sp_body_N` with category "sp"
   - Populates source_filenum from the entry's filenum for asset resolution
   - ~12 new entries: Eyespy, ChiCroBot, Mini Skedar, President Clone, Skedar King, The King, Grey, Beau variants

2. **Pipeline design doc update** (`context/designs/match-startup-pipeline.md`):
   - Added Phase C.5 "Full Game Catalog Registration" section (§6.5)
   - Added C.5 implementation checklist between C and D
   - Updated §10 "What This Replaces" and §11 "Estimated Effort"

3. **Task list update** (`context/tasks-current.md`):
   - Added Phase C.5 to Match Startup Pipeline table
   - Updated pipeline description from 7-phase to 8-phase

### Decisions Made

- SP characters registered with `requirefeature = 0` (no unlock gating yet) — unlock gating is a UI/selection concern, not a catalog concern.
- Category "sp" distinguishes SP-only models from "base" MP models.
- Stage registration (all mission maps) already in place via `s_BaseStages[]` — no additional work needed.

### Build Status

Both targets build clean: client + server (42s total).

### Next Steps

- Phase D: Transfer Gate — wire netdistrib for missing mod component delivery
- Phase E: Ready Gate — gate stage start on all clients READY
- UI exposure: character picker showing full roster with SP/unlock categories
