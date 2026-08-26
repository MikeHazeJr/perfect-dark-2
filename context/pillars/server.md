# Server / Hosting

> Listen-host is the shipping target. **Dedicated server (`PerfectDarkServer.exe`) is removed/deprecated** -- no build target, no routine verify, no ongoing development. P2P networking via listen-host is the canonical model. Hub holds rooms; rooms are demand-driven, not pre-allocated. Participant pool is the sole source of match slot truth. Admin RCON, persistent bans, hashed room passwords are wired.

---

## What it is

The server side of the multiplayer architecture. Two modes share most code:

- **Listen host** (`g_NetDedicated == 0`): server runs inside the game client process. Local player occupies a slot. The shipping target per [constraints.md](../constraints.md) S486 (2026-04-27).
- **Dedicated** (`g_NetDedicated == 1`, `g_NetLocalClient == NULL`): historical standalone `PerfectDarkServer.exe`. **Removed/deprecated** per Mike 2026-05-17 and reconfirmed 2026-05-21. The CMake `pd-server` target is gone; use `ninja -C Build pd pd-tests` or the session build wrapper targets.

Listen-host uses `port/src/net/net.c` transport, `port/src/net/netmsg.c` protocol handlers, and `port/src/room.c` room logic inside `PerfectDark.exe`. Historical standalone server stubs remain in tree for reference but are no longer built as a product target.

For friend-play, D-003 option A keeps social/group discovery peer-to-peer but
elects exactly one accepted peer as match authority before ENet startup. The
authority starts this same in-client listen-server path once and publishes a
separately typed signed match-server route through presence v5. Other peers
join that route once; probe endpoints and relay descriptors never substitute
for the listen address. Gameplay remains server-authoritative through the
existing ENet authentication, protocol-version, lobby, manifest, ready, and
`SVC_*` validation paths.

The final D-003 matrix uses ordinary client processes and one frozen client
hash. Initiator authority passes 143/143, invitee authority 142/142, failed
listen startup rollback 32/32, and failed client join rollback 38/38. The
complete suite passes 56,774/1,040. This verifies friend-play ownership without
claiming the separate real-NAT candidate/tier or live host-migration work.

Code:

- Hub + rooms: [port/include/hub.h](../../port/include/hub.h), [port/include/room.h](../../port/include/room.h), [port/src/hub.c](../../port/src/hub.c), [port/src/room.c](../../port/src/room.c).
- Server stubs: [port/src/server_stubs.c](../../port/src/server_stubs.c).
- Participant pool: [port/include/participant.h](../../port/include/participant.h), [src/game/mplayer/participant.c](../../src/game/mplayer/participant.c).
- Server bans: [port/src/server_bans.c](../../port/src/server_bans.c).
- Admin RCON: in [port/src/net/netmsg.c](../../port/src/net/netmsg.c) (`CLC_ADMIN 0x15`, `SVC_ADMIN 0x68`).
- Identity: [port/src/identity.c](../../port/src/identity.c).
- Group session (presence/auth/voice cross-cutting): [port/src/net/group_session.c](../../port/src/net/group_session.c).

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

**Identity cookie is the reconnect authority** (MASTER-C3/B-1064, updated 2026-08-14). `struct netpreservedplayer` records both `name` and a 16-byte `cookie[NET_AUTH_COOKIE_LEN]` server-issued at first `CLC_AUTH`. The ENet connect-data client ID is only a stable-slot hint; the server admits reconnect only after constant-time name-and-cookie authentication for that preserved record. Name-only lookup remains advisory and MUST NOT gate restore. The client cookie is module-static and never persisted: only retryable timeout teardown retains it, only for the same resolved endpoint, while intentional, policy, hosting, and other final teardown clears it.

**Server disconnect policy is authoritative** (B-1092/SP-57, source connected
2026-08-14). ENet delivers a disconnect datum to the remote peer but reports
zero to the initiating sender's acknowledged-disconnect event. Every
authenticated server close therefore enters through `netServerKick`, whose
first-writer intent is stored on `netclient` before ENet and consumed once by
server teardown. The intent overrides a racing transport observation: timeout
preserves the reservation, while kick, ban, admin lockout, content failure, and
other terminal reasons remove it. The current-source isolated build, focused
385/8, complete 61,291/1,159 suite, and native-source guard pass; the ordinary-
client reconnect runtime receipt remains pending.

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
- **One friend-play match authority, one startup action.** The accepted invite
  freezes election inputs before transport startup. Only the elected local peer
  may call `netStartServer`; a remote peer may join only a fresh verified
  presence-v5 match-server route, and startup failure clears the latch without
  implicit retry.

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
- Standalone `pd-server` is removed as a tooling target; listen-host client builds are the supported verification path.

---

## What is in flight (deferred per S486)

- **Game-agnostic dedicated server** (P4-B / P4-C). Plugin ABI design in [designs/connectivity/pd-server-plugin-abi.md](../designs/connectivity/pd-server-plugin-abi.md). Server stubs would shrink to almost nothing; manifest broker pattern; per-client dynamic catalogs; optional policy module. Deferred until in-client connectivity ships.
- **R-5 server GUI redesign**. Players + Rooms panels, operator actions (Move / Kick / Set Leader / Close Room). Planned per old infrastructure tracker.
- **L-5 dedicated Campaign / Counter-Op setup screen**. Deferred per the in-client pivot.
- **L-6 drop-in prompt on client side**. Deferred per the in-client pivot.
- **Multi-player stage startup transaction (B-1067/B-1103/B-1104).** Source
  now rolls back a preparing participant's ready-gate transaction before a
  valid settings publication and reverse-unwinds every committed player after
  a later co-op/Counter-Op reset or spawn failure. Protocol v58 now gives the
  server one explicit inactive/waiting/release/active publication lifecycle.
  `SVC_STAGE_START` carries a nonzero epoch; each remote READY must echo it; the
  listen authority owns a separate real post-load latch; and release publishes
  only a fresh baseline from dedicated packet storage before ordinary
  ACTIVE-frame traffic begins. The complete pending mask clears only after the
  room-scoped reliable packet queues, so no shared control-buffer reset can
  consume or partially publish it. NPC
  convergence is one transactional full resync plus an immediate canonical
  sync-ID-sorted digest of the exact serialized/applied snapshot, with no
  standalone periodic live checksum. The prior v57 four-fixture receipt remains rejected;
  replacement v58 automation passes isolated builds, complete tests
  66,421/1,200, the native-source guard, and unchanged product/verifier
  manifests. Ordinary-client receipts now pass co-op 96/96, Counter-Op 98/98,
  later-player rollback 60/60, settings rollback 43/43, initiator authority
  214/214, reconnect 99/99, and the focus-independent invitee-authority route
  170/170. The immutable raw route receipt remains rejected at 169/170 because
  its stale regex rejected valid epoch 1; corrected static coverage passes
  108/2 and the separately hashed retained logs pass 170/170 without a product
  change. The route proves one elected in-client listen authority, one signed
  typed match-server route, exactly one non-authority join, and no probe/relay
  endpoint handoff. This transaction is now a production regression gate.
  B-1076 subsequently
  centralized the shared Combat Simulator stage-request boundary in
  `mpStartMatch`: both the in-client listen authority and receiving client now
  release menu/input ownership before stage publication, matching offline
  starts. Exact client `04DB220E...` preserves this invitee-authority route at
  170/170 while the ordinary two-cycle graph/Play Again lifecycle passes 55/55
  without watchdog repair. B-1076 is a regression gate; it does not alter the
  authority election, typed-route, or protocol-v58 contracts above.

- **B-1096/B-1097 reconnect closure is production-verified.** Exact client
  `BD0F9DAC...` retains accepted isolated
  builds, focused 468/15, complete 66,705/1,204, and the native-source guard.
  Its sole ordinary run passes 101/105 and proves reciprocal attachment, the
  production `weaponDeleteFromChr` transition, exact reconnect world/
  inventories, one commit, resumed authority fire, and clean teardown. Normal
  cleanup fully frees the selected dynamic prop before PREPARE; a pristine
  receiver has no stale counterpart. Luna xhigh confirmed the failed
  `terminal_absent=1`/`removed>0` assertions inverted the desired lifecycle.
  Corrected harness source records the selected sync ID and later performs one
  read-only authority-pool scan after ordinary cleanup. The scenario requires
  that exact ID absent before reconnect auth, PREPARE `terminal_absent=0`,
  receiver `removed=0`, and `exact_set=1`; it does not retain dead props or edit
  reconnect packets, counters, or receiver state. The first corrected 477/15 +
  66,714/1,204 + guard batch passed. Its runtime launch rejected before ENet
  because the 34-character event name exceeded an existing 31-character parser
  buffer. Schema/harness source now shares a 64-byte complete-token capacity and
  rejects overflow before copy. The parser-safe refreeze passed focused 483/16,
  complete 66,720/1,205, the native-source guard, and zero drift on exact client
  `5AEC7918...`. The replacement ordinary run
  `results-20260826T111558Z.json` is retained/rejected at 93/109: authority
  retirement and read-only absence, pristine exact world, both inventories, and
  one commit pass, but the client then faults in `objTickPlayer` before restored
  fire. The snapshot currently routes its historical dead bit through the live
  `playerDieByShooter` event path, replaying score/drop/held-prop side effects
  after exact state restoration. Current source now plans live versus snapshot
  player-state transitions in the pure reconnect layer. A shared player
  dead-state boundary preserves presentation but the snapshot entry point omits
  score/killfeed, item drop, owner cleanup, menu/HUD retirement, and lifetime
  metrics. `SVC_PLAYER_STATS` selects it only while the ordered reconnect world
  transaction is complete and not yet committed, and emits an exact
  `live_side_effects=0` witness. Luna xhigh also proved a second ownership
  defect: the old dynamic spawn receiver inserted a prop into the active/paused
  scheduler before `propReparent` reused the same `next/prev` fields for child
  ownership. Current source removes sync-ID-zero local held weapons before
  authoritative adoption, constructs rows off-list, commits exactly one
  attached/active/paused placement, validates scheduler membership on both
  authority and receiver plus transaction end, and emits `topology=exclusive`.
  The 18-site `propReparent` propagation audit found this network path as the
  direct scheduler-before-reparent violation; common producers delist first or
  construct off-list. Exact product `1C184C57...` / client `8262681E...` and
  verifier `E0FA8E6A...` / tests `A9B1ADAA...` now pass isolated builds,
  focused 718/20, complete 66,960/1,209, and the native-source guard with zero
  manifest drift. The earlier stale-test-binary receipt is retained/rejected.
  Exact unchanged client `8262681E...` then passes
  `results-20260826T121959Z.json` 116/116: one production retirement and
  read-only authority absence, one retryable reconnect, snapshot
  `live_side_effects=0`, exclusive prop topology, exact world/inventories, one
  commit, 30 real Cyclone shots with `server_accepted=1`, final credential
  retirement, clean exits, and no process leak. B-1096/B-1097 plus SP-60,
  SP-61, SP-72, and SP-73 are regression gates. Together with the retained
  Campaign, Combat Simulator, Air Base, rollback, both-authority-role, and
  V-009 gates, this completes T-ENGINE-004's section 9.10 matrix. Durable index:
  `context/evidence/2026-08-26-t-engine-004-closure.md`.

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
