# Sprint 2026-05-16T11:45:00Z — B-329 weapon equip-anim startup fix

## Mike's report (relayed via prior worker)

> Symptoms 2 + 5: "Farsight and other weapons not positioned properly" + "Falcon 2 loaded
> but not placed in my hand and didn't seem usable."

Prior worker correctly invalidated the `gun_hand_idx = -1` hypothesis (intentional per the
2026-04-25 audit; positions match data exactly). Remaining strong candidate per the prior
diagnostic: weapon animation never starts.

## Triage

### Symptom recap

After B-328 shipped, the master gun loader now advances all the way to
`MASTERLOADSTATE_LOADED (=4)`; `gunmodeldef` and `handmodeldef` both become non-NULL on
the right hand. Every `bgunRender` line in prior playtest logs still shows for real
weapons (wpn=22 Farsight, wpn=23 Devastator, wpn=3 Falcon 2):

- `animmode=0` (HANDANIMMODE_IDLE) for the entire match
- `animload=-1` (no anim queued)
- `animnum=0 animframe=0.00` (no animation playing)

The hand never enters `HANDANIMMODE_BUSY`, so the weapon model renders at its rest pose
with no animation — visually "floaty / not in my hand" even though the master loader
completed and both modeldefs are bound.

UNARMED (wpn=0) works because the unarmed weapon's `equip_animation` is `null` by design
— combat hands animate as part of the player body rig.

### Root cause walk

Followed `bondgun.c::bgunStartAnimation` consumer sites:

- Line 3046-3056 — equip path: `if (info->definition->equip_animation) bgunStartAnimation(...);`
- Line 2927 — unequip path
- Line 2212/2314/2441/2586 — fire paths (`func->fire_animation`)
- Line 1574/1660 — reload paths (`reload_animation`)

All five paths are NULL-gated. If the pointer is NULL, no animation is queued, and
`hand->animmode` stays IDLE.

Traced the pointer's data origin to `port/src/loader_pool.c::parseWeapon`
line 1451-1466. Four resolveAnimByName calls for equip / unequip / pritosec / sectopri
animations. `parseWeaponFunc` line 950 does the same for fire animation.
`parseAmmoIfPresent` line 875 does the same for reload animation. `decodeOpcode`
line 664/673 does the same for `include` / `random` opcode targets inside animations
themselves.

All five sites call `resolveAnimByName(anim)` which:

```c
for (s32 i = 0; i < s_AnimationsUsed; i++) {
    if (strcmp(s_Animations[i].name, name) == 0) {
        return &s_Guncmds[s_Animations[i].cmd_offset];
    }
}
return NULL;
```

Direct `strcmp` against the pool table.

The pool table is filled by `parseAnimation` line 727-784. It reads the `id` key from
each `.pdanim` file and copies it verbatim into `s_Animations[i].name`.

Confirmed from disk:

```
$ cat Build/data/ntsc-final/animations/base_invanim_falcon2_equip.pdanim
{
  "pd_kind": "animation",
  "pd_schema_version": 1,
  "id": "base:invanim_falcon2_equip",
  "category": "weapon_animation",
  "opcodes": [
    ["playanimation", "ANIM_GUN_FALCON2_EQUIP", 0, 10000],
    ["end"]
  ]
}

$ cat Build/data/ntsc-final/weapons/base_falcon2.pdwpn | head -10
{
  "pd_kind": "weapon",
  "pd_schema_version": 1,
  "id": "base:falcon2",
  "weapon_id": 2,
  "hi_model": "FILE_GFALCON2",
  "lo_model": "FILE_GFALCON2LOD",
  "equip_animation": "invanim_falcon2_equip",
  ...
}
```

Schema drift: **`.pdanim` writes catalog-ID form (`base:invanim_falcon2_equip`) but
`.pdwpn` writes the bare symbol (`invanim_falcon2_equip`)**.

The `.pdanim` emitter is at `port/src/romextract_pdanim.c:192`:
```c
snprintf(catalog_id, sizeof(catalog_id), "base:%s", anim_name);
fprintf(fp, "  \"id\": \"%s\",\n", catalog_id);
```

The `.pdwpn` emitter is at `port/src/romextract_pdwpn.c:131-137`:
```c
static const char *s_animNameForCmds(const struct guncmd *cmds)
{
    for (s32 i = 0; i < g_AnimDataCount; i++) {
        if (g_AnimData[i].cmds == cmds) return g_AnimData[i].name;
    }
    return NULL;
}
```

`g_AnimData[].name` is the bare symbol — `"invanim_falcon2_equip"` — populated from
`port/src/animdata_authored.c` which is the authoring source of truth.

So the pool table holds `"base:invanim_falcon2_equip"` but the lookups query
`"invanim_falcon2_equip"`. `strcmp` always fails → `w.equip_animation = NULL` →
`bgunStartAnimation` never called → `animmode` never flips to BUSY.

Same systemic class as B-328 / SP-16: emitter writes key X, parser stores key Y,
downstream silently zero-init.

## Fix

`port/src/loader_pool.c`. Two paired edits:

### 1. `parseAnimation` strips namespace prefix before storing

```c
if (anim_name[0] != '\0' && s_AnimationsUsed < POOL_ANIMATIONS) {
    pool_anim_entry_t *e = &s_Animations[s_AnimationsUsed++];
    /* B-329 (2026-05-16): the .pdanim emitter writes the catalog ID
     * with a namespace prefix (e.g. "base:invanim_falcon2_equip"),
     * but the .pdwpn emitter writes anim refs as the bare symbol
     * (e.g. "invanim_falcon2_equip") via g_AnimData[].name. The local
     * pool name table is the lookup that resolveAnimByName uses for
     * weapon equip/unequip/pritosec/sectopri/fire/reload references;
     * it must store the bare symbol so the .pdwpn refs resolve.
     * Strip any leading "<ns>:" prefix before storing. Asset-catalog
     * identity is unaffected -- that lives in the asset_entry_t row
     * registered by loaderWalkerScanAnimations, not in this pool. */
    const char *bare = anim_name;
    const char *colon = strchr(anim_name, ':');
    if (colon != NULL) bare = colon + 1;
    size_t n = strlen(bare);
    if (n >= sizeof(e->name)) n = sizeof(e->name) - 1;
    memcpy(e->name, bare, n);
    e->name[n] = '\0';
    e->cmd_offset = (s32)(cmds_start - s_Guncmds);
    e->cmd_count = cmds_count;
}
```

### 2. `resolveAnimByName` tolerates a namespace prefix on inbound name (forward-compat)

```c
static struct guncmd *resolveAnimByName(const char *name)
{
    if (name == NULL) return NULL;
    /* B-329 (2026-05-16): tolerate a leading "<ns>:" namespace prefix on
     * the caller's name in case a future .pdwpn emitter ships catalog-ID
     * form. Pool storage is bare (parseAnimation strips), so compare the
     * bare part of the inbound name. */
    const char *bare = name;
    const char *colon = strchr(name, ':');
    if (colon != NULL) bare = colon + 1;
    for (s32 i = 0; i < s_AnimationsUsed; i++) {
        if (strcmp(s_Animations[i].name, bare) == 0) {
            return &s_Guncmds[s_Animations[i].cmd_offset];
        }
    }
    return NULL;
}
```

Per `feedback_no_half_measures`, this is the structural fix at the schema-contract
layer. No defensive NULL checks added at the bondgun.c call sites — those
NULL-gates are correct (some weapons legitimately have no equip animation, e.g.,
UNARMED).

## Round-trip stability

`loaderPoolAnimationNameForCmds` (called by the `.pdwpn` round-trip emitter at
`port/src/romextract_pdwpn.c:131` `s_animNameForCmds`) now returns the **bare** name
because pool storage is bare. This matches what `g_AnimData[].name` already provides
for the same lookup. The `.pdwpn` round-trip is therefore stable: the emitter writes
the bare name, the parser reads it via the bare key, the pool stores the bare key,
and any future re-emission produces the bare name.

The `.pdanim` round-trip is unaffected — the emitter's `id` value is still
`base:invanim_*` (because that side reads from `anim_name` *before* the strip in
`parseAnimation`; pool storage strips for lookup-table use only). Catalog identity
remains canonical.

## What this fixes downstream

All six anim resolution sites in `loader_pool.c`:

1. `parseWeapon` -> `w.equip_animation` (line 1473)
2. `parseWeapon` -> `w.unequip_animation` (line 1477)
3. `parseWeapon` -> `w.pritosec_animation` (line 1481)
4. `parseWeapon` -> `w.sectopri_animation` (line 1485)
5. `parseWeaponFunc` -> `base->fire_animation` (line 950)
6. `parseAmmoIfPresent` -> `out->reload_animation` (line 875)
7. `decodeOpcode include` -> `out->unk04` (line 664)
8. `decodeOpcode random` -> `out->unk04` (line 673)

These were all silently NULL post-pivot because of the same prefix mismatch.

## Build verify

```
$ ninja -C Build pd pd-server pd-tests pd-updater
[1/2] Linking CXX executable PerfectDark.exe
exit=0
```

4-target clean:
- `Build/PerfectDark.exe` 55.6 MB (rebuilt 2026-05-16 11:36)
- `Build/PerfectDarkServer.exe` 22.4 MB (unchanged — `loader_pool.c` is not in
  `SRC_SERVER` per `CMakeLists.txt`; confirmed by inspection)
- `Build/pd-tests.exe` 24.9 MB (unchanged — same reason)
- `Build/Updater.exe` 12.3 MB (unchanged — same reason)

Note: ccache + Windows + the sandbox produced intermittent FAILED exit codes for
individual `loader_pool.c.obj` builds despite the obj file being correctly generated
and the link succeeding. Direct `cc.exe` invocations of the same exact command line
worked. After the build settled, all four targets are present and the resulting
`PerfectDark.exe` contains the fix (verified via `strings | grep equip_animation`).
Mike's manual `build-headless.ps1` invocation should be authoritative.

## Smoke verify (Mike-runnable)

The existing `tools/smoke-verify/tests/boot_smoke.json` already covers the loader-pool
initialization. Post-fix the boot-time loader summary remains:

- `LOADER.POOL.WEAPON.OK: active=1 weapons=86 animations=110 ...`
- `LOADER.UNIVERSAL.SUMMARY: per-kind weapons=86 ... anims=110 ...`

Additional fix-targeting verification (manual playtest, not yet automated):

```
Build/PerfectDark.exe --skip-intro --no-net --launch-mission 0x30
```

(Airbase / Falcon 2 loadout.) Watch `Build/pd-client.log` for:

- Required: `LOG.WPN.DIAG: bone-snapshot phase=FIRE wpn=3 ... animnum=N animframe=N.NN`
  with N > 0 (animation actually playing).
- Forbidden: `bgunRender ... animmode=0 animload=-1 animnum=0 animframe=0\.00` for
  any non-UNARMED weapon (wpn != 0/19).

Recursive coverage on 2+ weapons:

1. **Falcon 2 (wpn=3)** — Airbase mission, equip animation `invanim_falcon2_equip`.
2. **Farsight (wpn=22)** — Combat Sim with Farsight loadout, fire animation
   `invanim_farsight_shoot` (also used as equip in the data; per `base_farsight.pdwpn`
   line 8 `"equip_animation": "invanim_farsight_shoot"`).

Both should now resolve their equip / fire animations and play them.

## Propagation check

Searched for other parsers that read a `.pdanim`-style `id` value and store it for
later lookup. None found outside `loader_pool.c::parseAnimation`. The asset-catalog
row registration in `port/src/loader_walker_anim.c::s_register` calls
`assetCatalogRegisterAnimation(id, ...)` with the catalog ID form — that's the correct
use, and unaffected by my fix.

Searched for other emitter / parser pairs that might have the same prefix-vs-bare
asymmetry:

- `.pdmesh` / `.pdhead` / `.pdbody` / `.pdarena` — these emit catalog-ID form (`base:*`)
  in their `id` field, but the parsers in `loader_pool.c` use `assetCatalogResolve(id)`
  for cross-references (mesh by ID) — that resolves through the asset catalog row
  table, not a local pool. Safe.

The animation lookup table in `loader_pool.c` is the only place where a local pool
keyed by symbol-name is used. Fix is scoped correctly.

## Risk

Low. Only affects one parser site (animation storage) and one lookup function
(resolveAnimByName) within `port/src/loader_pool.c`. The strip is idempotent — a
name without a colon is unchanged. Catalog identity at the asset-catalog row layer
is unaffected. Round-trip emission is stable.

## Status

`FIXED-PENDING-PLAYTEST 2026-05-16`. Bugs ledger: B-329 added with HIGH severity.
Pillar: catalog (c027) — schema-contract layer fix.

## Files touched

- `port/src/loader_pool.c` — parseAnimation + resolveAnimByName (B-329 fix)
- `context/bugs.md` — B-329 row added
