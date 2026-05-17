# Server / Hosting

> Listen-host is the shipping target. **Dedicated server (`PerfectDarkServer.exe`) is deprecated per Mike directive 2026-05-17** -- not built in routine verify, no ongoing development. P2P networking via listen-host is the canonical model. Hub holds rooms; rooms are demand-driven, not pre-allocated. Participant pool is the sole source of match slot truth. Admin RCON, persistent bans, hashed room passwords are wired.

---

## What it is

The server side of the multiplayer architecture. Two modes share most code:

- **Listen host** (`g_NetDedicated == 0`): server runs inside the game client process. Local player occupies a slot. The shipping target per [constraints.md](../constraints.md) S486 (2026-04-27).
- **Dedicated** (`g_NetDedicated == 1`, `g_NetLocalClient == NULL`): standalone `PerfectDarkServer.exe`. **Deprecated** per Mike 2026-05-17. The CMake target still exists for any legacy reference but should NOT appear in routine `ninja -C Build ...` verify lists -- use `ninja -C Build pd pd-tests` going forward. Memory file `feedback_no_pd_server_build.md` carries the rule.

Both share the same `port/src/net/net.c` transport, `port/src/net/netmsg.c` protocol handlers, and `port/src/room.c` room logic. The server target compiles a stub layer ([port/src/server_stubs.c](../../port/src/server_stubs.c)) that satisfies symbols the headless build does not need (audio, rendering, input).

Code:

- Hub + rooms: [port/include/hub.h](../../port/include/hub.h), [port/include/room.h](../../port/include/room.h), [port/src/hub.c](../../port/src/hub.c), [port/src/room.c](../../port/src/room.c).
- Server stubs: [port/src/server_stubs.c](../../port/src/server_stubs.c).
- Participant pool: [port/include/participant.h](../../port/include/participant.h), [src/game/mplayer/participant.c](../../src/game/mplayer/participant.c).
- Server bans: [port/src/server_bans.c](../../port/src/server_bans.c).
- Admin RCON: in [port/src/net/netmsg.c](../../port/src/net/netmsg.c) (`CLC_ADMIN 0x15`, `SVC_ADMIN 0x68`).
- Identity: [port/src/identity.c](../../port/src/identity.c).
- Group session (presence/auth/voice cross-cutting): [port/src/group_session.c](../../port/src/group_session.c).

---

## Hub

The hub is the singleton root that owns connected players, rooms, and (future) profile + federation links.

```
HUB
|
+-- g_NetClients[NET_MAX_CLIENTS + 1]    32 connected players + 1 temp slot
+-- Room pool                            up to HUB_MAX_ROOMS = 4
+-- Profile store (future)               player stats, achievements, shared content
+-- Federation link (future)             mesh peer routing
```

`NET_MAX_CLIENTS = 32` at [net.h:163](../../port/include/net/net.h:163). `HUB_MAX_ROOMS = 4`, `HUB_MAX_CLIENTS = 32` at [room.h:32-33](../../port/include/room.h:32). HUB_MAX_CLIENTS must match NET_MAX_CLIENTS.

---

## Rooms

Demand-driven: rooms are created when players need them, not pre-allocated. Zero players = zero rooms. The permanent room 0 is a transitional artifact that is being phased out per the R-2 design (now landed; see [pillars/connectivity.md](connectivity.md)).

Room types per [_old/network-architecture.md](_old/network-architecture.md) (historical reference):

| Type | Description | Max Players |
|------|-------------|-------------|
| MP_MATCH | Competitive multiplayer | 8 + 32 bots |
| CAMPAIGN | Campaign mission (solo or co-op) | 1-4 |
| SPLITSCREEN | Local splitscreen | 4 local |
| EDITOR | Level editor session | 1-4 |
| SPECTATE | Watching another room | unlimited |
| LOBBY | Waiting room | 8 |

Per the updated single-local-player constraint (since 2026-04-10, S188), splitscreen is dead code; PC supports exactly one local human player (Player 0).

---

## Participant pool

[port/include/participant.h](../../port/include/participant.h), [src/game/mplayer/participant.c](../../src/game/mplayer/participant.c). The single source of match slot truth, both client-side and server-side. Replaces the legacy `u64 chrslots` bitmask which was removed in B-12 Phase 3 (S324, 2026-04-17, wire bumped 36 -> 37).

`MAX_MPCHRS = 40` (8 players + 32 bots) at [pdgui_constants.h:23](../../port/include/pdgui_constants.h:23). Players occupy slots 0..MAX_PLAYERS-1; bots occupy MAX_PLAYERS..MAX_MPCHRS-1.

API:

- `mpIsParticipantActive(slot)` - is slot occupied?
- `mpAddParticipantAt(slot, type, team, client_id, localslot)` - register slot.
- `mpRemoveParticipant(slot)` - vacate slot.
- `mpGetActiveBotCount() / mpGetActivePlayerCount()`
- `mpParticipantsEncodeActiveMask() / mpParticipantsDecodeActiveMask()` - wire helpers (replaces the old `mpParticipantsTo/FromLegacyChrslots` shims).

Server now links `participant.c` directly (per S324 CMakeLists update); both sides use the same code path.

---

## Identity + cookies

[port/src/identity.c](../../port/src/identity.c). On PC, `identityGetActiveProfile()->name` is the canonical player name. The legacy N64 config field (`g_PlayerConfigsArray[0].base.name`) is consulted only as a fallback when the identity name is empty.

**Identity cookie is the reconnect authority** (MASTER-C3, S393, 2026-04-19). `struct netpreservedplayer` records both `name` and a 16-byte `cookie[NET_AUTH_COOKIE_LEN]` server-issued at first `CLC_AUTH`. Reconnect to a preserved slot requires `netServerFindPreservedByCookie(name, cookie)` to match BOTH fields in constant time. Legacy name-only lookup (`netServerFindPreserved`) remains for advisory/diagnostic use but MUST NOT be used to gate a preserved-slot restore. Client-side cookie is module-static in `netmsg.c` and is cleared on `netDisconnect`; never persisted to disk.

---

## Persistent bans

[port/src/server_bans.c](../../port/src/server_bans.c). `serverBansInit()` runs once at server start, after `hubInit()`. `netServerEvConnect` calls `serverBansIsBanned(ip)` before any client slot allocation; banned peers receive `DISCONNECT_BANNED` immediately.

Adding a ban via `serverBansAdd` (or `netServerBanClient`) saves `$S/bans.ini` atomically. Bans are NOT replicated across servers; each operator's `$S/bans.ini` is independent. (MASTER-C2c/d, S393.)

---

## Admin RCON

`serverAdminInit(cliToken, iniToken)` runs once at server start with tokens from `--admin-token` (CLI) or `[Admin] Token` (server.ini). CLI wins. Tokens shorter than 16 characters are rejected (boot warning if accepted length is below recommended 32). `serverAdminVerifyToken` does constant-time compare against the stored SHA-256 (domain-salted `"pd2-server-admin-token-v1\n"`). Once `serverAdminInit` returns, all plaintext token memory on the caller's stack MUST be zeroed. (MASTER-C2a, S393, 2026-04-19.)

Wire: `CLC_ADMIN 0x15` / `SVC_ADMIN 0x68`. Failed `ADMIN_SUB_AUTH` lockout returns `ADMIN_RESP_RATE_LIMIT 0x06` (v39).

---

## Room passwords

`roomCreateConfigured` accepts a plaintext password and immediately feeds it through `roomHashPassword` (domain-salted `"pd2-room-password-v1\n"` SHA-256) before storing. The plaintext is discarded. `roomCheckPassword(room, plaintext)` is the sole matcher; constant-time compare against `room->password_hash`. Open rooms have an all-zero hash and `roomCheckPassword` returns 1 unconditionally. (SEC-14, S393, 2026-04-19, wire v38.)

---

## Active invariants

Per [constraints.md](../constraints.md):

- **Server is not a player.** Dedicated server sets `g_NetLocalClient = NULL` and `g_NetNumClients = 0` at startup; slot 0 free. NULL-guard `g_NetLocalClient` everywhere.
- **ROM/mod check skipped on dedicated server.** `CLC_AUTH` ROM hash check gated behind `!g_NetDedicated` (no stub workarounds).
- **Identity profile is authoritative name source.**
- **Participant pool is sole match-slot store.** Use `mpIsParticipantActive` / `mpAddParticipantAt` / `mpRemoveParticipant`; do not reintroduce chrslots reads.
- **Single local player only** (S188). MAX_LOCAL_PLAYERS = 4 in arrays but only Player 0 is initialized for IMCs and bindings. No 2P splitscreen on PC.
- **Rooms are demand-driven.** Permanent room 0 is being retired (R-2).
- **Room-bound server state must be cleaned up on room teardown** (SP-14). Subsystems holding room-keyed state must hook `roomLeave` / `roomDestroy` or run a per-tick room-still-exists check. Reference fix: `s_ReadyGate` via `netReadyGateAbortForRoom` + `netReadyGateOnClientLeft` in `netmsg.c`.
- **Ready gate lifetime is bound to its room's lifetime** (Bug B, 2026-04-14).
- **Identity cookie is the reconnect authority** (MASTER-C3).
- **Persistent bans live in `$S/bans.ini`** (MASTER-C2c/d).
- **Admin RCON token is hashed, never stored as plaintext** (MASTER-C2a).
- **Room passwords are hashed at create time** (SEC-14).

---

## What is done

Per [audits/infrastructure-pillars-status-2026-04-27.md](../audits/infrastructure-pillars-status-2026-04-27.md):

- Hub + room pool + lifecycle (R-1 / R-2 / R-3 / R-4 protocol all shipped per session log).
- Participant pool replaces chrslots; B-12 Phase 3 done (S324, wire v37).
- Identity + cookie reconnect (MASTER-C3).
- Persistent bans (MASTER-C2c/d).
- Admin RCON token hashed (MASTER-C2a).
- Room passwords hashed (SEC-14, wire v38).
- Single local player constraint enforced.
- Listen-host shipping focus established (S486).
- `pd-server` builds clean as tooling target.

---

## What is in flight (deferred per S486)

- **Game-agnostic dedicated server** (P4-B / P4-C). Plugin ABI design in [designs/connectivity/pd-server-plugin-abi.md](../designs/connectivity/pd-server-plugin-abi.md). Server stubs would shrink to almost nothing; manifest broker pattern; per-client dynamic catalogs; optional policy module. Deferred until in-client connectivity ships.
- **R-5 server GUI redesign**. Players + Rooms panels, operator actions (Move / Kick / Set Leader / Close Room). Planned per old infrastructure tracker.
- **L-5 dedicated Campaign / Counter-Op setup screen**. Deferred per the in-client pivot.
- **L-6 drop-in prompt on client side**. Deferred per the in-client pivot.

---

## Known gaps

- The audit's Section 5 connectivity gaps apply to both modes (ICE peer candidate exchange, STUN reflexive transmission, kbps placeholder, UPnP partial port mapping, TURN no public fallback). See [pillars/connectivity.md](connectivity.md) Known Gaps.

---

## Active design references

- [designs/connectivity/pd-server-plugin-abi.md](../designs/connectivity/pd-server-plugin-abi.md) - ADR for game-agnostic server (deferred per S486).
- [designs/connectivity/hosting-modes-listen-vs-dedicated.md](../designs/connectivity/hosting-modes-listen-vs-dedicated.md) - threat model.
- [designs/connectivity/connectivity-and-modern-main-menu.md](../designs/connectivity/connectivity-and-modern-main-menu.md) - covers Phase 1 + 2 for in-client; this is the shipping target.

---

## Where to look

- For wire protocol + NAT traversal + transport: [pillars/connectivity.md](connectivity.md).
- For mod manifest + distribution + asset transfer: [pillars/modding.md](modding.md).
- For lobby / room / match menus: [pillars/menus.md](menus.md).
- For SP-13 (`manifestClear` discipline) and SP-14 (room teardown cleanup): [systemic-bugs.md](../systemic-bugs.md).
- For dev tooling that builds and runs the server: [pillars/build-dev-tooling.md](build-dev-tooling.md).
