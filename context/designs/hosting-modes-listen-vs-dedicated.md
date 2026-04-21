# Hosting modes: listen (in-client) vs dedicated (`PerfectDarkServer`)

**Status:** ADR (architecture + threat model)  
**Date:** 2026-04-20  
**Related:** [constraints.md](../constraints.md), [server-architecture.md](../server-architecture.md), [nat-traversal-architecture.md](nat-traversal-architecture.md)

---

## Modes

| Mode | Process | `g_NetDedicated` | Local player |
|------|---------|------------------|--------------|
| **Listen (in-client host)** | `PerfectDark.exe` runs ENet server + full game | `false` | Host is a normal client (`g_NetLocalClient` valid). |
| **Dedicated** | `PerfectDarkServer.exe` (headless or ImGui server GUI) | `true` | No local human player (`g_NetLocalClient == NULL`; slot 0 is for real clients). |

Both paths share the same wire protocol and most of `port/src/net/*`. Behavior diverges where the dedicated binary has no ROM, no gameplay tick, and no local match state.

---

## Threat model — ROM and mod agreement

### Listen server

On `CLC_AUTH`, the server validates the joining client’s **ROM CRC** (CRC32 of `g_RomName`) and **active mod directory** against the host’s loaded game **only when `!g_NetDedicated`**:

- Implementation: `netmsgClcAuthRead()` in `port/src/net/netmsg.c` (ROM + `fsGetModDir()` comparison).
- **Intent:** Prevent accidental mismatches (wrong region, wrong mod folder) from entering the lobby; the host’s files are the authority.

**Residual risks (listen):** A malicious host could still run a patched client; peers only see agreed wire content after catalog sync. ROM/mod checks are **consistency enforcement**, not DRM.

### Dedicated server (`PerfectDarkServer`)

Dedicated builds **do not load a consumer ROM** for gameplay. The same `CLC_AUTH` handler **skips** ROM and mod checks when `g_NetDedicated` is true (see constraints: *ROM/mod check skipped on dedicated server*).

- **Implication:** The dedicated process cannot cryptographically prove what ROM or mod folder a client is using at auth time. Agreement relies on:
  - **Catalog/session sync after auth** (clients pull what the match needs);
  - **Operator policy** (what mods are allowed on that server);
  - **Social/out-of-band** expectations (same mod list as friends).

**Threat:** A client could misreport or omit mod state in ways not caught at `CLC_AUTH` on dedicated; mitigations are operational (server rules, bans, future optional attestations), not the listen-host file compare.

### Bot authority (dedicated)

`SVC_BOT_AUTHORITY` is issued only in dedicated mode so one **human** client runs bot AI and sends `CLC_BOT_MOVE` (`netmsg.c`). In listen mode the host runs bots locally. From a threat perspective, the **designated bot-authority client** must be trusted for simulation of bots; a compromised authority client could send bad bot state (server should treat movement as untrusted input and rely on game rules where applicable).

---

## NAT, UPnP, STUN

- **Current:** Hosting uses ENet over UDP; **UPnP** is integrated for port mapping where the router supports it (`netUpnpSetup()` path — see `port/src/net/net.c`, `netupnp.c`).
- **Planned / design:** **STUN**-based external address discovery and **UDP hole punching** as a fallback when UPnP is unavailable — see [nat-traversal-architecture.md](nat-traversal-architecture.md) (design status: not all pieces may be implemented yet).

**Threat / abuse notes:**

- NAT traversal increases reachability; it does not authenticate peers. Pair with **room passwords**, **bans**, and **admin controls** for abuse handling.
- Third-party STUN servers learn client IP metadata when used; prefer configurable endpoints if privacy-sensitive deployments matter.

---

## Admin RCON

Admin commands use **`CLC_ADMIN` / `SVC_ADMIN`** on the wire (protocol includes admin RCON; see `context/constraints.md` ENet protocol notes).

- Tokens are configured at server start (`--admin-token` or `[Admin] Token` in server config); **plaintext is hashed** and not stored (constraint: *Admin RCON token is hashed*).
- **Threats:** Anyone who obtains the token can issue admin actions until rotation. Protect config files, process listings, and backups. Rate limiting exists for failed auth (`ADMIN_RESP_RATE_LIMIT` in protocol v39+).

Dedicated servers are the primary operators of long-lived RCON; listen hosts may expose a smaller surface but the same rules apply if admin is enabled.

---

## Connect codes vs raw IP (product constraint)

- **Constraint:** **No raw IP in any UI surface** — players share **four-word connect codes**; addresses are resolved internally (`connectCodeEncode` / `connectCodeDecode`).
- **Threat / privacy:** Prevents casual IP leakage in screenshots and streams. Internal storage (`g_NetLastJoinAddr`, recent servers) may still hold raw IPs — **must not** be shown in UI (see constraints).

This applies equally to joining a listen host or a dedicated server; the encoding hides the endpoint, not the trust relationship with the server operator.

---

## Client UI entry (H-1)

The game client’s **Multiplayer** menu (`pdgui_menu_network.cpp`) exposes **Host game (this PC)** — calls `netStartServer(port, maxclients)` with `g_NetDedicated == false` (same listen model as `--host` / `g_NetHostLatch`). After a successful start, the menu closes and `pdgui_lobby.cpp` routes **NETMODE_SERVER && !dedicated** through the same lobby/room overlays as `NETMODE_CLIENT` (social lobby → room). Room leader updates (`netSendRoomSettingsUpdate` / `netSendRoomPlaylistUpdate`) and **Leave room** use listen-specific paths when there is no ENet peer to self (`netmsg.c` / `netListenHostRoomLeave`).

---

## Summary

| Topic | Listen host | Dedicated |
|-------|-------------|-----------|
| ROM/mod check at `CLC_AUTH` | Yes (`!g_NetDedicated`) | Skipped (no host ROM) |
| Local player | Yes | No (`g_NetLocalClient` NULL) |
| Bot AI | Host runs locally | Delegated via `SVC_BOT_AUTHORITY` |
| RCON / bans | Optional | Typical for operators |

For file-level server layout and GUI, see [server-architecture.md](../server-architecture.md).
