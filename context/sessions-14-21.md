# Session Archive: Sessions 14–21

> **Period**: 2026-03-21 to 2026-03-22
> **Theme**: Memory modernization, combat stabilization, menu Phase 2, 32-bot support
> Back to [index](README.md)

---

## Session 21 — 2026-03-22

**Focus**: Combat log analysis, crash confirmed fixed, player instant death bug diagnosed and fixed

- **Crash chain fully resolved** — 3 minutes of 24-bot combat, clean shutdown.
- **B-02 (shots through bots)**: FIXED — 235 damage events, 19 kills. Scale clamp removal restored hitboxes.
- **B-03 (player instant death)**: FIXED — `handicap = 0` (BSS zero-init) → `mpHandicapToDamageScale(0) = 0.1` → 10× damage multiplier. Fix: force `handicap = 0x80` at match start in mplayer.c.
- **B-10 (End Game crash)**: NEW — ACCESS_VIOLATION on End Game selection. Backtrace captured.
- **Dual health system documented**: Bots use `chr->damage/maxdamage`, players use `bondhealth` (0.0–1.0).

**Files**: mplayer.c (handicap fix), player.c + chraction.c (diagnostic logging)

---

## Session 20 — 2026-03-21

**Focus**: First build test of Sessions 12–19, crash diagnosis, scale clamp removal

- **Build test PASS**: Model loading crash chain fix confirmed. Catalog validation clean, 24 bots spawn.
- **Scale clamp identified as destructive**: `modeldef->scale > 100` clamp destroyed valid mod model scales (700–2000). Removed; now only rejects ≤ 0.
- **Crash persisted** after scale fix → traced to `chrUpdateGeometry` POS_DESYNC diagnostic accessing unallocated model matrices during camera flythrough.
- **Fix**: Removed POS_DESYNC diagnostic from `chrUpdateGeometry` (fundamentally unsafe in collision path). Hardened `chrTestHit` with `model->matrices` check.
- **Debug symbols fix**: Added explicit `-g` to CMakeLists.txt.

**Files**: body.c, modelcatalog.c, CMakeLists.txt, chr.c

---

## Session 19 — 2026-03-21

**Focus**: Crash log analysis, hit radius root cause, ammo init fix, BotController design

- **Ammo init bug**: `botmgr.c:122` and `bot.c:146` iterated `i < 33` but array is `AMMO_TYPE_COUNT` (36). Last 3 slots × 24 bots = 72 uninitialized values. Fixed.
- **Hit radius traced**: `chrGetHitRadius()` → `modeldef->scale * model->scale`. If model loading corrupt (Sessions 12–13 fix not yet compiled), `modeldef->scale` is zeroed → zero hit radius.
- **BotController architecture approved**: Wrapper around existing chr/aibot. Extension points for physics, combat telemetry, lifecycle hooks.

**Files**: botmgr.c, bot.c, chr.c (diagnostic logging)

---

## Session 18 — 2026-03-21

**Focus**: Combat bug diagnosis, combat debug logging channel, constraints.md creation

- Deep diagnosis of two combat bugs: shots-through-bots (model root matrix vs prop→pos divergence) and player instant death (unknown scaling).
- **Combat debug logging**: 5 files, ~30 log points. COMBAT: prefix routed to LOG_CH_COMBAT. See [session log detail](session-log.md) for full log format reference.
- **Created [constraints.md](constraints.md)**.
- End Game crash initial analysis — likely array bounds in endscreen scoring.

**Files**: chr.c, chraction.c, botact.c, prop.c, system.c

---

## Session 17 — 2026-03-21

**Focus**: Fix model/anim pool exhaustion (ROOT CAUSE #2 of 12+ char crash)

- **Pool sizes expanded**: NUMTYPE1 35→70, NUMTYPE2 25→50, NUMTYPE3 20→48, NUMSPARE 60→80.
- **modelmgrAllocateSlots crash**: `numchrs` didn't count simulant bots. `g_MaxAnims = 0 + 20 = 20` → NULL after 20 bots. Fixed: bot counting from `g_MpSetup.chrslots` bitmask.
- **Object pools doubled**: weapons 50→100, hats 10→20, ammo 20→40, debris 15→30, projectiles 100→200, embedments 80→160.
- **Pattern found**: "numchrs doesn't count bots" existed in TWO call sites in setup.c (chrmgrConfigure + modelmgrAllocateSlots).

**Files**: setup.c, modelmgr.c, modelmgrreset.c, botmgr.c

---

## Session 16c — 2026-03-21

**Focus**: 12+ character crash ROOT CAUSE #1 found and fixed

- **ROOT CAUSE**: `chrmgrConfigure()` allocated `PLAYERCOUNT() + numchrs + 10` chr slots where `numchrs` only counted map NPCs, not simulant bots. 1 player + 0 NPCs + 10 buffer = 11 slots for 24 bots → chrInit returns NULL → NULL deref.
- **Fix**: Added bot-counting loop from `g_MpSetup.chrslots` bitmask before `chrmgrConfigure()`.
- **Safety nets**: NULL checks in chrInit, chr0f020b14, chrAllocate.
- Added LOG_CH_MATCH channel (9th debug channel).
- Match Setup UI: "Add Bot" button replaces +/- pair.

**Files**: setup.c, chr.c, chrmgr.c, system.h, system.c, pdgui_menu_matchsetup.cpp

---

## Session 16b — 2026-03-21

**Focus**: Match Setup layout restructure per Mike's annotated screenshot

- Swapped column order (Settings left, Players right). Removed renderSlotDetail (replaced by per-bot popup). Always-visible weapon slots. Collapsible Options section.

**Files**: pdgui_menu_matchsetup.cpp

---

## Session 16 — 2026-03-21

**Focus**: Deep crash audit, bug pattern catalog, Debug tab polish, config persistence

- Deep audit of MAX_PLAYERS-sized arrays. Key finding: `g_Vars.currentplayerstats->mpindex` is ALWAYS local (0–3). 12+ char crash is different bug.
- **Created [bug-patterns.md](systemic-bugs.md)** (now systemic-bugs.md): 9 recurring patterns.
- Debug tab: button width 80→110px, theme button coloring with accent/text colors.
- Verbose logging + channel mask persistence via configRegisterInt/UInt.

**Files**: botmgr.c, mplayer.c, pdgui_menu_mainmenu.cpp, system.c

---

## Session 15 — 2026-03-21

**Focus**: Combat sim crash fix (MAX_PLAYERS), Debug tab, Memory modernization M1

- **Fixed combat sim crash**: `g_Menus[MAX_PLAYERS]` (8) accessed with bot mpindex (8–31). 6 sites across 4 files. Bounds-check approach (bots skip menu ops, not array expansion).
- **Debug tab in Settings**: Log filters, verbose toggle, theme selector (7 palettes), memory diagnostics, shortcut reference.
- **M1**: Created `memsizes.h` with 30+ named constants. Replaced magic numbers in 8 files (menu.c, bg.c, botmgr.c, varsreset.c, chrmgr.c, bondgun.c, credits.c, zbuf.c).

**Files**: ingame.c, mplayer.c, bondview.c, menutick.c, pdgui_menu_mainmenu.cpp, memsizes.h (NEW), 8 game files

---

## Session 14 — 2026-03-21

**Focus**: Memory modernization audit and gameplan

- Full codebase audit of memp pool system (bump allocator, no individual free, 64MB heap).
- Memory aliasing audit: 13 union types, compressed-data-overlap pattern, pool boundary risks.
- Magic number audit: 155+ hardcoded sizes across ~40 files.
- IS4MB/ALIGN16 audit: IS4MB is compile-time dead, 119 ALIGN16 wrappers in 39 files.
- **Created [memory-modernization.md](memory-modernization.md)**: 6-phase plan (M0–M6).
- **Executed M0**: 24 diagnostic logs → LOG_VERBOSE, orphaned include cleanup.

**Files**: memory-modernization.md (NEW), pdmain.c, challengeinit.c
