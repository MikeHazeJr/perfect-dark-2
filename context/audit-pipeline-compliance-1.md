# Audit: Full Pipeline Catalog ID Compliance
# Independent Deep Audit #1
Date: 2026-04-02

## Executive Summary

Audit scope: all 10 mandatory grep patterns across the full codebase; all 8 audited areas read completely. Protocol version under audit: `NET_PROTOCOL_VER = 27` (header comment: "net_hash removed from wire; all asset identity uses catalog ID strings").

**Result: Protocol v27 claim is not yet true.** Net_hash (CRC32 u32) remains on the wire in at least 4 message types. Two deprecated pre-session helpers are still called in live paths. The catalog registration layer, struct definitions, menu/UI layer, save/load bot data, and match-start write paths are clean.

| Severity | Count | Items |
|----------|-------|-------|
| CRITICAL | 4     | net_hash on wire in SVC_CATALOG_INFO, SVC_SESSION_CATALOG, CLC_CATALOG_DIFF, SVC_DISTRIB_* |
| HIGH     | 3     | catalogWritePreSessionRef/Read still called; assetCatalogResolveByNetHash in 3 live files |
| MEDIUM   | 2     | g_BotProfiles[] raw integer body; manifestBuild reads stagenum not stage_id |
| LOW      | 2     | scenario_save dual integer+string write; savefile integer mpbodynum/mpheadnum fallback |
| CLEAN    | 8     | See section below |

---

## CLEAN — Fully Compliant Areas

### CLC_LOBBY_START Wire Path (netmsg.c)
Write path explicitly ignores the `stagenum` parameter (`(void)stagenum`), reads `g_MatchConfig.stage_id` (char[64]) directly, and sends it as a string. Bot body_id/head_id sent as catalog ID strings. Weapon slots sent as catalog ID strings. No raw integers on wire for asset identity.

Read path receives body_id/head_id as strings, resolves via `assetCatalogResolve()` + `catalogBodynumToMpBodyIdx()`. Fully catalog-native end to end.

### Struct Definitions — matchslot, matchconfig, mpsetup
`port/include/net/matchsetup.h`: `body_id[64]` and `head_id[64]` explicitly labeled `/* PRIMARY */`; `u8 headnum`, `u8 bodynum` explicitly labeled `/* DERIVED: set by matchStart */`. `stage_id[64]` labeled PRIMARY; `u8 stagenum` labeled DERIVED.

`src/include/types.h` (struct mpsetup): `char stage_id[64]; /* PRIMARY: catalog stage identity */` and `u8 stagenum; /* DERIVED from stage_id — set by mpStartMatch */`. Both structs properly annotated.

### Catalog Registration — assetcatalog_base.c / assetcatalog_base_extended.c
All MP bodies (63), MP heads (76, with "head_%d" fallback for unnamed), SP-only bodies/heads (FIX-24 as "base:sp_body_N"/"base:sp_head_N"), 87 stages, arenas in 5 category groups (GEX slots 32-54 intentionally omitted per design), 47 weapons, 1207 animations, NUM_TEXTURES textures, 8 prop categories, 6 game modes, 1545 SFX, 6 HUD elements, 68 lang banks — all registered with human-readable "base:" prefix catalog IDs.

### Menu/UI — Arena and Bot Selection
`pdgui_menu_room.cpp`: arena list built via `assetCatalogIterateByType(ASSET_ARENA, ...)`. Passes `s_Arenas[s_SelectedArena].id` (catalog string) to `netLobbyRequestStartWithSims`. Campaign/Counter-Op resolves stagenum → catalog ID via `catalogResolveStageByStagenum()` before passing. `stage_id` written to `g_MpSetup.stage_id` (PRIMARY field).

`pdgui_menu_matchsetup.cpp`: arena picker uses `s_ArenaId[CATALOG_ID_LEN]` state variable; bot edit sets `sl->body_id` and `sl->head_id` (char[64] PRIMARY fields). No raw integers passed for asset identity.

### Save/Load — Bot Body/Head Data (scenario_save.c, savefile.c)
`scenario_save.c` `scenarioSave()`: bot bodyId/headId written as catalog string IDs directly from `sl->body_id`/`sl->head_id`. Load path prefers `arenaId` catalog string.

`savefile.c` `saveSaveMpPlayer()`: writes `head_id`/`body_id` as catalog strings (SA-4 compliant). Load path prefers catalog strings with labeled integer fallback for backward compatibility.

### SAVE-COMPAT Branches
No `SAVE-COMPAT` strings found anywhere in the codebase. Clean.

### mpInit Default Stage (mplayer.c)
`mpInit()` sets default stage via `assetCatalogResolve("base:mp_skedar")` — catalog-native default.

### Stage Sync at Match Start (mplayer.c)
Lines 305-316: resolves stagenum → catalog ID string, writes to `g_MpSetup.stage_id` (PRIMARY field). Correct direction of sync.

### Debug Display (pdgui_menu_mainmenu.cpp)
Lines 1805, 1886, 1963, 1996: display of `net_hash` as CRC32 hex is diagnostic only (catalog browser tool). Not a functional asset identity use. No finding.

---

## VIOLATIONS — Non-Compliance Found

### [CRITICAL-1] SVC_CATALOG_INFO writes net_hash to wire
**File**: `port/src/net/netmsg.c`, line ~4263
**Code**: `netbufWriteU32(dst, e->net_hash)` — writes CRC32 u32 alongside catalog_id string in SVC_CATALOG_INFO broadcast.
**Impact**: CRC32 is transmitted on wire despite protocol v27 claiming net_hash is removed from wire. Receiving client can correlate by hash, creating a dependency path that bypasses catalog ID strings.
**Fix required**: Remove `net_hash` write. Receiving side must resolve by catalog ID string only.

### [CRITICAL-2] SVC_SESSION_CATALOG writes net_hash to wire
**File**: `port/src/net/sessioncatalog.c`, line 104
**Code**: `netbufWriteU32(&g_NetMsgRel, e->net_hash)` in `sessionCatalogBroadcast()` — writes CRC32 alongside catalog_id string per session catalog entry.
**Impact**: Session catalog (the server-authoritative wire-ID table) sends hash on wire in every match setup. The read side in `sessionCatalogReceive()` calls `assetCatalogResolveByNetHash(net_hash)` as its primary resolution path (string fallback is present but secondary).
**Fix required**: Remove net_hash write in broadcast; update receive to resolve by catalog ID string as primary path.

### [CRITICAL-3] CLC_CATALOG_DIFF sends array of net_hash u32 only
**File**: `port/src/net/netmsg.c`, lines 4303-4332
**Code**: CLC_CATALOG_DIFF message sends an array of u32 net_hashes. No catalog ID strings sent. No string fallback on either side.
**Impact**: Full message type is CRC32-only with no string identity. This is the most complete violation of the v27 claim.
**Fix required**: Replace u32 array with catalog ID string array, or include strings alongside for the protocol bump.

### [CRITICAL-4] SVC_DISTRIB_* uses net_hash as chunk correlation key on wire
**File**: `port/src/net/netmsg.c`; `port/src/net/netdistrib.c` line 382
**Code**: SVC_DISTRIB_BEGIN/CHUNK/END uses `net_hash` u32 as chunk correlation key. `netdistrib.c` line 382: `memcpy(p, &net_hash, 4)` — raw CRC32 written into chunk packet payload.
**Impact**: Asset distribution protocol uses CRC32 as the wire identifier for in-flight chunks. While this is a correlation key rather than pure asset identity, it is still a CRC32 u32 on the wire and depends on CRC32 uniqueness assumptions.
**Severity note**: Functional usage differs from pure identity — but the mechanism still places net_hash on wire in violation of v27 claim.
**Fix required**: Replace with catalog ID string as correlation key, or a session-local sequence number not tied to CRC32.

### [HIGH-1] catalogWritePreSessionRef / catalogReadPreSessionRef still called
**File**: `port/src/net/netmsg.c`, SVC_LOBBY_STATE write (line ~4194) and read (line ~4204)
**Code**:
- Write: `catalogWritePreSessionRef(dst, catalogResolveArenaByStagenum((s32)stagenum))`
- Read: `catalogReadPreSessionRef(src)`
**Implementation** (`port/src/assetcatalog_api.c` line 438/444): these functions encode/decode by net_hash CRC32 u32. They are explicitly marked deprecated.
**Impact**: SVC_LOBBY_STATE still uses CRC32-based encoding for arena identity in the lobby state broadcast. Any client receiving lobby state must do an O(n) hash scan to resolve the arena.
**Fix required**: Replace with direct catalog ID string write/read in SVC_LOBBY_STATE. The arena `id` string is available; write it directly.

### [HIGH-2] assetCatalogResolveByNetHash called in 3 live files
**Files and call sites**:
- `port/src/net/sessioncatalog.c` line 167: `assetCatalogResolveByNetHash(net_hash)` — primary resolution path in `sessionCatalogReceive()`
- `port/src/net/netdistrib.c` line 289: `assetCatalogResolveByNetHash(net_hash)` in `streamComponentToClient()`
- `port/src/net/netdistrib.c` line 598: second call to `assetCatalogResolveByNetHash`
- `port/src/assetcatalog_api.c` line 314: `catalogResolveByNetHash()` wrapper (called by catalogReadPreSessionRef)
**Impact**: O(n) linear scans on every asset resolution by hash. Correctness risk: CRC32 collisions between catalog IDs, though unlikely, are possible. All three files are in live network paths.
**Fix required**: Eliminate once CRITICAL-1/2/3 are fixed (removing hash from wire eliminates the need to resolve by hash). Until then, these are downstream symptoms of the CRITICAL violations.

### [HIGH-3] sessionCatalogReceive resolves by hash as primary, string as fallback
**File**: `port/src/net/sessioncatalog.c`, line 167
**Context**: Because SVC_SESSION_CATALOG puts both net_hash and string on the wire (CRITICAL-2), the receive side uses `assetCatalogResolveByNetHash` as primary and string-based `assetCatalogResolve` as fallback — exactly backwards from the v27 goal.
**Fix required**: Once CRITICAL-2 is fixed (remove hash from wire), invert this to string-primary and remove the hash fallback entirely.

---

## WARNINGS — Works But Could Be Improved

### [MEDIUM-1] g_BotProfiles[] uses raw MPBODY_* integer constants
**File**: `src/game/mplayer/mplayer.c`, lines 2223-2243
**Code**: `g_BotProfiles[]` static table uses `MPBODY_*` integer constants for the `.body` field.
**Downstream**: `mpAddSim()` (line 3489): `g_BotConfigsArray[botnum].base.mpbodynum = g_BotProfiles[profilenum].body` — raw integer assigned to `mpbodynum` without catalog resolution. This means bot characters spawned from profiles don't go through the catalog lookup path.
**Impact**: Bot body assignment bypasses the catalog. If catalog registrations ever change order, bot appearances would silently shift. Not a wire protocol issue (bots don't send identity on wire via this path), but breaks the catalog-native principle for sim players.
**Fix required**: Add `char body_id[64]` to `g_BotProfiles[]`. Populate with catalog ID strings. In `mpAddSim()`, resolve body_id → mpbodynum via `catalogBodynumToMpBodyIdx(assetCatalogResolve(profile->body_id))`.

### [MEDIUM-2] manifestBuild reads stagenum integer instead of stage_id string
**File**: `port/src/net/netmanifest.c`, line ~427
**Code**: `manifestBuild()` reads `g_MpSetup.stagenum` (u8 integer) and calls `catalogResolveStageByStagenum()` rather than reading `g_MpSetup.stage_id` (char[64]) directly.
**Impact**: Indirect path. `g_MpSetup.stage_id` is the PRIMARY field; `stagenum` is DERIVED. Reading the derived field for manifest construction creates an unnecessary indirection and a fragility: if stagenum is stale or incorrectly set, the manifest will reference the wrong stage even when stage_id is correct.
**Fix required**: Replace `catalogResolveStageByStagenum(g_MpSetup.stagenum)` with `assetCatalogResolve(g_MpSetup.stage_id)` in `manifestBuild()`.

### [LOW-1] scenario_save.c writes legacy integer "arena" field alongside string "arenaId"
**File**: `port/src/scenario_save.c`, `scenarioSave()` line 267
**Code**: Writes both `"arena": %u` (integer stagenum) and `"arenaId": "..."` (catalog string) in save version 2.
**Impact**: Load path correctly prefers `arenaId` string (SA-4 compliant). Legacy integer is backward-compat scaffolding. Not a correctness issue.
**Fix required (eventual)**: When save format v3 is introduced, drop the integer `"arena"` field. Document target version in code comment. No action needed now.

### [LOW-2] savefile.c load path has labeled integer mpbodynum/mpheadnum fallback
**File**: `port/src/savefile.c`, `saveLoadMpPlayer()` lines 667-698
**Code**: Labeled "SA-4 v1 fallback: legacy integer field" — falls back to `mpheadnum`/`mpbodynum` integers if `head_id`/`body_id` strings are absent.
**Impact**: Correct and intentional backward compatibility for saves from before catalog IDs were introduced. Load path prefers strings.
**Fix required (eventual)**: Remove fallback when save format v1 is no longer supported. No action needed now.

### [LOW-3] pdgui_bridge.c s_resolveStageIdToStagenum call is vestigial
**File**: `port/fast3d/pdgui_bridge.c`, `netLobbyRequestStartWithSims()` line 644
**Code**: Calls `s_resolveStageIdToStagenum(stage_id)` to convert catalog string → integer, then passes the integer to `netmsgClcLobbyStartWrite()`. Comment: "temporary — will be replaced by full catalog ID string wire format in the next protocol bump."
**Context**: `netmsgClcLobbyStartWrite` already ignores the stagenum parameter (`(void)stagenum`) and reads `g_MatchConfig.stage_id` directly. The integer conversion result is discarded.
**Impact**: Dead work — the conversion is performed and thrown away. Harmless but confusing.
**Fix required**: Remove `s_resolveStageIdToStagenum()` call and the stagenum local. Pass a sentinel (0 or remove param) since it is ignored.

---

## Search Results — Raw Findings

### Pattern: `net_hash`
```
port/include/assetcatalog.h          — struct field: u32 net_hash (CRC32 of id)
port/src/assetcatalog.c              — internal: computed at registration, stored on entry
port/src/assetcatalog_api.c          — catalogWritePreSessionRef encodes by net_hash; catalogResolveByNetHash wrapper
port/src/net/netmsg.c:4263           — VIOLATION: netbufWriteU32(dst, e->net_hash) in SVC_CATALOG_INFO
port/src/net/netmsg.c:4303-4332      — VIOLATION: CLC_CATALOG_DIFF sends u32 net_hash array only
port/src/net/sessioncatalog.c:104    — VIOLATION: netbufWriteU32(&g_NetMsgRel, e->net_hash) in sessionCatalogBroadcast
port/src/net/sessioncatalog.c:167    — assetCatalogResolveByNetHash(net_hash) in sessionCatalogReceive
port/src/net/netdistrib.c:289        — assetCatalogResolveByNetHash(net_hash) in streamComponentToClient
port/src/net/netdistrib.c:382        — memcpy(p, &net_hash, 4) chunk correlation key on wire
port/src/net/netdistrib.c:598        — assetCatalogResolveByNetHash(net_hash) second call
port/src/net/netmanifest.c           — internal deduplication only, not directly on wire as primary identity
port/fast3d/pdgui_menu_mainmenu.cpp  — diagnostic display only, no finding
```

### Pattern: `catalogWritePreSessionRef`
```
port/include/assetcatalog.h          — declared (marked deprecated)
port/src/assetcatalog_api.c:438      — defined: encodes by net_hash CRC32 u32
port/src/net/netmsg.c:~4194          — CALLED in SVC_LOBBY_STATE write path (VIOLATION)
```

### Pattern: `catalogReadPreSessionRef`
```
port/include/assetcatalog.h          — declared (marked deprecated)
port/src/assetcatalog_api.c:444      — defined: reads u32, calls catalogResolveByNetHash
port/src/net/netmsg.c:~4204          — CALLED in SVC_LOBBY_STATE read path (VIOLATION)
```

### Pattern: `assetCatalogResolveByNetHash`
```
port/include/assetcatalog.h          — declared
port/src/assetcatalog.c              — defined (O(n) scan)
port/src/assetcatalog_api.c:314      — catalogResolveByNetHash wrapper
port/src/net/sessioncatalog.c:167    — CALLED (live path)
port/src/net/netdistrib.c:289        — CALLED (live path)
port/src/net/netdistrib.c:598        — CALLED (live path)
```

### Pattern: `SAVE-COMPAT`
```
(no matches anywhere in codebase — CLEAN)
```

### Pattern: `bodynum` / `headnum`
```
port/include/net/matchsetup.h        — u8 bodynum/headnum: DERIVED fields, labeled correctly
src/include/types.h                  — mpbodynum/mpheadnum in mpplayer struct (game runtime)
port/src/net/netmsg.c                — CLC_LOBBY_START write path: (void)bodynum, reads body_id instead — CLEAN
port/src/savefile.c:667-698          — integer fallback load path: labeled "SA-4 v1 fallback"
src/game/mplayer/mplayer.c:3489      — mpAddSim: raw integer assignment from g_BotProfiles[].body — WARNING
```

### Pattern: `mpbodynum` / `mpheadnum`
```
src/include/types.h                  — runtime fields in game structs
port/src/savefile.c                  — labeled integer fallback (backward compat)
src/game/mplayer/mplayer.c           — mpAddSim assigns raw integer — WARNING
port/src/assetcatalog_api.c          — catalogGetBodyFilenumByIndex, catalogGetHeadFilenumByIndex (helpers for load paths — catalog-mediated)
```

### Pattern: `stagenum` as identity (vs. derived cache)
```
port/include/net/matchsetup.h        — matchconfig.stagenum: DERIVED, labeled correctly
src/include/types.h                  — mpsetup.stagenum: DERIVED, labeled correctly
port/src/net/netmanifest.c:~427      — manifestBuild reads g_MpSetup.stagenum instead of g_MpSetup.stage_id — WARNING (reads DERIVED not PRIMARY)
port/fast3d/pdgui_bridge.c:644       — s_resolveStageIdToStagenum() result discarded — vestigial, harmless
src/game/mplayer/mplayer.c:305-316   — stage sync writes to g_MpSetup.stage_id correctly (stagenum→string direction)
```

### Pattern: `filenum`
```
port/src/assetcatalog_api.c          — catalogGetBodyFilenumByIndex, catalogGetHeadFilenumByIndex: catalog-mediated helpers
port/src/preprocess/filemodel.c      — internal file loading by filenum (ROM data layer)
No direct filenum-as-asset-identity usages found in network or save paths.
```

---

## Summary of Required Fixes (Priority Order)

1. **[CRITICAL-1]** Remove `netbufWriteU32(dst, e->net_hash)` from SVC_CATALOG_INFO in `netmsg.c`. Receiving side must not use hash.
2. **[CRITICAL-2]** Remove `netbufWriteU32(&g_NetMsgRel, e->net_hash)` from `sessionCatalogBroadcast()`. Invert receive-side resolution to string-primary.
3. **[CRITICAL-3]** Redesign CLC_CATALOG_DIFF to use catalog ID strings instead of u32 hash array.
4. **[CRITICAL-4]** Replace net_hash chunk correlation key in SVC_DISTRIB_* with a session-local sequence number or catalog ID string.
5. **[HIGH-1]** Replace `catalogWritePreSessionRef`/`catalogReadPreSessionRef` in SVC_LOBBY_STATE with direct catalog ID string write/read.
6. **[HIGH-2]** Once CRITICAL fixes land, `assetCatalogResolveByNetHash` call sites will have no remaining inputs. Remove them.
7. **[MEDIUM-1]** Migrate `g_BotProfiles[]` to string body_id; update `mpAddSim()` to resolve through catalog.
8. **[MEDIUM-2]** Replace `catalogResolveStageByStagenum(g_MpSetup.stagenum)` with `assetCatalogResolve(g_MpSetup.stage_id)` in `manifestBuild()`.
9. **[LOW-3]** Remove vestigial `s_resolveStageIdToStagenum()` call in `pdgui_bridge.c`.
10. **[LOW-1/2]** Defer to save format v3 (drop legacy integer fields when backward compat window closes).
