# Catalog ID Compliance Audit — Full Codebase

**Date**: 2026-04-02
**Auditor**: Claude (Sonnet 4.6) — automated deep audit
**Scope**: All asset references across wire, disk, public API, runtime structs, UI
**Mandate**: Every asset reference must use catalog ID strings (`"namespace:readable_name"` format). Raw integers (bodynum, headnum, stagenum, etc.) and net_hash (u32 CRC32) are violations outside catalog internals.

---

## Executive Summary

The codebase is in a **transitional state**. A significant amount of work has already been done to introduce catalog ID strings at boundaries (savefile, netmsg CLC_LOBBY_START), but three systemic categories of violations remain:

1. **net_hash (CRC32) used on wire** — The entire `catalogWritePreSessionRef`/`catalogReadPreSessionRef` system, netmanifest, netdistrib, and sessioncatalog all use `u32` CRC32 hashes as wire identity. Per the mandate, net_hash is deprecated and must be replaced with catalog ID strings.

2. **`matchconfig.stagenum` and `g_MpSetup.stagenum` as runtime identity** — These u8/u16 integer fields are the primary stage identity in the match pipeline. No catalog ID string counterpart exists in these structs. The wire (via CLC_LOBBY_START) converts them to net_hash just before transmission, which is itself a violation.

3. **game-layer mpbodynum/mpheadnum defaults** — `mplayer.c` sets player and bot configs using `MPBODY_*`/`MPHEAD_*` integer constants directly, bypassing the catalog entirely.

---

## CRITICAL — Wire/Disk Boundary Violations

These must be fixed. Asset identity crosses a boundary (wire or disk) as an integer or net_hash.

---

### C-1. netmsg.c:3581 — CLC_LOBBY_START encodes arena as net_hash

**File**: `port/src/net/netmsg.c:3574–3602`
**Pattern**:
```c
u32 netmsgClcLobbyStartWrite(..., u8 stagenum, ...) {
    ...
    catalogWritePreSessionRef(dst, catalogResolveArenaByStagenum((s32)stagenum));
    ...
    catalogWritePreSessionRef(dst, catalogResolveWeaponByGameId((s32)g_MpSetup.weapons[wi]));
}
```
**Violation**: Arena and weapons are encoded as `u32` net_hash (CRC32) via `catalogWritePreSessionRef`. Net_hash is deprecated per the mandate. Should be encoded as catalog ID strings (null-terminated, length-prefixed).
**Boundary**: Wire — client → server
**Replace with**: Transmit `entry->id` as string; server resolves via `assetCatalogResolve(id)`.

---

### C-2. netmsg.c:3803, 3818 — CLC_LOBBY_START decodes arena/weapons via net_hash

**File**: `port/src/net/netmsg.c:3803–3820`
**Pattern**:
```c
const asset_entry_t *stage_entry = catalogReadPreSessionRef(src);
...
const asset_entry_t *we = catalogReadPreSessionRef(src);
```
**Violation**: Server reads `u32` net_hash from wire and resolves locally. Paired with C-1.
**Boundary**: Wire — server receives
**Replace with**: Read length-prefixed catalog ID string, call `assetCatalogResolve(id)`.

---

### C-3. netmsg.c:4164, 4174 — SVC_LOBBY_STATE encodes/decodes arena as net_hash

**File**: `port/src/net/netmsg.c:4153–4175`
**Pattern**:
```c
catalogWritePreSessionRef(dst, catalogResolveArenaByStagenum((s32)stagenum));  // write
const asset_entry_t *stage_entry = catalogReadPreSessionRef(src);              // read
```
**Violation**: Arena identity transmitted as `u32` net_hash in SVC_LOBBY_STATE.
**Boundary**: Wire — server → client
**Replace with**: Transmit catalog ID string directly.

---

### C-4. netdistrib.c — Component distribution uses net_hash as primary wire identity

**File**: `port/src/net/netdistrib.c:65, 83, 287, 361, 382, 448, 470, 631, 683, 726`
**Pattern**:
```c
u32 net_hash;                          // queue entry identity
streamComponentToClient(cl, net_hash); // dispatch by hash
assetCatalogResolveByNetHash(net_hash) // resolution
memcpy(p, &net_hash, 4);              // written to wire
```
**Violation**: The entire component distribution subsystem uses `u32` net_hash as the primary identity for assets on the wire. The `id` string is carried alongside (line 631) but hash is primary for lookup and dedup.
**Boundary**: Wire — both directions
**Replace with**: Use catalog ID string as primary wire key; net_hash lookup can be a local index into the catalog but must not cross the wire.

---

### C-5. netmanifest.c:258–344 — Manifest uses net_hash for dedup, fingerprint, wire

**File**: `port/src/net/netmanifest.c:258–344`
**Pattern**:
```c
void manifestAddEntry(match_manifest_t *m, u32 net_hash, const char *id, ...) {
    /* Dedup by net_hash — skip if already present */
    if (m->entries[i].net_hash == net_hash) return;
    e->net_hash = net_hash;
}
// Fingerprint hash feeds net_hash bytes:
h ^= (u8)(e->net_hash); h *= 0x01000193u;
```
**Violation**: Manifest deduplication and integrity fingerprint are built on `net_hash`. When the manifest is transmitted/compared, hash mismatch detection is based on CRC32 of catalog IDs rather than the strings themselves.
**Boundary**: Wire (manifest is broadcast/compared between client and server)
**Replace with**: Dedup and fingerprint by catalog ID string; drop net_hash from manifest entries.

---

### C-6. sessioncatalog.c:74, 167–169 — Session catalog broadcast uses net_hash

**File**: `port/src/net/sessioncatalog.c:74, 167–169`
**Pattern**:
```c
e->net_hash = ae->net_hash;                     // populate from asset entry
...
local = assetCatalogResolveByNetHash(net_hash); // primary resolution
if (!local && e->catalog_id[0]) {
    local = assetCatalogResolve(e->catalog_id); // string fallback
}
```
**Violation**: Session catalog entries are keyed by `net_hash` in the wire format. String is carried as a fallback but hash is primary.
**Boundary**: Wire — server broadcasts session catalog
**Replace with**: Key session catalog entries by catalog ID string; remove net_hash from wire format.

---

### C-7. scenario_save.c:267 — Raw integer arena index written to disk

**File**: `port/src/scenario_save.c:267, 387–408`
**Pattern**:
```c
// WRITE
fprintf(fp, "  \"arena\": %u,\n", (unsigned)g_MatchConfig.stagenum);  // raw int written
const char *stage_id = catalogResolveStageByStagenum(...);
fprintf(fp, "  \"arenaId\": \"...\",\n");                              // string also written

// READ (fallback)
jsonFindInt(buf, "arena", &arena);           // reads raw int
if (arena >= 0) g_MatchConfig.stagenum = (u8)arena;  // stored if string fails
```
**Violation**: Raw integer `stagenum` written to disk as `"arena"` field alongside the string `"arenaId"`. On load, if catalog resolution of the string fails, the raw integer is used directly. This means old saves or saves from a different catalog state will load integer arena identity from disk.
**Boundary**: Disk — scenario save files
**Replace with**: Write only `"arenaId"` string. On load, if resolution fails, use a safe default rather than an arbitrary integer from disk.

---

### C-8. savefile.c:790–793, 866 — Raw weapon integers written to and read from disk

**File**: `port/src/savefile.c:781–794, 866`
**Pattern**:
```c
// WRITE — both string and raw int written
fprintf(fp, "  \"weapon_ids\": [...]");   // catalog ID strings (new format)
fprintf(fp, "  \"weapons\": [");
fprintf(fp, "%u%s", g_MpSetup.weapons[i], ...);  // raw MPWEAPON_* ints also written

// READ fallback
g_MpSetup.weapons[i] = s_tok_int(&tok);  // line 866 — raw int fallback
```
**Violation**: Raw `MPWEAPON_*` integer enum values are written to disk alongside strings, and read back directly as a fallback.
**Boundary**: Disk — MP setup save files
**Replace with**: Write only `"weapon_ids"` strings. Remove legacy `"weapons"` write. On read fallback failure, use a safe default weapon.

---

## HIGH — Runtime Struct Integer Identity

Asset identity stored as integer in a runtime struct where it should be a catalog ID string. These don't cross wire/disk today in integer form (because conversion functions are called at boundaries), but block the full catalog-native migration and create fragile conversion points.

---

### H-1. matchsetup.h:58 — `matchconfig.stagenum` is u8, no string counterpart

**File**: `port/include/net/matchsetup.h:55–70`
**Pattern**:
```c
struct matchconfig {
    ...
    u8 stagenum;   /* stage index */
    ...
};
```
**Violation**: The match config struct has no `stage_id[CATALOG_ID_LEN]` string field. Stage identity in g_MatchConfig is integer-only. All UI code (room.cpp, matchsetup.cpp) and network code (netmsgClcLobbyStartWrite) convert from this integer at the last moment. Compare: matchslot properly has both `body_id`/`head_id` strings AND derived `bodynum`/`headnum` integers.
**Boundary**: Runtime — persisted across menu transitions; fed into CLC_LOBBY_START
**Replace with**: Add `char stage_id[CATALOG_ID_LEN]` as PRIMARY identity in matchconfig. Keep `stagenum` as DERIVED (set at matchStart from `stage_id` lookup). Mirror the matchslot body_id/head_id pattern.

---

### H-2. pdgui_bridge.c:611, 633 — `netLobbyRequestStart`/`WithSims` API takes u8 stagenum

**File**: `port/fast3d/pdgui_bridge.c:611–637`
**Pattern**:
```c
s32 netLobbyRequestStartWithSims(u8 gamemode, u8 stagenum, ...);
s32 netLobbyRequestStart(u8 gamemode, u8 stagenum, u8 difficulty);
```
**Violation**: Public API for initiating a match takes `u8 stagenum` instead of a catalog ID string. Callers (pdgui_menu_room.cpp:1744, 1761, 1766) pass raw integer stage indices.
**Boundary**: Internal C API boundary between UI and net layer
**Replace with**: Accept `const char *stage_id` or `const char *arena_id`. Let the function resolve to stagenum internally for the CLC_LOBBY_START encoding.

---

### H-3. mplayer.c:765–778, 826–827, 838 — Default player/bot identity set via MPBODY_*/MPHEAD_*/STAGE_* constants

**File**: `src/game/mplayer/mplayer.c:765–778, 826–838`
**Pattern**:
```c
g_PlayerConfigsArray[playernum].base.mpbodynum = MPBODY_DARK_COMBAT;
g_PlayerConfigsArray[playernum].base.mpbodynum = MPBODY_CASSANDRA;
g_PlayerConfigsArray[playernum].base.mpbodynum = MPBODY_CARRINGTON;
g_BotConfigsArray[index].base.mpheadnum        = MPHEAD_DARK_COMBAT;
g_BotConfigsArray[index].base.mpbodynum        = MPBODY_DARK_COMBAT;
g_MpSetup.stagenum                             = STAGE_MP_SKEDAR;
```
**Violation**: Player and bot defaults are set using integer enum constants directly, without going through the catalog. If the catalog mapping of these constants changes (e.g., a mod replaces an asset), these defaults silently point to wrong assets.
**Boundary**: Runtime struct — these values propagate to wire on match start
**Replace with**: Resolve defaults by catalog ID string: `catalogBodynumToMpBodyIdx(assetCatalogResolve("base:dark_combat")->runtime_index)`. Or store defaults as catalog ID strings in a config table.

---

### H-4. mplayer.c:286–302, setup.c passim — `g_MpSetup.stagenum` as primary stage identity

**File**: `src/game/mplayer/mplayer.c:197, 286–302, 522, 527`
**Pattern**:
```c
stagenum = g_MpSetup.stagenum;
if (g_MpSetup.stagenum == STAGE_MP_RANDOM)  stagenum = mpChooseRandomStage();
else if (g_MpSetup.stagenum == STAGE_MP_RANDOM_MULTI) ...
g_MpSetup.stagenum = (u8)stagenum;
titleSetNextStage(stagenum);
mainChangeToStage(stagenum);
```
**Violation**: `g_MpSetup.stagenum` is read, mutated (random resolution), and used to drive stage loading. No catalog ID string is consulted at any point in this path. The stage is fully identified by an integer that originated from UI without catalog resolution.
**Boundary**: Runtime — stage loading is a critical transition
**Replace with**: Add `char stage_id[CATALOG_ID_LEN]` to `struct mpsetup`. Random resolution should pick a catalog entry by ID, not by integer. Derive stagenum from the ID just before calling `titleSetNextStage`.

---

### H-5. savefile.c:834 — Legacy stagenum integer fallback in MP setup load

**File**: `port/src/savefile.c:832–834`
**Pattern**:
```c
} else if (strcmp(key, "stagenum") == 0) {
    /* SA-4 v1 fallback */
    tok = s_next(&p); g_MpSetup.stagenum = s_tok_int(&tok);
}
```
**Violation**: Old save files with `"stagenum"` key (raw integer) will set `g_MpSetup.stagenum` directly from disk. If catalog resolution of the associated `stage_id` fails, this fallback is the only recovery path — but it relies on the old integer mapping being stable.
**Boundary**: Disk — MP setup load
**Replace with**: Convert integer to catalog ID at load time via `catalogResolveStageByStagenum()`; store the string. Drop the raw integer assignment.

---

### H-6. savefile.c:693–698 — Legacy mpheadnum/mpbodynum integer fallback

**File**: `port/src/savefile.c:693–698`
**Pattern**:
```c
} else if (strcmp(key, "mpheadnum") == 0) {
    /* SA-4 v1 fallback: legacy integer field */
    tok = s_next(&p); pc->base.mpheadnum = s_tok_int(&tok);
} else if (strcmp(key, "mpbodynum") == 0) {
    /* SA-4 v1 fallback: legacy integer field */
    tok = s_next(&p); pc->base.mpbodynum = s_tok_int(&tok);
```
**Violation**: Old saves with raw integer body/head nums load without catalog resolution. The integer is stored directly into the player config.
**Boundary**: Disk — player config load
**Replace with**: Convert via `catalogResolveBodyByMpIndex()`/`catalogResolveHeadByMpIndex()` at load time, storing the resolved string ID, then re-derive mpbodynum/mpheadnum from that.

---

## MEDIUM — Array Access / UI Integer Identity / Public API Integer Leakage

These work today but are fragile. They directly couple non-catalog code to the integer index spaces and block catalog-native migration.

---

### M-1. assetcatalog.h:671, 680, 689 — Public result structs expose raw integers

**File**: `port/include/assetcatalog.h:~671, ~680, ~689`
**Pattern**:
```c
typedef struct { ...; s32 stagenum;   ... } catalog_stage_result_t;
typedef struct { ...; s32 weapon_num; ... } catalog_weapon_result_t;
typedef struct { ...; s32 prop_type;  ... } catalog_prop_result_t;
```
**Violation**: Public catalog API result structs expose `stagenum`, `weapon_num`, and `prop_type` as raw integers. External callers can bypass the string identity and use these integers directly for asset lookup.
**Boundary**: Public C API
**Replace with**: These fields are acceptable as convenience accessors for the game engine's internal arrays (the catalog must map to them eventually). Document explicitly that `entry->id` is the canonical identity; integer fields are engine-internal values only. Rename them to make the semantics unambiguous: `engine_stagenum`, `engine_weapon_id`, `engine_prop_type`.

---

### M-2. pdgui_menu_matchsetup.cpp:1041, 1130–1138 — UI selection state stored as stagenum integer

**File**: `port/fast3d/pdgui_menu_matchsetup.cpp:1041, 1130–1138`
**Pattern**:
```c
if (s_ArenaGroupCache[g].entries[a]->ext.arena.stagenum == (s16)s_ArenaIndex) ...
bool isSel  = (ae->ext.arena.stagenum == (s16)s_ArenaIndex);
bool isHov  = (ae->ext.arena.stagenum == (s16)s_ArenaModalHover);
s_ArenaIndex = ae->ext.arena.stagenum;
g_MatchConfig.stagenum = (u8)ae->ext.arena.stagenum;
```
**Violation**: UI selection state (`s_ArenaIndex`, `s_ArenaModalHover`) is tracked as a raw stagenum integer. The arena picker compares catalog entry stagenums to find the selected item. This works only because stagenums happen to be unique, but if/when two catalog entries map to the same stagenum (e.g., variants), selection will break.
**Boundary**: UI → Runtime struct
**Replace with**: Track selection by catalog ID string: `s_ArenaId[CATALOG_ID_LEN]`. Compare `strcmp(ae->id, s_ArenaId)` for selection. Write `stage_id` (once added to matchconfig) instead of `stagenum`.

---

### M-3. pdgui_menu_room.cpp:1203, 1729, 1744, 1761, 1766 — Stage selection and match start via stagenum

**File**: `port/fast3d/pdgui_menu_room.cpp:1203, 1729–1768`
**Pattern**:
```c
g_MatchConfig.stagenum = (u8)s_Arenas[ai].stagenum;          // arena click
g_MatchConfig.stagenum = (u8)s_Arenas[s_SelectedArena].stagenum;  // match start
netLobbyRequestStartWithSims(GAMEMODE_MP, (u8)s_Arenas[s_SelectedArena].stagenum, ...);
netLobbyRequestStart(GAMEMODE_COOP, s_Missions[s_CampaignMission].stagenum, ...);
```
**Violation**: UI writes raw stagenum to `g_MatchConfig.stagenum` and passes it directly to the net layer. No catalog ID string is set.
**Boundary**: UI → Runtime struct → Net API
**Replace with**: After H-1 (stage_id in matchconfig) and H-2 (API takes string) are fixed, write `stage_id` and pass `ae->id` to the net API.

---

### M-4. pdgui_menu_matchsetup.cpp:748–753 — Bot body/head assignment uses raw array index

**File**: `port/fast3d/pdgui_menu_matchsetup.cpp:748–753`
**Pattern**:
```c
bool isSel = (bot->bodynum == (u8)b);
if (ImGui::Selectable(itemLabel, isSel)) {
    bot->bodynum = (u8)b;
    bot->headnum = (u8)b;
```
**Violation**: Character selection loop variable `b` (0..N) is written directly to `matchslot.bodynum/headnum`. While these fields are documented as DERIVED, the write path bypasses the catalog — it doesn't set `body_id`/`head_id` strings first.
**Boundary**: UI → Runtime struct
**Replace with**: Set `bot->body_id`/`bot->head_id` from the catalog entry name (`mpGetBodyCatalogId(b)`). Derive bodynum/headnum from the string at matchStart, not at selection time.

---

### M-5. pdgui_menu_room.cpp:1850–1865 — Lobby slot body/head assignment via index

**File**: `port/fast3d/pdgui_menu_room.cpp:1850–1865`
**Pattern**:
```c
bool sel = (b == (u32)sl->bodynum);
if (ImGui::Selectable(itemLabel, sel)) {
    sl->bodynum = (u8)b;
    sl->headnum = (u8)b;
```
**Violation**: Same pattern as M-4, in the lobby character picker.
**Boundary**: UI → Runtime struct
**Replace with**: Same as M-4.

---

### M-6. bg.c:1027–1057, 1251–1257 — Stage environment control uses raw stagenum comparisons

**File**: `src/game/bg.c:974, 1027–1057, 1251–1257`
**Pattern**:
```c
s32 stagenum = g_Vars.stagenum;
if (stagenum == g_Stages[STAGEINDEX_INFILTRATION].id || ...
s32 bgGetStageIndex(s32 stagenum) {
    for (i = 0; i < g_NumStages; i++)
        if (g_Stages[i].id == stagenum) return i;
}
```
**Violation**: `bg.c` uses raw stagenum integers to control environment setup. `bgGetStageIndex` is a reverse lookup from stagenum → array index. This is fine for engine-internal code, but the function takes and returns raw integers, making it a point where callers can obtain array indices.
**Boundary**: Internal API (not boundary-crossing) but tightly coupled to integer space
**Note**: This is deep game engine code — lower priority than the boundary violations above. The `g_Vars.stagenum` field is the engine's authoritative stage ID and is not expected to be a catalog ID string. Flag for future review if the stage loading pipeline is ever fully catalog-native.

---

### M-7. mplayer.c:2841–2910 — mpGetHeadId / mpGetBodyId / mpGetBodyName take u8 integer

**File**: `src/game/mplayer/mplayer.c:2841–2910`
**Pattern**:
```c
s32 mpGetHeadId(u8 headnum)   { return modmgrGetHead(headnum)->headnum; }
s32 mpGetBodyId(u8 bodynum)   { return modmgrGetBody(bodynum)->bodynum; }
char *mpGetBodyName(u8 mpbodynum) { return langGet(modmgrGetBody(mpbodynum)->name); }
```
**Violation**: These public C API functions accept `u8` integer parameters for asset lookup. Any caller that uses these must have the integer in hand, creating a dependency on integer identity throughout the codebase.
**Boundary**: C API
**Replace with**: Add `mpGetBodyNameById(const char *catalog_id)` variants. Keep old functions as shims calling `assetCatalogResolve(id)->runtime_index` → existing lookup. Gradually migrate callers to string-based API.

---

## LOW — Hardcoded Constants, Debug Display

These don't cross boundaries in integer form but introduce hidden coupling to the integer index space.

---

### L-1. mplayer.c:765–778 — Hardcoded MPBODY_*/MPHEAD_* enum defaults (duplicates H-3)

See H-3. Integer enum constants like `MPBODY_DARK_COMBAT = 0`, `MPHEAD_DARK_COMBAT = 0` are used as defaults without catalog lookup.

---

### L-2. pdgui_menu_mainmenu.cpp:1805, 1963 — Debug catalog browser sorts/displays net_hash

**File**: `port/fast3d/pdgui_menu_mainmenu.cpp:1805, 1886, 1963, 1996`
**Pattern**:
```c
case 4: cmp = (a->net_hash < b->net_hash) ? -1 : ...;
ImGui::TextDisabled("%08X", e->net_hash);
```
**Violation**: Debug UI exposes net_hash as a column for sorting and display. This is informational only (no behavior depends on it) but reinforces net_hash as a user-visible identity.
**Boundary**: UI (debug only)
**Replace with**: Remove net_hash column from the debug catalog browser once the wire migration is complete. Until then, low priority.

---

### L-3. scenario_save.c:267 — Dual-write pattern (also CRITICAL C-7)

See C-7. The raw integer `"arena"` is written alongside the string `"arenaId"`. The LOW aspect is that this pattern is intentional backward compat — the CRITICAL aspect is that the raw integer is still being persisted.

---

### L-4. ingame.c:460 — Hardcoded STAGE_ATTACKSHIP comparison

**File**: `src/game/mplayer/ingame.c:460`
**Pattern**:
```c
if (g_Menus[g_MpPlayerNum].training.weaponnum == WEAPON_NECKLACE && g_Vars.stagenum == STAGE_ATTACKSHIP)
```
**Violation**: Hardcoded stage constant in game logic. Fine for engine-internal code — the engine's stage ID space is integers. Not a catalog boundary violation.
**Note**: No action needed unless stage loading is made catalog-native end-to-end.

---

## OK — Catalog Internals (Documented, Not Violations)

The following are permitted internal uses inside the catalog module:

| File | Pattern | Reason |
|------|---------|--------|
| `assetcatalog_base.c:421, 459–462, 472–529` | `g_Stages[]`, `g_MpBodies[]`, `g_MpHeads[]`, `g_HeadsAndBodies[]` array access | Catalog registration — legitimate data source |
| `assetcatalog_api.c:371–404` | `g_MpBodies[mpbodynum].bodynum`, `g_MpHeads[mpheadnum].headnum` | `catalogResolveBodyByMpIndex` / `catalogHeadnumToMpHeadIdx` internals |
| `assetcatalog_api.c:438–451` | `catalogWritePreSessionRef` / `catalogReadPreSessionRef` implementation | Catalog internal wire helpers (the callers are the violation, not these) |
| `assetcatalog_base.c:472` | `e->runtime_index = g_MpBodies[idx].bodynum` | Setting derived runtime_index during registration |
| `port/src/assetcatalog_base_extended.c` | Model/prop registration via `g_ModelStates[i].fileid` | Standard catalog registration |
| `port/src/net/netmsg.c:3996–4003` | `manifestAddEntry(..., be->net_hash, be->id, ...)` | Server builds manifest from catalog entries — the manifest system itself is the violation (M-5 / C-5) |
| `matchsetup.h:42–49` | `matchslot.bodynum/headnum` labeled DERIVED | Explicitly documented dual-identity pattern; PRIMARY strings exist. |
| `net.c:320–325` | `catalogResolveBodyByMpIndex((s32)mpbodynum)` | Correct use of index→string conversion at boundary |
| `savefile.c:604–610` | `catalogResolveBodyByMpIndex(pc->base.mpbodynum)` → writes string | Correct save path |
| `savefile.c:772–773` | `catalogResolveStageByStagenum` → writes string | Correct save path |

---

## Migration Priority

| Priority | Finding | Effort |
|----------|---------|--------|
| **1 (now)** | C-7: scenario_save dual-write — stop writing raw `"arena"` int | Trivial |
| **2 (now)** | C-8: savefile dual-write — stop writing raw `"weapons"` array | Trivial |
| **3 (high)** | H-1: Add `stage_id` to matchconfig struct | Small — mirrors matchslot pattern |
| **4 (high)** | H-2: Change `netLobbyRequestStart` API to accept stage_id string | Small |
| **5 (high)** | H-3/H-4: Replace MPBODY_*/STAGE_* defaults with catalog lookups | Medium |
| **6 (medium)** | M-2/M-3: UI arena picker tracks by catalog ID | Medium |
| **7 (medium)** | M-4/M-5: Bot/slot character selection writes string IDs | Medium |
| **8 (medium)** | C-1–C-6: Replace net_hash wire protocol with catalog ID strings | Large — protocol version bump required |
| **9 (low)** | H-5/H-6: Remove legacy integer fallbacks in save load | Small — break old saves intentionally |
| **10 (low)** | M-1: Rename result struct integer fields for clarity | Trivial rename |
| **11 (future)** | M-6/M-7: Migrate bg.c / mpGetBodyName to string API | Large — engine-deep |

---

## Summary Table

| ID | File | Lines | Severity | Boundary | Pattern |
|----|------|-------|----------|----------|---------|
| C-1 | netmsg.c | 3574–3602 | CRITICAL | Wire | arena/weapons as net_hash in CLC_LOBBY_START write |
| C-2 | netmsg.c | 3803, 3818 | CRITICAL | Wire | arena/weapons decoded from net_hash in CLC_LOBBY_START read |
| C-3 | netmsg.c | 4164, 4174 | CRITICAL | Wire | arena as net_hash in SVC_LOBBY_STATE |
| C-4 | netdistrib.c | 65, 287, 361, 382, 448, 470 | CRITICAL | Wire | net_hash as primary component identity |
| C-5 | netmanifest.c | 258–344 | CRITICAL | Wire | net_hash dedup and fingerprint in manifest |
| C-6 | sessioncatalog.c | 74, 167–169 | CRITICAL | Wire | net_hash primary in session catalog broadcast |
| C-7 | scenario_save.c | 267, 407–408 | CRITICAL | Disk | raw arena integer written/read as fallback |
| C-8 | savefile.c | 790–793, 866 | CRITICAL | Disk | raw weapon integers written/read as fallback |
| H-1 | matchsetup.h | 58 | HIGH | Runtime | matchconfig.stagenum u8, no stage_id string |
| H-2 | pdgui_bridge.c | 611, 633 | HIGH | API | netLobbyRequestStart takes u8 stagenum |
| H-3 | mplayer.c | 765–778, 826–827 | HIGH | Runtime | MPBODY_*/MPHEAD_* constant defaults |
| H-4 | mplayer.c | 286–302, 527 | HIGH | Runtime | g_MpSetup.stagenum as primary identity |
| H-5 | savefile.c | 832–834 | HIGH | Disk | legacy stagenum int fallback in load |
| H-6 | savefile.c | 693–698 | HIGH | Disk | legacy mpheadnum/mpbodynum int fallback |
| M-1 | assetcatalog.h | ~671, ~680, ~689 | MEDIUM | API | result structs expose stagenum/weapon_num/prop_type |
| M-2 | pdgui_menu_matchsetup.cpp | 1041, 1130–1138 | MEDIUM | UI→Runtime | s_ArenaIndex tracks by stagenum int |
| M-3 | pdgui_menu_room.cpp | 1203, 1729, 1744, 1761 | MEDIUM | UI→Net API | stagenum written to matchconfig, passed to net |
| M-4 | pdgui_menu_matchsetup.cpp | 748–753 | MEDIUM | UI→Runtime | bot bodynum/headnum written without catalog |
| M-5 | pdgui_menu_room.cpp | 1850–1865 | MEDIUM | UI→Runtime | slot bodynum/headnum written without catalog |
| M-6 | bg.c | 974, 1027–1057, 1251–1257 | MEDIUM | Internal | stagenum comparisons and bgGetStageIndex(s32) |
| M-7 | mplayer.c | 2841–2910 | MEDIUM | C API | mpGetHeadId/mpGetBodyId/mpGetBodyName take u8 |
| L-1 | mplayer.c | 765, 826 | LOW | Runtime | MPBODY_*/MPHEAD_* enum literals (see H-3) |
| L-2 | pdgui_menu_mainmenu.cpp | 1805, 1963 | LOW | UI | debug catalog browser displays net_hash |
| L-3 | scenario_save.c | 267 | LOW | Disk | dual-write pattern (see C-7) |
| L-4 | ingame.c | 460 | LOW | Internal | STAGE_ATTACKSHIP comparison in game logic |
