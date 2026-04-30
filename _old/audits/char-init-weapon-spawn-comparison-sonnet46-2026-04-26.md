# Char Init + Weapon Spawn: PD2 vs Upstream Comparison

> PD2 baseline is commit a30f3719, the dev HEAD before the bondgun per-hand matrix cache fix (Phase B, B-246 round 10) landed. This represents the broken state. Current dev tip ca23a3d2 has the fix applied.

Auditor: claude-sonnet-4-6
Date: 2026-04-27 (corrected run)
Session scope: character initialization, weapon slot assignment, equip path, hand model setup, weapon render path

---

## Upstream Reference

Repository: https://github.com/fgsfdsfgs/perfect_dark.git
Clone path: /tmp/upstream_pd_decomp_sonnet46
HEAD SHA: bed3bf52d0d5095d112940b1327ed6c256e54ea8
HEAD date: 2026-04-25 19:31:47 +0200

Files examined:
- src/game/playermgr.c
- src/game/player.c
- src/game/bondgun.c
- src/game/setup.c

---

## PD2 Reference (commit a30f3719 -- pre-Phase-B-fix baseline)

Repository: C:/Users/mikeh/Perfect-Dark-2/perfect_dark-mike
Baseline SHA: a30f3719
Baseline date: 2026-04-26 23:46:30 -0400
Branch at time of audit: claude/frosty-mayer-bf0154 (worktree)

This commit is the state immediately before the bondgun per-hand matrix cache
fix (B-246 round 10 Phase B) landed. The CARTS phase of bgunTickMasterLoad
still writes hand->unk0dd8 for HAND_RIGHT only (player->hands[0]), and the
idle render path continues to reference this cached matrix buffer when a0=true.

---

## Mission-Start to Weapon-Render Path (Side-by-Side)

The call chain from mission load through first weapon render frame:

```
[Stage load trigger]
  -> playermgrAllocatePlayers()          playermgr.c
       -> playermgrAllocatePlayer(i)     playermgr.c
  -> setCurrentPlayerNum(0)              playermgr.c
  -> [net players allocated if MP]       playermgr.c (PD2 addition)
  -> playerLoadDefaults()                player.c
  -> [intro cmd parsing: INTROCMD_WEAPON -> invGiveSingleWeapon / invGiveDoubleWeapon]
                                         player.c
  -> playerSpawn()                       player.c
     -> [chr body attached, haschrbody set]
  -> [per-frame tick begins]
  -> bgunTickGameplay()                  bondgun.c
       -> bgunTickSwitch()
            -> bgunTickSwitch2()         bondgun.c
                 -> [switchtoweaponnum resolved -> bgunSetGunMemWeapon()]
       -> bgunTickLoad()                 bondgun.c
            -> bgunTickMasterLoad() x N  bondgun.c  [multi-call per frame]
                 FLUX -> HANDS -> GUN -> CARTS -> LOADED
  -> [per-frame render]
  -> bgun0f0a5550()  (hand render fn)   bondgun.c
       a0 decision -> cached unk0dd8 path OR modelSetMatricesWithAnim fresh path
```

Key invariant before first render: masterloadstate must reach MASTERLOADSTATE_LOADED
and unk0dd8 (HAND_RIGHT) must be a valid Mtxf pointer inside gunmem for the
cached matrix path to be safe.

---

## Per-Function Diff Catalog

### playermgr.c -- playermgrReset()

Upstream (bed3bf52):
- Four hard-coded NULLs: players[0..3] = NULL
- playerorder[0..3] = 0..3 (hard-coded loop unroll)
- No conditional

PD2 a30f3719 (playermgr.c):
- Wraps player slot clears in `#if MAX_PLAYERS > 4` / `#else` block
- When MAX_PLAYERS > 4: uses a for-loop over all slots
- playerorder loop uses `for (s32 i ...)` iterating MAX_PLAYERS

Change class: MAX_PLAYERS generalization. PD2 supports more than 4 players
in netplay. No behavioral difference for <= 4 player configurations.

### playermgr.c -- playermgrAllocatePlayers()

Upstream:
- Solo branch calls `playermgrSetViewSize(playerGetFbWidth(), playerGetFbHeight() * 2)`
  when `g_Vars.fourmeg2player` is true; otherwise the single-height form.
- No netplay allocation call.

PD2 a30f3719:
- `fourmeg2player` branch removed entirely. Solo branch always calls
  `playermgrSetViewSize(playerGetFbWidth(), playerGetFbHeight())` with no
  doubling. `IS4MB()` is a compile-time 0 in PD2.
- When count > 0: calls `netPlayersAllocate()` if `g_NetMode && stagenum`
  is not TITLE/CITRAINING. This is a PD2 addition; upstream has no equivalent.

Change class: N64 4MB/2-player workaround removed; netplay allocation added.

### playermgr.c -- playermgrAllocatePlayer()

Upstream:
- `visionmode` init is inside `#if VERSION >= VERSION_JPN_FINAL` guard.
  On non-JPN_FINAL builds, visionmode is never initialized here.
- `gunctrl.handmodeldef` and `gunctrl.cartmodeldef` are NOT initialized.
  Only `gunctrl.gunmodeldef` receives a NULL assignment.
- No net-related fields.
- No jump/wantsjump fields.

PD2 a30f3719 (playermgr.c):
- `visionmode` init is unconditional. VERSION gate dropped.
- `gunctrl.handmodeldef = NULL` and `gunctrl.cartmodeldef = NULL` added
  unconditionally after gunmodeldef. Comment attributes this to B-246 round-7:
  mempAlloc does not zero-fill; without explicit init these would hold heap
  garbage.
- `wantsjump = false`, `jumpconsumed = true` added (PC input extension).
- `client = NULL`, `ucmd`, `isremote = false` added (netplay fields).
- MAX_PLAYERS > 4 conditional wraps the player slot null-fill loops.

Change class: uninit bug fixes (visionmode, handmodeldef, cartmodeldef);
netplay field init; jump input extension.

### playermgr.c -- playermgrGetPlayerNumByProp()

Upstream:
- No NULL guard on player slot: `if (prop == g_Vars.players[i]->prop)`
  dereferences directly.

PD2 a30f3719:
- Adds `if (!g_Vars.players[i]) continue;` before the prop dereference.

Change class: null safety for MP scenarios where player slots may be sparse.

### playermgr.c -- playermgrShuffle()

Upstream:
- No MP shuffle guard. Shuffles unconditionally.

PD2 a30f3719:
- Early return if `g_NetMode`. Comment: "don't shuffle in netgames".

Change class: netplay behavioral gate.

### playermgr.c -- playermgrGetModelOfWeapon()

Upstream has additional weapon cases not present in PD2 a30f3719:
- WEAPON_PP9I, WEAPON_CC13, WEAPON_KL01313, WEAPON_KF7SPECIAL,
  WEAPON_ZZT, WEAPON_DMC, WEAPON_AR53, WEAPON_RCP45

PD2 a30f3719 does not include these weapon model mappings. Those weapons are
not part of the AllInOneMods weapon set.

Change class: weapon roster difference. No behavioral effect for weapons
present in both.

### player.c -- playerReset() / INTROCMD_WEAPON branch

Upstream (bed3bf52):
- INTROCMD_WEAPON branch calls `invGiveDoubleWeapon` or `invGiveSingleWeapon`
  based on cmd[2] value.
- No diagnostic logging.

PD2 a30f3719:
- INTROCMD_WEAPON branch: same logic, but preceded by comment referencing
  B-246 round instrumentation.
- `playerLoadDefaults()` emits a LOG.WPN.DIAG sysLogPrintf line before
  executing: captures visionmode, cameramode, haschrbody, gunctrl_wpn,
  switchtoweaponnum, gunmemowner.

Change class: diagnostic instrumentation added; core logic identical.

### player.c -- playerLoadDefaults()

Upstream:
- Minimal. Sets eyeheight, headheight, globaldraws, cameramode, movement
  state, health, crosshair, colours, bondleandown, and a small set of
  device/training fields.
- No logging.

PD2 a30f3719:
- All upstream fields plus: `autoaimdamp` field assignment added,
  `usinggoggles`, NV fields, overexposure colour fields, `amdowntime`,
  `altdowntime` added (PC input extensions).
- LOG.WPN.DIAG sysLogPrintf at entry and later at PLAYER_SPAWN.
- `stageGetIndex()` call to adjust initial bondhealth for DUEL and MAIANSOS
  stages -- upstream does not do per-stage health init here.

Change class: PC input/optics extensions; per-stage health init; diagnostics.

### bondgun.c -- file-level globals

Upstream:
- VERSION-gated BSS blocks for audio handles and fireslots (PAL_BETA /
  NTSC_1_0 / else variants).
- `g_BgunGunMemBaseSizeDefault`: on `PLATFORM_64BIT` set to 150*1024*2,
  else 150*1024. Also has `g_BgunGunMemBaseSize4Mb2P = 120*1024*2 / 120*1024`.
- `g_BgunGeMuzzleFlashes` under `#ifndef PLATFORM_N64`.
- No `assetcatalog.h` or `assetload.h` includes.

PD2 a30f3719 (bondgun.c lines 1-90):
- VERSION-gated blocks removed. Single unified declaration for all audio
  handles and fireslots (no VERSION branching).
- `g_BgunGunMemBaseSize4Mb2P` removed. PD2 dropped the 4MB-2P gunmem pool.
  Only `g_BgunGunMemBaseSizeDefault` remains.
- `g_BgunGeMuzzleFlashes` unconditional (no PLATFORM_N64 guard needed).
- Adds `#include "assetcatalog.h"`, `#include "assetload.h"`, `#include
  "system.h"` for catalog-backed hand file lookup and instrumentation.
- Adds `MASTERLOADSTATE_CARTS` define (same as upstream; both codebases have
  all five MASTERLOADSTATE values).

Change class: N64 version fragmentation removed; 4MB pool removed; PC-only
includes added.

### bondgun.c -- bgunTickMasterLoad() (the critical function)

This function drives the FLUX->HANDS->GUN->CARTS->LOADED state machine that
loads gun models, hand models, and cartridge models per weapon switch.

Upstream (bed3bf52), HANDS state:
- When `hashands` and hand file differs: sets up `handmemloadptr` /
  `handmemloadremaining` pointing to base of `bgunGetGunMem()`.
- Hand file loaded via `bgunTickGunLoad()` which calls the generic
  `assetLoadRomToAddr()` path.
- `player->gunctrl.loadtomodeldef = &player->gunctrl.handmodeldef`.

PD2 a30f3719, HANDS state:
- Replaces the inline hand file lookup with `catalogGetBodyHandFilenum(bodynum)`.
  `bodynum` comes from `playerChooseBodyAndHead(&bodynum, &headnum, NULL)`.
  This is the asset catalog integration (SA-5d).
- `catalogGetBodyHandFilenum` returns a file number looked up by body index
  rather than a hard-coded file constant.
- Rest of hand load logic identical.

Change class: catalog-backed hand file resolution. Asset catalog replaces
direct file number.

Upstream (bed3bf52), CARTS state completion (bondgun.c lines 4485-4494):
```
hand = &player->hands[0];
hand->unk0dd4 = -1;

if (player->gunctrl.memloadremaining > 50 * sizeof(Mtxf)) {
    hand->unk0dd8 = (Mtxf *) player->gunctrl.memloadptr;
    player->gunctrl.memloadptr += 50 * sizeof(Mtxf);
    player->gunctrl.memloadremaining -= 50 * sizeof(Mtxf);
} else {
    hand->unk0dd8 = NULL;
}
```

PD2 a30f3719, CARTS state completion (bondgun.c lines 4489-4493):
Identical code. `hand = &player->hands[0]` (HAND_RIGHT index 0). unk0dd8
is written only for HAND_RIGHT. HAND_LEFT never receives an unk0dd8 assignment
in this path.

**This is the B-246 bug shape at a30f3719.** The Phase-B fix (landed at
ca23a3d2) adds `player->hands[1].unk0dd8 = NULL` immediately after the
HAND_RIGHT assignment to clear any stale pointer on HAND_LEFT. At a30f3719,
that line is absent.

Idle render path (bgun0f0a5550, bondgun.c line 8452):
```
renderdata.unk10 = player->hands[HAND_RIGHT].unk0dd8;
```
This path reads `hands[0].unk0dd8` when `a0=true` (idle/cached matrix mode).
On the first weapon load, `hands[0].unk0dd4 == -1`, so the cache-fill branch
executes: `modelSetMatricesWithAnim` is called with `renderdata.unk10` pointing
to the newly allocated cache buffer. Thereafter `hands[0].unk0dd4 = 1` and
subsequent idle frames use the cached matrices from `unk0dd8`.

HAND_LEFT (`hands[1]`) has no corresponding unk0dd8 assignment. Its unk0dd8
holds whatever was in the mempAlloc slab at allocation time (uninitialized).

### bondgun.c -- bgunFreeGunMem() and videoResetTextureCache()

Upstream:
- Sets `gunmemowner = GUNMEMOWNER_FREE`. No texture cache interaction.

PD2 a30f3719:
- Same ownership clear, plus `videoResetTextureCache()` call. Comment
  explains: fast3d texture cache is keyed by GBI load parameters and has no
  ranged-evict API; full clear is required when gunmem is released.

Change class: fast3d texture cache coherence. N64 had no texture cache to
invalidate; PC renderer caches textures by address/format so a full evict
is required on gunmem release.

### bondgun.c -- bgunSetGunMemWeapon()

Upstream: sets masterloadstate = MASTERLOADSTATE_FLUX, gunloadstate = FLUX,
gunmemnew, gunlocktimer = -1 when owner is BONDGUN. No logging.

PD2 a30f3719: identical logic plus LOG.WPN.DIAG sysLogPrintf at entry and
exit. No behavioral change.

### bondgun.c -- bgunEnterFlux()

Upstream: sets handfilenum=0xffff, handmodeldef=NULL, handmemloadptr=0,
handmemloadremaining=0, masterloadstate=FLUX, gunloadstate=FLUX. Clears
casing modeldef pointers.

PD2 a30f3719: identical logic plus LOG.WPN.DIAG sysLogPrintf instrumentation.
No behavioral change.

### bondgun.c -- bgunChangeGunMem()

Upstream: core state machine for gunmem ownership transfer. GUNMEMOWNER_BONDGUN
branch sets gunmemtype=-1, calls bgunEnterFlux(), sets loadall=true, sets
unlock=true. GUNMEMOWNER_CHRBODY branch checks mplayerisrunning and haschrbody.

PD2 a30f3719: identical ownership logic plus extensive LOG.WPN.DIAG
instrumentation: throttled enter log, gunmemnew range-watch (catches wild
values outside -1..100), throttled exit log. Static local `s_last_pair`
for transition-only logging.

Change class: diagnostic instrumentation only. No behavioral change to the
core ownership transfer.

### bondgun.c -- bgunIsLoaded()

Upstream and PD2 a30f3719: identical. Returns true when owner==BONDGUN and
(gunmemtype==WEAPON_NONE OR (gunmemnew < 0 AND masterloadstate==LOADED)).

### bondgun.c -- visibility gate in bgun0f0a5550

Upstream (bed3bf52, line ~7890 area):
- Checks `(hand->mode == HANDMODE_6 || hand->mode == HANDMODE_7)` directly
  to gate visibility.

PD2 a30f3719 (bondgun.c lines 8125-8178):
- Issue 1 root-cause fix comment (2026-04-25, B-246). Legacy gate replaced
  with `inHideTransition` scope: hide on mode 6/7 ONLY when
  state==HANDSTATE_CHANGEGUN AND stateminor is LOWER or RAISE.
- This prevents the weapon being invisible when the state machine is stuck
  in LOAD/HANDMODE_6 while masterloadstate==LOADED.

Change class: visibility gate refactor for stuck-mode recovery. Behavioral
difference: in the case where hand->mode=6 but state machine has otherwise
completed, upstream would show invisible weapon; PD2 would show the weapon.

### bondgun.c -- bgunCalculateGunMemCapacity()

Upstream:
- Two-branch: PLAYERCOUNT()==1 uses BaseSizeDefault + extragunmem;
  otherwise uses BaseSizeDefault.
- No 4MB branch (already removed in upstream).

PD2 a30f3719:
- Identical to upstream. The `g_BgunGunMemBaseSize4Mb2P` pool was removed.
  The 64-bit doubling is baked into `g_BgunGunMemBaseSizeDefault` itself at
  file-global level.

### setup.c -- file-level additions in PD2

Upstream:
- Declares only `g_SetupCurMpLocation`.
- Includes: game headers, lib headers, data.h, types.h only.

PD2 a30f3719:
- Adds `s_SetupMpWeaponLocationCount` and `s_SetupMpCreatedWeaponCount`
  (static MP weapon placement counters).
- Adds `#include "assetcatalog.h"`, `#include "assetload.h"`,
  `#include "net/matchsetup.h"`, `#include "game/spawnpool.h"`,
  `#include "game/forgemode.h"`, `#include "game/mplayer/participant.h"`.
- `langManifestRecordBank()` forward declaration for Phase 3 manifest tracking.

Change class: MP weapon placement tracking; catalog/net includes; lang
manifest tracking.

### setup.c -- propsReset()

Upstream: inline slot counts (MaxWeaponSlots=50, MaxHatSlots=10, etc.);
no conditional override when at STAGE_TITLE.

PD2 a30f3719: same slot counts, but adds a conditional block that zeroes all
limits when `g_Vars.stagenum >= STAGE_TITLE` (prevents wasted allocation on
title screen). Also: `g_MaxProjectiles = IS4MB() ? 20 : 100` retains the
compile-time IS4MB() call (always 0 on PC, so always 100).

Change class: title-screen allocation guard; IS4MB() is compile-time 0.

### setup.c -- setupLoadStage() / weapon placement

PD2 a30f3719 additions not present in upstream:
- Calls `catalogGetStageResultByIndex()` to look up stage asset entries by
  index rather than using hard-coded file numbers.
- Calls `assetLoadRomToAddr()` and `assetLoadToNew()` for stage file loading
  via the asset loader pipeline.
- `langManifestRecordBank(stagebank)` call records which language bank the
  stage loaded, for manifest tracking.
- MP weapon placement uses `catalogGetMpWeaponNum()`,
  `catalogGetMpWeaponPriAmmoType()`, `catalogGetMpWeaponPriAmmoQty()`,
  `catalogGetMpWeaponSecAmmoType()`, `catalogGetMpWeaponSecAmmoQty()` to
  derive weapon and ammo identities from the catalog instead of direct
  enum values.
- Spawn-with-weapon: `g_MatchConfig.spawnWeaponNum` populated via
  `catalogGetMpWeaponNum(setSpawnMpw)`.
- `s_SetupMpWeaponLocationCount` and `s_SetupMpCreatedWeaponCount` track
  how many pickup locations exist vs how many were instantiated.

Change class: full catalog integration for stage/weapon loading. Upstream
uses direct file numbers and enum constants; PD2 derives them at runtime
from the catalog.

---

## Catalog / N64 / ROM-Decoupling Impact Analysis

### Catalog System (asset_entry_t, runtime indices)

PD2 uses `catalogGetBodyHandFilenum(bodynum)` in bgunTickMasterLoad to
resolve the hand model file number. This means the hand model file number
is a function of the body index chosen by `playerChooseBodyAndHead()`, which
in turn queries the catalog at runtime. The upstream uses a direct file
constant derived at compile time (implicit in the MASTERLOADSTATE_HANDS
branch).

In setup.c, all stage and weapon file lookups go through `catalogGetStageResultByIndex()`
and `catalogGetMpWeapon*()` functions. The catalog maps game-facing indices
(stage index, MP weapon index) to ROM file handles. This decouples the game
code from hardcoded file numbers entirely.

The catalog lookup chain introduces a runtime dependency: if the catalog
is not populated or is populated with wrong data before bgunTickMasterLoad
runs, `catalogGetBodyHandFilenum()` may return an unexpected file number.
In that scenario, handmodeldef would be loaded from the wrong file.

### N64 Hardware Decoupling

Items removed from PD2 relative to upstream:
- `PLATFORM_N64` guards around `g_BgunGeMuzzleFlashes`, `#include "video.h"`,
  `#include "platform.h"` (upstream has these under `#ifndef PLATFORM_N64`).
- `fourmeg2player` height doubling in playermgrAllocatePlayers.
- `IS4MB()` calls in propsReset remain but evaluate to compile-time 0.
- `g_BgunGunMemBaseSize4Mb2P` pool removed.
- VERSION-gated BSS layouts in bondgun.c removed; single layout used.

Items added or changed for PC:
- `videoResetTextureCache()` in bgunFreeGunMem -- no N64 equivalent needed.
- `g_BgunGunMemBaseSizeDefault` doubled on `PLATFORM_64BIT` to accommodate
  pointer-sized fields expanding from 4 to 8 bytes after GBI preprocessing.
- `assetLoadRomToAddr()` used throughout for model loading. This replaces
  N64 DMA-based ROM loads with a PC file-system backed loader.

### ROM Version Gate Removal

Upstream has:
- `#if VERSION >= VERSION_JPN_FINAL` gate on visionmode init in
  playermgrAllocatePlayer.
- VERSION-gated BSS layouts (PAL_BETA, NTSC_1_0, else) in bondgun.c globals.
- `#if VERSION >= VERSION_NTSC_1_0` gate on NTSC_1_0 specific code in
  bgunRumble and bgunTickMasterLoad shortcut path.
- `#if PIRACYCHECKS` gate around ROM checksum verification in bgunTickGunLoad.

PD2 a30f3719:
- All VERSION gates dropped. visionmode init is unconditional.
- BSS layout is unified (no PAL/NTSC fragmentation).
- bgunRumble NTSC_1_0 path is the only path present (PC always targets the
  NTSC_1_0 logic, which uses joyGetContpadNumsForPlayer).
- PIRACYCHECKS block absent.

---

## Hypotheses: What Could Be Broken

### H-1: HAND_LEFT unk0dd8 uninitialized at first weapon load (HIGH)

This is the described B-246 bug shape at a30f3719.

`bgunTickMasterLoad` CARTS completion writes `player->hands[0].unk0dd8`
(HAND_RIGHT) to a valid Mtxf buffer inside gunmem. It does not write
`player->hands[1].unk0dd8` (HAND_LEFT). `mempAlloc` does not zero-fill.
`playermgrAllocatePlayer` copies a zeroed `hand` struct initializer into
both `hands[0]` and `hands[1]`, so unk0dd8 starts as zero (null) at
player allocation. However, between player allocation and the first
bgunTickMasterLoad completion, HAND_LEFT unk0dd8 remains whatever the
`hand` struct initializer produced -- which for unk0dd8 is implicitly zero
(not explicitly listed in the hand initializer in playermgr.c).

The render path (bgun0f0a5550) reads `player->hands[HAND_RIGHT].unk0dd8`
(i.e., `hands[0]`) on the cached path. HAND_LEFT does not appear to use its
own unk0dd8 in the same cached-matrix section. The confirmed bug shape is
therefore: on a weapon change (second weapon switch or round restart), the
CARTS completion path does NOT zero out `hands[1].unk0dd8`. If an earlier
load cycle had placed a valid pointer there (via any path that writes it),
that pointer MAY now dangle into stale gunmem from the previous weapon, since
gunmem is reused across weapon switches.

References: bondgun.c (a30f3719) lines 4485-4510 (CARTS completion);
bondgun.c (a30f3719) line 8452 (render read).

### H-2: handmodeldef and cartmodeldef uninit during fast-path first frame (MEDIUM)

At a30f3719, `playermgrAllocatePlayer` explicitly NULLs `handmodeldef` and
`cartmodeldef` (B-246 round-7 fix). So on initial player allocation this is
safe. However: if `bgunEnterFlux()` is called between allocation and first
masterload tick, it NULLs `handmodeldef` again and sets `handfilenum=0xffff`.
On a subsequent `bgunChangeGunMem` -> `bgunEnterFlux` call triggered by a
weapon switch, `gunmodeldef` is NOT cleared (only `handmodeldef` is cleared
in bgunEnterFlux). A stale gunmodeldef pointer could persist from a previous
weapon load if the CARTS state is bypassed via the SHORTCUT->LOADED path
(filenum==0 branch). This MAY cause the render path to use a modeldef from a
previous weapon for one frame.

References: bondgun.c (a30f3719) bgunEnterFlux (~line 3850); bgunTickMasterLoad
SHORTCUT path (~line 4516).

### H-3: catalogGetBodyHandFilenum returning wrong filenum if catalog not ready (MEDIUM)

`bgunTickMasterLoad` HANDS state calls `catalogGetBodyHandFilenum(bodynum)`
where bodynum comes from `playerChooseBodyAndHead()`. If the catalog is not
fully populated at the time bgunTickMasterLoad first runs (e.g., if stage
asset loading is still in progress), this function MAY return 0 or an
unexpected filenum. A filenum of 0 would cause bgunTickMasterLoad to take
the "no hands" branch (hashands stays false), loading no hand model. The
player would render with gunmodel matrices only, potentially misaligned
against the hand rig.

References: bondgun.c (a30f3719) ~line 4268 (catalogGetBodyHandFilenum call).

### H-4: netPlayersAllocate() called before player structs fully initialized (LOW)

In `playermgrAllocatePlayers`, when count > 0, `netPlayersAllocate()` is
called after the player allocation loop but before `setCurrentPlayerNum(0)`.
This ordering means net player state is initialized while `g_Vars.currentplayer`
is still NULL (setCurrentPlayerNum has not yet run). Any code inside
`netPlayersAllocate()` that dereferences `g_Vars.currentplayer` MAY fault
or produce undefined behavior. Upstream has no such call.

References: playermgr.c (a30f3719) ~line 64-68 (count > 0 branch).

### H-5: Visibility gate change causing weapon to appear one frame early (LOW)

PD2 a30f3719 refines the visibility gate (B-246 Issue 1 fix): hides weapon
on mode 6/7 only when stateminor is LOWER or RAISE, not during LOAD. Upstream
would hide the weapon throughout any mode 6 or 7 frame. The PD2 change means
the weapon MAY become visible during the LOAD stateminor while the model is
still being positioned by the change-gun animation. If the gunmodel matrices
have not been finalized by modelInit at that moment, this could produce a
one-frame visual glitch where the gun appears at an incorrect position before
settling.

References: bondgun.c (a30f3719) lines 8125-8178 (inHideTransition gate).

### H-6: Stale per-hand unk0dd8 pointer on WEAPON_NONE switch (MEDIUM)

When `bgunTickSwitch2` switches to `WEAPON_NONE`, it sets `lefthand->inuse =
false` and `righthand->inuse = false` and sets `ctrl->weaponnum = WEAPON_NONE`.
It does NOT call `bgunEnterFlux` or zero unk0dd8. On the subsequent weapon
equip, `bgunTickMasterLoad` runs CARTS completion which writes `hands[0].unk0dd8`
to a new location. But `hands[1].unk0dd8` still points to the previous load's
buffer region within gunmem. When bgunLoadAll() re-enters (e.g., after eyespy
or menu), the synchronous `do { bgunTickMasterLoad(); } while (!bgunIsLoaded())`
loop will overwrite gunmem from the start, potentially causing the old
`hands[1].unk0dd8` to now alias to the new gun's data at a different
interpretation.

References: bondgun.c (a30f3719) bgunTickSwitch2 (~line 5820), bgunLoadAll
(~line 4561), CARTS completion (~line 4485).

### H-7: MAX_PLAYERS > 4 playerorder loop initializing beyond actual player count (LOW)

In PD2, `playermgrReset()` iterates `for (s32 i = 0; i < MAX_PLAYERS; ++i)`
to initialize playerorder. If MAX_PLAYERS > 4, this initializes slots beyond
the four that upstream touches. No behavioral problem unless code elsewhere
iterates only the first four playerorder slots and the extra slots have stale
values. Because the loop fills them sequentially, this is unlikely to cause
a live bug but represents a divergence from the upstream contract.

References: playermgr.c (a30f3719) playermgrReset loop.

---

## Cross-Cuts: Related Subsystems

### Asset Catalog (assetcatalog.h)

PD2 integrates the catalog throughout the weapon-spawn path:
- `catalogGetBodyHandFilenum()` in bgunTickMasterLoad for hand model lookup.
- `catalogGetStageResultByIndex()`, `catalogGetMpWeapon*()` in setup.c for
  stage/weapon file resolution.

If the catalog API changes its return conventions (0-indexed vs 1-indexed,
sentinel for "no file" vs NULL), all three call sites are affected
simultaneously. The catalog is a shared dependency across the full init path.

### Texture Cache (videoResetTextureCache)

`bgunFreeGunMem()` now calls `videoResetTextureCache()`. This function is
specific to the fast3d PC renderer. Any code path that calls `bgunFreeGunMem()`
during a frame (e.g., eyespy return, menu exit, weapon switch) will flush the
entire texture cache. On stages with many textures, this MAY cause a one-frame
hitch as textures are re-uploaded to the GPU. The hitch does not exist in
upstream (N64 had no texture cache to flush).

### netPlayersAllocate() (net/net.h)

Called from playermgrAllocatePlayers in the MP case. This is a PD2-only
addition with no upstream equivalent. Its interaction with the player struct
initialization order (called before setCurrentPlayerNum) is a potential
ordering hazard noted in H-4 above.

### bgunLoadAll() / synchronous spin

`bgunLoadAll()` uses a synchronous `do { bgunTickMasterLoad(); } while`
loop. In PD2, each call to `bgunTickMasterLoad()` may invoke
`catalogGetBodyHandFilenum()`. If that function has any side effects or
mutates global state, the synchronous spin could call it multiple times
unexpectedly (once per FLUX->HANDS transition that retries). Upstream does
not have this concern because the hand file is a constant.

### stageGetIndex() in playerLoadDefaults

PD2 a30f3719 calls `stageGetIndex(g_Vars.stagenum)` inside `playerLoadDefaults()`
to set stage-specific initial health. Upstream does not. `stageGetIndex` queries
the stage table. If the stage table is not loaded when playerLoadDefaults runs,
this call MAY return an incorrect index, setting wrong initial health. The
interaction with the asset catalog stage load ordering is a latent hazard.

---

*End of audit. File represents the broken state at a30f3719 before Phase-B fix.*
