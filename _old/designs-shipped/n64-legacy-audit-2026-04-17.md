# N64 Legacy Code Audit — 2026-04-17

> **Status**: Reference document. No code changes in this pass.
> **Context**: After S319/S321 stripped the demo/attract mode system, Mike asked for a
> comprehensive audit of remaining IS4MB/IS8MB checks and broader N64 legacy blocks.
>
> Three tiers:
> - **Remove now** — dead code, unreachable paths, compile-time constants that always
>   evaluate one way
> - **Modernize soon** — working but suboptimal, limits artificially low for PC
> - **Modernize later** — deeper architectural changes

---

## Background

`IS4MB()` is compile-time `0`, `IS8MB()` is compile-time `1` (defined in
`src/include/constants.h:91-92`). The compiler dead-code-eliminates all IS4MB branches
in optimized builds, but ~100 sites remain in source. IS8MB branches always execute,
but some still bake in N64-era quality compromises ("8MB was luxury").

`PLATFORM_N64` — fully removed. Zero matches.

---

## Tier 1: Remove Now

### 1.1 IS4MB() Dead Branches (~100 sites)

Every `IS4MB()` call is always-false. The compiler strips them in Release builds but the
source noise is significant. Largest clusters:

| File | IS4MB calls | IS8MB calls | Notes |
|------|-------------|-------------|-------|
| `src/game/bondgun.c` | 7 | 4 | Gun HUD position, hand animation gates |
| `src/game/mplayer/setup.c` | 8 | 1 | 4MB menu routing, 2-player display limits |
| `src/game/menu.c` | 5 | 5 | Menu dialog routing, split-screen layout |
| `src/game/hudmsg.c` | 8 | 1 | HUD positioning for low-res |
| `src/game/player.c` | 5 | 2 | Camera, split-screen, player setup |
| `src/lib/audiomgr.c` | 5 | 0 | Audio freq, reverb params, ACMDList size |
| `src/lib/snd.c` | 3 | 0 | Sequence buffer size, heap config |
| `src/game/setup.c` | 2 | 0 | Projectile/embedment limits (200/160 always) |
| `src/game/propobj.c` | 6 | 0 | Weapon/prop culling |

**Specific removal candidates** (representative, not exhaustive):

- `port/src/pdmain.c:451-452` — redirects `STAGE_CITRAINING → STAGE_4MBMENU` on IS4MB. Dead.
- `src/game/setup.c:201-202` — `g_MaxProjectiles = IS4MB() ? 20 : 200` — always 200. Simplify constant.
- `src/game/setup.c:1549,1555` — IS4MB extra prop slots; line 1555 is `if (IS4MB());` (no-op semicolon).
- `src/game/smokereset.c:13` — `g_MaxSmokes = IS4MB() ? 10 : 20` — always 20.
- `src/game/modelmgr.c:113,194,209` — Three `if (IS4MB());` no-op semicolons.
- `src/game/texreset.c:129` — IS4MB texture clear. Dead.
- `src/game/bondgunreset.c:145` — IS4MB horizontal split force. Dead.
- `src/game/lv.c:2349` — `vmPrintStatsIfEnabled()` inside IS4MB block. Dead.
- `src/lib/audiodma.c:135` — `IS4MB() ? ADMA_MAX_ITEMS - 20 : ADMA_MAX_ITEMS`. Always full.
- `src/lib/memp.c:129,142` — IS4MB pool selection. Dead.
- `src/lib/rdp.c:70` — IS4MB RDP yield buffer. Dead.
- `src/game/filemgr.c:557` — IS4MB menu dialog routing. Dead.
- `src/game/titleinit.c:16` — IS4MB CITRAINING→4MBMENU redirect. Dead.
- `src/game/botmgr.c:64` — IS4MB forces DD-SHOCK head/body on all bots. Dead.
- `src/game/vtxstore.c:140` — IS4MB returns NULL from vtxstoreAllocate. Dead.
- `src/lib/snd.c:1464-1468` — IS4MB disables MP3, 1 FX bus. Dead.
- `src/lib/audiomgr.c:70,78,102,136,140` — IS4MB halves audio quality. Dead.
- `src/lib/snd.c:1410-1411` — IS4MB 14KB vs 18KB sequence buffer. Always 18KB.
- `src/game/menutick.c:276,298,445,471,564,565,657,761` — 4MB menu routing. Dead.
- `src/lib/vi.c:78,97,190,202,206,370` — IS4MB framebuffer/zbuffer sizing. Dead.

**IS8MB branches that are appropriate for PC** (keep the body, remove the IS8MB() guard):

- `src/game/camdraw.c:451` — `if (IS8MB()) pheadInit()` — always runs. Strip the guard.
- `src/lib/sched.c:868` — `if (IS8MB())` blur. Always runs. Strip.
- `src/game/mplayer/ingame.c:941` — `if (IS8MB())` save-player prompt. Always runs. Strip.
- `src/game/menu.c:2237,2379,3847,4068,4127,5630` — IS8MB menu model/animation gates. Always runs.
- `src/game/bondgun.c:1432,2926,2996,5458` — `IS8MB() || PLAYERCOUNT() != 1`. IS8MB=1 makes the whole condition always true; simplify to `true` or just remove the guard.
- `src/game/bondgun.c:7722,8102,8110,10989` — `PLAYERCOUNT() == 1 && IS8MB()`. Strip `&& IS8MB()`.
- `src/game/title.c:740` — `if (g_AltTitleEnabled && IS8MB())`. Strip `IS8MB()`.
- `src/game/game_1531a0.c:487` — `if (!g_Vars.normmplayerisrunning || IS8MB())`. IS8MB=1 makes this always true.

### 1.2 STAGE_4MBMENU and Associated Routing

`STAGE_4MBMENU (0x5d)` exists in:
- `src/game/stagetable.c` — stage table entry
- `port/src/pdmain.c:264` — arena config entry
- `src/game/lv.c`, `src/game/menu.c`, `src/game/menutick.c`, `src/game/main.c` — ~15 equality checks

All routing to this stage is guarded by IS4MB() dead paths. The stage could be stripped
from the stage table and its equality checks removed.

### 1.3 `g_Vars.fourmeg2player` Flag

Defined in `src/include/types.h:243`. Set to true only inside an IS4MB() block
(`src/lib/vi.c:213`). Can never be true on PC. Check sites:
- `src/game/player.c:3423`
- `src/game/playermgr.c:93`
- `src/lib/crash.c:611,639`
- `src/game/mplayer/scenarios.c:635`

All dead branches. The field can be removed from the vars struct (saves 1 byte per
instance; alignment-padded anyway).

### 1.4 FBALLOC_LO Constants (`src/include/constants.h:3692-3695`)

```c
#define FBALLOC_WIDTH_LO   320
#define FBALLOC_HEIGHT_LO  220
```

The LO (4MB) path is dead. These constants still initialize `g_ViDataArray` defaults
and the zbuf allocation. The LO path can be stripped; keep only FBALLOC_WIDTH_HI /
FBALLOC_HEIGHT_HI. The zbuf allocation itself (`src/game/zbuf.c:54-68`) is vestigial —
OpenGL handles depth on PC.

### 1.5 `osTvType` Inconsistency

`port/src/libultra.c:26` — `osTvType = OS_TV_NTSC` hardcoded. PAL builds set `-DPAL=1`
in CMake but don't update `osTvType`. PAL builds will see `osTvType == OS_TV_NTSC` while
PAL=1. Benign for now (osTvType is only used for MPAL/Brazil detection which is always
dead), but inconsistent.

---

## Tier 2: Modernize Soon

### 2.1 `MEMP_EXPANSION_POOL_SIZE = 8MB`  ← **High priority**

`src/lib/memp.c:53`:
```c
#define MEMP_EXPANSION_POOL_SIZE (8 * 1024 * 1024)
```

This is the *entire in-game runtime heap* — stage assets, model data, audio buffers,
collision geometry, everything allocated with `mempAlloc` during gameplay comes from
this 8MB pool. It is the N64 expansion pak size ported directly to PC.

Modern stages with mods + high bot counts may already be near this ceiling. The comment
in memp.c says "increase here if stages start OOM." There is no architectural reason to
keep this at 8MB. Increasing to 64MB or 128MB requires changing exactly one `#define`.

**Recommendation**: Bump to 64MB immediately. Can go higher — no downside on modern
hardware.

### 2.2 Audio Voice Limits  ← **High priority for large matches**

Active N64-era voice limits that cap PC audio polyphony:

| Location | Constant | Current Value | N64 Reason | PC Impact |
|----------|----------|---------------|------------|-----------|
| `src/lib/snd.c:1533` | `synconfig.maxVVoices` | 44 | N64 RSP limit | Virtual voice ceiling |
| `src/lib/snd.c:1535` | `synconfig.maxPVoices` | **30** | N64 RSP physical | Hard concurrent-sound cap |
| `src/lib/snd.c:1547` | `sndpconfig.maxSounds` | **20** | N64-era | Max simultaneous instances |
| `src/lib/audiodma.c:8` | `ADMA_MAX_ITEMS` | 80 | 8MB N64 | Audio streaming slots |
| `src/lib/snd.c:34` | `NUM_CACHE_SLOTS` | 45 | N64 memory | Sound bank cache |
| `include/PR/libaudio.h:414` | `AL_MAX_CHANNELS` | 16 | MIDI spec / N64 | MIDI channels |

With 32 bots in a firefight, `maxSounds = 20` means sounds get dropped frequently.
`maxPVoices = 30` is the hard upper bound. Both should be increased for the PC port's
scale. These are in the naudio synthesizer which still runs on PC.

**Recommendation**: `maxPVoices → 64`, `maxVVoices → 96`, `maxSounds → 48`,
`ADMA_MAX_ITEMS → 200`.

### 2.3 `MAX_OBJECTIVES = 10`

Defined in `src/include/constants.h`. Used for gameplay objective arrays. The N64 design
limit. Forge editor uses a separate `FORGE_MAX_OBJECTIVES = 16`. These are inconsistent.

On PC there is no memory reason to cap at 10. Raising to 16 (to match Forge) or 32 would
allow more complex modded missions. Note: changing affects save file layout via
`g_GameFile` objective arrays — requires a save format bump.

### 2.4 `CHR_MANAGER_SLOTS = 15`

`src/include/memsizes.h:113`. Only 15 character model manager slots per stage. With
32-bot matches, levels loading many character types may hit this limit. Could be 32.
Raises struct size, requires checking all allocation sites.

### 2.5 `MAX_ONSCREEN_PROPS = 200`

`src/include/memsizes.h:112`. The N64 rendering budget per stage. On PC with OpenGL
rendering, this is still the ceiling for how many props can exist in a stage at once.
Complex Forge-built levels may exceed this. Could be raised to 512 or 1024 with only
memory implications.

### 2.6 AI Behavior Throttles (Hardcoded Guard Limits)

`src/game/chraction.c:7242,7246,7313,7317,7336,7340,7372,7376` — Multiple AI checks
against `g_NumChrsSeenPlayerRecently2 <= 8`, `<= 9`, `>= 9`, `>= 10`. These throttle
how many NPC guards can simultaneously pursue a player. N64-era limits for CPU budget.

On PC with 32-bot matches + complex solo levels, these caps may prematurely suppress
guard AI in large scenarios. No comments explain the rationale. Audit and either
document (if they're intentional gameplay caps) or raise to match MAX_BOTS.

### 2.7 `MAX_PROPSPERROOMCHUNK = 7`

`src/game/prop.c`. The room prop chunk linked-list is an N64 memory layout optimization
(8-element struct fits in a cache line). On PC this is irrelevant but the structure is
still in use. Changing would require refactoring the prop/room chunk system.

### 2.8 `MAX_PLAYERNAME = 15`

`src/include/constants.h:60`. 15-character player name limit. Used for bot name arrays
(`aibotnames[MAX_BOTS][15]`) and embedded in save file format. The N64 controller/text
entry system maxed out at ~10 characters practically. On PC with keyboard input 15
characters is genuinely short for display names. Changing requires save format bump.

---

## Tier 3: Modernize Later

### 3.1 Full IS4MB/IS8MB Branch Sweep (Source Cleanup)

The ~100 IS4MB/IS8MB call sites spread across 30+ files can be machine-cleaned:
- Strip all IS4MB() branches (bodies become dead code, guard + body removed)
- Strip IS8MB() guards (bodies become unconditional)
- Remove the IS4MB/IS8MB macros from constants.h

This is a large mechanical change with no runtime impact (compiler already eliminates
them). Useful for readability. Low risk but high source-line delta.

### 3.2 N64 Audio Backend Replacement

The PC port still routes all audio through the N64 `naudio` software synthesizer
(`src/lib/naudio/`, `src/lib/ultra/audio/`). This is why voice limits (`maxPVoices=30`,
`AL_MAX_CHANNELS=16`) are structurally real constraints rather than just integers to
bump. A full PC audio backend would replace naudio with SDL_mixer or miniaudio and
remove the entire N64 audio synthesis stack.

This is a significant undertaking but would eliminate the voice ceiling entirely, add
proper 3D audio, and remove ~5000 lines of N64 audio stub code. Not a quick task.

### 3.3 Fixed-Point Packed Formats

Several encoding patterns from the N64 era remain in use for data compactness:

- **4096-unit angle/scale system**: casing rotation, gun FX rotation, prop scale stored
  as `value * 4096`. Encode/decode in `casingtick.c`, `gunfx.c`, `propobj.c`. Functions
  are `f32 → s32 * 4096 → f32` round-trips. Could be replaced with plain `f32` fields.

- **8.8 fixed-point extrascale**: `extrascale = 256` as "1.0×" in prop scaling
  (`propobj.c`, `player.c`, `scenarios.c`). Used for object scale encoding. Could be
  `f32 = 1.0f`.

- **Q16.16 setup data**: Level asset data decoded as `/ 65536.0f` in `setup.c`. This is
  the level *file format* — changing it would require re-exporting all level data.
  Likely keep as-is.

These are deep in the game systems and would require careful struct migrations.

### 3.4 `fourmeg2player` and STAGE_4MBMENU Full Removal from Structs

Removing `fourmeg2player` from the vars struct and `STAGE_4MBMENU` from the stage table
requires checking all serialization paths (save files, net protocol). Low priority since
they're dead no-ops, but they add clutter to core data structures.

### 3.5 z-Buffer Allocation (`src/game/zbuf.c`)

`zbuf.c` allocates 640×220×2 bytes (~280KB) as an N64 z-buffer into the stage memory
pool on every stage load. On PC the renderer uses OpenGL depth — this allocation is
pure waste but it's sized to fit within the 8MB pool. After increasing the pool
(tier 2 item 2.1), the waste is proportionally smaller. Eventually zbufInit/zbufFree
could become no-ops.

### 3.6 PAL Build `osTvType` Fix

`port/src/libultra.c:26` — PAL builds should set `osTvType = OS_TV_PAL` when `PAL=1`.
Currently hardcoded NTSC. Low priority since `osTvType` is only read in the
Brazil/MPAL detection path (dead on PC) and the framerate negotiation in
`src/lib/main.c:564-631`, which is also largely vestigial since SDL manages the actual
display timing.

---

## Quick Reference: Call-Site Counts

| Category | Count | Priority |
|---|---|---|
| IS4MB() dead branches | ~100 | Tier 1 (bulk sweep) |
| IS8MB() guard strips | ~25 | Tier 1 (trivial) |
| `if (IS4MB());` no-op semicolons | 4 | Tier 1 (trivial) |
| `fourmeg2player` check sites | 5 | Tier 1 |
| STAGE_4MBMENU equality checks | ~15 | Tier 1 |
| FBALLOC_LO usage | ~8 | Tier 1 |
| Audio voice limits | 6 constants | Tier 2 |
| Array limit increases | 5 constants | Tier 2 (some need save bump) |
| AI behavior throttles | 8 sites | Tier 2 |
| Fixed-point math patterns | ~20 sites | Tier 3 |

---

## Recommended Immediate Actions (Before Next Session)

1. **Bump `MEMP_EXPANSION_POOL_SIZE`** — one line, no risk, enables complex modded stages.
2. **Increase audio voice limits** — 4 constants in `snd.c` / `audiodma.c`, matters for 32-bot matches.
3. **Strip IS4MB() dead branches** — mechanical cleanup, either targeted or full sweep; reduces source confusion.
4. **Remove `fourmeg2player`** — 6-site dead-flag cleanup.
5. **Strip IS8MB() guards** where body should just be unconditional (~25 sites, some trivial one-liners).
