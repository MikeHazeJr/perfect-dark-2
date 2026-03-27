# Session Log (Active)

> Recent sessions only. Archives: [1-6](sessions-01-06.md) . [7-13](sessions-07-13.md) . [14-21](sessions-14-21.md) . [22-46](sessions-22-46.md)
> Back to [index](README.md)

## Session 56 — 2026-03-27

**Focus**: Audit and fix 5 lobby issues found in Mike's playtest of the S55 build

### What Was Done

**Issue 1 & 5 — Client lobby empty + no leader** (`port/src/net/netlobby.c`):
- Root cause: `lobbyUpdate()` skip guard `if (cl == g_NetLocalClient)` ran on clients too. After SVC_AUTH, `g_NetLocalClient = &g_NetClients[id]`, so the client's own slot was being skipped → 0 players found → "Waiting for players..." and `lobbyIsLocalLeader()` always returned 0.
- Fix: Changed guard to `if (g_NetMode != NETMODE_CLIENT && cl == g_NetLocalClient)` — clients always include their own slot. With the slot visible, eager leader election fires immediately and `lobbyIsLocalLeader()` returns 1 for the first connected client. (B-32)

**Issue 2 — Player name shows "Player 1" not agent name** (`port/src/net/netmsg.c`):
- Root cause: `netmsgClcSettingsWrite()` sent `g_NetLocalClient->settings.name` directly without the identity override check that `netmsgClcAuthWrite()` had. CLC_SETTINGS is processed after CLC_AUTH on the server and overwrites the correct identity name with the stale/legacy `settings.name` from `netClientReadConfig()`.
- Fix: Added identity profile check to `netmsgClcSettingsWrite()` — same pattern as `netmsgClcAuthWrite()`. Also syncs `settings.name` so future sends are consistent. (B-33)

**Issue 3 — Server SDL window title shows raw IP** (`port/src/server_main.c`, `port/src/video.c`):
- Two locations, two fixes:
  - `server_main.c`: Added `#include "connectcode.h"`. Parse `sscanf` into u32, call `connectCodeEncode()`. Show connect code if available, else fall back to port number. (B-35)
  - `video.c`: Same connect code logic (inline `extern` declaration since video.c doesn't include `connectcode.h`). (B-29 secondary, same session)

**Issue 4 — Server GUI shows "0/32 connected"** (`port/src/server_main.c`):
- Root cause: `displayClients = g_NetNumClients > 0 ? g_NetNumClients - 1 : 0` — pre-B-28 compensation that subtracts 1 for the server's own slot. After B-28, dedicated server has `g_NetLocalClient = NULL` and `g_NetNumClients` counts only real players. `1 - 1 = 0` with one player connected. `server_gui.cpp` was already corrected; `server_main.c` was not.
- Fix: `g_NetDedicated ? g_NetNumClients : (g_NetNumClients > 0 ? g_NetNumClients - 1 : 0)`. (B-34)

**SVC_LOBBY_LEADER broadcast after auth** (`port/src/net/netmsg.c`):
- Added: after successful `netmsgClcAuthRead`, call `lobbyUpdate()` then broadcast `SVC_LOBBY_LEADER` to all CLSTATE_LOBBY+ clients. Ensures new joiners and existing players all get the authoritative leader assignment from the server, not just client-side inference.

**Build verified**: All 4 modified files compile clean in both client and server headless builds.

### Decisions Made
- SVC_LOBBY_LEADER broadcast is the canonical source of truth for leader identity; client-side inference in `lobbyUpdate()` serves as a fallback for single-player/offline mode.
- `video.c` uses inline `extern` for `connectCodeEncode` rather than adding a new `#include` — avoids pulling in unnecessary headers in a large file.

### Bugs Fixed
- B-32: `lobbyUpdate()` client skip guard
- B-33: CLC_SETTINGS name override
- B-34: `server_main.c` player count off-by-one (B-28 missed location)
- B-35: `server_main.c` raw IP in SDL window title (B-29 missed location)

### Next Steps
- Playtest: connect one client, verify lobby shows player name (not "Player 1"), client sees themselves as leader, server title shows connect code and correct count.
- Playtest: two clients, verify leader broadcast reaches both and the non-first-joined client is not shown as leader.

---

## Session 55 — 2026-03-27

**Focus**: Harden lobby leader assignment + room display fixes (follow-up to S54)

### What Was Done

**Eager leader assignment in lobbyUpdate()** (`port/src/net/netlobby.c`):
- Added in-loop eager assignment: when `g_Lobby.leaderSlot == 0xFF` and the first CLSTATE_LOBBY+ client is seen, assign them immediately rather than waiting for the post-loop election block.
- Closes a same-frame race: if CLC_AUTH and CLC_LOBBY_START arrive in the same ENet batch, CLC_LOBBY_START is processed before the post-loop election has any chance to run.

**lobbyUpdate() refresh before leader validation** (`port/src/net/netmsg.c`):
- `netmsgClcLobbyStartRead()` now calls `lobbyUpdate()` before checking `leaderSlot`.
- Added fallback: if `leaderSlot == 0xFF` (still unset after refresh), scan `g_NetClients` for first CLSTATE_LOBBY+ client and accept if it matches `srccl`.
- Better rejection log: includes `leaderSlot` value for debugging.

**Empty room display fix** (`port/fast3d/pdgui_menu_lobby.cpp`):
- Room sidebar now skips rooms with `client_count == 0` — prevents permanent "Lounge" (Room 0) from appearing before any players join.
- Added `roomsShown` counter; "No active rooms" shows when `roomsShown == 0`.

### Decisions Made
- These are belt-and-suspenders hardening on top of S54's fixes; the root causes were already addressed but edge cases remained.
- Not removing Room 0 creation yet — hub.c still depends on roomGetById(0) for state sync in hubTick().

### Next Steps
- Build and playtest the 2-player Combat Sim flow end-to-end.
- If match starts but players don't spawn, check g_SpawnPoints (B-19 partial fix in S54).

---

## Session 54 — 2026-03-27

**Focus**: Full implementation to get two players into a working Combat Simulator match

### What Was Done

**lobbyUpdate() B-28 regression fixed** (`port/src/net/netlobby.c`):
- `i == 0` skip guard replaced with `cl == g_NetLocalClient` — which is `NULL` on dedicated servers, so no slot is ever skipped. First real player (slot 0) now appears in `g_Lobby.players[]`.
- Off-by-one: `i <= NET_MAX_CLIENTS` → `i < NET_MAX_CLIENTS`.
- Root cause: B-28 (S52) set `g_NetLocalClient = NULL` for dedicated servers but didn't update lobbyUpdate's hardcoded slot-0 skip. Leader validation in `netmsgClcLobbyStartRead()` always failed because the leader (slot 0) was invisible.

**Duplicate Room 0 display fixed** (`port/src/room.c`):
- Added `if (!s_Initialised) return 0/NULL;` guards to `roomGetActiveCount()` and `roomGetByIndex()`.
- Root cause: `s_Rooms[]` is a C static array zero-initialized to `ROOM_STATE_LOBBY=0`, so all 4 slots appeared "active" on the client (which never calls `hubInit()`).

**g_MpSetup configured for Combat Sim** (`port/src/net/netmsg.c`):
- `netmsgClcLobbyStartRead()` now sets `g_MpSetup.stagenum`, `scenario=0` (MPSCENARIO_COMBAT), `timelimit=0`, `chrslots` with bits 0..n-1 for n connected players.
- Assigns sequential `playernum` values to each connected client before calling `netServerStageStart()`.
- Without this, `SVC_STAGE_START` broadcast `chrslots=0` and `playernum=0` for everyone, so `mpStartMatch()` never spawned players.

**Off-by-one in netServerStageStart()** (`port/src/net/net.c`):
- Two loops `i <= NET_MAX_CLIENTS` → `i < NET_MAX_CLIENTS`.

**Simulant settings in lobby UI** (`port/fast3d/pdgui_menu_lobby.cpp`, `port/fast3d/pdgui_bridge.c`, `port/include/net/netmsg.h`, `port/src/net/netmsg.c`):
- CLC_LOBBY_START payload extended: `gamemode, stagenum, difficulty, numSims, simType` (added 2 bytes).
- Server reads numSims/simType, populates `g_BotConfigsArray[]` and bits 8+ of `g_MpSetup.chrslots`.
- Lobby UI: arena selector (20 maps), simulant count slider (0-8), difficulty dropdown (Meat/Easy/Normal/Hard/Perfect/Dark).
- `netLobbyRequestStartWithSims()` bridge function added; original `netLobbyRequestStart()` wraps it with zeros.

**Spawn point fallback for MP maps without INTROCMD_SPAWN** (`src/game/playerreset.c`):
- After intro cmd loop, if `g_NumSpawnPoints == 0` and `g_NetMode != NETMODE_NONE`, scans `g_PadsFile` for pads with valid room numbers and populates `g_SpawnPoints`.
- Fixes B-19 (bot spawn stacking) for mod stages and MP maps without proper setup sequences.
- Added `#include "net/net.h"` to playerreset.c.

### Decisions Made
- CLC_LOBBY_START protocol extension is backward-compatible within a single session (client/server always built together). No protocol version bump needed yet.
- Spawn fallback uses `g_NetMode != NETMODE_NONE` guard so solo missions are unaffected.
- Arena selector uses hardcoded stage numbers (0x1f..0x32) matching known PD MP maps. No stage table dependency.

### Bugs Fixed
- **B-28 regression in lobbyUpdate()** (unlabeled): `i == 0` skip broke leader detection after B-28.
- **B-19** (partial): spawn stacking reduced by pre-populating g_SpawnPoints from pad data.

### Next Steps
- Build and run end-to-end 2-player test: start dedicated server, both clients connect, leader hits Combat Simulator button.
- Verify `mpStartMatch()` fires on both clients with correct playernum and chrslots.
- R-2: Room lifecycle expansion if first match test passes.

---

## Session 53 — 2026-03-26

**Focus**: Two bugs blocking clients from reaching lobby after connecting to dedicated server

### What Was Done

**B-31 FIXED — SVC_AUTH malformed on client** (`port/src/net/netmsg.c`):
- Root cause: `netmsgSvcAuthRead` had guard `|| id == 0` that was correct pre-B-28 (slot 0 = server's own local client, never assigned to remote clients). After B-28 (S52), dedicated servers start slot search at `i=0`, so the first real client legitimately gets `g_NetClients[0]`, making `authcl - g_NetClients = 0`. The old guard rejected this valid ID.
- Fix: removed `|| id == 0` from the malformed-message check in `netmsgSvcAuthRead`. Applied to both main repo and worktree.
- Secondary fix: `netmsgClcAuthRead` (server side) previously called `netDistribServerSendCatalogInfo` BEFORE sending `SVC_AUTH`. Client was in `CLSTATE_AUTH` when catalog info arrived, which is incorrect ordering. Reordered so `SVC_AUTH` is sent first (client transitions to `CLSTATE_LOBBY`), then catalog info follows. Applied to both repos.

**B-26 fully fixed — Player name sends identity profile name** (`port/src/net/netmsg.c`, `port/src/net/net.c`):
- S49 fix was incomplete: identity was only used as empty-name fallback. "Player 1" (the N64 default) is non-empty, so identity was never consulted.
- Fix 1: `netClientReadConfig()` in `net.c` — identity profile is now the PRIMARY source; legacy N64 config is fallback only. (Main repo already had this; applied to worktree.)
- Fix 2: `netmsgClcAuthWrite()` in `netmsg.c` — directly reads `identityGetActiveProfile()->name` when available, so the wire packet uses the profile name regardless of what's in `settings.name`. Applied to both repos.
- Added `#include "identity.h"` to `netmsg.c`.

### Decisions Made
- `id == 0` guard removal is correct and safe: `NET_NULL_CLIENT = 0xFF` remains the "no client" sentinel. Slot 0 being valid is the correct post-B-28 state.
- Identity profile name is authoritative on PC; legacy N64 config name is a fallback only.

### Next Steps
- Build and run end-to-end join test to confirm client reaches lobby (B-31 and B-26 are testable together)
- R-2: Room lifecycle after lobby is confirmed working

---

## Session 52 — 2026-03-26

**Focus**: Phase R-1 implementation — hub slot pool API, dedicated server slot fix, IP scrubbing

### What Was Done

**R-1a: Hub slot pool API implemented** (`port/src/hub.c`):
- Added `#include "net/net.h"` so hub.c can read `g_NetMaxClients` / `g_NetNumClients`
- Implemented all 4 stubs declared in `hub.h` but missing in `hub.c`:
  - `hubGetMaxSlots()` → returns `g_NetMaxClients`
  - `hubSetMaxSlots(s32)` → clamps to [1, NET_MAX_CLIENTS], writes `g_NetMaxClients`
  - `hubGetUsedSlots()` → returns `g_NetNumClients`
  - `hubGetFreeSlots()` → returns `max - used`, clamped to 0

**R-1b: Dedicated server no longer occupies slot 0** (`port/src/net/net.c`, B-28 FIXED):
- `netStartServer()`: when `g_NetDedicated`, sets `g_NetLocalClient = NULL` and `g_NetNumClients = 0` (slot 0 stays free). When not dedicated, existing listen-server path unchanged.
- `netServerEvConnect()` slot search: changed from always starting at `i=1` to `i = g_NetDedicated ? 0 : 1`, so slot 0 is assignable to real players on dedicated servers.
- NULL guards added to `netServerStageStart()` for lines that unconditionally wrote `g_NetLocalClient->state` and called `netClientReadConfig(g_NetLocalClient, 0)` (two sites).
- NULL guard added to `netServerStageEnd()` for `g_NetLocalClient->state = CLSTATE_LOBBY`.
- Verified `netServerEvConnect()` line 942 already had NULL guard: `const bool ingame = (g_NetLocalClient && ...)`.

**R-1c: Raw IP removed from server GUI status bar** (`port/fast3d/server_gui.cpp`, B-29 FIXED):
- Line 695: Replaced `ImGui::TextColored(..., "%s:%u", ip, g_NetServerPort)` with `"Port %u"` (port only, no IP).
- Line 707: Fixed `displayClients` — dedicated servers now show `g_NetNumClients` directly (no `-1` compensation needed since slot 0 is no longer occupied by the server).

**R-1d: IP-bearing log lines replaced** (`port/src/net/net.c`, B-30 FIXED):
- `netServerEvConnect()`: Removed `addrstr = netFormatPeerAddr(peer)`. Connection event logs show "incoming connection", rejection logs show reason only (no IP).
- `netServerEvDisconnect()`: `"disconnect event from %s"` → `"disconnect event from client %u"` using `cl->id`.
- Spurious-peer logs (no attached client): replaced `netFormatPeerAddr(ev.peer)` with generic "unknown peer" messages.
- UPnP IP logs in `netupnp.c` left intact (internal infrastructure — not user-facing).

### Decisions Made
- `g_NetNumClients = 0` set when dedicated (not left at 1 from `netClientResetAll()`), so player count is accurate from the start.
- `hubSetMaxSlots` clamps to `[1, NET_MAX_CLIENTS]` — no silent negative or overflow.
- UPnP log lines (`UPNP: [thread] External IP: ...`) are classified as internal infrastructure and left as-is per R-1 design.

### Next Steps
- Build dedicated server target and run end-to-end join test (J-1) to verify R-1 changes
- Confirm slot 0 is now available (expect `Players: 1/32` with one client vs old `1/32` that was really `0/31`)
- R-2: Room lifecycle (expand HUB_MAX_ROOMS/CLIENTS, add `leader_client_id`, demand-driven rooms)

---

## Session 51 — 2026-03-26

**Focus**: Room architecture plan — code audit, struct corrections, message IDs, phase file refs

### What Was Done

**Code audit against draft `context/room-architecture-plan.md`** (created S50):

**Key findings from reading hub.h/hub.c, room.h/room.c, net.h, net.c, netmsg.h, server_gui.cpp, netlobby.c**:

1. **hub.h slot pool API not implemented**: `hubGetMaxSlots/SetMaxSlots/GetUsedSlots/GetFreeSlots` are declared but have no implementation in `hub.c`. Phase R-1 must add these.

2. **g_NetLocalClient = &g_NetClients[0] on server CONFIRMED**: `netStartServer()` lines 519-521 unconditionally claims slot 0 for the server. `lobbyUpdate()` already has a dedicated-server guard skipping slot 0, and `server_gui.cpp` compensates with `g_NetNumClients - 1`. Fix in R-1: set `g_NetLocalClient = NULL` when `g_NetDedicated`. (B-28)

3. **IP in server GUI status bar CONFIRMED**: `server_gui.cpp:695` shows raw `"%s:%u"` IP/port in gray below the connect code. Connect code display already exists (lines 689-693). Remove gray IP line. (B-29)

4. **IPs in log output CONFIRMED**: `netFormatClientAddr()` returns raw `"IP:port"` strings used in connection log calls. (B-30)

5. **hub_room_t struct**: `id` is `u8` (not `s32`). No `leader_client_id` field — needs to be added. `creator_client_id` exists. Types verified. Draft plan struct was close but types wrong.

6. **HUB_MAX_CLIENTS = 8** in room.h stale — `NET_MAX_CLIENTS` is 32. Must expand.

7. **HUB_MAX_ROOMS = 4** — needs expansion (plan: 16).

8. **Message ID ranges confirmed**: SVC free from `0x75`, CLC free from `0x0A`. Plan assigns SVC 0x75-0x77, CLC 0x0A-0x0F.

9. **roomsInit() creates room 0 permanently** — conflicts with demand-driven design. Phase R-2 removes this.

10. **Draft B-28 (player name)** = already B-26 (fixed). Removed from plan. Bugs renumbered: B-28 = slot 0, B-29 = server GUI IP, B-30 = log IPs.

**Plan revised**: `context/room-architecture-plan.md` rewritten with all corrections, specific code locations, and phase-level file references.

**Context files updated**:
- `context/room-architecture-plan.md`: full code-verified rewrite
- `context/tasks-current.md`: R-1 through R-5 added to Active Work Tracks + Prioritized Next Up
- `context/bugs.md`: B-28, B-29, B-30 added (all OPEN, part of R-1)
- `context/session-log.md`: this entry
- `context/infrastructure.md`: SPF section updated with R-series
- `context/README.md`: last-updated bumped

### Decisions Made
- `room_id` on `struct netclient` uses `s32` with `-1` as sentinel (not 0 — room IDs are 0-based)
- `CLC_LOBBY_START (0x08)` remains for backward compat; `CLC_ROOM_START (0x0F)` is the new primary; deprecated in R-4 but not removed until tested
- B-28/29/30 grouped into Phase R-1 (no protocol change required for any of them)

### Next Steps
- R-1: Start with hub slot pool stubs + `g_NetLocalClient = NULL` fix in `net.c`
- Continue J-1: Build server target, verify end-to-end join flow

---

## Session 50 — 2026-03-26/27

**Focus**: Server crash fix (B-27, 9 fixes), multiplayer regressions, build system hardening, v0.0.7 release

### What Was Done

**B-27 FIXED — Dedicated server crash on first client connect (9 fixes, 6 files)**:

Nine separate bugs all in the server connect path, discovered via real cross-machine playtest:

1. **`g_RomName` type mismatch** (`port/src/server_stubs.c`): stub declared `char g_RomName[64]` but `port/include/versioninfo.h` declared `const char *g_RomName`. Fix: changed stub to `const char *g_RomName = "pd-server"`.

2. **ROM/mod check not gated on dedicated** (`port/src/net/net.c`): The ROM hash + mod check ran unconditionally in `CLC_AUTH`, rejecting all real clients connecting to a dedicated server (which has no valid ROM). Fix: wrapped behind `!g_NetDedicated` guard.

3. **`SVC_AUTH` rejecting `id == 0`** (`port/src/net/net.c`): Handler had `if (id == 0) reject`. Now that dedicated servers assign slot 0 to real players (not reserved for server), this guard wrongly rejected the first player. Fix: removed the `id == 0` check.

4. **Hardcoded `g_NetClients[0].state` assumption** (`port/src/net/netmsg.c`): `netmsgClcAuthRead()` unconditionally read `g_NetLocalClient->state` instead of the connecting client's state. Fix: use `cl->state` directly.

5. **NULL guard missing on `g_NetLocalClient`** (`port/src/net/netmsg.c`): Dedicated server has `g_NetLocalClient = NULL`; dereference in `netmsgClcAuthRead` crashed. Fix: NULL guard added.

6. **`ev.packet` NULL check missing** (`port/src/net/net.c`): ENet receive callback could deliver an event with `ev.packet = NULL`; crash on `enet_packet_destroy`. Fix: NULL check added.

7. **`LOBBY_MAX_PLAYERS = 8` mismatch** (`port/src/net/netlobby.c`): Lobby capacity was still 8 while `NET_MAX_CLIENTS` was 32. Fix: `LOBBY_MAX_PLAYERS` updated to 32.

8. **Stale `#define NET_MAX_CLIENTS 8`** (`port/fast3d/server_gui.cpp`): Server GUI had its own local define shadowing the updated value in `net.h`. Fix: removed local define.

9. **GUI ping/kick used loop index instead of `clientId`** (`port/fast3d/server_gui.cpp`): Server action commands (ping/kick) passed the iteration index `i` instead of `cl->id`. Fix: use `cl->id`.

**B-22 FIXED — Version not baking into exe (third report)**:
- Root cause: `Get-BuildSteps` in `devtools/dev-window.ps1` built cmake configure args with no `-DVERSION_SEM_*` flags
- CMake used its cached value (from prior run) or the hardcoded CACHE default — Dev Window version boxes had no effect
- Fix: added `Get-UiVersion` call inside `Get-BuildSteps`; appends `-DVERSION_SEM_MAJOR=X -DVERSION_SEM_MINOR=Y -DVERSION_SEM_PATCH=Z` to both Configure steps (client + server)

**B-23 FIXED — Quit Game button clipped on right edge**:
- Root cause: fixed `quitBtnW = 100 * scale` placed button's right edge at the ImGui clip boundary with no margin; "Confirm Quit" label also wider than 100px
- Fix in `port/fast3d/pdgui_menu_mainmenu.cpp`: width now `CalcTextSize("Confirm Quit").x + FramePadding*2`; position now `dialogW - WindowPadding.x - quitBtnW - 4*scale`; Cancel button cursor updated

**F8 hotswap badge removed from main menu** (`port/fast3d/pdgui_menu_mainmenu.cpp`):
- Removed the F8 indicator badge from the main menu corner. The toggle (F8 / R3) still works — badge was visual noise.

**Always-clean build enforced** (`devtools/dev-window.ps1`):
- "Clean Build" toggle removed from Dev Window GUI. Every build now unconditionally deletes build directories before configure.
- Rationale: stale CMake CACHE caused B-22 and an entire class of version-baking/config-drift bugs. Clean builds eliminate this class.

**Auto-commit version from UI boxes** (`devtools/dev-window.ps1`):
- Auto-commit message (triggered before release builds) now reads version from the Dev Window boxes, not from CMakeLists.txt defaults.
- Ensures the auto-commit label always matches the actual binary being built.

**Update tab — cross-session staged version persistence** (`port/src/updater.c`, `port/include/updater.h`):
- Downloads now write a `.update.ver` sidecar file alongside the staged binary.
- `updaterGetStagedVersion()` reads this sidecar on startup.
- "Switch" button now appears immediately on reopen without requiring a re-download.

**Update tab button sizing** (`port/fast3d/pdgui_menu_update.cpp`):
- Download/Rollback/Switch buttons now use `CalcTextSize`-based widths instead of fixed pixel values.
- Per-row layout: each version row gets its own Download/Rollback/Switch button inline.

**v0.0.7 released to GitHub**:
- Built and tested as v0.0.6, released as v0.0.7.
- Includes all changes from S27–S50 (component mod architecture, room system, connect codes, participant system, update tab, all multiplayer regression fixes from S49d).

### Decisions Made
- Version boxes in the Dev Window are the single source of truth for ALL builds (not just releases). `Get-BuildSteps` is the authoritative cmake path.
- All builds are clean builds. No toggle. No exceptions. Stale CMake CACHE is eliminated by design.
- ROM/mod check is skipped on dedicated server via `!g_NetDedicated`. No hack guards.
- `id == 0` is a valid player slot on dedicated servers. The SVC_AUTH guard that rejected slot 0 is gone.

### Next Steps
- SPF-2b: verify SPF-1 server build end-to-end (J-1)
- SPF-3a: lobby ImGui screen
- Wire remaining menus through menu manager
- Collision Phase 2 design (HIGH PRIORITY)

---

## Session 49b — 2026-03-26

**Focus**: SPF-3 lobby+join, catalog audit, plan docs, stats, connect codes, IP fallback, updater

### What Was Done

**SPF-2a Build Pass**: menumgr.h was missing `extern "C"` guards → undefined reference errors in C++ TUs. Fix applied (`5e55e62`). SPF-2a (menumgr.c/h, 100ms cooldown) now builds.

**Release Pipeline**: `-Nightly` flag added to release.ps1: nightly builds use `nightly-YYYY-MM-DD` tag. Fixed post-batch-addin path (Split-Path parent traversal).

**SPF-3 — Lobby + Join by Code** (commit `3b588c1`): `pdgui_menu_lobby.cpp` integrated hub.h/room.h — lobby shows server state, room list with color-coded states and player counts. `pdgui_menu_mainmenu.cpp`: new menu view 4 "Join by Code" with phonetic code input + decode via `phoneticDecode()` (falls back to direct IP). Wired through menu manager (MENU_JOIN push/pop).

**Asset Catalog Audit Phase 1** (commit `3b588c1`): Failure logging at all critical asset load points: `fileLoadToNew`, `modeldefLoad`, `bodyLoad`, `tilesReset`, setup pad loading, lang bank loading.

**New Plan Documents** (commit `636b404`): `context/catalog-loading-plan.md` (C-1–C-9 phases). `context/menu-replacement-plan.md` (240 legacy menus → 9 ImGui groups, Group 1 highest priority).

**Player Stats System**: New `port/include/playerstats.h` + `port/src/playerstats.c`. `statIncrement(key, amount)` — named counter system, JSON persistence.

**Connect Code System Rewrite**: Sentence-based codes ("fat vampire running to the park") replace phonetic syllables as primary connect method. 256 words per slot × 4 slots = 32-bit IPv4.

**HTTP Public IP Fallback**: `netGetPublicIP()` tries UPnP first, then `curl` → `api.ipify.org`. Result cached after first success.

**Updater Unified Tag Format**: `versionParseTag()` now handles `"v0.1.1"` (unified) in addition to `"client-v0.1.1"` (legacy).

### Decisions Made
- Sentence-based connect codes are primary (phonetic module remains for lobby display)
- Menu replacement: Group 1 (Solo Mission Flow, 11 menus) first
- Stats: named counters (not fixed schema) for forward compatibility

### Next Steps
- SPF-3 playtest: lobby rooms, join-by-code
- Catalog Phase C-1/C-2; Menu Replacement Group 1

---

## Session 49c — 2026-03-26

**Focus**: Join flow audit, S49 architecture documentation, context hardening

### What Was Done

**Context audit — S49 architectural decisions captured**: Sentence-based connect codes, menu replacement plan, rooms + slot allocation, asset catalog as single source of truth (C-1–C-9 phases), campaign as co-op, player stats, HTTP IP fallback, updater unified tag format.

**Join flow audit — `context/join-flow-plan.md` created**: Full end-to-end flow mapped: code input → decode → netStartClient → ENet → CLC_AUTH → SVC_AUTH_OK → CLSTATE_LOBBY → lobby UI → netLobbyRequestStart → match. Gaps found: room state not synced to clients (SVC_ROOM_LIST needed), server GUI missing connect code display, recent server history stubbed.

**Plan: J-1 verify end-to-end, J-2 server GUI code display, J-3 SVC_ROOM_LIST protocol, J-4 server history UI, J-5 lobby handoff polish.**

Context files updated: networking.md (protocol v21, HTTP IP fallback), update-system.md (unified tag format), constraints.md (no raw IP in UI), infrastructure.md, tasks-current.md.

### Decisions Made
- Recent server history MUST encode IPs to codes, not store raw IP
- Server GUI should display connect code (currently only in logs)

### Next Steps
- J-1: Build server target, verify end-to-end join → match flow
- J-2: Add connect code display to server_gui.cpp

---

## Session 49d — 2026-03-26

**Focus**: Cross-machine multiplayer bug fixes (3 regressions from real playtest)

### What Was Done

**B-24 (was B-22) — Connect code byte-order reversal (CRITICAL, FIXED)**: `pdgui_menu_mainmenu.cpp` extracted bytes MSB-first `(ip>>24, ip>>16, ip>>8, ip)` while encoder + all other decode callers use LSB-first `(ip, ip>>8, ip>>16, ip>>24)`. Fix: 3-line change to LSB-first extraction.

**B-25 (was B-23) — Server max clients hardcoded to 8 (FIXED)**: `NET_MAX_CLIENTS` was `MAX_PLAYERS` (=8). Fixed: `NET_MAX_CLIENTS 32` in `net.h`, independent of `MAX_PLAYERS`. `PDGUI_NET_MAX_CLIENTS 32` in debug menu.

**B-26 (was B-24) — Player name shows "Player1" (FIXED)**: `netClientReadConfig()` reads from legacy N64 save field; empty on fresh PC client. Fix: identity profile fallback in `netClientReadConfig()` — copies from `identityGetActiveProfile()->name` when legacy name is empty.

### Decisions Made
- `NET_MAX_CLIENTS` = 32, decoupled from `MAX_PLAYERS` = 8. Server accepts 32 connections; match caps at 8 active slots.
- Identity profile is the authoritative source of local player display name. Legacy g_PlayerConfigsArray is fallback only.

---

## Session 49e — 2026-03-26

**Focus**: Version system full audit + fix

### What Was Done

**Root cause found**: CMake's `CACHE` variable behavior — when `CMakeCache.txt` exists, `set(VERSION_SEM_PATCH N CACHE STRING ...)` is silently ignored. `Set-ProjectVersion` edited CMakeLists.txt correctly but cmake configure didn't override the stale cache.

**Fixes**: `Get-BuildSteps` accepts `$ver` param, appends `-DVERSION_SEM_MAJOR/MINOR/PATCH` flags to BOTH configure steps. `Start-PushRelease` passes `$ver` to `Get-BuildSteps`. `port/src/video.c:91`: replaced hardcoded `"Perfect Dark 2.0 - Client (v0.0.2)"` with `"Perfect Dark 2.0 - v" VERSION_STRING`.

**`context/build.md`**: Added full Version System section documenting the CACHE pitfall and fix.

### Decisions Made
- (Note: later superseded by S49i — ALL builds now use version flags, not just releases)

---

## Session 49f — 2026-03-26

**Focus**: Updater UI — banner fix, per-row actions, server update mechanism

### What Was Done

**Client update banner (`pdgui_menu_update.cpp`)**: Replaced `SmallButton` with `Button` sized via `pdguiScale`; right-aligned via `SameLine(GetContentRegionMax().x - totalW)`. Added `s_DownloadingIndex` + `s_StagedReleaseIndex` state for per-release tracking.

**Settings > Updates tab**: 5-column table (added Action column). Per-row buttons: Download, Switch (staged), % (in-progress). Error message moved below table. Table shown during active download.

**Server update mechanism**: `server_main.c` added `updaterTick()` per frame, logs update availability. `server_gui.cpp`: "Updates (*)" tab with per-row Download/Switch buttons, progress display, Restart & Update button.

### Decisions Made
- `SameLine(GetContentRegionMax().x - totalW)` is the canonical ImGui right-align pattern
- Server headless update path: log URL + manual restart

---

## Session 49g — 2026-03-26

**Focus**: F8 hotswap hint removal

### What Was Done
- Removed deprecated F8 footer hint ("F8: toggle OLD/NEW") from `pdgui_menu_mainmenu.cpp` (footer block at bottom of `renderMainMenu`).

---

## Session 49h — 2026-03-26

**Focus**: Update tab button sizing audit

### What Was Done

**`pdgui_menu_update.cpp` button sizing overhaul**:
- `renderNotificationBanner`: `CalcTextSize()`-based widths for "Update Now", "Details", "Dismiss". Explicit `btnH = GetFontSize() + FramePadding.y * 2` — descender-safe.
- `renderVersionPickerContent`: `CalcTextSize("Check Now")` for "Check Now" button. Action column width from `CalcTextSize("Download")`.
- `TableSetupScrollFreeze(0, 1)` — header stays visible on scroll. Column widths use `pdguiScale()`. `ImGuiSelectableFlags_AllowOverlap` so per-row buttons receive input.
- Removed below-table "Download & Install" button (was off-screen, invisible).
- Download = green, Rollback = amber styling.

### Decisions Made
- Action buttons live in table rows (always visible), not below table (was off-screen)
- `AllowOverlap` is the correct pattern for interactive items in `SpanAllColumns` rows

---

## Session 49i — 2026-03-26

**Focus**: Build pipeline overhaul — always-clean, version baking on every build

### What Was Done

**`devtools/dev-window.ps1` overhaul**:
- **Always-clean builds**: `Start-Build` unconditionally deletes `build/client` + `build/server` before every build. No stale CMakeCache possible.
- **Version from UI on every build**: `Start-Build` reads `Get-UiVersion` → `$script:BuildVersion`, passes to `Get-BuildSteps $script:BuildVersion`. Version boxes are single source of truth.
- **Get-BuildSteps**: Accepts `$ver` parameter, injects `-DVERSION_SEM_MAJOR/MINOR/PATCH` flags into BOTH configure steps.
- **CMakeLists.txt updated after build**: On successful completion, `Set-ProjectVersion` called from `$script:BuildVersion` — file always reflects what was actually built.
- **`Start-PushRelease` updated**: Also cleans before queuing, sets `$script:BuildVersion = $ver`, passes to `Get-BuildSteps`.
- **Removed**: `$script:CleanBuildActive`, `$script:BtnCleanBuild` toggle, associated handler. BUILD button now full hero height.

### Decisions Made
- All builds are clean builds. "Incremental" option removed entirely.
- Version boxes initialize from CMakeLists.txt at startup (reflects last built state).
- CMakeLists.txt updated END of build; -D flags are authoritative during build, file updated after.
- For releases: CMakeLists.txt still updated BEFORE build (pre-release auto-commit).

### Next Steps
- Test full build to verify version bakes correctly
- Verify Release flow (clean → configure with -D → build → release.ps1)

---

## Session 50 — 2026-03-26

**Focus**: Update tab — cross-session staged version persistence

### What Was Done

**Staged version sidecar** (`updater.c`, `updater.h`):
- Added `versionPath` field to state (`exePath.update.ver`)
- `detectExePath()` now computes `versionPath` for both Win32 and Unix paths
- `writeStagedVersionFile()` / `readStagedVersionFile()` helpers — tiny text file, one version string
- `downloadThread()`: on DOWNLOAD_DONE, writes version sidecar outside mutex, then sets `stagedVersion` + `stagedVersionValid` in state
- `updaterInit()`: if `.update` file exists on disk, reads sidecar to restore staged version
- `updaterApplyPending()`: removes `.update.ver` after successful rename (both Win32 + Unix paths)
- New public API: `updaterGetStagedVersion()` — returns `&stagedVersion` if valid, NULL otherwise

**UI fix** (`pdgui_menu_update.cpp`):
- `isStaged` check now queries `updaterGetStagedVersion()` in addition to `s_StagedReleaseIndex`
- Cross-session staged version: if `.update.ver` matches this row's version, shows amber "Switch to this version" button immediately on launch
- Syncs `s_StagedReleaseIndex` from disk-persisted version so same-session Switch/restart flow works

**context/update-system.md**: Updated Self-Replacement section with sidecar file details and cross-session staged version note.

### Why This Matters
Before: if you downloaded a version then closed the game without restarting, reopening the Update tab showed no "Switch" button — you had to re-download. After: the sidecar file persists the staged version across sessions; the Switch button appears immediately.

### Decisions Made
- Sidecar is cleaned up by `updaterApplyPending()` so it's never stale post-apply
- `updaterGetStagedVersion()` is the cross-session source of truth; `s_StagedReleaseIndex` remains for same-session download tracking

### Next Steps
- Build test: Download a version, close without restarting, reopen — verify Switch button appears
- (Unchanged from S49) SPF-3 playtest, catalog C-1/C-2, menu replacement Group 1

---

## Session 48 -- 2026-03-25

**Focus**: Dev Window overhaul, project cleanup, infrastructure hardening

### What Was Done

**Dev Window (dev-window.ps1)**:
- Fixed UI thread hang: git status moved to background runspace, then to Activated event
- NotesSaveTimer race condition fixed (no more dispose-in-tick)
- Font caching in paint handlers (no per-frame allocation)
- Tab background white strip eliminated (dark panel wrapper)
- Auth label: clickable button, opens `gh auth login` if unauthenticated
- GitHub + Folder buttons moved to main UI (bottom of Build tab)
- Two font size settings (Button + Detail) with live refresh
- Stable/Dev toggle checkbox for releases
- Documentation tab (split pane: file list + content reader, 30/70 ratio)
- Clean Build toggle button (beneath BUILD, wipes build dirs before configure)
- Post-build copy list configurable via settings
- Client/server status labels show exe existence on startup
- Latest release label shows tag + dev/stable + color
- Background runspaces now pass PATH for gh CLI access

**Release pipeline (release.ps1)**:
- All 7 PS7-only syntax violations fixed for PS5 compatibility
- All em dashes replaced with ASCII
- Unified release: single tag (v0.0.1) with both client + server attached
- Auto-overwrite existing releases (delete + recreate with sound notification)
- GIT_TERMINAL_PROMPT=0 in subprocess environment

**Project cleanup**:
- Deleted: 6 runbuild scripts, fix_endscreen, phase3 docs, context-recovery.skill, mods folder info, PROMPT.md, context.md (106KB monolithic), ROADMAP.md, pd-port-director-SKILL.md, CHANGES.md, old devtools (build-gui, playtest-dashboard, doc-reader + .bat launchers)
- Deleted: 4.3GB of abandoned Claude Code worktrees
- Created: UNRELEASED.md (player-facing changelog), dist/windows/icon.ico + icon.rc
- Session log archived (S22-46 to sessions-22-46.md, active trimmed to 229 lines)
- tasks-current.md cleaned (completed items removed)
- COWORK_START.md rewritten as lean bootstrap pointer

**Code fixes**:
- fs.c: data directory search priority fixed (exe dir first, then cwd, then AppData)
- romdata.c: creates data/ dir + README.txt when ROM missing, then opens correct folder
- .build-settings.json: ROM path updated to new project location

**Skill + context**:
- game-port-director skill updated with Sections 8-9 (design principles, tool patterns)
- Skill packaged as .skill for reinstallation
- Context canonical location documented in CLAUDE.md
- 6 memories saved (profile, event-driven, clean structure, no worktrees, ACK messages, no ambiguous intent)

### Decisions Made
- Event-driven over polling (standing principle)
- Unified release tag (v0.0.1) replaces split client/server tags
- context/ is canonical location, parent copies are convenience mirrors
- No worktrees: all code changes in working copy

### Bugs Noted
- B-18: Pink sky on Skedar Ruins (possible texture/clear color issue)
- B-19: Bot spawn stacking on Skedar Ruins (all bots spawn at same pad)

### Session 48 continued -- Collision Rewrite + Debug Vis

**Collision system** (meshcollision.c + meshcollision.h):
- Triangle extraction from model DL nodes (G_TRI1, G_TRI4) -- WORKING
- Room geometry extraction (geotilei, geotilef, geoblock) -- WORKING, 7,110 tris on Skedar
- Static world mesh with spatial grid (256-unit cells) -- WORKING
- Dynamic mesh attachment via colmesh* field on struct prop -- CODED
- capsuleSweep: mesh primary, legacy fallback -- ACTIVE
- capsuleFindFloor: mesh primary -- ACTIVE, confirmed in logs
- capsuleFindCeiling: mesh primary -- FIXED slack formula, needs retest
- Stage lifecycle hooks in lv.c -- ACTIVE on all gameplay stages

**Debug visualization** (meshdebug.c):
- F9 toggles surface tinting in the GBI vertex pipeline
- Green=floor, Red=wall, Blue=ceiling based on vertex normals
- Zero overhead when off (cached flag check per frame)

**Data path fixes**:
- fs.c: exe dir searched first for data/ folder
- romdata.c: creates data/ dir + README.txt when ROM missing, opens correct folder
- dev-window.ps1: Copy-AddinFiles server guard removed (was blocking all copies)
- release.ps1: unified tag, auto-overwrite, PS5 compat, all em dashes fixed

### Session 48 continued -- Collision Disabled + Multiplayer Planning

**Collision rewrite DISABLED**: original system fully restored. Mesh collision code preserved
in meshcollision.c/h for Phase 2 redesign. Needs proper design accounting for: no original
ceiling colliders, jump-from-prop detection (simple downward raycast), slope behavior,
Thrown Laptop Gun as ceiling detection reference. HIGH PRIORITY return.

**ASSET_EFFECT type** added to catalog: 6 effect types (tint, glow, shimmer, darken, screen,
particle), 6 targets (scene, player, chr, prop, weapon, level). First effect mod pending.

**Live console**: backtick toggle, 256-line ring buffer, color-coded ImGui window.

**Multiplayer infrastructure vision confirmed (Mike)**: server = social hub with persistent
connections. Players connect and exist as presence regardless of activity (solo campaign,
MP match, co-op, splitscreen, level editor). Rooms for concurrent activities. Server mesh/
federation for load distribution. Player profiles with stats/achievements/shared content.
Menu system audit needed (double-press issues, hierarchy).

### Session 48 continued -- Menu Manager + Multiplayer Plan

**Menu State Manager (SPF-2a)**:
- New files: `port/src/menumgr.c` + `port/include/menumgr.h`
- Stack-based (8 deep), 2-frame input cooldown on push/pop
- Initialized in main.c, ticks in mainTick() (src/lib/main.c)
- pdguiProcessEvent blocks all key/button input during cooldown
- Pause menu wired: open checks cooldown, pushes MENU_PAUSE; close pops
- Modding hub wired: open pushes MENU_MODDING, back pops
- End Game confirm button now uses pdguiPauseMenuClose() instead of direct flag set
- Legacy PD menus (g_MenuData.root) not yet wrapped -- separate task

**Multiplayer Plan** (context/multiplayer-plan.md):
- Full design doc written covering server-as-hub, rooms, federation, profiles, phonetic
- Confirmed decisions: all MP through dedicated server, campaign = co-op (offline OK),
  automatic federation routing, stats framework first, editor pre-1.0 but lower priority
- Splitscreen works offline, treated as group when connected to server
- Campaign has dual authority: local (offline) or server (online)

**ASSET_EFFECT** added to catalog enum (12th asset type). Effect types + targets defined.
Release script updated: only zip attached (no separate exe files).
Collision mesh system disabled, original restored. Code preserved for Phase 2.

### Next Steps
- SPF-2b: verify SPF-1 build (hub/room/identity/phonetic)
- SPF-3a: lobby ImGui screen design + implementation
- ASSET_EFFECT mod creation + mods copy pipeline
- Wire remaining menus through menu manager (settings, etc.)
- B-19, B-20, B-18 bug investigation
- Collision Phase 2 design (HIGH PRIORITY)

---

## Session 47d — 2026-03-24

**Focus**: SPF-1 — Server Platform Foundation (hub lifecycle, room system, identity, phonetic encoding)

### What Was Done

Implemented the server platform foundation layer on top of the existing ENet dedicated server.
Four new module pairs + wiring into server_main.c + server_gui.cpp tab bar.

**New files (8):**

1. **`port/include/phonetic.h`** / **`port/src/phonetic.c`** — CV syllable IP:port encoding.
   16 consonants × 4 vowels = 6 bits/syllable × 8 syllables = 48 bits (IPv4 + port).
   Format: `"BALE-GIFE-NOME-RIVA"` — shorter than word-based connect codes. Both coexist.
2. **`port/include/identity.h`** / **`port/src/identity.c`** — `pd-identity.dat` persistence.
   Magic `PDID`, version byte, 16-byte UUID (xorshift128 seeded from SDL perf counter + time),
   up to 4 profiles (name/head/body/flags). Validates on load, rebuilds default on corruption.
3. **`port/include/room.h`** / **`port/src/room.c`** — Room struct + 5-state lifecycle.
   Pool of 4 rooms. Room 0 permanently wraps the existing match lifecycle (never truly closes).
   States: LOBBY→LOADING→MATCH→POSTGAME→CLOSED. Transitions logged via `sysLogPrintf`.
4. **`port/include/hub.h`** / **`port/src/hub.c`** — Hub singleton owning rooms + identity.
   `hubTick()` reads `g_Lobby.inGame` each frame → drives room 0 state machine.
   One-frame POSTGAME bridge on match end. Derives hub state from aggregate room states.

**Modified files (3):**

5. **`port/src/server_main.c`** — Added `hubInit()` / `hubTick()` / `hubShutdown()` calls.
6. **`port/fast3d/server_gui.cpp`** — Middle panel converted to tabbed layout.
   "Server" tab: existing player list + match controls. "Hub" tab: hub state + room table
   with color-coded states. Log panel: HUB: prefix highlighted purple.
7. **`context/server-architecture.md`** — SPF-1 section added (hub/room diagram, phonetic,
   GUI changes, new file table).

**Commit**: `fb5450b feat(SPF-1): hub lifecycle, room system, player identity, phonetic encoding`

### Decisions Made

- **Backward compatibility**: Room 0 driven by `g_Lobby.inGame` observation — zero changes
  to `net.c` or `netlobby.c`. Existing single-match path unchanged.
- **Protocol**: v21 unchanged. No new ENet messages. Both phonetic and word connect codes
  remain available.
- **`HUB_MAX_CLIENTS`**: Defined directly in `room.h` (= 8) rather than including `net/net.h`
  to keep hub modules standalone and avoid the full game header chain.
- **Boolean fields**: Used `int` not `_Bool`/`bool` in new C modules (port/ files, but
  matching the project convention of `s32` for boolean-like values).
- **Room 0 persistence**: `roomDestroy()` on room 0 resets to LOBBY instead of CLOSED —
  room 0 is the permanent lounge for the existing server lifecycle.

### Dev Build Status

**UNVERIFIED** — Build environment broken in session (GCC TEMP path issue in sandbox).
`build-headless.ps1` TEMP/TMP fix committed. User to verify build from local environment.

### Session 47e Follow-up — 2026-03-24

**Focus**: Fix server build — SPF-1 symbols undefined in pd-server

**Root cause**: SRC_SERVER in CMakeLists.txt is a manually curated list; the 4 new SPF-1
files (hub.c, room.c, identity.c, phonetic.c) were not added when coded in S47d.
Client uses GLOB_RECURSE so it picked them up automatically; server did not.

**Fix**: Added 4 entries to SRC_SERVER block in CMakeLists.txt (lines 478–482).
Commit `c788486`. Pushed to dev.

**Build status**: Cannot verify in sandbox (GCC DLL loading issue — cc1.exe needs
libmpfr-6.dll via Windows PATH, not POSIX PATH). Run `.\devtools\build-headless.ps1 -Target server`
from PowerShell to confirm.

### Next Steps

- Run `.\devtools\build-headless.ps1 -Target server` from PowerShell to confirm fix
- Build and QC test SPF-1 modules (see qc-tests.md)
- SPF-2: Room federation / multi-room support
- D5: Settings persistence for server configuration

---

## Session 47b — 2026-03-24

**Focus**: B-12 Phase 2 — Migrate chrslots callsites to participant API

### What Was Done

Completed the Phase 2 migration of all chrslots bitmask read/write sites across 5 files.
Phase 1 bulk-sync calls (`mpParticipantsFromLegacyChrslots`) replaced with targeted
`mpAddParticipantAt`/`mpRemoveParticipant` at each write site.

**Key design established:**
- Pool capacity is `MAX_MPCHRS` (40), not the Phase 1 default 32
- Pool slot `i` == chrslots bit `i` (players 0–7, bots 8–39)
- `mpIsParticipantActive(i)` is a direct drop-in for `chrslots & (1ull << i)`
- New `mpAddParticipantAt(slot, type, ...)` API for exact-slot placement

**Files changed (7):**

1. **`src/include/game/mplayer/participant.h`** — Added `mpAddParticipantAt()` declaration
2. **`src/game/mplayer/participant.c`** — Added `mpAddParticipantAt()` impl; rewrote
   `mpParticipantsToLegacyChrslots` (slot index IS bit index) and
   `mpParticipantsFromLegacyChrslots` (use `mpAddParticipantAt` for exact placement)
3. **`src/game/mplayer/mplayer.c`** — ~25 sites: mpInit, match lifecycle, bot create/copy/
   remove, score, team assignment, name generation, save/load config and WAD
4. **`src/game/mplayer/setup.c`** — 10 sites: handicap CHECKHIDDEN, team loop ×3,
   bot slot UI, simulant name display, player file availability
5. **`src/game/challenge.c`** — Read check + fix `1u`→`1ull` write bug + add participant
   calls alongside chrslots writes in `challengePerformSanityChecks`
6. **`src/game/filemgr.c`** — 2 player-file presence checks
7. **`port/src/net/matchsetup.c`** — `mpClearAllParticipants()` + `mpAddParticipantAt`
   at each player/bot write site

**Commit**: `94a2b1e feat(B-12-P2): migrate chrslots callsites to participant API`

### Dev Build Status

**PASS** — `cmake --build --target pd` clean (exit 0). All 7 files compiled without errors.

### Decisions Made

- `challengeIsAvailableToAnyPlayer` reads `chrslots & 0x000F` as a bitmask for challenge
  availability computation — left as-is (no clean participant API equivalent, chrslots
  still dual-written in Phase 2)
- `mp0f18dec4` VERSION guard retained (PC builds are >= JPN_FINAL, always included)
- `setup.c` fixes applied via line-by-line PowerShell replace (Edit tool had CRLF mismatch)

### Next Steps

- B-12 Phase 3: Remove `chrslots` field + legacy shims + BOT_SLOT_OFFSET
- Protocol version bump to v21 (SVC_STAGE_START uses participant list)
- QC: in-game bot add/remove, match start/end, save/load bot config

---

## Session 47c — 2026-03-24

**Focus**: Stage Decoupling Phase 2 (Dynamic stage table) + Phase 3 (Index domain separation)

### What Was Done

**Phase 2 — Dynamic stage table** (7 files):

1. **`src/game/stagetable.c`** — Renamed static array to `s_StagesInit[]`, added heap pointer `g_Stages` + `g_NumStages`. `stageTableInit()` mallocs+memcpys. `stageGetEntry(index)` bounds-checked accessor. `stageTableAppend(entry)` realloc-based. Both `stageGetCurrent()` and `stageGetIndex()` rewritten to use `g_NumStages`. `soloStageGetIndex(stagenum)` iterates `g_SoloStages[0..NUM_SOLOSTAGES-1]`.
2. **`src/include/data.h`** — `extern struct stagetableentry *g_Stages` + `extern s32 g_NumStages` (was array).
3. **`src/include/game/stagetable.h`** — Full declaration set for all Phase 2 + 3 functions.
4. **`src/game/bg.c`** — `ARRAYCOUNT(g_Stages)` replaced with `g_NumStages` (2 occurrences).
5. **`port/src/assetcatalog_base.c`** — Removed local `extern struct stagetableentry g_Stages[]` (conflicted with pointer decl). Bounds check `idx >= 87` → `idx >= g_NumStages`.
6. **`port/src/main.c`** — Added `stageTableInit()` call before `assetCatalogRegisterBaseGame()`.

**Phase 3 — Index domain guards** (2 files):

7. **`src/game/endscreen.c`** — 9 guard sites: `endscreenMenuTitleRetryMission`, `endscreenMenuTitleNextMission`, `endscreenMenuTitleStageCompleted`, `endscreenMenuTextCurrentStageName3`, `endscreenMenuTitleStageFailed`, `endscreenHandleReplayPreviousMission` (underflow), `endscreenAdvance()` (overflow), `endscreenHandleReplayLastLevel`, `endscreenContinue` DEEPSEA (2 paths, both guarded).
8. **`src/game/mainmenu.c`** — 4 guard sites: `menuTextCurrentStageName`, `soloMenuTitleStageOverview`, `soloMenuTitlePauseStatus`, `isStageDifficultyUnlocked` (top guard returns true for out-of-range — mod stages treated as unlocked).

**Bonus fix**: Restored `src/game/mplayer/setup.c` and `src/game/setup.c` from commit `4704eab` after auto-commit `0a36981` corrupted them (all tabs replaced with literal `\t`). Pre-existing bug revealed by full rebuild.

### Decisions Made

- `soloStageGetIndex()` lives in `stagetable.c` (iterates `g_SoloStages[]`). It is the Phase 3 domain translation function.
- `isStageDifficultyUnlocked(stageindex < 0 || >= NUM_SOLOSTAGES)` returns `true` — mod stages are "unlocked" by definition (no solo-stage-based unlock system applies to them).
- `ARRAYCOUNT(g_Stages)` was eliminated. Any future code must use `g_NumStages`.

### Dev Build Status

**PASS** — `build-headless.ps1 -Target client` clean (exit 0). All modified files compiled without errors. Warnings in bg.c are pre-existing.

### Next Steps

- MEM-2: `assetCatalogLoad()` / `assetCatalogUnload()`
- MEM-1 build test: full cmake pass confirms `assetcatalog.h` struct changes are stable
- S46b: Full asset catalog enumeration (animations, SFX, textures)

---

## Session 47a — 2026-03-24

**Focus**: MEM-1 — Asset Catalog load state tracking fields

### What Was Done

Added lifecycle state tracking to `asset_entry_t` as the foundation for Phase D-MEM
memory management. This is purely additive — no existing behavior changes.

**Files changed (4 files):**

1. **`port/include/assetcatalog.h`** — Added `asset_load_state_t` enum
   (`REGISTERED`/`ENABLED`/`LOADED`/`ACTIVE`). Added `#define ASSET_REF_BUNDLED 0x7FFFFFFF`.
   Added 4 fields to `asset_entry_t`: `load_state`, `loaded_data`, `data_size_bytes`,
   `ref_count`. Added `assetCatalogGetLoadState()` and `assetCatalogSetLoadState()`
   declarations in new "Load State API (MEM-1)" section.

2. **`port/src/assetcatalog.c`** — `assetCatalogRegister()` initializes new fields:
   `ASSET_STATE_REGISTERED`, `loaded_data=NULL`, `data_size_bytes=0`, `ref_count=0`.
   `assetCatalogSetEnabled()` now advances `REGISTERED→ENABLED` on first enable.
   Added `assetCatalogGetLoadState()` and `assetCatalogSetLoadState()` implementations.

3. **`port/src/assetcatalog_base.c`** — All 4 bundled registration sites (stages, bodies,
   heads, arenas) now set `load_state=ASSET_STATE_LOADED` and `ref_count=ASSET_REF_BUNDLED`.

4. **`port/src/assetcatalog_base_extended.c`** — All 7 bundled registration sites (weapons,
   animations, textures, props, gamemodes, audio, HUD) now set `ASSET_STATE_LOADED` and
   `ref_count=ASSET_REF_BUNDLED`.

### Decisions Made

- `ASSET_REF_BUNDLED = 0x7FFFFFFF` (S32_MAX) as documented in MEM-1 spec.
- `REGISTERED→ENABLED` transition happens in `setEnabled(id, 1)`. If load_state is already
  LOADED or ACTIVE (bundled assets), setEnabled does not downgrade state.
- `assetCatalogSetLoadState()` is a raw setter — callers own the validity of transitions.
  Future eviction logic will use `ref_count` to guard bundled assets.
- `loaded_data` / `data_size_bytes` fields left at NULL/0 for all existing entries —
  wired for the future loader, not populated yet.

### Dev Build Status

- Syntax-check (MinGW gcc -fsyntax-only): **PASS** on all 3 modified .c files
- Full cmake build: needs Mike's `build-headless.ps1` run (cmake env not available in session)

### Next Steps

- MEM-2: Implement `assetCatalogLoad()` / `assetCatalogUnload()` (allocate/free loaded_data)
- MEM-3: ref_count acquire/release + eviction policy (skip if `ref_count == ASSET_REF_BUNDLED`)
- Wire load state into mod manager UI (show loaded/active indicators)

---
