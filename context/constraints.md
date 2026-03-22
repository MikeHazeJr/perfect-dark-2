# Constraints Ledger

## Purpose
This file tracks what we must respect (active constraints) and what we've intentionally abandoned
(removed constraints). Before implementing anything complex, check whether the complexity comes
from a constraint that has already been removed.

---

## Active Constraints

These are things we must still respect:

- **Save file format compatibility**: Config values stored in `pd.ini` via configRegisterInt/UInt. Save migration framework (SAVE_VERSION) exists for future format changes.
- **ENet protocol version**: Must match across clients. Currently v19 (bumped from 18: chrslots u16→u32 in SVC_STAGE_START).
- **30 agent save slots**: Hardcoded in filelist struct layout (`g_MpSetup.chrslots` is a u32 bitmask — 32 bits max). Cannot increase without breaking save format.
- **MAX_MPCHRS = 36**: Maximum multiplayer characters (4 human + 32 bots). Arrays sized to this.
- **MAX_PLAYERS = 4**: Maximum local human players. Many arrays are sized to this.
- **PLAYERCOUNT()**: Returns number of local human players (1-4), not total chrs.
- **ROM data files**: Model/animation files come from ROM dump. Models not in ROM return NULL from fileLoadToNew (fixed Session 13). Mod content extends via mod loader.
- **C11 game code / C++ port code**: Game logic in C11, port/renderer in C++. Must not introduce C++ in `src/game/` or `src/lib/`.
- **CMake + MinGW/GCC on Windows**: User compiles on Windows. AI cannot compile — code must be reviewed for correctness before delivery.
- **ENet statically linked**: Along with SDL2, zlib, and libcurl.
- **60 Hz tick rate**: Game logic runs at 60 ticks per second. Network sync frequencies are multiples of this.

---

## Removed Constraints

These constraints have been explicitly abandoned. If a task involves working around one of these, **stop and propose the simpler approach**.

- **2026-03-01: N64 platform guards** — PC-only target. All 672 `PLATFORM_N64` guards stripped across 114+ files. Zero references remain. (Phase D1 complete)
- **2026-03-01: 4MB memory mode** — `IS4MB()` is compile-time `0`, `IS8MB()` is compile-time `1`. Compiler eliminates all 4MB branches. `g_Is4Mb` variable removed.
- **2026-03-01: N64 dead code** — All N64 assembly (.s), ultra/os, ultra/libc removed. Only ultra/audio, ultra/gu, and 4 ultra/io vi mode files remain.
- **2026-03-05: N64 micro-optimization** — Modern x86_64 HW. Prefer correctness over cycle-counting. Hand-rolled sorts, manual bit-packing, fixed-point math — replace with standard approaches.
- **2026-03-10: Host-based multiplayer** — Dedicated-server-only model adopted (Phase D9). Server runs headless or with ImGui GUI.
- **2026-03-12: N64 collision workarounds** — Legacy cdTestVolume/cdFindGroundInfoAtCyl hacks replaced by capsule sweep system (capsule.c). Use proper geometric solutions.
- **2026-03-15: 4-player bot limit** — Original N64 supported ~8 bots max. PC port supports 32 bots (chrslots bitmask). Pool sizes expanded (Session 17): NUMTYPE1=70, NUMTYPE2=50, NUMTYPE3=48, NUMSPARE=80, weapons=100, hats=20, ammo=40, debris=30, projectiles=200, embedments=160.
- **2026-03-18: N64 body/head restriction in MP** — `mpGetNumBodies` unrestricted. Full 63+ character roster available in network play.
- **2026-03-20: `--log` CLI flag requirement** — Logging is now unconditional (always on). Log filename depends on mode: pd-server.log, pd-host.log, pd-client.log.

---

## Notes

- When you're about to do something complicated, check whether the reason it's complicated appears under Removed Constraints.
- When a constraint is removed, add it here with the date and rationale.
- When a new constraint is discovered (e.g., an array size limit that can't easily change), add it to Active.
