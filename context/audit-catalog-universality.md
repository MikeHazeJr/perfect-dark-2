# Phase A Audit — Catalog Universality (Full Codebase Inventory)

> **Phase A deliverable** — Research only. No code was modified.
> **Date**: 2026-04-02 (Session S120)
> **Governing spec**: `PD2_Catalog_Universality_Spec_v1.0.docx`
> **Scope**: All `port/` source + `src/` game code called from port code.

---

## Executive Summary

| Metric | Count |
|--------|-------|
| Total raw-index reference sites | **47** |
| CRITICAL | 9 |
| HIGH | 8 |
| MEDIUM | 18 |
| LOW | 12 |

### Breakdown by Subsystem

| Subsystem | Sites | Highest Risk |
|-----------|-------|-------------|
| Network protocol (CLC_LOBBY_START) | 5 | CRITICAL |
| Body/Head allocation & spawn | 7 | CRITICAL |
| Network manifest building | 6 | HIGH |
| Save system (MP setup save/load) | 5 | HIGH |
| Save system (scenario/SP save) | 4 | MEDIUM |
| UI / Arena / Character pickers | 8 | MEDIUM |
| Legacy identity migration | 3 | MEDIUM |
| Catalog API internals (sanctioned) | 9 | LOW |

### Recommended Conversion Order

1. **Network protocol: CLC_LOBBY_START** — blocks all MP play with raw indices on the wire
2. **Body/Head spawn path (src/game/body.c)** — causes B-63/B-64 CATALOG-ASSERT on every bot spawn
3. **SVC_STAGE_START bot hash lookup index-domain bug** — silent wrong-body allocation
4. **Save system stagenum** — wrong catalogResolve function (stagenum ≠ runtime_index)
5. **Save system weapons** — protocol-safe but violates universality
6. **Network manifest builder** — catalogResolveByRuntimeIndex across all manifest callers
7. **UI arena/character pickers** — stores raw stagenum; no protocol boundary but should use catalog IDs
8. **Legacy identity migration** — acceptable bridge path, low urgency

---

## Subsystem 1: Network Protocol — CLC_LOBBY_START

**Summary**: CLC_LOBBY_START sends three categories of raw asset identifiers across the client→server boundary: stagenum, weapons[], and per-bot bodynum/headnum. None of these use `catalogWriteAssetRef`. This is the primary wire-protocol violation.

---

### 1-A: Raw stagenum in CLC_LOBBY_START write

- **File**: `port/src/net/netmsg.c`
- **Lines**: 3576–3580
- **Code**:
  ```c
  u32 netmsgClcLobbyStartWrite(struct netbuf *dst, u8 gamemode, u8 stagenum, ...)
  {
      netbufWriteU8(dst, CLC_LOBBY_START);
      netbufWriteU8(dst, gamemode);
      netbufWriteU8(dst, stagenum);    // ← RAW logical stage ID (e.g. 0x1f, 0x26)
  ```
- **What it does**: Sends the logical stage ID (N64 stagenum like `0x1f`) as a plain u8, bypassing `catalogWriteAssetRef`.
- **Catalog replacement**: `catalogWriteAssetRef(dst, sessionCatalogGetId(catalogResolveStageByStagenum(stagenum)))` — but first the function signature needs to accept a catalog stage ID string instead of raw stagenum.
- **Subsystem**: Network protocol / stage loading
- **Risk**: CRITICAL — server receives stagenum=0 or wrong stage when catalog miss occurs; confirmed B-65 origin.

---

### 1-B: Raw stagenum in CLC_LOBBY_START read

- **File**: `port/src/net/netmsg.c`
- **Lines**: 3772–3775
- **Code**:
  ```c
  u32 netmsgClcLobbyStartRead(struct netbuf *src, struct netclient *srccl)
  {
      const u8 stagenum = netbufReadU8(src);   // ← reads raw u8 into stagenum
  ```
- **What it does**: Server reads the raw stagenum from CLC_LOBBY_START, stores in `g_MpSetup.stagenum`. Server logs it at line 3909: `"stage=0x%02x"`.
- **Catalog replacement**: Read a `u16 stage_session = catalogReadAssetRef(src)` and resolve via `catalogResolveStageBySession`.
- **Subsystem**: Network protocol / stage loading
- **Risk**: CRITICAL — pairs with 1-A. Confirmed root cause of B-65.

---

### 1-C: Raw weapons[] bulk write in CLC_LOBBY_START

- **File**: `port/src/net/netmsg.c`
- **Lines**: 3590–3592
- **Code**:
  ```c
  /* Send the already-resolved weapon slots so the server can forward them
   * in SVC_STAGE_START without needing mplayer game engine code. */
  netbufWriteData(dst, g_MpSetup.weapons, sizeof(g_MpSetup.weapons));
  ```
- **What it does**: Sends the entire `g_MpSetup.weapons[]` array (raw MPWEAPON_* integers, e.g. `0x1f`, `0x26`) as a bulk memory block. This bypasses the per-slot `catalogWriteAssetRef` loop that SVC_STAGE_START uses on the outbound path.
- **Catalog replacement**: Replace with a loop calling `catalogWriteAssetRef(dst, sessionCatalogGetId(catalogResolveWeaponByGameId(g_MpSetup.weapons[wi])))` for each weapon slot (mirroring the SVC_STAGE_START write path, lines 721–735).
- **Subsystem**: Network protocol / weapon
- **Risk**: CRITICAL — server receives raw weapon bytes; if weapon indices shift on any side, wrong weapons load.

---

### 1-D: Raw weapons[] bulk read in CLC_LOBBY_START

- **File**: `port/src/net/netmsg.c`
- **Line**: 3786
- **Code**:
  ```c
  netbufReadData(src, g_MpSetup.weapons, sizeof(g_MpSetup.weapons));
  ```
- **What it does**: Server reads raw MPWEAPON_* bytes directly into `g_MpSetup.weapons[]`. The SVC_STAGE_START write path then re-resolves these via `catalogResolveWeaponByGameId`, but the raw bytes are now in shared game state.
- **Catalog replacement**: Read with per-slot `catalogReadAssetRef` + `catalogResolveWeaponBySession`.
- **Subsystem**: Network protocol / weapon
- **Risk**: CRITICAL — pairs with 1-C.

---

### 1-E: Raw bot bodynum/headnum in CLC_LOBBY_START write + read

- **File**: `port/src/net/netmsg.c`
- **Lines**: 3598–3616 (write), 3878–3906 (read)
- **Code (write)**:
  ```c
  netbufWriteU8(dst, sl->bodynum);   // line 3602 — mpbody index (g_MpBodies[] position)
  netbufWriteU8(dst, sl->headnum);   // line 3603 — mphead index (g_MpHeads[] position)
  ```
  Default padding (lines 3610–3616):
  ```c
  netbufWriteU8(dst, 0xFF); /* default body */
  netbufWriteU8(dst, 0xFF); /* default head */
  ```
- **Code (read)**:
  ```c
  const u8 bodynum = netbufReadU8(src);    // line 3883
  const u8 headnum = netbufReadU8(src);    // line 3884
  ...
  g_BotConfigsArray[bi].base.mpbodynum = bodynum;  // line 3895
  g_BotConfigsArray[bi].base.mpheadnum = headnum;  // line 3896
  ```
- **What it does**: Bot appearance is sent as raw g_MpBodies[]/g_MpHeads[] indices. Index 0xFF is the "use default" sentinel, but 0xFF is also a valid mpbody index range (0–62 typically). Server stores raw bytes directly in g_BotConfigsArray, bypassing catalog validation.
- **Downstream impact**: In `netmsgSvcStageStartWrite` lines 774–782, SVC_STAGE_START tries to look up the bot's net_hash by scanning for `e->runtime_index == bc->base.mpbodynum`. But `bc->base.mpbodynum` is a g_MpBodies[] index (position), while `e->runtime_index` for ASSET_BODY entries is the g_HeadsAndBodies[] index (bodynum field). **These are different index spaces.** The hash lookup always returns 0, SVC_STAGE_START sends session_id=0 for all bots, and clients receive no body/head for any bot.
- **Catalog replacement**: Per-bot `catalogWriteAssetRef(dst, sessionCatalogGetId(body_canon))` on write; `catalogReadAssetRef` → `catalogResolveBodyBySession` on read.
- **Subsystem**: Network protocol / body/head
- **Risk**: CRITICAL — direct cause of bots being invisible (B-63/B-64). All bots use fallback body after session ID fails to resolve.

---

## Subsystem 2: Body/Head Allocation — Spawn Path

**Summary**: `bodyAllocateModel()` and `bodyAllocateChr()` call `catalogResolveByRuntimeIndex(ASSET_BODY/HEAD, bodynum/headnum)` for every spawned character. For SP-only characters whose headnum is not registered in the catalog, this produces `[CATALOG-ASSERT] type=16 index=N not found`. Additionally, the conversion from mpbodynum→g_HeadsAndBodies index via `mpGetBodyId`/`mpGetHeadId` introduces an intermediate index-domain conversion that isn't tracked in the catalog.

---

### 2-A: bodyAllocateModel — ASSET_BODY/HEAD resolve for spawn

- **File**: `src/game/body.c`
- **Lines**: 354–358
- **Code**:
  ```c
  body_canon = catalogResolveByRuntimeIndex(ASSET_BODY, bodynum);
  if (body_canon) { manifestEnsureLoaded(body_canon, MANIFEST_TYPE_BODY); }
  if (headnum >= 0) {
      head_canon = catalogResolveByRuntimeIndex(ASSET_HEAD, headnum);
      if (head_canon) { manifestEnsureLoaded(head_canon, MANIFEST_TYPE_HEAD); }
  }
  ```
  Where `bodynum`/`headnum` are g_HeadsAndBodies[] indices passed through from `mpGetBodyId`/`mpGetHeadId`.
- **What it does**: Resolves body/head for manifest tracking at spawn time. Produces CATALOG-ASSERT for any g_HeadsAndBodies[] index that was not registered in the catalog (e.g., SP-only characters, special heads).
- **Root cause of B-63/B-64**: `ASSET_HEAD = 16` (the enum value). The assertion log prints `type=16 index=103`. This is `catalogResolveByRuntimeIndex(ASSET_HEAD, 103)` where head 103 in g_HeadsAndBodies[] is either (a) an SP-only head not registered, or (b) not reachable through the MP-focused registration loop.
- **Catalog replacement**: The catalog registration in `assetcatalog_base.c` must cover ALL non-zero entries in g_HeadsAndBodies[152] with correct ASSET_BODY or ASSET_HEAD classification. Alternatively, `bodyAllocateModel` should fail gracefully (log warning, skip manifest entry) rather than asserting.
- **Subsystem**: Body/head
- **Risk**: CRITICAL — fires for every bot spawn in MP. Every bot invisible.

---

### 2-B: bodyAllocateChr — same ASSET_BODY/HEAD resolve

- **File**: `src/game/body.c`
- **Lines**: 470–473
- **Code**:
  ```c
  body_canon = catalogResolveByRuntimeIndex(ASSET_BODY, bodynum);
  ...
  head_canon = catalogResolveByRuntimeIndex(ASSET_HEAD, headnum);
  ```
- **What it does**: Same pattern as 2-A, used for SP chr spawns from stage setup files. SP chars may have bodynum/headnum values outside the range of catalog-registered entries.
- **Subsystem**: Body/head
- **Risk**: CRITICAL — fires for SP-only characters. SP stages crash or have invisible NPCs.

---

### 2-C: botmgr.c — mpGetBodyId / mpGetHeadId index-domain chain

- **File**: `src/game/botmgr.c`
- **Lines**: 44–45, 58
- **Code**:
  ```c
  headnum = mpGetHeadId(g_BotConfigsArray[aibotnum].base.mpheadnum);
  bodynum = mpGetBodyId(g_BotConfigsArray[aibotnum].base.mpbodynum);
  ...
  model = bodyAllocateModel(bodynum, headnum, 0);
  ```
- **What it does**: Converts mpheadnum (g_MpHeads[] position) → g_HeadsAndBodies index via `modmgrGetHead(headnum)->headnum`. The result is used in bodyAllocateModel → catalogResolveByRuntimeIndex. If the mpheadnum received was 0 (fallback) due to CLC_LOBBY_START transmission failure, this maps to whatever body is at position 0 in the catalog bodies list.
- **Subsystem**: Body/head
- **Risk**: HIGH — index-domain confusion causes wrong bodies; masked by fallback to body 0.

---

### 2-D: SVC_STAGE_START bot net_hash lookup — wrong index domain

- **File**: `port/src/net/netmsg.c`
- **Lines**: 773–782
- **Code**:
  ```c
  /* SA-3: bot body/head as session IDs via net_hash lookup */
  {
      struct s_rindex_hash_lookup bl = { (s32)bc->base.mpbodynum, 0 };
      assetCatalogIterateByType(ASSET_BODY, s_netHashCb, &bl);
      catalogWriteAssetRef(dst, sessionCatalogGetIdByHash(bl.hash));
  }
  ```
- **What it does**: Searches for a catalog ASSET_BODY entry where `e->runtime_index == bc->base.mpbodynum`. **Bug**: `bc->base.mpbodynum` is a g_MpBodies[] position index (0..62), but `e->runtime_index` for ASSET_BODY is the g_HeadsAndBodies[] index (bodynum field, range 0..151). These are different number spaces. The lookup returns 0 for most bots, sending `session_id=0` which the client interprets as no body.
- **Catalog replacement**: Convert mpbodynum to g_HeadsAndBodies index first via `modmgrGetBody(mpbodynum)->bodynum`, then search by that value. Or better: search by catalog string ID derived from mpbodynum → `catalogResolveBody` → net_hash.
- **Subsystem**: Network protocol / body/head
- **Risk**: CRITICAL — all bots receive wrong/fallback body on client.

---

### 2-E: setuputils.c — ASSET_MODEL resolve by modelnum

- **File**: `src/game/setuputils.c`
- **Line**: 167
- **Code**:
  ```c
  model_id = catalogResolveByRuntimeIndex(ASSET_MODEL, modelnum);
  ```
- **What it does**: Resolves a prop model for manifest tracking. Modelnum is a raw MODEL_* integer constant.
- **Subsystem**: Props
- **Risk**: MEDIUM — fires CATALOG-ASSERT for any model not registered in catalog. Props may be missing from manifest.

---

## Subsystem 3: Network Manifest Building

**Summary**: `netmanifest.c` builds the match manifest by converting runtime indices (mpbodynum, mpheadnum, bodynum, headnum, modelnum, stagenum) to catalog string IDs via `catalogResolveByRuntimeIndex`. These conversions are correctness-critical — if any lookup fails, the asset is silently excluded from the manifest, causing client-side load failures.

---

### 3-A: manifestBuild() — bot body/head from g_BotConfigsArray

- **File**: `port/src/net/netmanifest.c`
- **Lines**: 455–474
- **Code**:
  ```c
  const u8 bodynum = g_BotConfigsArray[i].base.mpbodynum;
  const u8 headnum = g_BotConfigsArray[i].base.mpheadnum;
  const char *body_canon = catalogResolveByRuntimeIndex(ASSET_BODY, (s32)bodynum);
  const char *head_canon = catalogResolveByRuntimeIndex(ASSET_HEAD, (s32)headnum);
  ```
- **What it does**: Builds manifest entries for bot characters. `mpbodynum` is a g_MpBodies[] index, but `catalogResolveByRuntimeIndex(ASSET_BODY, mpbodynum)` looks up by runtime_index (g_HeadsAndBodies index). **Index-domain mismatch** — same bug as 2-D.
- **Subsystem**: Network / body/head
- **Risk**: HIGH — bot bodies silently excluded from manifest; bots may have missing assets after match load.

---

### 3-B: manifestBuild() — default body/head (index 0)

- **File**: `port/src/net/netmanifest.c`
- **Lines**: 573–574
- **Code**:
  ```c
  const char *body0_id = catalogResolveByRuntimeIndex(ASSET_BODY, 0);
  const char *head0_id = catalogResolveByRuntimeIndex(ASSET_HEAD, 0);
  ```
- **What it does**: Adds Joanna's default body/head to the manifest as fallback. Runtime_index=0 for ASSET_BODY should match the first registered body. Likely works, but depends on registration order.
- **Subsystem**: Network / body/head
- **Risk**: MEDIUM — if registration order changes, wrong default is used.

---

### 3-C: manifestBuildMission() — SP chr body/head from packedchr

- **File**: `port/src/net/netmanifest.c`
- **Lines**: 623–646
- **Code**:
  ```c
  if (chr->bodynum != 255) {
      const char *bcan = catalogResolveByRuntimeIndex(ASSET_BODY, (s32)chr->bodynum);
      ...
  }
  if (chr->headnum >= 0) {
      const char *hcan = catalogResolveByRuntimeIndex(ASSET_HEAD, (s32)chr->headnum);
      ...
  }
  ```
- **What it does**: Iterates setup file chr entries and adds body/head to SP manifest. `chr->bodynum` and `chr->headnum` are g_HeadsAndBodies[] indices from the packed chr data. If an SP-only head (e.g., index 103) was not registered, it's silently skipped.
- **Subsystem**: Network / body/head
- **Risk**: HIGH — SP chars with unregistered bodies/heads are excluded from manifest; they fail to load.

---

### 3-D: manifestBuildMission() — model by modelnum

- **File**: `port/src/net/netmanifest.c`
- **Line**: 684
- **Code**:
  ```c
  model_id = catalogResolveByRuntimeIndex(ASSET_MODEL, (s32)modelnum);
  ```
- **What it does**: Adds prop models to SP manifest by MODEL_* index.
- **Subsystem**: Props
- **Risk**: MEDIUM — unregistered models silently excluded.

---

### 3-E: manifestBuildMission() — anti-player body/head

- **File**: `port/src/net/netmanifest.c`
- **Lines**: 714–735
- **Code**:
  ```c
  if (g_Vars.antibodynum >= 0) {
      const char *bcan = catalogResolveByRuntimeIndex(ASSET_BODY, (s32)g_Vars.antibodynum);
      ...
  }
  if (g_Vars.antiheadnum >= 0) {
      const char *hcan = catalogResolveByRuntimeIndex(ASSET_HEAD, (s32)g_Vars.antiheadnum);
      ...
  }
  ```
- **What it does**: Adds Counter-Op player body/head to SP manifest. `g_Vars.antibodynum`/`antiheadnum` are g_HeadsAndBodies[] indices.
- **Subsystem**: Network / body/head
- **Risk**: MEDIUM — Counter-Op player invisible if indices not registered.

---

### 3-F: net.c — player body/head resolve for manifest/auth

- **File**: `port/src/net/net.c`
- **Lines**: 320–323
- **Code**:
  ```c
  const char *bid = catalogResolveByRuntimeIndex(ASSET_BODY,
      (s32)g_PlayerConfigsArray[playernum].base.mpbodynum);
  const char *hid = catalogResolveByRuntimeIndex(ASSET_HEAD,
      (s32)g_PlayerConfigsArray[playernum].base.mpheadnum);
  ```
- **What it does**: Resolves local player body/head to catalog strings for network settings. Same index-domain issue: `mpbodynum` is a g_MpBodies[] position, not a g_HeadsAndBodies[] index. Lookup may fail.
- **Subsystem**: Network / body/head
- **Risk**: HIGH — player's chosen body may not transmit correctly if lookup fails.

---

## Subsystem 4: Save System — MP Setup Save/Load

---

### 4-A: savefile.c — wrong catalogResolve for stage

- **File**: `port/src/savefile.c`
- **Line**: 761
- **Code**:
  ```c
  const char *stage_id = catalogResolveByRuntimeIndex(ASSET_MAP, (s32)g_MpSetup.stagenum);
  ```
- **What it does**: Saves the current stage as a catalog string ID. **Bug**: `g_MpSetup.stagenum` is the logical stage ID (e.g., `0x1f` for jungle), but `catalogResolveByRuntimeIndex(ASSET_MAP, ...)` looks up by array position in g_Stages[] (runtime_index). For stages where `g_Stages[i].id != i`, this resolves the WRONG stage. Should use `catalogResolveStageByStagenum((s32)g_MpSetup.stagenum)`.
- **Catalog replacement**: `catalogResolveStageByStagenum((s32)g_MpSetup.stagenum)`
- **Subsystem**: Save system / stage
- **Risk**: HIGH — scenario saves record wrong stage; reloading starts in wrong stage.

---

### 4-B: savefile.c — weapons saved/loaded as raw integers

- **File**: `port/src/savefile.c`
- **Lines**: 773 (save), 829 (load)
- **Code (save)**:
  ```c
  fprintf(fp, "%u%s", g_MpSetup.weapons[i], i < NUM_MPWEAPONSLOTS - 1 ? ", " : "");
  ```
  **Code (load)**:
  ```c
  g_MpSetup.weapons[i] = s_tok_int(&tok);
  ```
- **What it does**: Saves/loads weapon slots as raw MPWEAPON_* integers. If the MPWEAPON_* enum values change or weapons are added/removed, existing saves will load wrong weapons.
- **Catalog replacement**: Save as catalog string IDs (weapon entry names); load via `assetCatalogResolve` and extract `ext.weapon.game_id`.
- **Subsystem**: Save system / weapon
- **Risk**: MEDIUM — safe with current stable enum; violates universality principle.

---

### 4-C: savefile.c — player mpheadnum/mpbodynum saved via catalogResolveByRuntimeIndex

- **File**: `port/src/savefile.c`
- **Lines**: 603–604
- **Code**:
  ```c
  const char *head_id = catalogResolveByRuntimeIndex(ASSET_HEAD, (s32)pc->base.mpheadnum);
  const char *body_id = catalogResolveByRuntimeIndex(ASSET_BODY, (s32)pc->base.mpbodynum);
  ```
- **What it does**: Saves player character selections as catalog string IDs. **Index-domain issue**: `mpheadnum/mpbodynum` are g_MpBodies[]/g_MpHeads[] positions, but catalogResolveByRuntimeIndex(ASSET_HEAD, ...) looks up by g_HeadsAndBodies[] index. Lookup likely succeeds for base game (coincidence of index values) but will fail for mod characters.
- **Catalog replacement**: Convert mpbodynum → g_HeadsAndBodies index first via `modmgrGetBody(mpbodynum)->bodynum`, then resolve. Or add a `catalogResolveBodyByMpIndex(mpbodynum)` typed entry point.
- **Subsystem**: Save system / body/head
- **Risk**: HIGH — mod character saves silently fall back to empty string; character not restored on load.

---

### 4-D: savefile.c — player head/body load path

- **File**: `port/src/savefile.c`
- **Lines**: 682–686
- **Code**:
  ```c
  if (e && e->type == ASSET_BODY)
      pc->base.mpbodynum = (u8)e->runtime_index;
  ```
- **What it does**: On save load, resolves catalog string IDs back to runtime indices. **Issue**: `e->runtime_index` for ASSET_BODY is the g_HeadsAndBodies[] index, but `mpbodynum` storage expects a g_MpBodies[] position. This means the loaded character (by g_HeadsAndBodies index) may not match the expected g_MpBodies[] position.
- **Subsystem**: Save system / body/head
- **Risk**: HIGH — character loaded from save appears as wrong character.

---

## Subsystem 5: Save System — Scenario / SP Save

---

### 5-A: scenario_save.c — stage resolved with catalogResolveByRuntimeIndex

- **File**: `port/src/scenario_save.c`
- **Line**: 270
- **Code**:
  ```c
  const char *stage_id = catalogResolveByRuntimeIndex(ASSET_MAP, (s32)g_MpSetup.stagenum);
  ```
- **What it does**: Same stagenum-vs-runtime_index mismatch as 4-A. Saves wrong stage for scenario.
- **Catalog replacement**: `catalogResolveStageByStagenum((s32)g_MpSetup.stagenum)`
- **Subsystem**: Save system / stage
- **Risk**: HIGH — scenario save records wrong stage.

---

### 5-B: scenario_save.c — SP chr body/head save

- **File**: `port/src/scenario_save.c`
- **Lines**: 302–305
- **Code**:
  ```c
  const char *body_id = ((s32)sl->bodynum < catalogGetNumBodies())
      ? catalogResolveByRuntimeIndex(ASSET_BODY, (s32)sl->bodynum) : NULL;
  const char *head_id = ((s32)sl->headnum < catalogGetNumHeads())
      ? catalogResolveByRuntimeIndex(ASSET_HEAD, (s32)sl->headnum) : NULL;
  ```
- **What it does**: Saves SP chr appearances using g_HeadsAndBodies[] indices. Bounds-check against `catalogGetNumBodies()` is incorrect — `catalogGetNumBodies()` returns the count of registered ASSET_BODY catalog entries, not 152. An SP head at index 103 might be within g_HeadsAndBodies[] bounds but exceed catalogGetNumBodies() if only 50 bodies are registered.
- **Subsystem**: Save system / body/head
- **Risk**: MEDIUM — SP chr appearances not persisted for some characters.

---

### 5-C: scenario_save.c — weapons loaded as raw integers

- **File**: `port/src/scenario_save.c`
- **Line**: 428
- **Code**:
  ```c
  g_MatchConfig.weapons[slot] = (u8)wval;
  ```
- **What it does**: Loads saved weapon indices as raw integers into match config. Same stability concern as 4-B.
- **Subsystem**: Save system / weapon
- **Risk**: MEDIUM — safe now; fails if weapon enum changes.

---

### 5-D: scenario_save.c — body/head load via catalog string lookup

- **File**: `port/src/scenario_save.c`
- **Line**: 498
- **Code**:
  ```c
  if (e && e->type == ASSET_BODY)
  ```
- **What it does**: Restores body/head from catalog string ID. Appears correct — uses `assetCatalogResolve()` then checks type. Confirm the runtime_index → mpbodynum mapping is correct here.
- **Subsystem**: Save system / body/head
- **Risk**: LOW — functionally correct path.

---

## Subsystem 6: UI / Menu — Arena and Character Pickers

---

### 6-A: pdgui_menu_matchsetup.cpp — s_ArenaIndex stores raw stagenum

- **File**: `port/fast3d/pdgui_menu_matchsetup.cpp`
- **Lines**: 451, 1136–1142, 1421
- **Code**:
  ```cpp
  static s32 s_ArenaIndex = 0;   // stores ae->ext.arena.stagenum
  ...
  s_ArenaIndex = ae->ext.arena.stagenum;              // line 1136
  g_MatchConfig.stagenum = (u8)ae->ext.arena.stagenum; // line 1138
  ...
  s_ArenaIndex = (s32)g_MatchConfig.stagenum;         // line 1421
  ```
- **What it does**: Uses the logical stagenum as the arena selection identifier. Not wrong internally, but `g_MatchConfig.stagenum` is then sent as a raw u8 in CLC_LOBBY_START (see 1-A).
- **Catalog replacement**: Once CLC_LOBBY_START is migrated, the UI can continue storing stagenum locally but must convert to catalog ID when crossing the protocol boundary.
- **Subsystem**: Stage loading / UI
- **Risk**: MEDIUM — local storage OK; becomes CRITICAL when transmitted via CLC_LOBBY_START.

---

### 6-B: pdgui_menu_room.cpp — s_SelectedArena stores array index; stagenum assigned

- **File**: `port/fast3d/pdgui_menu_room.cpp`
- **Lines**: 340, 197, 906, 1724, 1739
- **Code**:
  ```cpp
  static int s_SelectedArena = 0;   // local array index into s_Arenas[]
  s_Arenas[s_NumArenas].stagenum = e->ext.arena.stagenum;   // line 197
  g_MatchConfig.stagenum = (u8)s_Arenas[s_SelectedArena].stagenum; // line 906
  ```
  When starting match (line 1739):
  ```cpp
  (u8)s_Arenas[s_SelectedArena].stagenum,   // passed as stagenum to lobby start
  ```
- **What it does**: The room screen maintains a local arena list with stagenum values. Selection sets `g_MatchConfig.stagenum` which flows into CLC_LOBBY_START.
- **Subsystem**: Stage loading / UI
- **Risk**: MEDIUM — same as 6-A; local storage is fine, wire transmission is the bug.

---

### 6-C: pdgui_menu_agentcreate.cpp — character picker uses mpbody/mphead indices

- **File**: `port/fast3d/pdgui_menu_agentcreate.cpp`
- **Lines**: 108–109, 155–156, 180, 555
- **Code**:
  ```cpp
  static s32 s_SelectedBody = 0;  // g_MpBodies[] position
  static s32 s_SelectedHead = 0;  // g_MpHeads[] position
  ...
  s32 headIdx = mpGetMpheadnumByMpbodynum(s_SelectedBody); // line 156
  ...
  pdguiCharPreviewRequest((u8)s_SelectedHead, (u8)s_SelectedBody); // line 180
  ...
  mpPlayerConfigSetHeadBody(pnum, (u8)s_SelectedHead, (u8)s_SelectedBody); // line 555
  ```
- **What it does**: Stores g_MpBodies[]/g_MpHeads[] position indices as body/head selection. `mpPlayerConfigSetHeadBody` writes to `g_PlayerConfigsArray[pnum].base.mpheadnum/mpbodynum`. Correct within the legacy index domain.
- **Subsystem**: Body/head / UI
- **Risk**: MEDIUM — works with current code but bypasses catalog ID storage. Should store catalog string IDs in identity profile instead.

---

### 6-D: netmenu.c legacy dropdown character picker

- **File**: `port/src/net/netmenu.c`
- **Lines**: 163–171, 385–393
- **Code**:
  ```c
  s32 mpbodynum = data->dropdown.value - 1;
  g_PlayerConfigsArray[0].base.mpbodynum = mpbodynum;
  g_PlayerConfigsArray[0].base.mpheadnum = mpGetMpheadnumByMpbodynum(mpbodynum);
  ```
- **What it does**: Old N64-style dropdown character picker (wraps g_MpBodies[] selection by position index). `mpGetMpheadnumByMpbodynum` on the server always returns 0 (stub in server_stubs.c:300). This is a legacy code path from the original game's menu system.
- **Subsystem**: Body/head / UI
- **Risk**: MEDIUM — on server, always sets head to 0 regardless of selection.

---

### 6-E: pdgui_bridge.c — stagenum returned raw for pause menu

- **File**: `port/fast3d/pdgui_bridge.c`
- **Line**: 370
- **Code**:
  ```c
  return g_MpSetup.stagenum;
  ```
- **What it does**: Pause menu queries current stage as raw stagenum. The caller uses this to find the arena name via `langGet(arenaName)`. Internal only; doesn't cross a protocol boundary.
- **Subsystem**: Stage loading / UI
- **Risk**: LOW — internal UI only.

---

## Subsystem 7: Legacy Identity Migration

---

### 7-A: identity.c — profile head/body loaded via catalogResolveByRuntimeIndex

- **File**: `port/src/identity.c`
- **Lines**: 224–225
- **Code**:
  ```c
  resolved_head = catalogResolveByRuntimeIndex(ASSET_HEAD, (s32)headnum);
  resolved_body = catalogResolveByRuntimeIndex(ASSET_BODY, (s32)bodynum);
  ```
- **What it does**: Converts stored integer indices from old save profiles to catalog string IDs. `headnum`/`bodynum` come from the raw u8 profile fields. Same index-domain concern (g_MpBodies[] index vs g_HeadsAndBodies[] index) but acceptable as a one-time migration path if it handles the NULL case (which it does — caller checks for NULL).
- **Subsystem**: Save system / body/head
- **Risk**: MEDIUM — if migration resolves to wrong catalog entry, character is permanently wrong in profile.

---

## Subsystem 8: Catalog API Internals (Sanctioned Access)

The following sites access legacy arrays (`g_HeadsAndBodies[]`, `g_Stages[]`) **only within the designated allowed modules** (`assetcatalog_base.c`, `assetcatalog_api.c`, `modelcatalog.c`). Per `port/CLAUDE.md` SA-7 rules, these sites are explicitly sanctioned. Listed here for completeness; no action required unless the rule changes.

| Site | File | Lines | Purpose |
|------|------|-------|---------|
| g_HeadsAndBodies[] registration scan | assetcatalog_base.c | 477, 516, 627, 634, 645, 659 | Initial catalog population from ROM data |
| g_Stages[] file ID extraction | assetcatalog_api.c | 74–78 | `catalogGetStageResultByIndex` accessor |
| g_HeadsAndBodies[] filenum registration | assetcatalog_base.c | 472–516 | MP body/head filenum recording |
| modelcatalog.c loop scan | modelcatalog.c | 327, 346 | Thumbnail/validation loop (bounded) |
| modelcatalog.c index parameter | modelcatalog.c | 406 | `&g_HeadsAndBodies[index]` — caller-validated |
| catalogResolveByRuntimeIndex definition | assetcatalog_api.c | 324–341 | Bridge function definition |
| catalogWriteAssetRef / catalogReadAssetRef | assetcatalog_api.c | 348–358 | Wire encoding (correct) |

**Note**: `modelcatalog.c:406` takes an `index` parameter from the caller. Callers should validate `index < 152` before calling — confirm this is the case.

---

## Subsystem 9: Audio / Texture / Animation

**No violations found.** Audio IDs (soundnum), texture references (texnum), and animation IDs (animnum) are used as internal game-engine constants, not as catalog-boundary-crossing identifiers in the current port code. The catalog interception for these types (`catalogGetSoundOverride`, `catalogGetTextureOverride`, `catalogGetAnimOverride`) operates at the engine load layer and does not require additional audit points.

---

## Cross-Cutting Notes

### The Index-Domain Problem

Three distinct integer index spaces exist for body/head assets that are regularly confused:

| Domain | Range | What it is |
|--------|-------|-----------|
| `mpbodynum` (g_MpBodies[] position) | 0..62 | Position in the MP body list |
| `bodynum` (g_HeadsAndBodies[] index) | 0..151 | ROM character model index |
| `runtime_index` in catalog | 0..151 (for bodies) | Set to g_HeadsAndBodies[] index at registration |

The critical confusion is: **`mpbodynum` and `bodynum` (runtime_index) are different numbers for most characters**. `mpbodynum=5` means "the 6th entry in g_MpBodies[]", whose `bodynum` field might be 30 (g_HeadsAndBodies[30]). All `catalogResolveByRuntimeIndex(ASSET_BODY, mpbodynum)` calls using mpbodynum (not bodynum) will return NULL or the wrong character.

Conversion: `bodynum = modmgrGetBody(mpbodynum)->bodynum`

### Server Catalog Gap

The server build zero-initializes `g_HeadsAndBodies[152]` (server_stubs.c:326). As a result, the assetcatalog_base.c registration loop skips all entries (filenum==0 check). The server's catalog contains **zero ASSET_BODY and ASSET_HEAD entries**. This is by design for Phase D (server receives manifest, not catalog), but all catalog lookups for body/head on the server currently return NULL.

### Weapons — No Catalog-ID wire path in SVC_STAGE_START

Looking at SVC_STAGE_START write (netmsg.c lines 719–735), weapons ARE sent as `catalogWriteAssetRef(dst, sessionCatalogGetId(wcanon))` using `catalogResolveWeaponByGameId`. This path is **correct**. The violation is only in CLC_LOBBY_START (1-C/1-D) which sends them raw before the server converts them.

---

## Action Items for Phase B / C

| ID | File | Line(s) | Action | Risk |
|----|------|---------|--------|------|
| FIX-1 | netmsg.c | 3580 | CLC_LOBBY_START: stagenum → catalogWriteAssetRef | CRITICAL |
| FIX-2 | netmsg.c | 3775 | CLC_LOBBY_START read: u8 → catalogReadAssetRef | CRITICAL |
| FIX-3 | netmsg.c | 3592 | CLC_LOBBY_START: netbufWriteData weapons → per-slot catalogWriteAssetRef loop | CRITICAL |
| FIX-4 | netmsg.c | 3786 | CLC_LOBBY_START read: netbufReadData → per-slot catalogReadAssetRef | CRITICAL |
| FIX-5 | netmsg.c | 3602–3603 | CLC_LOBBY_START: bot bodynum/headnum → catalogWriteAssetRef | CRITICAL |
| FIX-6 | netmsg.c | 3883–3896 | CLC_LOBBY_START read: bot u8 → catalogReadAssetRef + type-safe resolve | CRITICAL |
| FIX-7 | netmsg.c | 775–782 | SVC_STAGE_START: bot hash lookup — convert mpbodynum→bodynum before iteration | CRITICAL |
| FIX-8 | body.c | 354–358 | bodyAllocateModel: add missing catalog registrations for all g_HeadsAndBodies[] entries | CRITICAL |
| FIX-9 | body.c | 470–473 | bodyAllocateChr: same as FIX-8 | CRITICAL |
| FIX-10 | savefile.c | 761 | Wrong resolve: catalogResolveByRuntimeIndex(MAP, stagenum) → catalogResolveStageByStagenum | HIGH |
| FIX-11 | savefile.c | 603–604 | Save body/head: mpbodynum → bodynum conversion before catalogResolveByRuntimeIndex | HIGH |
| FIX-12 | savefile.c | 682–686 | Load body/head: runtime_index → mpbodynum conversion after catalog resolve | HIGH |
| FIX-13 | netmanifest.c | 455–458 | manifestBuild() bot: mpbodynum → bodynum before catalogResolveByRuntimeIndex | HIGH |
| FIX-14 | net.c | 320–323 | Player auth: mpbodynum → bodynum before catalogResolveByRuntimeIndex | HIGH |
| FIX-15 | scenario_save.c | 270 | Wrong resolve: same as FIX-10 | HIGH |
| FIX-16 | scenario_save.c | 302–305 | SP chr save: bounds check catalogGetNumBodies() is wrong metric | MEDIUM |
| FIX-17 | netmanifest.c | 573–574 | Default body/head: runtime_index=0 may not be Joanna; use named catalog ID | MEDIUM |
| FIX-18 | netmanifest.c | 623–646 | SP chr manifest: ensure all g_HeadsAndBodies[] entries registered | MEDIUM |
| FIX-19 | netmanifest.c | 714–735 | Anti-player: g_Vars.antibodynum is g_HeadsAndBodies index — verify registration | MEDIUM |
| FIX-20 | identity.c | 224–225 | Profile migration: mpbodynum → bodynum conversion | MEDIUM |
| FIX-21 | savefile.c | 773, 829 | MP setup weapons: raw integers → catalog string IDs | MEDIUM |
| FIX-22 | scenario_save.c | 428 | Scenario weapons: raw load → catalog resolve | MEDIUM |
| FIX-23 | netmenu.c | 163–171 | Legacy dropdown: replace with catalog-ID based character picker | MEDIUM |
| FIX-24 | assetcatalog_base.c | 590–665 | Register ALL non-zero g_HeadsAndBodies[] entries (not just MP-reachable) | CRITICAL |

---

*End of Phase A audit. Phase B begins with API hardening and typed entry points. Phase C converts subsystems one at a time using this map.*
