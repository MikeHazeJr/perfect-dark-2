# Player Count Constants Audit

> Created: Session 69 (2026-03-28)
> Back to [index](README.md)

This document catalogs every player-count-related constant in the codebase, identifies
inconsistencies, records what was fixed, and provides guidelines for future code.

---

## Constant Hierarchy (authoritative)

All values live in `src/include/constants.h` unless otherwise noted.

| Constant | Value | Meaning |
|----------|-------|---------|
| `PARTICIPANT_DEFAULT_CAPACITY` | 32 | **Root constant.** Total match participant pool (players + bots combined). All other limits derive from this. |
| `MAX_LOCAL_PLAYERS` | 4 | Max splitscreen players per machine. Engine/hardware limit. Does not change. |
| `MAX_PLAYERS` | 8 | Max human player slots in a match (across all clients). Distinct from local. |
| `MAX_BOTS` | `PARTICIPANT_DEFAULT_CAPACITY` = 32 | Max bots in an all-bot match. In practice = `PARTICIPANT_DEFAULT_CAPACITY - actual_player_count`. |
| `MAX_MPCHRS` | `MAX_PLAYERS + MAX_BOTS` = 40 | Total characters (humans + bots). Arrays sized to this. |
| `MAX_MPPLAYERCONFIGS` | `MAX_PLAYERS + MAX_COOPCHRS` = 16 | Size of `g_PlayerConfigsArray`. Includes scratch slots for co-op swap. |
| `BOT_SLOT_OFFSET` | `MAX_PLAYERS` = 8 | chrslots bitmask: bot bits start here. Players = bits 0-7, bots = bits 8-39. |
| `NET_MAX_CLIENTS` | 32 | Max simultaneous ENet connections. Independent of match slots. Lives in `port/include/net/net.h`. |
| `MATCH_MAX_SLOTS` | `PARTICIPANT_DEFAULT_CAPACITY` = 32 | Total match slots available in the lobby UI. Defined locally in `matchsetup.c` (C); manually mirrored as `32` in C++ GUI files (can't include constants.h there — types.h bool conflict). |
| `LOBBY_MAX_PLAYERS` | 32 | Lobby state array size. Lives in `port/include/net/netlobby.h`. Matches `NET_MAX_CLIENTS`. |
| `HUB_MAX_ROOMS` | 4 | Max concurrent rooms on a server. `port/include/room.h`. |
| `HUB_MAX_CLIENTS` | 32 | Hub client tracking. `port/include/room.h`. Matches `NET_MAX_CLIENTS`. (Fixed S69.) |
| `SAVE_MAX_BOTS` | 32 | Max bots saved in MP setup JSON. `port/include/savefile.h`. Matches `MAX_BOTS`. (Fixed S69.) |
| `SAVE_MAX_PLAYERSLOTS` | 8 | Player slots in saved MP setup. Matches `MAX_PLAYERS`. |
| `g_NetMaxClients` | runtime | Dynamic cap set by `hubSetMaxSlots()`. Starts at `NET_MAX_CLIENTS`, lowered by network benchmark. All client loops should use `g_NetMaxClients`, not `NET_MAX_CLIENTS`, where runtime cap matters. |

---

## Key Design Notes

### Why `MAX_PLAYERS = 8` but `MATCH_MAX_SLOTS = 32`

These are different things:
- `MAX_PLAYERS = 8` — how many human player slots exist in the game engine's player array (`g_Vars.players[8]`, `g_PlayerConfigsArray[]`, etc.)
- `MATCH_MAX_SLOTS = 32` — how many total slots (human + bot) the lobby UI can configure

In a match with 2 humans and 29 bots: `MAX_PLAYERS` covers the humans, `MAX_BOTS` covers the bots. The total is bounded by `MATCH_MAX_SLOTS = PARTICIPANT_DEFAULT_CAPACITY = 32`.

### Why C++ files can't use `constants.h`

`types.h` defines `bool` as `s32`, which conflicts with C++'s native `bool`. Since `constants.h` includes `types.h` (via game headers), all C++ port files must declare constants they need locally. The pattern is a local `#define` with a comment pointing to the canonical source.

### Why `g_PlayerConfigsArray[MAX_PLAYERS]` as an index is intentional

`g_PlayerConfigsArray` is sized `[MAX_MPPLAYERCONFIGS]` = 16. Index `MAX_PLAYERS` (= 8) is the first scratch slot used in co-op player swapping (`mplayer.c:562`). This is **not** an off-by-one bug.

---

## Instances Found (full catalog)

### ✅ Correctly using canonical constants

| File | Lines | What |
|------|-------|------|
| `src/include/constants.h` | 21-55 | Canonical definitions of all game constants |
| `src/include/types.h` | 77,144-146,221,227 etc. | Arrays sized to `MAX_PLAYERS`, `MAX_MPCHRS`, `MAX_BOTS` |
| `src/include/bss.h` | 131,135,167,173,175,194,279-283,288 | Global arrays sized to canonical constants |
| `port/include/net/net.h` | 12 | `NET_MAX_CLIENTS 32` — canonical net constant |
| `port/include/net/netlobby.h` | 14 | `LOBBY_MAX_PLAYERS 32` — correct, matches NET_MAX_CLIENTS |
| `port/src/net/matchsetup.c` | 72 | `MATCH_MAX_SLOTS PARTICIPANT_DEFAULT_CAPACITY` — correct |
| `port/src/net/matchsetup.c` | 257,276 | `MAX_PLAYERS`, `MAX_BOTS` guards — correct |
| `port/src/net/net.c` | all | Uses `NET_MAX_CLIENTS` and `g_NetMaxClients` correctly |
| `port/src/net/netmsg.c` | all | Uses canonical constants throughout |
| `src/game/mplayer/mplayer.c` | 45-50,145 | Arrays sized to `MAX_MPCHRS`, `MAX_BOTS`, `MAX_PLAYERS` |
| `src/game/bot.c` | 43 | `g_MpBotChrPtrs[MAX_BOTS]` |

### ✅ Local redefinitions — correct value, documented link (C++ constraint)

These files cannot include `constants.h` due to the `types.h` bool conflict. They redeclare
needed constants locally. Values are correct; comments link to canonical source.

| File | Lines | Constants | Status |
|------|-------|-----------|--------|
| `port/fast3d/pdgui_menu_matchsetup.cpp` | 64-74 | `MAX_PLAYERS 8`, `MAX_BOTS 32`, `MATCH_MAX_SLOTS 32` | Comments added S69 |
| `port/fast3d/pdgui_menu_room.cpp` | 88-112 | `MAX_PLAYERS 8`, `MATCH_MAX_SLOTS 32` | Comments added S69 |

### 🔧 Fixed in Session 69

| File | Line | Was | Now | Impact |
|------|------|-----|-----|--------|
| `port/fast3d/pdgui_lobby.cpp` | 52 | `NET_MAX_CLIENTS 8` | `32` | Lobby UI was capped at 8 clients in any logic using this constant |
| `port/fast3d/pdgui_menu_network.cpp` | 40 | `NET_MAX_CLIENTS 8` | `32` | Network menu capped at 8 |
| `port/fast3d/pdgui_menu_pausemenu.cpp` | 65-67 | `MAX_BOTS_PM 24`, `MAX_MPCHRS_PM 32` | `MAX_BOTS_PM 32`, `MAX_MPCHRS_PM 40` | **CRITICAL**: `mpchrconfig_pm.killcounts[]` was 8 entries short (32 vs 40), making `numdeaths`/`numpoints`/`unk40` fields read from wrong memory offsets (+16 bytes off). Pause menu post-game scores were reading garbage. |
| `port/include/room.h` | 33 | `HUB_MAX_CLIENTS 8` | `32` | Room system's client-tracking array was undersized |
| `port/include/savefile.h` | 47 | `SAVE_MAX_BOTS 24` | `32` | MP setup save could only store 24 bots; configs for bots 25-32 were silently dropped on save |

### ❌ Not player counts — confirmed safe to ignore

These loops/values look like player counts but are not:

| File | Lines | Value | What it actually is |
|------|-------|-------|---------------------|
| `src/game/camdraw.c` | 428-533, 2825-3051 | 4, 8 | Rendering viewport geometry, shadow buffer passes, N64 memory structures |
| `src/game/chraction.c` | 15150, 15209 | 8 | 8 angular directions (octants) for chr spawn position search |
| `port/src/net/netmsg.c` | 186, 2018 | 8 | `prop->rooms[8]` array size (rooms per prop) — not player count |
| `src/game/menugfx.c` | 1453, 1478 | 8 | 8 angular divisions for radial HUD rendering (`i * 2π / 8`) |
| `src/game/chr.c` | 3137 | 8 | Chr attribute or weapon slot index |
| `src/game/sky.c` | 1728, 1733, 1809, 2264 | 4, 8 | Sky segment count |
| `src/game/weather.c` | 3129 | 8 | Weather geometry segments |
| `port/src/mixer.c` | various | 4, 8 | Audio channel counts |
| `port/src/sha256.c` | 177 | 8 | SHA-256 has 8 state words |
| `port/src/phonetic.c` | 58, 125 | 8 | Phonetic word encoding (bits) |
| `port/src/phonetic.c` | 95 | 4 | 4-word connect code |
| `port/fast3d/gfx_pc.cpp` | various | 4 | RGBA channels, matrix rows |
| `port/src/savefile.c` | 544 | 8 | `teamnames[8]` (MAX_TEAMS=8), not players |
| `port/include/savefile.h` | 184 | 8 | Weapon slots (`weapons[8]`), not players |
| `port/include/external/minimp3.h` | various | 4, 8 | Audio decoding math |

---

## Deferred (known issues, not fixed this session)

| Item | Location | Issue | Notes |
|------|----------|-------|-------|
| `MATCH_MAX_SLOTS` shared header | C++ files | Local redefinitions in C++ files can't use `PARTICIPANT_DEFAULT_CAPACITY` directly | Would require a new `port/include/net/matchconfig.h` that avoids the types.h conflict. Medium priority — values are correct, just not auto-synced. |
| Network benchmark → dynamic cap | tasks-current.md backlog | `hubSetMaxSlots()` not yet called on server start | When implemented, all loops that use `NET_MAX_CLIENTS` directly (instead of `g_NetMaxClients`) will need review. Use `g_NetMaxClients` for runtime-bounded loops. |
| `port/src/net/netmenu.c:44` | `g_NetMenuMaxPlayers = NET_MAX_CLIENTS` | Currently initializes to 32; exposed to old menu UI which may assume smaller counts | Low priority — old menu superseded by ImGui UI |

---

## Guidelines for Future Code

1. **New player loops**: Use `g_NetMaxClients` (runtime cap), not `NET_MAX_CLIENTS` (compile-time ceiling).

2. **New arrays sized to player count**:
   - Per local player: `[MAX_LOCAL_PLAYERS]` (= 4)
   - Per match human player: `[MAX_PLAYERS]` (= 8)
   - Per bot: `[MAX_BOTS]` (= 32)
   - Per any match character: `[MAX_MPCHRS]` (= 40)
   - Per match slot (player or bot, for lobby UI): `[MATCH_MAX_SLOTS]` (= 32)

3. **C++ files**: If you need `MAX_PLAYERS` / `MAX_BOTS` / `MATCH_MAX_SLOTS` in a `.cpp` file:
   - Declare locally as `#define` with a comment: `/* = MAX_BOTS in src/include/constants.h */`
   - Do NOT attempt to include `constants.h` — it pulls `types.h` which breaks C++ `bool`
   - When changing `PARTICIPANT_DEFAULT_CAPACITY` in constants.h: grep all .cpp files for the manually-mirrored constants and update them too

4. **When `PARTICIPANT_DEFAULT_CAPACITY` changes**: Update these files manually:
   - `port/fast3d/pdgui_menu_matchsetup.cpp` — `MATCH_MAX_SLOTS`, `MAX_BOTS`
   - `port/fast3d/pdgui_menu_room.cpp` — `MATCH_MAX_SLOTS`
   - `port/fast3d/pdgui_menu_pausemenu.cpp` — `MAX_BOTS_PM`
   - `port/include/savefile.h` — `SAVE_MAX_BOTS`
   - `port/include/room.h` — `HUB_MAX_CLIENTS` (mirrors `NET_MAX_CLIENTS`)
   - `port/include/net/net.h` — `NET_MAX_CLIENTS` (may or may not need to change)
