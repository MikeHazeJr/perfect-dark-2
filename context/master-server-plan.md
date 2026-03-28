# Master Server / Server Pool — Design Plan

## Status: PLANNED (Phase D16)
Last updated: 2026-03-19

## Summary

A lightweight master server that maintains a live registry of dedicated game servers, enabling players to browse and join matches without knowing server IPs. Game servers register on startup and heartbeat periodically. Clients query the master for a server list, then connect directly to the chosen game server using the existing ENet protocol. The master is never in the gameplay data path.

## Why It Can Wait

The master server is **purely additive** — it introduces no changes to the existing networking, lobby, or message protocol layers. The interfaces it needs already exist or are trivially extensible:

- **Server side**: Game servers already have `g_NetServerPort`, `g_NetMaxClients`, `g_NetNumClients`, `g_NetGameMode`, and UPnP external IP discovery. Registration is just packaging these into a UDP packet.
- **Client side**: The `netRecentServer*` infrastructure (`netRecentServerGetCount`, `netRecentServerGetInfo`, server browser UI) already models a list of `{addr, flags, numclients, maxclients, online}`. The master query just becomes a new source for this same data.
- **Protocol**: No changes to CLC/SVC messages, ENet channels, or game state sync.

Building it later costs essentially zero rework.

## Architecture

```
                    ┌──────────────┐
                    │ Master Server │  (standalone process, ~500 LOC)
                    │  UDP :27099   │
                    └──────┬───────┘
                           │
              ┌────────────┼────────────┐
              │            │            │
        ┌─────┴─────┐ ┌───┴─────┐ ┌───┴─────┐
        │ Game Srv A │ │ Game Srv B│ │ Game Srv C│  (existing pd-server)
        │  :27100    │ │  :27100  │ │  :27100  │
        └─────┬─────┘ └────┬────┘ └────┬────┘
              │             │           │
              └──────┬──────┘           │
                     │                  │
              ┌──────┴──────┐    ┌──────┴──────┐
              │  Client 1   │    │  Client 2   │   (existing pd client)
              └─────────────┘    └─────────────┘

  Flow:
  1. Game server → Master: REGISTER (on startup), HEARTBEAT (every 15s)
  2. Client → Master: QUERY_SERVERS (browse request)
  3. Master → Client: SERVER_LIST (array of server info)
  4. Client → Game Server: direct ENet connect (existing protocol, unchanged)
```

## Master Protocol (new, simple UDP)

All messages are small UDP packets. No ENet needed — fire-and-forget with retry.

| Message | Direction | Payload |
|---------|-----------|---------|
| `MS_REGISTER` | Server → Master | protocol_ver, port, maxclients, gamemode, server_name[32], auth_token[16] |
| `MS_HEARTBEAT` | Server → Master | port, numclients, gamemode, lobby_state |
| `MS_UNREGISTER` | Server → Master | port, auth_token[16] |
| `MS_QUERY` | Client → Master | protocol_ver, filter_flags |
| `MS_SERVER_LIST` | Master → Client | count, array of {ip[4], port, numclients, maxclients, gamemode, name[32], ping_hint} |

## Implementation Phases

### D16a: Master Server Process (~300 LOC)
- New standalone C program: `master_server.c`
- UDP socket, listens on port 27099
- In-memory server registry (array of structs, max 256 servers)
- Handles REGISTER, HEARTBEAT, UNREGISTER, QUERY
- Ages out servers that miss 3 consecutive heartbeats (45s timeout)
- Simple auth: shared token in config file, prevents random registration
- No game logic, no ENet dependency, no SDL — pure sockets

### D16b: Game Server Registration (~100 LOC)
- Add to `server_main.c` main loop: on startup send MS_REGISTER, every 15s send MS_HEARTBEAT, on shutdown send MS_UNREGISTER
- Master server address from `--master` CLI arg or `server.cfg`
- Falls back gracefully if master is unreachable (server still works standalone)
- New file: `port/src/net/netmaster.c` + `port/include/net/netmaster.h`

### D16c: Client Server Browser (~200 LOC)
- Extend existing server browser UI (`pdgui_menu_network.cpp`)
- Add "Browse Servers" tab alongside manual IP entry and recent servers
- On open: send MS_QUERY to configured master address
- Parse MS_SERVER_LIST, populate same `netRecentServer`-style data model
- Player selects a server from list → existing join flow (ENet connect)
- Refresh button, sort by player count / ping / game mode

### D16d: Security & Polish
- Rate limiting on master (per-IP query throttle)
- Token rotation / simple HMAC for registration auth
- Optional: ping measurement (client sends UDP probe to each listed server)
- Optional: server tags/description for filtering

## Dependencies

- Requires: working dedicated server (done), working client join flow (done after current Phase 3)
- No dependency on: lobby system internals, CLC/SVC protocol, ImGui menu migration
- Blocked by: nothing — can be built anytime after Phase 3

## Estimated Effort

| Phase | LOC | Complexity |
|-------|-----|------------|
| D16a (master process) | ~300 | Low — just UDP socket + array |
| D16b (server registration) | ~100 | Low — periodic UDP send |
| D16c (client browser) | ~200 | Medium — UI integration |
| D16d (security/polish) | ~150 | Low-Medium |
| **Total** | **~750** | **Straightforward** |

## What to Keep In Mind Now

Nothing needs to change in current code to prepare for this. The only soft recommendation: when the server browser UI is finalized, keep the data model generic (address + metadata) rather than tightly coupling it to the recent-servers file format. The current `netRecentServerGetInfo` signature already does this well.
