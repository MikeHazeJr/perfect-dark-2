# Catalog-Owned Asset Pipeline: Weapons Proving Domain (Phase 1 Design)

**Status:** BASELINE UPDATED. Weapon-only scope is superseded by the all-assets catalog-owned pipeline; this document remains the weapon proving-domain design.
**Date:** 2026-04-27
**Session:** S484 (`suspicious-napier-2c0224`)
**Companion roadmap entry:** Full-Release Roadmap E.2.
**Scope gate:** Weapons are first proving domain only. The target is all declared assets, references, source handles, loading/unloading, dependencies, refcounts, and payload ownership.

> Methodology gates respected: comprehensive Layer A audit before migration; possibility framing on findings; no half measures (every weapon-data Layer A read migrates); hierarchical log channels (`CATALOG.MGR.WEAPON.*`, `LOADER.PDBASE.*`); pd-tests cases land in same commit as the invariant they enforce; no em-dashes.
>
> S485 correction: post-GF64-cull counts are runtime `g_Weapons[WEAPON_SUICIDEPILL + 1]` = 86 entries and MP `g_MpWeapons[NUM_MPWEAPONS]` = 41 slots. The base catalog seed now covers the 41 MP selector slots exactly and splits MP slot identity from runtime weapon identity.

---

## A. Comprehensive Layer A Audit (Weapons)

This is the exhaustive list of every site that reads or writes weapon DATA in the live tree. "Weapon data" means the fields of `struct weapon` and the chained sub-structs (`struct weaponfunc`, `struct weaponfunc_shoot/single/auto/projectile/throw/melee/special/device`, `struct inventory_ammo`, `struct invaimsettings`, `struct noisesettings`, `struct recoilsettings`, `struct gunviscmd`, `struct modelpartvisibility`), plus the AI-bot weapon preference table (`struct aibotweaponpreference` in `g_AibotWeaponPreferences[]`).

Mike's directive: "without these secret gaps we discover weeks after migration". Every direct read or write is enumerated below. Tier-2 sites (callers of the canonical accessor `weaponFindById` that then chain through `weapon->field`) are listed explicitly so it is clear no caller is left out, but the migration plan in Section F migrates the canonical accessor once and inherits all tier-2 sites without per-site edits.

### A.1 Source-of-truth data (the rows that become `.pdbase`)

| Symbol | File:line | Notes |
|---|---|---|
| `struct weapon *g_Weapons[]` | `src/game/invitems.c:5700-5789` | 86 entries indexed by `WEAPON_*` enum (range 0x00 to 0x55). `WEAPON_NONE` and `WEAPON_UNARMED` at slots 0/1. Runtime droppable/MP gameplay weapons run through `WEAPON_COMBATBOOST` at 0x23 after the 8 GF64 imports were culled; device/item slots continue at 0x24..0x55. MP setup identity is a separate 41-slot `MPWEAPON_*` table, not this runtime index space. |
| `invitem_*` `struct weapon` definitions | `src/game/invitems.c` (whole file) | One `static struct weapon invitem_<name> = { ... };` per weapon. Field set is hi_model/lo_model, equip / unequip / pri-to-sec / sec-to-pri animation pointers, functions[2] (primary / secondary `weaponfunc *`), ammos[2] (`inventory_ammo *`), aimsettings (`invaimsettings *`), muzzlez / posx / posy / posz / sway floats, gunviscmds (`gunviscmd *`), partvisibility (`modelpartvisibility *`), shortname / name / manufacturer / description (langbank u16), flags (u32). |
| `invfunc_*` `struct weaponfunc_*` definitions | `src/game/invitems.c` | One per primary or secondary fire mode. Subclass varies (shoot single / shoot auto / shoot projectile / throw / melee / special / device). Fields include damage, spread, recoilsettings, recoverytime, duration, shootsound, penetration, projectilemodelnum, scale, speed, etc. |
| `invammo_*` `struct inventory_ammo` definitions | `src/game/invitems.c` | `type` (ammo enum), `casingeject`, `clipsize`, `reload_animation`, `flags`. |
| `invaimsettings_*` `struct invaimsettings` definitions | `src/game/invitems.c` | `zoomfov`, gun translation when aiming up / down / sideways, `aimdamp` / `aimdamppal`, `tracktype`, `flags`. |
| `invnoisesettings_*` `struct noisesettings` definitions | `src/game/invitems.c` | `minradius`, `maxradius`, `incradius`, `decbasespeed`, `decremspeed`. |
| `invrecoilsettings_*` `struct recoilsettings` definitions | `src/game/invitems.c` | `xrange`, `yrange`, `zrange`, `unk0c`, `unk10`. |
| Per-weapon `gunviscmd[]` arrays | `src/game/invitems.c` | Animation visibility command sequences (terminator 0). |
| Per-weapon `modelpartvisibility[]` arrays | `src/game/invitems.c` | Conditional model part show/hide tables. |
| Per-weapon `vibrationstart[]` / `vibrationmax[]` `f32` arrays | `src/game/invitems.c` | Used by full-auto fire (`weaponfunc_shootauto`). |
| `struct aibotweaponpreference g_AibotWeaponPreferences[]` | `src/game/botinv.c:22-128` | Indexed by `WEAPON_*`. Holds bot AI preference data: pri / sec scores at 4 ranges, has-pri-ammo-goal, has-sec-ammo-goal, distance configs, target / critical ammo counts, reload delay, allow-partial-reload-delay. **Same key space as `g_Weapons[]`.** |
| `extern struct invaimsettings invaimsettings_default` | `src/include/game/inv.h:8` | The fallback aim-settings record returned by `gsetGetAimSettings` when a weapon has no aim settings. Defined in `invitems.c`. |
| `extern struct noisesettings invnoisesettings_silent` | `src/include/game/inv.h:7` | The fallback noise-settings record returned by `gsetGetNoiseSettings` when a weapon function has no noise settings. Defined in `invitems.c`. |
| `extern struct weapon *g_Weapons[]` | `src/include/data.h:236`, `src/include/game/inv.h:9` | Two extern declarations. The `inv.h` form sizes the array as `[WEAPON_SUICIDEPILL + 1]` (86 after the GF64 cull; `WEAPON_SUICIDEPILL` is 0x55). |

### A.2 Layer A read sites (direct table access)

`g_Weapons[i]` direct reads (24 sites across 5 files). One of these is the canonical accessor `weaponFindById`, which is the single migration point. The remaining 23 are direct accesses that bypass the accessor and must each be addressed (mostly by routing through the migrated `weaponFindById`, but a few reads inside `game_0b0fd0.c` are intentional file-local optimisations that re-read the table after a `weaponFindById` already validated the index).

| File:line | Pattern | Notes |
|---|---|---|
| `src/game/game_0b0fd0.c:34` | `return g_Weapons[itemid];` | Body of the canonical accessor `weaponFindById`. **The single migration point.** |
| `src/game/game_0b0fd0.c:61` | `struct weapon *weapon = g_Weapons[gset->weaponnum];` | `gsetGetWeaponFunction` (no bounds check; relies on caller). |
| `src/game/game_0b0fd0.c:78` | same | `weaponGetFunction`. |
| `src/game/game_0b0fd0.c:407` | same (inside `if (weaponnum != -1)`) | `weaponGetFileNum`. |
| `src/game/game_0b0fd0.c:682` | same | `handGetEquipAnim`. |
| `src/game/game_0b0fd0.c:693` | same | `handGetUnequipAnim`. |
| `src/game/game_0b0fd0.c:704` | same | `gsetGetPriToSecAnim`. |
| `src/game/game_0b0fd0.c:715` | same | `gsetGetSecToPriAnim`. |
| `src/game/bondgun.c:1021` | `info->definition = g_Weapons[weaponnum];` | `bgunGetWeaponInfo` caches the weapon-data pointer in `struct handweaponinfo.definition` for ~20 downstream reads in bondgun.c (see A.3 for the cached-pointer reads). |
| `src/game/bondgun.c:6417` | `struct weapon *weapon = g_Weapons[weaponnum];` | `bgunGetName` (reads `weapon->name`). |
| `src/game/bondgun.c:6428` | same | `bgunGetNameId`. |
| `src/game/bondgun.c:6439` | same | `bgunGetShortName`. |
| `src/game/bondgunreset.c:264` | `g_Weapons[WEAPON_EYESPY]->name = ...;` | DrugSpy/BombSpy/CamSpy mutator (3 cheat states, 9 lines total). **Mutation site.** |
| `src/game/bondgunreset.c:265-279` | same pattern | (9 mutation lines covering name / shortname / flags). |
| `src/game/playerreset.c:98-99` | `g_Weapons[WEAPON_EYESPY]->name = ...;` | EYESPY name override during player reset (mirror of bondgunreset). **Mutation site.** |
| `src/game/modelmgrreset.c:194` | `weapon = g_Weapons[weaponnum];` | `modelmgrLoadProjectileModeldefs`. **Already has a B-263 bounds-check + LOG_WARNING via `WEAPON.SLOT.MISS:` channel** (S483c). Reads `weapon->functions[i]` to discover projectile model files. |

`g_AibotWeaponPreferences[i]` direct reads (28 sites across 2 files):

| File:line | Pattern | Notes |
|---|---|---|
| `src/game/bot.c:2455` | `g_AibotWeaponPreferences[chr->aibot->weaponnum].reloaddelay` | Reload-delay computation. |
| `src/game/bot.c:2457` | `.allowpartialreloaddelay` | |
| `src/game/bot.c:2465-2466` | `.haspriammogoal` / `.hassecammogoal` (HASENOUGHPRI / HASENOUGHSEC macros) | |
| `src/game/bot.c:2652` | `.haspriammogoal` / `.hassecammogoal` | Has-ammo decision. |
| `src/game/bot.c:2763-2764` | same | |
| `src/game/bot.c:2788, 2794, 2811, 2813, 2822, 2823, 2831, 2832` | `.criticalammopri/sec`, `.targetammopri/sec`, `.haspriammogoal`, `.hassecammogoal` | Ammo target selection. |
| `src/game/botinv.c:467-474` | `.haspriammogoal`, `.hassecammogoal`, `.unk00`, `.unk01`, `.unk02`, `.unk03` | Best-weapon score function. |
| `src/game/botinv.c:827, 830` | `.pridistconfig`, `.secdistconfig` | Distance config selector. |
| `src/game/botinv.c:843, 847` | `.pridistconfig`, `.secdistconfig` (compared to `BOTDISTCFG_CLOSE`) | |
| `src/game/botinv.c:961, 963` | `.haspriammogoal`, `.hassecammogoal` | Can-use weapon predicate. |

`invaimsettings_default` and `invnoisesettings_silent` extern reads (2 sites):

| File:line | Pattern |
|---|---|
| `src/game/game_0b0fd0.c:124` | `return &invaimsettings_default;` (in `gsetGetAimSettings` fallback) |
| `src/game/game_0b0fd0.c:670` | `settings = &invnoisesettings_silent;` (in `gsetGetNoiseSettings` fallback) |

### A.3 Tier-2 read sites (through `weaponFindById` accessor)

37 callers across 8 files. Listed for completeness; the migration plan in Section F migrates the accessor body once and these inherit the change without per-site edits.

| File | Sites | Notes |
|---|---|---|
| `src/game/game_0b0fd0.c` | 19 | Internal accessor module. Most accessors take a `weaponnum` and look up via `weaponFindById` then chain through `weapon->functions[i]`, `weapon->ammos[i]`, `weapon->aimsettings`, `weapon->equip_animation`, `weapon->unequip_animation`, `weapon->pritosec_animation`, `weapon->sectopri_animation`, `weapon->posx/posy/posz`, `weapon->name`, `weapon->shortname`, `weapon->flags`. |
| `src/game/bondgun.c` | 9 | `bgunCreateWeaponObj`, `bgunReload`, `bgunStartUnequip`, `bgunUpdateGunAimingMode`, `bgunFire`, `bgunStartCrouchAnim`, `bgunIsAutoFire`, `bgunGetSwayAmount`, `bgunGetClipMaxAmmo`. Reads `weapon->ammos[]`, `weapon->equip_animation`, `weapon->unequip_animation`, `weapon->functions[i]`, `weapon->aimsettings`, `weapon->sway`, `weapon->name`. |
| `src/game/mainmenu.c` | 3 | Cheats menu inventory preview, draws weapon name+description. Reads `weapon->name`, `weapon->shortname`, `weapon->description`, `weapon->manufacturer`, `weapon->flags`. |
| `src/game/mplayer/ingame.c` | 2 | Training screen weapon select, MP pause weapon swap. Reads `weapon->name`, `weapon->flags`. |
| `src/game/gunfx.c` | 1 | Tracer / muzzle-flash gating (`weapondef->flags`). |
| `src/game/botact.c` | 1 | Bot weapon-attack action. Reads `weapon->functions[]`, `weapon->ammos[]`. |
| `src/game/bot.c` | 1 | Bot weapon-pickup decision (`weaponFindById(weapon->weaponnum)` -- `weapon` here is `struct weaponobj`, not `struct weapon`, and is name-shadowed inside the call). |
| `src/game/chraction.c` | 1 | Chr animation gating from gset weapon. |

### A.4 Tier-2 read sites (through cached `info->definition`)

`struct handweaponinfo.definition` is a `struct weapon *` cached at `bgunGetWeaponInfo` (`bondgun.c:1021`). Downstream reads inside bondgun.c (~20 sites at lines 1120, 1570, 1572, 1576, 1592, 1657, 1658, 2053, 2082, 2091, 2579, 2709, 2897, 3045, 3046, 3253, 3524) read `info->definition->ammos[i]`, `->equip_animation`, `->functions[i]`, `->sway`, etc. Once the canonical accessor `weaponFindById` is migrated, the cached pointer is still correct (it points to the same manager-served `weapon_data_t`), so these sites need no change.

### A.5 Indirect access through `gset` and `weaponnum`

`gset->weaponnum` is the wire / save / runtime weapon identity (`u8` enum `WEAPON_*`). 35+ files read `gset->weaponnum` and pass it to a `weaponFindById` accessor. These are NOT Layer A reads of weapon data. They are identity-index passers and need no migration.

Files reading `gset->weaponnum` (informational; not migration targets):
`activemenu.c, bondgun.c, bondmove.c, bot.c, botact.c, botcmd.c, botinv.c, botmgr.c, chr.c, chraction.c, chraicommands.c, explosions.c, game_0b0fd0.c, gunfx.c, inv.c, lv.c, mpstats.c, mplayer/scenarios.c, mplayer/scenarios/holdthebriefcase.inc, mplayer/scenarios/hackthatmac.inc, mplayer/scenarios/capturethecase.inc, player.c, prop.c, propobj.c, setup.c, sight.c, training.c, port/src/forge/forge_runtime.c, port/src/net/net.c, port/src/net/netmsg.c, port/src/preprocess/filesetup.c`.

### A.6 Mutation sites

Three mutation sites exist and must continue to function under the manager:

1. `bondgunreset.c:264-279` and `playerreset.c:98-99` mutate `g_Weapons[WEAPON_EYESPY]->name / shortname / flags` to swap CamSpy / DrugSpy / BombSpy display per cheat state. Manager API must offer a mutator: `catalogManagerWeaponSetEyespyVariant(variant_enum)`. This is the only intra-frame mutation pattern in the audit.
2. `game_0b0fd0.c:155-158` `currentPlayerSetWeaponPos` writes `weapon->posx / posy / posz` for the current weapon. This is an in-place tweak of position offsets at runtime. Manager must allow mutation of these three floats on a per-weapon basis: `catalogManagerWeaponSetPositionOffset(weapon_id, x, y, z)`. Or we revisit whether this in-place write is actually needed (likely a dev tool; see A.7 surprise).

### A.7 Surprises and notes

- The `g_Weapons[]` array carries 86 runtime entries, while `g_MpWeapons[]` carries 41 MP selector/setup slots. These are separate numeric spaces. Slots 0x24 onward in the runtime table are devices/items/cutscene props (keycards, briefcase, suitcase, hammer, rocket, watchlaser, suicidepill, etc.). They have a `struct weapon` row because they share the inventory and gunctrl pipeline, but many have minimal weapon behavior. The manager must hold all 86 runtime entries; the base ASSET_WEAPON selector seed currently covers all 41 MP slots exactly (`port/src/assetcatalog_base_extended.c:s_BaseWeapons`) and resolves runtime handoff separately.
- The `g_AibotWeaponPreferences[]` table is keyed by `WEAPON_*` enum (same as `g_Weapons[]`) and shares the same 86-slot range. It must become weapon DATA owned by the manager, not a separate parallel table. Phase 2 folds it into the per-weapon `weapon_data_t` payload as a `bot_pref` sub-struct.
- `invaimsettings_default` and `invnoisesettings_silent` are two singleton fallback records. Manager API exposes them as `catalogManagerWeaponDefaultAimSettings()` / `catalogManagerWeaponDefaultNoiseSettings()` returning const pointers.
- `currentPlayerSetWeaponPos` writes in-place to the static data table. This was almost certainly a debug or position-tuning hook and is not invoked during normal gameplay. Phase 2 keeps the manager mutable for this case; if no live caller exists, removal is queued behind a separate audit.
- `modelmgrreset.c:194` already uses the `WEAPON.SLOT.MISS:` log channel pattern (loud-fail discipline from S483c, B-263). The new manager accessors adopt the same discipline under `CATALOG.MGR.WEAPON.MISS:`.

### A.8 Site-count summary

| Site class | Count | Files |
|---|---|---|
| Direct `g_Weapons[i]` read | 24 | 5 (game_0b0fd0.c, bondgun.c, bondgunreset.c, playerreset.c, modelmgrreset.c) |
| Direct `g_Weapons[i]` write (mutation) | 11 | 2 (bondgunreset.c, playerreset.c) plus `currentPlayerSetWeaponPos` writes |
| `g_AibotWeaponPreferences[i]` read | 28 | 2 (bot.c, botinv.c) |
| `invaimsettings_default` / `invnoisesettings_silent` extern read | 2 | 1 (game_0b0fd0.c) |
| Tier-2 `weaponFindById` callers | 37 | 8 (game_0b0fd0.c, bondgun.c, mainmenu.c, mplayer/ingame.c, gunfx.c, botact.c, bot.c, chraction.c) |
| Tier-2 cached `info->definition` chained reads | ~20 | 1 (bondgun.c) |
| Indirect `gset->weaponnum` passers | 35+ files | identity-only, not migrated |

Total Layer A migration surface: 65 read / write sites in 8 files, plus generating catalog-owned runtime weapon payloads for 86 `WEAPON_*` entries while preserving the 41-slot MP selector seed. Mike's stop condition "> 50 read sites" is hit at 65 if every site requires per-call edits, but Section F shows that 37 of those 65 inherit the change automatically through the canonical accessor, leaving 28 explicit edits. **Phasing strategy is needed only at the all-asset pipeline level; the weapon proving-domain accessor migration itself remains one bounded slice.**

---

## B. Catalog Row + Manager Schema

### B.1 Catalog row (lightweight MP selector identity, in `asset_entry_t.ext.weapon`)

The existing catalog row stays lightweight and identity-focused. S485 corrected its numeric field to be the MP selector/setup slot (`MPWEAPON_*`), not the runtime `WEAPON_*` enum. Full runtime weapon data and `.pdbase` source refs belong in a typed payload sidecar/manager, not in the MP selector row.

```
struct asset_entry.ext.weapon {
    s32  weapon_id;             /* MPWEAPON_* selector/setup slot */
    char name[64];              /* display name (UI / lobby) */
    char model_file[128];       /* primary model file path (resolved from .pdbase or ROM) */
    f32  damage;                /* HEADLINE damage value (0 = data lives in manager only) */
    f32  fire_rate;             /* HEADLINE fire rate (0 = data lives in manager only) */
    s32  ammo_type;             /* HEADLINE ammo category (-1 = data lives in manager only) */
    s32  dual_wieldable;        /* bool */
    u8   requirefeature;        /* unlock check */
};
```

Note: `damage`, `fire_rate`, `ammo_type` retain their existing semantics as headline / UI metadata. The full damage curve, ammo enum, source handle, `.pdbase` offset/size, loaded payload, refcount, and unload behavior live in the manager's typed runtime payload.

### B.2 Manager-served `weapon_data_t` (typed payload)

```
typedef struct weapon_data {
    /* Mirrors struct weapon, byte-for-byte field set, so consumers of
     * `struct weapon *` can be cast or aliased without an indirection
     * change.  After the migration the typedef IS struct weapon. */
    u16 hi_model;
    u16 lo_model;
    struct guncmd          *equip_animation;
    struct guncmd          *unequip_animation;
    struct guncmd          *pritosec_animation;
    struct guncmd          *sectopri_animation;
    void                   *functions[2];   /* weaponfunc primary / secondary */
    struct inventory_ammo  *ammos[2];
    struct invaimsettings  *aimsettings;
    f32 muzzlez, posx, posy, posz;
    f32 sway;
    struct gunviscmd       *gunviscmds;
    struct modelpartvisibility *partvisibility;
    u16 shortname, name, manufacturer, description;
    u32 flags;

    /* Manager-only fields (not in legacy struct weapon): */
    struct aibotweaponpreference bot_pref;       /* fold A.2 g_AibotWeaponPreferences row */
    char                         catalog_id[64]; /* for diagnostic logging */
    s32                          weapon_id;      /* WEAPON_* (same as catalog row) */
} weapon_data_t;
```

The mirror discipline keeps `struct weapon *` and `weapon_data_t *` interchangeable. Phase 2 does NOT rename `struct weapon`; it keeps the legacy name as a typedef alias to `weapon_data_t` so existing field-chained reads compile unchanged.

### B.3 Field-by-field migration map

| Legacy field | Manager-served field | Notes |
|---|---|---|
| `g_Weapons[i]` indirection | `catalogManagerGetWeaponByIndex(i)` | O(1) array lookup inside manager |
| `weaponFindById(itemid)` | wraps the above with the same `itemid < 0 \|\| >= count` guard | Body changes, signature stays |
| `weapon->hi_model / lo_model` | `weapon->hi_model / lo_model` | unchanged, same struct shape |
| `weapon->functions[i]` | same | unchanged |
| `weapon->ammos[i]` | same | unchanged |
| `weapon->aimsettings` | same | unchanged |
| `weapon->{name,shortname,manufacturer,description}` | same | unchanged |
| `weapon->{posx,posy,posz}` | same (mutable; manager exposes `catalogManagerWeaponSetPositionOffset`) | |
| `weapon->flags` | same (mutable for EYESPY override; manager exposes `catalogManagerWeaponSetEyespyVariant`) | |
| `g_AibotWeaponPreferences[i]` | `&catalogManagerGetWeaponByIndex(i)->bot_pref` | bot AI fields fold in |
| `invaimsettings_default` | `catalogManagerWeaponDefaultAimSettings()` | const ptr, never mutates |
| `invnoisesettings_silent` | `catalogManagerWeaponDefaultNoiseSettings()` | const ptr, never mutates |

---

## C. `.pdbase` File Format (Weapon-Specific Aspect)

This section defines only the weapon-relevant slice of the `.pdbase` format. The format itself is shared across all asset types in the full-pipeline migration, so cross-cutting concerns (top-level container, signature, version field) are deferred to a Gate-3 design that lands when the second asset type migrates. For this session, `.pdbase` weapons are a self-contained record set with a minimal envelope.

### C.1 Container

`.pdbase` is a deflate-compressed archive analogous to `.pdmod` (see `context/designs/pdmod-unified-mod-format.md`). At the root: a `manifest.json` (the .pdbase manifest, distinct from match manifests) and one or more typed-record sections. Same loader, different namespace and precedence than `.pdmod` (precedence: `.pdbase` first as base game, `.pdmod` overlays).

**Discovery:**
- Startup walks `base/*.pdbase` (precedence 1: base game), then `mods/*.pdmod` (precedence 2: user overlays).
- Each registers lightweight catalog rows with file refs pointing back into the archive.

### C.2 Weapon record schema (JSON in `manifest.json` or per-weapon JSON file)

```jsonc
{
  "id": "base:falcon2",                 // catalog ID (must equal slug-derived id)
  "weapon_id": 2,                       // WEAPON_FALCON2 enum value, range 0..88
  "name": "Falcon 2",
  "shortname_langid": 1234,             // L_GUN_xxx text bank id (or string if mod-defined)
  "name_langid": 1235,
  "manufacturer_langid": 0,
  "description_langid": 1236,
  "model": { "hi": "weapon_falcon2_hi", "lo": "weapon_falcon2_lo" },  // model file slugs
  "anims": {
    "equip":   "anim_falcon2_equip",
    "unequip": "anim_falcon2_unequip",
    "pritosec": null,
    "sectopri": null
  },
  "muzzlez": 1.0,
  "pos": { "x": 12.5, "y": -18.2, "z": -28.0 },
  "sway": 1.0,
  "flags": "ONEHANDED|AICANUSE|DUALFLIP|FOOTONACTIVATE",
  "functions": [
    { /* primary fire spec, see C.3 */ },
    { /* secondary fire spec, see C.3 */ }
  ],
  "ammos": [
    { /* primary ammo spec */ },
    { /* secondary ammo spec or null */ }
  ],
  "aimsettings": { "zoomfov": 60.0, "guntransup": 0.0, /* ... */ },
  "gunviscmds": [ /* visibility command list */ ],
  "partvisibility": [ { "part": 1, "visible": 0 } ],
  "bot_pref": { /* g_AibotWeaponPreferences row, fields 1:1 */ }
}
```

### C.3 Fire-mode (`weaponfunc`) sub-records

Each entry in `functions` is a tagged union (matches the `INVENTORYFUNCTYPE_*` enum):

```jsonc
{ "type": "shoot_single", "name_langid": 99, "ammoindex": 0,
  "noisesettings": { "minradius": 100.0, ... },
  "fire_animation": "anim_falcon2_fire",
  "flags": "AUTOAIM|...",
  "recoilsettings": { "xrange": 1.0, "yrange": 1.0, "zrange": 0.0 },
  "recoverytime60": 4, "damage": 30.0, "spread": 0.5,
  "duration60": 4, "shootsound": 17, "penetration": 1, "impactforce": 1.0 }

{ "type": "shoot_auto", /* shoot fields */ +
  "initialrpm": 600.0, "maxrpm": 800.0,
  "vibrationstart": [0.5,0.5,0.5], "vibrationmax": [1.0,1.0,1.0],
  "turretaccel": 4, "turretdecel": 4 }

{ "type": "shoot_projectile", /* shoot fields */ +
  "projectile_model": "model_falcon_bullet", "scale": 1.0,
  "speed": 12000, "traveldist": 6000, "timer60": 240,
  "reflectangle": 0.0, "soundnum": 23 }

{ "type": "throw", "ammoindex": 0,
  "projectile_model": "model_grenade",
  "activatetime60": 60, "recoverytime60": 60, "damage": 100.0 }

{ "type": "melee", "damage": 5.0, "range": 50.0 }

{ "type": "special", "specialfunc": 1, "recoverytime60": 30 }

{ "type": "device", "device": "DEVICE_NIGHTVISION" }
```

### C.4 Ammo (`inventory_ammo`) sub-records

```jsonc
{ "type": "AMMOTYPE_FALCON", "casingeject": "EJECT_BRASS",
  "clipsize": 8, "reload_animation": "anim_falcon_reload",
  "flags": "INCREMENTALRELOAD|..." }
```

### C.5 Loader parsing path

`.pdbase` parsing happens at startup and is staged:

1. **Discovery (eager):** `loaderPdbaseScan(base_dir)` enumerates `base/*.pdbase`. For each archive, the loader opens the central directory, reads `manifest.json`, validates JSON, walks the `weapons[]` array, and registers each entry as a lightweight catalog row via `assetCatalogRegisterWeapon` (existing API, extended). Failures register a `LOADER.PDBASE.WEAPON.SCAN_FAIL:` warning and skip that record (PER-ELEMENT failure granularity).
2. **Build (eager):** `loaderPdbaseBuildWeaponManager()` runs after catalog scan completes. It iterates ASSET_WEAPON entries, loads each weapon's full `weapon_data_t` from its `.pdbase` archive, and registers it with the manager via `catalogManagerRegisterWeapon(id, &data)`. Missing model files / animations log `LOADER.PDBASE.WEAPON.RESOLVE_FAIL:` and the weapon is excluded (TOTAL failure for that weapon).
3. **Read (lazy / hot path):** `catalogManagerGetWeaponByIndex(i)` is O(1) array lookup. No I/O. No string hashing. The manager owns a `weapon_data_t s_Weapons[NUM_WEAPONS]` block sized to cover all WEAPON_* slots; entries are populated at startup and never freed (weapons are bundled, evicted only when a mod toggle replaces them).

### C.6 Validation rules

- `weapon_id` must be in [0, 88] and unique per `.pdbase` namespace (no two records own the same slot in one namespace; mod overlays can override base entries).
- `id` must match the slug-derived form `<namespace>:<slug>` (e.g., `"base:falcon2"`).
- `model.hi` / `model.lo` must resolve to a registered ASSET_MODEL or be a literal numeric (legacy) ROM file num. Unresolvable values fail PER-ELEMENT and the weapon's `hi_model` / `lo_model` are zeroed.
- Each `functions` entry's `type` enum must match a known `INVENTORYFUNCTYPE_*`.
- `ammoindex` in [-1, 1].
- `flags` strings must parse against the WEAPONFLAG_* / AMMOFLAG_* lookup tables; unknown flags fail PER-ELEMENT and are logged but the weapon still loads.

### C.7 Versioning

`manifest.json` carries `"pdbase_version": 1`. The loader rejects unknown major versions. Field additions are forward-compatible (unknown fields ignored with `LOADER.PDBASE.FIELD_UNKNOWN:` debug-level note).

---

## D. Manager API Spec

### D.1 Public accessors (declared in `port/include/catalog_mgr_weapons.h`)

```c
/* Index-based hot-path accessor.  O(1).  Returns NULL on out-of-range
 * or unloaded entry; logs CATALOG.MGR.WEAPON.MISS: on failure. */
const weapon_data_t *catalogManagerGetWeaponByIndex(s32 weapon_id);

/* String-id accessor.  FNV hash + table walk.  For boundary use
 * (wire / save / mod loader); not a hot path. */
const weapon_data_t *catalogManagerGetWeaponById(const char *catalog_id);

/* Iterator over loaded weapon data (for selectors, UI). */
s32 catalogManagerWeaponCount(void);
const weapon_data_t *catalogManagerGetWeaponAt(s32 iter_index);

/* Defaults exposed for fallback.  Pure const.  Replaces extern
 * invaimsettings_default and invnoisesettings_silent. */
const struct invaimsettings *catalogManagerWeaponDefaultAimSettings(void);
const struct noisesettings  *catalogManagerWeaponDefaultNoiseSettings(void);

/* Mutators (rare; named explicitly to keep the audit trail clear). */
void catalogManagerWeaponSetEyespyVariant(s32 variant);
void catalogManagerWeaponSetPositionOffset(s32 weapon_id, f32 x, f32 y, f32 z);

/* Bot AI preference accessor (folds g_AibotWeaponPreferences). */
const struct aibotweaponpreference *catalogManagerGetWeaponBotPref(s32 weapon_id);
```

### D.2 Lifecycle

```c
/* Called at startup, after loaderPdbaseScan completes. */
void catalogManagerWeaponInit(void);

/* Register one weapon.  Used by loader and mod overlay path. */
void catalogManagerRegisterWeapon(const char *catalog_id,
                                   const weapon_data_t *data);

/* Unregister (mod toggle off).  Reverts to base game entry. */
void catalogManagerUnregisterWeapon(const char *catalog_id);

/* Tear down (only at shutdown). */
void catalogManagerWeaponShutdown(void);
```

### D.3 Reference counting / retention

For weapons specifically: every loaded weapon has `ref_count = ASSET_REF_BUNDLED` because all weapons are required for any match (the catalog already registers them as bundled). Eviction is therefore a no-op for weapons in this phase. Manifest-tagged retention applies to OTHER asset types (heads, bodies, arenas) but weapons are universally retained until shutdown.

This simplification is deliberate: weapons are the simplest asset class to migrate and serve as the validation case for the Manager pattern. Per-asset-type retention policy lives in the Gate-3 design for downstream asset types.

### D.4 Logging

Hierarchical channels under `CATALOG.MGR.WEAPON.*`:

- `CATALOG.MGR.WEAPON.MISS:`. Out-of-range index or unregistered string id (rate-limited, mirrors `MODEL.RODATA.MISS:` discipline).
- `CATALOG.MGR.WEAPON.OVERRIDE:`. Mod overlay replaced a base entry (info level).
- `CATALOG.MGR.WEAPON.MUTATE:`. Explicit mutator called (Eyespy variant change, position offset write).
- `CATALOG.MGR.WEAPON.LOAD:`. Startup completion log, prints count loaded vs. expected.

Loader channels under `LOADER.PDBASE.*`:

- `LOADER.PDBASE.WEAPON.SCAN_FAIL:`. JSON parse error or file open error, archive level.
- `LOADER.PDBASE.WEAPON.RESOLVE_FAIL:`. Model / anim / ammo reference unresolvable, weapon level (TOTAL failure for that weapon).
- `LOADER.PDBASE.WEAPON.FIELD_UNKNOWN:`. Unrecognised field in record (debug, PER-ELEMENT non-fatal).
- `LOADER.PDBASE.WEAPON.OK:`. End-of-load summary (info level).

---

## E. Loader Integration

### E.1 Eager build phase (startup)

Sequence:

```
1. assetCatalogInit()                        // existing
2. assetCatalogRegisterBaseGame()            // existing, registers 41 MP-selector ASSET_WEAPON rows
3. loaderPdbaseScan("base/")                 // NEW: walks base/*.pdbase
   - For each archive, parse manifest.json
   - Register/generate runtime weapon payload records for all 86 WEAPON_* rows
   - Preserve the 41 MP selector rows as the boundary-facing ASSET_WEAPON identity set until the typed payload sidecar lands
4. modmgrScanDirectory("mods/")              // existing, walks mods/*.pdmod
   - Already registers mod-side weapons
5. catalogManagerWeaponInit()                // NEW
6. loaderPdbaseBuildWeaponManager()          // NEW
   - Iterate ASSET_WEAPON catalog rows
   - For each, load weapon_data_t from its .pdbase / .pdmod source
   - Register with manager
   - Log CATALOG.MGR.WEAPON.LOAD: count=N expected=86
7. weaponFindById() now routes through manager
```

A per-frame load-status message ("Building catalog: weapons 41 selector slots / 86 runtime rows...") displays in the corner during startup so the user sees progress (mirrors Mike's "load-status message in corner" directive). The text is rendered through the existing splash / HUD message path; new strings added to `port/fast3d/pdgui_splash.cpp` (or equivalent).

### E.2 Lazy read phase (runtime)

Weapons are eagerly loaded at startup and remain resident. Lazy reads via `catalogManagerGetWeaponByIndex` are O(1) array index lookups against `s_Weapons[NUM_WEAPONS]`.

Lazy load via manifest applies to OTHER asset types in later sessions (per Mike's directive: heads, bodies, arenas, audio). For weapons in Phase 1, the manifest scope is unused; every weapon is always loaded.

### E.3 Failure granularity

Per Mike's architecture decisions:

- **TOTAL failure (weapon excluded):** model file unresolvable, JSON parse error, weapon_id out of range, duplicate weapon_id within a namespace. Manager registration is skipped; `weaponFindById(i)` returns NULL for that slot; tier-2 callers handle NULL safely (every caller in A.3 already does, audited for the B-263 guard sweep). The weapon is excluded from any lobby pool, scenario weapon set, or HUD list.
- **PER-ELEMENT failure (partial weapon):** unknown flag in `flags` string, missing animation reference, unknown function type. The weapon loads with the unrecognised piece zeroed; gameplay continues; debug log surfaces the gap. Loud-fail discipline applies via `LOADER.PDBASE.WEAPON.FIELD_UNKNOWN:`.

---

## F. Migration Plan (Per Consumer)

Sequential commits. Each commit lands code + the pd-tests case that pins its invariant.

| # | Site / consumer | Commit description | Build verify | Test |
|---|---|---|---|---|
| F1 | Manager skeleton | New `port/include/catalog_mgr_weapons.h` + `port/src/catalog_mgr_weapons.c` with the public API (D.1 / D.2). Stub bodies that proxy to `g_Weapons[]` for parity. Adds `catalog_id` field-level tests. | pd + pd-server + pd-tests | `tests/test_catalog_mgr_weapons_api.cpp` (API contract: index range, string id round-trip, NULL on miss). |
| F2 | `weaponFindById` migration | `game_0b0fd0.c:24-35` body changed to `return catalogManagerGetWeaponByIndex(itemid);`. Signature unchanged. The 37 tier-2 callers and ~20 cached `info->definition` reads inherit automatically. | pd + pd-server + pd-tests | `tests/test_weapon_findbyid_routes_to_manager.cpp` (asserts same returned pointer for valid range, NULL for out-of-range). |
| F3 | Direct `g_Weapons[]` reads inside `game_0b0fd0.c` | Lines 61, 78, 407, 682, 693, 704, 715 -- replace with `weaponFindById(...)` (now manager-routed). Line 34 already migrated in F2. | pd + pd-server + pd-tests | Existing F2 test covers; add `tests/test_game_0b0fd0_no_g_weapons_direct.cpp` (compile-time greps `g_Weapons[` and asserts only invitems.c + assetcatalog_base_extended.c match). |
| F4 | bondgun.c direct reads | Lines 1021 (`info->definition = ...`), 6417, 6428, 6439 -- swap to `weaponFindById(...)`. | pd + pd-server | Same compile-time grep test as F3. |
| F5 | bondgunreset.c + playerreset.c EYESPY mutators | 11 mutation lines -- replace with single call `catalogManagerWeaponSetEyespyVariant(variant)`. The mutator itself flips `weapon_data_t.name / shortname / flags` in place inside the manager. | pd + pd-server + pd-tests | `tests/test_eyespy_variant_mutator.cpp` (asserts variant 0/1/2 produces expected name/shortname/flags). |
| F6 | modelmgrreset.c direct read | Line 194 -- swap to `weaponFindById(...)`. The B-263 guard already exists; manager preserves it (`CATALOG.MGR.WEAPON.MISS:` channel). | pd + pd-server | Existing B-263 invariant remains. |
| F7 | bot.c + botinv.c g_AibotWeaponPreferences reads | 28 sites -- swap to `catalogManagerGetWeaponBotPref(weaponnum)`. Sub-struct lookup returns const ptr; field reads unchanged. | pd + pd-server + pd-tests | `tests/test_bot_weapon_pref_via_manager.cpp` (round-trip pin: every WEAPON_* index returns matching pref values vs. legacy table). |
| F8 | game_0b0fd0.c default-fallback externs | `gsetGetAimSettings` and `gsetGetNoiseSettings` -- swap `&invaimsettings_default` / `&invnoisesettings_silent` for manager defaults. | pd + pd-server | `tests/test_weapon_defaults.cpp` (asserts pointer identity stable across calls, fields match expected). |
| F9 | Catalog/runtime payload seed | `port/src/assetcatalog_base_extended.c::s_BaseWeapons` remains the exact 41-slot MP selector seed. A generated sidecar/manager payload covers all 86 runtime `WEAPON_*` entries (0x00..0x55). Adds `pdbase_path` / `pdbase_offset` / `pdbase_size` to the typed weapon payload owner rather than overloading the MP selector field. | pd + pd-server + pd-tests | `tests/test_catalog_weapon_count.cpp` (asserts 41 MP selector rows and 86 runtime payload rows). |
| F10 | Loader skeleton | New `port/src/loader_pdbase.c` with `loaderPdbaseScan` + `loaderPdbaseBuildWeaponManager`. Handles `base/` directory only; mod overlay path lives in modmgr. For now the loader has no .pdbase to load (Section G.1) so the code path is exercised but yields no records. Manager continues to source from `g_Weapons[]` until G.2. | pd + pd-server + pd-tests | `tests/test_loader_pdbase_scan_empty.cpp` (asserts empty `base/` produces zero records, no errors). |
| F11 | g_Weapons retire | After F1-F10 stable, delete `g_Weapons[]` declaration from `data.h` and `inv.h`. Delete the array literal from `invitems.c`. Delete the `invitem_*` static records (move them to `base/weapons.pdbase` JSON). Loader fully sources weapons from .pdbase. | pd + pd-server + pd-tests | `tests/test_no_g_weapons_extern.cpp` (compile + grep test ensures `g_Weapons` symbol is gone). |
| F12 | g_AibotWeaponPreferences retire | Delete table from `botinv.c`. Manager's per-weapon `bot_pref` is sole source. | pd + pd-server + pd-tests | `tests/test_no_g_aibot_extern.cpp` (compile + grep). |
| F13 | invaimsettings_default + invnoisesettings_silent retire | Delete externs. Manager defaults are sole source. | pd + pd-server + pd-tests | Existing F8 test covers. |

Each commit independently builds and passes pd-tests. F11-F13 are the "retire" commits that finalise the migration. After F13, `g_Weapons[]`, `invitem_*`, `invfunc_*`, `invammo_*`, `invaimsettings_*`, `invnoisesettings_*`, `invrecoilsettings_*`, and `g_AibotWeaponPreferences[]` are all gone from the live tree. Manager + .pdbase is the sole source of truth.

---

## G. Wire / Save Format Implications

### G.1 Wire format

Weapon identity already crosses the wire as catalog session ref u16 per the v30 rule (constraint: `Asset references use catalog ID strings everywhere` and `weapon identity uses catalog session refs u16`). Weapon DATA never crosses the wire; clients have their own catalog + manager.

**Conclusion: No NET_PROTOCOL_VER bump required.**

The match manifest pipeline already broadcasts ASSET_WEAPON entries via `SVC_MATCH_MANIFEST` (S483 confirmed). Mod weapons distribute via `SVC_CATALOG_INFO` -> `CLC_CATALOG_DIFF` -> `SVC_DISTRIB_*`. The .pdbase / manager swap is invisible on the wire because the wire never carried weapon DATA, only weapon identity.

### G.2 Save format

Weapon identity in saves uses catalog ID strings (constraint: `weapon_ids[6][64]` and `spawn_weapon_id[64]` are PRIMARY identity). `MPSETUP_VERSION` covers wire format only. The on-disk `mpsetup` WAD already saves catalog ID strings; the legacy u8 `weapons[]` integer field in `g_MpSetup` is a derived value.

**Conclusion: No save format change required.**

### G.3 Mod manifest

`.pdmod` mods can already declare ASSET_WEAPON entries via their `mod.json` (existing code path). The .pdbase loader treats `base/*.pdbase` archives identically to mod archives at the registration layer; the only difference is precedence (base first) and namespace (`base:` vs. mod's `<id>:`).

---

## H. Test Coverage (pd-tests, lands per commit)

Each pd-tests case uses `[catalog-mgr-weapon]` and `[s484]` tags so the running invariant set is visible. Cases pinned to commits via Section F mapping.

| Test case file | Invariant | Commit |
|---|---|---|
| `tests/test_catalog_mgr_weapons_api.cpp` | API contract: index range, string id round-trip, NULL on miss, count is 86 runtime payloads after init. | F1 |
| `tests/test_weapon_findbyid_routes_to_manager.cpp` | weaponFindById returns the same pointer as catalogManagerGetWeaponByIndex. | F2 |
| `tests/test_game_0b0fd0_no_g_weapons_direct.cpp` | Compile-time grep: `g_Weapons[` appears only in invitems.c + assetcatalog_base_extended.c. | F3 |
| `tests/test_eyespy_variant_mutator.cpp` | Variant 0/1/2 sets expected name + shortname + flags. | F5 |
| `tests/test_bot_weapon_pref_via_manager.cpp` | catalogManagerGetWeaponBotPref returns identical fields to legacy table for every WEAPON_*. | F7 |
| `tests/test_weapon_defaults.cpp` | Default aim / noise pointers stable across calls and fields match expected. | F8 |
| `tests/test_catalog_weapon_count.cpp` | ASSET_WEAPON catalog has 41 MP selector rows and the runtime manager has 86 payload rows after init. | F9 |
| `tests/test_loader_pdbase_scan_empty.cpp` | Empty base/ produces 0 records, no errors. | F10 |
| `tests/test_loader_pdbase_parse_roundtrip.cpp` | Round-trip: weapon_data_t -> JSON -> weapon_data_t produces identical struct. | F10 (added when JSON path is wired) |
| `tests/test_no_g_weapons_extern.cpp` | After F11, `g_Weapons` symbol is gone. | F11 |
| `tests/test_no_g_aibot_extern.cpp` | After F12, `g_AibotWeaponPreferences` symbol is gone. | F12 |
| `tests/test_pdbase_manifest_diff.cpp` | Manifest diff against current loaded set produces correct delta. | F10 (deferred; weapons are eagerly loaded so diff is trivial) |

---

## I. Decisions for Mike (architecturally significant)

Most architectural decisions are pre-made via the architecture memory note in the directive. The only items that actually need Mike's call before Phase 2 starts:

### I.1 `currentPlayerSetWeaponPos` writeable position offsets

The function `currentPlayerSetWeaponPos` (game_0b0fd0.c:150-159) writes `weapon->posx / posy / posz` in place. This is a debug / position-tuning hook. **Question for Mike: keep the manager mutable for this case, or remove the function?**

- Option A: Keep mutable. Manager exposes `catalogManagerWeaponSetPositionOffset(weapon_id, x, y, z)`. Cost: one mutator function and a `MUTATE:` log line.
- Option B: Remove. Audit shows zero live callers in the active code (the function is exposed in the header but no consumer is wired). Drops the mutator from the API.

**My recommendation: Option B (remove).** The debug hook is dead code; removing it tightens the manager's contract.

### I.2 Catalog row size

The catalog row's `ext.weapon` already carries `damage`, `fire_rate`, `ammo_type` as headline metadata for selectors (catalog universality sweep). Now the manager carries the FULL data. **Question for Mike: keep these shadowed in the row, or drop them and force selectors to read through the manager?**

- Option A: Keep. Selectors that only need headline data (e.g., a quick UI tooltip) hit the row without going through the manager. Cost: ~15 bytes per row, two sources of truth for damage/fire_rate (manager wins on conflict).
- Option B: Drop. Single source of truth; selectors call `catalogManagerGetWeaponByIndex(i)->...`. Cost: every selector pays the manager indirection (still O(1)).

**My recommendation: Option B (drop).** Single source of truth is worth the ~5-cycle indirection. The catalog row stays lighter.

### I.3 `.pdbase` JSON vs. binary

The format proposal is JSON in `manifest.json` for human authorability and consistency with `.pdmod`. **Question for Mike: agree, or prefer a binary record format for faster parse?**

- Option A: JSON. Parses ~0.5ms per weapon on modern HW. 86 runtime weapons = ~43ms once at startup. Authorable by hand or by mod tools. Same parser as `.pdmod`.
- Option B: Binary. Sub-millisecond load. Requires a separate authoring tool (the mod-tools layer would generate it from JSON anyway).

**My recommendation: Option A (JSON).** 45ms at startup is negligible; authoring story matters.

### I.4 Phase 2 scope: stop after F10 or push through F13?

F1-F10 land the manager + loader skeleton + catalog extension while preserving `g_Weapons[]` as the data source. F11-F13 retire the legacy table and switch the manager's data source to `.pdbase`. Doing F11-F13 in the same session means Phase 2 includes **moving all 86 runtime weapon definitions from `invitems.c` C records into `base/weapons.pdbase` JSON.** That is mechanical but verbose.

**Question for Mike: land F1-F10 only this session (manager scaffold + parity with g_Weapons), defer F11-F13 (data move) to a follow-up session?**

- Option A: F1-F10 only. Manager pattern validated. `g_Weapons[]` still lives. Next session moves data and retires.
- Option B: F1-F13 all in this session. Full migration done. Larger diff. More risk if a field gets transcribed wrong.

**My recommendation: Option A (F1-F10 this session).** The directive says "no half measures: every weapon-data Layer A read migrates" and "g_Weapons[] and invitem_* direct-access patterns are GONE for weapon data". Both are satisfied by F1-F10 because all reads route through the manager after F1-F10. The data move (F11-F13) is purely a source-of-truth change with no behaviour change. Splitting reduces risk; if F1-F10 has any subtle parity bug, we catch it before retiring the source data.

---

## J. Decisions Made During Execution

### J.1 Architectural decisions (decided 2026-04-27 by parent session per delegated authority)

All four Section I decisions resolved per AI's recommendations. Mike's parent session approved all four with the standard "decided 2026-04-27 by parent session per delegated authority" framing.

- **I.1 RESOLVED:** Remove the `currentPlayerSetWeaponPos` mutator dead-code hook. No live callers in active tree. Phase 2 deletes the function and its header declaration. Manager API does not expose a position-offset mutator.
- **I.2 RESOLVED:** Drop the catalog row shadow fields (`damage`, `fire_rate`, `ammo_type` in `ext.weapon`). Selectors route through manager. Single source of truth. Phase 2 (F9) removes those three fields from `asset_entry_t.ext.weapon` and migrates any consumers.
- **I.3 RESOLVED:** JSON for `.pdbase` weapon record format. Authorable, ~45ms startup parse acceptable.
- **I.4 RESOLVED:** Phase 2 scope this session = F1-F10 only (manager scaffold + accessor migration + 89-entry catalog extension, `g_Weapons[]` still alive as data source). F11-F13 data move to `.pdbase` JSON deferred to a follow-up session to reduce transcription-error risk.

### J.2 Implementation-level decisions (S484 Phase 2 execution)

- **CATALOG_MGR_WEAPON_COUNT corrected from 89 to 86.** Audit Section A.7 / A.8 said "89 entries". The actual `g_Weapons[WEAPON_SUICIDEPILL + 1]` is sized 0x55 + 1 = 86. Pinned by `tests/test_catalog_mgr_weapons_api.cpp` constant + `WEAPON_SUICIDEPILL = 0x55` mirror.
- **Manager parity policy: silent-on-negative, loud-on-over-positive.** F2 `weaponFindById` migration revealed that legacy callers pass -1 as a "no weapon equipped" sentinel (gunctrl.weaponnum, hand->gset.weaponnum during unarmed / between-weapon states). `catalogManagerGetWeaponByIndex` and `catalogManagerGetWeaponBotPref` therefore split: `weapon_id < 0` returns NULL silently (parity); `weapon_id >= count` returns NULL with `CATALOG.MGR.WEAPON.MISS:` log (B-263 sentinel-leakage class). The pure validator `catalogMgrWeaponIsInRangePure` keeps a single yes/no contract; live impl chose log policy independently. Adjustment landed in F3 commit (`5e929166`).
- **Pure-layer split for testability.** The manager comes in two TUs: `port/src/catalog_mgr_weapons.c` (live router with globals dependencies, into `pd` only) + `port/src/catalog_mgr_weapons_pure.c` (pure validators + variant spec helpers, into `pd-tests` only). Tests pin the pure layer; runtime exercises the live layer. The directive's "manager API contract tests" land via the pure layer at F1.
- **Eyespy variant handling: convenience accessor.** F5 added `catalogManagerWeaponSetEyespyForStage(s32 stage_index)` as a convenience wrapper combining `catalogMgrWeaponEyespyFromStageIndexPure` (pure spec) and `catalogManagerWeaponSetEyespyVariant` (live mutator). Single call site for both `bondgunreset.c::bgunReset` and `playerreset.c::playerInitEyespy` migrations. Mode side-effect on `eyespy->mode` stays inline because it owns runtime state, not weapon data.
- **86-entry catalog row expansion deferred to F11+.** Section F's F9 row called for `s_BaseWeapons` extension to 86 entries this session. During execution the call was made to defer because: (a) the 39 non-MP rows would have empty `pdbase_path` until F11+ populates them, (b) no live consumer needs them until the data move, (c) adding now creates dead weight that complicates the audit. The `pdbase_path / pdbase_offset / pdbase_size` field scaffold landed in F9 (the structural change); the row population follows in F11+ as a paired-with-data-move step. Pinned via F9 pd-tests case [s484][f9].
- **`fsFileExists` API absent.** F10 loader scaffold initially called `fsFileExists(dir)` to suppress a "no such directory" log. The fs API doesn't expose that primitive. Reverted to silent-zero behavior: missing dir is treated as "no archives available" with the standard OK log line. F11+ will use `fsFileLoad` / `fsFileSize` patterns for actual archive open / read.
- **Bulk g_AibotWeaponPreferences migration via sed.** F7 ran `sed` over `bot.c` + `botinv.c` to replace 28 sites mechanically. The pattern `g_AibotWeaponPreferences[<expr>].<field>` -> `catalogManagerGetWeaponBotPref(<expr>)-><field>` is too uniform for 18+ Edit calls but too varied for a single replace_all. The sed pass plus a follow-up regex for nested-bracket subscripts (`weaponnums[i]`) caught all 28. The table definition `g_AibotWeaponPreferences[]` (empty subscript) stays in `botinv.c` as the canonical owner until F11+ retires it.
- **Test framework: file-grep static audit.** Sites that are structurally migrated but functionally identical (same call graph behavior) are pinned via static grep tests in `tests/test_weapon_direct_reads_audit.cpp`. The test reads the source file at test time and asserts the absence of the legacy access pattern (`g_Weapons[`, `g_AibotWeaponPreferences[<expr>]`). This pins the *structural* migration without coupling to runtime semantics.
- **Pre-existing test failures not regressions.** The full test suite shows 2 failures (`test_catalog_provider_static.cpp:580` and `test_cutscene_layer.cpp:330`). Both reference text in `port/src/testscenarios.c` and `port/src/net/net.c`. Neither file is touched by F1-F10. Failures are pre-existing in the dev tree and unrelated to S484.

### J.4 Path B decision (2026-04-30, S591) - data-driven animations

Mid-session F11 work, Mike clarified the long-term vision: "I do want data driven, because ultimately I want to convert certain anims to use IK." This supersedes the Path A (named C symbols) recommendation from earlier in the same session.

- **Path B chosen.** Animation `guncmd[]` arrays are encoded as JSON opcode sequences in `base/weapons.pdbase` alongside weapon records. Each opcode is `[mnemonic, args...]` where mnemonic is the bare `gunscript_*` macro suffix (`playanimation`, `waittime`, `playsound`, `random`, `include`, etc.) and args carry the macro arguments with symbolic enum values (ANIM_*, SFX_*, MODELPART_*) preserved as JSON strings.
- **Animation cross-references** (e.g., `gunscript_random(20, invanim_punch_type1)`) become bare-string anim refs in JSON. The future loader (F12) resolves them to runtime pointers via the same name table that holds all 110 animations.
- **Future IK runway.** When IK animations land, they get a different record type (e.g., `"opcodes": [...]` becomes `"ik_goals": [...]`). Both flow through the same `.pdbase` data layer; the runtime selects an opcode-playback evaluator vs an IK evaluator per record type. No data-format upheaval needed.
- **Path A (animations stay in C) was rejected.** It would have required a follow-up F-phase to actually move animations to data, defeating the IK runway purpose.

### J.5 F11 implementation notes (2026-04-30, S591)

- **Authoring strategy: Python extractor.** Hand-authoring 86 weapons + 110 animations of JSON would have been ~3700 lines of error-prone manual work. The extractor (`devtools/extract_weapons_pdbase.py`, 1329 lines) reads `invitems.c` directly with a state machine, resolves constants from `constants.h` + `gunscript.h`, and emits JSON deterministically. Same source -> same output bytes (checked by re-running the extractor).
- **Constants resolution.** The extractor only resolves `#define`-style constants. Enum-style identifiers (ANIM_GUN_*, L_GUN_*, FILE_G*, SFX_*, MODELPART_*) live in generated headers (`src/generated/<rom>/animations.h`, `lang/gun.h`, etc.) as enums, not `#define`s, so they fall through to the "bare symbol -> JSON string" path. This is intentional per Path B: symbolic names stay readable in JSON; the runtime opcode decoder resolves them via the same enum tables at load time.
- **Macro decoders.** `gunscript_*` and `gunviscmd_*` calls in struct-array bodies are decoded directly via regex against the macro names (the parser does not see them as struct-tuple initializers because they're macro CALLS, not struct LITERALS). The decoder maps each macro to a `(mnemonic, [arg_names])` table.
- **Catalog ID uniqueness.** Several `invitem_*` symbols are reused across multiple `g_Weapons[]` slots (`invitem_keycard` x8, `invitem_hammer` x4, `invitem_rocket` x2). The extractor disambiguates by appending `_slot<N>` when a slug is repeated, so each of the 86 weapons has a unique catalog ID.
- **VERSION resolution.** `#if VERSION >= VERSION_NTSC_1_0` blocks are evaluated against `VERSION=2`. JPN_FINAL and PAL_FINAL branches are dropped; the NTSC_1_0 (Mike's primary build target) content lands in the JSON.
- **Per-element failure granularity not yet exercised.** All 86 weapons + 110 animations parsed cleanly with no `_unresolved` markers. F12's loader will exercise PER-ELEMENT failure (unknown flag, missing animation reference) when the `LOADER.PDBASE.WEAPON.FIELD_UNKNOWN:` channel actually fires.

### J.6 F11 commit ledger (S591)

| Commit | Step | Files | Tests |
|---|---|---|---|
| 41ccbfed | F11: extractor + archive + structure-pin tests | `devtools/extract_weapons_pdbase.py` (new, 1329 lines), `base/weapons.pdbase` (new, 12823 lines), `tests/test_loader_pdbase_scan.cpp` (extended) | 8 new cases / 35 assertions in `[catalog-mgr-weapon][s484][f11]` |

### J.3 Final commit ledger (S484 Phase 2)

| Commit | Step | Files | Tests |
|---|---|---|---|
| 9961113a | Phase 1 design doc | `context/designs/catalog-full-pipeline-weapons-2026-04-27.md` | n/a |
| 4dd6e486 | Section J Mike's decisions | design doc J.1 | n/a |
| d7487736 | F1 Manager skeleton | `port/include/catalog_mgr_weapons.h` + `_pure.h`, `port/src/catalog_mgr_weapons.c` + `_pure.c`, CMakeLists | new `tests/test_catalog_mgr_weapons_api.cpp` (40/12) |
| e67f1f66 | F2 weaponFindById migration | `src/game/game_0b0fd0.c` | new `tests/test_weapon_findbyid_migrated.cpp` (10/2) |
| 5e929166 | F3 game_0b0fd0.c direct reads + parity policy fix | `src/game/game_0b0fd0.c`, `port/src/catalog_mgr_weapons.c` | new `tests/test_weapon_direct_reads_audit.cpp` (1/1) |
| ecc9d880 | F4 bondgun.c direct reads | `src/game/bondgun.c` | extends audit (1) |
| 3445de52 | F5 EYESPY mutators | `src/game/bondgunreset.c`, `src/game/playerreset.c`, manager | extends audit (2) |
| a564aa03 | F6 modelmgrreset.c direct read | `src/game/modelmgrreset.c` | extends audit (1) |
| 697ce338 | F7 g_AibotWeaponPreferences reads | `src/game/bot.c`, `src/game/botinv.c` | extends audit (2) |
| 33ac09ee | F8 default fallbacks + I.1 mutator removal | `src/game/game_0b0fd0.c` | extends audit (3) |
| a146d415 | F9 I.2 shadow drop + pdbase fields | `port/include/assetcatalog.h`, `port/src/assetcatalog.c`, `_base_extended.c`, `_scanner.c`, `port/src/net/netdistrib.c` | extends audit (3) |
| 077ba241 | F10 .pdbase loader skeleton | `port/include/loader_pdbase.h`, `port/src/loader_pdbase.c` | new `tests/test_loader_pdbase_scan.cpp` (3/3) |

Final tally: pd-tests `[s484]` tag, 107 assertions / 29 cases. Build clean across pd, pd-server, pd-tests.


---

## Stop conditions watched

- **> 50 read sites:** 65 raw sites, but 37 inherit through accessor migration; 28 explicit edits. **Phasing not needed.** Proceeding without phasing.
- **.pdbase format design:** matched `.pdmod` envelope; weapon-specific JSON schema specified in C.2 / C.3 / C.4. Defers cross-cutting envelope decisions to Gate-3 design.
- **Manager API decisions:** reference counting / eviction simplified for weapons (always bundled, no eviction). Per-asset-type policy lives in Gate-3 design for downstream types.
- **Wire / save format incompatibility:** none surfaced. No protocol bump needed.
- **Real merge conflicts at Phase 4:** TBD; surfaces only at merge time.

Phase 2 implementation gated on Mike's responses to I.1, I.2, I.3, I.4.
