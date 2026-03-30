# NAT Traversal Architecture: STUN + UDP Hole Punch

**Status**: Design (not yet implemented)
**Date**: 2026-03-30
**Dependencies**: ENet, miniupnpc (existing), `port/src/net/net.c`, `port/src/net/netupnp.c`, `port/src/connectcode.c`, `port/src/phonetic.c`

---

## Problem Statement

The current NAT traversal story is UPnP-only (`netUpnpSetup()` in `netStartServer()`). UPnP works on roughly 40–50% of home networks. Players on the other 50% must manually port-forward or use Hamachi (`--bind`). This is a high barrier.

**Goal**: Add STUN-based external address discovery and UDP hole punching as a fallback layer, so most players (targeting ~90%) can host without any router configuration.

**Non-goals**: No relay server (TURN). Symmetric NAT (~5–10% of hosts) is an accepted failure case. No IPv6-specific hole punching (IPv6 needs no NAT traversal anyway).

---

## Architecture Overview

```
SERVER STARTUP                              CLIENT CONNECTION
─────────────                              ────────────────
netStartServer()                           User enters connect code
      │                                           │
      ├─ netUpnpSetup()  ── background thread     │ connectCodeDecode()
      │       │                                   │      │
      │       └─ s_ExternalIP (UPnP)              │   ip:port
      │                                           │
      ├─ stunStartDiscovery()  ── background       │
      │       │                                   │
      │       └─ s_StunExternalIP:port            │
      │                                           │
      ├─ bestAddress = pick(UPnP > STUN > LAN)    │
      │                                           │
      └─ connect code = connectCodeEncode(best)   │
                                                  │
 Client query response includes best address ─────┘
                                                  │
                               netStartClientWithHolePunch(addr)
                                                  │
                                    ┌─────────────┴──────────────┐
                                    │  Step 1: direct connect     │
                                    │  enet_host_connect(addr)    │
                                    └─────────────┬──────────────┘
                                                  │ timeout (3s)
                                    ┌─────────────┴──────────────┐
                                    │  Step 2: hole punch         │
                                    │  send PUNCH_REQ → server    │
                                    │  server sends probes back   │
                                    │  retry enet_host_connect    │
                                    └─────────────┬──────────────┘
                                                  │ timeout (5s)
                                    ┌─────────────┴──────────────┐
                                    │  Step 3: failure + message  │
                                    └────────────────────────────┘
```

---

## Module 1: STUN Client (`port/src/net/stun.c`)

### RFC 5389 Minimal Subset

Only the Binding Request / Binding Response exchange is needed. No authentication, no TURN, no ICE.

**STUN Binding Request wire format (28 bytes):**
```
 0               1               2               3
 0 1 2 3 4 5 6 7 0 1 2 3 4 5 6 7 0 1 2 3 4 5 6 7 0 1 2 3 4 5 6 7
├───────────────────────────────┼───────────────────────────────┤
│  Message Type: 0x0001         │  Message Length: 0x0000       │
├───────────────────────────────┴───────────────────────────────┤
│                 Magic Cookie: 0x2112A442                       │
├───────────────────────────────────────────────────────────────┤
│                                                               │
│              Transaction ID (96 bits / 12 bytes)              │
│                                                               │
└───────────────────────────────────────────────────────────────┘
```

**STUN Binding Success Response parses:**
- Attribute `XOR-MAPPED-ADDRESS` (type `0x0020`): XOR'd with magic cookie
- Attribute `MAPPED-ADDRESS` (type `0x0001`): fallback for older STUN servers

The XOR-MAPPED-ADDRESS decode:
```c
uint32_t xor_ip   = ntohl(attr_value.addr) ^ 0x2112A442;
uint16_t xor_port = ntohs(attr_value.port) ^ 0x2112;
```

### STUN Server List

```c
// port/include/net/netstun.h
#define STUN_MAX_SERVERS 8
static const char *k_StunServers[] = {
    "stun.l.google.com:19302",
    "stun1.l.google.com:19302",
    "stun.cloudflare.com:3478",
    "stun.nextcloud.com:3478",
    NULL
};
```

This list lives in `netstun.c` and is configurable via `pd.ini` (`stun_server_0` through `stun_server_3`). Future self-hosting drops in a custom entry.

### State Machine

```
STUN_IDLE
    │  stunStartDiscovery(port)
    ▼
STUN_RESOLVING          ← DNS resolution of first server (non-blocking via ENet)
    │  address ready
    ▼
STUN_SENDING            ← raw UDP socket, send Binding Request
    │  packet sent
    ▼
STUN_WAITING            ← polling for response, 1-second timeout per server
    │  response received
    ▼
STUN_SUCCESS            ← s_StunExternalIP:s_StunExternalPort populated

    │  all servers timed out
    ▼
STUN_FAILED
```

### Non-Blocking Design

STUN runs on a background thread, identical pattern to `upnpWorkerThread` in `netupnp.c`.

```c
// Thread signature mirrors UPnP:
#ifdef _WIN32
static DWORD WINAPI stunWorkerThread(LPVOID param)
#else
static void *stunWorkerThread(void *param)
#endif

// Thread uses its own raw UDP socket (not ENet's socket):
SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
// Bind to the server's port so the NAT sees the right local port.
// This is critical: the STUN-discovered external port must match
// the port ENet is listening on.
struct sockaddr_in local = { .sin_family = AF_INET, .sin_port = htons(s_BindPort) };
bind(sock, (struct sockaddr*)&local, sizeof(local));
```

**Why bind to the server's port?** NAT devices map (internal_ip, internal_port) → (external_ip, external_port). If the STUN socket binds to a different port than ENet, the discovered external port is wrong and hole punching won't work. The STUN socket must share the same local port as the ENet host.

**Problem**: ENet already owns that port. The STUN probe must happen *before* `enet_host_create()` binds it, or use `SO_REUSEPORT`/`SO_REUSEADDR`.

**Solution for server STUN** (discover before clients arrive):
- Call `stunStartDiscovery(port)` inside `netStartServer()`, immediately after `netUpnpSetup()`.
- The STUN thread opens a temporary UDP socket on the server port with `SO_REUSEADDR`, does its probe, then closes it.
- ENet's socket is created *after* STUN completes — but STUN runs async, so ENet is created immediately too. Both bind `SO_REUSEADDR`. The NAT entry from the STUN probe is what matters; the actual ENet socket will get the same external mapping.

**Solution for client STUN** (client discovering its own address for hole punch):
- Client creates a raw UDP socket on ephemeral port, probes STUN.
- Result tells client its external address.
- This external address is sent to the server in the `PUNCH_REQ` connectionless message.

### NAT Type Detection (Simplified)

Full RFC 3489 classification requires multiple probes to multiple addresses. We only need to distinguish:

**Cone NAT** (Full Cone, Restricted Cone, Port-Restricted Cone): The external (IP, port) is the same regardless of which remote server we talk to. Hole punching works.

**Symmetric NAT**: Different external ports for each destination. Hole punching will fail.

**Detection procedure** (2 probes, both sent in the STUN thread):
```
Probe 1: Binding Request → STUN server A → external_addr_1
Probe 2: Binding Request → STUN server B → external_addr_2

if external_addr_1.port == external_addr_2.port:
    NAT type = cone (hole punch viable)
else:
    NAT type = symmetric (hole punch will fail, report early)
```

This adds ~1 extra STUN round-trip but is worth doing — we can warn the user before they waste 8 seconds timing out.

### Public API (`port/include/net/netstun.h`)

```c
#ifndef _IN_NETSTUN_H
#define _IN_NETSTUN_H
#include "types.h"

#define STUN_STATUS_IDLE      0
#define STUN_STATUS_WORKING   1
#define STUN_STATUS_SUCCESS   2
#define STUN_STATUS_FAILED    3

#define STUN_NAT_UNKNOWN      0
#define STUN_NAT_CONE         1   // hole punch viable
#define STUN_NAT_SYMMETRIC    2   // hole punch will fail

/* Start async STUN discovery using the given local port.
   Returns immediately. Poll stunGetStatus(). */
s32  stunStartDiscovery(u16 localport);

/* Current state: IDLE / WORKING / SUCCESS / FAILED */
s32  stunGetStatus(void);

/* NAT type detected during discovery (valid when SUCCESS or after 2 probes) */
s32  stunGetNatType(void);

/* Discovered external IP string, e.g. "203.0.113.5" (empty if not ready) */
const char *stunGetExternalIP(void);

/* Discovered external port (0 if not ready) */
u16  stunGetExternalPort(void);

/* Cancel any in-progress discovery */
void stunCancel(void);

#endif
```

### Estimated Size: ~300 lines

---

## Module 2: External Address Advertising

### Address Priority

The server picks the "best" address to advertise in this order:

```c
// In netStartServer(), polled during the lobby tick:
const char *netGetBestExternalAddr(char *buf, s32 bufsize) {
    // Priority 1: UPnP succeeded — has real external IP + mapped port
    if (netUpnpIsActive() && netUpnpGetExternalIP()[0]) {
        snprintf(buf, bufsize, "%s:%u", netUpnpGetExternalIP(), g_NetServerPort);
        return buf;
    }
    // Priority 2: STUN succeeded — external IP:port pair
    if (stunGetStatus() == STUN_STATUS_SUCCESS) {
        snprintf(buf, bufsize, "%s:%u", stunGetExternalIP(), stunGetExternalPort());
        return buf;
    }
    // Priority 3: LAN address (current behavior — works for LAN play)
    // Obtained from miniupnpc's s_LanAddr (available even when UPnP port mapping fails)
    // or from ENet's local address
    snprintf(buf, bufsize, "<lan>:%u", g_NetServerPort);
    return buf;
}
```

Note: The LAN fallback is the existing behavior. `s_LanAddr` from `netupnp.c` is already populated by `UPNP_GetValidIGD()` even when port mapping fails.

### Connect Code Generation

Currently in `netupnp.c:upnpWorkerThread()`:
```c
connectCodeEncode(ipAddr, code, sizeof(code));
sysLogPrintf(LOG_NOTE, "UPNP: Connect code: %s", code);
```

This needs to be centralized into a `netUpdateConnectCode()` function called from the lobby tick, which picks the best address and re-generates the connect code. The connect code is displayed in the lobby overlay.

**Constraint reminder**: `connectCodeEncode()` takes host byte order IP (little-endian on Windows). The default port (27100) is assumed. If the external port is non-standard (UPnP mapped to a different port, or STUN port differs), we need to embed the port. The phonetic system (`phonetic.c`) already handles IP + port in 48 bits; the sentence system (`connectcode.c`) does not. See "Connect Code Integration" section below.

### Query Response Extension

`netServerQueryResponse()` currently does not include the server's external address. Add a field:

```c
// In netServerQueryResponse(), after existing fields:
char externalAddr[NET_MAX_ADDR] = {0};
netGetBestExternalAddr(externalAddr, sizeof(externalAddr));
netbufWriteStr(&buf, externalAddr); // "" if unknown
```

**Protocol version bump needed**: This new field must be guarded by protocol version, or appended at the end so old clients ignore it. Bump `NET_PROTOCOL_VER` to 23 when this is implemented.

### Estimated Size: ~100 lines (spread across existing files)

---

## Module 3: Hole Punch Protocol

### Overview

Hole punching requires *both* peers to send UDP packets to each other's external address at approximately the same time. This is "simultaneous open."

The challenge: ENet does not directly support hole punching. The standard `enet_host_connect()` sends SYN packets to the server's address, but if the NAT hasn't been punched yet, those packets are dropped.

**Approach**: Use a short pre-connection handshake over raw UDP (connectionless, not through ENet), then initiate the ENet connection once both holes are open.

### Wire Protocol

Two new connectionless message types, distinct from `NET_QUERY_MAGIC`:

```
PUNCH_REQ magic: "PDPH\x01"   (5 bytes)
PUNCH_ACK magic: "PDPH\x02"   (5 bytes)
```

**PUNCH_REQ** (client → server, sent to server's external address):
```
[5]  magic "PDPH\x01"
[4]  u32   client_external_ip    (network byte order)
[2]  u16   client_external_port  (network byte order)
[4]  u32   protocol_ver          (NET_PROTOCOL_VER, for version check)
```

**PUNCH_ACK** (server → client external address):
```
[5]  magic "PDPH\x02"
[4]  u32   server_lan_ip         (network byte order, for fallback info)
[2]  u16   server_port
[1]  u8    nat_viable            (0 = server can't reach client, 1 = ACK reached client)
```

The server, upon receiving `PUNCH_REQ`:
1. Parses client's external address from the packet payload.
2. Sends 3 `PUNCH_ACK` packets to `client_external_ip:client_external_port` at 50ms intervals.
3. These packets punch a hole in the client's NAT.
4. The ENet SYN from the client then passes through.

The client sends `PUNCH_REQ`, waits up to 500ms for any `PUNCH_ACK`, then immediately fires `enet_host_connect()`.

### Hole Punch Sequence Diagram

```
CLIENT                          SERVER
  │                               │
  │  stunStartDiscovery(0)        │
  │  (discover client ext addr)   │
  │                               │
  │──── PUNCH_REQ ───────────────▶│  (sent to server's known address)
  │      ext_ip, ext_port         │
  │                               │  server learns client's external addr
  │                               │  from PUNCH_REQ *and* from the packet's
  │                               │  source address (which the OS fills in)
  │                               │
  │                               │──── PUNCH_ACK ───────────────▶ (to client ext addr)
  │                               │──── PUNCH_ACK ───────────────▶ (50ms later)
  │◀─── PUNCH_ACK ────────────────│──── PUNCH_ACK ───────────────▶ (100ms later)
  │                               │
  │  both NAT holes open          │
  │                               │
  │════ enet_host_connect() ════▶│
  │  (ENet SYN passes through)    │
  │                               │
  │◀════ ENET_EVENT_CONNECT ══════│
```

### Server-Side Handler

Extend `netServerConnectionlessPacket()` (already exists, handles `NET_QUERY_MAGIC`):

```c
static s32 netServerConnectionlessPacket(ENetEvent *event, ENetAddress *address,
                                          u8 *rxdata, s32 rxlen)
{
    if (rxdata && rxlen >= 5) {
        if (!memcmp(rxdata, NET_QUERY_MAGIC, 5)) {
            netServerQueryResponse(address);
            return 1;
        }
        if (!memcmp(rxdata, "PDPH\x01", 5)) {      // PUNCH_REQ
            netServerHandlePunchReq(address, rxdata, rxlen);
            return 1;
        }
    }
    return 0;
}
```

`netServerHandlePunchReq()` in `netstun.c` (or a new `netholepunch.c`):
```c
static void netServerHandlePunchReq(ENetAddress *srcAddr, u8 *data, s32 len) {
    // The source address of the incoming packet IS the client's external address.
    // The payload also contains it, but the socket-level source is more reliable
    // (no NAT-rewriting concerns, payload might be from a misbehaving client).
    // Use the socket-level source as the authoritative client external address.

    // Build PUNCH_ACK
    u8 ack[12];
    memcpy(ack, "PDPH\x02", 5);
    // ... fill server lan ip, port, nat_viable=1

    ENetBuffer buf = { .data = ack, .dataLength = sizeof(ack) };
    // Send 3 times, 50ms apart, to punch the client's NAT
    for (int i = 0; i < 3; i++) {
        enet_socket_send(g_NetHost->socket, srcAddr, &buf, 1);
        // 50ms sleep — acceptable on server, hole punch window is short
        SDL_Delay(50);
    }
    sysLogPrintf(LOG_NOTE, "NET: sent PUNCH_ACK x3 to %s", netFormatAddr(srcAddr));
}
```

**Note**: SDL_Delay in a server packet handler is acceptable here because:
1. The 150ms total is a tiny fraction of the server's frame time budget.
2. This is a connectionless pre-auth path, not in the main game loop.
3. It runs at most once per incoming client, not every frame.

### Client-Side Hole Punch Logic

New function `netStartClientWithHolePunch(const char *addr)` wraps `netStartClient()`:

```c
s32 netStartClientWithHolePunch(const char *addr) {
    // Step 1: Try direct connection (current behavior, 3s timeout)
    s32 ret = netStartClient(addr);
    if (ret != 0) return ret;

    // The connection timeout is handled in netStartFrame/netEndFrame via ENet events.
    // The UI shows "Connecting..." during the timeout.
    // If ENET_EVENT_CONNECT arrives, we're done.
    // If the connection times out (no event for 3s), the game loop calls
    // netStartClientHolePunch_phase2(addr).

    g_NetHolePunchAddr = addr;  // save for phase 2
    g_NetHolePunchPhase = 1;
    g_NetHolePunchStartMs = SDL_GetTicks();
    return 0;
}
```

The waterfall is driven by the existing `netStartFrame()` event loop, not a blocking wait. This avoids any game loop stalls.

### Estimated Size: ~250 lines (stun.c extensions + netholepunch.c)

---

## Module 4: Connect Code Integration

### Current State

- `connectcode.c` / `connectCodeEncode(u32 ip)`: 4-word sentence, IPv4 only, port hardcoded to 27100
- `phonetic.c` / `phoneticEncode(u32 ip, u16 port)`: consonant-vowel syllables, IPv4 + port (48 bits)
- UPnP logs the connect code but assumes port == 27100
- Constraint: "No raw IP in any UI surface" — connect codes are the sole sharing mechanism

### Problem

If UPnP maps the server to an external port other than 27100 (very common — routers pick an available port), or if STUN discovers a non-27100 external port, the connect code must carry the port. The `connectcode.c` sentence system cannot encode port.

### Options

| Option | Pros | Cons |
|--------|------|------|
| Always use port 27100, reject non-standard UPnP mappings | Simple | Fragile — many routers won't map to 27100 if it's taken |
| Use phonetic system (IP+port in 48 bits) | Handles any port | Less human-readable ("BALE-GIFE-NOME-RIVA") |
| Extend sentence system to include port hints | Familiar format | Complex decode, longer strings |
| Use phonetic for non-default ports, sentence for default | Best of both | Two formats to maintain |

**Recommendation**: Adopt the phonetic system as the primary connect code format going forward. It already encodes IP+port in 19 characters (`BALE-GIFE-NOME-RIVA`). The sentence system becomes legacy. This is a constraint-compatible change since the constraint says "no raw IP in UI," not "use sentence codes specifically."

**Migration plan**:
- Phase 1 (STUN PR): Use phonetic system for STUN/UPnP codes when port != 27100, sentence system otherwise.
- Phase 2 (future): Migrate all code generation to phonetic. Update UI decode to try both.

### NAT Traversal Flag in Connect Code

Consider encoding a "hole punch needed" flag. The phonetic system uses 48 bits for IP+port; there are no spare bits. Options:
- Use a special prefix character before the phonetic code: `H:BALE-GIFE-NOME-RIVA` means "hole punch required"
- The `H:` prefix tells the client to go straight to Phase 2 (hole punch) instead of trying direct first

This is optional for v1 — the client can always try direct first and fall back.

### Code Changes

**`netupnp.c`** (`upnpWorkerThread`): Replace `connectCodeEncode()` call with a central function that picks encoding based on port:

```c
// Replace existing connect code log in upnpWorkerThread:
netUpdateConnectCode(ipAddr, (u16)atoi(s_MappedPort));
```

**New `netUpdateConnectCode(u32 ip, u16 port)` in `net.c`**:
```c
void netUpdateConnectCode(u32 ip, u16 port) {
    char code[256];
    if (port == NET_DEFAULT_PORT) {
        connectCodeEncode(ip, code, sizeof(code));
    } else {
        phoneticEncode(ip, port, code, sizeof(code));
    }
    sysLogPrintf(LOG_NOTE, "NET: connect code: %s", code);
    // Store in g_NetConnectCode for UI display
    strncpy(g_NetConnectCode, code, sizeof(g_NetConnectCode) - 1);
}
```

**`connectcode.h`**: Add `connectCodeDecodeWithPort(code, ip, port)` that also tries the phonetic decoder:
```c
s32 connectCodeDecodeAny(const char *code, u32 *outIp, u16 *outPort) {
    // Try sentence decode first (sets port to default)
    if (connectCodeDecode(code, outIp) == 0) {
        *outPort = NET_DEFAULT_PORT;
        return 0;
    }
    // Try phonetic decode (includes port)
    return phoneticDecode(code, outIp, outPort);
}
```

### Estimated Size: ~80 lines (new functions + changes to existing)

---

## Module 5: Connection Waterfall

### State Machine

Driven from the existing `netStartFrame()` / `netEndFrame()` event loop in `net.c`. No blocking anywhere.

```
State: CONN_DIRECT
  Entry: enet_host_connect(serverAddr)
  Timeout: 3000ms
  Success: ENET_EVENT_CONNECT → CONN_ESTABLISHED
  Timeout: → CONN_PUNCH_INIT (if STUN available) or CONN_FAILED

State: CONN_PUNCH_INIT
  Entry: stunStartDiscovery(localPort)  (discover client's external addr)
         Send PUNCH_REQ → serverAddr (raw UDP on a temporary socket)
  Timeout: 500ms for PUNCH_ACK or skip
  Success/Skip: → CONN_PUNCH_ENET

State: CONN_PUNCH_ENET
  Entry: netDisconnect() + enet_host_connect(serverAddr) (fresh attempt)
  Timeout: 5000ms (longer — NAT holes need time to stabilize)
  Success: ENET_EVENT_CONNECT → CONN_ESTABLISHED
  Timeout: → CONN_FAILED

State: CONN_ESTABLISHED
  Entry: existing game connection flow (auth, lobby, etc.)

State: CONN_FAILED
  Entry: show error with NAT type diagnostic
```

### Timeout Integration

The existing `netStartClient()` uses `enet_host_connect()` which starts ENet's internal connection retry loop. ENet's default timeout is controlled by `enet_peer_timeout()`. Currently the timeout is whatever ENet defaults to.

For Phase 1 (direct), set peer timeout to 3 seconds:
```c
// In netStartClient(), after enet_host_connect():
enet_peer_timeout(g_NetLocalClient->peer,
    ENET_PEER_TIMEOUT_LIMIT,    // 32 (retries)
    300,                         // minimum timeout: 300ms
    3000);                       // maximum timeout: 3s
```

For Phase 3 (post-punch), set timeout to 5 seconds to account for NAT entry stabilization.

### UI Integration

The connection flow state needs to feed the UI in `pdgui_menu_network.cpp` (or wherever the "connecting" screen lives). Add `netGetConnectionPhase()` → returns an enum with `CONN_DIRECT`, `CONN_PUNCHING`, `CONN_FAILED`. The UI displays:
- Phase 1: "Connecting..."
- Phase 2: "Trying NAT traversal..."
- Failed + symmetric NAT: "Could not connect. Your network uses carrier-grade NAT. Ask your ISP to enable port forwarding."
- Failed + cone NAT: "Could not connect. Check that the server is running."

### Estimated Size: ~150 lines (new connection state machine in net.c)

---

## Module 6: Server-Side Address Management

### Startup Flow

```
netStartServer(port)
    │
    ├── enet_host_create(...)     ← ENet socket opened on port
    │
    ├── netUpnpSetup(port)        ← existing: UPnP thread started
    │       └── (async, ~2-5s)
    │
    ├── stunStartDiscovery(port)  ← NEW: STUN thread started
    │       └── (async, ~1-2s)
    │
    └── return 0 (both run in background)
```

The server lobby overlay (in `server_gui.cpp` or wherever) polls both:
```c
// Called each frame from server GUI tick:
void serverUpdateAddressDisplay(void) {
    char addr[256];
    netGetBestExternalAddr(addr, sizeof(addr));
    if (strcmp(addr, s_LastAddr) != 0) {
        strncpy(s_LastAddr, addr, sizeof(s_LastAddr));
        netUpdateConnectCode(/* parse ip:port from addr */);
        // Update GUI display of connect code
    }
}
```

The connect code re-generates as better address info arrives:
- T+0s: LAN address (immediate)
- T+1-2s: STUN address replaces LAN (if UPnP still working)
- T+2-5s: UPnP address replaces STUN (if UPnP succeeds — UPnP is more reliable)

### STUN on Server Startup vs. Always-On

Two options:
1. **One-shot at startup** (simpler): Probe once when server starts. Store result. Never re-probe.
2. **Periodic re-probe** (robust): Re-probe every 5 minutes. Handles DHCP IP changes.

Recommendation: Start with one-shot (Phase 1), add periodic re-probe later if users report DHCP issues.

### Hole Punch Cooperation

The server needs to know client's external address to send hole-punch probes. Two sources:
1. **PUNCH_REQ payload**: Client sends its STUN-discovered external address explicitly.
2. **Packet source address**: The OS fills in the actual source address of the `PUNCH_REQ` packet. This is more reliable — the NAT will have already rewritten it to the client's external address.

Use source 2 (packet source) as authoritative, source 1 as a sanity check. If they don't match within reason (same IP, port within ±100), log a warning.

### Multiple Clients Behind Same NAT

If two clients share the same external IP but different external ports (hairpinning scenario):
- STUN will return the same external IP but different ports for each client.
- The server sends PUNCH_ACK to each distinct (ip:port) — they're different NAT sessions.
- ENet connects to each using their individual external ports.
- This works correctly as long as the NAT supports hairpinning (most do).

If the NAT does *not* support hairpinning (rare), these clients cannot connect to the same server via external address. They should connect via LAN address directly. Detecting this is complex — skip for v1, document as a known issue.

---

## Integration Points (Existing Code)

| Location | Change | Why |
|----------|--------|-----|
| `netStartServer()` in `net.c:502` | Add `stunStartDiscovery(port)` call after `netUpnpSetup(port)` | Start STUN async on server startup |
| `netServerConnectionlessPacket()` in `net.c:426` | Add `PUNCH_REQ` handler | Server-side hole punch entry point |
| `netServerQueryResponse()` in `net.c:375` | Add external address field | Clients can discover server's external addr via query |
| `upnpWorkerThread()` in `netupnp.c:48` | Replace direct `connectCodeEncode()` call with `netUpdateConnectCode()` | Centralized code generation |
| `netStartClient()` in `net.c:732` | Add peer timeout, hook connection waterfall state machine | Phase 2/3 fallback |
| `connectcode.h` | Add `connectCodeDecodeAny()` that tries both formats | Client can decode phonetic or sentence codes |
| `CMakeLists.txt` | Add `port/src/net/netstun.c`, `port/src/net/netholepunch.c` | New source files |

---

## Edge Cases and Failure Modes

### All STUN Servers Unreachable

If all STUN servers time out (e.g., firewall blocks UDP to port 3478/19302):
- `stunGetStatus()` returns `STUN_STATUS_FAILED`
- Server falls back to LAN address only
- Connect code encodes LAN IP (existing behavior)
- Hole punch is impossible — client attempts direct connection only
- Log: "STUN: all servers unreachable, NAT traversal unavailable"
- No crash, no hang — graceful degradation

### Symmetric NAT

Detected during STUN two-probe phase. `stunGetNatType()` returns `STUN_NAT_SYMMETRIC`.

Server-side: Log warning, don't try to advertise hole-punch path. Connect code still generated from external IP (even if hole punch won't work — the external IP might still be reachable if the firewall allows it).

Client-side: After Phase 1 timeout, check `stunGetNatType()`. If `STUN_NAT_SYMMETRIC`, skip Phase 2 and go directly to `CONN_FAILED` with a specific error message about symmetric NAT / CGNAT.

### External Address Changes Between STUN Probe and Connection

DHCP lease change between STUN discovery and client connection attempt. Extremely rare (minutes to hours between events). Accept as a failure case. The user can restart the server to get a fresh STUN probe.

### STUN Probe Races with ENet Socket

The STUN thread's temporary UDP socket and ENet's socket both want port 27100. With `SO_REUSEADDR`:
- Windows: Both sockets can bind the same port if `SO_REUSEADDR` is set on both.
- The STUN socket is short-lived (< 2 seconds). Race window is tiny.
- If there's a conflict, STUN probe fails gracefully and `STUN_STATUS_FAILED` is returned.
- Mitigation: Start STUN *before* `enet_host_create()`, close STUN socket before ENet creates its own.

**Revised startup order**:
```c
s32 netStartServer(u16 port, s32 maxclients) {
    netUpnpSetup(port);       // 1. UPnP thread (uses its own TCP sockets, no conflict)
    stunStartDiscovery(port); // 2. STUN thread (briefly binds port, then closes)
    // ... small delay or yield to let STUN thread bind first ...
    g_NetHost = enet_host_create(&g_NetLocalAddr, ...); // 3. ENet (SO_REUSEADDR)
```

Actually the simpler solution: STUN thread uses `SO_REUSEADDR` and probes with a very short timeout (500ms). ENet uses `SO_REUSEADDR` on its host socket. They can coexist.

### IPv6

ENet supports IPv6. STUN also works with IPv6. However:
- IPv6 hosts don't have NAT (typically). No hole punching needed.
- `connectCodeEncode()` and `phoneticEncode()` are IPv4-only (both encode 32-bit IP).
- For v1: IPv6 hosts can connect directly without hole punching. Skip STUN for IPv6 hosts.
- Detection: check if `g_NetLocalAddr` is IPv6 before starting STUN.

---

## Phased Implementation Plan

### Phase 1: STUN Discovery Only (~2-3 days)

Build just the server-side STUN discovery. No hole punching yet.

**Files added/changed**:
- `port/src/net/netstun.c` (new, ~300 lines)
- `port/include/net/netstun.h` (new, ~30 lines)
- `port/src/net/net.c` — add `stunStartDiscovery()` call in `netStartServer()`
- `port/src/net/netupnp.c` — add `netUpdateConnectCode()` call
- `port/src/connectcode.c` — add `connectCodeDecodeAny()`
- `CMakeLists.txt` — add netstun.c

**Result**: Server discovers its external address via STUN. Connect code encodes it. Clients who already have a clear path (UPnP working, or open ports) benefit from better address discovery. No change to client connection flow.

### Phase 2: Server-Side Query Advertising (~1 day)

**Files changed**:
- `port/src/net/net.c` — `netServerQueryResponse()` includes external address
- Bump `NET_PROTOCOL_VER` to 23

**Result**: `netQueryRecentServers()` returns the server's STUN-discovered external address in the query response. Recent server list benefits from correct external addresses.

### Phase 3: Client-Side Hole Punch (~3-4 days)

Build the connection waterfall and hole punch protocol.

**Files added/changed**:
- `port/src/net/netholepunch.c` (new, ~250 lines)
- `port/include/net/netholepunch.h` (new, ~30 lines)
- `port/src/net/net.c` — connection waterfall state machine in `netStartFrame()`
- `port/src/net/net.c` — `netServerConnectionlessPacket()` handles `PUNCH_REQ`
- UI layer — show connection phase state

**Result**: Full NAT traversal. ~90% of networks should work without port forwarding.

### Phase 4: NAT Type Diagnostics (~1 day)

**Files changed**:
- `port/src/net/netstun.c` — add 2-probe NAT type detection
- UI layer — specific error messages for symmetric NAT

**Result**: Users with symmetric NAT see a helpful message instead of a generic timeout.

---

## Module Summary

| Module | File | Lines | Phase |
|--------|------|-------|-------|
| STUN client | `port/src/net/netstun.c` | ~300 | 1 |
| STUN header | `port/include/net/netstun.h` | ~30 | 1 |
| Hole punch | `port/src/net/netholepunch.c` | ~250 | 3 |
| Hole punch header | `port/include/net/netholepunch.h` | ~30 | 3 |
| net.c changes | existing file | ~150 | 1+3 |
| connectcode.c changes | existing file | ~40 | 1 |
| netupnp.c changes | existing file | ~20 | 1 |
| UI changes | `pdgui_menu_network.cpp` or similar | ~60 | 3 |
| **Total** | | **~880 lines** | |

---

## Constraints Checklist

Before implementing, verify against `context/constraints.md`:

- [x] **No raw IP in UI**: STUN-discovered IPs are internal only. Connect code is the sole UI surface.
- [x] **Connect code byte order**: `connectCodeEncode()` / `phoneticEncode()` use host byte order. STUN gives network byte order — apply `ntohl()` before passing to encode functions.
- [x] **ENet protocol version**: Query response extension requires protocol bump to 23.
- [x] **Server is not a player**: `g_NetLocalClient` may be NULL on dedicated server. `netGetBestExternalAddr()` must not dereference it.
- [x] **bool is s32**: Use `s32` not `bool` for status fields in netstun.h.
- [x] **C11 game code**: netstun.c and netholepunch.c are port code — C11 types OK.
- [x] **Non-blocking**: STUN runs on background thread, same pattern as UPnP.
- [x] **Windows/MinGW**: Use `CreateThread` / `SOCKET` / `WSA*` APIs with `#ifdef _WIN32` guards, same as `netupnp.c`.

---

## Open Questions for Mike

1. **STUN server list**: Should the default list be configurable via `pd.ini`? (Recommendation: yes, via `stun_server_0` etc. with the Google/Cloudflare list as defaults.)

2. **Phonetic vs. sentence codes going forward**: OK to make phonetic primary for non-27100 ports, with sentence for 27100? Or should we migrate everything to phonetic for simplicity?

3. **Hole punch timeout values**: 3s for direct, 5s for post-punch — do these feel right for the user? (Can always tune.)

4. **STUN probes on client**: Should clients discover their own external address proactively when the game starts, or only when a connection attempt fails direct? Proactive is slightly better UX (no extra delay when hole punch is needed) but uses a background thread on client too.

5. **`SO_REUSEPORT` on Windows**: Available on Windows 10+. Since we target Windows 11 (MinGW), this is safe. But test to make sure ENet and the STUN socket don't conflict.
