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
- **30 agent save slots**: Hardcoded in filelist struct layout. Cannot increase without breaking save format.
- **chrslots bitmask**: Currently u32 (8 player + 24 bot bits). **Being replaced by dynamic participant system** (B-12). Phase 1 coded (S26): `participant.h`/`.c` with heap-allocated pool runs parallel. Phase 2: migrate callsites. Phase 3: remove chrslots + protocol bump to v20. Default capacity 32, cheat-expandable to arbitrary.
- **MAX_LOCAL_PLAYERS = 4**: Maximum splitscreen players per machine. MAX_PLAYERS = 8 includes remote. Many arrays sized to these. Participant system uses `localslot` (0-3) per machine, `client_id` per network client.
- **PLAYERCOUNT()**: Returns number of local human players (1-4), not total chrs.
- **ROM data files**: Model/animation files come from ROM dump. Models not in ROM return NULL from fileLoadToNew (fixed Session 13). Mod content extends via mod loader.
- **`bool` is `s32`, not `_Bool`**: Defined in `types.h` and `data.h` as `#define bool s32`. **Never include `<stdbool.h>`** in game code — it redefines `bool` to `_Bool` and causes type conflicts. New game headers should include `"types.h"` to get `bool`.
- **C11 game code / C++ port code**: Game logic in C11, port/renderer in C++. Must not introduce C++ in `src/game/` or `src/lib/`.
- **CMake + MinGW/GCC on Windows**: User compiles on Windows. AI cannot compile — code must be reviewed for correctness before delivery.
- **ENet statically linked**: Along with SDL2, zlib, and libcurl.
- **60 Hz tick rate**: Game logic runs at 60 ticks per second. Network sync frequencies are multiples of this.
- **Name-based asset resolution only**: All asset references must use string IDs resolved through the Asset Catalog. No numeric ROM addresses, table indices, or array offsets for asset identity. The catalog returns runtime indices internally, but no code outside the catalog may hardcode or assume those indices. See [component-mod-architecture.md](component-mod-architecture.md) §5. Added Session 27.

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
- **2026-03-23: Shared memory pools for mods** — N64-era pre-allocated pools (modconfig.txt `alloc` values) replaced by dynamic `malloc`-based allocation. Each component manages its own memory. Advisory `hint_memory` field in `.ini` for UI display only.
- **2026-03-23: Monolithic mod structure** — Mods are no longer single directories loaded/unloaded as a unit. Replaced by component-based architecture where each asset (map, character, skin, etc.) is an independent folder with its own `.ini` manifest. See [component-mod-architecture.md](component-mod-architecture.md).
- **2026-03-23: Numeric asset lookups** — ROM addresses, table indices, and array offsets for asset identity are abandoned. All asset references go through the string-keyed Asset Catalog. This eliminates the root cause of B-13 (scale), B-17 (stage ID mismatch), and the entire class of index-shift bugs when mods change.

---

## Index Domain Warning

Three separate index spaces exist and must not be confused:

| Domain | Array | Size | Valid Range |
|--------|-------|------|-------------|
| Stage table index (`g_StageIndex`) | `g_Stages[87]` | 87 | 0–86 |
| Solo stage index | `g_SoloStages[21]` / `besttimes[21][3]` | 21 | 0–20 |
| Stagenum | Logical ID (e.g., 0x5e) | N/A | Arbitrary |

Mod stages have stage table indices 61–86 but **no** solo stage index. Flowing a stage table index into `g_SoloStages[]` or `besttimes[]` is an OOB access. Phase 1 safety guards added (Session 23). Phase 2 will add `soloStageGetIndex()` lookup.

---

## Notes

- When you're about to do something complicated, check whether the reason it's complicated appears under Removed Constraints.
- When a constraint is removed, add it here with the date and rationale.
- When a new constraint is discovered (e.g., an array size limit that can't easily change), add it to Active.
