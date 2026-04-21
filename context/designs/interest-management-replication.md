# Interest management & replication narrowing (P5-A / SEC-8, SEC-9)

**Status:** Design draft (no implementation commitment)  
**Date:** 2026-04-21  
**Related:** [`context/audits/2026-04-19-server-security-scaling.md`](../audits/2026-04-19-server-security-scaling.md) (SEC-8, SEC-9), [`context/audits/2026-04-21-resolution-prompts.md`](../audits/2026-04-21-resolution-prompts.md) Tier 5.

---

## 1. Audit — current behavior (code)

### 1.1 Server game-state broadcast (`port/src/net/net.c`)

`netEndFrame()` (server branch, `g_NetMode == NETMODE_SERVER && g_NetNumClients > 0`):

- **Players:** Loops `i = 0 .. g_NetMaxClients-1`, and for each client in `CLSTATE_GAME` with a player, calls `netmsgSvcPlayerMoveWrite(&g_NetMsgRel | &g_NetMsg, cl)` (`net.c` ~1982–1990). Each call **appends** one `SVC_PLAYER_MOVE` (and optional room data) into **shared** buffers `g_NetMsg` / `g_NetMsgRel`.
- **Bots:** On `g_NetNextUpdate` ticks, loops all bots and appends `SVC_CHR_MOVE` via `netmsgSvcChrMoveWrite` (~1994–2010); less often `SVC_CHR_STATE`, `SVC_CHR_SYNC` (~2013–2026).
- **Co-op / anti NPCs:** Additional loops over `g_ChrSlots` for `SVC_NPC_*` (~2037–2073).
- **Flush:** `netFlushSendBuffers()` (~408–425) sends the accumulated buffers with `netSend(NULL, …)`, which uses **`enet_host_broadcast`** when `dstcl == NULL` (`net.c` ~2210–2211).

So: **one unreliable packet** (and one reliable when non-empty) per frame **to every connected peer**, containing **all** player move records and **all** bot move records for that frame. There is **no** per-recipient filtering.

**Bandwidth scaling:** Each peer receives a payload of size **Θ(P + B)** (players + bots in the move batch). Total bytes **per frame** over all links is **Θ(N × (P + B))** for **N** peers — **linear in N** per peer payload, **quadratic in aggregate** across the host uplink when counting all client edges. At large **N**, host uplink is the bottleneck.

### 1.2 `netdistrib.c` (not the same problem)

`port/src/net/netdistrib.c` implements **mod component distribution** (catalog diffs, `SVC_DISTRIB_*`, per-client queues). It is **already** targeted per client. SEC-8/9 refer to **in-match character/player replication**, not distrib.

### 1.3 `netmsg.c`

Message **encoding** for `SVC_PLAYER_MOVE`, `SVC_CHR_MOVE`, NPCs, props lives here; the **fan-out policy** is “append everything, then broadcast” from `netEndFrame`. Interest management would either:

- **Filter what gets appended** (preferred first step), and/or  
- **Split sends** per peer or per room (more invasive).

---

## 2. Goals

1. **Reduce aggregate bandwidth** for large player counts and high bot counts without breaking correctness for shooters (everyone still sees threats that can shoot them).
2. **Preserve dedicated-server** invariant: `g_NetLocalClient` may be NULL; server path must remain reachable (`net.c` comment ~1976–1981).
3. **Protocol changes** only with explicit **`NET_PROTOCOL_VER`** bump and `constraints.md` review (new message types, interest bitmasks, or per-peer payloads).

---

## 3. Design options (phased)

### Phase A — “Good enough” **room / stage relevance** (no PVS)

Use existing **room membership** on props (`prop->rooms`, `netbufWriteRooms` already in `SVC_PLAYER_MOVE` when `UCMD_FL_FORCEMASK`):

- **Rule (example):** For each **observer** client **C**, when building what they receive, include moves for entity **E** only if **E**’s rooms intersect **C**’s predicted visibility set (e.g. same **primary room** or **shortest path** under a conservative rule).
- **Implementation sketch:** Instead of a single `g_NetMsg` broadcast, **either**:
  - **Duplicate buffers per peer** (simple, more CPU) — **N** encodes per frame, or  
  - **Single pass + bitmask** — encode entities once with IDs, then send **small per-client masks** (new wire) — **protocol bump**.

Start with **duplicate encode per recipient group** only if **N** is small (e.g. ≤8); for larger **N**, prefer **bitmask + shared payload** to avoid CPU regression.

### Phase B — **Radius / coarse grid** (“good enough” for LAN)

- Partition stage space into **cells**; each entity reports **cell id** cheaply each frame (derived from position).
- Include entity **E** in client **C**’s batch if **cell(E)** is within **R** cells of **cell(C)** (tune **R** by weapon range).

Works without full **PVS**; may over-send in open maps.

### Phase C — **PVS / portal** (long horizon)

Reuse or mirror renderer **visibility** data if exposed to game code — only send entities visible from **C**’s camera region. Highest engineering cost; best bandwidth.

### Phase D — **Cadence throttling**

- Full rate for **high threat** (visible + line-of-sight or same room).
- **2–5 Hz** position-only for **offscreen** entities (FPS netcode standard).

Reduces bytes without dropping entities entirely.

---

## 4. Protocol impact

| Change | Likely bump? |
|--------|----------------|
| Filter append only (same messages, fewer records for some peers) | **Maybe not** if each client still receives a valid superset subset of today’s stream (careful with client-side prediction). |
| Per-client different **sets** of `SVC_*` in one frame | Often **yes** — clients must know interest semantics. |
| New **aggregate** message: “batch + entity id list” | **Yes** — document in `net/net.h` and bump **`NET_PROTOCOL_VER`**. |

---

## 5. Risks

- **Fairness / cheats:** Aggressive culling can hide enemies that should be visible; conservative rules first.
- **CPU:** Per-peer encoding **N** times per frame can dominate; measure before shipping.
- **Co-op NPCs:** Same framework as bots; shared loops in `netEndFrame` (~2037+).

---

## 6. Suggested next steps (implementation)

1. **Instrument** `netEndFrame`: log `g_NetMsg.wp`, `g_NetMsgRel.wp`, and **N** per tick on a stress map (document baseline).
2. Prototype **Phase D** (cadence) + **Phase B** (cell distance) behind **`pd.ini`** / compile flag.
3. Open **protocol** PR only when wire format changes are required.

---

## 7. References

- `port/src/net/net.c` — `netEndFrame`, `netFlushSendBuffers`, `netSend`, `enet_host_broadcast`
- `port/src/net/netmsg.c` — `netmsgSvcPlayerMoveWrite`, `netmsgSvcChrMoveWrite`, NPC helpers
- `port/src/net/netdistrib.c` — mod distribution (orthogonal)
- Prior audit narrative: SEC-8 (bot × clients), SEC-9 (aggregate broadcast scaling) in `2026-04-19-server-security-scaling.md`
