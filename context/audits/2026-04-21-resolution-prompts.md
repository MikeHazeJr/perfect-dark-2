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

**Direction (locked 2026-04-21):** The dedicated server is a **manifest broker**, not a copy of the game. **Catalog ID strings** (`namespace:name`) are the asset identity at protocol boundaries (`context/constraints.md`). Each client has a **private dynamic catalog**; **loading and memory** for a match follow the **host manifest**, validated by **hashes** (and/or host revision) for netplay. The server binary should **not** embed game content (ROM, stages, art) — only transport, room/hub state, manifest storage/distribution, readiness/trust gating, admin. Optional **loadable policy code** is secondary to **data-first** rules. Full ADR: [`context/designs/pd-server-plugin-abi-adr.md`](../designs/pd-server-plugin-abi-adr.md).

### P4-A — Broker + catalog-ID architecture (doc-only) — **DONE**

```
Delivered: context/designs/pd-server-plugin-abi-adr.md (updated 2026-04-21) —
manifest broker as primary model; optional PD2 policy module; P4-B/C migration
notes; versioning (NET_PROTOCOL_VER vs manifest revision vs optional
PD_SERVER_PLUGIN_ABI_VERSION). **Link:** `context/server-architecture.md` see-also
line points at this ADR (Tier 4 C-1). **Optional follow-up:** wire-level sketch
for manifest revision + per-player readiness (Trust Everyone / Friends /
Confirm First) without stalling the whole lobby — add to ADR appendix or small
`context/designs/` note when implementing P4-B.
```

### P4-B — Implementation spike (after P4-A approved)

```
Smallest vertical slice aligned with the ADR — NOT "move g_MpArenas behind a
function pointer" as the primary story. Prefer instead:
- Host (or host client) path submits a manifest to the server; server stores it
  and fans it out to peers; manifest entries use catalog IDs + expected hashes
  (or host baseline revision) per constraints.md.
- Per-player readiness bits for "accepted manifest rev / hash" so Confirm First
  can exist without blocking other players or the host (server must not freeze
  the lobby for everyone when one player is in a modal).
- Deliberate NET_PROTOCOL_VER bump if new fields are required; do not claim
  byte-identical wire if the protocol changes.

Single PR scope. Optional loadable DLL/static policy module only if the spike
requires a hook the manifest cannot express — keep game payloads out of the
server binary.
```

### P4-C — Stubs reduction + broker completion (follows P4-B iterations)

```
Continue removing reliance on port/src/server_stubs.c baked PD2 tables where the
manifest + catalog-ID path replaces server-side authority. Remaining link glue
may move to an optional policy target or stay minimal in core. Track stub
line-count and explicit link surfaces in CMake / session-log so shrinkage is
visible. Align netmsg paths with "broker compares IDs + hashes + readiness"
before PD2-specific branches.
```

---

## Tier 5 — Scaling / performance (longer horizon; parallel OK after design)

**Depends on:** product decision to raise caps or fix LAN party perf — not required for Tier 3.

### P5-A — Interest management / broadcast narrowing (**SEC-8/9** class from prior audits) — **design doc DONE**

```
Delivered: context/designs/interest-management-replication.md (2026-04-21) —
audit of net.c netEndFrame + shared buffers + enet_host_broadcast; netdistrib.c
called out as orthogonal (per-client mod transfer); phased plan (room relevance,
radius/cell, PVS long-term, cadence throttling); protocol / NET_PROTOCOL_VER notes;
references prior SEC-8/9 write-up in context/audits/2026-04-19-server-security-scaling.md.

Implementation remains future work (instrumentation → prototype behind flags → wire
change only when needed).
```

---

## Tier 6 — Systemic / game code hygiene (parallel OK; no net dependency)

### P6-A — SP-1 remaining sites (**M-3**) — **DONE**

```
Landed: menu.c — currentPlayerIsMenuOpenInSoloOrMp() rejects mpindex outside
[0, MAX_PLAYERS) before the >=4 fold (fixes 12→8 OOB). func0f0f8120() removes
AVOID_UB % MAX_LOCAL_PLAYERS alias; bounds-check g_MpPlayerNum then index g_Menus[].

activemenu.c — amOpen() / amOpenPickTarget() / amRender() guard g_AmIndex /
ARRAYCOUNT(g_AmMenus). endscreen.c already had mpindex guards on coop/counter-op push.

context/systemic-bugs.md SP-1 table updated.
```

### P6-B — Hub room 0 legacy note (**M-2**) — **DONE**

```
port/src/hub.c and port/src/room.c: ADR-style comment blocks (P6-B) on room 0,
g_Lobby.inGame sync, HUB_MAX_ROOMS / multi-room roadmap — no behavior change.
```

---

## Suggested execution order (summary)

| Order | Tier | Rationale |
|------:|------|-----------|
| 1 | **0** | Tripwire + comments — unblock future constant changes safely |
| 2 | **1** | Lock down exposed-server abuse before advertising listen host |
| 3 | **2** | Doc threat model while behavior is fresh |
| 4 | **3** | Ship Host / go-online UX on hardened base |
| 5 | **4** | Long-running **manifest broker** programme (catalog IDs, no game content in server exe) in parallel with features |
| 6 | **5–6** | As capacity work and cleanup sprints allow |

---

*Generated for audit [`2026-04-21-full.md`](2026-04-21-full.md). Adjust if priorities shift.*

**Tier 4 note (2026-04-21):** Tier 4 revised to match [`../designs/pd-server-plugin-abi-adr.md`](../designs/pd-server-plugin-abi-adr.md) — manifest broker + catalog IDs first; remaining prompts assume that direction.
