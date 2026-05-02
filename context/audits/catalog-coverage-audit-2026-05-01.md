# Catalog coverage audit — 2026-05-01

> Status: SCOPED, not started. Filed in response to Mike's no-half-measures
> directive after the bgun weapon-model regression
> (filenum=907 / FILE_GZ2020 / Farsight) was triaged and closed in
> S484-followup. This audit surfaces parallel coverage gaps in other
> asset types so they can be closed before they bite a player.

## Premise (from Mike, 2026-05-01)

> "The correct solution is not to fallback to legacy, but to strengthen
>  our initial cataloging to be full, correct, and complete."

The catalog is the SOLE pipeline for asset resolution. Any code path
that loads an asset by filenum MUST go through the catalog (handle ->
provider -> data). Legacy direct-from-ROM loads are forbidden -- they
silently obscure registration gaps and remove the pressure to fix them.

The Farsight regression surfaced because:

1. The S484 F4 refactor (commit ecc9d880) routed bgun loads through
   `catalogHandleByModelSourceFilenum` and removed the
   `assetLoadRomToAddr(filenum, ...)` fallback.
2. The catalog had ASSET_MODEL entries for chr-side prop models
   (`g_ModelStates[]`) and hand-model files (`g_HeadsAndBodies[].handfilenum`)
   but NOT for gun-side files (`weapon::hi_model`, `weapon::lo_model`,
   `g_CartFileNums[]`).
3. Every weapon-switch attempted a catalog lookup that missed, the load
   failed, gunloadstate flipped to FLUX, and the master-load retried
   the same miss next tick. The CATALOG_CRITICAL log flooded; weapons
   never finished loading; fire fell through to melee.

The closure (S484-followup-2):
- New `assetCatalogRegisterWeaponModelFiles()` walks the loader-populated
  weapon pool plus `g_CartFileNums[]` and registers every filenum as
  ASSET_MODEL with source_filenum binding. Idempotent.
- `bondgun.c::bgunQueued{LoadToAddr,GetInflatedSize,GetLoadedSize}` were
  reverted to strict-catalog-only (no ROM fallback).
- The CATALOG_CRITICAL log throttle (one per filenum) stays as the
  loud-but-not-spammy surface for any remaining gap.

This audit catalogues other asset types that may have parallel gaps.

## Audit scope and method

For each asset type, identify:
- WHERE filenum-based loads happen in game code (`grep -r "assetLoadRom"`,
  `grep -r "fileGetLoaded"`, `grep -r "->filenum"`).
- WHETHER each filenum source (struct field, table, manifest) is
  exhaustively reflected in catalog ASSET_* entries with source_filenum
  set.
- ANY gap is a bug to register, not a bug to route around.

Method:
1. Walk every consumer of asset loads in game code.
2. Trace each consumer's filenum source.
3. Cross-reference against catalog registration paths
   (`port/src/assetcatalog_base*.c`, `port/src/loader_pdbase.c`,
   `port/src/catalog_mgr_*.c`).
4. Mark each filenum source as COVERED, GAP, or UNKNOWN.

## Suspected gap surfaces

Each entry below is a HYPOTHESIS, not a confirmed gap. Verification
work is part of this audit.

### A. Animation files

- **Filenum source**: `weapon::equip_animation`, `unequip_animation`,
  `pritosec_animation`, `sectopri_animation` (struct weapon, loaded
  from weapons.pdbase).
- **Consumer**: `bgunStartAnimation(weapon->equip_animation, ...)` in
  bondgun.c. The chain inside likely calls `animLoadHeader(animnum)` /
  `animLoadFrame(animnum, ...)`.
- **Catalog coverage**: ASSET_ANIMATION entries are registered in
  `assetcatalog_base_extended.c` for indices 0x0000..0x04B6. Whether
  these are looked up by ANIMNUM (the array index) or by FILENUM (the
  ROM file ID) is the discriminator -- if by animnum, no gap (the
  registration is by index). If by filenum, gap.
- **Verification**: read `animLoadHeader` / `animLoadFrame` to confirm.

### B. Projectile model files

- **Filenum source**: `weaponfunc::projectilemodelnum` (an MODEL_*
  enum, not a filenum) plus `g_ModelStates[modelnum].fileid`.
- **Consumer**: `weaponCreateProjectileFromGset(modelnum, ...)`. The
  chain calls `bodyAllocateModel(modelnum, ...)` which routes through
  modelmgr's catalog-aware allocator.
- **Catalog coverage**: `assetCatalogRegisterBaseGameExtended()` walks
  `g_ModelStates[]` and registers each as ASSET_MODEL with
  source_filenum = `g_ModelStates[i].fileid`. So projectile model
  loads are covered IF the projectile's MODEL_* index has a
  `g_ModelStates` entry.
- **Verification**: confirm all projectile MODEL_* indices fall within
  the registered range.

### C. Cartridge / casing models (now COVERED)

Closed by `assetCatalogRegisterWeaponModelFiles` extension in this
session. Documented for completeness.

### D. Sound effect filenums

- **Filenum source**: `weaponfunc::sound_xxx` fields, audio cue tables.
- **Consumer**: `sndStart(...)` and friends. Sound load is via SFX
  bank (ASSET_AUDIO entries).
- **Catalog coverage**: `assetcatalog_base_extended.c` registers 1545
  SFX entries (full main bank, 0x0000..0x0608). Scope vs whether each
  weapon's referenced sound is in the bank: likely covered, but worth
  verifying for mod-shipped weapons that ship custom SFX.
- **Verification**: walk a sample weapon's sound references against
  the SFX registration count.

### E. Texture filenums in gun models

- **Filenum source**: gun model rodata embeds texture references.
- **Consumer**: gun model load (bgunTickGunLoad) extracts texconfigs
  from the loaded modeldef and calls texLoad.
- **Catalog coverage**: ASSET_TEXTURE entries are registered for
  NUM_TEXTURES base textures (3503 NTSC). Mod textures register on
  scan via assetCatalogScanComponents.
- **Verification**: confirm gun-embedded textures are within
  NUM_TEXTURES range, otherwise mod gun load fails.

### F. Body / head LOD model files

- **Filenum source**: `g_HeadsAndBodies[].lod_filenum` (if present),
  separate from the primary `filenum`.
- **Consumer**: chr-distance-based LOD switching may load lower-res
  body / head models.
- **Catalog coverage**: catalog registers body filenum (via
  `g_HeadsAndBodies[].filenum`) and handfilenum, not LOD variants.
- **Verification**: confirm LOD models exist as separate filenums and
  whether they're loaded.

### G. Stage-dependent files (intro, outro, lighting, audio cues)

- **Filenum source**: stage manifests, mission scripts.
- **Consumer**: stage loader.
- **Catalog coverage**: ASSET_STAGE entries cover stagenum-driven
  setup. Whether all derived files (intro PAM, outro PAM, lighting,
  per-stage audio) are individually catalog-registered is unclear.
- **Verification**: large surface; defer to a separate stage-load
  audit.

### H. Casing eject + muzzle flash effect models

- **Filenum source**: hardcoded MODEL_* references in bondgun.c
  effects code.
- **Consumer**: ejected casing prop spawn, muzzle flash particle.
- **Catalog coverage**: g_ModelStates[] covers if the effect uses a
  MODEL_* enum entry. If hardcoded filenum, gap.
- **Verification**: walk casing/muzzle code for filenum literals.

## Action items

1. (CLOSED THIS SESSION) Cartridge models registered.
2. **A. Animation files**: trace `bgunStartAnimation` call chain to
   confirm anim load is by animnum (index) not filenum.
3. **B. Projectile model files**: enumerate all
   `weaponfunc::projectilemodelnum` values across weapons; confirm each
   maps into g_ModelStates[].
4. **D. SFX**: random-sample 5 weapons; confirm referenced sound files
   resolve cleanly through the SFX catalog.
5. **E. Textures**: load a gun model and inspect numtexconfigs +
   filenums; confirm all are within NUM_TEXTURES.
6. **F. LOD models**: confirm whether body/head LOD load paths exist
   and what filenum source they use.
7. **G. Stage-dependent files**: file as a separate stage-load audit;
   too broad to fold into this one.
8. **H. Effects**: walk muzzle-flash and casing-eject code for filenum
   literals.

## Closure criteria

- Each suspected gap above marked CLOSED, COVERED, or DEFERRED with
  rationale.
- Any GAP confirmed -> register the missing source in
  `assetcatalog_base_extended.c` (or the appropriate registration
  module) with source_filenum binding.
- No defensive ROM fallback added anywhere. If a load fails, the
  failure surfaces via CATALOG.MISS warning + CATALOG_CRITICAL error
  (throttled to one per filenum).
- Doc lands in context/audits/ when audit is complete; updated as
  items close.

## Cross-references

- Closure of original Farsight regression:
  `port/src/assetcatalog_base_extended.c::assetCatalogRegisterWeaponModelFiles`
- Strict-catalog reversion in bgun:
  `src/game/bondgun.c::bgunQueued{LoadToAddr,GetInflatedSize,GetLoadedSize}`
- CATALOG_CRITICAL throttle: `src/game/bondgun.c::bgunTickGunLoad`
- Mike's no-half-measures principle: see commit message of the
  S484-followup-2 merge for the full quote and rationale.
