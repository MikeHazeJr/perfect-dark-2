# Memory Modernization Gameplan (Phase D-MEM)

**Created**: 2026-03-21, Session 14
**Status**: IN PROGRESS — M0 done (S14), M1 started (S15)
**Premise**: The N64's 4–8MB RAM constraint is gone. We have gigabytes. Stop reusing memory
locations for different things, stop hardcoding N64-era buffer sizes, and make allocations
self-documenting and crash-resistant.

---

## Current Architecture (What We're Working With)

The memory system is a **bump allocator with bulk pool reset** — a classic N64/embedded pattern:

- **9 pools per bank**, 2 banks (onboard + expansion) = 18 slots total
- **No individual free** — memory is reclaimed only by resetting an entire pool
- **MEMPOOL_PERMANENT** grows left, never cleared
- **MEMPOOL_STAGE** grows left after PERMANENT, reset on every stage transition
- **MEMPOOL_PC_PERSISTENT** (0xFE) — PC-only, malloc-backed, individual tracking, guard canaries in debug
- Heap is 64MB by default (`g_OsMemSizeMb = 64`), allocated at startup via `sysMemZeroAlloc`
- **Not thread-safe** — zero synchronization primitives

### Key Design Consequence
Because pools have no individual free, the N64 codebase reuses memory aggressively:
overlapping unions, pointer casting between unrelated types, scratch buffers that serve
double duty, and compressed-data-overlapping-decompressed-output patterns. This was
*necessary* on N64. On PC, it's a source of silent corruption bugs.

---

## Audit Findings Summary

### A. Pool Overlap / Memory Aliasing (HIGH concern)

The three primary pools (MEMPOOL_0, MEMPOOL_PERMANENT, MEMPOOL_STAGE) **initially point to
the same memory region**. PERMANENT grows left, then STAGE's start is repositioned after
PERMANENT's leftpos on reset. This means a use-after-reset bug in PERMANENT could silently
corrupt STAGE data and vice versa.

Additionally, `fileLoad` in `file.c` uses an **overlap decompression pattern**: compressed
data is DMA'd to the END of the allocation buffer, then `rzipInflate` decompresses from
that tail region into the beginning of the same buffer. If the compressed size exceeds
expectations, source and destination overlap and corrupt each other.

**13 union types** overlay incompatible structures in the same memory:
- `modelrodata` (16 struct variants), `modelrwdata` (9 variants), `geounion` (4 variants)
- `menuitemdata` (8 variants), `handlerdata` (10 variants)
- `soundnumhack` (bit-field aliasing on raw s16)
- `audioparam` (s32/f32 type punning)

Most of these unions are *architecturally correct* — they represent tagged unions where the
type field determines which member is active. These are fine to keep. The concern is the
**untagged** reuse: scratch buffers, pool overlap, and the inflate pattern.

### B. Hardcoded Magic Numbers (155+ instances)

| Category | Count | Example |
|----------|-------|---------|
| Direct hex/decimal mempAlloc sizes | 3 | `mempAlloc(0x4b00, ...)`, `mempAlloc(10000, ...)` |
| Arithmetic with hardcoded scratch offsets | 11 | `inflatedsize + 0x8010`, `loadsize + 0x8000` |
| Manual alignment masks `(x + 0xf) & ~0xf` | 8 | Should use ALIGN16() macro |
| Stack buffers with hex sizes | 28+ | `u8 sp60[0x4000]`, `u8 scratch[0x2000]` |
| Hardcoded array multipliers | 6+ | `36 * sizeof(s32)` (ammo types), `200 * sizeof(void *)` (props) |

### C. ALIGN16 Ceremony (119 instances, 39 files)

On N64, DMA required 16-byte-aligned addresses. On PC, `dmaStart` is `bcopy` (memcpy).
The 119 `ALIGN16()` wrappers waste 0–15 bytes per allocation and obscure actual sizes.
Not harmful, but noisy. Low priority to strip — useful only for readability cleanup.

### D. IS4MB/IS8MB Branches (compile-time eliminated)

`IS4MB()` is `#define IS4MB() (0)` — compiler dead-code-eliminates all 4MB paths. No
runtime cost. The code is cluttered but not dangerous. Low priority to clean up, but
straightforward when we do.

### E. MAX_BOTS/MAX_PLAYERS Stragglers

| Item | Status | Risk |
|------|--------|------|
| `g_AmBotCommands[9]` | **NOT a bug** — 9 = 3×3 active-menu grid slots, not per-bot | Safe |
| `challengesInit buffer[0x1ca]` | **FIXED** (Session 13) — now `sizeof(struct mpconfigfull) + 16` |
| `challengeApply buffer[0x1ca]` | **FIXED** (Session 13) |
| `mp0f18dec4 buffer[0x1ca]` | **FIXED** (Session 13) |
| All other MAX_BOTS/MAX_PLAYERS arrays | Already use constants | Safe |

### F. Large Stack Allocations (crash risk on PC)

N64 had a single known stack. PC has threads with default 1MB stacks. Large stack buffers
risk overflow, especially in recursive or deeply-nested call chains.

| File | Buffer | Size | Hot Path? |
|------|--------|------|-----------|
| `pak.c` | `sp60[0x4000]` | 16KB | Yes (file I/O) |
| `texdecompress.c` | `scratch[0x2000]` + `lookup[0x1000]` | 12KB combined | Yes (rendering) |
| `texdecompress.c` | `scratch2[0x800]` | 2KB | Yes (rendering) |
| `menuitem.c` | 3× `char[8192]` text wraps | 24KB combined | Yes (menu rendering) |
| `file.c:fileLoad` | `buffer[5 * 1024]` | 5KB | Yes (all file loading) |
| `camdraw.c` | `sp44[0x1000]` | 4KB | Yes (rendering) |
| `snd.c` | 14+ buffers 0x50–0x150 each | ~2KB each | Yes (audio) |

---

## Execution Plan

### Phase M0: Immediate Safety (do with next build)

**Goal**: Fix the remaining MAX_BOTS straggler and clean up diagnostic logging.

- [x] ~~`g_AmBotCommands[9]`~~ — NOT a bug. 9 = 3×3 active-menu UI grid. Not per-bot.
- [x] Clean up `INIT:` diagnostic logging in pdmain.c — converted 24 lines from LOG_NOTE to LOG_VERBOSE
- [x] Remove orphaned `#include "system.h"` from challengeinit.c (was only needed for debug logging)
- [ ] Verify challengesInit buffer fix boots the game (rebuild test from Session 13)

**Risk**: Minimal. Array expansion is additive; diagnostic cleanup is log-only.

### Phase M1: Name the Magic Numbers

**Status**: IN PROGRESS (Session 15) — Header created, 8 high-priority files done, ~100 ALIGN16 replacements remaining.

**Goal**: Make every allocation self-documenting. No behavioral change.

**Approach**:
1. Create `src/include/memsizes.h` with named constants for all magic-number allocations:
   ```c
   /* Background loading scratch space — used during compressed-to-inflated decompression */
   #define BG_INFLATE_SCRATCH_LARGE   0x8000   /* 32KB */
   #define BG_INFLATE_SCRATCH_SMALL   0x800    /* 2KB */
   #define BG_INFLATE_SCRATCH_HEADER  0x10     /* 16B — header prefix on large scratch */

   /* Gun model loading */
   #define GUN_LOAD_SCRATCH           0x8000   /* 32KB decompression scratch */
   #define GUN_TEXTURE_RESERVE        0xe00    /* 3.5KB texture expansion space */

   /* Blur/framebuffer */
   #define BLUR_BUFFER_SIZE           0x4b00   /* 19.2KB menu blur buffer */

   /* Entity limits */
   #define MAX_ONSCREEN_PROPS         200
   #define NUM_AMMO_TYPES             36
   #define MAX_CHR_MANAGER_SLOTS      15

   /* Alignment padding (legacy N64 DMA, not needed on PC) */
   #define DMA_ALIGN_PAD_16           0x10
   #define DMA_ALIGN_PAD_64           0x40
   ```

2. Replace all bare hex/decimal literals in mempAlloc calls with these constants
3. Replace all manual `(x + 0xf) & ~0xf` patterns with `ALIGN16()` macro calls

**Files affected**: ~40 files, ~155 replacements
**Risk**: Zero behavioral change. Pure readability/maintainability improvement.

### Phase M2: Heap-Promote Dangerous Stack Buffers

**Goal**: Move large stack-allocated buffers to heap to prevent stack overflow.

**Approach**:

For **one-shot / init-time buffers** (pak.c, menuitem.c):
```c
/* Before (stack bomb): */
u8 sp60[0x4000];

/* After (heap-safe): */
u8 *sp60 = malloc(0x4000);
/* ... use sp60 ... */
free(sp60);
```

For **hot-path buffers** (texdecompress.c, snd.c, file.c):
```c
/* Use thread-local or persistent scratch allocation */
static u8 *s_TexScratch = NULL;
if (!s_TexScratch) {
    s_TexScratch = malloc(TEX_SCRATCH_SIZE);
}
/* ... use s_TexScratch ... */
/* freed at shutdown, not per-call */
```

**Files affected**: ~8 files
**Risk**: Low. Behavioral change is only allocation source (stack → heap). Logic unchanged.
Must verify no pointer arithmetic depends on stack-relative addressing (unlikely but check).

### Phase M3: Collapse IS4MB/IS8MB Branches

**Goal**: Remove dead code paths that the compiler already eliminates.

**Approach**: Since `IS4MB()` is `(0)`, every `IS4MB() ? small : large` expression can be
replaced with just `large`. Similarly, `if (IS4MB()) { ... } else { ... }` collapses to
just the else-branch.

Example:
```c
/* Before: */
g_PsChannels = mempAlloc(ALIGN16((IS4MB() ? 30 : 40) * sizeof(struct pschannel)), MEMPOOL_STAGE);

/* After: */
#define MAX_PS_CHANNELS 40
g_PsChannels = mempAlloc(ALIGN16(MAX_PS_CHANNELS * sizeof(struct pschannel)), MEMPOOL_STAGE);
```

**Files affected**: ~15 files with IS4MB/IS8MB ternaries
**Risk**: Zero — compiler was already eliminating these branches. We're just making
the source match reality.

### Phase M4: Strip ALIGN16 from Non-DMA Allocations

**Goal**: Remove unnecessary alignment padding from allocations that don't need it.

**Approach**:
1. Identify which ALIGN16 calls are on actual DMA paths (audio heap, vertex data)
2. Strip ALIGN16 from all others (general game data, string buffers, etc.)
3. Keep ALIGN16 on audio and vertex paths where hardware may care

**Files affected**: ~35 files, ~100 ALIGN16 removals
**Risk**: Low. x86-64 has no 16-byte alignment requirement for general data. Audio/vertex
paths preserved. Must verify no code assumes allocation sizes are multiples of 16.

### Phase M5: Separate Overlapping Pool Regions (FUTURE)

**Goal**: Eliminate the pool-overlap design where PERMANENT, STAGE, and POOL_0 share the
same address space.

**This is the big one** — and the one most relevant to Mike's concern about memory reuse.

**Current danger**: PERMANENT grows left in a shared region, then STAGE starts where
PERMANENT ended. If PERMANENT's data is accessed after STAGE has been allocated into
that tail space, silent corruption occurs.

**Approach**:
1. Give PERMANENT its own malloc'd region (sized generously — 16MB)
2. Give STAGE its own malloc'd region (32MB)
3. Give EXPANSION its own region (8MB, or remove entirely since PC doesn't have an expansion pak)
4. Each pool still uses the bump allocator internally — we're not changing the allocation
   strategy, just separating the address spaces so one pool can't stomp another

```c
/* New mempSetHeap approach: */
void mempSetHeap(void)
{
    g_MempPermanentRegion = malloc(PERMANENT_POOL_SIZE);  /* 16MB */
    g_MempStageRegion     = malloc(STAGE_POOL_SIZE);      /* 32MB */
    g_MempPool8Region     = malloc(POOL8_SIZE);           /* 4MB */

    mempInitPool(MEMPOOL_PERMANENT, g_MempPermanentRegion, PERMANENT_POOL_SIZE);
    mempInitPool(MEMPOOL_STAGE,     g_MempStageRegion,     STAGE_POOL_SIZE);
    mempInitPool(MEMPOOL_8,         g_MempPool8Region,     POOL8_SIZE);
}
```

**Risk**: Moderate. This changes the memory layout. Any code that does pointer arithmetic
across pool boundaries (unlikely but possible) would break. Thorough testing required.
The overlap decompression pattern in `fileLoad` would need to be audited separately.

### Phase M6: Increase Default Heap / Make Configurable (FUTURE)

**Goal**: The 64MB default heap was set conservatively. Modern PCs have 16–64GB RAM.

**Approach**:
- Increase default from 64MB to 256MB (or query available system RAM and use a percentage)
- Add a command-line flag: `-memp <size_mb>` for users to override
- Add an ImGui debug panel showing pool usage, high-water marks, and fragmentation

**Risk**: Minimal. Purely additive.

---

## Execution Order & Dependencies

```
M0 (immediate safety)
 ↓
M1 (name magic numbers)  ←  no dependencies, can start immediately
 ↓
M2 (heap-promote stacks)  ←  depends on M1 for named constants
 ↓
M3 (collapse IS4MB)  ←  independent, can parallel with M1/M2
 ↓
M4 (strip ALIGN16)  ←  depends on M1 (need to know which are DMA-path)
 ↓
M5 (separate pools)  ←  depends on M1–M4 being stable; this is the structural change
 ↓
M6 (heap sizing)  ←  depends on M5 for new pool architecture
```

M0 through M4 are safe, incremental, and independently testable. Each can be a single
build-and-test cycle. M5 is the architectural change that actually eliminates the pool
overlap risk Mike is concerned about. M6 is polish.

---

## Files Inventory (All Phases)

### Headers to Create
- `src/include/memsizes.h` — named constants for all magic-number allocations

### Core Memory System (M5)
- `src/lib/memp.c` — pool initialization, allocation, reset
- `src/include/lib/memp.h` — pool API
- `port/src/main.c` — heap setup
- `port/src/pdmain.c` — pool init sequence

### Magic Number Replacements (M1) — ~40 files
- `src/game/bg.c` — 6 scratch offset constants
- `src/game/bondgun.c` — 2 gun-load constants
- `src/game/menu.c` — blur buffer, model memory
- `src/game/utils.c` — 2 utility buffer sizes
- `src/game/botmgr.c` — ammo type count
- `src/game/varsreset.c` — onscreen prop count
- `src/game/chrmgr.c` — chr slot count
- `src/game/stars.c` — star geometry multipliers
- `src/game/portal.c` — portal element size
- Plus ~30 more with ALIGN16 and alignment mask fixes

### Stack-to-Heap Promotions (M2)
- `src/game/pak.c` — `sp60[0x4000]`
- `src/game/texdecompress.c` — `scratch[0x2000]`, `lookup[0x1000]`, `scratch2[0x800]`
- `src/game/menuitem.c` — 3× `char[8192]`
- `src/game/file.c` — `buffer[5 * 1024]`
- `src/game/camdraw.c` — `sp44[0x1000]`
- `src/lib/snd.c` — 14+ audio scratch buffers

### IS4MB Collapse (M3)
- `src/game/setup.c`, `src/game/smokereset.c`, `src/game/propsnd.c`
- `src/game/propsndreset.c`, `src/game/menu.c`
- `src/game/modelmgr.c`, `src/game/modelmgrreset.c`
- `src/lib/memp.c` — pool selection logic

### MAX_BOTS Stragglers (M0)
- ~~`g_AmBotCommands[9]`~~ — False alarm. 3×3 active-menu UI grid, not per-bot.

---

## Testing Strategy Per Phase

| Phase | Test | Pass Criteria |
|-------|------|---------------|
| M0 | Build + boot client | Title screen reached, no crash in challengesInit |
| M1 | Build + full playthrough | Identical behavior to pre-change build |
| M2 | Build + stress test (many explosions, menus, tex loads) | No stack overflow, no crash |
| M3 | Build + compare allocations in log | Same pool usage as before |
| M4 | Build + audio test + vertex-heavy scene | Audio plays, geometry renders correctly |
| M5 | Build + stage transitions + multiplayer | No corruption across pool boundaries |
| M6 | Build + check pool usage with ImGui panel | Pools sized appropriately |
