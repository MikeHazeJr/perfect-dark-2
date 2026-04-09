# Audit: Legacy ROM Array Direct Accesses

**Date**: 2026-04-06
**Session**: Phase 8 post-elimination audit
**Scope**: All `g_HeadsAndBodies[]`, `g_MpBodies[]`, `g_MpHeads[]`, `g_Weapons[]` direct accesses in `port/src/`, `port/fast3d/`, `src/game/`

## Executive Summary

| Array | Total Accesses | INTERNAL | RENDER-LAST-MILE | BYPASS |
|-------|---------------|----------|-------------------|--------|
| g_HeadsAndBodies[] | 93 | 19 | 68 | 6 |
| g_MpBodies[] | 25 | 20 | 0 | 5 |
| g_MpHeads[] | 23 | 18 | 0 | 5 |
| g_Weapons[] | 25 | 14 | 10 | 1 |
| **TOTAL** | **166** | **71** | **78** | **17** |

**INTERNAL** = Catalog implementation (registration, cache building, data init). Expected; no action needed.
**RENDER-LAST-MILE** = Engine consuming data AFTER catalog resolution for rendering/physics. Expected; no action needed.
**BYPASS** = Accessing array for identity/logic without going through catalog. **These are gaps.**

---

## BYPASS Findings (17 total — action items)

### CRITICAL: savefile.c — O(n) scan duplicating catalogGetMpIndex (2 sites)

These perform the EXACT same linear scan that `catalogGetMpIndex()` did, re-implementing the removed function inline.

**savefile.c:690-694** — g_MpHeads scan
```c
for (s32 hi = 0; hi < ARRAYCOUNT(g_MpHeads); hi++) {
    if ((s32)g_MpHeads[hi].headnum == (s32)e->runtime_index) {
        pc->base.mpheadnum = (u8)hi;
        break;
    }
}
```
**Fix**: Replace with `pc->base.mpheadnum = (e->mp_index >= 0) ? (u8)e->mp_index : 0;`

**savefile.c:711-715** — g_MpBodies scan
```c
for (s32 bi = 0; bi < ARRAYCOUNT(g_MpBodies); bi++) {
    if ((s32)g_MpBodies[bi].bodynum == (s32)e->runtime_index) {
        pc->base.mpbodynum = (u8)bi;
        break;
    }
}
```
**Fix**: Replace with `pc->base.mpbodynum = (e->mp_index >= 0) ? (u8)e->mp_index : 0;`

### LOW: chr.c — Direct write to g_HeadsAndBodies (1 site)

**chr.c:4912** — Anti-piracy checksum zeroes SKEDARKING filenum
```c
g_HeadsAndBodies[BODY_SKEDARKING].filenum = 0;
```
**Category**: BYPASS — direct mutation of ROM array for anti-piracy logic.
**Note**: Legacy anti-cheat code. Harmless but technically a direct array mutation outside the catalog. Not worth refactoring — the anti-piracy system is a historical artifact.

### LOW: game_0b0fd0.c — weaponFindById returns raw array element (1 site)

**game_0b0fd0.c:29** — Returns `g_Weapons[itemid]` directly
```c
return g_Weapons[itemid];
```
**Category**: BYPASS — weapon identity lookup via raw integer index.
**Note**: This is the root weapon lookup function. All weapon consumers call this. The weapon catalog (future Phase 9+) would replace this with `assetCatalogResolve(weapon_id)`. Not actionable until weapon catalog migration.

### INFO: Comments referencing legacy patterns (13 sites)

These are not code accesses — they are comments or extern declarations. Included for completeness.

- **savefile.c:678** — Comment: "runtime_index is g_HeadsAndBodies[] index"
- **savefile.c:699** — Comment: same
- **identity.c:226** — Comment: "legacy integer indices from g_MpHeads[]/g_MpBodies[]"
- **modmgr.c:1283** — Comment: registration order for g_MpBodies cache
- **modmgr.c:1324** — Comment: registration order for g_MpHeads cache
- **netmanifest.c:501** — Comment: g_MpBodies zeroed on dedicated server
- **netmsg.c:1073** — Comment: bodyreset() NULLs g_HeadsAndBodies[].modeldef
- **modelcatalog.c:635** — Comment: runtime_index mapping
- **modelcatalog.c:657** — Comment: runtime_index mapping
- **pdgui_menu_agentcreate.cpp:83-84** — extern declarations (g_MpBodies, g_MpHeads)
- **pdgui_menu_room.cpp:112-113** — extern declarations (g_MpBodies, g_MpHeads)

---

## INTERNAL Accesses (71 total — no action needed)

### g_HeadsAndBodies[] — INTERNAL (19)

| File | Count | Purpose |
|------|-------|---------|
| assetcatalog_base.c | 8 | Reads bodynum/headnum/filenum during base game catalog registration |
| modelcatalog.c | 3 | Metadata caching loop, lazy validation, entry counting |
| bodyreset.c | 2 | Clears all modeldef pointers on stage unload |
| assetcatalog_api.c | 2 | Comments describing server stub behavior |
| server_stubs.c | 1 | Zero-initialized array declaration for server build |
| robot.c | 1 | Array definition — the static data table itself |

### g_MpBodies[] — INTERNAL (20)

| File | Count | Purpose |
|------|-------|---------|
| assetcatalog_base.c | 7 | Reads fields during base game body registration + unregistered entry detection |
| assetcatalog_api.c | 1 | Reads bodynum to build mp body runtime cache in catalogBuildRuntimeCaches() |
| server_stubs.c | 1 | Array declaration |
| pdgui_menu_agentcreate.cpp | 1 | extern declaration |
| pdgui_menu_room.cpp | 1 | extern declaration |
| mplayer.c | 1 | Array definition (static data table) |

### g_MpHeads[] — INTERNAL (18)

| File | Count | Purpose |
|------|-------|---------|
| assetcatalog_base.c | 5 | Reads fields during base game head registration + unregistered entry detection |
| assetcatalog_api.c | 1 | Reads headnum to build mp head runtime cache in catalogBuildRuntimeCaches() |
| server_stubs.c | 1 | Array declaration |
| pdgui_menu_agentcreate.cpp | 1 | extern declaration |
| pdgui_menu_room.cpp | 1 | extern declaration |
| mplayer.c | 1 | Array definition (static data table) |

### g_Weapons[] — INTERNAL (14)

| File | Count | Purpose |
|------|-------|---------|
| bondgunreset.c | 9 | Populates weapon name/shortname/flags from catalog data |
| game_0b0fd0.c | 1 | weaponGetFileNum — reads hi_model field for model file lookup |
| playerreset.c | 2 | Populates weapon name/shortname from catalog data |
| invitems.c | 1 | Array definition (static data table) |
| modelmgrreset.c | 1 | Iterates weapon functions during model definition loading |

---

## RENDER-LAST-MILE Accesses (78 total — no action needed)

### g_HeadsAndBodies[] — RENDER-LAST-MILE (68)

| File | Count | Purpose |
|------|-------|---------|
| body.c | 35 | Modeldef loading/caching, type classification, height/canvaryheight, ismale, unk00_01 flags for head attachment, animation, voice profile |
| player.c | 20 | Modeldef loading for 2-4 player setup, fallback model loading, height/eye-height calculation |
| chraction.c | 3 | ismale flag for animation and sound effect selection |
| bondgun.c | 1 | handfilenum for weapon/hand model |
| bot.c | 1 | height for speed calculation |
| botmgr.c | 1 | ismale flag for voice profile |
| menu.c | 1 | unk00_01 flag for head integration during menu loading |
| mplayer.c | 1 | ismale flag for random head gender selection |
| setup.c | 1 | unk00_01 flag for body model integration detection |

### g_Weapons[] — RENDER-LAST-MILE (10)

| File | Count | Purpose |
|------|-------|---------|
| game_0b0fd0.c | 7 | Weapon function pointers (fire, equip, unequip), animation IDs |
| bondgun.c | 3 | Weapon name/shortname for UI display via langGet() |

---

## Actionable Items

### Immediate (Phase 8 cleanup)

1. **savefile.c:690-694** — Replace g_MpHeads O(n) scan with `e->mp_index`. **Severity: HIGH** — this is a hidden `catalogGetMpIndex` reimplementation.
2. **savefile.c:711-715** — Replace g_MpBodies O(n) scan with `e->mp_index`. **Severity: HIGH** — same.

### Future Phases

3. **g_Weapons[] BYPASS (game_0b0fd0.c:29)** — weaponFindById returns raw array element. Blocked on weapon catalog migration (Phase 9+).
4. **g_HeadsAndBodies[] RENDER-LAST-MILE (68 sites)** — All body.c/player.c accesses read rendering metadata (modeldef, type, height, ismale). These are legitimate engine data accesses. Could be migrated to catalog entry metadata fields in a future phase, but they are NOT identity bypasses.
5. **chr.c:4912** — Anti-piracy artifact. No action.
