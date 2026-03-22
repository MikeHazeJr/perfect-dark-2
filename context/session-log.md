# Session Log

Reverse-chronological. Each entry is a self-contained summary of what happened.

---

## Session 20 — 2026-03-21

**Focus**: First build of Sessions 12–19, crash diagnosis, scale clamp removal, debug symbol fix

### What Was Done

1. **First build test** — Mike compiled Sessions 12–19 together and ran the client. The Session 12–13 model loading crash chain fix is **confirmed working**: catalog validation completes without ACCESS_VIOLATION, all 24 bots spawn successfully with valid prop pointers.

2. **Scale clamping identified as destructive** — The `bodymodeldef->scale > 100.0f` clamp in both `body.c` and `modelcatalog.c` was destroying legitimate mod model data. AllInOneMods models have `modeldef->scale` values of 700–2000, which is their intended scale. The clamp forced these to 1.0, making effective model scale ~0.1 instead of ~116 — breaking hit radii, rendering, and animation positioning. **Removed the >100 threshold**. Now only rejects scale ≤ 0 (truly degenerate). Added LOG_NOTE diagnostic to body.c so we can see actual scale values in the log.

3. **Crash still occurs** — ACCESS_VIOLATION at offset 0x60002, 2 seconds after spawn (during opening camera animation). Same timing pattern as pre-fix crash. Root cause unknown — need symbol names to identify the crashing function.

4. **Debug symbols fix** — `addr2line` returned all `??` because DWARF debug info wasn't being embedded. Added explicit `-g` to `CMakeLists.txt` compile options (MinGW's `RelWithDebInfo` wasn't reliably passing `-g`). After next clean build, `addr2line` should produce function names and line numbers.

### Files Modified
- `src/game/body.c` — Removed `modeldef->scale > 100` clamp, added LOG_NOTE diagnostic
- `port/src/modelcatalog.c` — Removed `mdef->scale > 100` clamp, only reject ≤ 0
- `CMakeLists.txt` — Added explicit `-g` for DWARF debug symbol generation

### Key Finding
The scale clamp was a defensive measure added before Session 12–13's model loading fix. Now that `fileLoadToNew` properly returns NULL for missing ROM files (and `body0f02ce8c` checks for NULL/invalid modeldefs), the clamp serves no purpose and actively harms model data. The three combat bugs (hit radius, position desync, camera crash) may all improve with correct scale values.

### Next Steps
- Clean rebuild with scale clamp removed + debug symbols enabled
- If crash persists: run `addr2line` on new backtrace to get function names
- Verify `objdump -h PerfectDark.exe | grep debug` shows DWARF sections
- Check log for `body0f02ce8c: bodynum 86 ... modeldef->scale=1162.33` (no more WARNING)

---

## Session 19 — 2026-03-21

**Focus**: Crash log analysis, hit radius root cause, ammo init fix, BotController architecture decision

### What Was Done

1. **Crash log analysis** — `pd-client.log` shows ACCESS_VIOLATION (`0xc0000005`) at module offset `0x5ff82` exactly 2 seconds after "spawning done total=24", during the opening camera animation. No symbol names in backtrace (21 frames). Crash happens before any gameplay or combat debug output.

2. **Ammo initialization bug found and fixed** — `botmgr.c:122` and `bot.c:146` both iterated `i < 33` to initialize `aibot->ammoheld`, but the array is allocated for `AMMO_TYPE_COUNT` (36). Last 3 slots per bot left uninitialized. With 24 bots, that's 72 uninitialized `s32` values. Fixed both to use `AMMO_TYPE_COUNT`. Added `#include "memsizes.h"` to `bot.c`.

3. **Hit radius root cause traced** — `chrGetHitRadius()` → `modelGetEffectiveScale(chr->model)` → `modeldef->scale * model->scale`. The `model->scale` is set in `body0f02ce8c()` as `g_HeadsAndBodies[bodynum].scale * 0.1`. For body 86 (FILE_CDARK_COMBAT, all bots), `g_HeadsAndBodies[86].scale = 1.0`, so `model->scale = 0.1`. The final radius depends entirely on `modeldef->scale` from ROM data. If the Session 12-13 model loading fixes haven't been compiled, `modeldef->scale` could be zeroed/corrupt — producing a tiny or zero hit radius. Added diagnostic logging to `chrTestHit` that captures `modeldef->scale`, `model->scale`, and final `hitradius` separately.

4. **Architecture decision: BotController layer approved** — Mike asked whether to create a custom bot management wrapper. Recommended a `BotController` struct that wraps existing chr/aibot without rewriting AI logic. Extension points for physics (jumping, mesh collision), combat telemetry (post-game screen data), and lifecycle hooks. Aligns with M-steps memory modernization (clean pool boundaries).

### Files Modified
- `src/game/botmgr.c` — Fixed ammo init: `33` → `AMMO_TYPE_COUNT`
- `src/game/bot.c` — Fixed ammo init: `33` → `AMMO_TYPE_COUNT`, added `memsizes.h` include
- `src/game/chr.c` — Added `chrGetHitRadius` diagnostic logging (modeldef_scale, model_scale, effective, hitradius)

### Critical Finding
The Session 12-13 model loading fixes (fileLoadToNew crash chain) have NOT been compiled yet. The crash, hit radius problem, and position desync may ALL stem from corrupt model data caused by the same underlying bug. **Priority: compile Session 12-13 + Session 17-19 changes together and test.**

### Next Steps
- Build with all pending changes (Sessions 12, 13, 17, 18, 19)
- Test: does the camera transition crash resolve?
- Test: check `chrGetHitRadius` log values — is `modeldef->scale` a sane number (~200-300)?
- If combat bugs persist after model fix: investigate further
- Begin BotController header design if build is stable

---

## Session 18 — 2026-03-21

**Focus**: Combat bug diagnosis + combat debug logging channel + constraints.md creation

### What Was Done

1. **Deep diagnosis of two critical combat bugs in local play (31 bots)**:
   - **Shots pass through bots**: Traced the full hit detection path: prop.c (shot loop) → chrTestHit (chr.c:4460) → chrHit (chr.c:4559) → func0f0341dc → chrDamage. Player shots test against model root matrix position (rootmtx->m[3]), NOT prop->pos. Bot geocyl collision geometry uses prop->pos. If these diverge (position desync), shots pass through visible bots. Also identified PROPFLAG_ONTHISSCREENTHISTICK as a gatekeeper — if bots don't get this flag, they're invisible to hit detection entirely.
   - **Player instant death**: In MP combat sim, damage = raw_damage * damagescale (default 1.0). No obvious scaling issue found. Needs log data to confirm actual values. May be related to health/handicap settings.

2. **Implemented comprehensive combat debug logging** across 5 files:
   - `chr.c` — chrTestHit: logs bounding sphere test result (prop pos vs model pos, radius, HIT/MISS) for every tested chr. Also logs when chrs are SKIPPED (hidden or not on screen).
   - `chr.c` — chrHit: logs when player bullet successfully hits a chr (weapon, damage, shield, health).
   - `chr.c` — chrUpdateGeometry: **position desync detector** — every 120 frames, compares prop->pos to model root matrix position. Logs WARNING if divergence exceeds 10 units.
   - `chraction.c` — chrDamage: logs every damage event with sender type (PLAYER/BOT/NPC), receiver type, raw damage, health before/after, weapon, hitpart, shield. Also logs after damage scaling with final values.
   - `botact.c` — bot hit detection: logs when a bot's shot hits an opponent (attacker pos, target type/pos, damage, weapon).
   - `prop.c` — shot processing: logs SHOT_FIRED (player pos, weapon, onscreen chr count, total onscreen props) and SHOT_RESULT (HIT/HIT_BG/MISS).
   - `system.c` — added "COMBAT:" prefix to channel classifier so all new logs route to LOG_CH_COMBAT.

3. **Created context/constraints.md** — Formal constraint ledger with Active Constraints (save format, protocol version, MAX_MPCHRS=36, MAX_PLAYERS=4, C11/C++ split, build system) and Removed Constraints (N64 guards, 4MB mode, dead code, micro-optimization, host-based MP, N64 collision, bot limit, body restriction, --log flag).

4. **End Game crash analysis** — mpEndMatch() calls func0f0f820c(NULL, -6) for endscreen. Without crash log, likely candidate is array bounds issue in endscreen rendering/scoring when iterating 31+ chrs. Need log file from reproduction.

### Files Modified
- `src/game/chr.c` — Combat debug logging in chrTestHit, chrHit, chrUpdateGeometry
- `src/game/chraction.c` — Combat debug logging in chrDamage (entry + scaling + application)
- `src/game/botact.c` — system.h include, combat debug logging in bot hit path
- `src/game/prop.c` — system.h include, combat debug logging in shot processing loop
- `port/src/system.c` — Added "COMBAT:" prefix to log channel classifier
- `context/constraints.md` — NEW: Constraint ledger
- `context/README.md` — Added constraints.md to file index
- `context/session-log.md` — This entry

### Key Diagnostic: How to Read Combat Logs

Enable verbose logging (F12 debug menu → Verbose checkbox, or `--verbose` CLI flag) and ensure Combat channel is enabled. Then look for:

```
COMBAT: SHOT_FIRED — every time player fires (shows onscreen chr count)
COMBAT: chrTestHit SKIP — chr not tested (hidden or not on screen)
COMBAT: chrTestHit ... bsphere=MISS — bullet didn't intersect bounding sphere
COMBAT: PLAYER_HIT — bullet hit a chr (shows damage, health)
COMBAT: chrDamage — damage applied (shows sender/receiver, raw damage)
COMBAT: DMG_SCALED — after difficulty/MP scaling
COMBAT: DMG_APPLIED — health change (before/after, dead?)
COMBAT: BOT_HIT — bot's shot hit an opponent
COMBAT: POS_DESYNC — WARNING: prop->pos diverged from model position by >10 units
```

### Remaining for Next Session
- **Build and test** with combat logging enabled
- **Reproduce "shots through bots"** and check for POS_DESYNC warnings or chrTestHit SKIP/MISS patterns
- **Reproduce "instant death"** and check DMG_SCALED values for anomalies
- **Reproduce "End Game crash"** and capture full log tail
- If POS_DESYNC confirmed: investigate model matrix update vs prop->pos update path
- If chrTestHit SKIP: investigate PROPFLAG_ONTHISSCREENTHISTICK assignment for bot props

---

## Session 17 — 2026-03-21

**Focus**: Fix ROOT CAUSE #2 of 12+ character crash (model/anim pool exhaustion) + preemptive pool audit

### What Was Done

1. **Increased model rwdata binding pools** — `NUMTYPE3() = 20` was too small for 32 characters. Increased: NUMTYPE1 35->70, NUMTYPE2 25->50, NUMTYPE3 20->48, NUMSPARE 60->80. Added LOG_WARNING when pools exhaust.

2. **Fixed modelmgrAllocateSlots receiving numchrs=0** — The ACTUAL crash cause. `modelmgrAllocateSlots(numobjs, numchrs)` in setup.c used a `numchrs` that didn't count simulant bots (same bug pattern as ROOT CAUSE #1). Result: `g_MaxAnims = 0 + 20 = 20`. After 20 bots allocated animated models, `modelmgrInstantiateAnim()` returned NULL, bodyAllocateModel failed. Fix: added bot counting from `g_MpSetup.chrslots` bitmask before the call.

3. **Preemptively doubled hardcoded object pool limits** — N64 pools tuned for 4 players: g_MaxWeaponSlots 50->100, g_MaxHatSlots 10->20, g_MaxAmmoCrates 20->40, g_MaxDebrisSlots 15->30, g_MaxProjectiles 100->200, g_MaxEmbedments 80->160.

4. **Added diagnostic logging** — MODELMGR AllocateSlots logs pool sizes, botmgr logs explicit error on NULL model.

   **Files modified:**
   - `src/game/setup.c` — Bot counting before modelmgrAllocateSlots, doubled object pool limits
   - `src/game/modelmgr.c` — Increased NUMTYPE macros, exhaustion warning logging
   - `src/game/modelmgrreset.c` — Matched pool sizes, added system.h, allocation logging
   - `src/game/botmgr.c` — LOG_ERROR else-branch for NULL model

### Root Cause Pattern

"numchrs doesn't count bots" existed in TWO call sites in setup.c:
1. `chrmgrConfigure(numchrs)` — fixed Session 16c
2. `modelmgrAllocateSlots(numobjs, numchrs)` — fixed this session

Both used local `numchrs` from `setupCountCommandType(OBJTYPE_CHR)` only. Each needed `g_MpSetup.chrslots` bitmask counting.

### Audit: No further numchrs-dependent allocators found. g_Vars.maxprops shares the same now-fixed numchrs. Gfx/vtx buffers scale by PLAYERCOUNT (local). Proxy mines are static.

---

## Session 16 — 2026-03-21

**Focus**: Deeper crash audit for 12+ chars, bug pattern catalog, Debug tab polish, config persistence

### What Was Done

1. **Deep audit of 12+ character combat sim crash** — Comprehensive analysis of all MAX_PLAYERS-sized arrays (g_Menus, g_AmMenus, g_LaserSights, g_BgunAudioHandles, g_Pfses, g_PlayerExtCfg, g_FileLists, g_MpSelectedPlayersForStats). Key finding: `g_Vars.currentplayerstats->mpindex` is ALWAYS a local player index (0-3) because `setCurrentPlayerNum()` only receives local player indices. This means our Session 15 g_Menus fixes were correct for the paths they guard, but the 12+ character crash is a DIFFERENT bug — likely in the bot spawn/init path, not in menu array overflow. Added diagnostic logging to botmgrAllocateBot and mpReset to capture the exact crash site on next reproduction.

   **Files modified:**
   - `src/game/botmgr.c` — Added BOT_ALLOC logging at bodyAllocateModel and chrAllocate return points
   - `src/game/mplayer/mplayer.c` — Added mpReset player enumeration logging

2. **Bug pattern reference document** — Created `context/bug-patterns.md` cataloging 9 recurring bug patterns found during crash investigation: MAX_PLAYERS array overflow, AVOID_UB modulo hacks, g_PlayerExtCfg bounds, hardcoded magic numbers, large stack buffers, overlap decompression, IS4MB dead branches, untagged unions, config persistence gaps. Includes search commands for global codebase pass and priority ordering.

3. **Debug tab button widths** — Increased base button width from 80px to 110px (scaled) so "Black & Gold" and "All Channels" text fits without truncation.

4. **Theme button coloring** — Each theme selector button is now tinted to preview the theme it represents. Added `s_ThemeAccentColors[]` and `s_ThemeTextColors[]` arrays with representative colors for all 7 themes. Selected theme button gets brightened with a visible border in theme text color.

5. **Verbose logging + channel mask persistence** — Registered `s_LogVerbose` and `s_LogChannelMask` as config entries via `configRegisterInt/UInt` in `sysInit()`. Values are now loaded from `pd.ini` on startup (Debug.VerboseLogging, Debug.LogChannelMask) and saved on every toggle in the Debug tab. CLI `--verbose` flag still overrides config value.

6. **Task tracker update** — Added 11 new known issues to tasks.md: 12+ char crash, button widths, theme colors, verbose persistence, connect codes, offline servers, F11 storyboard removal, Mods tab, Match Setup layout, and their statuses.

### Files Modified
- `src/game/botmgr.c` — diagnostic logging
- `src/game/mplayer/mplayer.c` — diagnostic logging
- `port/fast3d/pdgui_menu_mainmenu.cpp` — button widths (80→110), theme accent/text color arrays, colored theme buttons with border on selection, configSave on verbose/channel/theme changes
- `port/src/system.c` — config.h include, configRegisterInt/UInt for verbose + channel mask
- `context/bug-patterns.md` — NEW: 9-pattern bug reference for global codebase pass
- `context/tasks.md` — 11 new known issues added
- `context/session-log.md` — this entry

### Remaining for Next Session
- **12+ character crash**: Enable verbose logging, reproduce crash, check log for last BOT_ALLOC entry to identify exact failure point. May need debug symbols (.pdb) for the backtrace.
- **Mods tab**: Create renderSettingsMods() function, add 7th tab to Settings
- **M2-M6**: Continue memory modernization phases after crash fix is resolved

---

## Session 16c — 2026-03-21

**Focus**: 12+ character crash ROOT CAUSE found and fixed, Match Setup UI polish, Match log channel

### What Was Done

1. **FIXED: 12+ character combat sim crash** — ROOT CAUSE: `chrmgrConfigure()` in `src/game/chrmgr.c` allocates `g_NumChrSlots = PLAYERCOUNT() + numchrs + 10`, where `numchrs` only counts guards from the map setup file — NOT simulant bots. With 1 player + 0 map NPCs + 10 buffer = 11 chr slots, but 24 bots need 25+ slots. chrInit runs out of free slots at bot #12, returns chr=NULL, then `chr->chrnum = ...` dereferences NULL → ACCESS_VIOLATION at +0x5eb54.

   **Fix**: Added bot-counting loop in `src/game/setup.c` before `chrmgrConfigure()` call. When in MP mode with simulants, counts set bits in `g_MpSetup.chrslots` for bot slots and adds to `numchrs`. Now 1 player + 24 bots + 0 map NPCs + 10 buffer = 35 chr slots.

   **Safety nets**: Added NULL checks in `chrInit()` and `chr0f020b14()` in `src/game/chr.c` so exhausted slot pools log errors instead of crashing. `chrAllocate()` handles NULL return from `chr0f020b14()`.

2. **Added LOG_CH_MATCH channel** (0x0100) — New 9th debug log channel for match setup pipeline tracing. Appears in Debug tab channel checkboxes automatically. Added diagnostic logging to `chrmgrConfigure()` showing slot budget calculation.

3. **Match Setup UI: Add Bot button** — Replaced +/- button pair with single "Add Bot" button (100px scaled), keeping the "Bots: 02" counter. X remove buttons on each bot row were already working.

### Files Modified
- `src/game/setup.c` — Bot count added to numchrs before chrmgrConfigure (the crash fix)
- `src/game/chr.c` — NULL safety checks in chrInit, chr0f020b14, chrAllocate
- `src/game/chrmgr.c` — Diagnostic logging in chrmgrConfigure
- `port/include/system.h` — Added LOG_CH_MATCH (0x0100), LOG_CH_COUNT 8→9
- `port/src/system.c` — Added "Match" to channel name/bit arrays
- `port/fast3d/pdgui_menu_matchsetup.cpp` — Replaced +/- with "Add Bot" button
- `context/session-log.md` — This entry

---

## Session 16b — 2026-03-21

**Focus**: Match Setup menu layout restructure per Mike's annotated screenshot

### What Was Done

1. **Match Setup layout restructure** — Complete rearrangement of `pdgui_menu_matchsetup.cpp` to match Mike's annotated screenshot reference:
   - **Swapped column order**: Settings column now on LEFT (42% width), Players panel on RIGHT (53% width). Previously was inverted.
   - **Removed `renderSlotDetail`**: The old multi-select detail panel (Name, Character, Team, Bot Type, Difficulty) is fully replaced by the per-bot edit popup in `renderPlayersPanel`. Click any bot in the player list → opens popup with Name, Character, Type, Difficulty, Team fields.
   - **Always-visible weapon slots**: Slot 1–6 weapon dropdowns now always render below the Weapons preset dropdown. When using a preset (not Custom), slots show the preset weapons in a disabled/greyed state. When Custom is selected, slots become fully editable.
   - **Renamed labels**: "Time Limit" → "Length", "Score Limit" → "Score" to match the screenshot labels.
   - **Collapsible Options section**: Moved option checkboxes (Teams, One-Hit Kills, No Radar, etc.) below weapon slots into a CollapsingHeader so the main settings (Scenario/Arena/Length/Score/Weapons/Slots) are always visible first.
   - **Removed unused multi-select helpers**: `selectionToggle`, `selectionRange`, `selectionHasBots`, `selectionAllBots` were only used by the deleted `renderSlotDetail` — removed to eliminate compiler warnings.
   - **Fixed `pad` variable bug**: `renderPlayersPanel` referenced undefined `pad` variable in the bot button area; replaced with local `btnPad`.

### Files Modified
- `port/fast3d/pdgui_menu_matchsetup.cpp` — Full layout restructure (all changes above)
- `context/session-log.md` — This entry

### Remaining
- **Build verification**: Mike needs to rebuild client to verify compile
- **Visual polish**: May need width/spacing tweaks once Mike sees the new layout in-game
- **12+ character crash**: Still pending verbose log output from crash reproduction
- **Mods tab**: Not started
- **M2-M6**: Continue memory modernization phases

---

## Session 15 — 2026-03-21

**Focus**: Combat sim crash fix, Debug tab in Settings, Memory modernization M1

### What Was Done

1. **Fixed combat sim crash (1 player + 11 bots)** — Root cause: `g_Menus[MAX_PLAYERS]` (8 elements) accessed with bot `mpindex` values (8-31) causing ACCESS_VIOLATION. Full audit found 6 vulnerable sites across 4 files. Added bounds checks at each source where bot indices flow into g_Menus access. Bots now correctly skip all menu operations.

   **Files fixed:**
   - `src/game/mplayer/ingame.c` — `mpPushPauseDialog()`: early return for bot mpindex; `mpPushEndscreenDialog()`: early return for bot playernum
   - `src/game/mplayer/mplayer.c` — `mpIsPaused()`: bounds check on mpindex; `mpRenderModalText()`: bounds check on two g_Menus accesses (curdialog read + openinhibit write)
   - `src/game/bondview.c` — Two functions: replaced `#ifdef AVOID_UB` modulo hack and raw `g_Vars.currentplayerstats->mpindex` access with proper `mpindex < MAX_PLAYERS` bounds check
   - `src/game/menutick.c` — Loop mpindex validation: added `mpindex < MAX_PLAYERS` to guard

2. **Added Debug tab to Settings** — New 6th tab in Settings menu with log channel filters (All/None presets + per-channel checkboxes in 2-column layout), verbose logging toggle, UI theme selector (7 palettes in 3-column grid), memory diagnostics (persistent allocs, heap size, validate button), and keyboard shortcut reference (F11/F12).

   **Files modified:**
   - `port/fast3d/pdgui_menu_mainmenu.cpp` — Added `renderSettingsDebug()`, 6th tab, bumper wrap 4→5, selFlag5, extern "C" memory function declarations

3. **Memory modernization M1** — Created `src/include/memsizes.h` with 30+ named constants covering all categories (BG, GUN, MENU, TEX, CAM, PAK, SND, ZBUF, entity limits). Replaced magic numbers in 8 high-priority files:
   - `src/game/menu.c` — blur buffer (0x4b00), menu model buffers (0xb400/0x38400/0x25800)
   - `src/game/bg.c` — 6 scratch headroom values (0x8010, 0x8000, 0x800, 0x1000, 0x100)
   - `src/game/botmgr.c` — ammo type count (36)
   - `src/game/varsreset.c` — onscreen props (200)
   - `src/game/chrmgr.c` — chr manager slots (15)
   - `src/game/bondgun.c` — gun model load scratch (0x8000)
   - `src/game/credits.c` — credits model buffer (0x25800)
   - `src/game/zbuf.c` — z-buffer alignment (0x40/0x3f)

### Key Decisions
- **Bounds-check approach** over array expansion for g_Menus fix — bots don't have menus, so the correct fix is preventing the access, not making the array bigger
- Debug tab goes in Settings (not a separate menu) — discoverability for users who don't know about F12
- M1 header uses category prefixes (BG_, GUN_, MENU_, etc.) for organization

### Files Created
- `src/include/memsizes.h` — Named constants for all magic-number allocations

### Files Modified
- `src/game/mplayer/ingame.c` — Bot bounds checks in pause/endscreen
- `src/game/mplayer/mplayer.c` — Bot bounds checks in isPaused/renderModalText
- `src/game/bondview.c` — Replaced AVOID_UB hack with proper bounds check
- `src/game/menutick.c` — Loop mpindex bounds check
- `port/fast3d/pdgui_menu_mainmenu.cpp` — Debug tab, 6-tab wrapping, memory externs
- `src/game/menu.c` — memsizes.h include + 3 magic number replacements
- `src/game/bg.c` — memsizes.h include + 6 magic number replacements
- `src/game/botmgr.c` — memsizes.h include + 1 replacement
- `src/game/varsreset.c` — memsizes.h include + 1 replacement
- `src/game/chrmgr.c` — memsizes.h include + 1 replacement
- `src/game/bondgun.c` — memsizes.h include + 1 replacement
- `src/game/credits.c` — memsizes.h include + 1 replacement
- `src/game/zbuf.c` — memsizes.h include + 2 replacements

### Next Steps
- **Build and test** — combat sim with 12 players, Settings → Debug tab, verify no regressions
- **M1 continuation** — ~100 remaining manual ALIGN16 → ALIGN16() macro replacements, stack buffer naming
- **M2** — heap-promote dangerous stack buffers (pak.c 16KB, texdecompress.c 12KB, menuitem.c 24KB)
- **M3** — collapse 107 IS4MB() ternaries (all dead code paths)

---

## Session 14 — 2026-03-21

**Focus**: Memory modernization audit and gameplan

### What Was Done

1. **Full codebase audit** of the memp memory pool system — documented the bump allocator architecture, pool overlap design (PERMANENT/STAGE/POOL_0 share address space), lack of individual free, and zero thread safety.

2. **Memory aliasing audit** — identified 13 union types overlaying incompatible structs (most are architecturally valid tagged unions), the compressed-data-overlapping-decompressed-output pattern in `fileLoad`, and pool boundary overlap risks.

3. **Magic number audit** — found 155+ hardcoded allocation sizes across ~40 files. Categorized by risk: 3 direct hex mempAlloc sizes, 11 arithmetic scratch offsets, 8 manual alignment masks, 28+ stack buffers with hex sizes, 6+ hardcoded array multipliers.

4. **IS4MB/ALIGN16 audit** — IS4MB is compile-time `(0)`, all 4MB branches dead-code-eliminated. 119 ALIGN16 wrappers across 39 files, mostly ceremonial on PC (DMA is just memcpy). Low priority but contributes to code noise.

5. **MAX_BOTS stragglers** — investigated `g_AmBotCommands[9]`, confirmed it's a 3×3 active-menu UI grid (not per-bot). No fix needed. All other arrays properly use constants.

6. **Created `context/memory-modernization.md`** — comprehensive 6-phase plan (M0–M6) covering immediate safety fixes through full pool separation. Each phase independently testable.

7. **Executed M0** — converted 24 INIT:/INTRO: diagnostic log lines from `LOG_NOTE` → `LOG_VERBOSE` in pdmain.c. Removed orphaned `#include "system.h"` from challengeinit.c. Verified all 3 buffer-overflow fix sites are consistent (`sizeof(struct mpconfigfull) + 16`), no remaining `0x1ca`.

### Key Decisions
- Memory modernization will be phased (M0–M6), not a single big-bang refactor
- Phase M5 (separate pool regions) is the architectural change that eliminates pool overlap risk
- Phases M0–M4 are safe, incremental, no behavioral change
- `g_AmBotCommands[9]` confirmed NOT a MAX_BOTS bug — it's a 3×3 active-menu UI grid

### Files Changed
- `context/memory-modernization.md` — NEW: full audit + 6-phase gameplan
- `context/README.md` — added memory-modernization.md to file index
- `context/session-log.md` — this entry
- `port/src/pdmain.c` — 24× `LOG_NOTE` → `LOG_VERBOSE` for INIT:/INTRO: diagnostic lines
- `src/game/challengeinit.c` — removed orphaned `#include "system.h"`

### Next Steps
- **Rebuild** to verify Session 13 buffer fix + M0 logging cleanup boots cleanly
- **New session**: Begin M1 (name magic numbers) — create `memsizes.h`, replace literals

---

## Session 13 — 2026-03-21

**Focus**: Fix client crash — model loading at boot before subsystems ready

### What Was Done

1. **Identified true root cause** — Rebuild confirmed: files DO exist in ROM (`romdataFileGetData` returns non-NULL). The real problem: `catalogValidateAll()` ran at `pdmain.c:309`, BEFORE `texInit()` (line 319), `langInit()` (line 320), and other subsystems. Model loading via `modelPromoteOffsetsToPointers` / `modeldef0f1a7560` touches texture and skeleton systems that aren't initialized at that point → ACCESS_VIOLATION on ALL 151 models. 151 VEH/longjmp cycles corrupted heap state; game died silently after "End Validation".

2. **Removed `catalogValidateAll()` from boot (pdmain.c)** — The original port never bulk-validated models at boot. Replaced the call with an explanatory comment. Models will be validated lazily via `catalogGetSafeBody()`/`catalogGetSafeHead()` → `catalogValidateOne()` when first accessed during gameplay, by which point all subsystems are initialized.

3. **Kept defensive improvements from earlier in session** — These are still valuable for the lazy validation path:
   - `file.c` (`fileLoadToNew`): pre-check `romdataFileGetData` — returns NULL for non-existent files
   - `modeldef.c` (`modeldefLoad`): clears `g_LoadType` on NULL early-return
   - `modelcatalog.c` (`catalogValidateOne`): pre-check `romdataFileGetData` + `#include "romdata.h"`

4. **Confirmed ROM is valid** — `romdataInit: loaded rom, size = 33554432` (32MB NTSC-Final). All segments loaded successfully. No ROM corruption.

### Key Insight
Boot-time model validation was doomed by init ordering. `catalogValidateAll()` sat between `mempSetHeap()` (line 304) and the subsystem init cascade (`texInit` at 319, `langInit` at 320, etc.). Models depend on those subsystems — they can only be loaded after the full init sequence completes. The correct architecture is lazy/on-demand validation during gameplay.

### Files Changed
- `port/src/pdmain.c` — Removed `catalogValidateAll()` call, replaced with comment
- `src/game/file.c` — `fileLoadToNew`: pre-check for non-existent ROM files
- `src/game/modeldef.c` — `modeldefLoad`: clear `g_LoadType` on NULL return
- `port/src/modelcatalog.c` — `catalogValidateOne`: pre-check + romdata.h include

### Next Steps
- Rebuild and test — game should boot past init to title screen
- Verify server launch from build tool works (confirmed working in log)

---

## Session 12 — 2026-03-21

**Focus**: Client crash-on-launch diagnosis and fix; server log filename bug

### What Was Done

1. **Diagnosed client crash-on-launch** — Client opened and immediately closed after build. Root cause: `catalogValidateAll()` was called in `main.c:198` BEFORE `mempSetHeap()` (called later in `pdmain.c:303` inside `mainInit()`). Every `modeldefLoadToNew()` call triggered `mempAlloc()` on uninitialized pool state, causing 151 consecutive access violations caught by VEH. Memp internals corrupted by longjmp, program crashed silently in `mainProc()`.

2. **Fixed init ordering** — Moved `catalogValidateAll()` from `main.c` to `pdmain.c` after `mempSetHeap()`. Added `#include "modelcatalog.h"` to pdmain.c. Left explanatory comment at the old call site.

3. **Added defensive guard** — `catalogValidateAll()` now checks `mempGetStageFree() == 0` before attempting model loads. If the pool system isn't ready, it logs an error and returns safely instead of crashing.

4. **Fixed server log filename** — `PerfectDarkServer.exe` was writing to `pd-client.log` instead of `pd-server.log`. Root cause: `sysInit()` checked `sysArgCheck("--dedicated")` (CLI args) but the server sets `g_NetDedicated = 1` directly without passing `--dedicated` in argv. Fix: check both `g_NetDedicated` variable AND `sysArgCheck("--dedicated")`.

5. **Boot path audit** — Scanned all init functions called before `mempSetHeap()` (romdataInit, gameInit, modmgrInit, catalogInit, filesInit) — confirmed none call `mempAlloc`. catalogValidateAll was the only offender.

### Files Modified
- `port/src/main.c` — Removed `catalogValidateAll()` call, added comment
- `port/src/pdmain.c` — Added `catalogValidateAll()` after `mempSetHeap()`, added include
- `port/src/modelcatalog.c` — Added `mempGetStageFree()` guard, added `lib/memp.h` include
- `port/src/system.c` — Log path selection now checks `g_NetDedicated`/`g_NetHostLatch` variables

### Key Takeaway
When adding new init-time systems (like the model catalog), always verify the full dependency chain. The "heap is allocated" != "pool allocator is initialized". The raw memory (`sysMemZeroAlloc`) must be passed through `mempSetHeap()` before `mempAlloc()` can use it.

---

## Session 11 — 2026-03-21

**Focus**: Log channel filter system, always-on logging, debug menu wiring, build tool polish, release cleanup

### What Was Done

1. **Always-on logging** (`port/src/system.c`)
   - Removed `sysArgCheck("--log")` gate — logging is now unconditional in `sysInit()`
   - Log filename: `pd-server.log` (dedicated), `pd-host.log` (host), `pd-client.log` (client)
   - Removed `--log` from build-gui.ps1 launch args (no longer needed)

2. **Log channel filter system** (`port/include/system.h`, `port/src/system.c`)
   - 8 channels: Network, Game, Combat, Audio, Menu, Save, Mods, System
   - Bitmask constants (LOG_CH_NETWORK through LOG_CH_SYSTEM) + LOG_CH_ALL/LOG_CH_NONE presets
   - `sysLogClassifyMessage()` — maps ~30 known string prefixes to channel bitmask
   - Filter logic in `sysLogPrintf()`: warnings/errors always pass, LOG_NOTE filtered by channel, untagged messages always pass
   - `sysLogSetChannelMask()` / `sysLogGetChannelMask()` with change logging
   - Zero changes to existing 470+ log call sites (prefix-based classification)

3. **ImGui debug menu log section** (`port/fast3d/pdgui_debugmenu.cpp`)
   - `pdguiDebugLogSection()` — All/None preset buttons, per-channel checkboxes, hex mask readout
   - Wired into `pdguiDebugMenuRender()` after Theme section

4. **Build tool version layout fix** (`build-gui.ps1`)
   - Moved increment/decrement buttons from beside version fields to below them
   - Layout now: `[0] . [0] . [0]` row, then `[-][+] [-][+] [-][+]` row beneath
   - All downstream section Y-positions shifted to accommodate

5. **Latest released version display** (`build-gui.ps1`)
   - VERSION section shows `dev: X.Y.Z` and `stable: X.Y.Z` from GitHub Releases
   - Added `Prerelease` field to release cache system
   - Labels refresh on startup (disk cache) and after each API fetch
   - `*` suffix when showing cached (offline) data

6. **Window height reduction** (`build-gui.ps1`)
   - Form 880x680 → 880x560, sidebar trimmed to fit content
   - Console, utility bar, progress bar repositioned to sit just below sidebar

7. **Release script cleanup** (`release.ps1`)
   - Added step 6/6: local backup + cleanup after push
   - Stable releases: zip backed up to `backups/` before cleanup
   - Dev releases: no local backup, GitHub is source of truth
   - Staging directories auto-cleaned after push + orphans cleaned
   - Added `backups/` to .gitignore

8. **Version reset** — CMakeLists.txt VERSION_SEM_PATCH 4→1

9. **CMake icon.rc fix** (`CMakeLists.txt`)
   - Both `add_executable` blocks referenced `dist/windows/icon.rc` which didn't exist
   - Guarded both with `if(WIN32 AND EXISTS ...)` to prevent configure failures
   - Affects both client (line ~419) and server (line ~482) targets

10. **CMake error log surfacing** (`build-gui.ps1`)
    - On configure failure, reads last 40 lines of `CMakeFiles/CMakeError.log` and `CMakeFiles/CMakeConfigureLog.yaml` (or `CMakeOutput.log`)
    - Prints with color classification (errors red, warnings orange, info white)
    - Helps diagnose build failures without leaving the build tool

11. **LOG_VERBOSE level** (`port/include/system.h`, `port/src/system.c`, `port/fast3d/pdgui_debugmenu.cpp`)
    - New `LOG_VERBOSE` enum value below `LOG_NOTE` — trace-level detail, off by default
    - `sysLogGetVerbose()` / `sysLogSetVerbose()` API
    - Dropped early in `sysLogPrintf()` unless `--verbose` CLI flag or debug menu toggle
    - Subject to channel filtering same as LOG_NOTE
    - Debug menu: Verbose checkbox + mask readout shows `+V` when enabled
    - Stdout routing: verbose goes to stdout (like NOTE/CHAT), not stderr

12. **Font size increase** (`build-gui.ps1`)
    - All UI font sizes bumped: form 9→10, title 14→16, section headers 8→9, buttons 9→10, small labels 7→8
    - Console output stays 9pt Consolas (monospace)
    - Error count 7→8, progress label 8→9

13. **Custom font — Handel Gothic Regular** (`build-gui.ps1`)
    - `PrivateFontCollection` loads `fonts/Menus/Handel Gothic Regular/Handel Gothic Regular.otf`
    - `New-UIFont` helper creates fonts from Handel Gothic (falls back to Segoe UI)
    - All Segoe UI instances replaced with `New-UIFont` calls (console Consolas kept)
    - Bold variant supported via `[switch]$Bold` parameter

14. **Build tool version label** (`build-gui.ps1`)
    - "Build tool version 3.1" in lower-left corner, very dim gray (70,70,70)
    - Version stored in `$script:BuildToolVersion` for easy bumping

15. **Settings restructure** (`build-gui.ps1`)
    - Moved Settings from File menu to new Edit menu (standard convention)
    - Settings dialog now uses TabControl with two tabs: General, Asset Extraction
    - General tab: GitHub auth, repo, sounds toggle (unchanged functionality)
    - Asset Extraction tab: ROM path field with Browse dialog, extraction tools section
    - ROM path persisted in `.build-settings.json`, auto-detected on startup
    - `Resolve-RomPath` helper: opens `OpenFileDialog` if ROM not found
    - Extraction tools: reusable `New-ExtractToolRow` creates button + status label rows
    - Sound Effects tool: functional (runs `extract-build-sounds.py`)
    - Models & Textures, Animations, Levels: placeholder rows (disabled until tools exist)
    - Each tool row auto-detects existing extracted files and shows count in green

### Files Modified
- `port/include/system.h` — LOG_CH_* defines, LOG_VERBOSE, verbose API, extern arrays
- `port/src/system.c` — Unconditional logging, filter state, classifier, filter logic, verbose state, --verbose flag
- `port/fast3d/pdgui_debugmenu.cpp` — Log section + render wiring + verbose checkbox
- `build-gui.ps1` — Layout, version labels, window resize, --log removal, CMake error surfacing, font system, version label, Settings restructure (Edit menu, tabs, ROM path, extraction tools)
- `release.ps1` — Post-push cleanup, stable backup to backups/
- `CMakeLists.txt` — Version patch reset to 1, icon.rc EXISTS guards
- `.gitignore` — Added backups/

---

## Session 10 — 2026-03-21 (continued)

**Focus**: Connect code system completion, ROM missing dialog, build tooling polish

### What Was Done

1. **Connect code system** (`port/src/connectcode.c`, `port/include/connectcode.h`)
   - Cleaned up stale `s_WordTable[256]` from first attempt, keeping only `s_Words[256]`
   - 256 unique PD-themed words: characters, locations, weapons, gadgets, vehicles, missions, multiplayer, sci-fi
   - Encode: IP (4 bytes) + port (2 bytes) → 6 words (e.g. "JOANNA FALCON CARRINGTON SKEDAR PHOENIX DATADYNE")
   - Decode: case-insensitive, supports space/hyphen/dot separators
   - Added to CMakeLists.txt server source list (client auto-discovers via GLOB_RECURSE)

2. **Server GUI connect code** (`port/fast3d/server_gui.cpp`)
   - Status bar now shows connect code in green when UPnP provides external IP
   - "Copy Code" button copies connect code to clipboard via SDL
   - Raw IP:port shown underneath in subdued gray for reference
   - Falls back to port-only display when UPnP inactive

3. **In-game server overlay connect code** (`port/fast3d/pdgui_lobby.cpp`)
   - Dedicated server overlay shows connect code + "Copy" button when public IP available
   - Same subdued raw IP display underneath

4. **Server console connect code** (`port/src/net/netupnp.c`)
   - UPnP success log now also prints the connect code for easy sharing from console

5. **Client join flow — connect code support** (`port/fast3d/pdgui_menu_network.cpp`, `port/src/net/netmenu.c`)
   - Direct Connect input now accepts either raw IP:port OR word-based connect codes
   - Auto-detects input type by checking for alpha characters
   - Connect code decoded to IP:port string, then passed to `netStartClient()` as usual
   - Both ImGui and native menu paths updated
   - Hint text added: "Enter IP:port or connect code"

6. **ROM missing dialog** (`port/src/romdata.c`)
   - Replaced bare `sysFatalError()` SDL message box with custom `SDL_ShowMessageBox`
   - Dialog clearly states: exact ROM filename (`pd.ntsc-final.z64`), data folder path, z64 format requirement
   - Two buttons: "Open Folder" (opens data dir in system file manager) and "Exit"
   - Cross-platform: `explorer` on Windows, `open` on macOS, `xdg-open` on Linux

### Files Modified
- `port/src/connectcode.c` — cleaned up stale array
- `CMakeLists.txt` — added connectcode.c to server source list
- `port/fast3d/server_gui.cpp` — connect code display + copy button in status bar
- `port/fast3d/pdgui_lobby.cpp` — connect code in dedicated server overlay
- `port/src/net/netupnp.c` — connect code in UPnP success log
- `port/fast3d/pdgui_menu_network.cpp` — connect code decode in ImGui join flow
- `port/src/net/netmenu.c` — connect code decode in native menu join flow
- `port/src/romdata.c` — ROM missing dialog with Open Folder button

### Decisions Made
- Connect code detection is simple alpha-char check — if input contains letters, treat as code; otherwise as raw IP
- ROM dialog uses SDL_ShowMessageBox (not ImGui) because ROM loading happens before ImGui is initialized
- Raw IP still shown in subdued color alongside connect code for debugging/advanced users

### Next Steps
- Build test all changes (Mike needs to compile)
- Verify connect code round-trip encoding/decoding works correctly
- Test ROM missing dialog on Windows
- Continue with master server / server browser if needed

---

## Session 9 — 2026-03-21 (continuation)

**Focus**: Version format simplification, build tool polish, exe renaming, release packaging, connect code design

(See previous session summary for details — this session covered extensive changes to
version format from 4-field to 3-field, dark menu theme, taskbar visibility, GitHub
release checking, offline cache support, executable renaming to PerfectDark.exe /
PerfectDarkServer.exe, release.ps1 restructuring, and began connect code implementation.)

---

## Session 8 — 2026-03-21 (late evening)

**Focus**: Updates tab wiring, download button UX, spawn pads, branch consolidation

### What Was Done

1. **Wired "Updates" tab into Settings menu** (`pdgui_menu_mainmenu.cpp`)
   - Added 5th tab "Updates" (index 4) to `renderSettingsView()`
   - Added `selFlag4` for bumper-driven tab selection
   - Updated bumper wrap range from 0-3 to 0-4
   - Updated `s_SettingsSubTab` comment to document all 5 tabs
   - Added `extern "C"` forward declaration for `pdguiUpdateRenderSettingsTab()`

2. **Refactored update UI for inline Settings rendering** (`pdgui_menu_update.cpp`)
   - Extracted `renderVersionPickerContent(tableH)` from `renderVersionPicker()`
   - `renderVersionPicker()` now wraps content in floating window (for notification banner)
   - New `pdguiUpdateRenderSettingsTab()` renders content inline in Settings tab
   - Auto-triggers version check on first tab view (one-shot `s_TabCheckTriggered`)
   - Avoids double-render: Settings tab renders inline, floating picker only from banner

3. **Fixed download button UX bugs** (`pdgui_menu_update.cpp`)
   - Added `s_DownloadFailed` state flag — set on `UPDATER_DOWNLOAD_FAILED`
   - Download failure now shown as red text with error message from `updaterGetError()`
   - Button label changes to "Retry Download" after failure, resets on click
   - Added null check on `updaterGetCurrentVersion()` — shows "(unknown)" if null
   - Added null guard on `cur` in `versionCompare` calls (isCurrent, rollback check)

4. **Expanded spawn pad arrays** (`src/game/player.c`)
   - Changed `verybadpads[24]`, `badpads[24]`, `padsqdists[24]` to `[MAX_MPCHRS]` (32)
   - Updated comment: removed `@dangerous` warning, documented MAX_MPCHRS sizing
   - Prevents overflow on custom maps with >24 spawn points

### Files Modified
- `port/fast3d/pdgui_menu_mainmenu.cpp` — 5th Settings tab (Updates), bumper wrap, selFlag4
- `port/fast3d/pdgui_menu_update.cpp` — content extraction refactor, inline tab, download failure UX, null safety
- `src/game/player.c` — spawn pad arrays 24→MAX_MPCHRS (32)

5. **Consolidated to single-branch workflow**
   - Merged `release` branch into `dev` (resolved all conflicts in favor of dev)
   - Removed branch switcher dropdown from `build-gui.ps1`
   - Replaced with read-only "Channel" indicator (derived from version: dev > 0 = Dev, else = Stable)
   - Removed `Switch-Branch`, `Refresh-BranchList` functions entirely
   - Push Release now uses `git branch --show-current` instead of dropdown selection
   - Root cause of yesterday's entire bug cascade: switching to `release` branch built stale code missing all UI work

### Files Modified
- `port/fast3d/pdgui_menu_mainmenu.cpp` — 5th Settings tab (Updates), bumper wrap, selFlag4
- `port/fast3d/pdgui_menu_update.cpp` — content extraction refactor, inline tab, download failure UX, null safety
- `src/game/player.c` — spawn pad arrays 24→MAX_MPCHRS (32)
- `build-gui.ps1` — removed branch switcher, added channel indicator derived from version

### Decisions
- **Single branch forever**: no dev/release split, channel is a version flag
- Update content renders inline in Settings tab (not as floating dialog)
- Floating version picker kept for notification banner "View updates" flow
- `pdguiUpdateRenderSettingsTab()` is the canonical Settings entry point
- Version field `dev > 0` = prerelease (Dev channel), `dev = 0` = Stable channel

---

## Session 7 — 2026-03-21 (evening)

**Focus**: Branch reconciliation, multi-bug triage, full system audit

### Problem Statement
Mike reported 5 issues after building from `release` branch for buddy testing:
1. Update menu tab missing from Settings
2. Tons of libcurl DLLs required (should be static)
3. Server crashed when joining
4. Can't add bots in local lobby
5. Can't reach online lobby

### Root Cause Analysis

**Root Cause 1: Branch Divergence** — `dev` branch had working UI (Update tab,
menu fixes, matchsetup improvements) that was never merged into `release`. The
`release` branch got backend changes (strncpy, 32-slot expansion) that `dev` didn't
have. Result: release was missing half the UI work.

**Root Cause 2: MATCH_MAX_SLOTS Struct Mismatch** — The 32-slot expansion
(Session 6) changed `matchsetup.c` (C) to `MATCH_MAX_SLOTS = MAX_MPCHRS = 32`
but `pdgui_menu_matchsetup.cpp` (C++) still had `MATCH_MAX_SLOTS = 16`. Since
both files define `struct matchconfig` with `slots[MATCH_MAX_SLOTS]`, all fields
after `slots[]` were at wrong offsets in C++. This caused: bot addition failure
(numSlots read as garbage), corrupted match data flowing to server, server crash
on join, and client unable to reach lobby (malformed SVC_STAGE_START).

**Root Cause 3: Static libcurl OpenGL Crash** — Static linking of curl + OpenSSL +
30 transitive deps caused Windows GDI to init before the GPU's OpenGL ICD,
resulting in GL 1.1 fallback instead of full driver. Fix: switch to dynamic linking.

### What Was Done

1. **Switched to dev branch** as working base (all UI working there)
2. **Cherry-picked strncpy fixes** (e0a8853 from release) → commit `dad0256`
3. **Cherry-picked 32-slot expansion** (8c6e47a from release) with critical fix:
   synced MATCH_MAX_SLOTS in C++ from 16 to MAX_MPCHRS (32) → commit `9722013`
4. **Switched libcurl to dynamic linking** — replaced static curl CMake block with
   simple dynamic link + post-build DLL copy (16 DLLs) + CURLSSLOPT_NATIVE_CA
   for Windows cert store → commit `9eb899e`
5. **Full system audit** (4 parallel audits):

   **Menu Audit** — ALL PASS. Controller nav, MKB, focus management, B/Escape,
   combo boxes, tab switching all working correctly across all 8 menu files.

   **Lobby/Network Audit** — ALL PASS. State transitions sound, leader election
   secure with non-leader rejection, bot sync correct up to 24, async tick
   scheduler sound, protocol v19 enforced at all entry points. No O(n²) loops.
   Estimated bandwidth: ~180 KB/s downstream with 32 characters (acceptable).

   **Spawn System Audit** — PASS with caveat. All arrays correctly sized for 32
   characters. chrslots bitmask correct (u32, bits 0-7 players, 8-31 bots).
   **One vulnerability**: `playerChooseSpawnLocation()` has `verybadpads[24]`,
   `badpads[24]`, `padsqdists[24]` — safe for all stock maps (max 21 pads)
   but will overflow on custom maps with >24 spawn points. Needs expanding.

   **Update Tab Audit** — CRITICAL FINDING: The "Updates" tab exists on `dev`
   branch's code. `pdguiUpdateShowPicker()` is defined but never called from
   the Settings menu. Need to wire it into Settings as a 5th tab.

### Commits on dev (this session)
- `dad0256`: strncpy null-termination (cherry-pick from release)
- `9722013`: 32-slot expansion + MATCH_MAX_SLOTS C/C++ sync fix
- `9eb899e`: Dynamic libcurl + DLL copy + SSL CA cert fix

### Still In Progress
- **Add "Updates" tab to Settings** — need to add 5th tab to renderSettingsView()
  that calls pdguiUpdateShowPicker(), update bumper wrap from 3→4, add selFlag4
- **Fix Update download button bugs** — download failure not displayed, button
  clickable after failure but does nothing, missing null check on current version
- **Expand spawn pad arrays** from 24 to MAX_MPCHRS (32) in player.c

### Decisions
- Dev branch is now the primary development branch (release was a dead end)
- Dynamic curl is the permanent approach (static caused OpenGL init race)
- MATCH_MAX_SLOTS must always be defined via MAX_MPCHRS, never hardcoded

---

## Session 6 — 2026-03-21

**Focus**: strncpy null-termination audit, ADR-001, build tool commit

### What Was Done
- Re-applied netmsg.c fixes lost during context compaction (3 strncpy + 1 strcpy→snprintf)
- **Propagation check**: Scanned entire port/ codebase for the same strncpy class bug
  - Found 17 additional instances across 8 files (net.c, updater.c, modmgr.c, fs.c, libultra.c, config.c, input.c, optionsmenu.c)
  - Fixed all 17 with consistent `buf[SIZE - 1] = '\0'` pattern
- Created ADR-001 documenting the architecture audit findings
- Previous session's build tool changes committed: static linking fix, headless server, auto-stash, version increment buttons, release overwrite

### Files Modified
- `port/src/net/netmsg.c` — 3 strncpy + 1 strcpy fix
- `port/src/net/net.c` — 4 strncpy fixes
- `port/src/updater.c` — 2 strncpy fixes
- `port/src/modmgr.c` — 4 strncpy fixes
- `port/src/fs.c` — 5 strncpy fixes
- `port/src/libultra.c` — 3 strncpy fixes
- `port/src/config.c` — 3 strncpy fixes
- `port/src/input.c` — 1 strncpy fix
- `port/src/optionsmenu.c` — 2 strncpy fixes
- `context/ADR-001-lobby-multiplayer-architecture-audit.md` — NEW

### Decisions
- strncpy null-termination is now a project-wide standard; future code should prefer snprintf

---

## Sessions 4-5 — 2026-03-20

**Focus**: Architecture audit, build tool improvements, release tooling

### What Was Done
- Complete architecture audit of lobby/multiplayer systems
- Fixed CLC_MAP_VOTE_START ID collision (was 0x09, same as CLC_LOBBY_MODE → shifted to 0x0A)
- CMakeLists.txt: Fixed static linking path resolution (SDL2, zlib, libcurl)
- build-gui.ps1: Server headless launch, auto-stash branch switching, version increment buttons
- release.ps1: Release overwrite support, DLL warning cleanup
- Identified and documented 3 verified false positives in the audit

### Decisions
- Server launches headless by default (avoids OpenGL context contention)
- Auto-stash on branch switch with tagged restore
- Release overwrite: delete existing GitHub release + tags before recreating

---

## Session 3 — 2026-03-19

**Focus**: Phase 3 dedicated server, lobby system

### What Was Done
- Completed Phase 3: Dedicated-server-only multiplayer model
- New multiplayer menu (server browser, direct IP connect)
- Lobby system rewrite with leader election
- Lobby screen with game mode selection
- Server GUI (4-panel layout) and headless mode
- CLC_LOBBY_START protocol for match launching
- Cleanup: renamed "Network Game" to "Multiplayer", removed stale host menus

---

## Session 2 — 2026-03-18

**Focus**: ImGui debug menu, styling, build system

### What Was Done
- PD-authentic styling with pixel-accurate shimmer from menugfx.c
- 7 built-in palettes including Black & Gold
- F12 debug menu with mouse capture/release
- Build tool: colored progress bar, gated run buttons, process monitoring
- Font size 16pt → 24pt

---

## Session 1 — 2026-03-17

**Focus**: Menu system Phase 2 (agent create, delete, typed dialogs, network audit)

### What Was Done
- Agent Create screen with 3D character preview (FBO)
- Agent Select enhancements (contextual actions, delete confirmation)
- Typed dialog system (DANGER + SUCCESS)
- Network audit began (ENet protocol, message catalog)
- Skedar/DrCaroll mesh fix (duplicate array indices in g_MpBodies)
