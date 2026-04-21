# Resolution prompts — Super Audit 2026-04-21

Use these with Cursor / Claude against this repo. **Order matters** where noted; sections marked **Parallel OK** can run in any order relative to each other.

Finding IDs reference [`2026-04-21-full.md`](2026-04-21-full.md).

---

## Tier 0 — No prerequisites (do first; parallel OK)

These are tiny, safe guardrails and do not block other work.

### P0-A — Compile-time tripwire for participant wire mask (**H-2**, **M-1**)

```
Add a _Static_assert(MAX_PLAYERS + MAX_BOTS <= 64, "...") next to
mpParticipantsEncodeActiveMask / mpParticipantsDecodeActiveMask in
src/game/mplayer/participant.c (or port/include/pdgui_constants_check.c if
that's where similar checks live — match existing project pattern). Ensure
the message explains the u64 active-mask wire format in SVC_STAGE_START.
Build with MSYS headless per devtools/build-headless.ps1.
```

### P0-B — Clarify internal-only `net_hash` (**L-1**)

```
In port/src/assetcatalog.c and port/include/assetcatalog*.h, add a short comment
that net_hash is an internal catalog cache key only — never wire/save/public
API identity (constraints.md catalog-ID rules). No behavior change.
```

---

## Tier 1 — Server/network security (parallel OK among themselves)

Independent fixes; do **before** shipping a public **listen-host** flow if that host is internet-exposed, so brute-force and token issues are closed.

### P1-A — Admin auth brute-force + log hygiene (**S-1**, ties **AUDIT-M1** from 2026-04-20 delta)

```
In port/src/net/netmsg.c netmsgClcAdminRead, add per-client (and optionally per-IP)
rate limiting for ADMIN_SUB_AUTH: model on s_ChatRate / netmsgRoomRateAllow.
After N failures in a window: return ADMIN_RESP_RATE_LIMIT or disconnect;
optional temp ban hook. Reduce LOG_WARNING spam (rate-limit logs). Bump minimum
admin token length to 16 in server_admin.c with loud boot warning if below
entropy threshold. Follow constraints.md (NET_PROTOCOL_VER only if wire changes).
Read pd2-networking-protocol skill if touching wire enums.
```

### P1-B — CSPRNG for identity cookies (**S-2**)

```
Replace netServerIssueCookie in port/src/net/net.c: use BCryptGenRandom on
Windows and getrandom on Linux (or equivalent) for NET_AUTH_COOKIE_LEN bytes per
issue, or seed the rolling SHA context once at boot with 32 bytes of OS entropy.
Update the comment — remove "Not a full CSPRNG" if no longer true. Keep
single-threaded contract documented (AUDIT-M3) or add a mutex if justified.
```

### P1-C — Ban file atomicity + IPv6 canonicalization (**S-3**)

```
Harden port/src/server_bans.c: (1) Windows — atomic replace via MoveFileExA with
MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH after fflush+_commit on the
temp file; POSIX path keeps atomic rename. (2) banAddrEq — canonicalize IPv6
with inet_pton/inet_ntop or compare in6_addr bytes; document IPv4-mapped IPv6.
Add tests or manual checklist in commit message.
```

### P1-D — Admin LIST/STATUS truncation (**AUDIT-M2**, medium, optional same session as P1-A)

```
In netmsg.c SVC_ADMIN handlers for LIST/STATUS, detect snprintf truncation /
buffer full and append an explicit "(truncated)" footer or a has_more bit for
follow-up SVC_ADMIN packets. Document ADMIN_PAYLOAD_MAX behavior.
```

---

## Tier 2 — Documentation and threat model (**H-3**)

**Run after** Tier 1 if you want one doc to reference hardened behavior; **can run in parallel** with Tier 0 only if you only describe *current* behavior.

### P2-A — Dual hosting model (listen vs dedicated)

```
Add a short ADR or context doc (e.g. context/designs/hosting-modes-listen-vs-dedicated.md):
threat model for listen-in-client vs PerfectDarkServer — ROM/mod checks
(g_NetDedicated gates in netmsg), NAT/UPnP/STUN, admin RCON, connect codes vs raw IP
(constraints). Link from README server section. No code unless fixing a doc bug.
```

---

## Tier 3 — In-client “Host / Go online” (**H-1**)

**Depends on:** understanding `netStartServer`, `g_NetDedicated`, main-menu flow. **Recommend Tier 1 complete** if the host button is for WAN; **Tier 2** helps UX copy.

### P3-A — ImGui Host flow (core)

```
Implement a first-class "Host game" / "Go online" path in the client that calls
netStartServer(port, maxclients) with g_NetDedicated == false — same process as
--host / g_NetHostLatch. Touch pdgui_menu_network.cpp, netmenu.c headers/comments
(today they say clients never host — update accurately). Wire port from UI or
pd.ini; show UPnP/STUN status if existing APIs exist (netUpnpGetStatus, stunGetStatus).
Use input context stack for menus (pd2-imgui-input-context skill); no direct SDL
mouse calls. Follow context/constraints.md.
```

### P3-B — Host flow + lobby integration (compound)

```
Extend P3-A: after netStartServer succeeds, transition to the same lobby/room UX
path used when joining a dedicated server (pdgui_menu_room / hub). Ensure
g_NetLocalClient slot-0 host behavior matches net.c listen-server comments.
Regression: dedicated-only join still works. Document any new menu_type / menupool
interaction with menupool skill patterns.
```

---

## Tier 4 — Game-agnostic dedicated server programme (**C-1**)

**Depends on:** explicit design before large refactors. **No dependency** on Tier 3 (orthogonal).

### P4-A — Plugin boundary design (doc-only)

```
Draft an ADR: game-agnostic pd-server plugin ABI — what lives in core vs PD2 DLL:
message dispatch, lobby/match config shapes, stub replacement strategy for
server_stubs.c, CMake targets, versioning. Reference port/src/server_main.c,
server_bridge.c, server_stubs.c. No implementation yet — design only.
```

### P4-B — Implementation spike (after P4-A approved)

```
Implement the smallest vertical slice of P4-A: e.g. extract one PD2-specific
table (g_MpArenas or equivalent) behind a function pointer loaded from a PD2
registration module; keep PerfectDarkServer.exe behavior byte-identical. Single
PR scope — no full plugin load yet unless ADR says otherwise.
```

### P4-C — Stubs reduction (follows P4-B iterations)

```
Continue migrating globals from port/src/server_stubs.c behind the plugin/core
split per ADR. Track line-count and link symbols explicitly in CMake.
```

---

## Tier 5 — Scaling / performance (longer horizon; parallel OK after design)

**Depends on:** product decision to raise caps or fix LAN party perf — not required for Tier 3.

### P5-A — Interest management / broadcast narrowing (**SEC-8/9** class from prior audits)

```
Audit port/src/net/netmsg.c and netdistrib.c for O(clients²) or full-mesh fan-out;
draft a design for stage-scoped or PVS-based replication for player props — even
a "good enough" radius filter. Protocol impact may require NET_PROTOCOL_VER bump
per constraints.
```

---

## Tier 6 — Systemic / game code hygiene (parallel OK; no net dependency)

### P6-A — SP-1 remaining sites (**M-3**)

```
Per context/systemic-bugs.md SP-1: grep remaining g_AmMenus[MAX_PLAYERS] / bot
mpindex risks (activemenu.c, endscreen.c, menu.c sites listed in systemic-bugs).
Fix with bounds-check + skip — never modulo-alias bots onto player slots.
```

### P6-B — Hub room 0 legacy note (**M-2**)

```
Add an ADR comment block to hub.c / room.c documenting room 0 backward-compat
sync with g_Lobby.inGame and the multi-room roadmap — no behavior change unless
you find a real bug while reading.
```

---

## Suggested execution order (summary)

| Order | Tier | Rationale |
|------:|------|-----------|
| 1 | **0** | Tripwire + comments — unblock future constant changes safely |
| 2 | **1** | Lock down exposed-server abuse before advertising listen host |
| 3 | **2** | Doc threat model while behavior is fresh |
| 4 | **3** | Ship Host / go-online UX on hardened base |
| 5 | **4** | Long-running plugin programme in parallel with features |
| 6 | **5–6** | As capacity work and cleanup sprints allow |

---

*Generated for audit [`2026-04-21-full.md`](2026-04-21-full.md). Adjust if priorities shift.*
