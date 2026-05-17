# Connectivity / Online

> ENet UDP transport. Server-authoritative wire protocol at v46. 6-tier P2P NAT traversal (LAN -> DIRECT -> STUN -> UPnP -> ICE -> TURN). Connect codes hide raw IPs. Presence service (Ed25519 v2). Voice (libopus, optional). Listen-host is the current shipping target; dedicated server deferred.

---

## What it is

The networking subsystem covers transport, protocol, NAT traversal, presence, voice, and the peer-to-peer orchestrator. Server-authoritative model: one peer is the host; all gameplay state flows through it.

Code:

- Transport + connection: [port/src/net/net.c](../../port/src/net/net.c) (2864 lines).
- Wire protocol: [port/src/net/netmsg.c](../../port/src/net/netmsg.c), `port/include/net/netmsg.h`.
- Wire buffer: [port/src/net/netbuf.c](../../port/src/net/netbuf.c).
- P2P orchestrator: [port/src/net/p2p.c](../../port/src/net/p2p.c) (370 lines).
- Tier modules: `p2p_lan.c`, `p2p_direct.c`, `p2p_stun.c`, `p2p_upnp.c`, `p2p_ice.c`, `p2p_turn.c`.
- Hole punch (legacy): [port/src/net/netholepunch.c](../../port/src/net/netholepunch.c).
- STUN: [port/src/net/netstun.c](../../port/src/net/netstun.c).
- UPnP: [port/src/net/netupnp.c](../../port/src/net/netupnp.c).
- Presence: [port/src/presence.c](../../port/src/presence.c).
- Voice: [port/src/voice.c](../../port/src/voice.c) (gated on `HAVE_OPUS`).
- Connect codes: [port/src/connectcode.c](../../port/src/connectcode.c).
- Network manifest + distribution: see [pillars/modding.md](modding.md).

---

## Wire protocol

`NET_PROTOCOL_VER 48` at [port/include/net/net.h:12](../../port/include/net/net.h:12). The header carries an in-source changelog from v27 through v48. The version is pinned by [tests/test_versions.cpp:46](../../tests/test_versions.cpp:46) (`g_TestExpectedNetProtocolVer`) which reads the live header.

Mixed-version play is rejected at the ENet auth handshake ([port/src/net/net.c:1560](../../port/src/net/net.c:1560) `enet_peer_disconnect(peer, DISCONNECT_VERSION)`) and at the presence-channel proto check ([port/src/group_session.c:212](../../port/src/group_session.c:212)).

### Recent bumps (full changelog in `net.h:12-150`)

| Bump | What changed |
|------|--------------|
| **v48 (c3807 Track 2d, 2026-05-16)** | `SVC_GPUSWARM_STATE 0x6c` -- listen-host-only Mode B broadcasts the GPU swarm state texture to all peers at 10 Hz; 20-byte packed_bot quantization (pos/vel s16 cm, surface_up s8 ratio, AI ints exact); 4 chunks of 1024 bots over the unreliable channel at SWARM_GPU_MAX = 4096. Receiver dequantizes via `swarmGpuApplyRemoteState` and skips local compute on `g_NetMode == NETMODE_CLIENT`. v1 limitations tracked in B-333. |
| v47 (c3738 Slice 3, 2026-05-14) | Skedar surface-normal locomotion MP sync -- `chr->surface_up` over the wire so remote clients tilt bots correctly along wall/ceiling surfaces |
| **v46 (S507/S511, 2026-04-28)** | Mandatory SHA-256 digest on `SVC_DISTRIB_BEGIN`; cutscene network semantics (`SVC_CUTSCENE active+player_mask`, `CLC_CUTSCENE_SKIP 0x17`) |
| v45 (S482-S483, 2026-04-27) | Spawn-weapon mode wire fields (`SPAWNWEAPON_MODE_*`, `SPAWNWEAPON_FIESTA_SENTINEL=0xFE`) |
| v44 (S468, 2026-04-26) | Hygiene bump alongside `MPSETUP_VERSION 1->2` for Goldfinger weapon + AllInOne arena cull |
| v43 (S458, 2026-04-25) | `SVC_PLAYER_STATS` gains `attacker_id` field (B-256 fix) |
| v42 | Spectator: `CLC_SPECTATE_REQUEST 0x16`, `SVC_SPECTATE_ACK 0x6a`, `SVC_STATE_FRAME 0x6b` at 10Hz |
| v41 | `SVC_ACHIEVEMENT_TOAST 0x69` |
| v39 | `ADMIN_RESP_RATE_LIMIT 0x06` |
| v38 | SEC-7 server-info challenge handshake; MASTER-C3 cookie reconnect; SEC-14 room passwords; admin RCON |
| v37 (S324, 2026-04-17) | B-12 Phase 3: `chrslots` removed, encoded as derived active-slot mask |
| v35 (S241) | L5 match lifecycle: `match_seed`, co-op manifest, `CLC_STAGE_READY` |
| v32 | Scenario identity uses catalog ID string on wire |
| v31 | `SVC_PROP_SPAWN modelnum` uses catalog session refs |
| v30 | Weapon identity uses catalog session refs (u16) |
| v27 | All `net_hash` removed from wire |

**No integer asset identity may appear on the wire.** Weapons and models use catalog session u16 refs; scenario uses catalog ID string. (Constraint, since 2026-04-02.)

### Listen vs dedicated mode

- **Listen host** (`g_NetDedicated == 0`): server runs inside the game client process; local player occupies a slot. Current shipping target per S486.
- **Dedicated** (`g_NetDedicated == 1`, `g_NetLocalClient == NULL`): standalone `PerfectDarkServer.exe`; no local player; ROM/mod check skipped at `CLC_AUTH`. Buildable as tooling/regression coverage; in-client work has priority per [constraints.md](../constraints.md) S486.

`netmsg.c:721` checks `g_NetDedicated` for ROM/mod skip. Bot authority at `netmsg.c:7298`. Listen-only server tick at `netmsg.c:7242`.

---

## P2P NAT traversal (6-tier orchestrator)

[port/src/net/p2p.c](../../port/src/net/p2p.c) (370 lines). 64-entry pair table (`P2P_MAX_PAIRS`, line 24). `p2pPairBegin` (line 223) allocates a slot, stores hint IP/port, calls `enterTier(p, P2P_TIER_LAN)` (line 256). Per-tier soft timeout via `P2P_TIER_TIMEOUT_MS`. `p2pTick` (line 182) polls all six tier polls; `escalate` (line 136) advances on overage; exhaustion sets `P2P_PAIR_FAILED` (line 146). Late-success accepted (line 333). Diag API (`p2pPairDiag`) at line 370.

Escalation chain:

| Tier | Module | Wire / port | What it does |
|------|--------|-------------|--------------|
| 0 LAN | [p2p_lan.c](../../port/src/net/p2p_lan.c) | UDP broadcast 27101, 32-byte PDLAN | LAN discovery; 64-entry cache, 15s TTL, BYE on shutdown |
| 1 DIRECT | [p2p_direct.c](../../port/src/net/p2p_direct.c) | 16-byte PDDIR probe | Direct-IP probe with ACK reflection; nonce-matched |
| 2 STUN | [p2p_stun.c](../../port/src/net/p2p_stun.c) + [netstun.c](../../port/src/net/netstun.c) | RFC 5389 | XOR-MAPPED-ADDRESS + MAPPED-ADDRESS fallback; two-probe NAT type detection (cone vs symmetric); Google + Cloudflare server list |
| 3 UPnP | [p2p_upnp.c](../../port/src/net/p2p_upnp.c) + [netupnp.c](../../port/src/net/netupnp.c) | miniupnpc | Background thread; HTTP fallback via libcurl against api.ipify.org; quit-guard skips slow `UPNP_DeletePortMapping` |
| 4 ICE | [p2p_ice.c](../../port/src/net/p2p_ice.c) | 16-byte PDICE | Two local candidates (default NIC + STUN reflexive); parallel probes |
| 5 TURN | [p2p_turn.c](../../port/src/net/p2p_turn.c) | Player-hosted relay | Selects highest-kbps candidate; not RFC 5766 |

**Legacy hole-punch path** ([netholepunch.c](../../port/src/net/netholepunch.c)) runs in parallel: `DIRECT -> PUNCH -> PUNCH_ENET` waterfall using the ENet socket directly. Predates the tier machine, handles ENet connection phase after NAT piercing.

---

## Presence

[port/src/presence.c](../../port/src/presence.c). Always-on UDP socket port 27105. 184-byte frames signed Ed25519 over first 120 bytes plus domain string `"pd-presence-v2"` (lines 64-70). v2 added pubkey + signature on 2026-04-25.

Ping interval 30s, online window 60s. Friends pinged via social friend list iteration. **No external presence service**; no lobby browser, no HTTPS matchmaking.

---

## Voice

[port/src/voice.c](../../port/src/voice.c). Gated on `HAVE_OPUS` build flag. UDP port 27108. Frame Ed25519 signed over `"pd-voice-v1"`.

When undefined: module is no-op shell with state, settings, PTT toggle, UI hooks but no encode/decode/wire.

When defined: SDL capture device 16 kHz mono S16, per-peer `OpusDecoder`, friend allowlist + handle binding + per-friend mute.

`HAVE_OPUS` is currently set manually via `pacman -S` per a comment at [voice.c:16-19](../../port/src/voice.c:16). Build script does not auto-detect; gap noted in [audits/infrastructure-pillars-status-2026-04-27.md](../audits/infrastructure-pillars-status-2026-04-27.md) Section 5.

---

## Connect codes

[port/src/connectcode.c](../../port/src/connectcode.c). 256-entry word dictionary. 4-slot or 6-slot sentence form (with port). Used to share server addresses without exposing raw IPs.

**Byte-order convention**: `connectCodeEncode / connectCodeDecode` use host byte order (little-endian on Windows), NOT network byte order. Both sides use the same convention so round-trips are consistent. Do not apply `htonl()` before passing to these functions. (Constraint, header comments corrected 2026-04-28.)

**No raw IP in any UI surface** (constraint, since 2026-04-28). Players join via 4-word or 6-word sentence connect codes only. `g_NetLastJoinAddr` and `g_NetRecentServers` store raw IPs internally but must never be exposed in UI. Server history must encode stored IPs back to connect codes for display.

Static test at [tests/test_connectcode.cpp:193-266](../../tests/test_connectcode.cpp:193) reads source files via `std::ifstream` and verifies UI surfaces use `connectCodeDecodeWithPort`, "Connect Code:" present, "Enter IP:port" labels absent.

---

## Wire buffer (`netbuf`)

[port/src/net/netbuf.c](../../port/src/net/netbuf.c). Cursor-based buffer, multi-byte writes via `PD_LE16/LE32/LE64`, error-sticky flag, length-prefixed strings, zero-length safe static empty string return.

Hardened for malformed input (S575): zero-length wire strings return safe empty string instead of pointer into following payload. Unterminated wire strings rejected without mutating inbound packet payload. See [bugs.md](../bugs.md) entries B-280+ for the recent malformed-packet hardening pass.

---

## Server-authoritative discipline

`g_NetMode == NETMODE_SERVER` is the authority gate at [port/src/net/netmsg.c](../../port/src/net/netmsg.c). [tests/test_net_lifecycle_static.cpp](../../tests/test_net_lifecycle_static.cpp) verifies parse-before-commit ordering on every sensitive handler:

- `CLC_LOBBY_START` (B-272): authority check before payload commit
- `CLC_AUTH` (B-285): malformed local-player count rejected
- `CLC_BOT_MOVE` (B-287): impossible record counts rejected
- `CLC_MOVE` (B-290): parse-error returns before state writes
- `CLC_MANIFEST_STATUS` (B-286): unknown status + hash mismatch rejected
- Chat rate-limit, room rate-limit, desync threshold

This static-test discipline catches the "trust client byte before validating" class of bugs (10+ slices in S575/S581).

---

## Active invariants

Per [constraints.md](../constraints.md):

- **ENet protocol version v46** must match across clients.
- **Server is not a player.** Dedicated server sets `g_NetLocalClient = NULL` and `g_NetNumClients = 0` at startup; slot 0 free for real players. All paths that dereference `g_NetLocalClient` must NULL-guard.
- **No raw IP in any UI surface.** Connect codes only.
- **Connect code byte order** is host-order, not network-order.
- **MAX_LOCAL_PLAYERS = 4**, **MAX_PLAYERS = 8** (includes remote).
- **60 Hz tick rate.** Network sync frequencies are multiples.
- **Identity cookie is the reconnect authority** (MASTER-C3, S393): 16-byte server-issued cookie at first `CLC_AUTH`. Reconnect requires both name and cookie match in constant time.
- **Persistent bans live in `$S/bans.ini`** (MASTER-C2c/d). `serverBansInit` runs once at server start; `netServerEvConnect` checks `serverBansIsBanned(ip)` before any client slot allocation.
- **Admin RCON token is hashed** (MASTER-C2a). Domain-salted SHA-256, never stored as plaintext.
- **Room passwords are hashed at create time** (SEC-14). Domain-salted SHA-256.
- **Spawn-weapon Random/Fiesta pool sources from match-manifest** (S483). See [pillars/catalog.md](catalog.md).
- **Layer transition flushes must include declared shared actions** (S489). See [pillars/input.md](input.md).
- **`manifestClear` before `mainChangeToStage` during MP teardown** (SP-13). See [systemic-bugs.md](../systemic-bugs.md).
- **Room-bound server state must be cleaned up on room teardown** (SP-14). Ready gate, room-keyed state.

---

## What is done

Per [audits/infrastructure-pillars-status-2026-04-27.md](../audits/infrastructure-pillars-status-2026-04-27.md) Section 5:

- 6-tier escalation orchestrator (late-success handling, stale-failure filtering, per-tier accounting, diagnostic API).
- LAN tier complete (BYE, TTL eviction, version filtering, block-list integration).
- STUN RFC 5389 compliant (XOR-MAPPED-ADDRESS + fallback, two-probe NAT type, real public servers).
- UPnP background thread; quit-guard skips slow teardown.
- Protocol buffer is safe (overread, overwrite, malformed strings, error-sticky).
- Static tests verify parse-before-commit ordering on every sensitive handler.
- Connect codes hide raw IPs; static tests gatekeep UI regression.
- Dedicated server skips ROM/mod check at `g_NetDedicated` boundary.
- Voice gated on optional dependency with graceful no-op.
- Presence Ed25519 signed; v2 pubkey-bound.
- TURN selects by measured kbps from `groupSessionUpdateKbps`.

---

## What is in flight

- **Connectivity Phase 2 / Phase 3** per [designs/connectivity/connectivity-and-modern-main-menu.md](../designs/connectivity/connectivity-and-modern-main-menu.md). Phase 1 + 2 substantially shipped (presence, voice, social shell, NAT diagnostics, public mods registry, file transfer, listening rooms, theater); remaining items vary by sub-pillar.

---

## Known gaps

- **ICE peer candidate exchange not wired.** [p2p_ice.c:260](../../port/src/net/p2p_ice.c:260) `p2pIceAddPeerCandidate` exists but is never called from presence or invite layer in surveyed files. Without it, ICE tier probes only local NIC + local STUN reflexive against the hint, functionally equivalent to Tier 1 with backup.
- **STUN reflexive not transmitted to peer.** [p2p_stun.c:125-131](../../port/src/net/p2p_stun.c:125) reports success with local reflexive as endpoint but does not coordinate with remote. Real STUN-based hole punching needs both sides to know each other's reflexive. Presence packet has `listen_ipv4 / listen_port` fields but the bridge from `p2pPublishMyReflexive` to presence outbound is not visible.
- **kbps measurement is a placeholder.** `groupSessionRecomputeAuthority` at [group_session.c:136](../../port/src/group_session.c:136) comments `/* placeholder for local kbps until measurement is wired */`. Local kbps initialized to 0; first non-zero reporter wins authority.
- **UPnP only maps the ENet port.** [p2p_upnp.c:75-76](../../port/src/net/p2p_upnp.c:75) calls `netUpnpSetup(g_NetServerPort ? ... : NET_DEFAULT_PORT)`. Direct probe (27102), ICE (27103), TURN (27104) sockets remain unmapped. Tier 3 UPnP success is partial.
- **TURN has no public fallback.** [p2p_turn.c:211-213](../../port/src/net/p2p_turn.c:211) reports "no relay available" when `s_Cands` is empty. Brand-new server with no players in group session fails at tier 5.
- **netholepunch parallel to tier machine.** Two systems overlap. The handoff from `p2pPairGetEndpoint` to `enet_host_connect` is not visible in surveyed files. Unify.
- **Voice silent without `HAVE_OPUS`.** [voice.c:16-19](../../port/src/voice.c:16) documents the manual `pacman -S` step. Build script does not check or auto-set the flag. Detect Opus via CMake `find_package` and auto-set.

---

## Tests

`tests/test_netbuf`, `test_net_lifecycle_static`, `test_connectcode`, `test_manifest`. Pure-C mirror `manifest_pure.c` (drift-prone, see Known Gaps in [pillars/modding.md](modding.md)).

---

## Active design references

- [designs/connectivity/connectivity-and-modern-main-menu.md](../designs/connectivity/connectivity-and-modern-main-menu.md) - Phase 1 + Phase 2 design; substantially shipped per session log.
- [designs/connectivity/pd-server-plugin-abi.md](../designs/connectivity/pd-server-plugin-abi.md) - ADR for game-agnostic dedicated server (P4-A doc; P4-B/C deferred per S486).
- [designs/connectivity/interest-management-replication.md](../designs/connectivity/interest-management-replication.md) - P5-A scaling design draft.
- [designs/connectivity/hosting-modes-listen-vs-dedicated.md](../designs/connectivity/hosting-modes-listen-vs-dedicated.md) - threat model.
- [designs/connectivity/manifest-architecture.md](../designs/connectivity/manifest-architecture.md) - manifest lifecycle for all stage types (implemented).

---

## Where to look

- For mod distribution + manifest types + asset transfer: [pillars/modding.md](modding.md).
- For listen-vs-dedicated server semantics: [pillars/server.md](server.md).
- For wire/save format versioning: [pillars/save-wire-format.md](save-wire-format.md).
- For lobby/room/match menu UX: [pillars/menus.md](menus.md).
- For catalog identity on the wire: [pillars/catalog.md](catalog.md).
