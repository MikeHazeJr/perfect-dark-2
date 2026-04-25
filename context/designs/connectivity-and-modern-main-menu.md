# Connectivity + Modern Main Menu Design

**Status:** DESIGN READY FOR PHASE 1 IMPLEMENTATION (pending Mike's final read).
**Author:** AI session 2026-04-24 (S456-S457) on Mike's verbatim spec, mid-session amendments, and 18 question answers (Q1-Q18, captured 2026-04-24).
**Audit linkage:** This design substantially de-risks the longest-pole carry-over from `context/audits/2026-04-23-full.md` and `context/audits/2026-04-24-full.md` (C-1 / MASTER-C5, game-agnostic dedicated server). See section 0.

---

## 0. Strategic positioning

> "we will incorporate connectivity into the client. ... will in the short term allow us to pivot away from the current dedicated server. It should effectively be a replacement of that in the context of being able to play with friends" -- Mike, 2026-04-24

> "removed entirely from current releases. Future return = matchmaking and hosted-server-networking only, never direct friend-play." -- Mike Q4, 2026-04-24

The dedicated server (`PerfectDarkServer.exe`) is **removed from current releases**. Friend-play moves to client-side P2P. The dedicated-server binary may return at a later phase, scoped narrowly to **matchmaking + hosted-server-networking** (public lobbies, ranked, anti-cheat-validated infrastructure). It will never again carry friend-play traffic.

### Effect on the audit's C-1 / MASTER-C5 carry-over

The 2026-04-23 audit flagged the **game-agnostic server / broker spike (P4-B)** as the longest pole on the roadmap and warned that further drift would leave the pillar permanently doc-only. This pivot **resolves that pole by removal**: P4-B is no longer the gating concern because the dedicated-server-pillar's friend-play role is gone. The C-1 carry-over now reduces to "if and when matchmaking returns, P4-B's broker abstractions get reconsidered" -- a much smaller, optional decision that can be deferred indefinitely.

### What's IN this design

- Always-on peer-to-peer presence (no central servers).
- Direct P2P friend-play matches up to 4 humans.
- Spectator + Theater (saved-match playback) as a unified subsystem.
- Public mods page driven through the existing mod distribution system.
- Player profile pages (Halo 3 File Share lineage).
- Modern main menu with sidebar, full social menu, and private chat.

### What stays out

- Public-internet matchmaking. Stays on the roadmap as a future dedicated-server return; not in scope here.
- Voice chat. Queued for a later phase.
- Cross-platform play. PD2 is PC-only.

---

## 1. System overview

### What changes for the client end-to-end

```
Boot -> Local presence stack comes up; pings known friend connect codes
     -> Main Menu (status indicator, sidebar with friends)
     -> Either:
        - Local solo: Solo Mission / Combat Sim against bots
        - With friends: P2P group session (no central infra)
        - Spectate: read-only stream from a friend's match
        - Theater: replay a saved match (same camera as spectator)
     -> Match
     -> Return to Main Menu (still online; friends still see status)
```

### New top-level subsystems

1. **Presence layer** -- always-on, direct peer-to-peer pings to known friend connect codes. The friend list IS the address book. (Section 3.)
2. **Group session** -- the active P2P mesh with up to 4 humans. Match authority elected by upload speed (or initiator-as-fallback). (Section 2.)
3. **Spectator + Theater unified subsystem** -- one camera/control architecture; live stream OR saved-match playback drive the same component. (Section 5.)
4. **Listening room (music together)** -- public playlist owned by session host, propagated through the existing mod distribution system. Not a new audio-streaming protocol. (Section 6.)
5. **Player profile + public mods page** -- per-player profile with stats and public mod list (Halo 3 File Share lineage); session-aggregate browse view. (Section 7.)
6. **Modern main menu** -- new UI surface with status indicator, sidebar, full Social menu, private chat with file/mod attachments. (Section 8.)

---

## 2. P2P topology, connect codes, signaling

### Topology: mesh with elected authority

**Mesh** (every peer talks to every other peer) for groups of up to 4 humans. N=4 means 6 edges, trivially handled.

**Within-match authority is required** (Q1). Authority is per-match, not persistent across the group's lifetime: a new match elects a new authority. Election rule:

1. **Highest measured upload speed** wins. Speed-test data is collected passively during prior sessions and attached to the peer's presence broadcast.
2. **Match initiator** as fallback when upload-speed data is unavailable (first session of a fresh install) or tied.

The authority owns the match's `SVC_*` state stream. Other peers attach as participants. If the authority drops mid-match, **quiet failover** to the next-highest-bandwidth peer in the mesh; the match continues.

### Connect codes (Q5)

The friend graph is keyed on **game-generated connect codes**, not IP addresses. Two players behind the same NAT must remain distinguishable, so the connect code is a stable per-account identifier issued at first launch.

**Display format** (used in the friends panel, invitations, chat headers):

```
[nickname]: [agentname]
e.g. "Chris: smarch"
```

- `agentname` -- the player's chosen handle on their account. Stable across sessions.
- `nickname` -- local annotation set by the friend who added them. Different friends can give the same agent different nicknames.

The connect code itself is opaque; the player never types or sees it. Add-friend flow exchanges connect codes via QR / share-link / direct entry as a future polish; phase 1 ships a "paste my friend code" copy-button.

### Signaling: no central infrastructure (Q2)

There is no signaling server. The friend list IS the address book.

When a client comes online:
1. Read the local friend list (stored in user-data root, see Section 3.5).
2. For each friend's connect code, attempt direct STUN-resolved contact.
3. On contact, exchange presence state.
4. Each peer keeps the connection alive with low-rate keepalives (every 30-60 s).

When a client goes offline (clean exit, or detected by missed keepalive), the loss propagates through the mesh.

**Anti-spam / DoS protection** for unsolicited pings: a connect code only accepts presence pings from connect codes already in the local friend list (or pending an accepted invite). Strangers' pings are dropped silently. Rate limit per source IP at 1 ping per 5 seconds; further pings discarded without response.

### NAT traversal: 5-tier escalation, TURN as last resort (Q3 refined)

Per Mike's refinement: try every direct-connection mechanism before falling back to relayed traffic. Each peer pair escalates through the tiers **in sequence**, not in parallel (parallel attempts confuse intermediate NATs and waste time).

| Tier | Mechanism | Cumulative coverage | Bandwidth cost |
|---|---|---|---|
| **1** | **Direct UDP** to the peer's known endpoint | ~30-40% | Zero overhead |
| **2** | **STUN-assisted UDP hole punching** -- both peers exchange reflexive addresses via signaling and send simultaneously | ~85-90% | Zero overhead post-punch |
| **3** | **UPnP / NAT-PMP automatic port mapping** -- request a temporary port mapping on the peer's router | ~88-95% | Zero overhead |
| **4** | **ICE-style candidate gathering and pair testing** -- collect host + server-reflexive + peer-reflexive candidates; test pairs in parallel; pick lowest-latency working pair | ~95%+ | Zero overhead |
| **5** | **TURN relay** -- a peer with bandwidth headroom relays for the pair | safety net | ~2-3x relayer outbound per relayed pair |

**Per-tier escalation logic:**
- Each tier gets a ~2-3 second timeout. On timeout, escalate to the next tier.
- Connection UX surfaces the escalation visibly: "Trying direct connection... Trying NAT traversal... Trying relay..." Silent escalation is opaque; users blame the game when they should be blaming their router.

**Public STUN server:** `stun.l.google.com:19302` or similar (read-only, no authentication, free). Used by tiers 2 and 4.

**TURN library (tier 5):** small open-source TURN implementation (MIT/BSD) compiled into the game binary. Any peer can act as a relay; selection prefers highest measured upload-speed peer with bandwidth headroom (i.e., not the match authority if it's saturated). Quiet failover: if the relayer's bandwidth degrades mid-session (packet loss / RTT spike), reroute through a different relay peer or retry tier 4 in case the network state changed.

**Bandwidth math at scale:** in a 4-player match, with 6 peer pairs:
- All pairs on tiers 1-4: zero relay overhead. The match's bandwidth profile is identical to today's dedicated-server match.
- One pair on tier 5: that pair's relayer sees roughly double its direct-connection outbound (it's mirroring traffic for two endpoints). For most home connections this is fine; the worst-case is a 4-player match with all relays through one peer, where that peer's outbound roughly quadruples.
- The 5-tier escalation exists specifically to keep tier 5 rare. Empirically: ~5% of peer pairs in the wild.

**Unsolvable residue:** of the ~5% that hit tier 5, a sub-fraction will still fail because their network blocks UDP entirely (rare, but real -- some corporate firewalls, some carrier-grade NAT configurations). Document this as the un-fixable residue. UX response: "Connection failed. Your network blocks the traffic this game uses. Contact your network admin or try a different network." No further game-side mitigation possible without a TCP fallback (out of scope -- introduces latency that breaks gameplay anyway).

---

## 3. Presence model

### Always-on lifecycle

```
   OFFLINE
     |
     v  (boot, network up, friend list loaded)
   PRESENCE_BOOTSTRAP
     | (pings each known connect code; collects responses)
     v
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

### Visibility states (Q7)

Three values of `social_visibility`:

| Value | Behaviour |
|---|---|
| **Public** | All connect codes that ping see online status |
| **Friends Only** | Only entries in the local friend list see online status |
| **Appear Offline** | All pings receive "offline" response; no presence broadcast |

Default is **Friends Only** for fresh installs.

### Block list (Q7, separate from visibility)

Block / unblock list is **independent** from the friend list. Block effect is symmetric:

- Blocker becomes invisible to blockee (presence pings from blockee get "offline" response regardless of `social_visibility`).
- Blockee becomes invisible to blocker (presence pings from blocker get dropped on receive).
- Invitations cannot flow in either direction.
- Existing chat threads are sealed; no new messages.

Block list lives in the same user-data root as the friend list (see 3.5).

### Notification categories (Q8)

Two top-level categories with **independent toggles** in settings:

| Category | Examples | Default | Toggle |
|---|---|---|---|
| **Social / Achievements** | "Chris came online", "Mike got Perfect Agent", "Sam joined a music room" | ON | Silenceable independently |
| **Invitations / Messages** | "Chris invited you to play co-op", "Sam: hey, you up?" | ON | Default keeps these on (UX guidance) |

Future additional categories (Q8 leaves the door open) can be added without breaking the toggle UI.

### Per-friend mute (Q9)

Per-category global toggles (Section 3.4) live in **Settings**. Per-friend mute lives in the **social sidebar's friend-row context menu**: right-click / hold-button on a friend row to open a context menu including "Mute notifications from [friend]".

Mute effects:
- Notifications from that friend are suppressed (still recorded; no toast / popup).
- Friend can still send invites (which appear in the invitations panel without a popup).
- Mute is per-friend, not per-friend-per-category. Settings-level toggles handle the per-category cut.

### Persistence: global, not per-profile (Q6)

The friend list, block list, social-visibility setting, and notification toggles are stored at the **device / account level**, NOT per save-game profile. This is one level above per-character / per-save state.

Concretely: store under user-data root in a path like:

```
%APPDATA%/PerfectDark2/social/
    friends.json       # connect codes + nicknames + per-friend mute
    blocks.json        # blocked connect codes
    presence.json      # social_visibility, notification toggles
```

Per-save-game profile (`%APPDATA%/PerfectDark2/saves/<profile>/...`) does NOT include any of this. A new save profile inherits the device's existing social state.

---

## 4. Independent-but-connected sessions

> "The connected players can play together or independently (or in separate groups doing their own thing, but will be connected and able to invite / message one another, get status popups, see each others public / shared mods, listen to music together, and spectate one another in any mode."

Two friends in the same group can simultaneously be in totally separate matches. What synchronises across them in that scenario, and what does not:

| Channel | Sync across the group | Lives separately per match |
|---|---|---|
| **Game simulation state** | No | Match A's bot positions, match B's bot positions are independent |
| **Presence** | Yes (presence layer) | n/a |
| **Chat** | Yes (group chat channel; both peers see regardless of which match they're in) | n/a |
| **Music room** | Optional (see precedence below) | Match-internal music when listening room is off |
| **Mod metadata (public mods page)** | Yes (aggregates from all peers in session) | Mod-driven gameplay state stays in the match |
| **Spectator stream** | On-demand | Defaults to off |
| **Achievement / status popups** | Yes (subject to mute settings) | n/a |

### Listening-room precedence (Q10 explicit rule)

When peers are in a listening room AND in a match together:

- **Match track wins** (Issue 4a's match-scoped track lock from `b3d6e5f9`). The match imposes its scenario music; the listening-room track is suppressed for participants of that match.
- After the match ends, **listening-room track resumes** automatically for those peers.

When peers are in a listening room but NOT in a match (main menu, free roam, lobby), the listening-room track plays.

When some peers are in a match and others are not (independent-but-connected scenario):
- Match peers hear match track.
- Non-match peers hear listening-room track if they joined.
- They are still in the same group; chat / presence / invitations flow normally.

---

## 5. Spectator + Theater unified subsystem (Q12)

> "This subsystem is the basis for Theater mode (saved-match playback). Don't build Theater twice."

### Architectural rule

Spectator (live stream from authoritative peer) and Theater (file-driven playback of a saved match) share **one** camera + control + UI component. Two drivers feed it:

```
                 +--------------------+
   live stream   |                    |   saved-match file
   from authority -> [   subsystem   ] <-  loaded from disk
                 |  (camera, controls,|
                 |   UI, follow logic)|
                 +--------------------+
                        |
                        v
                    rendering
```

The subsystem reads a pure data interface (chr positions, weapons, scoreboard, viewport), abstracted away from where the data came from. Live driver: receives `SVC_*` state from a peer authority. Theater driver: parses a recorded match file and feeds equivalent state.

### Camera + control scheme

| Input | Behaviour |
|---|---|
| **D-pad Up / Down** | Cycle team subset: Players / All Participants / Red Team / Green Team / Blue Team / etc. |
| **D-pad Left / Right** | Cycle members within the current subset |
| **Default camera** | Third-person, follows the spectated character |
| **Right Stick click (R3)** | Toggle first-person view (see what the spectated player sees) |
| **Hold Y** | Detach camera for free flight; release to re-attach to last spectated character |
| **B / Cancel** | Exit spectator (live) or return to Theater menu (file-driven) |

Spectators **do not consume a player slot** in the match (Mike's spec). The authoritative peer's outbound dispatcher emits state to participants AND to spectator-subscribed peers, with private packets (e.g. private chat) gated on `SPECTATOR_VISIBLE` bits.

### Theater extension (file-driven)

Theater needs match-recording infrastructure. The recorder is a separate pipeline (out of scope for the unified subsystem; landed alongside Theater UI when phase 5 ships). It captures the same `SVC_*` state stream the spectator path receives, written to disk. Theater playback reads it back and feeds the subsystem as if it were live.

**Don't build Theater twice:** the camera math, follow logic, team-subset cycling, UI overlay, and key bindings are written ONCE in the subsystem. The recorder + replay file format are the only Theater-specific additions.

### Late-join (live spectator)

A spectator joining mid-match needs a state snapshot + delta replay. Existing `SVC_STAGE_START` semantics apply (same as a player joining mid-match). Limit to one snapshot per spectator-join to discourage abuse.

---

## 6. Music-listening-together (Q10)

### Mechanism

The session host owns a **public playlist** -- a list of audio tracks (typically mod tracks). Songs propagate to other peers through the **existing mod distribution system** (`port/src/net/netdistrib.c` etc.). No new audio-streaming protocol; the file transfer pipe that ships mods between peers also ships listening-room songs.

Each peer plays the track **locally** once the file lands. Sync between peers uses Issue 4b / B-237's `SVC_MUSIC_ADVANCE` v40 (`track_id` + `match_clock_offset_ms`): the host re-broadcasts every 2 s; clients lerp playback rate in [0.97, 1.03] or hard-seek if drift exceeds 5 s.

### Save-or-cache opt

Listeners receive song files as they would any mod. Default: **cached for the session only**, deleted on quit. Listener can opt **"save permanently"** in the chat message UI when the song lands, which moves the file to the user's mod library.

### Precedence with match audio

Documented in section 4; restated for cross-reference:

- In a match WITH listening-room peers: match track wins (Issue 4a lock). Listening-room track is suppressed for that peer.
- After match ends: listening-room track resumes.
- Peers not in a match keep the listening-room track playing.

---

## 7. Player profile + public mods page (Q11)

### Halo 3 File Share lineage

Mike's explicit reference: **Halo 3 File Share**. Each player has a profile page; mods are flagged Public or Private; Public mods are browsable by friends and aggregated by the session.

### Player Profile page

Per-player profile, viewable by any friend with `social_visibility >= Friends Only` toward you. Displays:

- **Stats** (kills, missions completed, etc.) -- per-account, persistent.
- **Current character + head**, rendered live (the same model preview already used in character select).
- **Public mods list** -- mods the player has flagged Public, browsable and downloadable.
- **(Future)** achievements, recent matches summary, custom layouts.

Profile page is reachable from the friends list (right-click / context-menu "View profile") and from chat headers.

### Public Mods Page

Aggregates Public-flagged mods from all peers in the current session into one browsable list. Implementation:

1. Each peer broadcasts a manifest of their Public mods on group join (`mod_id`, `name`, `version`, `size`, `sha256_hash`).
2. Aggregator merges manifests; UI shows the union with per-mod "owner" badges.
3. Click a mod -> initiate file transfer through the existing mod distribution pipe (Section 9, file-transfer plumbing).
4. Hash-checked on receipt for transfer corruption.

### Trust model

> "Hash-checked for transfer corruption, trust-by-distribution otherwise (not strict crypto signing)." -- Mike Q11

Peers in your friend group are trusted to ship benign mods. Hashes catch transfer errors. **No** crypto signing; that would require a CA-style infrastructure we explicitly opted out of. Phase 1 trust scope: trusted-by-friendship.

### Privacy default

**All mods default to Private** (Q11). The player must explicitly flag a mod Public for it to appear on their profile or in the session aggregate. Distribution platform may serve any mod regardless of flag (mod sharing IS a feature), but un-flagged mods are not browsable.

---

## 8. UI surface -- modern main menu

### Top-level UX principle (Q16)

> "I shouldn't have to tap A to enter a panel, or B to exit it to leave the sidebar etc."  -- Mike Q16, elevated to TOP-LEVEL UX PRINCIPLE

**Full controller-only navigation, no drill-in / drill-out friction.** Every screen in the social / sidebar / profile / chat surfaces obeys:

1. **D-pad navigates DIRECTLY** between any visible widget on screen, regardless of which panel it lives in. Cross-panel navigation is the same input as within-panel.
2. **A engages an ACTION** (open a chat thread, accept an invite, play a mod). It does NOT enter a panel. It does NOT confirm a navigation step.
3. **B engages a CANCEL ACTION** (close a popup, dismiss a notification). It does NOT exit a panel.
4. **Sticks** continue to drive analog inputs (look, scroll, list navigation).

The drill-in / drill-out pattern is banned. There is no "press A to enter the friends list, then arrows to navigate, then B to leave." The friends list is just visible; D-pad reaches it directly from any other widget on screen.

**This rule gates every UI section below.** It also constrains layout: panels must be statically visible (or summoned by a single button press, not by drilling), with widgets reachable via spatial D-pad navigation rather than focus stacks.

### Visual style (Q15)

Designer's choice anchored in existing PD2 UI aesthetic. Apply graphic-design principles in relation to the current PD2 visual language. **Consistency over novelty.** Glyphs, colours, fonts, and panel framings inherit from the existing menu styling already shipped.

### Three social surfaces (Q17)

| Surface | Purpose | When shown |
|---|---|---|
| **Sidebar** | Quick access during gameplay or menus; peek view | Always available; toggleable on/off |
| **Social menu** | Full-screen / large-panel; complete friend info; deeper view | On explicit open from main menu / in-game |
| **Private chat** | 1:1 messaging with file/mod attachment support | On click on a friend's "message" action |

Relationship: sidebar is the peek; Social menu is the full view; chat is a per-friend deep-dive. Sidebar exposes the most-used quick actions; full Social menu gives the rest. They share data sources; each is a different window onto the same friend graph.

### Sidebar (peek view)

```
+---------------------------------------+
|  Friends (3 online)                   |
|  - Chris      In Mission "Pelagic"    |
|    [Spectate] [Invite to play]        |
|  - Jason      Online                  |
|    [Invite to play] [Message]         |
|  - Sam        Listening to "Investig" |
|    [Join music room] [Message]        |
+---------------------------------------+
|  Invitations                          |
|  - Chris invited you to play co-op    |
|    [Accept] [Decline]                 |
+---------------------------------------+
|  Recent activity                      |
|  - Chris got "Perfect Agent" 2m ago   |
|  - Sam joined Mike's listening room   |
+---------------------------------------+
```

D-pad navigates directly between any visible row. A engages the action highlighted on the focused row. B dismisses a notification or closes a popup.

### Social menu (full view)

Full-screen panel reachable from main menu. Contains:

- Complete friend list (paginated if > sidebar capacity).
- Profile page for the focused friend (Section 7).
- Public Mods Page aggregator (Section 7).
- Listening-room browser (Section 6).
- Settings shortcut (visibility, notification toggles, block list).

### Private chat (1:1 deep-dive)

Per-friend chat thread with:

- Message history (persistent across sessions).
- Attachment send: drag-drop or "attach" button for files / mods. Uses the file-transfer plumbing (Section 9.4).
- Inline preview of attached files (name, size, type-icon, plus a "save permanently" button when the file lives in a per-session cache).
- **Per-attachment context menu** -- right-click (or controller equivalent: Y button on the focused file row, OR a long-press on A) opens a context menu. See Section 8.5.1 for the full action list.

#### 8.5.1 File storage layout (Q18 amendment)

Received files land in predictable per-type folders so they integrate cleanly with the rest of the game:

| File type | Destination folder | Notes |
|---|---|---|
| **Mod** | `%APPDATA%/PerfectDark2/mods/shared/<friend_agentname>/<mod_id>/` | Tagged "received from [friend]" in the mod manager UI. Shows up alongside locally-installed mods but in a sub-section so the user can tell what they got from whom. |
| **Music track** (from listening-room, Q10) | `%APPDATA%/PerfectDark2/cache/music/<session_id>/` initially | Per-session cache, deleted on quit. "Save permanently" context-menu action moves the file to `%APPDATA%/PerfectDark2/music/saved/`. |
| **Screenshot** | `%APPDATA%/PerfectDark2/screenshots/received/<friend_agentname>/` | Dated subfolder for organization. |
| **Save game** | `%APPDATA%/PerfectDark2/saves/received/<friend_agentname>/` | Never auto-applied; user must explicitly "Apply" via context menu (a future option). |
| **Other / unknown** | `%APPDATA%/PerfectDark2/cache/files/<session_id>/` | Per-session cache; "Save permanently" prompts for a destination. |

Storage paths use the same user-data root as Section 3.5's social storage. Each received file's metadata (sender, timestamp, original filename, sha256) is written alongside it as a sidecar `.meta.json` so the chat history can resolve "the mod I sent Mike on Tuesday" cleanly even after the chat thread scrolls.

#### 8.5.2 Per-attachment context menu (Q18 amendment)

Available actions on the focused file row, surfaced via right-click on mouse or **Y button** (controller default) on the focused row:

- **Open file location** -- opens the OS file manager focused on the saved file.
  - Windows: `ShellExecute(NULL, "open", "explorer.exe", "/select,\"<absolute_path>\"", NULL, SW_NORMAL)` so the file is highlighted in Explorer.
  - macOS: `open -R <absolute_path>` (Finder reveal).
  - Linux: `xdg-open <directory>` (best-effort; not all file managers support file-highlighting).
- **Copy path to clipboard** -- copies the absolute path; useful when the user wants to drag the file elsewhere or reference it in another tool.
- **Apply mod** -- only shown if the file is a recognised mod type. Triggers the existing mod-install flow.
- **Save permanently** -- only shown if the file currently lives in a per-session cache. Moves the file from `cache/` to its permanent home (table above).
- **Delete from cache** -- only shown if the file is in a per-session cache. Removes both the file and its sidecar.
- **Reveal in chat history** -- jumps the chat scroll position back to the message where this file first appeared (useful after long scrollback).

**Controller binding choice.** Y is the default because:
- A is "engage action on focused element" (open the chat thread of the focused friend; activate a button) per the top-level UX principle (Section 8.1).
- B is "cancel / exit at top level" per the same principle.
- X is reserved for a primary in-context action (e.g. "send" in a chat composer).
- Y is the natural fourth face button and is unbound during chat-history navigation. It maps semantically to "more options" / "context menu", consistent with how many console UIs use Y.

If chat-input mode is active (text-entry focus), Y is captured by the text-input IMC (`g_ImcTextInput` priority 30) and behaves as a normal text key. The context menu is only reachable when focus is on a file row outside the text-entry box.

### Status indicator (top-right)

```
[Offline]            -- not connected to presence
[Online * Mike]      -- ONLINE_IDLE
[In Mission * Mike]  -- IN_MATCH, mission
[In CS * Mike]       -- IN_MATCH, combat sim
[Spectating * Mike]  -- SPECTATING someone
[Theater * Mike]     -- watching a saved match
[Appear-offline]     -- connected but visibility = Appear Offline
```

Click / direct-D-pad to indicator: quick toggle for visibility (Public / Friends Only / Appear Offline) and pause notifications.

---

## 9. Wire protocol additions

### New packets

| Packet | Channel | Direction | Notes |
|---|---|---|---|
| `CLC_PRESENCE_PING(my_connect_code, my_state)` | reliable, low-rate | client -> peer (direct) | Friend-list-driven; rate-limited per source |
| `SVC_PRESENCE_PONG(my_state, my_visibility)` | reliable | peer -> client (direct) | Visibility may say "appear offline" |
| `CLC_INVITE(target_connect_code, kind, payload)` | reliable | client -> peer | Match invite, music-room invite, chat invite |
| `SVC_INVITE_RESPONSE(invite_id, accept)` | reliable | peer -> client | |
| `CLC_CHAT_MESSAGE(target, text, attachments)` | reliable | client -> peer | Attachments are file-transfer descriptors |
| `SVC_CHAT_MESSAGE(sender, text, attachments)` | reliable | peer -> client | Mirror |
| `CLC_FILE_TRANSFER_REQUEST(file_descriptor, sha256)` | reliable | client -> peer | Used by mods, music, chat attachments (Q18 plumbing convergence) |
| `SVC_FILE_TRANSFER_CHUNK(seq, bytes, more)` | reliable | peer -> client | Standard chunked transfer |
| `CLC_SPECTATE_REQUEST(target_player, mode)` | reliable | client -> match authority | `mode` selects free-cam vs follow |
| `SVC_SPECTATE_ACK(allowed, stream_token)` | reliable | match authority -> client | |
| `SVC_PROFILE_INFO(connect_code, profile_payload)` | reliable | peer -> client | Stats, character preview, public mod manifest |
| `SVC_PUBLIC_MODS_MANIFEST(mod_list)` | reliable | peer -> session | Aggregator collects these on group join |
| (existing `SVC_*` state) | per existing | match authority -> spectator | `SPECTATOR_VISIBLE` bit added to writes |

### Versioning

`NET_PROTOCOL_VER` is currently 40 (Issue 4b). The connectivity layer will bump to v41 when phase 1 lands. Phase-2 features add minor revisions (presence-only adds, no protocol-break additions). Match-state packets stay aligned with the existing `SVC_*` evolution.

### Version mismatch UX (Q14)

When peer connect attempts encounter a version difference (you on v41, friend on v40), the invite/connect flow surfaces a clear plain-English error. **Exact format Mike specified:**

```
"Invite failed. Version info (you: 0.1.0, smarch: 0.0.165 (old))"
```

Where:
- `0.1.0` -- local client version (PD2 release version, not protocol version).
- `smarch` -- friend's agentname.
- `0.0.165` -- friend's reported version.
- `(old)` -- annotation showing which side is behind. If you are behind, your version gets `(old)`.

Surfaces at:
- Invite dialog (when sending an invite).
- Connect attempt failure toast (when receiving an invite from a stale peer).
- Friend-row context menu, in greyed-out form, when the friend is online but on an incompatible version.

### File transfer plumbing (Q10, Q11, Q18 convergence)

The same `CLC_FILE_TRANSFER_REQUEST` + `SVC_FILE_TRANSFER_CHUNK` plumbing serves three features:

- **Mod distribution** (Q11 public mods, Q11 chat-attached mods) -- the existing mod distribution system extends to user-driven sends.
- **Listening room track delivery** (Q10) -- songs propagate as mods.
- **Chat attachments** (Q18) -- arbitrary files / mods sent via private chat.

Single transport, single corruption check (sha256), single user UX ("X is sending you a file: filename, NN MB"). Don't build three transports.

---

## 10. Phasing -- 6 phases

Each phase build-verifies and commits independently. **Phase 1 must ship fully before phase 2 starts** (Mike Q13 discipline note: completion-before-pivot).

### Phase 1 -- minimum viable replacement (target: 1-2 sprints)

**Scope:** two friends can play together without the dedicated server.

- Always-on presence layer with peer pings (Section 3).
- Friend list / block list / visibility / connect codes (Sections 3.5, 3.7, 2.2).
- Direct invite + accept flow (Section 9).
- P2P group session, 4-peer mesh, authority election (Section 2).
- **All 5 NAT traversal tiers** ship in Phase 1 (Mike-confirmed 2026-04-24): direct UDP, STUN punch, UPnP/NAT-PMP, ICE candidate gathering, TURN relay. The full stack lands at once; no staged rollout. (Section 2.4)
- Sidebar UI, status indicator, invitations panel (Section 8 sidebar).
- Notification toggles in settings (Q8).
- Version-mismatch UX (Q14 string format) (Section 9.2).
- **Real-NAT verification matrix before ship.** Required test environments:
  - Open / no-NAT (direct UDP succeeds at tier 1)
  - Full-cone NAT (tier 1 succeeds)
  - Restricted-cone NAT (tier 2 STUN punch succeeds)
  - Port-restricted-cone NAT (tier 2 STUN punch succeeds with simultaneous-send timing)
  - Symmetric NAT (tier 4 ICE pair testing succeeds, OR tier 5 TURN if not)
  - Carrier-grade NAT / cellular hotspot (tier 5 TURN required; verify quiet failover)
  - Corporate firewall blocking unsolicited UDP (tier 5 TURN required; some sub-fraction will be unconnectable, verify error UX)
  - Mixed pairs (one peer behind symmetric NAT, one open) so each tier's per-pair escalation is exercised separately
- Synthetic-NAT testing alone is not sufficient; matrix must include lab plus at least 3-4 beta-tester networks across ISPs.

**Files touched (estimate):**
- `port/src/net/presence.c` (new, ~700 lines)
- `port/src/net/p2p_nat.c` (new, ~700 lines: tier-1 direct UDP attempt, sequential-tier escalation logic with per-tier timeouts and UX hooks, tier-1/tier-2 dispatch)
- `port/src/net/p2p_stun.c` (new, ~400 lines: STUN client for tier 2 + reflexive-address gathering used by tier 4)
- `port/src/net/p2p_upnp.c` (new, ~500 lines: UPnP IGD + NAT-PMP probe and port-mapping for tier 3; targets the common consumer-router protocols)
- `port/src/net/p2p_ice.c` (new, ~500 lines: tier 4 ICE-style candidate gathering and pair testing; integrates the host / server-reflexive / peer-reflexive candidate lists)
- `port/src/net/p2p_turn.c` (new, ~600 lines: tier 5 embedded TURN library + relay logic + bandwidth-aware relayer selection + quiet failover)
- `port/src/net/group_session.c` (new, ~500 lines, mesh + authority election + match dispatch)
- `port/src/net/netmsg.c` -- new `CLC_PRESENCE_*`, `CLC_INVITE_*` packets (~250 lines added)
- `port/include/net/net.h` -- `NET_PROTOCOL_VER 40 -> 41`, packet decls
- `port/fast3d/pdgui_menu_mainmenu.cpp` -- status indicator, sidebar friends panel
- `port/fast3d/pdgui_menu_friends.cpp` (new, ~500 lines, sidebar + friend-row actions)
- `port/fast3d/pdgui_nat_diagnostics.cpp` (new, ~250 lines, real-NAT verification matrix harness for the in-build acceptance test)
- User-data root social storage in `port/src/social_store.c` (new, ~250 lines)

**Total Phase 1 estimate revised UP** to ~5500 lines of new code (was ~2500 in the staged-NAT plan). The full NAT stack is the largest single contribution; the discipline note (Q13 -- completion-before-pivot) carries extra weight here because the temptation to ship "tiers 1-3 only and call it Phase 1" is real but Mike has explicitly ruled it out.

**Risk note (Q13):** completion-before-pivot. Phase 1 must hit its exit criteria before any phase 2 scope opens. The temptation to slip "just text chat" forward into phase 1 is real; resist it. Chat with attachments (Q18) is large enough to deserve its own phase.

**Exit criteria:**
- Two friends on different ISPs, no dedicated server running, can launch the client, see each other online, exchange invitations, start a co-op match together.
- Presence stays connected through match-start and match-end.
- All 5 NAT-traversal tiers verified on the real-NAT matrix above. Each tier's success path exercised at least once on a real network. TURN failover tested by forcing symmetric-NAT; relayer-bandwidth-degrade quiet-failover tested by throttling the relayer mid-match.
- Tier escalation UX feedback ("Trying direct connection... Trying NAT traversal... Trying relay...") is user-visible and accurate; each tier transition fires within its 2-3s timeout.
- Pairs that genuinely cannot connect (carrier-grade NAT or UDP-blocked corporate firewall) see the explicit "Connection failed. Your network blocks the traffic this game uses." error UX, not a silent hang.
- Version-mismatch UX surfaces correctly on a v40<->v41 attempt.

### Phase 2 -- chat + file transfer + status popups / achievements

NAT traversal completes in Phase 1 (all 5 tiers); Phase 2 reorients around social richness.

**Scope (chat, Q18):**
- Private chat UI (Section 8.5).
- File transfer pipe (`CLC_FILE_TRANSFER_REQUEST` + chunks). Single transport that Phase 3 and Phase 4 reuse.
- Chat attachments: send a mod or file through chat.
- Per-attachment context menu with "Open file location" + Apply / Save / Copy path actions (Section 8.5.2).
- Predictable per-type storage layout (Section 8.5.1).

**Scope (status popups + achievements):**
- Toast notification system: bottom-right popups for "X came online", "X started playing Y", "X got [achievement]", "X invited you to play".
- Achievement-event broadcast over the presence channel: when the local player earns an achievement, presence layer pushes a `presence.achievement` event to subscribed friends (subject to mute settings).
- Mission-start / mission-complete / scenario-change events similarly broadcast as derived presence updates.
- Recent-activity feed in the sidebar (Section 8 sidebar layout) populated from received presence events.
- Settings gate: respect the Q8 notification-category toggles (Social/Achievements vs Invitations/Messages) at the toast layer.

**Dependencies:** Phase 1.

**Files touched (estimate):**
- `port/src/net/file_transfer.c` (new, ~600 lines, generic chunked transfer pipe shared by chat / mods / music)
- `port/fast3d/pdgui_menu_chat.cpp` (new, ~700 lines, 1:1 chat thread UI with attachment handling + context menu + Y-button reveal)
- `port/src/net/netmsg.c` -- new `CLC_CHAT_*`, `CLC_FILE_TRANSFER_*`, `SVC_PRESENCE_ACHIEVEMENT` packets (~250 lines added)
- `port/src/social_events.c` (new, ~300 lines, achievement / mission-event broadcast wiring)
- `port/fast3d/pdgui_notifications.cpp` (new, ~400 lines, toast notification system + per-category mute gate)

**Exit criteria:**
- Chat round-trips with text + attached mod between two peers; receiver gets "save permanently" prompt and the mod lands in their library at the predictable path; per-attachment context menu's "Open file location" launches Explorer on Windows (Finder reveal on macOS, xdg-open on Linux) focused on the saved file.
- File transfer pipe is generic enough to serve Phase 3 (mod manifests) and Phase 4 (listening-room track delivery) without rework.
- "X came online", "X started playing Y", "X got [achievement]" toasts surface correctly on a friend client.
- Per-category mute toggles in Settings actually silence the right toasts; per-friend mute (Section 3.6) suppresses toasts from a specific friend without breaking invites.

### Phase 3 -- public mods page + player profile

**Scope:** Section 7 in full.

- Mod public/private flag.
- Player Profile page with character preview, stats, public mods list.
- Public Mods Page aggregator across the session.

**Dependencies:** Phase 2 (file-transfer pipe).

**Exit criteria:** Mike can browse Chris's profile, see his public mods, click one, and have it land in his own library. Session aggregator merges manifests correctly when 3+ peers join.

### Phase 4 -- listening-room (music together)

**Scope:** Section 6 in full.

- Public playlist owned by session host.
- Track distribution via the file-transfer pipe (existing mod distribution).
- Issue 4b sync (already shipped, B-237 from Priority G) extended to room scope.
- Save-or-cache opt for listeners.
- Match-vs-room precedence (Section 4.5 + Section 6.4).

**Dependencies:** Phase 2 (file transfer), Phase 3 (mod manifests for tracks), Issue 4b / B-237 (already landed).

**Exit criteria:** two friends in the main menu hear the same track in sync; one enters a match and switches to match audio; on match exit, room track resumes.

### Phase 5 -- spectator (live)

**Scope:** Section 5 spectator half. Theater (saved-match playback) is queued for phase 6 because the recorder is its own scope.

- Unified subsystem (camera, controls, UI) per Section 5.2.
- Live driver: subscribe to authoritative peer's `SVC_*` stream.
- Late-join snapshot path.
- `SPECTATOR_VISIBLE` bits on existing `SVC_*` writes.

**Dependencies:** Phase 1.

**Exit criteria:** Mike picks a friend's match, clicks Spectate, sees the match in real-time; D-pad navigation through team subsets and members works; R3 toggles first-person; hold-Y free flight works.

### Phase 6 -- Theater (saved-match playback)

**Scope:** Theater driver feeding the same Phase 5 subsystem.

- Match-recording infrastructure (write `SVC_*` stream to disk).
- Replay file format.
- Theater UI: browse saved matches, play, pause, seek.

**Dependencies:** Phase 5 (the unified subsystem). Theater is the second driver; the camera + controls + UI are reused.

**Exit criteria:** record a match, exit, find it in Theater, play it back with full camera control. Don't-build-Theater-twice rule (Section 5.2) holds: zero new camera / control / UI code; only the recorder + replay parser are Theater-specific.

---

## 11. Decisions (was Open Questions; Mike answered all 18 on 2026-04-24)

| ID | Question | Decision |
|---|---|---|
| **Q1** | Mesh vs star; authority? | Mesh confirmed. **Within-match authority required**; elected by highest upload speed (with match-initiator as fallback when speed-test data unavailable). Authority is per-match, not persistent. (Section 2.1) |
| **Q2** | Signaling infrastructure? | **No central infra.** Direct peer-to-peer pings to known friend connect codes, with rate limits / DoS protection. Direct broadcast on coming online / disconnecting / leaving session. **The friend list IS the address book.** (Section 2.3, 3.2) |
| **Q3** | NAT traversal strategy? | **5-tier escalation, all 5 tiers ship in Phase 1** (Mike refined twice 2026-04-24: first the 5-tier model, then "include all 5 tiers" for Phase 1 -- no staged rollout). Order: (1) Direct UDP, (2) STUN-assisted hole punching, (3) UPnP / NAT-PMP, (4) ICE candidate gathering + pair testing, (5) TURN library shipped in-game. Per-pair sequential escalation with ~2-3s timeout per tier and visible UX feedback. Bandwidth: tiers 1-4 zero overhead; tier 5 ~2-3x relayer outbound per relayed pair. Empirical coverage: ~30-40% (1) -> ~85-90% (2) -> ~88-95% (3) -> ~95%+ (4) -> safety net (5). Phase 1 must verify all 5 tiers on the real-NAT matrix (open / full-cone / restricted-cone / port-restricted / symmetric / carrier-grade / corporate firewall) before ship. (Section 2.4, Phase 1 scope) |
| **Q4** | Dedicated server? | **REMOVED from current releases.** Future return scoped strictly to matchmaking + hosted-server-networking, never direct friend-play. C-1 / MASTER-C5 audit-pole resolved by removal for friend-play; reduces to optional matchmaking work later. (Section 0) |
| **Q5** | Identity bootstrap? | **Game-generated connect codes**, not IP-based (two players behind same NAT must be distinguishable). Display format: `[nickname]: [agentname]` e.g. "Chris: smarch". `agentname` is account-stable; `nickname` is local annotation. Codes are stable per-account. (Section 2.2) |
| **Q6** | Friend list persistence scope? | **Global, NOT per-profile.** Stored in user-data root above per-profile save folder. New save profile inherits the device's existing social state. (Section 3.5) |
| **Q7** | Privacy / blocking? | **Three-state visibility:** Public / Friends Only / Appear Offline. **Block list separate from friend list.** Block effect symmetric: invisible to blocker AND blockee, no invitations possible in either direction. (Section 3.3, 3.4) |
| **Q8** | Notification categories? | At minimum two with independent toggles: **Social/achievements** (silenceable separately) and **Invitations/messages** (kept on by default). Per-category toggles in settings. (Section 3.5) |
| **Q9** | Per-user mute? | **Per-friend mute** in social sidebar's friend-row context menu. Per-category global toggles in settings (Q8). Mute suppresses notifications without blocking invites. (Section 3.6) |
| **Q10** | Music-listening-together? | Session host owns a public playlist. Songs propagate via the **existing mod distribution system** (no new audio-streaming protocol). Listeners opt save-permanently or cache-for-session. **Match track wins** over listening-room track when in same match (Issue 4a lock); room resumes on match end. (Section 6) |
| **Q11** | Mod sharing model? | **Halo 3 File Share lineage.** Mods default Private; flag Public to expose. Browse via **Player Profile page** (per-player, with character preview + stats + public mods) and **Public Mods Page** (session-aggregate from all peers). Hash-checked for transfer corruption; trust-by-distribution otherwise (no crypto signing). (Section 7) |
| **Q12** | Spectator architecture? | **Spectator + Theater unified subsystem.** One camera + control + UI; two drivers (live stream / saved-match file). Don't build Theater twice. Camera scheme: D-pad UD = team subset, D-pad LR = member, default 3rd-person follow, R3 toggles 1st-person, hold-Y free flight. **No player slot consumed.** (Section 5) |
| **Q13** | Phase 1 scope? | Accepted. **Discipline note: completion-before-pivot.** Phase 1 must ship fully before phase 2 starts. Resist the temptation to slip phase-2 features early. (Section 10.1 risk note) |
| **Q14** | Version mismatch UX? | **Exact string format:** `"Invite failed. Version info (you: 0.1.0, smarch: 0.0.165 (old))"`. Surfaces at invite dialog, connect-attempt failure toast, friend-row context menu greyed entry. (Section 9.2) |
| **Q15** | Visual style? | Designer's choice **anchored in existing PD2 UI aesthetic**. Apply graphic-design principles in relation to current visual language. **Consistency over novelty.** (Section 8.2) |
| **Q16** | Controller UX? | **TOP-LEVEL UX PRINCIPLE: full controller-only navigation, no drill-in / drill-out friction.** D-pad navigates DIRECTLY between any visible widget, regardless of panel. A engages action, B engages cancel. Sticks drive analog. Drill-in / drill-out is banned. (Section 8.1) |
| **Q17** | Social surfaces? | **Three:** Sidebar (quick peek during gameplay/menus), Social menu (full-screen complete view), Private chat (1:1 deep-dive with file/mod attachments). (Section 8.3) |
| **Q18** | Chat scope? | Phase 2: **text chat with file/mod attachments**. Voice chat queued for later. File-transfer plumbing serves chat AND mods AND music (Q10, Q11 convergence on a single transport). **Q18 amendment (2026-04-24):** received files land in predictable per-type folders (`mods/shared/`, `music/saved/`, `screenshots/received/`, etc.); per-attachment context menu (Y on controller) exposes "Open file location" -- using `explorer.exe /select,"<path>"` on Windows and Finder/xdg-open on other OSes -- plus type-appropriate actions (Apply mod, Save permanently, Delete from cache, Copy path). (Section 8.5, 8.5.1, 8.5.2; Section 9.4; Phase 2) |

---

## 12. Out of scope for this design

- Replay analytics (post-match leaderboard / heatmap / stats aggregation across many matches).
- Anti-cheat replay validation.
- Cross-platform play.
- Voice chat (text + attachments in scope; voice is a phase-7+ stretch).
- Public-internet matchmaking (the dedicated server's possible future return; explicitly out per Q4).
- CA-style mod signing / trust authority.

---

## 13. Reading order for review

1. **Section 0** -- strategic positioning + audit-pole resolution.
2. **Section 11** -- the decisions table; canonical record of Mike's Q1-Q18 answers.
3. **Section 8** -- UI surface, especially the top-level UX principle (Q16) which gates every screen.
4. **Section 10** -- phasing.
5. **Sections 1-7, 9** -- subsystem detail in order.

If section 0 is wrong, nothing downstream matters. Section 11 is the single canonical record of what Mike chose; treat it as the ground truth when cross-referencing the rest of the doc.
