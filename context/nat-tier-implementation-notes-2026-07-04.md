# NAT tier (c054-c060) -- implementation notes from code-level scoping (2026-07-04)

All six are **architectural connectivity work** (not bounded wiring) and are
**runtime-unverifiable** without real NAT / two internet peers or a loopback-sim
harness. This captures the exact integration point + shape for each, from reading the
actual code, so a focused connectivity session (or the relay branch) executes rather
than re-discovers. The working tree is clean -- `group_session.c` is committed, so
there is no longer an uncommitted-collision blocker; these are gated on design +
traversal testing, not on file ownership.

Ports (all real, bound): LAN announce 27101, direct probe 27102, ICE 27103,
TURN relay 27104, presence 27105, voice 27108.

---

## c057 -- UPnP map the probe/ICE/TURN ports

- **Gap:** `p2p_upnp.c` maps only the game/ENet port via `netUpnpSetup(port)`.
- **Blocker:** `netUpnpSetup(u16 port)` / `netUpnpTeardown()` / `netUpnpGetStatus()`
  (netupnp.h) model a SINGLE mapping (one global worker). Mapping 27102/27103/27104
  additionally needs `netupnp` to hold **multiple simultaneous mappings**.
- **Plan:** extend `netupnp.c` to a small mapping table (add `netUpnpMapAdditional(port)`
  + per-port status; teardown removes all). Then in `p2p_upnp.c` tier-3 activation,
  map 27102/27103/27104 after the primary. Graceful-fallback already handled (a rejected
  mapping just fails that tier), so blast radius is low.
- **Verify:** a router with UPnP; confirm the extra port maps appear + the probe/ICE/TURN
  tiers succeed behind UPnP-only NAT.

## c055 -- publish STUN reflexive to the peer

- **Gap:** `p2pPublishMyReflexive()` stores the reflexive (`p2pMyReflexiveIpv4/Port()`
  getters exist, p2p_stun.c:102-103) but it is never written into an outbound presence
  packet's `listen_ipv4/listen_port`.
- **Key finding:** the LAN announce (`p2p_lan.c sendAnnounce`, line 296) CORRECTLY writes
  `listen_ipv4=0` (LAN peers use packet src). So the target is the **internet presence
  SERVICE** outbound builder (Ed25519-signed social presence), NOT the LAN announce.
- **Plan:** in the internet presence outbound builder, populate `listen_ipv4/listen_port`
  from `p2pMyReflexiveIpv4()/Port()` when non-zero (after STUN completes; 0 before, which
  the receiver already handles by falling back to src -- p2p_lan.c:341). Low blast radius.
- **Verify:** two peers behind different NATs; confirm each learns the other's reflexive
  and the STUN/ICE tier connects.

## c054 -- wire ICE peer-candidate exchange

- **Gap:** `p2pIceAddPeerCandidate()` (p2p_ice.c:260) exists but is never called; ICE
  degrades to a slower Tier 1.
- **Plan:** call it from the presence **inbound** delivery path -- when a peer's presence
  (or invite) arrives carrying candidate endpoints, feed each into
  `p2pIceAddPeerCandidate(pair, cand)` so ICE tests host+srflx+prflx pairs. Pairs with
  c055 (reflexive must be published for the srflx candidate to be useful).
- **Verify:** two-peer NAT; confirm multi-candidate ICE picks a working pair.

## c058 -- real kbps measurement (replaces placeholder)

- **Gap:** `group_session.c:136` uses a placeholder `best_kbps=0` for local kbps; first
  non-zero reporter wins relay authority.
- **Plan:** add byte counters on the net send/recv path (rolling-window bytes -> kbps),
  expose a getter, and feed it to `groupSessionUpdateKbps()`. Instrumentation is
  cross-cutting (net path), not local to group_session.c.
- **Verify:** measured kbps is sane under load; relay authority elects the
  highest-bandwidth peer.

## c059 -- public TURN relay fallback

- **Gap:** `p2p_turn.c:211-213` reports "no relay available" when `s_Cands` is empty (a
  brand-new server with no players fails Tier 5).
- **Plan:** add a configurable public TURN address (config/pd.ini), and when `s_Cands` is
  empty register it as a fallback relay candidate. Keep the custom 24-byte PDTRN protocol
  (port 27104).
- **Verify:** a server with no peers still relays via the configured fallback.

## c060 -- unify legacy hole-punch with the tier machine

- **Gap:** `netholepunch.c` runs in parallel with the 6-tier orchestrator; the handoff
  `p2pPairGetEndpoint -> enet_host_connect` is not visible/unified.
- **Plan:** route the legacy path's endpoint handoff through the tier orchestrator (single
  source of truth), then retire the parallel netholepunch path. This is a refactor of the
  connect flow -- do it last, after c054/c055 land, so the tier machine is the one path.
- **Verify:** connect flow works via the orchestrator only; no regressions in existing
  (working) direct/LAN connects.

---

*Scoped 2026-07-04 by reading netupnp.h, p2p_upnp/stun/ice/turn/lan.c. Ages out per
retention.md once the tier is implemented. Runtime traversal verification (real NAT /
2 peers / loopback-sim) is required for every item and is the human/network-gated part.*
