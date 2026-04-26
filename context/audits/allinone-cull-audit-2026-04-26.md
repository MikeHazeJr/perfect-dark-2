# AllInOne Cull Audit -- 2026-04-26

**Status:** PHASE 1 COMPLETE -- awaiting Mike review before Phase 2 (removal).

**Mike's directive (verbatim):** "My call on the mod weapons (and arenas) for now is to remove them."

**Scope:** 8 Goldfinger 64 lineage weapons + Goldfinger / GEX / AllInOne lineage
arenas. The catalog (per Mike's recent project memory `catalog-builds-all-selector-pools`)
is the new single source of truth; entries removed from the catalog vanish from
every selector (random pool, picker UI, AI weapon spawn, weapon cycle, spawner
placement, arena selector).

This audit enumerates every codebase touchpoint so we can decide what to delete,
what to redirect, and what (if anything) to preserve as a sentinel before any
code is changed.

---

## TL;DR -- the eight findings that must drive the design call

1. **`WEAPON_RCP45` is a load-bearing sentinel.**
   - `port/src/net/netmsg.c:3064`: `weaponnum > WEAPON_RCP45` is the upper-bound
     validity check on a wire-received weapon drop.
   - `src/game/bondgun.c:6755`: `weaponnum <= WEAPON_RCP45` gates the
     drop-on-death path.
   - Removing `WEAPON_RCP45` requires picking a new sentinel -- the highest
     remaining "real" weapon enum or an explicit `WEAPON_MAX_DROPPABLE` constant.

2. **`WEAPON_RCP45` is the autogun's beam tag.**
   - `src/game/propobj.c:9387`: `struct gset gset = { WEAPON_RCP45, 0, 0, FUNC_PRIMARY };`
   - `src/game/propobj.c:9684`: `beam->weaponnum = autogun->base.modelnum == MODEL_CETROOFGUN ? WEAPON_CALLISTO : WEAPON_RCP45;`
   - `src/game/propobj.c:21763`: a `case WEAPON_RCP45:` dispatches autogun audio (`SFX_805A`).
   - This is gameplay code that uses the enum slot as a generic
     "autogun-style beam" tag, not because RCP45 was the right weapon
     conceptually. Removing the enum requires re-tagging autogun beams to a
     stock weapon (e.g. `WEAPON_RCP120` or `WEAPON_LAPTOPGUN`) **or** introducing
     a dedicated `WEAPON_AUTOGUN_BEAM` tag.

3. **The 8 weapons are firing-range single-player unlocks.**
   - `src/game/training.c:308 frIsClassicWeaponUnlocked()` tests
     `ciGetFiringRangeScore(N) == 3` for each of the 8 weapons. Returns
     unlock status used by SP firing range progression.
   - `src/game/cheats.c:95-102` registers all 8 with `CHEATFLAG_FIRINGRANGE`.
   - `src/game/playerreset.c:613-648 invGiveSingleWeapon(WEAPON_*)` -- the
     "give me my unlocked classic weapon" SP flow.
   - `port/fast3d/pdgui_menu_cheats.cpp:177-184 SC_CHEAT_PP9I..SC_CHEAT_RCP45`
     and `:258-265` register all 8 in the cheats menu UI.
   - Removing the weapons removes the firing-range unlock reward path. **No
     gameplay loss** because the unlocks are mod-specific gameplay; the firing
     range itself stays in stock PD.

4. **Two stage setup files use `WEAPON_PP9I` as the intro weapon.**
   - `src/setups/setupref.c:28 intro_weapon(WEAPON_PP9I, -1)` -- Complex (MP).
   - `src/setups/mp_setuppete.c:51 intro_weapon(WEAPON_PP9I, -1)` -- Chicago (MP).
   - These stages are present in the AllInOne tree. Easy fix:
     `intro_weapon(WEAPON_FALCON2, -1)` (the stock PD2 default starting weapon).

5. **The Dish stage uses 8 character models from this set as visual props.**
   - `src/setups/mp_setupdish.c:279-286` spawns 8 weapons whose `modelnum` is
     `MODEL_CHRWPPK`, `MODEL_CHRTT33`, `MODEL_CHRSKORPION`, `MODEL_CHRKALASH`,
     `MODEL_CHRUZI`, `MODEL_CHRMP5K`, `MODEL_CHRM16`, `MODEL_CHRFNP90` -- but
     the `weapon_type` is `WEAPON_LAPTOPGUN`. The mod hijacked the GE gun
     models as decorative props in CI Training (Dish).
   - `src/setups/setupsho.c:220` ammocrate uses `MODEL_CHRUZI` as visual
     icon for a `AMMOTYPE_FARSIGHT` ammo crate in Skedar Ruins (sho).
   - **Decision needed:** if we keep `MODEL_CHRWPPK..MODEL_CHRFNP90` in the
     model enum (cheap -- they're just IDs into a model table), the props
     keep working. If we cull the models too, the props must be re-targeted
     to stock models. **Recommendation:** keep the model enum slots but mark
     them deprecated, since the binary models are loaded from the ROM dump
     at runtime and are not in the repo.

6. **Save format must bump.** `mpsetupfileSaveWad` /
   `mpsetupfileLoadWad` (`src/game/mplayer/mplayer.c:4470, 4492`) write a
   `u64` packed bitmap of `g_MpWeaponSetRandomFilters[NUM_MPWEAPONS]`. With
   the cull `NUM_MPWEAPONS` drops 0x31 -> 0x29 (49 -> 41). Bits at slots 0x27..0x2e
   that were "GE weapon enabled" decode after the cull as
   `MPWEAPON_SHIELD/DISABLED/...` (the slots they shift to take). Old WADs
   loaded post-cull would have garbage filter state. `g_MpSetup.weapons[i]`
   itself is saved as 7 bits per slot -- old saved values 0x27..0x2e (the
   former GE weapon IDs) would, post-cull, decode to whatever lives at those
   indices now (`SHIELD`, `DISABLED`, junk past `NUM_MPWEAPONS`).
   - **Path A (clean):** bump SAVE_VERSION; old saves are version-rejected
     or migrated by zeroing the random-filter mask + re-validating the
     weapons array.
   - **Path B (sentinel):** keep `MPWEAPON_PP9I..MPWEAPON_RCP45` as
     numeric reservations pointing at a "deleted" stub entry in
     `g_MpWeapons[]` that no UI surface displays and `mpSetRandomWeapons`
     filters out. Requires a new `MPFEATURE_*` "this slot is permanently
     deleted" or a NULL `g_MpWeapons` entry.
   - **Recommendation: Path A.** Half-measures violate
     `feedback_no_half_measures`. Bumping save version is the clean answer
     given the catalog-as-source-of-truth direction. There is no shipping
     userbase yet whose saves we'd break.

7. **Wire format already does NOT carry raw weapon enum integers.**
   Per `constraints.md`: "v30: weapon identity uses catalog session refs
   u16" and "No integer asset identity may appear on the wire (weapons and
   models use session catalog u16 refs)". Removing the enum slots changes
   only the *internal* server-side / client-side index space; the wire is
   already index-agnostic. **This is the single largest piece of good news
   in the audit.** No protocol version bump is required for the cull
   itself. (A protocol bump may still be desired alongside a
   save-format bump for hygiene.)

8. **The misc.c preprocess shift table will simplify.**
   - `port/src/preprocess/misc.c:60-83` has a stair-step shift that
     converts ROM-baked mpconfig weapon enum values into runtime
     `MPWEAPON_*` enum values. The +8 stage (`MPWEAPON_SHIELD - MPWEAPON_PP9I`)
     exists *because* the 8 GE weapons were inserted between the original
     N64 SHIELD slot (0x25) and the runtime SHIELD slot (0x2f). After the
     cull the +8 shift goes away entirely; only the +2 IRScanner /
     NightVision shift (and JPN's +1 combat-knife shift) remains. ROM
     mpconfig data will resolve cleanly to the new enum without further
     migration.

---

## Section A. The 8 weapons -- complete touchpoint inventory

### A.0 Naming reconciliation

Mike's prompt referenced `WEAPON_ZZT9MM` (0x28). Source of truth is
`src/include/constants.h:4494` -- the actual symbol is **`WEAPON_ZZT`**
(no 9MM suffix). The MP variant is **`MPWEAPON_ZZT`** at 0x2b. The "ZZT (9mm)"
label is the English display string; `WEAPON_ZZT` is the C identifier. All
8 entries below use the symbol as it exists in the source.

### A.1 Enum slot definitions (constants.h)

```
src/include/constants.h:3037-3044  MPWEAPON_PP9I .. MPWEAPON_RCP45  (0x27..0x2e)
src/include/constants.h:4490-4497  WEAPON_PP9I  .. WEAPON_RCP45    (0x24..0x2b)
```

`NUM_MPWEAPONS = 0x31` (currently 49). Post-cull: 0x29 (41).

### A.2 File ID defines (files.h)

```
src/include/files.h:916-923
  FILE_GWPPK     0x0394
  FILE_GTT33     0x0395
  FILE_GSKORPION 0x0396
  FILE_GAK47     0x0397
  FILE_GUZI      0x0398
  FILE_GMP5K     0x0399
  FILE_GM16      0x039a
  FILE_GFNP90    0x039b
```

These are weapon (held-gun) model file IDs. Pluggable post-cull -- the
file numbers can be removed but holes in the enum range are harmless
(no array uses contiguous file IDs). `src/assets/<region>/files/list.c`
lines 921-928 carry the same IDs as filename strings (`"GwppkZ"` etc.);
those ROM-side filenames cease to be referenced.

### A.3 Weapon-model file ID references in invitems.c

```
src/game/invitems.c
  4197       gunscript_playanimation(ANIM_GUN_PP9I_SHOOT, 0, 10000)   -- pp9i shoot anim
  4233-4234  invitem_pp9i: FILE_GWPPK hi/lo
  4258       gunscript_playanimation(ANIM_GUN_CC13_SHOOT, ...)        -- cc13 shoot anim
  4294-4295  invitem_cc13: FILE_GTT33
  4355-4356  invitem_kl01313: FILE_GSKORPION
  4416-4417  invitem_kf7special: FILE_GAK47
  4477-4478  invitem_zzt9mm: FILE_GUZI       (struct named *_zzt9mm with FILE_GUZI)
  4538-4539  invitem_dmc: FILE_GMP5K
  4599-4600  invitem_ar53: FILE_GM16
  4660-4661  invitem_rcp45: FILE_GFNP90
```

These are the `struct weapon` definitions (`invitem_pp9i` etc.) referenced
indirectly via `g_Weapons[]` table lookups in `bgun.c` / `gset.c`. Removal
deletes these structs and their associated `invanim_*`, `invfunc_*`,
`invammo_*`, `invaimsettings_*` records (each weapon has ~6 supporting
records).

Range: `src/game/invitems.c:4180-4720` (approximate -- need precise line
spans during Phase 2 to avoid clipping neighbors).

### A.4 Multiplayer weapon registry (mplayer.c)

```
src/game/mplayer/mplayer.c:78-129  g_MpWeapons[NUM_MPWEAPONS]
   /*0x27*/ MPWEAPON_PP9I       (PISTOL, 80 rounds, MODEL_CHRWPPK)
   /*0x28*/ MPWEAPON_CC13       (PISTOL, 80,         MODEL_CHRTT33)
   /*0x29*/ MPWEAPON_KL01313    (SMG,    100,        MODEL_CHRSKORPION)
   /*0x2a*/ MPWEAPON_KF7SPECIAL (RIFLE,  100,        MODEL_CHRKALASH)
   /*0x2b*/ MPWEAPON_ZZT        (SMG,    100,        MODEL_CHRUZI)
   /*0x2c*/ MPWEAPON_DMC        (SMG,    100,        MODEL_CHRMP5K)
   /*0x2d*/ MPWEAPON_AR53       (RIFLE,  150,        MODEL_CHRM16)
   /*0x2e*/ MPWEAPON_RCP45      (SMG,    150,        MODEL_CHRFNP90)
```

Removal collapses entries 0x27..0x2e; entries 0x2f (`MPSHIELD`) and 0x30
(`DISABLED`) shift down to 0x27..0x28. `NUM_MPWEAPONS` drops 0x31 -> 0x29.

### A.5 Bot inventory configs (botinv.c)

```
src/game/botinv.c:70-77  g_BotInvCfg[]
   /*0x24*/ WEAPON_PP9I        BOTDISTCFG_PISTOL/CLOSE
   /*0x25*/ WEAPON_CC13        BOTDISTCFG_PISTOL/CLOSE
   /*0x26*/ WEAPON_KL01313     BOTDISTCFG_DEFAULT/CLOSE
   /*0x27*/ WEAPON_KF7SPECIAL  BOTDISTCFG_DEFAULT/DEFAULT
   /*0x28*/ WEAPON_ZZT         BOTDISTCFG_DEFAULT/DEFAULT
   /*0x29*/ WEAPON_DMC         BOTDISTCFG_DEFAULT/DEFAULT
   /*0x2a*/ WEAPON_AR53        BOTDISTCFG_DEFAULT/DEFAULT
   /*0x2b*/ WEAPON_RCP45       BOTDISTCFG_DEFAULT/DEFAULT
```

Array is enum-indexed. Removing entries 0x24-0x2b shifts entries
0x2c+ (PSYCHOSISGUN through end) down by 8 -- which is correct
because the WEAPON_* enum values themselves shift. No code shift needed,
but renumbered comments must follow.

### A.6 Weapon -> language string mapping (propobj.c determiner table)

```
src/game/propobj.c:16862-16869  -- 8 entries map WEAPON_* to L_GUN_*N
  WEAPON_PP9I       -> L_GUN_050
  WEAPON_CC13       -> L_GUN_051
  WEAPON_KL01313    -> L_GUN_052
  WEAPON_KF7SPECIAL -> L_GUN_053
  WEAPON_ZZT        -> L_GUN_054
  WEAPON_DMC        -> L_GUN_055
  WEAPON_AR53       -> L_GUN_056
  WEAPON_RCP45      -> L_GUN_057
```

The "determiner" array carries grammar info ("a"/"an"/"the") for
SP picked-up-weapon HUD messages. 8 rows to delete.

### A.7 Auto-switch primary weapon list (bondgun.c)

```
src/game/bondgun.c:6150-6194  g_AutoSwitchWeaponsPrimary[]
  WEAPON_RCP45      (line 6152)
  WEAPON_AR53       (line 6156)
  WEAPON_KF7SPECIAL (line 6157)
  WEAPON_ZZT        (line 6163)
  WEAPON_DMC        (line 6164)
  WEAPON_KL01313    (line 6165)
  WEAPON_PP9I       (line 6177)
  WEAPON_CC13       (line 6178)
```

Plain list of weapons in priority order for the "auto-switch to best"
HUD path. 8 entries to delete.

### A.8 Beam-creating weapon switch (bondgun.c)

```
src/game/bondgun.c:7945-7973  switch (weaponnum) -> beamCreateForHand
  case WEAPON_PP9I .. case WEAPON_RCP45  (lines 7956-7963)
```

8 case labels in a switch that creates muzzle beams for shooting weapons.
The 8 cases all fall through to the same body. Trivial removal -- the
remaining `case WEAPON_TRANQUILIZER:` and similar above continue to work
identically.

### A.9 Beam dispatch for chr fire animations (chraction.c)

```
src/game/chraction.c:9627-9634   -- 8 case labels (default fall-through)
src/game/chraction.c:10575-10582 -- 8 case labels (makebeam = true)
```

Two switches in chraction.c. First treats the 8 weapons as "use default
taperdist" (the default-case behavior). Second sets `makebeam = true` for
the 8 weapons (alongside REAPER/SNIPERRIFLE/FARSIGHT/TRANQUILIZER/LASER).
Both are case-label deletions only.

### A.10 SP firing range unlock predicate (training.c)

```
src/game/training.c:308 frIsClassicWeaponUnlocked()
  case WEAPON_PP9I:        ciGetFiringRangeScore(0,1,2) == 3
  case WEAPON_CC13:        ciGetFiringRangeScore(3..7) == 3
  case WEAPON_KL01313:     ciGetFiringRangeScore(8..11) == 3
  case WEAPON_KF7SPECIAL:  ciGetFiringRangeScore(12..16) == 3
  case WEAPON_ZZT:         ciGetFiringRangeScore(17,18,24,25) == 3
  case WEAPON_DMC:         ciGetFiringRangeScore(29..31) == 3
  case WEAPON_AR53:        ciGetFiringRangeScore(19,20,26,28) == 3
  case WEAPON_RCP45:       ciGetFiringRangeScore(21,22,23) == 3
```

Per-weapon predicate. Whole function deleted; all callers (`frGetSlot` /
`frGetWeaponBySlot` / cheat-grant paths) lose this branch but don't
require restructuring -- just the case labels go.

### A.11 Cheat registry (cheats.c)

```
src/game/cheats.c:95-102  g_Cheats[]
  L_MPWEAPONS_109, WEAPON_PP9I       CHEATFLAG_FIRINGRANGE
  L_MPWEAPONS_110, WEAPON_CC13       CHEATFLAG_FIRINGRANGE
  L_MPWEAPONS_111, WEAPON_KL01313    CHEATFLAG_FIRINGRANGE
  L_MPWEAPONS_112, WEAPON_KF7SPECIAL CHEATFLAG_FIRINGRANGE
  L_MPWEAPONS_113, WEAPON_ZZT        CHEATFLAG_FIRINGRANGE
  L_MPWEAPONS_114, WEAPON_DMC        CHEATFLAG_FIRINGRANGE
  L_MPWEAPONS_115, WEAPON_AR53       CHEATFLAG_FIRINGRANGE
  L_MPWEAPONS_116, WEAPON_RCP45      CHEATFLAG_FIRINGRANGE
```

8 cheat rows to delete. Consumer is the SP cheats menu and the
`CHEATFLAG_FIRINGRANGE` unlock path in `playerreset.c` (next item).

### A.12 SP cheat-grant single-weapon path (playerreset.c)

```
src/game/playerreset.c:613-648
  if (cheat ...) invGiveSingleWeapon(WEAPON_PP9I);
  ... (similarly for CC13, KL01313, KF7SPECIAL, ZZT, DMC, AR53, RCP45)
```

8 sequential `if` blocks to delete. Tied to the cheat registry above.

### A.13 ImGui cheats menu UI (pdgui_menu_cheats.cpp)

```
port/fast3d/pdgui_menu_cheats.cpp
  177-184  #define SC_CHEAT_PP9I .. SC_CHEAT_RCP45  (34..41)
  258-265  cheat-row table entries with English labels ("PP9i", "CC13", ...)
```

8 enum defines + 8 table entries. All numeric IDs above SC_CHEAT_RCP45
shift down by 8.

### A.14 SP weapon model lookup (playermgr.c)

```
src/game/playermgr.c:777-784
  case WEAPON_PP9I:       model = MODEL_CHRWPPK;
  case WEAPON_CC13:       model = MODEL_CHRTT33;
  case WEAPON_KL01313:    model = MODEL_CHRSKORPION;
  case WEAPON_KF7SPECIAL: model = MODEL_CHRKALASH;
  case WEAPON_ZZT:        model = MODEL_CHRUZI;
  case WEAPON_DMC:        model = MODEL_CHRMP5K;
  case WEAPON_AR53:       model = MODEL_CHRM16;
  case WEAPON_RCP45:      model = MODEL_CHRFNP90;
```

8 case labels in a switch returning held-weapon model. Deletion-only.

### A.15 Sight overlay routing (game_0b0fd0.c)

```
src/game/game_0b0fd0.c:655-663
  case WEAPON_PP9I:
  ...
  case WEAPON_RCP45:
      return SIGHT_CLASSIC;
```

8 case labels -> a single return. Deletion-only.

### A.16 Gun FX (gunfx.c)

```
src/game/gunfx.c:720
  if (weaponnum == WEAPON_PP9I || weaponnum == WEAPON_CC13 ...
```

Single boolean expression with 8 ORs (subset; need full read of the line at
phase-2 time). Deletion-only -- the whole expression collapses to `false`
and dependent branch is dead-codable.

### A.17 Sentinels & range checks (THIS IS WHERE GAMEPLAY USES OUR WORK)

#### A.17.a Net validity

```
port/src/net/netmsg.c:3064
  if (weaponHasFlag(weaponnum, WEAPONFLAG_UNDROPPABLE)
      || weaponnum > WEAPON_RCP45
      || weaponnum <= WEAPON_UNARMED) { ... }
```

Reject path for `SVC_PROP_DROP` etc. Currently `WEAPON_RCP45 = 0x2b` is
the upper bound. Post-cull the highest dropped-weapon enum is
`WEAPON_COMBATBOOST` (0x23) or whatever lives at the new tail.
**Decision needed:** introduce `WEAPON_MAX_DROPPABLE` constant or
re-anchor on `WEAPON_COMBATBOOST` / `WEAPON_LASER`. Recommend: define a
named constant and have both bondgun.c and netmsg.c use it.

#### A.17.b Drop-on-death

```
src/game/bondgun.c:6755
  if (!weaponHasFlag(weaponnum, WEAPONFLAG_UNDROPPABLE) && weaponnum <= WEAPON_RCP45) { ... }
```

Same fix as A.17.a.

#### A.17.c Random-pool "Classic" preset

```
src/game/mplayer/setup.c:1543
  if (i >= MPWEAPON_PP9I && i <= MPWEAPON_RCP45) { ... }
```

The "Select Classic" button in the random-weapon-pool UI only enables
the 8 classic weapons. Deleting the entire `case 1: // Select Classic`
arm of the switch (`src/game/mplayer/setup.c:1540-1549`) removes the
button and its handler in one go. UI side (`pdgui_menu_mpsetup.cpp`) has
no parallel table -- it delegates via s203 shadow handler.

### A.18 Autogun beam tagging (propobj.c) -- HIGH-RISK GAMEPLAY USE

```
src/game/propobj.c:9387
  struct gset gset = { WEAPON_RCP45, 0, 0, FUNC_PRIMARY };

src/game/propobj.c:9684
  beam->weaponnum = autogun->base.modelnum == MODEL_CETROOFGUN
                  ? WEAPON_CALLISTO : WEAPON_RCP45;

src/game/propobj.c:21763
  case WEAPON_RCP45:
      psStopSound(fromprop, PSTYPE_GENERAL, 0xffff);
      psCreate(0, fromprop, SFX_805A, ...);
      arg1->unk08 = g_Vars.lvframe60 + 2;
      break;
```

This is the riskiest finding. **Autoguns currently fire `WEAPON_RCP45`-tagged
beams.** Removing `WEAPON_RCP45` requires re-tagging:

- **Option 1:** retarget to `WEAPON_RCP120` (closest stock SMG analogue). The
  `case WEAPON_RCP45:` audio dispatcher case becomes `case WEAPON_RCP120:`.
  Verify the `gunfx`/`hud`/`damage` consumers do not rely on the original tag's
  damage profile.
- **Option 2:** retarget to `WEAPON_LAPTOPGUN` (already used in MP autogun
  comment at propobj.c:9393).
- **Option 3:** introduce dedicated `WEAPON_AUTOGUN_BEAM` enum slot. Most
  surgical for damage tuning but adds a new enum.

**Recommendation:** Option 1 (`WEAPON_RCP120`). Stock PD2 weapon, similar
performance profile (SMG, 150 rounds, no scope), already in
`g_AutoSwitchWeaponsPrimary`. Re-test that autogun audio + beam visuals
look correct in MP autogun setups.

### A.19 Setup files using WEAPON_PP9I as intro_weapon

```
src/setups/setupref.c:28      Complex (MP) -- intro_weapon(WEAPON_PP9I, -1)
src/setups/mp_setuppete.c:51  Chicago (MP) -- intro_weapon(WEAPON_PP9I, -1)
```

Both setups belong to MP variants of stock PD2 stages (Complex MP and
Chicago MP). The intro_weapon line is a stage-init script directive --
"player spawns holding this weapon for the cinematic intro." Easy redirect:

```c
intro_weapon(WEAPON_FALCON2, -1)
```

(or `WEAPON_UNARMED` for a clean "no-weapon" intro.)

### A.20 Language strings

| English ID | Japanese variant in JSON | Display string |
|---|---|---|
| `L_GUN_050` | -- | "PP9i" (short + long) |
| `L_GUN_051` | -- | "CC13" |
| `L_GUN_052` | -- | "KL01313" |
| `L_GUN_053` | -- | "KF7 Special" |
| `L_GUN_054` | -- | "ZZT (9mm)" / "ZMG (9mm)" (jpn) |
| `L_GUN_055` | -- | "DMC" / "D5K" (jpn) |
| `L_GUN_056` | -- | "AR53" / "AR33" (jpn) |
| `L_GUN_057` | -- | "RC-P45" / "RC-P90" (jpn) |
| `L_MPWEAPONS_109..116` | -- | MP scoreboard names for the same 8 weapons |

JSON files (lang/gun.json, lang/mpweapons.json) across 6 region folders:
- `src/assets/ntsc-final/lang/{gun,mpweapons}.json`
- `src/assets/ntsc-1.0/lang/{gun,mpweapons}.json`
- `src/assets/ntsc-beta/lang/{gun,mpweapons}.json`
- `src/assets/pal-beta/lang/{gun,mpweapons}.json`
- `src/assets/pal-final/lang/{gun,mpweapons}.json`
- `src/assets/jpn-final/lang/{gun,mpweapons}.json`

Each weapon also has a manufacturer + description pair (`L_GUN_080..087`,
`L_GUN_115..122` per region; need precise sweep at Phase 2). 16-24
total string entries per region per file. ~144 string deletions total
across all regions.

Removing the `L_GUN_050..057` IDs requires a coordinated check that no
other code path still references them. The map I see is closed
(propobj.c determiner table + invitems.c name fields), so they are
self-contained.

### A.21 Random-pool save/load (save format)

```
src/game/mplayer/mplayer.c:4387-4402
  packWeaponSetRandomFilters() / unpackWeaponSetRandomFilters()
src/game/mplayer/mplayer.c:4475   savebufferReadBits(buffer, 64) -> u64 packed
src/game/mplayer/mplayer.c:4540   savebufferOr(buffer, packed, 64)
```

`u64` mask wide enough for current 49 slots. Post-cull 41 slots fit
(unchanged width, narrower in practice). See TL;DR finding #6 for save
strategy (recommend SAVE_VERSION bump + zero filter mask on legacy load).

### A.22 ROM mpconfig preprocess shift

```
port/src/preprocess/misc.c:60-83
  if (cfg->setup.weapons[j] >= 0x25)
      cfg->setup.weapons[j] += (MPWEAPON_SHIELD - MPWEAPON_PP9I);  // = +8
  if (cfg->setup.weapons[j] >= 0x23)
      cfg->setup.weapons[j] += (MPWEAPON_CLOAKINGDEVICE - MPWEAPON_NIGHTVISION);  // = +2
```

Maps ROM-baked enum values to runtime enum values. The +8 stage exists
solely to skip past the 8 GE weapons. Post-cull both shifts may stay
(constant evaluates to 0 if `MPWEAPON_SHIELD == MPWEAPON_PP9I`, which
won't be the case after cull) -- but cleaner to delete the +8 block
entirely. The +2 IRScanner / NightVision shift remains unchanged.

JPN variant (line 61-73) carries an extra +1 combat-knife shift; same
treatment -- delete the +8 block, keep the others.

---

## Section B. Arenas -- AllInOne / GEX / Goldfinger lineage

### B.1 Current state of arena tables

There are **THREE** arena tables that must agree (per `setup.c:210` comment):

1. `src/game/mplayer/setup.c:115-195 g_MpArenas[]` -- client-side. 75 entries.
2. `port/src/server_stubs.c:113-160 g_MpArenas[]` -- server-side. 75 entries
   (must match client stagenum-for-stagenum).
3. `port/src/assetcatalog_base.c:623-672 s_ArenaNames[75]` -- catalog
   slug map. NULL marks "do not register in catalog".

Plus the runtime gate:

4. `src/game/mplayer/setup.c:217-227 stagenumIsPlayableInMp()` -- hides
   arena entries from UI even if they exist in `g_MpArenas[]`.

### B.2 Inventory of mod-derived arena entries

Note: indices below are positions in `g_MpArenas[]`.

#### Already gated (B-225, 2026-04-23) -- candidates for **hard delete**:

| idx | stagenum | display name | catalog slug | gate state |
|---|---|---|---|---|
| 55 | STAGE_24 | "Kakariko Village (Stormy)" | NULL | hidden via stagenumIsPlayableInMp + catalog NULL |
| 56 | STAGE_TEST_MP7 | "Dark Noon Mod Valley" | NULL | hidden via stagenumIsPlayableInMp + catalog NULL |
| 70 | STAGE_EXTRA25 | "Paradox" | NULL | hidden via stagenumIsPlayableInMp + catalog NULL |

These have BG data missing in the PD2 base build (per B-225 root cause).
They're carried as shells. Hard delete is safe.

#### GoldenEye X Mod group (indices 32-54) -- 23 entries:

| idx | stagenum | display name |
|---|---|---|
| 32 | STAGE_EXTRA6 | "Tample" (Temple typo) |
| 33 | STAGE_EXTRA2 | "Complex" |
| 34 | STAGE_EXTRA8 | "Caves" |
| 35 | STAGE_EXTRA9 | "Library" |
| 36 | STAGE_EXTRA13 | "Basement" |
| 37 | STAGE_EXTRA15 | "Stack" |
| 38 | STAGE_EXTRA10 | "Facility" |
| 39 | STAGE_EXTRA11 | "Bunker" |
| 40 | STAGE_EXTRA4 | "Archives" |
| 41 | STAGE_EXTRA12 | "Caverns" |
| 42 | STAGE_EXTRA14 | "Egyptian" |
| 43 | STAGE_TEST_MP17 | "Facility BZ" |
| 44 | STAGE_EXTRA1 | "Frigate" |
| 45 | STAGE_TEST_SILO | "Archives 1F (GE-X 5e)" |
| 46 | STAGE_TEST_MP16 | "Archives BZ" |
| 47 | STAGE_TEST_MP14 | "Streets" |
| 48 | STAGE_EXTRA3 | "Train" |
| 49 | STAGE_TEST_MP18 | "Cradle" |
| 50 | STAGE_EXTRA5 | "Aztec" |
| 51 | STAGE_TEST_MP20 | "Citadel" |
| 52 | STAGE_TEST_MP19 | "Labyrinth" |
| 53 | STAGE_EXTRA7 | "Icicle Pyramid" |
| 54 | STAGE_TEST_MP8 | "Cliff Base" |

All 23 are GoldenEye X (GEX) port arenas. **Already excluded from catalog
registration** (`s_ArenaNames[32..54]` are all `NULL`, and the
`s_ArenaGroupMap` skips this range). They appear in `g_MpArenas[]` only
for stagenum-index parity. **Recommendation: hard delete.** Per Mike's
directive these are mod content not part of stock PD2.

#### Bonus group (indices 55-70) -- 16 entries, mixed lineage:

| idx | stagenum | display name | currently in catalog? |
|---|---|---|---|
| 55 | STAGE_24 | Kakariko Village (Stormy) | NO (B-225 gated) |
| 56 | STAGE_TEST_MP7 | Dark Noon Mod Valley | NO (B-225 gated) |
| 57 | STAGE_TEST_ARCH | Suburb | YES (`test_arch`) |
| 58 | STAGE_TEST_DEST | Training Day / Blank Map | YES (`test_dest`) |
| 59 | STAGE_EXTRA16 | Runway | YES (`extra16`) |
| 60 | STAGE_EXTRA17 | Control | YES (`extra17`) |
| 61 | STAGE_EXTRA18 | Tawfret Ruins | YES (`extra18`) |
| 62 | STAGE_EXTRA19 | Targitzan's Temple | YES (`extra19`) |
| 63 | STAGE_EXTRA20 | Junkyard | YES (`extra20`) |
| 64 | STAGE_EXTRA21 | Steel Mill | YES (`extra21`) |
| 65 | STAGE_EXTRA22 | Mall | YES (`extra22`) |
| 66 | STAGE_EXTRA23 | Tunnels | YES (`extra23`) |
| 67 | STAGE_EXTRA24 | Rogue | YES (`extra24`) |
| 68 | STAGE_EXTRA26 | War Colors | YES (`extra26`) |
| 69 | STAGE_TEST_LAM | Grand Library | YES (`test_lam`) |
| 70 | STAGE_EXTRA25 | Paradox | NO (catalog NULL) |

**Comment from `assetcatalog_base.c:649-656` (Mike's "Priority F" note,
2026-04-24):** "the 'test' arenas are real bonus stages with proper
langbank names ('Suburb', 'Training Day', 'Runway', etc.). Mike re-enabled
them because they ARE valid Grid arenas; the data is shipped, the geometry
exists, the names show in the picker. The earlier hide-by-default
(Priority A 7ff165b0) was over-eager. STAGE_TEST_DEST (index 58 /
'Training Day') is also the Blank Map target -- see GRID_BLANK_STAGE in
port/include/pdgui_menu_grid.h."

This means the 13 "Bonus" arenas registered in the catalog (slugs
`test_arch`, `test_dest`, `extra16`-`extra24`, `extra26`, `test_lam`) are
**actively used** as PD2 Grid (Forge) arenas. **`STAGE_TEST_DEST` is the
Blank Map target for the Forge editor.**

**Stop-condition:** This contradicts the assumption that "Bonus" =
mod content. Mike, before any cull of indices 57-69, please confirm:

> Are the Bonus group arenas (Suburb, Training Day, Runway, Control,
> Tawfret Ruins, Targitzan's Temple, Junkyard, Steel Mill, Mall, Tunnels,
> Rogue, War Colors, Grand Library) part of stock PD2 or AllInOne lineage?
> Asset Catalog Priority F note suggests they are stock-and-shipped,
> particularly STAGE_TEST_DEST (Blank Map target).

**Recommendation: do NOT cull the Bonus group on this pass** (besides the
already-gated 55/56/70). They appear to be active PD2 content.

#### Random GoldenEye X (index 73)

| idx | stagenum | display name | catalog status |
|---|---|---|---|
| 73 | STAGE_MP_RANDOM_GEX | "Random GoldenEye X" | not registered (`s_ArenaGroupMap` Random group covers only 71-72) |

This is the random-arena selector for the GEX bucket. Already excluded
from catalog. **Recommendation: hard delete** (and remove the explanatory
comment about its absence on the catalog side).

#### Junk entry (index 74)

| idx | stagenum | display name | catalog status |
|---|---|---|---|
| 74 | `1` | "Random" (L_MPMENU_136) | not registered |

This entry has stagenum literal `1` -- a sentinel/junk slot. Catalog
already skips. **Recommendation: hard delete.**

### B.3 Total arena cull recommendation

**Phase 2 arena cull (recommended):**

| index range | reason |
|---|---|
| 32-54 | GEX arenas, 23 entries -- mod content, not in catalog |
| 55-56 | Kakariko + Dark Noon, B-225 gated, BG data absent |
| 70 | Paradox, catalog NULL, gated |
| 73 | Random GEX, mod-only random pool |
| 74 | Junk literal-1 entry |

**Total: 28 entries removed, leaving 47 (down from 75).** Indices
57-69 (Bonus group) preserved pending Mike's confirmation.

This requires identical changes to **all three tables** (client
`g_MpArenas`, server `g_MpArenas`, and `s_ArenaNames`).

### B.4 Stage enum / stagenum impact

The stage enum constants (`STAGE_EXTRA1..STAGE_EXTRA26`, `STAGE_TEST_*`,
`STAGE_24`, `STAGE_MP_RANDOM_GEX`) are referenced from many places beyond
`g_MpArenas[]`. **The cull does not remove the stagenum constants
themselves** -- only their entries in `g_MpArenas[]`. The constants stay
in `constants.h` because they are used by:

- Stage table (`g_StageIndex`, 87 entries)
- File-id mapping (`FILE_BG_*_SEG`)
- Setup file linkage (mp_setup<x>.c, setup<x>.c)
- Mod registry tests

Removing the stagenum constants would be a separate, larger undertaking
and is **out of scope** for this cull. The audit calls them out so we
don't accidentally break unrelated stage code paths.

---

## Section C. Wire / save format compat decision

### C.1 Wire format -- NO BUMP REQUIRED

Per `constraints.md` v30+ rule: weapons cross the wire as **catalog session
ref u16**, never as raw `WEAPON_*` enum values. Per v32: scenarios cross
as catalog ID strings. Removing enum slots affects the *internal* index
space only; the wire itself is index-agnostic. Confirmed by reading
`netmsg.c` -- the `MPWEAPON_*` IDs are compared only against the local
`g_MpWeapons[]` table after a session-ref roundtrip.

### C.2 Save format -- BUMP REQUIRED

`mpsetupfileSaveWad` writes:

- `g_MpSetup.weapons[i]` as 7 bits per slot (values 0..40 post-cull, fits)
- `wpnRndPacked` as 64 bits (mask over `NUM_MPWEAPONS` bits)

Old WAD with `weapons[0] = 0x27` (was MPWEAPON_PP9I) decodes post-cull as
0x27 = MPWEAPON_SHIELD, which is an invalid spawn-weapon slot. Old WAD
with random-filter bits set in the 0x27..0x2e range similarly decodes to
SHIELD/DISABLED/junk slots.

**Recommended migration:** SAVE_VERSION bump. Legacy WAD load path
(version < N):
- `g_MpSetup.weapons[i]` clamped to `MPWEAPON_DISABLED` for any value
  >= old `MPWEAPON_PP9I`.
- `g_MpWeaponSetRandomFilters[i]` cleared to 0 for any slot that no
  longer corresponds to a registered weapon.
- Optionally clear the random filter bitmap entirely on legacy WAD load
  and rebuild from `g_MpWeaponSets[g_MpWeaponSetNum]` defaults.

Mike confirmed in the spawn prompt: "no half measures." Recommend a
clean SAVE_VERSION bump + migration helper.

### C.3 Protocol bump -- not required, but recommended for hygiene

Same deployment churn as the save bump. Bumping `NET_PROTOCOL_VER` and
documenting the cull as the v44 changelog gives operators a clear "fresh
build only" signal.

---

## Section D. What is preserved (sentinels) -- recommendation: NONE

Per `feedback_no_half_measures` and Mike's "no half measures" line in the
spawn prompt:

- **No** preserved enum slot pointing at NULL.
- **No** half-removed language strings.
- **No** orphan asset binaries (binaries are ROM-derived, not in repo, so
  not applicable -- the `Gwppk` filename strings in `list.c` are the only
  repo-side artifacts and they go too).
- **No** zombie save-format slots.

Either a touchpoint is fully gone, or it's deliberately preserved as a
documented sentinel. If we go Path A (clean save bump), nothing needs
preservation.

---

## Section E. Risk register

| Risk | Severity | Mitigation |
|---|---|---|
| `WEAPON_RCP45` autogun beam tag breakage | HIGH | Re-tag to `WEAPON_RCP120` (Section A.18). Test MP autogun audio + visuals post-cull. |
| `WEAPON_RCP45` upper-bound sentinels in netmsg.c / bondgun.c | HIGH | Define `WEAPON_MAX_DROPPABLE` constant; update both call sites. |
| Save format breakage on legacy WADs | HIGH | SAVE_VERSION bump + zero-out filter mask on old loads. |
| Stage setupref.c / mp_setuppete.c intro_weapon | LOW | Redirect to `WEAPON_FALCON2`. |
| Cheats menu numeric IDs `SC_CHEAT_*` shift | LOW | Pure UI enum -- save state is keyed by L_MPWEAPONS_* IDs not numeric. |
| g_BotInvCfg index renumbering | MEDIUM | Renumber comments; verify no caller does `g_BotInvCfg[N]` with hardcoded N >= 0x24. |
| Bonus-group arena lineage uncertainty | UNKNOWN | **STOP -- do not cull indices 57-69 without Mike's confirmation** that they are not stock PD2 content. |
| Dish stage / Skedar Ruins stage prop models | LOW | Keep `MODEL_CHRWPPK..CHRFNP90` model enum slots even if culling the weapon code; binary models are ROM-derived. |
| Misc.c preprocess +8 shift becomes a no-op | LOW | Delete the +8 block; leave +2 IR/NV and JPN +1 knife alone. |

---

## Section F. Phase 2 sequencing (proposed)

If Mike approves this audit, Phase 2 should execute in this order to keep
each step build-verifiable:

1. **Define `WEAPON_MAX_DROPPABLE`** (or pick the new sentinel) and update
   `netmsg.c:3064` + `bondgun.c:6755`. Build-verify.
2. **Re-tag autogun beams** (`propobj.c:9387, 9684, 21763`). Build-verify.
3. **Redirect stage intro_weapon** (`setupref.c:28`, `mp_setuppete.c:51`).
   Build-verify.
4. **Delete `g_MpWeapons[]` entries** (mplayer.c:119-126), update
   `NUM_MPWEAPONS` (constants.h:3047). Build-verify.
5. **Delete enum entries** (`constants.h:3037-3044, 4490-4497`). This will
   cascade through every remaining touchpoint as build errors. Fix each
   as they appear:
   - cheats.c:95-102
   - playerreset.c:613-648
   - playermgr.c:777-784
   - botinv.c:70-77
   - propobj.c:16862-16869
   - bondgun.c:6150-6194 (auto-switch list), 7945-7973 (beam switch)
   - chraction.c:9627-9634, 10575-10582
   - game_0b0fd0.c:655-663
   - gunfx.c:720
   - training.c:308 (whole function deletion)
   - invitems.c:4180-4720 (struct deletions)
6. **Delete `pdgui_menu_cheats.cpp` SC_CHEAT_*` defines and rows.
7. **Delete `mplayer/setup.c:1540-1549`** "Select Classic" preset arm.
8. **Delete `port/src/preprocess/misc.c` +8 shift blocks.**
9. **Bump SAVE_VERSION + add legacy migration** in
   `mpsetupfileLoadWad`.
10. **Delete arenas**: `g_MpArenas[]` indices 32-56, 70, 73, 74 in BOTH
    client (`src/game/mplayer/setup.c`) and server (`port/src/server_stubs.c`)
    versions. Update `s_ArenaNames` and `s_ArenaGroupMap` in
    `assetcatalog_base.c`. Update `stagenumIsPlayableInMp` (no longer
    needed for these stages -- the entries are gone).
11. **Delete language strings** `L_GUN_050..057`, `L_MPWEAPONS_109..116`,
    and per-weapon manufacturer/description IDs across all 6 region JSON
    files.
12. **Delete `FILE_GWPPK..FILE_GFNP90`** from files.h and
    `src/assets/<region>/files/list.c`.
13. **Final propagation grep**: `WEAPON_PP9I|WEAPON_CC13|WEAPON_KL01313|...`
    should return zero hits across all source. Same for `MPWEAPON_*`,
    `FILE_G*` (the 8 IDs), `MODEL_CHRWPPK..CHRFNP90` references in
    non-prop code, `L_GUN_050..057`, `L_MPWEAPONS_109..116`.
14. **Build pd + pd-server clean.**
15. **Headless playtest:** verify random-pool default no longer offers
    these weapons; verify the 13 "Bonus" arenas (if preserved) still
    show in the picker; verify autogun audio is unchanged; verify Dish
    stage props still render (model enum slots preserved).

---

## Section G. Stop conditions resurfaced

Per spawn prompt's stop conditions, surface to Mike before continuing if:

1. **Asset binaries are large and uncertain.** -- N/A. Binaries are
   ROM-derived; only filename strings + file-id defines exist in repo.
   These are deletable as part of normal touchpoint cull.
2. **Wire/save compat looks more invasive than expected.** -- Save bump
   is required; wire is unaffected. **Mike should approve the SAVE_VERSION
   bump direction (Path A) before Phase 2.**
3. **The 8 weapons referenced by stages/scripts/AI in gameplay-breaking
   ways.** -- YES, two findings:
   - **`WEAPON_RCP45` is the autogun beam tag** (Section A.18 / TL;DR #2).
     Mike should confirm the recommended re-tag to `WEAPON_RCP120`.
   - **`WEAPON_RCP45` is a sentinel boundary** for droppable weapons in
     `netmsg.c` and `bondgun.c` (Section A.17 / TL;DR #1). Mike should
     confirm the recommended `WEAPON_MAX_DROPPABLE` constant approach.
4. **Goldfinger arenas actually used by current MP gameplay.** -- YES,
   13 of the 16 "Bonus" group arenas are catalog-registered and noted as
   "real bonus stages" by Mike's own Priority F note. **Mike should
   confirm whether the Bonus group is stock PD2 (preserve)** or AllInOne
   lineage (cull). Recommendation: preserve unless told otherwise.

---

## Section H. Decisions (resolved 2026-04-26, delegated authority from Mike)

After surfacing the audit and a follow-up clarification round on the
autogun re-tag (provenance, mechanism, three call-site analysis), Mike's
verbatim response was: "I leave it to you, but that sounds good to me."
Authority delegated; the 9 decisions below are locked in for Phase 2.

1. **Save format strategy:** **Path A.** Clean `SAVE_VERSION` bump +
   zero-out the random-filter mask + clamp `g_MpSetup.weapons[i]` to
   `MPWEAPON_DISABLED` for any value `>= old MPWEAPON_PP9I` on legacy WAD
   load. Aligns with `feedback_no_half_measures` -- the catalog is
   platform foundation, half-preserved sentinel slots would be churn.

2. **Autogun beam re-tag:** **`WEAPON_LAPTOPGUN`** (revised from the
   audit's original `WEAPON_RCP120` recommendation). Applies to **both**
   sites #1 (`propobj.c:9387` -- the gameplay damage gset) and #2
   (`propobj.c:9684` -- the beam visual identity for non-CETROOFGUN
   autoguns). Reasons:
   - The MP autogun *is* a deployed Laptop Sentry Gun (`propobj.c:9393`
     comment).
   - The same function already uses `WEAPON_LAPTOPGUN` at the
     firing-range target-hit gset (`propobj.c:9541`), so the cull brings
     the two gsets into agreement instead of leaving them divergent.
   - MP killfeed becomes meaningful ("killed by Laptop Gun") instead of
     crediting a culled mod weapon.
   - Same `MODEL_CHRPCGUN` model is used for both the held weapon and
     the deployed sentry, reinforcing identity.
   - Beam visual texture stays default (`gunfx.c:333-347` switch has no
     `case WEAPON_LAPTOPGUN`, same as RCP45's fall-through behavior).
   - Cost: SP stage-placed autoguns (Aztec, CI Training, Investigation,
     Deep Sea Cetan ship roof gun) credit "Laptop Gun" in the killfeed
     too. SP killfeed is not a visible surface, so this is acceptable.
   - The CETROOFGUN-uses-CALLISTO branch at site #2 stays as-is.

3. **Site #3 (`propobj.c:21763 case WEAPON_RCP45`)**: **delete outright.**
   Confirmed dead code in the current call graph -- no caller passes
   `WEAPON_RCP45` to `projectileCreate`. SFX_805A is the K7 Avenger's
   burst-fire sound (`invitems.c:2256, 2284`), not specifically
   RCP45's. No replacement needed.

4. **Sentinel constant:** **introduce `WEAPON_MAX_DROPPABLE`.** Define
   in `constants.h` with a header comment describing its meaning and
   pointing at `netmsg.c` + `bondgun.c` consumers. Replace the two
   `WEAPON_RCP45` sentinel uses (`port/src/net/netmsg.c:3064`,
   `src/game/bondgun.c:6755`). Pick the value to be the new highest
   droppable stock weapon enum slot post-cull.

5. **Bonus group arena lineage:** **PRESERVE** the 13 active "Bonus"
   group arenas (indices 57-69 minus the gated 70). Mike's own Priority
   F note in `assetcatalog_base.c:649-656` flags them as "real bonus
   stages" with shipped data; STAGE_TEST_DEST is the Forge Blank Map
   target. Cull only:
   - GEX block (indices 32-54), 23 entries
   - Already-gated shells (55, 56, 70), 3 entries
   - Random GEX (73) + junk slot (74), 2 entries
   - **Net 28 arena entries removed, 47 preserved.**

6. **Setup file `intro_weapon` redirect target:** **`WEAPON_FALCON2`.**
   Stock PD2 default starting weapon -- the right idiomatic replacement
   for any setup file currently pointing at one of the 8 culled GE
   imports (`setupref.c`, `mp_setuppete.c`).

7. **Model enum slots `MODEL_CHRWPPK..CHRFNP90`:** **KEEP** (out of
   scope for this cull). Used as visual props in `setupdish.c` and
   `setupsho.c`; the binary models load from the ROM dump at runtime
   and removing the model slots would break those stage props. Future
   cull pass can re-evaluate; for now the model enum slots remain.

8. **Protocol version bump:** **YES.** Bump `NET_PROTOCOL_VER` v43 -> v44
   alongside the SAVE_VERSION bump for hygiene, even though the wire
   protocol itself is unchanged (per v30+ catalog session ref rule). One
   coordinated "fresh build only" signal to operators.

9. **`STAGE_EXTRA*` / `STAGE_TEST_*` enum constants:** **KEEP** in
   `constants.h`. Out of scope for this cull. The cull removes
   *entries* from `g_MpArenas[]`, not the underlying stagenum constants
   (which are referenced by stage table, file-id mapping, setup file
   linkage, mod registry tests). Future task can revisit.

### Phase 2 execution gates locked in by Mike

- Audit doc must be committed first with this Section H update before
  any code changes.
- Phase 2 commits sequenced per Section F. Smaller commits over one big
  bundled commit -- regressions must be bisectable.
- Build verify after each major step, not at the end.
- Headless build verify on both `pd` and `pd-server` targets.
- Merge to dev with pre/post line-count snapshots per
  `worktree-truncation-danger` memory.
- Report dev HEAD commit hash post-merge.

### Stop conditions still in effect during Phase 2

If during Phase 2 a touchpoint surfaces that's not in this audit, or a
wire/save compat surface turns out bigger than expected, **stop and
surface to Mike**. Mike has delegated the 9 decisions above, not
"all decisions for the entire cull." Anything outside that scope: ping
back.

Methodology rules: no em-dashes in code or commit messages
(`no-em-dashes-in-code` memory), possibility-framed language for
ambiguity, no half measures.

---

## Appendix: Quick-reference touchpoint counts

**Files modified during Phase 2 (estimated):**

| File | Changes |
|---|---|
| src/include/constants.h | 16 enum/define lines deleted; NUM_MPWEAPONS adjusted |
| src/include/files.h | 8 file-id defines deleted |
| src/game/invitems.c | ~240 lines (struct + supporting records) deleted |
| src/game/mplayer/mplayer.c | 8 lines in g_MpWeapons[]; 1 array width macro |
| src/game/botinv.c | 8 lines deleted |
| src/game/propobj.c | 8 determiner rows + 3 autogun re-tag points |
| src/game/bondgun.c | 8 lines (auto-switch) + 8 case labels (beam) + 1 sentinel |
| src/game/chraction.c | 16 case labels deleted |
| src/game/training.c | ~50 lines (frIsClassicWeaponUnlocked deleted) |
| src/game/cheats.c | 8 lines deleted |
| src/game/playerreset.c | ~36 lines deleted |
| src/game/playermgr.c | 8 case labels deleted |
| src/game/game_0b0fd0.c | 8 case labels deleted |
| src/game/gunfx.c | 1 boolean expression simplified |
| src/game/mplayer/setup.c | 1 "Select Classic" arm deleted; 75 -> 47 in g_MpArenas[] |
| port/src/server_stubs.c | 75 -> 47 in g_MpArenas[] |
| port/src/assetcatalog_base.c | s_ArenaNames + s_ArenaGroupMap shrink |
| port/src/preprocess/misc.c | +8 shift blocks deleted (NTSC + JPN paths) |
| port/src/net/netmsg.c | 1 sentinel updated |
| port/fast3d/pdgui_menu_cheats.cpp | 8 defines + 8 table entries deleted |
| src/setups/setupref.c | intro_weapon target changed |
| src/setups/mp_setuppete.c | intro_weapon target changed |
| src/assets/{ntsc-final,ntsc-1.0,ntsc-beta,pal-beta,pal-final,jpn-final}/lang/{gun,mpweapons}.json | ~144 string entries deleted total |
| src/assets/{...}/files/list.c | 8 filename strings deleted per region |

**Total: ~25 source files, ~144 JSON string entries, ~6 list.c files.**

---

**Audit produced 2026-04-26. Section H decisions resolved 2026-04-26 with
Mike's delegated authority. Phase 2 in progress.**

### Section H follow-up clarifications

After the audit was first surfaced, Mike asked for a deeper round on the
autogun re-tag (RCP45 provenance, what an "autogun" / "beam" is in PD,
what each of the three call sites actually does, why RCP120 was the
recommendation, alternative candidates). The clarification surfaced one
correction: **site #3 (`propobj.c:21763`) is in `projectileCreate`, not
in the autogun firing path**, and is dead code. The clarification also
surfaced `WEAPON_LAPTOPGUN` as a stronger candidate than the audit's
original `WEAPON_RCP120` recommendation, because the MP autogun *is* a
deployed Laptop Sentry Gun and the same function already uses LAPTOPGUN
for the firing-range target-hit gset. Decision #2 above reflects the
revised recommendation.
