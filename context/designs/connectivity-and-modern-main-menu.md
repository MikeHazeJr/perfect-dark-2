# Connectivity + Modern Main Menu Design

**Status:** DESIGN PROPOSAL, no implementation. Mike reviews before any code lands.
**Author:** AI session 2026-04-24 (S456) on Mike's verbatim spec, plus mid-session amendments captured 2026-04-24.
**Audit linkage:** This design substantially de-risks the longest-pole carry-over from `context/audits/2026-04-23-full.md` and `context/audits/2026-04-24-full.md` (C-1 / MASTER-C5, game-agnostic dedicated server). See section 0.

---

## 0. Strategic positioning

> "we will incorporate connectivity into the client. ... will in the short term allow us to pivot away from the current dedicated server. It should effectively be a replacement of that in the context of being able to play with friends, and even if a player hasn't joined the session they should be able to express their online status / be joinable if they set their social settings to appear online / be joinable" -- Mike, 2026-04-24

This design replaces the dedicated server **for friend-play** with a P2P presence + matchmaking layer that lives in the client. It does NOT replace the dedicated server entirely; it narrows its remaining role.

### What stays on the dedicated server (`PerfectDarkServer.exe`)

- Public-internet matchmaking. A player who wants to find strangers (open lobbies, ranked, public Combat Sim) still funnels through dedicated infrastructure.
- High-population servers (>4 humans). The P2P design is bounded at 4-player connectivity per group (the spec calls "up to 4 players to be connected directly via p2p"); 8-bot / 32-slot CS matches can host on dedicated when needed.
- Persistent state that the host group cannot self-host: match history aggregates, leaderboards, anti-cheat replay storage. Out of scope today.

### What moves to client-side P2P

- Friend-with-friend play. Two-to-four humans want to play together: never need to touch dedicated.
- Presence (online / offline / in-mission / spectating) -- always-on, independent of being in a match.
- Invitations and chat between friends.
- Status notifications ("Chris started playing X", "Mike got an achievement", "Jason invited you to play co-op").
- Music-listening-together rooms.
- Mod metadata sharing.
- Spectator streams between connected friends.

### Effect on the audit's C-1 / MASTER-C5 carry-over

The 2026-04-23 audit flagged the **game-agnostic server / broker spike (P4-B)** as the longest pole on the roadmap. P4-A (the design doc) had landed; P4-B (a vertical-slice implementation) was awaiting Mike's approval and the audit warned that further drift risked the pillar becoming permanently doc-only.

This pivot shrinks P4-B's scope materially. The dedicated server no longer needs perfect game-agnostic abstractions for friend-play -- friend-play does not go through it. P4-B can focus on the public-matchmaking + open-lobby use case where game-agnostic abstractions actually pay off, OR be deferred entirely if Mike decides public matchmaking is post-MVP. **Document this scope-narrowing decision in the C-1 carry-over notes after Mike reviews this design.**

---

## 1. System overview

### What changes for the client end-to-end

A client that today does:

```
Boot -> Main Menu -> [Combat Sim | Solo Mission | Multiplayer (joins dedicated)] -> Match
```

becomes:

```
Boot -> Connect to presence service (always-on, async)
     -> Main Menu (with online status + friends panel + invites)
     -> Either:
        - Local: [Solo Mission | Combat Sim against bots]   (no networking)
        - With friends: [P2P group session]                 (no dedicated)
        - Public: [Matchmaking via dedicated]               (existing path, narrowed)
     -> Match
     -> Return to Main Menu (still online, friends still see status)
```

The presence service is a **client-lifecycle subsystem**, not a feature of the lobby. It comes up at boot before the player decides what to do, and stays up until quit. Section 3 details it.

### New top-level subsystems

1. **Presence service** -- always-on connection to the friend graph; broadcasts the local player's status; receives others'. (Section 3.)
2. **Group session** -- the active P2P mesh / star with up to 4 humans. (Section 2.)
3. **Spectator stream** -- a read-only state feed from one peer to another, layered on top of the group session. (Section 5.)
4. **Listening room** -- a music-sync overlay across peers in the same group. Builds on Issue 4a (match-track lock) and Issue 4b (speed-lerp, B-237). (Section 6.)
5. **Modern main menu** -- new UI surface that hosts status indicator, friends list, invitation panel, popup notifications. (Section 7.)

---

## 2. P2P topology

### Topology choice

**Mesh** (every peer talks to every other peer) is the strong default for 4-player groups:

- N=4 means 6 edges. Outbound bandwidth per peer scales with `(N-1)`. At 4 players each peer sends to 3 others; trivially handled.
- Latency is direct (not relayed). Important for music sync and spectator state.
- Resilience: any one peer dropping does not partition the group; the remaining peers still see each other.
- Authority can rotate cleanly (whoever runs the match becomes authoritative for it; other peers attach as observers).

**Star** (one peer is hub, others connect through it) loses on resilience -- if the hub drops, the group dies. Good only when one peer has dramatically better connectivity than the others; not a general solution.

**Recommendation:** mesh with implicit authority promotion when the current authority drops.

### Signaling and NAT traversal

The hard part of P2P is "two clients behind NAT discover each other and punch through." Three options on a trade-off scale:

| Strategy | Pros | Cons | Verdict |
|---|---|---|---|
| **Hosted lobby + STUN/TURN** | Fast to ship, well-understood, every peer reaches the lobby with the same connection logic | Requires a small relay server we operate. Costs scale with simultaneous concurrent groups. | First-phase choice. |
| **Pure STUN, no relay** | No infra cost. | Symmetric NAT (carrier-grade NAT, mobile hotspots) breaks 100% of the time. ~10-25% of users. | Too brittle for friend-play. |
| **Discord / Steam-style mediated lobby** | Use existing identity infra. | Lock-in to that ecosystem; PD2 is open source. | Reject. |

**Recommendation:** small lightweight signaling server + TURN fallback (only when STUN punchthrough fails, which it will for ~15% of attempts). This lobby server is much simpler than the current `PerfectDarkServer.exe` -- it does not run game state, only ephemeral handshake assistance.

### Identity and authentication

Already on the system: `MASTER-C3 identity cookie` (16-byte reconnect cookie; landed S416 / v38). Re-use for friend-graph identity.

Open question (Q5 in section 10): does the friend graph need its own account registration step, or can the existing identity cookie + a name/handle pair carry the social graph?

---

## 3. Presence model

### Always-on presence lifecycle

Boot -> client opens a persistent connection to the signaling server (`presence` channel). State machine:

```
   OFFLINE
     |
     v  (boot, network up)
   CONNECTING
     |
     v  (presence handshake OK)
   ONLINE_IDLE  <-----+
     |                |
     | start match    | match ends
     v                |
   IN_MATCH ---------+
     |
     | spectate
     v
   SPECTATING
     |
     | stop spectating
     v
   ONLINE_IDLE
```

The `OFFLINE` state is reached on user request (toggle "Appear offline") or network failure with retry exhausted.

### Message types and audiences

| Message | Direction | Audience | Frequency | Privacy |
|---|---|---|---|---|
| `presence.set_state(state, details)` | client -> presence service -> all subscribed friends | Friends with `appear_online=true` toward us | On state transitions only | Hidden if `appear_online=false` |
| `presence.heartbeat()` | client -> presence service | Service only | 30-60 s | n/a |
| `presence.invite(target, kind, payload)` | client -> presence service -> target | One target | On user action | Blocked if target's `joinable=false` toward us |
| `presence.invite_response(invite_id, accept)` | client -> presence service -> inviter | Inviter | On user action | n/a |
| `presence.message(target, text)` | client -> presence service -> target | One target | On user action | Blocked if target's `joinable=false` toward us (chat is part of joinable surface, see section 4 for the alternative split) |
| `presence.notification(text, link)` | service -> client | Receiving client | Server-rate-limited | n/a |

The "X started playing Y" / "X got an achievement" notifications are derived from `presence.set_state` plus achievement-event payloads, not first-class wire messages. The service collates and pushes notifications on the receiving end.

### Privacy and opt-out

Two top-level toggles in player settings (see section 4 for the matrix):

- `appear_online` -- if false, the client connects to presence but reports OFFLINE to all friends.
- `joinable` -- if false, the client visible online but cannot receive invites or chat.

Per-friend overrides are an optional layer (Q3). The first-phase impl ships the global toggles only.

---

## 4. Independent-but-connected sessions

> "The connected players can play together or independently (or in separate groups doing their own thing, but will be connected and able to invite / message one another, get status popups ..., and be able to see each others public / shared mods, listen to music together, and spectate one another in any mode."

Two friends in the same "group" can simultaneously be in totally separate matches. What synchronises across them in that scenario, what does not, and what channels carry which data:

| Channel | Synchronised across the group | Lives separately per match |
|---|---|---|
| **Game simulation state** | No -- each match has its own authoritative simulation | Match A's bot positions, match B's bot positions are independent |
| **Presence** | Yes -- "Mike is in Mission X, Chris is in CS arena Y" | n/a |
| **Chat** | Yes -- group chat channel that both peers see regardless of which match they are in | n/a |
| **Music room** | Optional yes -- if the listening-room is active, both peers hear the same track regardless of which match they are in. Match track in Mike's match overrides if in same match. (Section 6.) | Match-internal music when listening room is off |
| **Mod metadata** | Yes -- Mike sees Chris's loaded mods in the friends-list panel | Mod-driven gameplay state stays in the match |
| **Spectator stream** | On-demand -- Chris can spectate Mike's match (section 5) | Defaults to off |
| **Achievements** | Yes -- "Chris got an achievement" notifications surface to Mike | Achievement state internal to Chris's profile until earned |

### Channel design

- **Group control channel** -- low-rate, reliable. Carries presence updates, invites, chat, music-room config, mod-metadata diffs.
- **Match channels** -- per-match, owned by the authoritative peer of that match. Carry game state. Existing protocol (`SVC_*`) applies; up to 32-slot CS matches host on whichever peer started them.
- **Spectator channels** -- per-spectator on-demand subscription to a match channel as a read-only listener. (Section 5.)

---

## 5. Spectator (non-counting) mode

> "Spectators should not take up a player spot in a match."

### Mechanism

Spectator is a **subscription** to an authoritative match's state stream. The spectator client receives the same `SVC_*` packets a player would, but:

- Does not consume a participant pool slot.
- Does not contribute `CLC_MOVE` / `CLC_FIRE` packets back to the authority.
- Renders the match using its existing rendering pipeline -- the same code that draws a remote player's view draws the spectator view.
- Does not have a chr in the world; the camera is free-fly or follows a chosen player.

### Wire shape

Two new packet types:

- `CLC_SPECTATE_REQUEST(target_player, mode)` -- client asks an authoritative peer to add it as a spectator. `mode` selects free-cam vs follow-player.
- `SVC_SPECTATE_ACK(allowed, stream_token)` -- authoritative peer responds. `stream_token` is included in subsequent state packets so the client knows which subscription a packet belongs to.

The state packets themselves are the EXISTING `SVC_*` set. Adding a spectator gate at the top of each state-emit path: the authoritative peer's outbound dispatcher emits to participants (existing behaviour) AND to spectator-subscribed peers (new branch). Spectators never receive participant-only packets (e.g. private chat); add a `SPECTATOR_VISIBLE` bit to each `SVC_*` write per audit.

### Bandwidth implications

Spectator stream is roughly the per-client bandwidth a normal participant would consume from the authoritative peer. Adding 3 spectators to a 4-player match adds 3x the per-client outbound load, peaking around 3 * 50 KB/s = 150 KB/s extra outbound on the host. Acceptable for residential broadband; may need rate-limiting for cellular hotspots.

### Late-join

Spectator joining mid-match needs the same catchup mechanism as a player joining mid-match: a state snapshot + delta replay. Existing protocol's `SVC_STAGE_START` semantics apply. Limit to one snapshot per spectator-join to avoid abuse.

---

## 6. Music-listening-together

### Two modes, with precedence rules

**Mode A: shared listening room (out of match)**

A peer creates a "music room" inside the group. Other peers can join. The room has an authoritative track owner (defaults to the room creator) and broadcasts `SVC_MUSIC_ADVANCE` + the v40 `match_clock_offset_ms` (Issue 4b, B-237) periodically. All peers in the room hear the same track within ~30ms drift. The audio backend (modmusic.c) supports this directly today via `audioMusicSyncReceive` + `audioMusicSyncCorrectionTick`.

**Mode B: in-match track (existing behaviour)**

When a peer is in a match, the match's stage music plays. Issue 4a (match-scoped track lock, S456) prevents the match track from re-rolling on respawn; Issue 4b syncs the track across match participants.

### Precedence

If a peer is in a listening room AND in a match, the in-match track wins. The listening room continues to broadcast for other peers who are NOT in a match; the in-match peer simply does not subscribe to the room while the match owns its audio.

If a peer is in a listening room and the match they are in EXITS the match (returns to main menu), the listening room track resumes -- the peer auto-resubscribes.

### Wire reuse

`SVC_MUSIC_ADVANCE` v40 already carries `(track_id, match_clock_offset_ms)`. Tagged with a room-id field (Q6 -- existing room-id semantics for chat may already cover this, see section 8 for the version bump if needed) it carries listening-room sync without further changes.

---

## 7. UI surface -- modern main menu

### Layout sketch (text)

```
+--------------------------------------------------------------------+
| Perfect Dark 2                                  [Online * Mike   ] |
| New Game / Continue / Modes / Mods / Options / Quit                |
+----------------------------+---------------------------------------+
|                            |  Friends (3 online)                   |
|        Hero panel          |  - Chris      In Mission "Pelagic"    |
|     (or last-played art)   |    [Spectate] [Invite to play]        |
|                            |  - Jason      Online                  |
|                            |    [Invite to play] [Message]         |
|                            |  - Sam        Listening to "Investig" |
|                            |    [Join music room] [Message]        |
|                            +---------------------------------------+
|                            |  Invitations                          |
|                            |  - Chris invited you to play co-op    |
|                            |    [Accept] [Decline]                 |
|                            +---------------------------------------+
|                            |  Recent activity                      |
|                            |  - Chris got "Perfect Agent" 2m ago   |
|                            |  - Sam joined Mike's listening room   |
+----------------------------+---------------------------------------+
```

### Status indicator state diagram (top-right)

```
   [Offline]              -- not connected to presence
   [Online * Mike]        -- ONLINE_IDLE
   [In Mission * Mike]    -- IN_MATCH, mission
   [In CS * Mike]         -- IN_MATCH, combat sim
   [Spectating * Mike]    -- SPECTATING someone
   [Appear-offline * Mike] -- connected but appear_online=false
```

Click the indicator: opens a small status menu (toggle appear_online, toggle joinable, view presence settings).

### Friends list panel

Shows up to ~10 friends. Each row:
- Status icon (Online / In Mission with mission name / In CS with arena / In Listening Room with track / Offline)
- Quick actions appropriate to current state (Invite to Play / Spectate / Join Music Room / Message)

### Invitations panel

Stacks pending invitations. Auto-expires per invitation (e.g. 5 minutes); user can dismiss.

### Popup notifications

Bottom-right toast notifications for: friend came online, achievement earned by friend, friend started a match, invitation received. Dismissable; rate-limited so high-activity friends don't spam.

---

## 8. Wire protocol additions

### New packets

| Packet | Channel | Direction | Notes |
|---|---|---|---|
| `CLC_PRESENCE_LOGIN(name, identity_cookie)` | reliable | client -> presence svc | Reuse v38 cookie; signaling identifier |
| `CLC_PRESENCE_STATE(state, details)` | reliable | client -> presence svc | Pushes state changes |
| `SVC_PRESENCE_FRIENDS(friend_list)` | reliable | presence svc -> client | Initial friend list + state on login |
| `SVC_PRESENCE_FRIEND_UPDATE(friend, state, details)` | reliable | presence svc -> client | Per-friend updates after login |
| `CLC_PRESENCE_INVITE(target, kind, payload)` | reliable | client -> presence svc -> client | Match-invite, music-room-invite, chat-invite |
| `SVC_PRESENCE_INVITE(invite_id, sender, kind, payload)` | reliable | presence svc -> client | Mirror of above; flows through service for rate-limit |
| `CLC_PRESENCE_INVITE_RESPONSE(invite_id, accept)` | reliable | client -> presence svc -> inviter | |
| `CLC_PRESENCE_MESSAGE(target, text)` | reliable | client -> presence svc -> client | Text chat; service rate-limits |
| `SVC_PRESENCE_MESSAGE(sender, text)` | reliable | presence svc -> client | Mirror |
| `CLC_SPECTATE_REQUEST(target_player, mode)` | reliable | client -> match authority | Direct, P2P |
| `SVC_SPECTATE_ACK(allowed, stream_token)` | reliable | match authority -> client | |
| (existing `SVC_*` state) | per existing | match authority -> spectator | New `SPECTATOR_VISIBLE` bit per packet write |

### Versioning

Bump `NET_PROTOCOL_VER` 40 -> 41 for the presence channel; v40 (Issue 4b) is the last version without presence. Presence packets only fire after `CLC_PRESENCE_LOGIN`, so an old client that never sends LOGIN will never receive any of the new packets and can stay on v40 indefinitely (acts like a dedicated-only client).

This is the version-migration story: clients that don't opt into presence stay on dedicated-server semantics. Adding presence is a CONNECTING-state action that triggers an implicit version negotiation.

### Backwards-compat with current protocol

The dedicated server (`PerfectDarkServer.exe`) does not handle the new packets. A v41 client connecting to a v40-only dedicated server falls back gracefully because presence is a separate channel to a separate service. The dedicated server's match flow is unchanged.

---

## 9. Phasing -- 5 phases, foundation-first

The deliverable order matters: phase 1 must be playable for two friends without the dedicated server. Each subsequent phase layers on capability without breaking phase 1.

### Phase 1 -- minimum viable replacement (target: 1-2 sprints)

**Scope:** two friends can play together without the dedicated server.

**Files touched (estimate):**
- `port/src/net/presence.c` (new, ~800 lines) -- presence client lifecycle
- `port/src/net/p2p_signaling.c` (new, ~400 lines) -- STUN/TURN punchthrough
- `port/src/net/group_session.c` (new, ~600 lines) -- 4-peer mesh + match dispatch
- `port/src/net/netmsg.c` -- new `CLC_PRESENCE_*` / `SVC_PRESENCE_*` packets, ~300 lines added
- `port/include/net/net.h` -- `NET_PROTOCOL_VER 40 -> 41`, presence packet decls
- `port/fast3d/pdgui_menu_mainmenu.cpp` -- status indicator, friends list panel (read-only at this phase)
- `port/fast3d/pdgui_menu_friends.cpp` (new, ~400 lines) -- friends list UI
- New external service: `pd-presence-server` -- small Go / Node / Python service that brokers presence + signaling. Not in this repo. ~500 lines.

**Dependencies:** existing v38 identity cookie, existing actionmap, existing netbuf/netchan.

**Build vs buy:** Build the presence service ourselves (small scope, lock-in concerns rule out Discord / Steam). For STUN, use a public free STUN server (`stun.l.google.com:19302`). For TURN, ship our own TURN server alongside presence; cost is ~$5/mo on a small VPS for friend-play scale.

**Exit criteria:**
- Two friends on different ISPs, no dedicated server running, can launch the client, see each other online, exchange invitations, start a co-op match together.
- Presence stays connected through match-start and match-end.
- TURN fallback fires when direct STUN punchthrough fails (test by forcing it).

### Phase 2 -- chat + invitations polish

**Scope:** text chat between friends, invitation flow refined, popup notifications.

**Files touched:**
- `port/src/net/presence.c` -- chat message handling
- `port/fast3d/pdgui_menu_friends.cpp` -- chat UI
- `port/fast3d/pdgui_notifications.cpp` (new) -- toast notification system

**Dependencies:** Phase 1. Standalone otherwise.

**Exit criteria:** chat round-trips between friends (in-game and out), notifications appear correctly for state changes.

### Phase 3 -- music-listening-together

**Scope:** shared listening rooms; precedence rules between match and room.

**Files touched:**
- `port/src/audio.c` -- listening-room subscribe/unsubscribe; precedence with match track
- `port/src/net/presence.c` -- listening-room invite + member list
- `port/fast3d/pdgui_menu_friends.cpp` -- "Join music room" action

**Dependencies:** Phase 1 (group session), Issue 4b / B-237 (already landed -- music speed-lerp infra).

**Exit criteria:** two friends in the main menu can hear the same track in sync; one of them enters a match and switches to match audio; on exit they re-hear the room track.

### Phase 4 -- mod metadata sharing + status popups

**Scope:** mod-list visible to friends; achievement / mission-start notifications surface to friends.

**Files touched:**
- `port/src/net/presence.c` -- mod metadata payload, achievement event payload
- `port/fast3d/pdgui_menu_friends.cpp` -- "View mods" and recent-activity feed
- `port/src/modmgr.c` -- expose mod manifest summary to presence layer

**Dependencies:** Phase 1.

**Exit criteria:** friends panel shows what mods each friend has loaded; "X earned an achievement" toast appears on friend client.

### Phase 5 -- spectator streams

**Scope:** non-counting spectator mode, free-cam or follow-player.

**Files touched:**
- `port/src/net/netmsg.c` -- `CLC_SPECTATE_REQUEST`, `SVC_SPECTATE_ACK`, `SPECTATOR_VISIBLE` bits on existing state packets
- `port/src/net/group_session.c` -- spectator subscription list per authoritative peer
- `port/fast3d/pdgui_spectator_hud.cpp` (new) -- spectator camera UI
- `src/game/cam.c` or equivalent -- spectator free-cam mode (likely a thin wrapper over the existing freefly camera infra)

**Dependencies:** Phase 1, plus a state-snapshot mechanism for late-join. Snapshot may need a new packet (`SVC_STATE_SNAPSHOT`).

**Exit criteria:** Mike can pick a friend's match, click Spectate, see the match in real-time, free-cam around it, exit without disturbing the match.

---

## 10. Open questions

The numbered question list is **answered separately by Mike** (per his 2026-04-24 directive to handle the connectivity-questions Q&A out-of-band). The IDs below are stable so Mike's reply can reference them:

- **Q1.** STUN/TURN: build our own TURN, or piggyback on an existing free TURN service? Cost vs reliability trade-off. (Section 2.)
- **Q2.** Presence service language / runtime: Go (single binary, easy ops), Node (fast iteration, JSON-native), or Rust (perf, type safety)? Mike's preference shapes the build-or-buy answer.
- **Q3.** Per-friend privacy overrides: should `appear_online` and `joinable` support per-friend exceptions (e.g. "appear offline to Steve, online to everyone else"), or are global toggles sufficient for phase 1?
- **Q4.** Friend graph storage: server-authoritative (presence service knows the graph; client reads it on login) vs client-side (each client maintains its own friend list, syncs via the service). Trade-off is convenience vs decentralisation.
- **Q5.** Identity bootstrap: does the existing v38 identity cookie + a chosen handle suffice, or does the friend graph need a registration / login step (email + password, OAuth, etc.)?
- **Q6.** Listening-room and group chat: do we re-use the v29 room protocol concept (with a new `room_kind` enum for music vs match), or define a separate "social room" type? Section 6 currently assumes a tagged `SVC_MUSIC_ADVANCE` extension; a richer alternative is a full social-room channel.
- **Q7.** Notification rate limit: how many notifications per friend per minute is the user-tolerable upper bound? Affects how aggressive the presence service is at coalescing rapid state changes.
- **Q8.** Achievement events: are achievements a first-class wire concern (push them through presence as `presence.achievement(name)`) or a derived concern (presence service polls a profile API)? Phase-4 design depends on the answer.
- **Q9.** Matchmaking pivot: does the dedicated server's public-matchmaking role survive long-term, or does friend-play scale enough to deprecate it entirely? This is the question the audit's C-1 carry-over hinges on.
- **Q10.** Anti-grief: in P2P friend groups, what stops a malicious peer from broadcasting bogus presence states or invitations? Spec-level deferred but worth Mike's call before phase-1 ships.

Mike is sending a separate numbered question list with his answers; cross-reference here when received.

---

## 11. Out of scope for this design

- Replay storage (persistent match recordings).
- Anti-cheat replay validation.
- Cross-platform play (PD2 is PC-only).
- Voice chat (text-only chat in scope; voice is a phase-6+ stretch).
- Public matchmaking improvements -- those stay on the dedicated server's roadmap.

---

## 12. Reading order for review

1. Section 0 (strategic positioning) -- the framing decision.
2. Section 9 (phasing) -- the deliverable shape.
3. Section 10 (open questions) -- the decisions Mike owns.
4. Sections 1-8 -- detail, in order.

If section 0 is wrong, nothing downstream matters. If section 9 is right, phase 1 can dispatch to a future batch on Mike's go-ahead.
