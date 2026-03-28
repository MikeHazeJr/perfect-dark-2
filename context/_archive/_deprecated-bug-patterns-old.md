# Bug Pattern Reference — Global Codebase Pass

**Created**: 2026-03-21, Session 15–16
**Purpose**: Catalog recurring bug patterns found during crash investigation and memory
modernization work. Use this as a checklist during a systematic codebase pass to find and
fix all instances of each pattern.

---

## Pattern 1: MAX_PLAYERS Array Indexed by Bot mpindex

**Severity**: CRITICAL — ACCESS_VIOLATION crash
**Root cause**: Arrays sized `MAX_PLAYERS` (8) are accessed with bot mpindex values (8–31).
On N64 with max 4 players and 8 bots, the bot indices (4–11) happened to fit within some
oversized arrays. On PC with MAX_PLAYERS=8 and MAX_BOTS=24, bot indices 8–31 far exceed
any array sized to MAX_PLAYERS.

**Symptom**: EXCEPTION 0xc0000005 in combat simulator with many bots.

**Where it occurs**:
- Any array declared as `[MAX_PLAYERS]` that is indexed with `g_MpPlayerNum`,
  `mpindex`, or `playernum` when the value comes from a bot context.

**Key arrays affected**:
```c
struct menu g_Menus[MAX_PLAYERS];                    /* bss.h:173 */
struct activemenu g_AmMenus[MAX_PLAYERS];            /* bss.h:175 */
u8 g_MpSelectedPlayersForStats[MAX_PLAYERS];         /* bss.h:167 */
struct sndstate *g_BgunAudioHandles[MAX_PLAYERS];    /* bss.h:131 */
struct lasersight g_LaserSights[MAX_PLAYERS];        /* bss.h:135 */
OSPfs g_Pfses[MAX_PLAYERS];                          /* bss.h:194 */
struct extplayerconfig g_PlayerExtCfg[MAX_PLAYERS];  /* data.h:527 */
struct filelist *g_FileLists[MAX_PLAYERS];            /* data.h:301 */
```

**Fix strategy**: Guard every entry point where `g_MpPlayerNum` is SET from a
bot-accessible source (currentplayerstats->mpindex, playernum parameter, etc.):
```c
/* Before calling any menu function: */
if (g_MpPlayerNum >= MAX_PLAYERS) {
    g_MpPlayerNum = prevplayernum;
    return;
}
```

**Important architectural note**: `g_Vars.currentplayerstats->mpindex` is ALWAYS a local
player index (0–3) because `setCurrentPlayerNum()` only receives 0..PLAYERCOUNT()-1.
The danger is when code sets g_MpPlayerNum from OTHER sources — function parameters,
loop variables, or mpindex values read from chrdata/mpchrconfig.

**Files already fixed (Session 15)**:
- ingame.c: mpPushPauseDialog, mpPushEndscreenDialog
- mplayer.c: mpIsPaused, mpRenderModalText (2 sites)
- bondview.c: 2 AVOID_UB sites
- menutick.c: background loop

**Files still needing audit**:
- activemenu.c:71 — amOpenPickTarget sets g_MpPlayerNum from mpindex
- player.c:5094 — playerDieByShooter sets g_MpPlayerNum from mpindex
- endscreen.c:1704, 1806 — endscreenPushCoop/Anti set g_MpPlayerNum from mpindex
- menu.c:5590 — menu render path for MPPAUSE/MPENDSCREEN
- menu.c:5972, 6280 — menuPushPakDialogForPlayer, menuPushPakErrorDialog

---

## Pattern 2: Modulo-Hack Bounds "Fix" (AVOID_UB)

**Severity**: HIGH — silent data corruption
**Root cause**: Some prior contributor used `% MAX_PLAYERS` as a "bounds clamp" to prevent
array overflow. This doesn't prevent the bug — it silently aliases bot data onto the wrong
player's data. Bot index 11 becomes index 3 (`11 % 8 = 3`), silently corrupting player 3's
menu state.

**Example**:
```c
/* BAD — modulo masks the overflow but corrupts data: */
#ifdef AVOID_UB
u32 mpindex = g_Vars.currentplayerstats->mpindex % MAX_PLAYERS;
#else
u32 mpindex = g_Vars.currentplayerstats->mpindex;
#endif
```

**Correct fix**: Bounds-check and SKIP the operation for bots, don't alias their index
onto a random player:
```c
u32 mpindex = g_Vars.currentplayerstats->mpindex;
if (mpindex < MAX_PLAYERS && g_Menus[mpindex].curdialog == NULL) {
    /* safe to access g_Menus */
}
```

**Where to search**: `grep -rn 'AVOID_UB\|% MAX_PLAYERS\|% MAX_LOCAL' src/`

**Files known affected**:
- bondview.c (fixed Session 15 — replaced modulo with proper bounds check)
- mplayer.c:704, 3754 — `g_PlayerExtCfg[playernum % MAX_PLAYERS]`

---

## Pattern 3: g_PlayerExtCfg Indexed Beyond MAX_LOCAL_PLAYERS

**Severity**: MEDIUM — reads garbage data for remote/bot players
**Root cause**: `g_PlayerExtCfg[MAX_PLAYERS]` holds extended config for LOCAL players only
(mouse sensitivity, jump height, etc.). It's meaningful only for indices 0–3
(MAX_LOCAL_PLAYERS). But code indexes it with `playernum % MAX_PLAYERS` which gives 0–7,
reading uninitialized data for indices 4–7.

**Known sites**:
```c
mplayer.c:704:  g_PlayerExtCfg[playernum % MAX_PLAYERS].extcontrols
mplayer.c:3754: g_PlayerExtCfg[playernum % MAX_PLAYERS].extcontrols
bondwalk.c:908: g_PlayerExtCfg[pidx] (checked < MAX_PLAYERS, should be < MAX_LOCAL_PLAYERS)
data.h:561:     PLAYER_EXTCFG() uses mpindex & 3 (correct — masks to 0–3)
```

**Fix strategy**: Use `MAX_LOCAL_PLAYERS` (4) as the bound, or use the `PLAYER_EXTCFG()`
macro which correctly masks with `& 3`:
```c
if (playernum < MAX_LOCAL_PLAYERS && g_PlayerExtCfg[playernum].extcontrols) {
    /* ... */
}
```

---

## Pattern 4: Hardcoded Magic Numbers as Array Sizes

**Severity**: LOW (readability) to MEDIUM (if the constant is wrong)
**Root cause**: N64 developers used bare hex/decimal literals for allocation sizes, buffer
offsets, and array multipliers. When constants change (e.g., MAX_BOTS goes from 8 to 24),
hardcoded values don't update.

**Example**:
```c
/* BAD: */
mempAlloc(0x4b00, MEMPOOL_STAGE);
mempAlloc(36 * sizeof(s32), MEMPOOL_STAGE);

/* GOOD: */
mempAlloc(MENU_BLUR_BUFFER_SIZE, MEMPOOL_STAGE);
mempAlloc(AMMO_TYPE_COUNT * sizeof(s32), MEMPOOL_STAGE);
```

**Status**: Phase M1 of memory modernization. memsizes.h created with 30+ named constants.
8 high-priority files converted. ~100 ALIGN16 replacements remaining.

**Where to search**: `grep -rn 'mempAlloc(0x\|mempAlloc([0-9]' src/`

---

## Pattern 5: Large Stack-Allocated Buffers

**Severity**: MEDIUM — stack overflow on PC threads
**Root cause**: N64 had a single known stack and developers used large stack buffers freely.
PC threads default to 1MB stacks. Large buffers in recursive or deeply-nested call chains
can overflow.

**Known dangerous buffers**:
| File | Buffer | Size |
|------|--------|------|
| pak.c | sp60[0x4000] | 16KB |
| texdecompress.c | scratch[0x2000] + lookup[0x1000] | 12KB |
| menuitem.c | 3× char[8192] | 24KB |
| file.c | buffer[5*1024] | 5KB |
| camdraw.c | sp44[0x1000] | 4KB |
| snd.c | 14+ buffers 0x50–0x150 | ~2KB each |

**Fix strategy**: Phase M2 — heap-promote dangerous buffers.
- One-shot buffers: malloc/free
- Hot-path buffers: static persistent allocation (freed at shutdown)

**Where to search**: `grep -rn 'u8 sp\|u8 scratch\|char.*\[.*[0-9][0-9][0-9][0-9]\]' src/`

---

## Pattern 6: Overlap Decompression (File Load Pattern)

**Severity**: LOW on PC (works by coincidence) but architecturally fragile
**Root cause**: `fileLoad` in file.c DMA's compressed data to the END of the allocation
buffer, then `rzipInflate` decompresses from that tail into the head. If compressed size
exceeds expectations, source and destination overlap and corrupt each other.

**Where it occurs**: file.c fileLoad, bg.c BG section loading

**Fix strategy**: Phase M2/M5 — separate source and destination buffers when
heap memory is plentiful.

---

## Pattern 7: Dead IS4MB/IS8MB Branches

**Severity**: ZERO (compiler eliminates them) — readability only
**Root cause**: `IS4MB()` is `#define IS4MB() (0)`, so all 4MB paths are dead code.
The ternaries clutter the source and obscure the actual values used.

**Example**:
```c
/* Before: */
g_PsChannels = mempAlloc(ALIGN16((IS4MB() ? 30 : 40) * sizeof(struct pschannel)), MEMPOOL_STAGE);
/* After: */
#define MAX_PS_CHANNELS 40
g_PsChannels = mempAlloc(ALIGN16(MAX_PS_CHANNELS * sizeof(struct pschannel)), MEMPOOL_STAGE);
```

**Fix strategy**: Phase M3 — collapse all IS4MB ternaries to their 8MB branch.
**Where to search**: `grep -rn 'IS4MB\|IS8MB' src/`

---

## Pattern 8: Untagged Union Memory Reuse

**Severity**: INFORMATIONAL — most are architecturally correct tagged unions
**Root cause**: N64 memory constraints led to aggressive union use. 13 union types overlay
incompatible structures. Most are properly tagged (modelrodata, menuitemdata, etc.).
The concern is UNTAGGED reuse: scratch buffers serving double duty, pool overlap.

**Tagged (safe to keep)**: modelrodata (16 variants), modelrwdata (9), geounion (4),
menuitemdata (8), handlerdata (10)

**Untagged (audit needed)**: soundnumhack (bit-field aliasing), audioparam (type punning)

---

## Pattern 9: Config Persistence Gaps

**Severity**: LOW — user annoyance, not crash
**Root cause**: Some settings are runtime-only (stored in global variables) and not
serialized to the config file. They reset on restart.

**Known instances**:
- Verbose logging toggle — sysLogSetVerbose() is runtime-only, not saved to config
- Log channel filter state — not persisted

**Fix strategy**: Add config file entries for each setting. On startup, read and apply.
On change, write back.

---

## Search Commands for Global Pass

```bash
# Pattern 1: MAX_PLAYERS arrays
grep -rn '\[MAX_PLAYERS\]' src/include/bss.h src/include/data.h src/include/types.h

# Pattern 1: g_MpPlayerNum as array index
grep -rn 'g_MpPlayerNum\]' src/game/

# Pattern 2: AVOID_UB modulo hacks
grep -rn 'AVOID_UB\|% MAX_PLAYERS' src/

# Pattern 3: g_PlayerExtCfg access
grep -rn 'g_PlayerExtCfg\[' src/

# Pattern 4: Magic number allocations
grep -rn 'mempAlloc(0x\|mempAlloc([0-9]' src/

# Pattern 5: Large stack buffers
grep -rn 'u8.*\[0x[0-9a-fA-F]\{3,\}\]' src/

# Pattern 7: Dead IS4MB branches
grep -rn 'IS4MB\|IS8MB' src/
```

---

## Priority Order for Global Pass

1. **Pattern 1** (MAX_PLAYERS overflow) — crash severity, fix entry points first
2. **Pattern 2** (AVOID_UB modulo hacks) — silent corruption, fix alongside Pattern 1
3. **Pattern 3** (g_PlayerExtCfg bounds) — medium severity, quick fix
4. **Pattern 5** (stack buffers) — Phase M2, systematic heap promotion
5. **Pattern 4** (magic numbers) — Phase M1 continuation
6. **Pattern 7** (IS4MB) — Phase M3, readability
7. **Pattern 6** (overlap decompression) — Phase M5, architectural
8. **Pattern 9** (config persistence) — ongoing, fix as encountered
