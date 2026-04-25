# Connectivity Phase 1 -- Decisions log

> Tracks reasonable-but-not-trivial design choices made during the Phase 1 implementation that aren't pre-pinned by `context/designs/connectivity-and-modern-main-menu.md`. Each decision lists rationale + rollback path so the next session can revisit.

## Identity is Ed25519-bound, network-agnostic (Mike clarification 2026-04-25)

**Decision:** the connect-code "handle" is `SHA256(pubkey || domain)[:4]`. Pubkey is Ed25519, generated once at first launch, persisted in `pd-identity.dat`. The 4-word phrase is a memorable rendering of the handle; the pubkey is the identity.

**Rationale:**
- Mike rejected any IP-derived encoding because it breaks the moment a player roams networks (different ISP, mobile, VPN, multi-device).
- A pubkey-bound handle keeps identity stable across networks, gives every ping signature-verification, and makes spoofing a friend's handle require their private key.
- Ed25519 verify is already vendored (`port/src/ed25519.c`) for the updater. Sign + keygen pulls from the same statically-linked OpenSSL build -- no new dependencies.

**Rollback:** revert to `SHA256(device_uuid || domain)[:4]` (the pre-clarification model). Friends would still get a stable handle per device, but cross-device account portability is lost (if a player reinstalls and the device UUID regenerates, their handle changes silently). Don't roll back unless asymmetric crypto becomes infeasible.

## Trust On First Use (TOFU) for friend pubkeys

**Decision:** when a presence ping from a known connect code arrives without a cached pubkey, the recipient stores the embedded pubkey on the friend record after verifying the handle/key bind. Subsequent pings must match. Mismatch == reject.

**Rationale:**
- Mike's broader trust model (Q11 mod sharing) is "trust by friend graph, not strict crypto signing." TOFU mirrors this: the human friend act is the trust bootstrap; the cryptographic check stops a third party from impersonating the handle on the wire.
- Avoids requiring out-of-band pubkey exchange -- friends just type the connect code and the first inbound ping cements identity.
- A stronger model (manual key fingerprint verification) is available later if Mike wants it; it can layer on top without breaking Phase 1's data structures.

**Rollback:** require explicit user-confirm of every new friend's pubkey via a fingerprint dialog. Adds friction; defer until / unless impersonation incidents are observed.

## Endpoint TTL = 5 minutes

**Decision:** cached `{ip, port, ttl}` entries on a friend record expire 5 minutes after the last successful pong. After expiry, presence treats the friend as cold.

**Rationale:**
- Long enough to span a typical between-match idle without re-discovery.
- Short enough that ISP DHCP renewal (typical 1-24h) and corporate-NAT short-lived NAT bindings drop stale endpoints before they cause confusing "online but unreachable" UX.
- The number is configurable in code (`PRESENCE_PONG_FRESH_MS`) -- a future revision can split "fresh" (60s) from "TTL" (5 min) if needed.

**Rollback:** raise to 30+ minutes if Mike's playtest shows churn, or shorten to 60s if mobile-tethered scenarios drift faster.

## Phase-1 deferral: DHT / rendezvous discovery for moved-network friends

**Decision:** Phase 1 does NOT ship a discovery mechanism for friends with no cached endpoint and no LAN co-presence. Such friends remain `PRESENCE_OFFLINE` from our side until they ping us first.

**Rationale:**
- A genuine no-central-infra rendezvous (BitTorrent DHT, IPFS-style overlay, or small bootstrap nodes) is a substantial subproject. Implementing it in Phase 1 would balloon scope past the design's 1-2 sprint target.
- The asymmetric reachability is acceptable in practice: in any pair of friends, the one who came online first usually still has a fresh cached endpoint (their own + the friend's prior session). When the second friend launches, their ping reaches the first friend, the first friend's pong restores both endpoints, and presence stabilises within seconds.
- LAN cohabiting friends (T0) discover each other regardless of cached state.
- Phase 2 can layer in a rendezvous mechanism without breaking the Phase 1 data structures: the friend-record schema already has space for a fresh endpoint; the rendezvous module would just write into that field on resolution.

**Rollback:** N/A -- adding rendezvous is purely additive.

**Future options to evaluate when rendezvous lands:**
- Embed `libp2p` Kademlia DHT.
- Operate a small set of bootstrap "presence" nodes that only relay encrypted endpoint announces, never see message contents.
- Use a lightweight per-friend "share my endpoint via mutual friend" relay -- if A and B share friend C, A's announce reaches C, who relays to B.
- Defer entirely to the planned matchmaking server (Mike's Q4 future work), but accept that this re-introduces a central element.

## Phase-1 deferral: NET_PROTOCOL_VER 40 -> 41

**Decision:** Phase 1 does NOT bump the existing `NET_PROTOCOL_VER` because no ENet wire packets were added or changed. All Phase 1 traffic runs on dedicated UDP sockets (LAN announce 27101, direct probe 27102, ICE probe 27103, TURN relay 27104, presence 27105).

**Rationale:**
- Bumping the protocol version on a build that has no actual wire change creates an unnecessary compatibility break with prior clients.
- The bump comes naturally with P1.J (in-match invite) when a new `CLC_INVITE_JOIN` lands on the existing match channel.

**Rollback:** N/A -- correct posture is to bump when wire actually changes, not before.

## Phase-1 deferral: P1.J in-match invite + P1.K NAT diagnostics harness

**Decision:** Both deferred to a follow-up session. P1.J needs a new ENet packet on the match channel (and the protocol bump above). P1.K is the in-build harness that walks each tier sequentially against a known peer, used by Mike to fill the real-NAT verification matrix.

**Rationale:** the four commits already landed (social store, p2p layer, presence layer, UI) cover the bulk of the design's structural surface. P1.J + P1.K are scoped enough to ship cleanly as their own commits; bundling them in this session would have either rushed the identity-rework Mike asked for or skipped the verification matrix doc.

**Rollback:** N/A.

## Phase-1 deferral: per-friend "connect code copy" modal

**Decision:** the local connect code is shown in the Social menu Settings tab and the status indicator pill. A dedicated copy-to-clipboard button + share-link modal is deferred to Phase 2 polish.

**Rationale:** users can read the 4-word phrase off the pill / Settings tab today. Copy/share UX is Phase 2 polish.

**Rollback:** N/A.
