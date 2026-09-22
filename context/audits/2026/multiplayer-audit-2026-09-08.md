# Multiplayer functionality audit, 2026-09-08

Workbench: T-NETWORKING-010. Auditor: Codex / GPT-6.

## Evidence boundary

This is a current-working-tree source audit, including existing uncommitted changes, on canonical dev HEAD `fadf9ff6a38e3164a25ebd522085a327d3814631`. No production code was changed. No build, executable test, game, network experiment, capture, physical-controller check, or audible voice test ran. Source defects below are traced mechanisms, not newly reproduced runtime incidents. Performance implications are unmeasured unless explicitly calculated.

Menu and asset sessions are concurrently active. The latest recorded ordinary-client gate stops during asset boot on hand-source identity; that owner is repairing it. The Sep8 menu37 unit also awaits current-unit verification. These facts prevent inheriting prior binary acceptance. The adjacent JSON receipt records the inspected source hashes at audit closeout; it is not a build freeze or a complete product manifest.

Existing evidence is substantial: the Aug12 real-peer Needler distribution document records 87/87 and concrete random-stage selection 34/34; connectivity records later v58 co-op, Counter-Op, return, authority-role and reconnect receipts. Those are historical regression evidence. Current protocol is v59, not v58. No assertion here that multiplayer is entirely unimplemented or that those earlier passes were false.

## Flow coverage

| Player-facing flow | Source/evidence inspected | Assessment / next action |
|---|---|---|
| Add friend, invite, accept, share | presence.c, group_session.c, social_store.c, pdgui_friends.cpp | Signed identity/route foundation exists; chat endpoint and voice audience are broken. |
| NAT and connect | p2p.c, STUN/UPnP/relay modules, group_session.c, networking Workbench items | Candidate and authority-route separation exists; TURN fails closed; real WAN proof remains open. |
| Social lobby and room membership | pdgui_lobby.cpp, pdgui_menu_lobby.cpp, room.c, CLC room handlers | Listen-host UI dispatch and membership rollback defects. |
| Settings and playlist | room dirty flush, update handlers, local send adapters, lobby resync | Outgoing host settings have an adapter; incoming remote-leader updates lack authoritative per-room persistence (F18). |
| Manifest and content | netmanifest.c inventory, distribution sender/receiver, public-source constraints, existing Needler evidence | Hash/admission/recovery foundation exists; ordering, approval and pacing gaps remain. |
| Ready, countdown and abort | distribution completion-to-manifestCheck, room-leave ready abort, stage-ready protocol, lifecycle tests | Content failures can prevent readiness; exact post-load epoch barrier exists. |
| Stage load | stage-start/reconnect entry points, source constraints, current asset gate | Current asset boot blocker is separate active work. |
| Live gameplay | CLC/SVC movement, walking consumer, NPC/prop/snapshot protocol, GPU-swarm sender | Movement trust and correction defects; broad sync vocabulary exists, but no new convergence proof. |
| Match end and return | lobby renderer cleanup, resync lifecycle tests, prior v58 evidence | Production cleanup/resync paths exist; current full two-match menu flow remains unverified. |
| Drop, reconnect and independent/grouped play | preserved-slot auth, group transport failure lifecycle | Reconnect is distinct from fresh drop-in; new mid-match joins rejected; no live authority migration. |

## Highest-priority defects

### F01. Listen-host Social Lobby Create/Join does not execute on the host (P1)

`pdgui_lobby.cpp:509` routes the shipping listen host into the social lobby. `pdgui_menu_lobby.cpp:128` and `:386` serialize CLC_ROOM_CREATE/JOIN and call `netSend(NULL, ...)`. In server mode, `net.c:3979` treats NULL as broadcast to peers, not a loopback request. There is no local room mutation in these callbacks. A host with no peers sends nowhere; with peers it sends client opcodes into their server-message decoders.

Use one create/join operation adapter that invokes the authoritative handler locally for the listen host and sends CLC for remote clients. `netmsg.c:14190` already demonstrates that pattern for room settings. Verify the actual host lobby buttons, not only CLI/direct-start helpers.

### F02. Failed room changes remove membership before destination admission (P1)

`netmsgClcRoomJoinRead` leaves the old room at `netmsg.c:13619` before `roomJoin` checks destination capacity. If the destination is full, the handler returns with `srccl->room_id` still naming the old room, but `roomLeaveInternal` has removed its membership and may have destroyed it (`room.c:234`). Create has the same class: old membership is removed at `netmsg.c:13492` before access validation and room allocation.

Prepare and validate the destination first; commit membership, leadership and client assignment together. Failures must preserve the old room and send a structured result to the UI. Include full-room, no-free-room, duplicate request and same-room cases.

### F03. Distribution completion is ordered on a different channel from its bytes (P1)

BEGIN and END use NETCHAN_CONTROL (`netdistrib.c:896`, `:1009`); all chunks use NETCHAN_TRANSFER (`:1026`). These are different ENet channels (`net.h:418`). Per-channel reliability does not establish ordering between them. CHUNK before BEGIN is discarded as unknown (`:1520`); END before all chunks immediately verifies the partial buffer, fails the digest, frees the slot and fails the transfer (`:2762`). There is no pending-END or pre-BEGIN staging state.

Put each transfer's ordered framing on the same reliable stream, or implement a transfer state machine that tolerates legal cross-channel arrival orders. Test delayed control, delayed data, retransmission and multiple components. Earlier loopback success does not exercise this failure condition.

### F04. Download approval is unreachable and loses completion (P1 when configured)

`netDistribGetPendingApproval`, `netDistribApproveTransfer` and `netDistribDeclineTransfer` have definitions/declarations but no production UI callers. BEGIN above the configured threshold sets `needs_approval` (`netdistrib.c:1482`), while chunks still buffer. END returns without retaining its result (`:2787`). Even a future caller approving afterward only clears the flag (`:3333`); it does not complete buffered data. Decline only frees the slot, without resolving the transfer set or notifying the ready gate.

Default threshold is 256 MiB while the sender limits one archive to 50 MiB, so ordinary default transfers do not reach this prompt. A supported lower `Net.DistribTrustThresholdMB` setting does expose it. Make consent a real protocol/UI state before bulk transmission, with durable completion, rejection and cancellation results.

### F05. Remote movement is substantially client-position-authoritative (P1)

`netmsg.c:921` reads position, speed, angles and command flags directly. The CLC handler at `:1876` checks message structure, weapon ownership, vehicle intent and stale ticks, but does not reject non-finite movement floats, implausible displacement/speed or client-supplied force-position flags. `netbufReadF32` is a raw bit conversion (`netbuf.c:110`). Remote players are marked `isremote` on the host (`net.c:4178`, `:4223`). `bwalkTick` then calls `bwalkUpdateRemote`, which directly copies received position on the first sample, a force flag, or a displacement over 512 units (`bondwalk.c:64`).

Thus the authority follows client position reports at an important gameplay boundary. Malformed values or a modified client can bypass expected movement constraints; even honest divergence has no explicit acceptance contract here. Separate untrusted input intent from authoritative positions, validate numeric domains and timing, and reconcile the local prediction against authority. This is a structural change that must preserve jumping, collision and vehicle behavior.

### F06. Private text chat uses the presence port (P1)

Presence caches its sender's UDP endpoint in `socialFriendUpdateEndpoint` (`presence.c:893`). `chat.c:604` retrieves that same endpoint and uses its nonzero port unchanged; the fallback to CHAT_PORT is unreachable because `socialFriendGetEndpoint` rejects zero ports (`social_store.c:1037`). Chat's receiver binds 27106 (`chat.c:489`), while presence normally binds 27105 and can fall back to an ephemeral port. No chat forwarding from the presence receiver was found.

A normally discovered friend therefore receives chat datagrams at the presence endpoint. Define explicit service endpoints or carry social messages over one authenticated session transport. Test two normal peers whose endpoints were populated by actual presence traffic.

### F07. Voice sends the microphone to all cached friends (P1)

`broadcastVoiceFrame` iterates `socialFriendCount()` and sends to every friend with a fresh cached endpoint (`voice.c:350`). It does not restrict recipients to the accepted party, current room, team or explicit call. Incoming voice likewise admits allowlisted friends without a group/session check (`:437`). A cached friend outside the current match can receive push-to-talk audio. Voice also targets fixed UDP 27108 rather than a negotiated per-service NAT endpoint.

Make the audience explicit and session-scoped, authenticated in each frame, with a visible current voice channel. Keep friend mute/block separate from outgoing audience authorization. Verify that an online nonparticipant receives no microphone packets.

## Reliability, performance and incomplete UX

### F08. Private chat reports local success without delivery (P2)

`sendChunk` ignores `sendto` results (`chat.c:557`). `chatSendText` appends outgoing history and returns success regardless (`:759`). ACK is declared but inbound accepts only TEXT; incomplete fragments expire after ten seconds. No retransmission or delivered/failed state exists. Even after F06, loss can silently erase a message, especially multi-packet messages. Add acknowledgement, bounded retry, deduplication and visible send status.

### F09. Voice has no simultaneous-speaker mixer or packet timeline (P2)

`voice.c:428` discards sequence and destination fields. Every decoded speaker frame is appended to the same SDL playback queue (`:451`), rather than mixed for the same playback interval. With two continuous 20 ms streams arriving every 20 ms, approximately 40 ms of audio is queued per interval. That creates increasing latency and serializes speech. There is no queue-duration bound, jitter buffer, replay window or sequence-based loss handling in this receive path.

Use bounded per-speaker jitter/replay state, loss concealment and one real-time mixed output. Reordering, replay and multiple talkers need tests; this is not an audible quality verdict from this audit.

### F10. Voice-activated mode is selectable but does not transmit (P2)

`pdgui_friends.cpp:1595` offers Voice-activated. `voiceLocalIsTransmitting` returns zero for that mode (`voice.c:574`), and `voiceTick` only captures while `s_PttActive` (`:607`). Implement the level detector and mode lifecycle or clearly disable the unsupported choice. Builds without HAVE_OPUS retain the same UI with no codec transport, so capability reporting also needs checking.

### F11. Asset transfer work is synchronous and bursty (P2)

`netDistribServerTick` processes one *whole transfer*, not one chunk (`netdistrib.c:1196`). It reads/builds the archive, compresses and hashes it, then allocates and queues every reliable packet in one call (`:960-1059`). Multiple clients repeat archive preparation. Client END synchronously decompresses, extracts, validates and registers content (`:2818` onward).

This can stall the host/game thread and queue large bursts while other players are active. Use source-hash-keyed prepared archives, bounded worker preparation, fair per-peer byte budgets and backpressure. Preserve existing hash, temporary-content and transactional admission checks. Measure frame time, memory and gameplay latency during downloads before choosing budgets.

### F12. Transfer limits, accounting and progress disagree (P2)

The sender caps an archive at 50 MiB and the named session cap is 200 MiB (`net.h:424`). BEGIN separately allows 512 MiB; END later rejects above 256 MiB. The aggregate check adds incoming *raw* bytes to previously received *compressed* bytes (`netdistrib.c:1431`, `:1558`) and does not reserve pending transfers. Progress similarly divides compressed bytes received by raw archive size (`pdgui_lobby_distrib.cpp:364`). It can display well below 100 percent immediately before successful completion.

Specify compressed wire size, expanded/install size, aggregate reservation and progress independently. Expose why content is ineligible before download. Larger valid custom packages currently cannot be distributed seamlessly, and the receive-side accounting is not a consistent resource contract.

### F13. Receive-buffer allocation failure leaves false capacity (P2)

`netdistrib.c:1543` doubles `compressed_cap` before calling `realloc`. If allocation fails, it returns with the old pointer but the larger capacity. A later chunk can pass the capacity check and copy beyond the actual allocation. `expected_chunk` was also advanced before the failed write. Commit capacity and chunk state only after allocation/copy succeeds, or terminate the transfer cleanly. Verify allocator failure, not just successful small assets.

### F14. Duplicate movement correction writes all axes into X (P2)

`bondwalk.c:84-86` assigns `delta.x` three times, leaving Y and Z zero. In the zero tick-delta branch, the final Z error becomes X movement and the other corrections disappear. The server CLC handler rejects stale duplicates, but SVC receive does not contain the matching tick rejection. Fix the vector and establish a shared duplicate/stale sample contract. The frequency of visible effects is unmeasured.

### F15. Room and social UX expose unfinished functionality (P2/P3)

Social lobby Join always sends an empty password (`pdgui_menu_lobby.cpp:389`); Create hardcodes an open room and default capacity (`:128`). Invite-only admission permits only the creator, with no invite-grant check (`netmsg.c:13612`). Room rejection mostly logs and returns without a specific client-facing result. Server Chat is literally coming soon (`pdgui_menu_lobby.cpp:417`). Friend character preview and stats are placeholders (`pdgui_friends.cpp:805`). The lobby labels listen hosting as dedicated, and its local connect-code cache is generated only once (`pdgui_menu_lobby.cpp:205-244`), so a later host endpoint can leave this particular copy control stale.

Prioritize actionable errors, password/access/capacity controls, real invite grants and functional group/room chat. Then connect profile presentation and refresh endpoint-derived UI. Existing private friend chat is a different surface from room chat.

### F16. Difficult-NAT fallback and host continuity remain incomplete (known scope)

`p2pTurnStart` always reports failure and returns -1 (`p2p_turn.c:60`); no relay socket/path is opened. This fail-closed behavior is intentional, but leaves no working TURN fallback for peers who need it. T-NETWORKING-001/002/003/006 remain partial and T-NETWORKING-005 missing. Signed candidate work must not be mistaken for a fully qualified game route through real routers.

Fresh mid-match connections are rejected unless they carry a preserved-slot reconnect hint (`net.c:3118`). `groupSessionOnTransportDisconnected` suppresses implicit rejoin until a fresh invite/session (`group_session.c:656`). There is no live authority migration; T-NETWORKING-008 explicitly leaves it post-1.0. These are product limitations, not reasons to weaken existing authentication or automatically promote migration into release scope.

### F17. GPU-swarm replication pays for the entire maximum pool (P2, conditional mode)

When GPU swarm mode is active, `netSendGpuSwarmState` reads the full 4096-bot state and broadcasts all 4096 slots even if fewer are live (`netmsg.c:13352`). At the intended 10 Hz, 4096 x 20 bytes x 10 is 819,200 payload bytes/s per receiver, before headers and fragmentation. It uses global `netSend(NULL, ...)`, not a room participant set. GPU readback, copying, unused slots and per-send logging also add work. This is a calculated cost, not a measurement or a claim about ordinary non-swarm matches.

Publish exact live count and room-scoped recipients first, then measure packet sizing, readback and quantization. Do not reduce simulation population or functionality to improve the metric.

### F18. Room settings/playlist rebroadcast is not authoritative room storage (P1)

`netmsgClcRoomSettingsUpdateRead` and `netmsgClcRoomPlaylistUpdateRead` validate the room leader and rebroadcast received values (`netmsg.c:14018`, `:14130`), but do not persist the accepted settings/playlist into a room-owned snapshot. When the leader is a remote client and the listen host is a room member, the host's peerless local slot receives a `netSend` that is deliberately discarded (`net.c:4018`), so its local config/playlist is not updated either. Lobby resync later serializes process-global `g_MatchConfig` and the host audio playlist, not the requested room's last accepted state (`netmsg.c:9510`).

This can leave the host viewing old options and overwrite a resyncing client's view with stale or another room's values. It does not establish that the separately prepared stage-start transaction is broken. Store one authoritative revisioned settings/playlist snapshot per room; apply local host UI projections directly and replay that exact room snapshot to joiners and returning clients. Verify remote leader plus listen host plus a third peer, including resync before match launch.

## Verification debt and repair order

The inspected tests have strong parser, lifecycle and source-contract coverage. For example `test_net_lifecycle_static.cpp:1712` asserts source structure around failed distribution. That does not exercise cross-channel scheduling, approval after END, concurrent speakers, real presence-to-chat service routing, allocator failure or host lobby button dispatch. No passing current-source runtime evidence for these boundary cases was found in the inspected surfaces. This is a coverage gap statement, not an assertion that no other test exists anywhere.

1. Repair authority and state safety: F01/F02/F05/F13/F14/F18. Preserve exact prior state on rejection.
2. Repair distribution ordering and full state lifecycle: F03/F04; then F11/F12 pacing and resource accounting.
3. Repair social transport/audience: F06/F07/F08; then voice timeline/mixing and supported UI modes, F09/F10.
4. Complete room/social controls and errors, F15. Keep menus compatible with MKB and controller, including text entry, confirmation and Back.
5. Qualify real network conditions under existing networking items, F16. Decide separately whether to promote fresh drop-in or host migration.
6. Measure scaling and optimize only demonstrated costs, including F17.

For each coherent repair batch, freeze its exact product/verifier inputs, run the smallest direct coordinated gate, and stop on the first terminal failure. Later acceptance should cover two ordinary independent installs, both authority roles, stock and custom assets, delayed/lost traffic, simultaneous speakers, stage/end/return/reconnect, plus actual graphical and controller checks. Do not repeatedly run the unchanged ordinary fixture while the separate asset boot gate is red.

## Durable handoff

T-NETWORKING-010 retains these unresolved findings and this source-only evidence boundary. Existing networking cards keep their statuses; no gameplay feature is marked validated by this audit. The audit report, source receipt and additive context references are the only authored files. Repairs await the user's next direction. No staging, commit or push performed.

End of audit.
