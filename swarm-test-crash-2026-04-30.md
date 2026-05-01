# Swarm test crash analysis (S483 follow-up, 2026-04-30)

## Crash header
```
EXCEPTION: 0xc0000005   (access violation)
PC:        0x00007ff6cc17b39f
MODULE:    [0x00007ff6cbdd0000]
MAIN MODULE: [0x00007ff6cbdd0000]
```

## PC offset (relative to main module base 0x7ff6cbdd0000)
- `0x3ab39f` -- crash site
- `0x082dc6`
- `0x0874be`
- `0x06961d`
- `0x1351d0`
- `0x0cc389`
- `0x22a798`
- `0x22b36e`
- `0x22b6bf`
- `0x1eb948`
- `0x0010d9` -- `__mingw_invalidParameterHandler`-ish region
- `0x001436`
- `BaseThreadInitThunk+23`
- `RtlUserThreadStart+44`

(addr2line will be re-run after the in-progress rebuild produces a new `Build/PerfectDark.exe`.)

## Smoking gun -- last breadcrumb before crash
```
#7263 [29.509773s] CHR.TICK slot=9 chrnum=-1 action=1 race=1 model=0000000000000000
[00:29.56] ERROR: FATAL: Crash!
```

`chrnum=-1` is the sentinel `chrRemove()` writes at the very end of removal (`src/game/chr.c:1508`). A chr slot with `chrnum=-1` AND `model=NULL` means the chr was removed but its slot is still being walked by some tick path that derefs `chr->model` without a NULL guard.

## Full leading sequence (from pd-client.log, frame 781 / time ~29.46s)
- `LVTICK frame=779 stage=0x32 update240=8 paused=0`  -- stage 0x32 = STAGE_RUINS = base:mp_skedar
- `CHRTICKBG frame=781 bg=2 slots=11 alive_on_screen=prev`  -- 11 chr slots active
- `BWALK.TICK player=0 pos=(-40,-241,-31) room=2 floorroom=2`
- `CHR.TICK slot=8 chrnum=5031 action=0 race=1 model=...`
- `CHR.TICK slot=7 chrnum=5030 action=0 race=1 model=...`
- ... slots 6,5,4,3,2,1 with chrnums 5029..5024 (8 freshly spawned skedars)
- `CHR.TICK slot=9 chrnum=-1 action=1 race=1 model=0000000000000000`  -- STALE slot (race=1 = skedar, action=1 = ACT_STAND from previous lifetime)
- crash

## Pattern over previous frames
- Many frames show the same slot ticked 2-3 times within a single timestamp:
  ```
  CHR.TICK slot=4 chrnum=5017 ...
  CHR.TICK slot=3 chrnum=5016 ...
  CHR.TICK slot=2 chrnum=5015 ...
  CHR.TICK slot=1 chrnum=5014 ...
  CHR.TICK slot=4 chrnum=5017 ...   <-- repeat
  CHR.TICK slot=3 chrnum=5016 ...
  ```
- The CHR.TICK breadcrumb is logged from multiple dispatch sites (chrtick + per-action chrUpdateTickAction etc) so the duplicates can be benign. The crash-frame anomaly is the `chrnum=-1 model=NULL` slot, not the duplication.

## Hypothesis
1. `chrRemove(prop, true)` sets `chr->chrnum = -1` and `chr->model = NULL` but DOES NOT mark the chr slot as "free" -- the chr slot remains in `g_ChrSlots[]` and `g_NumChrSlots` is unchanged. (Verified at `src/game/chr.c:1441-1513`.)
2. The chr-tick / chr-bg-tick walks `g_ChrSlots[0..g_NumChrSlots-1]` and is supposed to skip `chrnum < 0` slots, but at least one tick path in this branch (the one at PC offset `0x3ab39f`) derefs `chr->model` without that guard.
3. The 256-bot benchmark amplifies the latent bug: at small counts the stale slot is rare; at higher counts and rapid count-change cycles, the stale slot is hit on every frame within the GC window.

## Related symptoms
- Bots not moving (CPU + GPU paths)
- Bots spawn inside player on count-change (greenish texture clipping = bot mesh overlapping camera)
- Bots not cleared on count change
- Player not invincible
- Player has no visible/usable weapons (despite swarm test setup calling `invSetAllGuns(true)`)
- Bots go invisible after ~3-4 count-change cycles
- Cycle missing 48: target ladder is 4, 8, 16, 32, **48**, 64, 128, 256

## Investigation plan
1. Identify the function at PC offset 0x3ab39f via addr2line on the new build.
2. Walk the chr-bg-tick path; find any `chr->model->...` deref without a `chr->chrnum >= 0` or `chr->model != NULL` guard. Add the guard, plus a `CHR.MODEL.MISS:` LOUDFAIL warning.
3. Walk swarm_test.c spawn helpers; ensure freshly-spawned chrs are fully initialized before any tick can see them, and ensure the despawn path actually frees the chr pool slot (not just nulls fields).
4. Walk the player-init path; figure out why `invSetAllGuns(true)` and the cheat flag set are not producing visible weapons.
5. Update cycler ladder to include 48.

## Why the log was lost
Mike clean-built immediately; the log lives in `Build/pd-client.log` which the build wipes. Preserved here as the durable record.
