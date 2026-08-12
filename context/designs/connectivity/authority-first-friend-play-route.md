# Authority-first friend-play route

Status: D-003 option A, decided and production-verified 2026-08-12.

## Goal and reason

Keep the persistent friend/group topology as a small peer mesh, but run each
actual match through one elected in-client ENet listen host. This preserves the
existing server-authoritative `SVC_*` protocol while removing B-1058's endpoint
ambiguity and T-NETWORKING-006's duplicate group connection ownership.

## Endpoint types are not interchangeable

1. **Match-server route**: the signed address of the elected authority's ENet
   listen socket. Only this may reach `netStartClient*`.
2. **Probe candidate**: a LAN, STUN, UPnP, or ICE address used to test whether a
   peer-to-peer UDP path is reachable. Probe success never becomes an ENet
   route by itself.
3. **Relay descriptor**: a TURN relayer address plus target identity. It is not
   a direct ENet endpoint unless the relay transport explicitly presents an
   ENet-compatible forwarding socket.

All three require separate fields, validation, freshness, and lifecycle. A
producer may not write one type into another type's slot.

## Match start sequence

1. Each side freezes its local upload claim before sending its signed invite or
   acceptance, so both peers elect from the same claims.
2. Both sides record the same match initiator and elect from local plus accepted
   peers. Highest fresh upload wins; initiator wins exact/no-data ties; public
   handle is the stable final tie-break.
3. Both sides latch the same elected authority before either side performs an
   ENet action. Election inputs cannot change underneath startup or join.
4. The elected local authority starts `netStartServer` exactly once. No other
   peer starts a server for that match.
5. Presence publishes the authority's signed match-server route. The route is
   source-derived for LAN/direct startup and upgrades when the ENet-bound STUN
   or UPnP result becomes available.
6. Non-authority peers wait for a fresh signed route and enter one ENet join
   handoff. The group layer never formats a probe-candidate address as the
   match address.
7. Startup, route-publication, or join failure clears the route and authority
   latch and suppresses automatic retry. Mid-match authority loss/migration
   remains an explicit later transaction, not an implicit re-election while a
   client connect is active.

## Wire and validation

- Presence v5 is 196 bytes, signs the first 132 bytes with the
  `pd-presence-v5` domain, and uses packet kind 5 for immediate route
  publication/clear.
- The route occupies bytes 88-99: IPv4, port, flags, reserved zero, and issued
  Unix seconds. The sender handle is already bound to the signed frame; the
  public key and signature begin at offsets 100 and 132.
- Zero IPv4 means use the verified packet source address; zero port, multicast,
  broadcast, unknown flags, future timestamps, and stale reports reject.
- Route publication is immediate on authority start and whenever STUN/UPnP
  upgrades it. STUN is eligible only when the current discovery used the exact
  ENet listen port and reports cone NAT; the shared worker's port-zero P2P
  probe result is ineligible. The normal 30-second presence cadence remains
  the retry path.
- No raw IP is shown in UI. Connect codes remain stable identity, not endpoint.

## Implementation slices

1. Pure route validation/action planner plus focused tests.
2. Signed presence route transport and freshness cache.
3. Pre-connect frozen authority election and idempotent listen-host/client
   actions, including failure rollback with no implicit retry.
4. Remove `p2pPairGetEndpoint -> netStartClientWithHolePunch` from group match
   startup; keep probe candidates separate for T-NETWORKING-001/002/003.
5. Keep the remaining direct/hole-punch/tier connection machinery as explicit
   follow-up `T-NETWORKING-006` scope. D-003 removes that machinery from the
   friend-play route but does not claim the broader real-NAT unification done.
6. Verify two ordinary clients for both initiator-authority and invitee-
   authority cases, failure rollback, clean shutdown, and unchanged lobby /
   manifest / ready / match flow.

## Completion boundary

The D-003 completion boundary passed on one frozen client: focused `[d-003]`
tests pass 90 assertions/5 cases; the complete suite passes 56,774/1,040; the
native-source guard passes; initiator-authority and invitee-authority ordinary-
client receipts pass 143/143 and 142/142; authority-start and client-join
rollback receipts pass 32/32 and 38/38; and the final-binary V-009 gate passes
21/21 with both frames inspected as unobstructed. Real-NAT candidate exchange,
tier unification, and host migration remain separate parent connectivity work.
