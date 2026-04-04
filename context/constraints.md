# Constraints Ledger

## Purpose
This file tracks what we must respect (active constraints) and what we've intentionally abandoned
(removed constraints). Before implementing anything complex, check whether the complexity comes
from a constraint that has already been removed.

---

## Active Constraints

These are things we must still respect:

- **Save file format compatibility**: Config values stored in `pd.ini` via configRegisterInt/UInt. Save migration framework (SAVE_VERSION) exists for future format changes.
- **ENet protocol version**: Must match across clients. Currently **v27** (v20: D3R-9 NETCHAN_TRANSFER; v21: chrslots u32→u64 in SVC_STAGE_START; v22: CLC_LOBBY_START weapons[] array, S81; v23: NAT traversal messages + connect code port encoding, S83; v24: SVC_MATCH_MANIFEST + CLC_MANIFEST_STATUS + SVC_MATCH_COUNTDOWN, S84; v25: SVC_STAGE_START bot config block using catalog hashes, S90; v26: reserved; v27: all remaining net_hash u32 wire fields replaced with catalog ID strings — SVC_LOBBY_STATE, SVC_CATALOG_INFO, CLC_CATALOG_DIFF, SVC_DISTRIB_BEGIN/CHUNK/END, CLC_MANIFEST_STATUS, SVC_SESSION_CATALOG, SVC_MATCH_MANIFEST serialize). No net_hash may appear on the wire. **Next bump**: remove chrslots (B-12 Phase 3).
- **30 agent save slots**: Hardcoded in filelist struct layout. Cannot increase without breaking save format.
- **chrslots bitmask**: Currently **u64** (8 player + 32 bot bits, bits 0-7 players, bits 8-39 bots). **Being replaced by dynamic participant system** (B-12). Phase 1 coded (S26): `participant.h`/`.c` with heap-allocated pool runs parallel. Phase 2 done (S47b): callsites migrated. Phase 3 (remove chrslots): NEXT — will bump protocol to v24 (v22 and v23 already used). Default capacity 32. Expanded from u32 in S45 to support 31-bot matches. Use `1ull <<` for all chrslots bit operations.
- **MAX_LOCAL_PLAYERS = 4**: Maximum splitscreen players per machine. MAX_PLAYERS = 8 includes remote. Many arrays sized to these. Participant system uses `localslot` (0-3) per machine, `client_id` per network client.
- **PLAYERCOUNT()**: Returns number of local human players (1-4), not total chrs.
- **ROM data files**: Model/animation files come from ROM dump. Models not in ROM return NULL from fileLoadToNew (fixed Session 13). Mod content extends via mod loader.
- **`bool` is `s32`, not `_Bool`**: Defined in `types.h` and `data.h` as `#define bool s32`. **Never include `<stdbool.h>`** in game code — it redefines `bool` to `_Bool` and causes type conflicts. New game headers should include `"types.h"` to get `bool`.
- **C11 game code / C++ port code**: Game logic in C11, port/renderer in C++. Must not introduce C++ in `src/game/` or `src/lib/`.
- **CMake + MinGW/GCC on Windows**: User compiles on Windows. AI cannot compile — code must be reviewed for correctness before delivery.
- **ENet statically linked**: Along with SDL2, zlib, and libcurl.
- **60 Hz tick rate**: Game logic runs at 60 ticks per second. Network sync frequencies are multiples of this.
- **Name-based asset resolution only**: All asset references must use string IDs resolved through the Asset Catalog. No numeric ROM addresses, table indices, or array offsets for asset identity. The catalog returns runtime indices internally, but no code outside the catalog may hardcode or assume those indices. See [component-mod-architecture.md](component-mod-architecture.md) §5. Added Session 27.
- **Catalog ID strings at all interface boundaries**: All asset references at interface boundaries (wire protocol, save files, public APIs) must use **full catalog ID strings** in `"namespace:readable_name"` format (e.g., `"base:dark_combat"`, `"base:arena_felicity"`). net_hash u32 CRC32 compact references are **deprecated and must not be used anywhere**. Raw N64 array indices (bodynum, headnum, filenum, stagenum, texnum, animnum) must not cross boundaries. Integer indices are resolved from catalog only at the final handoff to legacy engine API. Added S90 (bot config transmission fix; asset reference audit ~180 sites, 20 patterns). Upgraded to no-net_hash mandate 2026-04-02.
- **Asset references use catalog ID strings everywhere**: ALL asset references — bodies, heads, stages, weapons — use the catalog ID string format `"namespace:readable_name"`. This applies to: wire protocol messages (CLC_LOBBY_START, SVC_LOBBY_STATE, SVC_STAGE_START, etc.); save files (scenarios, config); runtime structs (matchslot, match config). No exceptions. Added 2026-04-02.
- **matchslot/match config: catalog ID strings are primary identity**: The `matchslot` struct stores `body_id` and `head_id` as catalog ID strings. `mpbodynum`/`mpheadnum`/`bodynum`/`headnum` integers are derived values resolved from the catalog ONLY when a legacy engine API requires them. Do NOT call `catalogBodynumToMpBodyIdx` or `catalogHeadnumToMpHeadIdx` on the match config path — these are catalog internals. Added 2026-04-02.
- **Catalog registers ALL assets (unlock is a separate layer)**: The catalog must register every asset that exists, including SP-only heads and bodies. Whether an asset is available to a player in MP is a gameplay/unlock decision — it is NOT a catalog registration decision. The head registration loop in `assetcatalog_base.c` must iterate ALL entries in `g_HeadsAndBodies[]`, not just MP-reachable ones. The MP character selection UI checks unlock status to filter what is shown; the catalog does not filter. SP-only characters unlock for MP when: the player has beaten the mission where that character appears, OR the Unlock All cheat is active. Added 2026-04-02.
- **ImGui is the sole menu system**: ImGui menus (`pdgui_menu_*.cpp`) are the ONLY menu system. The legacy N64 `menuPush`/`menuPop` stack (`menumgr.c`) is deprecated and will be stripped entirely. ALL menu work — bug fixes, state management, Esc handling, duplicate prevention, theme/tint, mouse capture — must target the ImGui layer. Legacy menu textures/assets may be harvested for ImGui replacements. Menu stack architecture: push/pop with duplicate rejection, input context per menu. Theme = persistent visual identity (user-chosen, draws first). Tint = transient overlay (composites on top of theme, cleared on pop). Added 2026-04-02.
- **Mouse capture is driven by menu stack position**: Each ImGui menu declares its input context (cursor mode, capture mode, allowed bindings). Pushing a menu activates its input context; popping deactivates it. Mouse capture is an automatic consequence of menu stack position — not manual calls. Game has capture, menus release; transitions are atomic with menu push/pop. No menu may call raw capture/release functions directly. Added 2026-04-02.
- **No raw IP in any UI surface**: Players join via 4-word sentence connect codes only. No UI element may display or accept a raw IP address. Connect codes are the sole mechanism for sharing/entering server addresses. The IP is resolved internally and never shown. `g_NetLastJoinAddr` and `g_NetRecentServers` store raw IPs internally — must never be exposed in UI. Future server history must encode stored IPs back to connect codes for display. Added Session 49.
- **Connect code byte order**: `connectCodeEncode()/connectCodeDecode()` use host byte order (little-endian on Windows), NOT network byte order despite the header comment. Both sides use the same convention so round-trips are consistent. Do not apply `htonl()` before passing to these functions. Added Session 49.
- **Server is not a player**: Dedicated server sets `g_NetLocalClient = NULL` and `g_NetNumClients = 0` at startup. Slot 0 is free for real players. All code paths that dereference `g_NetLocalClient` must have a NULL guard. Do not assume the server occupies any player slot. Added Session 50 (B-28 fix).
- **ROM/mod check skipped on dedicated server**: The ROM hash verification and mod check in `CLC_AUTH` is gated behind `!g_NetDedicated`. Dedicated servers have no valid ROM — the check must not run. Use `!g_NetDedicated` guards, not stub workarounds. Added Session 50 (B-27 fix).
- **Identity profile is authoritative name source**: On PC, `identityGetActiveProfile()->name` is the canonical player name. The legacy N64 config field (`g_PlayerConfigsArray[0].base.name`) is only consulted as a fallback when the identity name is empty. New code reading player names must prefer identity profile. Added Session 50 (B-26 fix).
- **All builds are clean builds**: The "Clean Build" toggle was removed. Every build unconditionally deletes build directories before CMake configure. No incremental builds. This eliminates the class of CMake CACHE stale-value bugs (e.g., B-22 version baking failures). Added Session 50.
- **Rooms are demand-driven**: Rooms are created when players need them, not pre-allocated. Zero players = zero rooms. The permanent room 0 created by `roomsInit()` is a transitional artifact — R-2 removes it. Added Session 51.

---

## Removed Constraints

These constraints have been explicitly abandoned. If a task involves working around one of these, **stop and propose the simpler approach**.

- **2026-03-01: N64 platform guards** — PC-only target. All 672 `PLATFORM_N64` guards stripped across 114+ files. Zero references remain. (Phase D1 complete)
- **2026-03-01: 4MB memory mode** — `IS4MB()` is compile-time `0`, `IS8MB()` is compile-time `1`. Compiler eliminates all 4MB branches. `g_Is4Mb` variable removed.
- **2026-03-01: N64 dead code** — All N64 assembly (.s), ultra/os, ultra/libc removed. Only ultra/audio, ultra/gu, and 4 ultra/io vi mode files remain.
- **2026-03-05: N64 micro-optimization** — Modern x86_64 HW. Prefer correctness over cycle-counting. Hand-rolled sorts, manual bit-packing, fixed-point math — replace with standard approaches.
- **2026-03-10: Host-based multiplayer** — Dedicated-server-only model adopted (Phase D9). Server runs headless or with ImGui GUI.
- **2026-03-12: N64 collision workarounds** — Legacy cdTestVolume/cdFindGroundInfoAtCyl hacks replaced by capsule sweep system (capsule.c). Use proper geometric solutions.
- **2026-03-15: 4-player bot limit** — Original N64 supported ~8 bots max. PC port supports up to 31 bots + 1 player = 32 total (participant pool). `MAX_BOTS=32` (S45, was 24), `chrslots u64` (S45, was u32). Pool sizes expanded (Session 17): NUMTYPE1=70, NUMTYPE2=50, NUMTYPE3=48, NUMSPARE=80, weapons=100, hats=20, ammo=40, debris=30, projectiles=200, embedments=160.
- **2026-03-18: N64 body/head restriction in MP** — `mpGetNumBodies` unrestricted. Full 63+ character roster available in network play.
- **2026-03-20: `--log` CLI flag requirement** — Logging is now unconditional (always on). Log filename depends on mode: pd-server.log, pd-host.log, pd-client.log.
- **2026-03-23: Shared memory pools for mods** — N64-era pre-allocated pools (modconfig.txt `alloc` values) replaced by dynamic `malloc`-based allocation. Each component manages its own memory. Advisory `hint_memory` field in `.ini` for UI display only.
- **2026-03-23: Monolithic mod structure** — Mods are no longer single directories loaded/unloaded as a unit. Replaced by component-based architecture where each asset (map, character, skin, etc.) is an independent folder with its own `.ini` manifest. See [component-mod-architecture.md](component-mod-architecture.md).
- **2026-03-23: Numeric asset lookups** — ROM addresses, table indices, and array offsets for asset identity are abandoned. All asset references go through the string-keyed Asset Catalog. This eliminates the root cause of B-13 (scale), B-17 (stage ID mismatch), and the entire class of index-shift bugs when mods change.
- **2026-03-24: g_ModNum integer mod selector** — `g_ModNum` (0=Normal, 1=GEX, 2=Kakariko, 3=DarkNoon, 4=GF64) fully removed. MOD_NORMAL/MOD_GEX/etc. constants removed. Asset Catalog is sole mod authority. (D3R-11, S45)
- **2026-03-24: modconfig.txt parsing** — `modConfigLoad()` and all parsing logic removed. Mods require `mod.json`. Dirs without `mod.json` are skipped by `modmgrScanDirectory`. (D3R-11, S45)
- **2026-03-24: Shadow asset arrays** — `g_ModBodies[]`, `g_ModHeads[]`, `g_ModArenas[]` static shadow arrays in modmgr.c removed. Catalog-backed caches (`s_CatalogBodies/Heads/Arenas`) are the only backing store. (D3R-11, S45)
- **2026-03-24: fileSlots 2D array** — `fileSlots[5][ROMDATA_MAX_FILES]` (per-mod file slot banks) flattened to `fileSlots[ROMDATA_MAX_FILES]`. Only slot 0 was ever active. (D3R-11, S45)
- **2026-04-02: net_hash wire format** — u32 CRC32 compact references (`net_hash`) are fully deprecated. All wire messages, save files, and APIs must use full catalog ID strings. The hash was an optimization that creates indirection away from actual asset identity and makes debugging harder. Do not introduce any new net_hash fields.
- **2026-04-02: Legacy N64 menu system** — `menuPush`/`menuPop` stack in `menumgr.c` is deprecated. ImGui menus (`pdgui_menu_*.cpp`) are the sole menu system. The legacy stack will be stripped entirely; do not add new entries to it or rely on its state.
- **2026-04-02: Integer-native match config** — `matchslot` struct no longer treats `mpbodynum`/`mpheadnum` as primary identity. Catalog ID strings (`body_id`/`head_id`) are primary. Integer indices are resolved from catalog only at legacy engine handoff. Do not pass raw integer indices through match config APIs.

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
