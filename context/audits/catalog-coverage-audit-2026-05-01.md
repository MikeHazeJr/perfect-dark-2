# Catalog asset-coverage audit -- 2026-05-01 (comprehensive pass)

> Status: Phase 1 audit complete. Replaces the earlier scoped stub of the same
> filename (which carried hypothesis-only entries). Committed in worktree
> `claude/catalog-coverage-audit-0501` against dev tip `a98d7be8`.
>
> Mike's directive (verbatim, 2026-05-01):
>
> > "It seems we need to sweep our catalog and determine our gaps and close
> >  them properly, and fully. And references for assets to utilize it
> >  properly if not already doing so."
>
> Methodology gates: file:line evidence per claim, possibility framing on
> uncertain root causes, no em-dashes, no code shipped in this audit doc.

## Premise

The catalog is the sole asset pipeline. Every consumer load goes
`catalogResolve* -> asset_data_handle_t -> assetLoad*`. Direct
`assetLoadRomTo*(filenum, ...)` calls bypass the catalog and hide
registration gaps until a player trips over them at runtime (Farsight
filenum=907 was the first surfaced; this audit finds the rest).

The Universality Sweep (2026-04-27 audit, dev `cea836e5...`) covered
SELECTORS (random pools, character pickers, map pickers). This pass
covers REGISTRATION + CONSUMER coverage across every asset class and is
the prerequisite for the Catalog Gate 3 Bodies / Heads / Arenas data
moves staying held until findings here close.

## Boot-time registration inventory

`port/src/main.c::main` calls registration in this order
([port/src/main.c:286-356](../../port/src/main.c:286)):

1. `modmgrInit()` -- discovers mods on disk, populates registry.
2. `catalogInit()` -- modelcatalog metadata cache (separate from asset
   catalog).
3. `stageTableInit()` -- populates heap-allocated `g_Stages[]` from ROM.
4. `assetCatalogInit()` -- allocates the hash table and entry pool.
5. `assetCatalogRegisterBaseGame()`
   ([port/src/assetcatalog_base.c:436](../../port/src/assetcatalog_base.c:436)).
6. `assetCatalogScanComponents(modsdir)` + `assetCatalogScanBotVariants(modsdir)`
   ([port/src/assetcatalog_scanner.c:655](../../port/src/assetcatalog_scanner.c:655)
   and `:717`).
7. `catalogManagerHeadInit()` -- Catalog Gate 3 F1 head manager pool.
8. `loaderPdbaseScan("base", ...)` then
   `loaderPdbaseBuildWeaponManager()` and
   `loaderPdbaseBuildHeadManager()`. After weapons are built,
   `assetCatalogRegisterWeaponModelFiles()` registers the weapon hi/lo
   model files plus cartridge models as ASSET_MODEL entries
   ([port/src/main.c:325-343](../../port/src/main.c:325)).
9. `catalogBuildRuntimeCaches()` -- O(1) runtime-index reverse lookups.
10. `catalogLoadInit()` -- filenum/texnum/animnum/soundnum to pool index
    reverse-index for override lookup.
11. `modmgrLoadComponentState()` -- restore per-component enabled state.

The dedicated server build skips the ROM-dependent registrations
([port/src/server_main.c:306](../../port/src/server_main.c:306) calls
`assetCatalogRegisterBaseGame()` only; `g_NumStages == 0` server-side
short-circuits stage registration; loaderPdbaseBuild* runs but on empty
data; weapon model registration is no-op without a populated weapon
pool).

### Base-game asset types registered today

| Type | Count | Source | Where | Source-filenum bound? |
|---|---:|---|---|---|
| `ASSET_MAP` | NUM_BASE_STAGES | `s_BaseStages[]` -> `g_Stages[idx]` | `assetcatalog_base.c:445-475` | NO -- five per-stage filenums (bg, tile, pads, setup, mpsetup) are not bound to source provider handles. **Gap, see Section 3.A.** |
| `ASSET_BODY` (MP) | NUM_BASE_BODIES | `s_BaseBodies[]` -> `g_MpBodies[idx]` | `assetcatalog_base.c:484-549` | YES (`g_HeadsAndBodies[bodynum].filenum`). Hand-model filenums bound separately in extended pass. |
| `ASSET_BODY` (SP) | up to ~120 | `g_HeadsAndBodies[i]` SP fallback | `assetcatalog_base.c:776-829` | YES (`g_HeadsAndBodies[i].filenum`). |
| `ASSET_HEAD` (MP) | 76 | all `g_MpHeads[mpidx]` | `assetcatalog_base.c:563-606` | YES (`g_HeadsAndBodies[headnum].filenum`). |
| `ASSET_HEAD` (SP) | up to ~32 | `g_HeadsAndBodies[i]` SP fallback | `assetcatalog_base.c:776-829` | YES. |
| `ASSET_ARENA` | 47 | `s_ArenaNames[] x s_ArenaGroupMap[]` | `assetcatalog_base.c:611-734` | NO (arenas are stage references; the underlying stage's files are the same gap as ASSET_MAP). |
| `ASSET_WEAPON` | NUM_BASE_WEAPONS = 41 | `s_BaseWeapons[]` (MPWEAPON_* slugs) | `assetcatalog_base_extended.c:415-445` | NO at registration. Closed at boot step 8 by `assetCatalogRegisterWeaponModelFiles()` which adds ASSET_MODEL entries for hi/lo/cart filenums. |
| `ASSET_ANIMATION` | NUM_BASE_ANIM_ENTRIES = 1207 | indices 0x0000..0x04B6 | `assetcatalog_base_extended.c:447-466` | NO. ASSET_ANIMATION carries `runtime_index = animnum` only; no per-anim filenum binding because animations are inlined in the `g_Anims[]` table loaded once from the `animations` ROM segment. **See Section 3.B.** |
| `ASSET_TEXTURE` | NUM_TEXTURES = 3503 (NTSC) | indices 0x0000..NUM_TEXTURES | `assetcatalog_base_extended.c:468-487` | NO. Same shape as animations; texture data lives in the texture segment. |
| `ASSET_PROP` | NUM_BASE_PROPS = 8 | `s_BaseProps[]` (PROPTYPE_*) | `assetcatalog_base_extended.c:489-510` | NO. Categories only. **Gap if individual prop assets need registration; see Section 3.C.** |
| `ASSET_GAMEMODE` | NUM_BASE_GAMEMODES = 6 | `s_BaseGameModes[]` (MPSCENARIO_*) | `assetcatalog_base_extended.c:512-541` | n/a (no file). |
| `ASSET_AUDIO` (cat=SFX) | NUM_BASE_SFX_ENTRIES = 1545 | indices 0x0000..0x0608 | `assetcatalog_base_extended.c:543-562` | NO. Sounds load through a separate audio bank pipeline. SFX alias range (0x8000+) is **NOT REGISTERED** -- see Section 3.D. |
| `ASSET_AUDIO` (cat=MUSIC) | NUM_BASE_MUSIC_TRACKS = 43 | `s_BaseMusicTracks[]` | `assetcatalog_base_extended.c:564-593` | n/a (sequencer-driven). |
| `ASSET_BOT_PROFILE` | 18 | `g_BotProfiles[]` | `assetcatalog_base_extended.c:595-651` | n/a. |
| `ASSET_HUD` | NUM_BASE_HUD = 6 | `s_BaseHud[]` (HUD_ELEM_*) | `assetcatalog_base_extended.c:653-674` | n/a (engine-rendered). |
| `ASSET_MODEL` (props) | NUM_MODELS | every `g_ModelStates[i]` | `assetcatalog_base_extended.c:691-714` | YES (`g_ModelStates[i].fileid`). |
| `ASSET_MODEL` (hands) | unique handfilenums | `g_HeadsAndBodies[i].handfilenum` dedup | `assetcatalog_base_extended.c:716-769` | YES (handfilenum). Negative `runtime_index` to avoid collision with prop MODEL_*. |
| `ASSET_LANG` | NUM_BASE_LANG_BANKS | `s_BaseLangBanks[]` (LANGBANK_*) | `assetcatalog_base_extended.c:771-792` | NO. ASSET_LANG carries `bank_id` only; the file ID returned by `langGetFileId(bank)` is **NOT bound** to a provider handle. **Gap, see Section 3.E.** |
| `ASSET_MODEL` (weapons) | post-loader pass | weapon hi/lo + cartridge models | `assetcatalog_base_extended.c:859-967` (`assetCatalogRegisterWeaponModelFiles`) | YES (catalog-set primary handle binds `source_filenum`). This is the canonical pattern Section 4 prescribes for closing the rest. |

### Mod / scan-time registrations

| Pathway | Where | Asset types touched |
|---|---|---|
| `assetCatalogScanComponents(modsdir)` | `port/src/assetcatalog_scanner.c:655` | All types via `categoryToType` (line 205) and `sectionToType` (line 231): MAP, CHARACTER, SKIN, BOT_VARIANT, WEAPON, TEXTURES, SFX, MUSIC, PROP, VEHICLE, MISSION, UI, TOOL, ANIMATION, HUD, GAMEMODE, AUDIO, LANG. |
| `assetCatalogScanBotVariants(modsdir)` | `port/src/assetcatalog_scanner.c:717` | ASSET_BOT_VARIANT only (flat directory scan). |
| `modmgr.c::loadCompoundMod` | `port/src/modmgr.c:843-870` | ASSET_BODY, ASSET_HEAD, ASSET_ARENA via direct `assetCatalogRegister*` calls per compound entry. |
| `modmgr.c::audio import` | `port/src/modmgr.c:1842` | ASSET_AUDIO from imported audio mod files. |
| `port/src/net/netdistrib.c:1178` | catalog-distrib receive | ASSET_AUDIO from network-shared mod content. |
| `port/src/botvariant.c:127` | UI-created bot preset save | ASSET_BOT_VARIANT. |

## 1. Consumer audit -- direct ROM filenum loads (catalog-bypassing)

These call sites read filenums and load through the legacy
`assetLoadRomTo*` / `romdataFileLoad` / `fileLoadRomToNew` paths
instead of `catalogResolve* -> assetLoad*`. Each is a coverage gap
candidate.

| Site | Filenum source | Catalog binding present? | Notes |
|---|---|---|---|
| [src/game/lang.c:404](../../src/game/lang.c:404) `langPrintInfo` | `langGetFileId(bank)` (= `g_LangBanks` file id table) | NO | Reads inflated size of each language bank file directly via `fileGetInflatedSize`. |
| [src/game/lang.c:410](../../src/game/lang.c:410) `langPrintInfo` | same | NO | Loads bank into temp buffer via `assetLoadRomToAddr` (sniffer path). |
| [src/game/lang.c:418](../../src/game/lang.c:418) `langLoad` | `langGetFileId(bank)` | NO | Allocates and loads the bank via `assetLoadRomToNew`. **Primary lang-load consumer.** |
| [src/game/lang.c:429](../../src/game/lang.c:429) `langLoadToAddr` | same | NO | Lang-load-into-buffer variant. |
| [src/game/langreset.c:75-100](../../src/game/langreset.c:75) `langResetForLevel` | `langGetFileId(LANGBANK_*)` x7 | NO | Per-stage lang reload. |
| [src/game/setup.c:1331](../../src/game/setup.c:1331) `setupReadFile` | `setupfilenum` from `g_Stages[g_StageIndex].setupfileid` (or mpsetupfileid) | NO | Loads stage setup via `assetLoadRomToAddr`. **Per-stage scene load.** |
| [src/game/bg.c:1262](../../src/game/bg.c:1262) `bgLoadFile` | `g_Stages[g_StageIndex].bgfileid` | NO | Reads BG geometry segment via `romdataFileLoad`. |
| [src/game/tilesreset.c:29](../../src/game/tilesreset.c:29) (load path) | `g_Stages[g_StageIndex].tilefileid` | NO | Stage tile load. |
| [src/game/setup.c:1472-1476](../../src/game/setup.c:1472) | `g_Stages[g_StageIndex].padsfileid` | NO | Stage pad load. |
| [src/game/modeldef.c:354](../../src/game/modeldef.c:354) `modeldefLoad` | parameter `fileid` (caller-supplied filenum) | varies (catalog manager IS used for hand / weapon / prop model paths now -- see Section 2.A; this path remains for callers passing raw filenums) | The bgun and modelmgr paths route through catalog handles; legacy callers (e.g. dev tools, AllInOne legacy) still use this entry. |

The lang and stage scene file loads are the largest live coverage gaps
in this list.

## 2. Consumer audit -- catalog-driven loads (already correct)

For each of these the consumer resolves a catalog ID first, then loads
the bound provider handle. No new work needed.

### 2.A Weapon hi/lo/cart models

[src/game/bondgun.c:4445-4570](../../src/game/bondgun.c:4445) (master
load) routes every weapon model load through
`catalogHandleByModelSourceFilenum` -> `assetLoad*`. The handle
resolution lands on the ASSET_MODEL entry registered by
`assetCatalogRegisterWeaponModelFiles` in step 8 of boot. Strict
catalog-only since S484-followup-2 (commit `ecc9d880`); the
CATALOG_CRITICAL log surfaces any future miss.

### 2.B Body / head primary models

[src/game/body.c::bodyAllocateModel](../../src/game/body.c) and
[src/game/chr.c::chrSetBody](../../src/game/chr.c) read the catalog's
`source_filenum` for the body/head's `bodynum` or `headnum`. ASSET_BODY
and ASSET_HEAD entries are bound to the correct ROM filenum via
`catalogSetPrimaryRomFilenum`.

### 2.C Hand models

[src/game/bondgun.c::bgunMasterLoad](../../src/game/bondgun.c) queues
hand-model loads via `catalogGetBodyHandFilenum`. The catalog has
ASSET_MODEL entries for each unique handfilenum (negative
`runtime_index` to disambiguate from prop MODEL_*).

### 2.D Prop models

`g_ModelStates[]` MODEL_* entries are registered as ASSET_MODEL by
`assetcatalog_base_extended.c:691-714`. Consumers
(`modeldef.c::modelmgrAllocateAndLoadModel` etc.) resolve via
catalog when given a MODEL_* index.

### 2.E Music

[src/game/mplayer/mplayer.c::mpGetTrackMusicNum](../../src/game/mplayer/mplayer.c)
and friends iterate ASSET_AUDIO category=MUSIC via the catalog
(post-Universality Sweep). `mpIsTrackUnlocked` (best-time-based)
determines selector eligibility.

### 2.F SFX (basic case)

[src/lib/snd.c::sndStart](../../src/lib/snd.c:2123) calls
`catalogResolveSound(sp40.id)` (line 2154) before dispatching to
`func00033820` (the per-platform SFX bank loader). For mod-overridden
sounds this routes through `audioPlayFileSound`. **However**, sound IDs
in the alias range (`0x8000` mask set) bypass the catalog check before
the russ-mapping lookup; see Section 3.D.

## 3. Coverage gaps (each one a registration to add or a consumer to migrate)

### 3.A Stage scene files (bg / tile / pads / setup / mpsetup)

Per-stage assets `bgfileid`, `tilefileid`, `padsfileid`, `setupfileid`,
`mpsetupfileid` (struct fields at
[src/include/types.h:3129-3133](../../src/include/types.h:3129)) are
not catalog-bound. Each call site (Section 1 above) loads via
`romdataFileLoad` / `fileLoadRomToNew` directly.

**Impact**: mods cannot override BG geometry, collision tile data,
collision pad placement, mission setup script, or MP setup file for
any stage. The Forge / Grid level-editor stack depends on the BG +
tile + pads + setup load chain; a custom map pdmod cannot supply its
own files via the standard mod override mechanism.

**Migration shape (mirroring `assetCatalogRegisterWeaponModelFiles`)**:
new `assetCatalogRegisterStageSceneFiles()` walks every registered
ASSET_MAP entry, reads the runtime stage record's five filenums, and
registers each as ASSET_MODEL (or a new ASSET_BG / ASSET_TILES /
ASSET_PADS / ASSET_SETUP type if the asset class deserves its own
slot) with `source_filenum` and `catalogSetPrimaryRomFilenum`. The
five consumer call sites switch to
`catalogResolveFile(filenum) -> assetLoadToNew(handle, ...)` per the
weapon-load template.

### 3.B Animation file binding (low priority; verify before opening)

ASSET_ANIMATION entries register `runtime_index = animnum` only.
[src/lib/anim.c::animLoadHeader](../../src/lib/anim.c:353) reads from
the prebuilt `g_Anims[animnum]` table populated once from the ROM
`animations` segment ([port/src/romdata.c::preprocessAnimations](../../port/src/romdata.c)).
Per-anim file overrides flow through `modAnimationLoadData(animnum)`
([src/lib/anim.c:382](../../src/lib/anim.c:382)) which is a separate
mod path.

**Reading**: animations don't have per-anim ROM filenums; the whole
`g_Anims[]` table comes from one ROM segment. So a per-anim catalog
binding is not meaningful at the file level. Mods override at the
`modAnimationLoadData` boundary instead.

**Hypothesis to verify in Phase 2 if Mike wants belt-and-braces**:
whether `modAnimationLoadData` consults the catalog for resolution or
goes straight to a separate mod registry. If the latter, a small
ASSET_ANIMATION binding could route mod animation loads through the
same provider chain as everything else.

### 3.C Prop instances vs prop categories (low priority; semantic clarification)

ASSET_PROP entries register the 8 PROPTYPE_* CATEGORIES, not individual
prop instances. The runtime prop spawn (e.g. `setupCreateProp`) reads
from the per-stage setup file (Section 3.A) and instantiates props by
their MODEL_* index, which IS catalog-registered as ASSET_MODEL. So
prop coverage is effectively complete for runtime spawn -- the
"per-prop instance" granularity is not what the catalog tracks.

Only nominal gap: ASSET_PROP entries carry empty `model_file` (line 497
in extended). The CATEGORY abstraction is for mod authors to scope
their content; per-instance assets ride ASSET_MODEL.

### 3.D SFX alias-range entries (0x8000+) not registered

[port/src/assetcatalog_base_extended.c:208](../../port/src/assetcatalog_base_extended.c:208)
explicitly comments: "The high-bit mapped entries (SFX_8000+) are
internal aliases remapped by snd.c and are not registered as separate
catalog entries."

[src/lib/snd.c::sndStart](../../src/lib/snd.c:2145) at the top of the
function decodes the packed `soundnumhack` form. For a sound whose
high bit is set (e.g. Farsight's `SFX_813E` = 0x813E),
`sp44.hasconfig == 1` and `sp40.id` becomes
`g_AudioRussMappings[confignum].soundnum`. The catalog resolve at
line 2154 then runs against the post-mapping `sp40.id`.

**Surface**: the catalog does not own the alias mapping table.
`g_AudioRussMappings[]` ([src/lib/snd.c:178](../../src/lib/snd.c:178))
is the source of truth for "what does SFX_813E ultimately play".
Mods cannot override an alias; if a mod wanted to redirect Farsight's
shootsound to a custom file, they would need to register the
post-mapping ID, which is implementation-internal and not exposed.

**Mike's named bug -- "Farsight fire SFX plays a voiceline"**.
Possibilities ordered by likelihood:

1. **`g_AudioRussMappings[0x13E].soundnum` points to a voiceline sound
   slot.** Data-side bug in the alias table; the catalog gap above is
   the architectural reason this can't be tested or modded around.
   Phase 2 verification: dump `g_AudioRussMappings[0x13E]` at runtime,
   check what SFX_* it resolves to.
2. **`loader_pdbase_enums.c` table for SFX_813E points to the wrong
   integer**. Per
   [port/src/loader_pdbase_enums.c:2417](../../port/src/loader_pdbase_enums.c:2417),
   `SFX_808B = 32907`. The Farsight JSON specifies `SFX_813E`; the
   enum table needs verification that 0x813E resolves to 33086 (it
   should, as the high-bit-set form). If the enum table has a wrong
   value here, the parser stores the wrong sfx ref. Phase 2: grep
   `loader_pdbase_enums.c` for SFX_813E entry, confirm value.
3. **Pdbase parser writes shootsound to wrong offset for some
   variant**. Section 5 catalogues the cross-variant write hardening
   shipped in S484-followup-4. The shootsound write at
   [port/src/loader_pdbase.c:1006](../../port/src/loader_pdbase.c:1006)
   is unconditional (no `struct_name` gate). For non-shoot variants
   that happen to ship a `shootsound` JSON key (none in the current
   archive AFAICT), this would write to the wrong offset. Low
   likelihood but flagged.

**Migration shape**: add ASSET_AUDIO entries for the alias range so
mods can override the post-mapping sound, OR move the russ-mapping
table into the catalog so mods can rewrite the alias chain. Either
closes the architectural gap.

### 3.E Lang banks (file binding missing on ASSET_LANG)

ASSET_LANG entries register `bank_id` only ([extended.c:783-787](../../port/src/assetcatalog_base_extended.c:783)).
[src/game/lang.c:418](../../src/game/lang.c:418) loads each bank via
`assetLoadRomToNew(langGetFileId(bank), ...)` -- direct ROM, not
catalog-routed.

**Impact**: mods cannot override language banks. A mod that wanted to
ship an entire alternate weapon-name table (covering Falcon 2 secondary
text, every weapon's display string) cannot do so through the standard
provider mechanism.

**Migration shape**: in the ASSET_LANG registration loop, after
setting `ext.lang.bank_id`, also call `e->source_filenum = langGetFileId(bank_id)` and
`catalogSetPrimaryRomFilenum(e, e->source_filenum)`. Then
`langLoad` switches from `assetLoadRomToNew(langGetFileId(bank), ...)`
to `assetLoad(catalogGetLangHandle(bank_id), ...)`. Same pattern as
weapon model files.

### 3.F LOD body / head model files (verify whether they exist)

The earlier audit hypothesised LOD variants. Investigation:
`g_HeadsAndBodies[]` carries one `filenum` and one `handfilenum` per
entry. No second LOD field in the struct
([src/include/types.h:3083-3098](../../src/include/types.h:3083)).
Conclusion: there is no separate body/head LOD filenum source. The
"LOD" hypothesis from the prior stub doc closes as NOT APPLICABLE.

### 3.G Casing eject / muzzle flash effect models

Effect spawns in [src/game/bondgun.c](../../src/game/bondgun.c) reference
MODEL_* constants for cartridge models (covered by
`assetCatalogRegisterWeaponModelFiles` cart loop) and for muzzle-flash
particles (which are GBI-rendered by code in `gunfx.c`, not loaded as
separate model files).

**Conclusion**: cartridge models COVERED. Muzzle flash NOT a model
asset. No gap.

### 3.H Voice lines (mission briefing / banter / death)

Voice asset class is enumerated in `categoryToType` ("voice" maps to
`AUDIO_CAT_VOICE`) but no base-game voice entries are registered
([extended.c:543-562](../../port/src/assetcatalog_base_extended.c:543)
only registers SFX entries with `category = 0`; the music loop sets
`AUDIO_CAT_MUSIC`; no `AUDIO_CAT_VOICE` registration).

**Impact**: mod voice-line replacements have nowhere to register and
nothing to override. Mission-briefing voiceovers, in-game banter
shouts, death cries all run through the normal SFX bank but are not
distinguished as ASSET_AUDIO category=VOICE entries.

**Migration shape**: identify the voice-line range in the SFX bank
(by enum naming convention or by content) and re-register those
indices as ASSET_AUDIO with `category = AUDIO_CAT_VOICE`. Or accept
that voice and SFX share the bank and surface the distinction at the
manifest / consumer level instead.

### 3.I UI / Menu chrome assets

ASSET_UI is enumerated but no base-game registration. The UI chrome
extraction path
([port/fast3d/pdgui_theme.cpp::pdguiThemeExtractRomTextures](../../port/fast3d/pdgui_theme.cpp))
writes loose textures to `mods/base-ui/textures/` and the theme
loader reads them back. This path is documented in
[context/audits/rom-extraction-audit-2026-04-30.md](rom-extraction-audit-2026-04-30.md).
The audit there proposes
moving to a single `data/ui/pd-original.pdui` archive. That work is
out of scope here but is the existing tracked path for closing the
ASSET_UI gap.

### 3.J Effect / particle definitions

ASSET_EFFECT is enumerated but no base-game registration. Effects are
synthesised in code (gunfx.c, expmgr.c). No file-backed asset to
register today.

**Reading**: ASSET_EFFECT becomes meaningful when mods can author
particle-system descriptions. Today the effect pipeline is fully
imperative. Out of scope for this audit; flag for future Forge /
particle-system work.

## 4. Mike's named bugs -- triage

### 4.1 Farsight gun model `filenum=907` missing from catalog -- weapons -> melee bug

**STATUS: CLOSED.** S484-followup-2 (commit `ecc9d880` and the
`assetCatalogRegisterWeaponModelFiles` extension at
[port/src/assetcatalog_base_extended.c:859](../../port/src/assetcatalog_base_extended.c:859))
registered weapon hi/lo + cartridge model files as ASSET_MODEL with
`source_filenum` binding. The bgun load chain (strict catalog since
S484 F4) now resolves the Farsight handle and loads cleanly.

### 4.2 Falcon 2 Silencer secondary fire UI shows wrong text but dispatches correctly

**STATUS: DIAGNOSTIC SHIPPED, ROOT CAUSE NOT YET CONFIRMED.**

Symptom: secondary fire label reads "Rapid Fire" (`L_GUN_086 = 19534`).
JSON specifies "Pistol Whip" (`L_GUN_094 = 19542`). Dispatch is
correct -- the actual mechanic on secondary fire is Pistol Whip.

Diagnostic landed S484-followup-4 (commit `5655c6ea`):
[port/src/loader_pdbase.c:1345-1370](../../port/src/loader_pdbase.c:1345)
dumps `LOADER.PDBASE.WEAPON.STORED` per weapon at parse time. The next
playtest log will reveal whether the parser stored the wrong langid
(parser-side bug, wrong offset / cross-variant corruption) or whether
the consumer (the UI label render) is reading from a stale source.

**Possibilities to keep in scope for Phase 2**:

1. Pdbase parser still has cross-variant write corruption on `name` or
   `unk25/26/27` even after the recoil-block fix. The `name` field is
   in the BASE struct (offset 0x04) so should be safe across variants;
   `unk25/26/27` writes at
   [port/src/loader_pdbase.c:998-1000](../../port/src/loader_pdbase.c:998)
   are unconditional (no `struct_name` gate). For melee/throw/special
   variants whose JSON ships unk25, this corrupts other fields.
2. UI label-render reads from a different source than the parser
   stores into (e.g. the legacy `g_Weapons[]` cache that was retired).
   With `g_Weapons[]` retired, the only remaining read path is the
   manager pool. So a stale read seems unlikely unless something still
   references the retired symbol via stale macro.
3. The langbank lookup itself is wrong: L_GUN_086 = 19534 but the
   weapon's ID-to-string mapping somewhere is shifted by 8.

### 4.3 Farsight fire SFX plays a voiceline instead of the gun sound

**STATUS: SCOPED, ROOT CAUSE NOT YET CONFIRMED.**

Three possibilities (Section 3.D enumerates):
- Data: `g_AudioRussMappings[0x13E]` table entry is wrong.
- Data: `loader_pdbase_enums.c` SFX_813E entry has wrong int.
- Code: pdbase parser cross-variant write to shootsound (low likelihood
  -- the shootsound write IS unconditional but no non-shoot variant
  ships a `shootsound` JSON key).

**Architectural gap regardless of root cause**: SFX alias range is
not in the catalog, so this class of bug cannot be diagnosed via
catalog tooling. Section 3.D migration shape closes the gap.

### 4.4 Fire-time crash in recoil block

**STATUS: CLOSED.** S484-followup-4 (commit `7a4786c9`) fixed the
pdbase parser cross-variant writes at
[port/src/loader_pdbase.c:918-996](../../port/src/loader_pdbase.c:918)
for `recoverytime60`, `damage`, `unk24` and others. Each write is now
gated on `struct_name` so non-active variants do not stomp on the
active variant's `recoilsettings` pointer. The recoil-block deref
crash root cause closed.

**Latent risk in same class**: the `unk25/unk26/unk27`,
`recoildist/recoilangle/slidemax/impactforce/duration60/penetration`
writes at
[port/src/loader_pdbase.c:998-1007](../../port/src/loader_pdbase.c:998)
are still unconditional. They write to the SHOOT variant offsets;
for non-shoot variants whose JSON happens to ship those keys, the
write lands at offsets used by other fields.

Audit of the active `weapons.pdbase`: most non-shoot variants do
NOT ship those keys. Risk is latent (mod-authored or future-edited
records could trip it).

**Recommendation**: harden the remaining writes to gate on
`struct_name` for symmetry, even if no current data exercises them.
Carry as a Phase 2 item: "loader_pdbase.c parser: complete the
variant-gating pass (line 998-1007 + any others)".

## 5. Cross-cutting risks / structural drift

### 5.1 Heads migration interaction (Catalog Gate 3 F1+F12)

The Catalog Gate 3 Heads work
([context/designs/catalog/catalog-full-pipeline-heads.md](../designs/catalog/catalog-full-pipeline-heads.md)
if present, otherwise tracked under the same
[catalog-full-pipeline-weapons.md](../designs/catalog/catalog-full-pipeline-weapons.md)
template) introduces a parallel `s_Heads[152]` mirror. It does not yet
shift any of the body / head SP-fallback registrations in
`assetcatalog_base.c:776-829`, but a future move of head DATA to a
`heads.pdbase` archive will need the same parser-discipline as
weapons (variant gating, no cross-write corruption).

**Recommendation**: heads parser should be reviewed against the
S484-followup-4 fix list before its data migration ships. If the
heads parser uses the same pattern as the weapons parser, the same
class of latent bug exists.

### 5.2 Body / head shared `g_HeadsAndBodies[]` field offsets

`g_HeadsAndBodies[i]` is a single 152-entry array with `headorbody`
struct
([src/include/types.h:3083](../../src/include/types.h:3083)). Both
ASSET_BODY and ASSET_HEAD entries reference index `i` in this same
array. If the heads migration shifts field offsets in `headorbody`,
every ASSET_BODY consumer that reads `g_HeadsAndBodies[bodynum].field`
sees the same shift. The cross-cut is contained as long as both
domains use the catalog's typed accessors (`catalogGetBodyFilenumByIndex`
etc.) instead of raw struct reads.

**Audit to land in Phase 2**: grep every `g_HeadsAndBodies[` raw read
in `src/game/` and confirm each is either inside the catalog API
(legitimate) or has been migrated to a typed accessor. Any caller still
doing raw reads is at risk.

### 5.3 Boot-order assumption: modmgr enable-state restore is AFTER scan

`modmgrLoadComponentState()` is called at
[port/src/main.c:355](../../port/src/main.c:355), AFTER the catalog
scan. So a disabled-mod entry IS registered (during scan) and THEN
flipped to `enabled = 0`. The selector iterators (per the Universality
Sweep B-303 fix) skip `enabled == 0`. **Correct as designed.**

**Risk**: if a future refactor moves `modmgrLoadComponentState()`
before the scan, disabled mods would never register at all and re-enable
would silently fail. Add a comment at both call sites pinning the order.

## 6. Closure criteria + Phase 2 sequence

Each Phase 2 commit is its own merge per `feedback_auto_merge_by_default`.
Sequential, queued build only (`devtools\build-session.ps1`). Standing
rules continue.

### Recommended Phase 2 order (smallest first)

1. **Stage scene file binding (Section 3.A)** -- new
   `assetCatalogRegisterStageSceneFiles()` walks ASSET_MAP entries,
   binds bg/tile/pads/setup/mpsetup as ASSET_MODEL (or per-class new
   types if Mike prefers semantic separation). Five consumer call
   sites migrate to `assetLoadToNew(catalogResolveFile(filenum), ...)`.
2. **Lang bank file binding (Section 3.E)** -- one-line addition in
   the ASSET_LANG registration loop to set `source_filenum` +
   `catalogSetPrimaryRomFilenum`. Then `langLoad`, `langLoadToAddr`,
   `langPrintInfo` migrate to handle-based load.
3. **Loader_pdbase.c parser variant-gating completion (Section 4.4
   latent risk)** -- gate the remaining writes at lines 998-1007 on
   `struct_name`. No data exercises the gap today; this is a
   structural hardening commit.
4. **Falcon 2 secondary text root cause (Section 4.2)** -- once the
   diag log from `LOADER.PDBASE.WEAPON.STORED` is in hand, fix is
   either parser-side (one of the variant-gating bugs) or
   consumer-side (UI label render reads wrong source). Specific fix
   determined post-log.
5. **Farsight SFX root cause (Section 4.3)** -- dump
   `g_AudioRussMappings[0x13E]` and the `loader_pdbase_enums.c`
   SFX_813E entry. Fix is data-side or table-side; the architectural
   SFX-alias-coverage gap is a separate, larger commit.
6. **SFX alias range registration (Section 3.D, optional larger)** --
   register the alias-mapped SFX_8000..SFX_FFFF range as ASSET_AUDIO
   so mods can override aliases. Larger commit; may defer.

The Catalog Gate 3 Bodies and Arenas data moves can resume after
Commit 1 lands (stage scene file binding). They do not interact with
Commits 2-5. Commit 6 is its own larger track and can land later.

### Closure tracking

| # | Item | Status |
|---|---|---|
| 3.A | Stage scene files (bg/tile/pads/setup/mpsetup) | CLOSED Phase 2 Commit 1 (dev `8df41e59`) |
| 3.B | Animation file binding | NOT APPLICABLE (one ROM segment, no per-anim filenum) |
| 3.C | Prop instance vs category | NOT APPLICABLE (instances ride ASSET_MODEL) |
| 3.D | SFX alias range | ACCEPTED LIMIT (Phase 2 Commit 6 deliberation, 2026-05-02). Catalog already supports leaf-level overrides via the existing 1545 ASSET_AUDIO base entries (sound_id 0..0x608 -> source_soundnum -> s_SoundnumOverride). Alias-range IDs (0x8000+) decode to confignum + russ-mapping inside snd.c BEFORE catalogResolveSound runs, so mods can already override the post-mapping leaf. Direct alias override would require growing LOAD_MAX_SOUNDS from 4096 to 65536 (256KB array) plus parallel-index plumbing for marginal value. The user-visible Farsight bug closed via Commit 4 (L_GUN regen) and S484-followup-5 (SFX enum drift); no live alias-override use case requested. Phase 3 (data-on-disk migration) is the better venue if/when alias-level mod control becomes a use case. |
| 3.E | Lang bank file binding | CLOSED Phase 2 Commit 2 (dev `7034b115`) |
| 3.F | Body / head LOD | NOT APPLICABLE |
| 3.G | Casing / muzzle flash | COVERED (cartridges) + N/A (muzzle = GBI synth) |
| 3.H | Voice lines | OPEN (low priority) -- defer or accept SFX bank ownership |
| 3.I | UI chrome assets | TRACKED ELSEWHERE (rom-extraction-audit-2026-04-30 Section 3) |
| 3.J | Effect / particle defs | NOT APPLICABLE (imperative pipeline today) |
| 4.1 | Farsight 907 | CLOSED (S484-followup-2) |
| 4.2 | Falcon 2 sec text | CLOSED Phase 2 Commit 4 (dev `68fb0ae3`) -- L_GUN enum table regenerated, 8 phantom L_GUN_050..057 entries removed, L_GUN_058+ values restored to source-of-truth gun.h |
| 4.3 | Farsight SFX | CLOSED (S484-followup-5 commit `6aaabf44` + Phase 2 Commit 4 verification) -- SFX_813E correct at 33086 in current loader_pdbase_enums.c |
| 4.4 | Recoil crash | CLOSED (S484-followup-4) + latent variant-gating CLOSED Phase 2 Commit 3 (dev `5a12c1c5`) |
| 5.1 | Heads migration parser discipline | RECOMMEND review before Gate 3 Heads ships |
| 5.2 | g_HeadsAndBodies raw reads | RECOMMEND grep audit in Phase 2 |
| 5.3 | Boot-order pinning | RECOMMEND comment-only |

## Cross-references

- Earlier scoped stub (now superseded by this doc):
  `context/audits/catalog-coverage-audit-2026-05-01.md` (this file).
- Universality Sweep selector audit:
  `context/audits/catalog-universality-sweep-2026-05-01.md` (B-303 +
  netmenu.c migration).
- Weapon proving-domain design (the Manager + .pdbase pattern):
  `context/designs/catalog/catalog-full-pipeline-weapons.md`.
- ROM-extraction architecture audit (UI extraction + per-asset-class
  schema):
  `context/audits/rom-extraction-audit-2026-04-30.md`.
- Catalog pillar:
  `context/pillars/catalog.md`.
