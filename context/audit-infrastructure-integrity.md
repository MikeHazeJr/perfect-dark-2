# Infrastructure Integrity Audit

**Date**: 2026-04-02
**Auditor**: Claude (Sonnet 4.6) — independent deep audit, read-only
**Scope**: Wire protocol, persistence, runtime data flow, menu system, deprecated API surface, constraint consistency
**Protocol version in code**: `NET_PROTOCOL_VER 27` (net.h:12)

---

## Executive Summary

The codebase is in a **partially migrated** state. Significant progress has been made since the prior catalog-ID compliance audit (also 2026-04-02), but a structural problem exists: **the header file (`netmsg.h`) was updated to declare v27-compliant signatures (catalog ID strings) but the implementations in `netmsg.c` and `netdistrib.c` still use the old v26 wire format (u32 net_hash)**. This is a build-breaking signature mismatch for at least five functions. Beyond that, several net_hash usages on the live wire have not been replaced.

---

## Dimension A — Wire Protocol Integrity

### A-1. FAIL — `netmsgClcCatalogDiffWrite`: header/implementation signature mismatch

**File**: `port/include/net/netmsg.h:172` vs `port/src/net/netmsg.c:4303`

Header declares:
```c
u32 netmsgClcCatalogDiffWrite(struct netbuf *dst, const char (*missing_ids)[CATALOG_ID_LEN], u16 count, u8 temporary);
```
Implementation defines:
```c
u32 netmsgClcCatalogDiffWrite(struct netbuf *dst, const u32 *missing_hashes, u16 count, u8 temporary)
```
And writes `netbufWriteU32(dst, missing_hashes[i])` — sending u32 CRC32 hashes, not catalog ID strings.

The caller in `netdistrib.c:627` passes `u32 missing[]` — consistent with the old signature, not the new header.

**Verdict**: BUILD-BREAKING signature mismatch. Header says v27 (strings), implementation is v26 (hashes). Wire format does NOT match the v27 declaration.

---

### A-2. FAIL — `netmsgClcManifestStatusWrite`: header/implementation signature mismatch

**File**: `port/include/net/netmsg.h:199–200` vs `port/src/net/netmsg.c:4524–4525`

Header declares:
```c
u32 netmsgClcManifestStatusWrite(struct netbuf *dst, u32 manifest_hash, u8 status,
                                  const char (*missing_ids)[CATALOG_ID_LEN], u8 num_missing);
```
Implementation defines:
```c
u32 netmsgClcManifestStatusWrite(struct netbuf *dst, u32 manifest_hash, u8 status,
                                  const u32 *missing_hashes, u8 num_missing)
```
And writes `netbufWriteU32(dst, missing_hashes[i])` — u32 net_hash values.

**Verdict**: BUILD-BREAKING signature mismatch. Wire format is v26 (hashes); header claims v27 (strings).

---

### A-3. FAIL — `netmsgSvcDistribBeginWrite/ChunkWrite/EndWrite`: header/implementation signature mismatch

**File**: `port/include/net/netmsg.h:177–183` vs `port/src/net/netmsg.c:4337–4431`

Header declares:
```c
u32 netmsgSvcDistribBeginWrite(struct netbuf *dst, const char *catalog_id, const char *category, u32 total_chunks, u32 archive_bytes);
u32 netmsgSvcDistribChunkWrite(struct netbuf *dst, const char *catalog_id, u16 chunk_idx, u8 compression, const u8 *data, u16 data_len);
u32 netmsgSvcDistribEndWrite(struct netbuf *dst, const char *catalog_id, u8 success);
```
Implementations define (at lines 4337, 4375, 4413):
```c
u32 netmsgSvcDistribBeginWrite(struct netbuf *dst, u32 net_hash, const char *id, ...);
u32 netmsgSvcDistribChunkWrite(struct netbuf *dst, u32 net_hash, u16 chunk_idx, ...);
u32 netmsgSvcDistribEndWrite(struct netbuf *dst, u32 net_hash, u8 success);
```
All three write `netbufWriteU32(dst, net_hash)` as the first payload field.

Callers in `netdistrib.c` pass `net_hash` values, consistent with old signatures.

**Verdict**: BUILD-BREAKING signature mismatch for three functions (BeginWrite, ChunkWrite, EndWrite). The entire SVC_DISTRIB_* wire protocol is still u32 net_hash, contrary to the v27 declaration in the header.

---

### A-4. FAIL — `SVC_CATALOG_INFO` still writes `e->net_hash` on wire

**File**: `port/src/net/netmsg.c:4263`

Comment at `port/include/net/netmsg.h:54` says: `SVC_CATALOG_INFO` is "v27: no net_hash". But the implementation at line 4263:
```c
netbufWriteU32(dst, e->net_hash);
netbufWriteStr(dst, e->id);
netbufWriteStr(dst, e->category);
```
Net_hash is still emitted as the first field in each catalog entry. The read path at line 4285 reads it back as `hashes[i] = netbufReadU32(src)` and passes it to `netDistribClientHandleCatalogInfo` which uses it to diff against local catalog via `assetCatalogResolveByNetHash(hashes[i])`.

**Verdict**: FAIL — SVC_CATALOG_INFO wire format does not match v27 mandate. Net_hash is still the primary identity field on wire.

---

### A-5. FAIL — `SVC_LOBBY_STATE` encodes arena as net_hash (unchanged from prior audit)

**File**: `port/src/net/netmsg.c:4188–4219`

```c
u32 netmsgSvcLobbyStateWrite(struct netbuf *dst, u8 gamemode, u8 stagenum, u8 status)
{
    ...
    catalogWritePreSessionRef(dst, catalogResolveArenaByStagenum((s32)stagenum));
    ...
}
u32 netmsgSvcLobbyStateRead(...) {
    const asset_entry_t *stage_entry = catalogReadPreSessionRef(src);
    ...
}
```

Comment in the write path (line 4192): "encode arena as catalog wire format (u32 net_hash)". This is explicitly net_hash on the wire. The comment also references "B-72" as the rationale, but per the v27 mandate net_hash is deprecated.

**Verdict**: FAIL — SVC_LOBBY_STATE still uses `catalogWritePreSessionRef`/`catalogReadPreSessionRef` (u32 CRC32 net_hash) for the arena field.

---

### A-6. FAIL — `SVC_SESSION_CATALOG` writes `e->net_hash` alongside catalog ID string

**File**: `port/src/net/sessioncatalog.c:104`

```c
netbufWriteU32(&g_NetMsgRel, e->net_hash);
netbufWriteStr(&g_NetMsgRel, e->catalog_id);
```

Read path (line 148): `net_hash = netbufReadU32(src)` then `assetCatalogResolveByNetHash(net_hash)` is the primary resolution, with string as fallback (line 167–169).

**Verdict**: FAIL — SVC_SESSION_CATALOG wire format includes net_hash as a primary resolution field. String is present as a "fallback" but hash is the primary key. Per v27, net_hash must not cross the wire.

---

### A-7. FAIL — `manifestSerialize`/`manifestDeserialize` include `net_hash` in SVC_MATCH_MANIFEST

**File**: `port/src/net/netmanifest.c:698–749`

```c
// manifestSerialize (line 704):
netbufWriteU32(dst, e->net_hash);
// manifestDeserialize (line 732):
const u32 net_hash = netbufReadU32(src);
manifestAddEntry(out, net_hash, id, type, slot_index);
```

The manifest fingerprint (`manifest_hash`) is computed at lines 340–344 by feeding `e->net_hash` bytes through FNV-1a — so the manifest integrity check is still dependent on CRC32 net_hash values.

**Verdict**: FAIL — SVC_MATCH_MANIFEST wire payload carries u32 net_hash per entry. The manifest fingerprint is built from net_hash bytes.

---

### A-8. PASS — `CLC_LOBBY_START` write path: arena uses catalog ID string

**File**: `port/src/net/netmsg.c:3574–3649`

The write path ignores the `u8 stagenum` parameter (line 3576: `(void)stagenum`) and instead writes `g_MatchConfig.stage_id` as a string (line 3582). Per-bot `body_id`/`head_id` are written as strings (lines 3621–3622). Per-slot weapons are written as strings via `catalogResolveWeaponByGameId()` (line 3601–3603).

**Verdict**: PASS — CLC_LOBBY_START write is catalog-ID-string-native as of v27.

---

### A-9. PASS — `CLC_LOBBY_START` read path: arena and weapons use catalog ID strings

**File**: `port/src/net/netmsg.c:3802–3994`

Arena resolved via `assetCatalogResolve(stage_id)` on the string read from wire (lines 3809–3817). Weapons resolved by string (lines 3834–3849). Bot configs read via `netbufReadStr(src)` for `body_id`/`head_id`, resolved via `assetCatalogResolve()` (lines 3945–3975).

Stale comment at line 3916 says "resolved from arena net_hash in FIX-2" — this is a **stale comment**; the actual code is correct (catalog ID strings), but the comment is misleading.

**Verdict**: PASS (with stale comment WARN).

---

### A-10. PASS — `SVC_STAGE_START` uses session IDs (u16) not net_hash

**File**: `port/src/net/netmsg.c:678–693`

Stage is encoded via `catalogWriteAssetRef(dst, sessionCatalogGetId(stage_canon))` — a u16 session wire ID. Weapons similarly use `catalogWriteAssetRef`. Per-player/bot body/head use `catalogWriteAssetRef` with session IDs. The session catalog itself maps string ID → wire_id, so there is no raw net_hash on the SVC_STAGE_START wire.

**Verdict**: PASS — SVC_STAGE_START uses session wire IDs (u16), which are resolved from catalog ID strings via the session catalog. Compliant.

---

### A-11. WARN — `CLC_SETTINGS` uses session IDs for body/head — correct but dependent on session catalog

**File**: `port/src/net/netmsg.c:499–563`

Body/head written as session IDs (u16) via `catalogWriteAssetRef`. Read back and resolved via session catalog translation. This is correct, but it means that if the session catalog hasn't been broadcast yet when CLC_SETTINGS arrives, resolution will fail (session_id → NULL).

**Verdict**: WARN — session ID dependency in CLC_SETTINGS. Functional but order-sensitive.

---

### A-12. WARN — Stale comment in CLC_LOBBY_START server read path

**File**: `port/src/net/netmsg.c:3916`

```c
g_MpSetup.stagenum = stagenum; /* resolved from arena net_hash in FIX-2 */
```

The comment references net_hash but the actual `stagenum` was resolved via `assetCatalogResolve(stage_id_str)` earlier in the same function (line 3811–3816). The comment is stale.

**Verdict**: WARN — stale comment only; no behavioral impact.

---

## Dimension B — Persistence Integrity

### B-1. FAIL — `scenario_save.c:267` still writes raw `"arena"` integer

**File**: `port/src/scenario_save.c:267`

```c
fprintf(fp, "  \"arena\": %u,\n", (unsigned)g_MatchConfig.stagenum);
```

The raw integer `stagenum` is written alongside `"arenaId"` string. On load (line 387–408), `"arena"` integer is read and used as direct fallback if `"arenaId"` string fails to resolve OR if `"arenaId"` is absent. This means save files from before the catalog-ID migration will load integer arena identity from disk with no validation.

Additionally, at line 271, `catalogResolveStageByStagenum()` is used to get the string — but there is no corresponding write of `g_MatchConfig.stage_id` (the primary field). If `g_MatchConfig.stage_id` was set correctly by the UI, it is not written to the save file. Instead, a secondary derivation via stagenum is used.

**Verdict**: FAIL — dual-write pattern persists; raw integer written to disk; load path uses integer as direct fallback.

---

### B-2. FAIL — `scenario_save.c` load path sets `g_MatchConfig.stagenum` directly without updating `stage_id`

**File**: `port/src/scenario_save.c:402–408`

```c
if (arena_id[0]) {
    s32 idx = assetCatalogResolveStageIndex(arena_id);
    if (idx >= 0) g_MatchConfig.stagenum = (u8)idx;
    else if (arena >= 0) g_MatchConfig.stagenum = (u8)arena;
} else if (arena >= 0) {
    g_MatchConfig.stagenum = (u8)arena;
}
```

`g_MatchConfig.stage_id` (the PRIMARY field per `matchsetup.h` comments) is never set here. After a scenario load, `g_MatchConfig.stage_id` is empty/default while `g_MatchConfig.stagenum` is set. If CLC_LOBBY_START is sent after scenario load, line 3582 will write `g_MatchConfig.stage_id` which is the default (empty string), NOT the loaded scenario's arena.

**Verdict**: FAIL — scenario load does not populate `g_MatchConfig.stage_id`, creating a split-brain between stagenum and stage_id after load.

---

### B-3. FAIL — `savefile.c:790–793` still writes raw `"weapons"` integer array

**File**: `port/src/savefile.c:790–793`

```c
fprintf(fp, "  \"weapons\": [");
for (s32 i = 0; i < NUM_MPWEAPONSLOTS; i++) {
    fprintf(fp, "%u%s", g_MpSetup.weapons[i], i < NUM_MPWEAPONSLOTS - 1 ? ", " : "");
}
```

Raw `MPWEAPON_*` integers written alongside `"weapon_ids"` strings. Load path at line 860–870 reads back raw integers as direct fallback. No validation.

**Verdict**: FAIL — dual-write pattern; raw weapon integers written/read from disk.

---

### B-4. FAIL — `savefile.c:832–834` legacy `"stagenum"` integer fallback on load

**File**: `port/src/savefile.c:832–834`

```c
} else if (strcmp(key, "stagenum") == 0) {
    /* SA-4 v1 fallback */
    tok = s_next(&p); g_MpSetup.stagenum = s_tok_int(&tok);
}
```

Raw integer set directly into `g_MpSetup.stagenum` with no catalog lookup. If the integer mapping has changed (mod added/removed), this silently loads the wrong stage.

**Verdict**: FAIL — raw integer fallback from disk directly into runtime field.

---

### B-5. FAIL — `savefile.c:693–698` legacy `"mpheadnum"`/`"mpbodynum"` integer fallback on load

**File**: `port/src/savefile.c:693–698`

```c
} else if (strcmp(key, "mpheadnum") == 0) {
    tok = s_next(&p); pc->base.mpheadnum = s_tok_int(&tok);
} else if (strcmp(key, "mpbodynum") == 0) {
    tok = s_next(&p); pc->base.mpbodynum = s_tok_int(&tok);
```

Raw integer set directly into player config without catalog lookup.

**Verdict**: FAIL — raw integer fallback from disk.

---

### B-6. PASS — `savefile.c` player config write path uses catalog ID strings

**File**: `port/src/savefile.c:604–608`

```c
const char *head_id = catalogResolveHeadByMpIndex((s32)pc->base.mpheadnum);
const char *body_id = catalogResolveBodyByMpIndex((s32)pc->base.mpbodynum);
writeJsonString(fp, "head_id", head_id ? head_id : "");
writeJsonString(fp, "body_id", body_id ? body_id : "");
```

String IDs are written as the primary fields.

**Verdict**: PASS — write path is string-primary.

---

### B-7. PASS — `savefile.c` MP setup write path uses catalog ID string for stage

**File**: `port/src/savefile.c:772–773`

```c
const char *stage_id = catalogResolveStageByStagenum((s32)g_MpSetup.stagenum);
writeJsonString(fp, "stage_id", stage_id ? stage_id : "");
```

**Verdict**: PASS — stage written as catalog ID string.

---

### B-8. PASS — `savefile.c` weapon write path uses catalog ID strings as primary

**File**: `port/src/savefile.c:784–788`

`"weapon_ids"` array written as catalog strings. Legacy `"weapons"` integer array also written (B-3 violation), but the primary format is correct.

**Verdict**: PASS for the write (string-primary), FAIL for dual-write (see B-3).

---

## Dimension C — Runtime Data Flow

### C-1. PASS — `struct matchconfig.stage_id` exists as PRIMARY field

**File**: `port/include/net/matchsetup.h:59–61`

```c
char stage_id[64];  /* PRIMARY: catalog ID — e.g. "base:mp_complex" */
u8 stagenum;        /* DERIVED: resolved from stage_id at matchStart */
```

The struct correctly has both fields with documented semantics.

**Verdict**: PASS — struct definition is correct.

---

### C-2. PASS — `matchStart()` resolves stagenum from stage_id

**File**: `port/src/net/matchsetup.c:255–270`

`g_MatchConfig.stage_id` is resolved to stagenum via `assetCatalogResolve()` at matchStart time. Integer is DERIVED at the last moment.

**Verdict**: PASS.

---

### C-3. FAIL — `matchConfigAddBot()` derives `slot->bodynum`/`slot->headnum` immediately

**File**: `port/src/net/matchsetup.c:181–194`

```c
slot->bodynum = 0;
slot->headnum = 0;
if (body_id && body_id[0]) {
    const asset_entry_t *be = assetCatalogResolve(body_id);
    s32 mpb = be ? catalogBodynumToMpBodyIdx((s32)be->runtime_index) : -1;
    if (mpb >= 0) slot->bodynum = (u8)mpb;
}
```

The `matchslot.bodynum`/`headnum` are set during `matchConfigAddBot()`, not deferred to `matchStart()`. Per `matchsetup.h:43–49` comments, bodynum/headnum are DERIVED and should be set by `matchStart()` only. Setting them earlier is premature and breaks the invariant that body_id is the sole primary.

This is a WARN rather than FAIL because the derivation happens correctly (catalog lookup → mpbodynum), but it happens early. If the catalog changes between bot-add and match-start (e.g., mod reloaded), the cached integer would be stale.

**Verdict**: WARN — bodynum/headnum derived at bot-add time, not deferred to matchStart.

---

### C-4. PASS — CLC_LOBBY_START client-send path reads from `g_MatchConfig.stage_id`

**File**: `port/src/net/netmsg.c:3582`

```c
netbufWriteStr(dst, g_MatchConfig.stage_id[0] ? g_MatchConfig.stage_id : "");
```

Stage identity on the wire comes from the PRIMARY string field.

**Verdict**: PASS.

---

### C-5. FAIL — Server CLC_LOBBY_START read sets `g_MatchConfig.stagenum` but not `g_MatchConfig.stage_id` via `g_MpSetup`

**File**: `port/src/net/netmsg.c:3816–3818` and `3916`

Line 3817 sets `g_MatchConfig.stage_id` from the wire string. Line 3816 derives `g_MpSetup.stagenum` from the catalog resolution. Line 3916 sets `g_MpSetup.stagenum = stagenum`. However, `g_MpSetup.stage_id` (the primary field added as H-4 fix) is never updated from the wire. After CLC_LOBBY_START is processed on the server, `g_MpSetup.stage_id` may be empty/stale while `g_MpSetup.stagenum` is correctly set.

**Verdict**: WARN — `g_MpSetup.stage_id` not updated during server CLC_LOBBY_START processing. The stagenum is correct, but the primary string field is diverged.

---

### C-6. PASS — `netlobby.c` lobbyUpdate uses catalog resolution for body/head

**File**: `port/src/net/netlobby.c:69–72`

```c
const asset_entry_t *be = assetCatalogResolve(cl->settings.body_id);
const asset_entry_t *he = assetCatalogResolve(cl->settings.head_id);
lp->bodynum = be ? (u8)be->runtime_index : 0;
lp->headnum = he ? (u8)he->runtime_index : 0;
```

String → runtime index conversion at the display boundary. No direct integer identity used.

**Verdict**: PASS.

---

### C-7. PASS — SVC_STAGE_START uses session catalog (wire_id) for stage and weapons

**File**: `port/src/net/netmsg.c:678–722`

Stage and weapons encoded as u16 session wire IDs via `catalogWriteAssetRef`. The session catalog maps catalog_id → wire_id. No raw stagenum or net_hash on wire.

**Verdict**: PASS.

---

### C-8. WARN — stagenum passed as u8 through much of the match start pipeline even after CLC_LOBBY_START

**File**: `port/src/net/netmsg.c:3916`, `4113`, `4124–4127`; `port/src/net/net.c:687–753`

After CLC_LOBBY_START resolves stage_id → stagenum on the server, stagenum (u8) is stored in `s_ReadyGate.stagenum` (line 74 of netmsg.c) and then passed to `mainChangeToStage(stagenum)` at lines 4113 and 4127. `netServerCoopStageStart()` accepts `u8 stagenum` and calls `titleSetNextStage(stagenum)` and `mainChangeToStage(stagenum)`. This is the correct pattern — stagenum is the engine API boundary. But the pipeline is tracked only as integer from this point onward.

**Verdict**: WARN — stagenum (integer) is the working currency from ready-gate fire to stage load. Catalog ID string is not propagated through `s_ReadyGate` or `netServerCoopStageStart`. Acceptable at the engine boundary but note that if stage load fails, there is no string to report.

---

## Dimension D — Menu System Integrity

### D-1. WARN — `menuPush`/`menuPop` called from ImGui layer for state registration

**Files**:
- `port/fast3d/pdgui_menu_pausemenu.cpp:214, 239` — `menuPush(MENU_PAUSE)` / `menuPop()`
- `port/fast3d/pdgui_menu_mainmenu.cpp:2239` — `menuPush(MENU_JOIN)`
- `port/fast3d/pdgui_menu_mainmenu.cpp:2201, 2204` — `menuPop()`

These calls use the legacy `menuPush`/`menuPop` API. Per the active constraint ("ImGui is the sole menu system"), these should be replaced with the ImGui menu stack mechanism. However, examination of context shows these calls are currently used for:
1. `MENU_PAUSE` — cooldown / state registration with the menu manager so the game knows a pause is active
2. `MENU_JOIN` — registering a network join in-progress state

These are NOT the full N64 menu stack flow (no dialog defs, no legacy menu rendering). They are using the menumgr state enum for a coarser-grained purpose. Still, per the strict constraint, all `menuPush`/`menuPop` calls must eventually be replaced.

**Verdict**: WARN — five `menuPush`/`menuPop` calls remain in the ImGui layer (`pausemenu.cpp:214,239`, `mainmenu.cpp:2201,2204,2239`). No calls remain in `src/game/` code at the bare `menuPush`/`menuPop` (non-dialog) level (confirmed by grep). The calls in `src/game/` are all `menuPushDialog`/`menuPopDialog`/`menuPushRootDialog` which are the N64 dialog system — these remain active in game code (`activemenu.c`, `endscreen.c`, etc.), which is the larger legacy menu surface.

---

### D-2. FAIL — `menuPushDialog`/`menuPopDialog` called from active game code paths

**Files** (partial list):
- `src/game/activemenu.c:72` — `menuPushRootDialog(...)`
- `src/game/endscreen.c:37, 38, 64, 65, 654, 656, 663, 664, 711, 713, 718, 723, 724, 726, 732, 733, 735, 741, 743, 778, 799, 802, 842, 845, 1519, 1521, 1767` — extensive use
- `src/game/camdraw.c:1358` — `menuPopDialog()`

The N64 dialog menu system (`menuPushDialog`/`menuPopDialog`) is deeply embedded in active game code in `src/game/`. These are not the `menuPush(enum)` form but the dialog-def form. The active constraint says "the legacy N64 `menuPush`/`menuPop` stack in `menumgr.c` is deprecated and will be stripped entirely."

**Verdict**: FAIL — The legacy dialog menu system is still actively called from `src/game/` code. This is a known architectural debt item, not a new discovery. At least 30 call sites in `endscreen.c` alone use the dialog form. Stripping this is a large task per the "will be stripped entirely" mandate.

---

### D-3. PASS — No ImGui menus call the N64 menu stack except as noted in D-1

The ImGui menu files (`pdgui_menu_*.cpp`) forward-declare `menuPushDialog`/`menuPopDialog` but use them only in:
- `pdgui_menu_agentcreate.cpp`: `menuPopDialog()` (for name entry dialog integration)
- `pdgui_menu_agentselect.cpp`: `menuPushDialog(&g_FilemgrEnterNameMenuDialog)`
- `pdgui_menu_challenges.cpp`: `menuPopDialog()`
- `pdgui_menu_mainmenu.cpp`: `menuPopDialog()`, `menuPushDialog(&g_ChangeAgentMenuDialog)`, `menuPushDialog(&g_NetJoiningDialog)`
- `pdgui_menu_matchsetup.cpp`: `menuPopDialog()`
- `pdgui_menu_mpsettings.cpp`: `menuPopDialog()`
- `pdgui_menu_network.cpp`: `menuPushDialog(&g_NetJoiningDialog)`, `menuPopDialog()`
- `pdgui_menu_solomission.cpp`: forward declarations only

These represent **bridge calls** where ImGui menus invoke legacy dialogs (name entry, agent selection, network joining dialogs). They indicate incomplete migration to ImGui for these flows.

**Verdict**: WARN — ImGui menus call legacy dialog system in 8 places across 7 files. These are known migration targets.

---

## Dimension E — Deprecated API Surface

### E-1. FAIL — `catalogWritePreSessionRef`/`catalogReadPreSessionRef` still called

**Files**:
- `port/src/net/netmsg.c:4194` — `catalogWritePreSessionRef` in SVC_LOBBY_STATE write
- `port/src/net/netmsg.c:4204` — `catalogReadPreSessionRef` in SVC_LOBBY_STATE read

The `catalogWritePreSessionRef`/`catalogReadPreSessionRef` functions write/read u32 net_hash values. Per the v27 mandate, these functions are deprecated. They remain called in the SVC_LOBBY_STATE path.

**Verdict**: FAIL — two live calls to deprecated net_hash wire functions.

---

### E-2. FAIL — `assetCatalogResolveByNetHash` still called in live code paths

**Files**:
- `port/src/net/netdistrib.c:289, 598` — primary resolution in `streamComponentToClient` and `netDistribClientHandleCatalogInfo`
- `port/src/net/sessioncatalog.c:167` — primary resolution in `sessionCatalogReceive`
- `port/src/assetcatalog_api.c:316` — implementation (permitted as catalog internal)

The live paths in `netdistrib.c` and `sessioncatalog.c` use `assetCatalogResolveByNetHash` as the primary asset lookup — because the wire still sends net_hash values (see A-1 through A-7).

**Verdict**: FAIL — `assetCatalogResolveByNetHash` used as primary resolution in live network code. This is a consequence of the wire not being migrated (A-1–A-7).

---

### E-3. PASS — `netLobbyRequestStartWithSims`/`netLobbyRequestStart` API accepts `const char *stage_id`

**File**: `port/fast3d/pdgui_bridge.c` (per prior audit, S128 fix)

Per the catalog compliance audit (H-2 resolved), these functions now accept catalog ID strings.

**Verdict**: PASS (not re-verified by code read; taken from prior audit status).

---

### E-4. WARN — `matchConfigAddBot` signature accepts `const char *body_id`/`const char *head_id` but derives integers immediately

See C-3. The public API is string-based (correct), but the implementation eagerly derives integers.

**Verdict**: WARN.

---

## Dimension F — Constraint and Documentation Consistency

### F-1. WARN — `constraints.md` protocol version says v25, code says v27

**File**: `context/constraints.md:15`

Constraints.md states: `Currently v25 (... v25: SVC_STAGE_START bot config block using catalog hashes, S90). Next bump (v26): will replace all remaining net_hash u32 wire fields...`

**Actual code** (`port/include/net/net.h:12`): `#define NET_PROTOCOL_VER 27`

The constraints document is three versions behind. The bump to v27 (which claims "net_hash removed from wire; all asset identity uses catalog ID strings") has already been committed in the header, but the implementation has not caught up (see A-1 through A-7).

**Verdict**: WARN — constraints.md is stale on protocol version. The v27 declaration in the code header is aspirational/premature; the wire format is still partially v26.

---

### F-2. WARN — `NET_PROTOCOL_VER 27` header comment overstates v27 compliance

**File**: `port/include/net/net.h:12–15`

The comment claims: `v27: net_hash removed from wire; all asset identity uses catalog ID strings. CLC_LOBBY_START stage/weapons, SVC_LOBBY_STATE arena, SVC_MATCH_MANIFEST entries, CLC_MANIFEST_STATUS missing list, CLC_CATALOG_DIFF, SVC_DISTRIB_BEGIN/CHUNK/END, SVC_SESSION_CATALOG — all now transmit catalog ID strings, never u32 CRC32 hashes.`

In reality:
- CLC_LOBBY_START: **DONE** (strings)
- SVC_LOBBY_STATE: **NOT DONE** (still net_hash)
- SVC_MATCH_MANIFEST entries: **NOT DONE** (still net_hash in manifestSerialize)
- CLC_MANIFEST_STATUS missing list: **NOT DONE** (still u32 hashes; signature mismatch)
- CLC_CATALOG_DIFF: **NOT DONE** (still u32 hashes; signature mismatch)
- SVC_DISTRIB_BEGIN/CHUNK/END: **NOT DONE** (still u32 net_hash; signature mismatch)
- SVC_SESSION_CATALOG: **NOT DONE** (net_hash still on wire alongside string)

The header comment declares v27 complete, but ~6 of the 7 listed messages are still using net_hash.

**Verdict**: FAIL — `NET_PROTOCOL_VER 27` in the header was declared prematurely. The wire format is not yet v27-compliant in most of the listed messages. This creates an interoperability hazard: two clients both claiming v27 will use incompatible wire formats.

---

### F-3. FAIL — `netmsg.h` declares v27 signatures that don't match implementations

See A-1 through A-3 (the three sets of signature mismatches). The header and implementation are out of sync. This will cause compilation errors in any translation unit that calls the updated signatures with the expected types.

**Verdict**: FAIL — header/implementation signature mismatch is a build-breaking defect.

---

### F-4. PASS — `matchsetup.h` struct design is correct

`struct matchconfig` has `stage_id` as PRIMARY with `stagenum` as DERIVED. `struct matchslot` has `body_id`/`head_id` as PRIMARY with `bodynum`/`headnum` as DERIVED. Both are properly documented.

**Verdict**: PASS.

---

### F-5. PASS — `catalog-id-compliance.md` findings H-1 through H-4 and M-2 through M-5 shown as resolved (S128)

The prior audit marked these as resolved. This audit verifies:
- H-1 (stage_id in matchconfig): confirmed PASS (C-1 above)
- H-2 (netLobbyRequestStart API): taken as PASS per prior audit
- H-3/H-4 (mplayer.c defaults): not re-audited in detail
- M-2 (matchsetup UI): not re-audited; taken as PASS per prior audit

**Verdict**: PASS (inherited from prior audit; not re-verified in detail).

---

## Summary Table

| ID | Finding | Severity | File(s) |
|----|---------|----------|---------|
| A-1 | `netmsgClcCatalogDiffWrite` header/impl signature mismatch | **BUILD-BREAKING** | netmsg.h:172, netmsg.c:4303 |
| A-2 | `netmsgClcManifestStatusWrite` header/impl signature mismatch | **BUILD-BREAKING** | netmsg.h:199, netmsg.c:4524 |
| A-3 | `netmsgSvcDistribBeginWrite/ChunkWrite/EndWrite` header/impl mismatch | **BUILD-BREAKING** | netmsg.h:177–183, netmsg.c:4337–4413 |
| A-4 | SVC_CATALOG_INFO still writes `e->net_hash` on wire | FAIL | netmsg.c:4263 |
| A-5 | SVC_LOBBY_STATE arena encoded as net_hash (catalogWritePreSessionRef) | FAIL | netmsg.c:4194,4204 |
| A-6 | SVC_SESSION_CATALOG writes net_hash alongside catalog_id | FAIL | sessioncatalog.c:104 |
| A-7 | SVC_MATCH_MANIFEST entries include net_hash (manifestSerialize) | FAIL | netmanifest.c:704, 732 |
| A-8 | CLC_LOBBY_START write is catalog-ID-string-native | PASS | netmsg.c:3574–3649 |
| A-9 | CLC_LOBBY_START read uses catalog ID strings | PASS (WARN: stale comment) | netmsg.c:3802–3994 |
| A-10 | SVC_STAGE_START uses session wire IDs (u16) | PASS | netmsg.c:678–722 |
| A-11 | CLC_SETTINGS uses session IDs | WARN | netmsg.c:499–563 |
| A-12 | Stale comment in CLC_LOBBY_START read path | WARN | netmsg.c:3916 |
| B-1 | scenario_save writes raw `"arena"` integer alongside `"arenaId"` | FAIL | scenario_save.c:267 |
| B-2 | scenario_save load does not set `g_MatchConfig.stage_id` | FAIL | scenario_save.c:401–408 |
| B-3 | savefile writes raw `"weapons"` integer array | FAIL | savefile.c:790–793 |
| B-4 | savefile load has legacy `"stagenum"` integer fallback | FAIL | savefile.c:832–834 |
| B-5 | savefile load has legacy `"mpheadnum"`/`"mpbodynum"` integer fallback | FAIL | savefile.c:693–698 |
| B-6 | savefile player config write uses catalog ID strings | PASS | savefile.c:604–608 |
| B-7 | savefile MP setup write uses catalog ID string for stage | PASS | savefile.c:772–773 |
| B-8 | savefile weapon write uses catalog ID strings | PASS (dual-write FAIL B-3) | savefile.c:784–788 |
| C-1 | `matchconfig.stage_id` exists as PRIMARY | PASS | matchsetup.h:59–61 |
| C-2 | `matchStart()` resolves stagenum from stage_id | PASS | matchsetup.c:255–270 |
| C-3 | `matchConfigAddBot()` derives bodynum/headnum immediately (not deferred) | WARN | matchsetup.c:181–194 |
| C-4 | CLC_LOBBY_START send reads from `g_MatchConfig.stage_id` | PASS | netmsg.c:3582 |
| C-5 | Server CLC_LOBBY_START does not update `g_MpSetup.stage_id` | WARN | netmsg.c:3916 |
| C-6 | netlobby.c uses catalog resolution for body/head display | PASS | netlobby.c:69–72 |
| C-7 | SVC_STAGE_START uses session catalog (u16) | PASS | netmsg.c:678–722 |
| C-8 | stagenum (u8) used as runtime currency after ready-gate fires | WARN | netmsg.c:4113, net.c:749,753 |
| D-1 | `menuPush`/`menuPop` (enum form) called 5 times from ImGui layer | WARN | pausemenu.cpp:214,239; mainmenu.cpp:2201,2204,2239 |
| D-2 | `menuPushDialog`/`menuPopDialog` (dialog form) used extensively in `src/game/` | FAIL | endscreen.c (30+), activemenu.c, camdraw.c |
| D-3 | ImGui menus call legacy dialog system in 8 bridge points | WARN | 7 pdgui_menu_*.cpp files |
| E-1 | `catalogWritePreSessionRef`/`catalogReadPreSessionRef` still called | FAIL | netmsg.c:4194,4204 |
| E-2 | `assetCatalogResolveByNetHash` used as primary resolution in live net code | FAIL | netdistrib.c:289,598; sessioncatalog.c:167 |
| E-3 | `netLobbyRequestStart` API accepts catalog ID strings | PASS | pdgui_bridge.c |
| F-1 | constraints.md protocol version says v25, code says v27 | WARN | constraints.md:15 |
| F-2 | `NET_PROTOCOL_VER 27` header comment overstates v27 compliance | FAIL | net.h:12–15 |
| F-3 | netmsg.h declares v27 signatures that don't match implementations | BUILD-BREAKING | netmsg.h:172, 177–183, 199 |
| F-4 | matchsetup.h struct design is correct | PASS | matchsetup.h |
| F-5 | Prior audit H-1 through H-4 and M-2 through M-5 resolved | PASS | — |

---

## Priority Action Items

| Priority | Item | Effort |
|----------|------|--------|
| **P0 — Immediate** | F-3: Fix the 5 build-breaking signature mismatches in netmsg.h/netmsg.c (revert header to match implementation, OR update implementations to match header) | Small |
| **P0 — Immediate** | F-2: Either revert `NET_PROTOCOL_VER` to 26, or complete the v27 wire migration before shipping. Do not claim a protocol version the wire doesn't implement. | Medium–Large |
| **P1 — High** | A-5: Replace `catalogWritePreSessionRef`/`catalogReadPreSessionRef` in SVC_LOBBY_STATE with catalog ID strings (or session wire IDs) | Small |
| **P1 — High** | A-4: Replace `e->net_hash` with `e->id` string in SVC_CATALOG_INFO; update read path in netDistribClientHandleCatalogInfo to diff by string | Medium |
| **P1 — High** | A-7: Replace net_hash in `manifestSerialize`/`manifestDeserialize`; rebuild fingerprint from catalog ID strings | Medium |
| **P1 — High** | A-6: Remove net_hash from SVC_SESSION_CATALOG wire format; make catalog_id string the sole identity | Small |
| **P2 — Medium** | B-1: Remove raw `"arena"` integer write from scenario_save; use only `"arenaId"` string | Trivial |
| **P2 — Medium** | B-2: scenario_save load must set `g_MatchConfig.stage_id` from the resolved catalog ID string, not just stagenum | Small |
| **P2 — Medium** | B-3: Remove raw `"weapons"` integer write from savefile; use only `"weapon_ids"` strings | Trivial |
| **P3 — Low** | B-4/B-5: Remove legacy integer fallbacks in savefile load (break old saves intentionally, or convert on load) | Small |
| **P3 — Low** | C-5: Update `g_MpSetup.stage_id` from wire during CLC_LOBBY_START server processing | Trivial |
| **P3 — Low** | D-1: Replace `menuPush`/`menuPop` (enum form) in ImGui layer with ImGui-native state tracking | Small |
| **P4 — Future** | D-2/D-3: Full removal of `menuPushDialog`/`menuPopDialog` from game code and ImGui bridges | Large |
| **P4 — Future** | F-1: Update constraints.md with current protocol version and completed work | Trivial |
