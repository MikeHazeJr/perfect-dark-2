# Multiplayer repair program, 2026-09-08

Goal: implement the recommendations from the multiplayer audit, with full
production and verification coverage. Workbench T-NETWORKING-011 owns the
program; T-NETWORKING-010 preserves the original 18-finding audit.

Mike authorized implementation at 13:28 ET. This authorization covers the
recommended repairs and improvements, not merely another audit or proposals.
Host migration remains the separately scoped post-1.0 product choice identified
in the audit; it must not silently become a substitute for reconnect correctness.

## Requirements and acceptance

Sep22 19:31 ET: Mike requested commit/push of MP-owned work and then a pause.
No further MP build, focused selector, ordinary distribution retry, or source
repair will run until he explicitly resumes. The combined asset0905b client
and tests binaries compiled with the two-path weapon-set correction, but the
network lifecycle/vehicle selectors and actual two-process transfer retry are
still unrun. A scoped checkpoint is being prepared on a separate branch because
canonical `dev` also contains other sessions' uncommitted CMake, room/menu,
vehicle and context changes. Those mixed hunks must not be staged as MP-owned;
the working tree retains them for owner coordination on resume. T-NETWORKING-011
remains partial, and the F04 consent/F11 pacing/whole-match acceptance gaps stay
open.

Sep22 19:18 ET: the byte-identical short-name two-process distribution retry
passed the prior cache-path blocker and reached ENet, then stopped RED35/67
before transfer. Every `--host-autostart` retry rejected the host's lobby start
with `invalid_weapon_set`: filtered menu index 6 had resolved to Random set 13,
but the server compared raw index 6 with the resolved Random constant, re-rolled
the transmitted exact slots, and rejected the mismatch. Source correction in
`netmsg.c` now classifies the resolved set while preserving the original menu
index for non-random preparation; a focused lifecycle guard was added. This
two-path correction is **applied, not yet built or run**. Receipts:
`.claude/mp0908-repairs/followups/distrib-budget/real-peer-short-r2-exit.json`,
`.claude/smoke-verify-runs/results-20260922T231327Z.json`; frozen source
D56CDDD9/client7F5DD99D/short fixture F5A89345. No ready-gate or F12
real-peer verdict follows. A fresh combined build and runtime gate are needed.

Sep22 18:57 ET: F12 distribution resource-accounting source6 passed a bounded
component gate on frozen full3623 SHA4463E90D with no drift. Isolated client
2FDBE1EE and tests 13008318 built cleanly; the exact four transfer
allocation/budget cases passed with zero failures; the required native asset
source guard exited 0. BEGIN now uses the sender's 50 MiB expanded-archive
limit, reserves expanded bytes against the 200 MiB session budget before
admission, rejects duplicate IDs and invalid chunk geometry, and bounds its
compressed receive buffer by zlib `compressBound`. END allocates the declared
expanded size and checks the actual size. The lobby bar uses received/total
chunks for its fraction and labels compressed bytes versus expanded bytes.
Receipts: `.claude/mp0908-repairs/followups/distrib-budget/{source-applied,
full-source-r1,build-exit,build-tests-exit,native-exit,native-guard-exit}.json`.
The original 50 MiB sender limit, approval-before-bulk and frame pacing remain
open; this is bounded accounting, not large-content support or real-peer proof.

Sep22 18:59 ET: the first ordinary two-process typed-pdmod distribution smoke
stopped RED20/44 before transfer. Its long per-test install path made all
private cache roots exceed the platform path budget for the host's public
`base:animation_character_ao` source; host exited with `ASSET.SOURCE_ONLY`
before an ENet transfer began. Product source and client binary were unchanged.
Retain `.claude/mp0908-repairs/followups/distrib-budget/real-peer-exit.json`
and `.claude/smoke-verify-runs/results-20260922T225929Z.json`. The short-name
retry above cleared this path blocker but exposed a separate lobby-start failure.
No real-peer distribution conclusion follows from either red.

Sep22 18:34 ET: F10 voice VAD/device capability source8 passed a bounded
component gate. Frozen full2156 SHA59011833 stayed unchanged; isolated clean
client C28ED419 and tests 691259AF built. The production-adapter voice selector
matched all 17 expected cases with zero JUnit failures, including real Opus
and Ed25519 through deterministic SDL/transport fixtures and an independent
no-codec compile path. The native asset source guard exited 0. Receipts are
`.claude/mp0908-repairs/followups/voice/vad-{build-client,build-tests,17,guard}-exit.json`.
This validates threshold/hysteresis/silence, Agent/audience/mode ownership,
device failure fallback and capability reporting at component level. Actual
presence-socket voice exchange, physical microphone/speaker audibility,
ordinary Settings interaction and full multiplayer acceptance remain open.
MP build/test queues and source freeze were released after green.

Sep22 18:28 ET: F10 voice VAD/device capability eight-path candidate is
**source-applied, unbuilt and untested** after exact baseline and candidate
hash checks. It adds energy hysteresis, a 300 ms silence hold, capture mode
ownership, output/microphone/codec failure state, Settings controls, and focused
production adapter tests including a no-Opus compile path. CMake test entries
were rebased additively over current file-transfer tests. Application receipt:
`.claude/mp0908-repairs/followups/voice/source-vad8-applied.json`. Existing
voice playout13 evidence applies to the prior source only. Neither real audio
nor ordinary UI acceptance is implied.

Sep22 18:19 ET: signed FT socket r2 **passed** on one ordinary portable Agent
and a disposable localhost UDP peer. Frozen full2154 DFE17DD4 and client
AB36312D remained unchanged. The smoke runner passed 6/6 assertions; the peer
verified the game's signed 4097-byte outbound payload, sent a signed 4097-byte
file back, and received the final ACK again after repeating the final chunk.
Post-run inspection checked the installed inbox bytes/SHA, JSON sidecar with
escaped original quote, and exactly one inbound publication. Receipts:
`.claude/mp0908-repairs/followups/ft-storage/socket-r2-{smoke-exit,peer-result,installed-receipt}.json`
and `.claude/smoke-verify-runs/results-20260922T221856Z.json`. The original r1
harness red remains retained. All MP gates/source holds released. This is
localhost socket acceptance, not sender/receiver UI, WAN/NAT, user consent,
large transfer pacing, or whole multiplayer acceptance.

Sep22 18:14 ET: the peer now ignores non-FT presence datagrams and uses the
production `file` kind / `files` inbox convention. Only that peer and its
attempt-specific private runner changed from the first frozen source; full2154
r2 SHA DFE17DD4 confirms all compiled product/test and fixture inputs remain
identical. The client AB36312D is reused; r2 runtime is queued behind the
menu owner's ordinary-client gate. This correction is not yet socket proof.

Sep22 18:12 ET: first signed-socket smoke stopped red at the synthetic peer.
Frozen full2154 SHA DB405E40 had client/tests builds0, exact schema1/0fail
and asset guard0. The ordinary client loaded its Agent, bound UDP28105, sent
the FT INIT to the invitee, and exited cleanly after its scripted wait. The
peer on UDP28106 rejected an earlier presence datagram as if it were an FT
frame, then exited; the sender consequently abandoned after no INIT ACK.
Smoke assertions were 4/6. This is a harness filtering defect, not evidence
of a production FT failure or success. Retained receipts are
`.claude/mp0908-repairs/followups/ft-storage/socket-{smoke-exit,peer-result}.json`
and `.claude/smoke-verify-runs/results-20260922T221209Z.json`. The runtime
FIFO and full-source hold were released at first red.

Sep22 18:02 ET: a narrow signed presence-socket smoke probe is source-applied
and **unverified**. It adds one ordinary-client smoke event and fixture plus a
disposable Python/OpenSSL UDP peer. The peer is designed to verify the game's
outbound file, return a signed file, and repeat its final chunk to test the
saved receipt's ACK replay. No compile, socket run, installed inbox inspection,
or release evidence exists for this unit yet; the accepted storage5 gate below
is unchanged.

Sep22 17:52 ET: receiver storage/receipt six-path repair passed its bounded
native gate. Clean isolated client and test builds succeeded on frozen full2149
SHA8337DB9E with zero drift. Native storage5 passed all five exact cases with
zero JUnit failures; the required asset source guard exited 0. Receipts are in
`.claude/mp0908-repairs/followups/ft-storage/`. This proves local atomic file
publication, failure preservation, bounded receipt matching and safe names;
actual UDP/socket delivery, sidecar parsing, UI, and WAN remain unverified.

Sep22 17:40 ET: receiver storage/receipt six-path repair APPLIED.
The final chunk is acknowledged only after SHA verification and an atomic inbox
commit. Failed commits send a signed rejection without publishing a receipt;
matching retransmissions can use a bounded, Agent-scoped saved receipt. Inbox
directories derive from the peer handle, filenames include a digest prefix,
and sidecar strings are JSON escaped. The private candidate was rebased over
the later texture CMake additions and matched all six canonical baselines.
The first shared client build stopped on a remaining local music-conversion
caller of the removed filename sanitizer. Source6 r2 restores that local helper
without using it for remote inbox paths; the menu owner has the exact retry
hash. The bounded gates above now cover client/tests/native5/guard; actual UDP
transfer remains pending.

21:24 ET: FT wire8 bounded gate GREEN. Native7 PASS2898 assertions/exact names;
client/tests builds0, asset-source guard0, full2132 SHA438CF4A4 unchanged, client
4DDA994D/tests6A2856DE. Receipts in followups/ft-wire; queues finished and all
holds released. Real Ed25519 plus canaries/all truncated lengths, malformed
geometry/copy ranges, peer/recipient/transfer matching and stale/future ACK
progress passed. Production adapters compile against the new boundary. Actual
UDP delivery, replay/dedup, receiver verification/atomic commit receipt, complete
content distribution, UI/Agent/controller/WAN acceptance remain open.

21:19 ET: FT wire8 APPLIED under root terminal release/grant, source72C4260E,
explicitly QUIESCENT. Capacity-checked1320-byte v2 signatures, exact chunk
geometry/copy limits, recipient/signing-peer/transfer-bound controls and random
outbound IDs feed production FT. Presence now dispatches/sends FT on its actual
discovered endpoint; the mismatched dedicated socket is removed. ACK advances
immediately; stale/future ACKs cannot rewind/skip. Native7/client/tests/guard
pending a fresh common freeze. No actual socket or receiver commit acceptance.

21:16 ET: actual received-mod activation core PASS in menu Core2 (35/35 overall,
full049E558F/client811BE779). Independent ft-activation-runtime-review.json checks
unchanged FT4 bytes plus real failed-save pending1/effects1, same-plan retry with
saved selection/loaded1/dirty0, and explicit discard pending0 preserving partial
enabled1/dirty1. Seven Public Mods source-contract cases are accepted; initial
stale loader/walker assertions and Core1 exclusive-create setup red are retained.
No network download, ordinary input, Agent-switch UI or controller proof inferred.

New F04/F12 source findings take priority over private VAD: signed FT allocated
1280 bytes but its full signature ends at1320; inconsistent chunk geometry can
read beyond the packet; controls did not bind signing peer/recipient/transfer.
Private wire8 fixes these through a checked v2 boundary and discovered presence
transport, with native7 proposed. It also removes the250ms wait after successful
ACK and rejects stale progress. No wire8 native changes or passing tests yet;
root installed22 owns the current frozen client before the next MP source window.

20:43 ET: received-mod activation caller migration applied, verification pending.
Install-only archive API feeds the existing friend-aware policy through an
Agent-owned prompt with a retained persistent activation plan. Failure keeps
the prompt and error; retry uses the same plan; explicit discard frees ownership
without claiming rollback. Agent changes/shutdown dispose pending plans and
in-flight transfer buffers. UI now closes on checked success only and exposes
partial effects. Menu owner is adding an actual-core queue retry/discard probe;
no runtime, input, or Agent-switch acceptance claimed yet. VAD remains private.

20:22 ET: playout native13 PASS701 assertions; exact case names independently
verified, voice4 unchanged. Tests86BFB2D2/clientA88A88A3/full19F5FAEF built and
guard0, no drift. Real Opus/Ed25519 tested with simulated SDL timing/drain and
captured presence; two-peer mix equals saturating sum of isolated decodes.
Ordered decode, bounded PLC/backlogs, expiry/STOP/Agent and mode/capture cases
passed. Root installed command proof remains separately owned; no voice socket,
audible/device/full UI or VAD acceptance inferred. Review voice-playout13-native-review.json.

20:16 ET: playout4 source APPLIED after menu Core Apply2 terminal release and
root's conditional grant. Manifest source-playout4-settled.json SHA42602FDC;
voice.c/.h and existing fixture/test only, no CMake. Encoded peer buffers feed
ordered20ms Opus decode into a shared saturating mix; playback/capture/stall and
PLC limits connected, plus signed STOP on mode/disable. Native13/current client
unrun. MP explicitly quiescent for root batch19 combined client/tests/guard.
Earlier voice9 and corrected client receipts describe pre-playout source.

20:05 ET: batch18 correction checkpoint terminal PASS. Current v2 clientCF294A6D
built on full80E30B41/2121 inputs with zero drift and guard0. Earlier audio36 and
voice9/102 retained; compile and link reds remain historical failed receipts.
Root's installed sound20 passed27/27 with dummy audio; that is not a live voice
transport/audibility test. Next playout4 is PRIVATE, manifestA705120E: ordered
eight-frame peer buffers, shared20ms saturating mix, bounded PLC/output/capture,
thirteen updated/new adapter cases. No playout application/build/test verdict yet.

19:49 ET: voice-session native9 PASS102 assertions on testsFEE7C21E/full3F22B541,
exact names independently verified and voice4 unchanged. Native review saved in
followups/voice/voice-session9-native-review.json. Shared client build FAILED at
catalog_audio_generation.c:105 and audio.c:986 (implicit isfinite declaration).
Root owns correction; preserve red and passing native proof, no unchanged voice9
rerun. Current-client/guard/runtime acceptance unavailable until corrected build.

19:46 ET: signed voice-session v2 four-path source applied under root's explicit
pre-freeze grant; source-session-v2-4-settled.json SHA4886DAC4. PDVOC envelope now
150..550 bytes with128-bit sender epoch and receiver challenge,32-bit sequence
and64-frame replay window. Media proves a pending challenge before replacing an
active session; old STOP/HELLO cannot adopt that session. Idle receive sessions
expire after1s, pending challenge after3s; OS RNG failure refuses session start.
Nine adapter cases prepared; client/native9 unrun. MP explicitly QUIESCENT for
root sound15+voice checkpoint. Wire version is independently PDVOC2; ENet61
layout unchanged. Mixed ordered playback, VAD and real socket/audio proof open.

19:32 ET: transport7 checkpoint terminal PASS. Voice production-adapter5 passed
55 assertions, exact JUnit names complete, no source drift on fullFBCC1DBA /
tests30D27547 / clientFB499E55. Client/tests and guard passed; menu released holds.
Real Opus/signatures exercised against fake SDL and captured transport; actual
presence socket exchange, physical audio and full Agent UI lifecycle remain open.
Receipts: followups/voice/voice-transport5-exit.json and name-audit.json,
menu0908/callers-native-guard-exit.json. No unchanged policy/chat cases repeated.

19:26 ET: transport7 applied after root batch17 terminal release and explicit
grant. Manifest followups/voice/source-transport7-settled.json SHA33083E3C;
voice uses discovered presence-bound transport, signed exact recipients, bounded
516-byte ingress and production-adapter fixture5. All baseline hashes matched;
diff check passed. MP explicitly quiescent, awaiting menu's independent source
settle before full shared snapshot/client/tests. No new test/runtime verdict yet.

19:19 ET evidence: owner policy6 passed40 assertions on tests09DD35F0/fullD9048BFF,
exact six case names verified from JUnit, no source drift. ClientE69F3AF4 compiled
with Opus enabled. Receipts are menu0908/apply-r3-policy6-exit.json and MP
followups/voice/owner6-native-review.json. Menu remaining5 also passed separately.
Actual Agent/group adapter and signed voice/SDL/audible proof remain pending.
Next transport4 is PRIVATE only: presence-bound endpoint, 516-byte admission,
per-recipient signing and ingress callback; not applied, compiled or tested.

19:11 ET checkpoint: group/voice owner7 applied and explicitly QUIESCENT for the
shared fresh client/tests checkpoint. Manifest
`.claude/mp0908-repairs/followups/voice/source-owner7-settled.json` SHA774288F4.
Current-Agent ownership and accepted-group voice admission are connected; six
policy tests registered but not yet run. Voice transport, replay, mixing, VAD and
actual Agent/SDL runtime acceptance remain open. Earlier menu main5 preflight
failed on MP group-file drift during crossed hold announcements; no main5 cases
ran. MP read the hold too late before mutation; preserve that failed receipt and
require explicit quiescent acknowledgments before future freezes. Menu owns fresh
full manifest/client/tests/remaining5 plus policy6 and guard; no unchanged catalog4 rerun.

| Findings | Required outcome | Proof still required |
|---|---|---|
| F01/F02/F18 | Shared local/remote room operations, atomic membership and authoritative per-room settings/playlist snapshots | Production-linked transaction tests, host buttons, remote-leader/third-peer resync, rejected changes preserve room |
| F05/F14 | Validated authoritative remote movement and correct sample correction | Invalid numeric/intent/position rejection, client prediction reconciliation, jump/collision/vehicle parity under latency |
| F03/F04 | Ordered complete transfers and real consent/cancel/failure lifecycle | Delayed control/data, pending approval, simultaneous transfers, cancellation, READY/rollback |
| F11/F12/F13 | Bounded transfer work, consistent resource accounting and allocation safety | Injected allocator failures, size/reservation limits, progress, transfer-time frame and bandwidth measurements |
| F06/F08 | Correct chat service transport and visible reliable delivery | Presence-discovered peers, loss/reordering, retry/ACK/deduplication and failed status |
| F07/F09/F10 | Explicit voice audience, mixed bounded playback, working activation mode/capability reporting | Nonparticipant exclusion, multiple speakers, jitter/replay/loss, PTT/VAD lifecycle, audible checks |
| F15 | Functional access/capacity/invite controls, actionable errors, lobby chat and profile presentation | Ordinary MKB and controller paths, text entry, Back/focus, asynchronous response and endpoint refresh |
| F16 | Preserve typed authority routing, finish relay/traversal qualification and clear unsupported continuity UX | Real NAT/WAN peers including relay-required conditions; authenticated service configuration may require external credentials |
| F17 | Replicate exact live swarm to eligible room participants with measured costs | Live counts and recipient isolation, full-population parity, bandwidth/readback/frame-time evidence |

Preserve base-game, simulation, asset, graph and input functionality. Do not
weaken authentication, content validation, exact stage epochs or reconnect
transactions. Existing passing receipts apply only to their frozen sources.

## Current state

**18:50 corrected two-client Join-r3 PASS37/37:** runtime209.9s on client6C12FA9B,
full2111 manifest639A68A8, fixture2330BBCA; no source/binary/fixture drift across
10-second watches. Both initial roster packets accepted after auth; no malformed
message. Host created room, guest clicked observed Join, server committed member1
into room1, both clients exited normally. Three observer images reviewed: global2,
joined-room2, host leader/local you labels and guest waiting-for-leader footer.
Root batch16 prerequisite passed18/18 inner23/23 outer with base110 assets593
commands intact. MP runtime FIFO d1c2df7b released and root/menu notified immediately.
Receipts: runtime/roster-join-pair-r3-exit.json and roster-join-pair-r3-visual-review.json;
runner results-20260908T225023Z.json. Prior failed/invalidated runs remain retained.

This establishes initial roster delivery and basic local two-client room joining,
not full multiplayer acceptance. Identical default names/appearances cannot prove
distinct profile/portrait mapping; global host status still says In Lobby despite
its occupied room. Contrasting profiles, third-peer isolation, room settings/bots,
match start/gameplay authority, content consent/pacing, live chat/voice/NAT and
swarm convergence/performance remain in the full original18 requirements.

**18:41 continuation:** root installed command-import gate is terminal FAILED:
inner15/18, outer15/23 in98.6s, results-20260908T223845Z.json. Root diagnosed exact
activation-ledger rollback promoting enabled-but-never-loaded REGISTERED rows to
ENABLED; root owns the bounded repair and next verification. MP Join-r3 has NOT
started and owns no runtime FIFO/process. Numeric7 remains compiled/native5/362
PASS; no unchanged native rerun. Keep MP native sources held. Full18 program
remains active, with no movement simulation/voice/content/runtime closure.

Read-only voice lifecycle follow-up: Agent activation commits new social handle
via prefsAgentPublishActive/socialRebindToActiveAgent, while already-online hub
initialization returns early. No production caller of socialHubGoOffline was
found; groupSessionOnTransportDisconnected preserves peers and group state has
no local Agent owner field. Voice audience repair must explicitly bind/revoke
accepted membership on Agent change. This is source evidence, not a live replay.

**18:36 movement native5 PASS:** four actual codec cases plus affected inventory
contract passed362 assertions on testsA832459C/fullD0FE92DF; exact names, zero
source/binary drift. ClientB52B721F compiled this same product; the only difference
between original3B8304A3 and corrected manifest is one test's CRLF normalization,
independently verified in runtime/batch15-client-product-equivalence.json.
Root remaining3 and native-source guard passed after preserving original red.
MP test-runner released; root installed18/base110-assets593-commands gate precedes
Join-r3. No movement runtime/physical authority or roster-r3 acceptance yet.
Receipts: followups/movement/movement-numeric5-exit.json and name-audit.json.

**18:33 batch15 boundary:** clientB52B721F and tests151D9840 both built0 on
full2111 manifest3B8304A3, including numeric7. Root affected4 stopped at a stale
multiline effect-ingress source assertion after one case passed; remaining2
unrun. MP cancelled queued numeric5 before execution (5388164d); no movement
native/runtime verdict and no Join-r3 run. Root owns exact test-only repair;
retain the client and global product freeze, no unchanged client rebuild.

**18:26 movement integration:** numeric7 is now canonical after the exact menu
CMake window was released; all private hashes/baseline checks passed. Manifest
followups/movement/source-numeric7-settled.json SHA15948AC5. The shared movement
codec now initializes the entire candidate and rejects nonfinite floats before
publication. Both receive handlers initialize scratch; writer bytes remain the
existing53/57 layout. Four direct codec cases plus the affected inventory-gating
contract are prepared UNRUN for the next common tests binary. No movement runtime
or full authority verdict. MP native sources held for root's common freeze;
roster Join-r3 still awaits matching client/base command coverage.

**18:23 continuation boundary:** corrected roster9 passed393 assertions. Shared
root/menu work is still at a tests-only checkpoint; no matching client exists
yet for Join-r3. MP owns no active FIFO or running game process. A private,
uncompiled movement numeric decoder draft is prepared under
followups/movement/numeric-draft: complete initialization/transactional decode,
all53/57-byte truncations and all12 float NaN/Inf positions, golden writer
compatibility and finite-extreme coverage. Four cases are UNRUN. It does not
change canonical source, acknowledgement/force policy, wire format or physical
movement authority. Next: coherent client/base coverage, explicit shared hold,
Join-r3 once; then coordinate movement integration and full remaining scope.

**18:12 corrected codec native9 PASS:** actual Join-r2 on client D988EC27/full2096
5BC637B3 completed35/37 without drift. Three reviewed observer images show global2
and joined-room2 with correct local/leader/appearance and guest authority. The
run remains FAILED: initial roster0x7b rejected empty appearance IDs because the
codec counted2 bytes while netbufWriteStr emits3. Canonical empty-ID2 fixes sizing
and accepts both empty encodings; nine affected native cases passed on331490B9/
fullC9A6A2C9, with exact identities and zero drift. The first Join attempt825BC441
was INVALIDATED by root source drift and missing base command coverage; preserve
its startup timeout. Join-r2 requires and proves110 base animation-command assets/
593 commands. Distinct Join-r3 is prepared UNRUN pending matching client after
root import propagation. Full18 scope remains partial. Receipts under
.claude/mp0908-repairs/: runtime/roster-join-pair-invalidated.json,
runtime/roster-join-pair-r2-exit.json and roster-join-pair-r2-visual-review.json;
followups/roster/source-empty-ids2.json and roster9-empty-r3-exit.json/name-audit.

**17:42 native green:** repaired manifest1 + previously unrun16 passed17/17 on
matching tests16EEFA8B/full2096 B28DAEC1, exact-name audit and zero drift. Retain
roster7/363 and versions4 from their original XML without rerunning them:28
accepted cases total across the same product sources (only the stale source
contract changed). Original wrapper and affected21 failures remain retained.
Root proceeds with the matching game client/installed command gate; MP's distinct
SDL Join pair follows only on a usable matching client. Global/room roster live
acceptance remains pending, as do the other original18 requirements.

**17:40 native checkpoint:** roster seven top-level cases passed363 assertions
on testsD52CC21E/full2096 manifest5985038B. The wrapper counted Catch's separate
`/hidden suffix` SECTION as an eighth record and failed its cardinality check;
`roster7-native-review.json` accepts only the exact seven passing cases while
retaining the original wrapper failure. No rerun. Affected21 then passed versions4
and stopped at a stale modmgr inline-persistence assertion, leaving16 unrun.
The actual CSV/JSON helper excludes session rows; the source contract now follows
both helpers and checked delegation. Only `tests/test_net_lifecycle_static.cpp`
changed (9777E2BE); repaired1+unrun16 is prepared for the next matching test binary.
Original XML/receipts remain under `followups/roster/`; product client/SDL proof
is still pending. This is not full multiplayer acceptance.

**17:27 integration:** roster18 applied after explicit peer source release; all
baseline and draft hashes checked before writing, all18 matched afterward.
Settled manifest `followups/roster/source-roster18-settled.json` SHA256
`02816D3E240112BFFA8D847C5CEDB54982F67B1B1594FAADEE69511DD6560073`.
Canonical protocol is61. Root command-source CMake entries are preserved.
Sources are held for the next joint freeze; native seven/affected, client, guard
and SDL Join verification remain unrun. No unchanged passing gates repeated.

**17:19 continuation:** roster18 is a private, uncompiled draft; no new product
integration or passing evidence. It adds a transactional presentation roster,
change-only authenticated publication, room membership filtering, common portrait
mapping, and stable Counter-Op selection with an owned preview string. Provisional
reconnect records are excluded until restoration commits. Protocol61 is proposed
in the draft; canonical protocol remains60. Seven codec/application cases and a
distinct SDL Join pair are prepared but unrun. The reviewed CMake rebase preserves
the asset session's two command-source entries. Exact baseline/draft hashes and
review patch are in `.claude/mp0908-repairs/followups/roster/`; N-0136 records the
handoff. Integration awaits the shared source window after the asset session's
current client/runtime checks. Full18 requirements above remain active.

**16:50 current:** pair-r2 passed31/31 in168.6s on client9805 and2090 frozen
inputs8B2CFF2A, with zero drift. Observer client1 authenticated after83.8s;
the actual lobby barrier held its screenshots until then. Both images show
Crispy Monkey [Lobby],1/4 players and Join, matching host room1's creation log.
This validates remote room-list reception after the primary Lounge fix.
Receipts: runtime/room-list-pair-r2-exit.json and
runtime/room-list-pair-r2-visual-review.json. All runtime/source holds released.

The same images confirm an open F15/F18 defect: the observer's Connected Players
list contains only itself and shows1/32 despite the connected listen host.
`netmsgServerPublishAuthenticatedTopology` sends leader identity and room counts,
not complete player records. Next MP unit must replicate the authoritative
global player roster and selected-room membership without overwriting active
gameplay/reconnect state. No remote Join, match start, full settings, chat peer,
voice, WAN or physical controller acceptance is claimed. The full goal remains
partial; current social delivery source/native checks do not close F06/F08 runtime.

**16:44 current:** the new authenticated-lobby readiness case passed on
tests0F79510B/source2090/8B2CFF2A. All14 affected readiness cases passed too.
The wrapper expected13 and failed its cardinality check after native exit0;
retained XML confirms all13 expected policies plus the existing virtual-time
barrier case. `runtime/lobby-readiness-affected-review.json` records that
classification without a rerun or changing the original failed receipt.
Matching client980529BEAA5E221B2DE1ECB26DE161B40E7D73F2F03277D999AFF3EDF4717B81
and guard passed. Distinct pair-r2 is queued behind the asset owner's weapon
runtime, still unstarted. No remote room-list acceptance yet.

**16:42 current:** matching client1D52 and source2090/830F0AE4 built and guarded.
The first two-process room-list attempt failed27/30 because the observer's fixed
timers captured extraction and exited at29.49s before authentication; the image
shows Extracting arenas86%. There is no room-list product verdict from this
attempt. Preserve runtime/room-list-pair-exit.json and room-list-pair-review.json.
Four source paths now add `network_lobby_ready`, requiring actual completed boot,
active networking and CLSTATE_LOBBY, with one new direct rejection/acceptance case.
`runtime/source-lobby-readiness4.json` SHA50AE06BF9A04737748CB7E3758BE2BE98C109FE47BD8C87549EC66B5498B6D56
records the inputs. Root owns the next shared build; readiness cases and distinct
pair-r2 runtime are unrun. Prior27 native repair cases retain their passing receipts.

**16:28 current:** combined social9/Lounge2/menu7 native build passed in39s.
The2087-input `source-social-menu-native.json` manifest is
E5AC3C2D1DDF7BA68353110C13514E3B664A8FA350991AC082C5A231DC02B5E2;
pd-tests is055E7589F914B60B95129E035E4B7FDA4286056978048596AF3763513C1DCBE4.
Chat9, fresh-process Lounge1 and affected17 all passed with zero source drift.
Receipts: social/transport/{social-menu-build,chat9,social-affected17}-exit.json
and room-roster/lounge1-exit.json beneath `.claude/mp0908-repairs/followups/`.
Real Ed25519 is exercised, but discovery transport remains captured and the
chat fixture performs no filesystem writes; atomic-save helpers have separate
real file success/failure coverage in affected17. Client compilation, real
discovered peer delivery, Friends UI and updated room-list reception remain open.

**16:23 current:** F18 propagation review found permanent room0 initialized
with capacity0, causing the strict receiver to reject the entire room list.
The host Create18 assignment proof does not cover this defect. Two owned paths
now initialize valid capacity/unowned creator and preserve capacity on reset,
with a fresh initialization/reset case tagged `[net][room][defaults]`.
`followups/room-roster/source-lounge-defaults2.json` records the settled inputs.
Native and updated client list-reception gates remain unrun. The broader room
membership projection is still only a private plan, including the distinction
between human connection capacity and the separate bot participant allowance.

**16:17 current:** social9 is integrated after the shared hold was released.
All nine canonical contents match the reviewed09CBE candidate after newline
normalization; strict patch applicability and whitespace checks passed.
`followups/social/transport/source-social9-settled.json` has SHA-256
271DF705FBCBD5B0FD597ECD7FA88387F8CAAD0F37BE20366D46094F70F77546.
Its nine direct real-Ed25519 cases remain uncompiled/unrun, pending the next
coordinated source freeze and build. CMake ownership is released to the peers.

**16:16 current:** the installed Create-accept fixture passed18/18 on client
E36F3211A5C75C83712EF02000E5A792BF09137759DEB18EBFBF01AA20CC4082 and
the387-input source manifest6C79C9F1DB7910B9D71837A5DBDD120157945F86DC93526067A8AA15103DEB97,
with zero drift. Injected SDL clicks open the modal and accept Create; production
logs prove room1 commit, local assignment and Room OPEN. Image inspection shows
the local host as leader. This adds host creation proof, not remote peers, match
start, physical controller or WAN acceptance. Receipts are
`.claude/mp0908-repairs/runtime/host-create-accept-exit.json` and
`host-create-accept-visual-review.json`; the latter records recovery of the already
captured final screenshot after a wrapper filename typo, without a game rerun.
Root released the shared runtime source hold after its separate gates passed.

**16:03 current:** no additional multiplayer runtime was launched after the
asset owner's weapon14 terminal red. The distinct Create-accept fixture was
cancelled from FIFO before launch and remains ready for a corrected matching
client; there is no acceptance receipt to misclassify. Common source ownership
stays with the graph/menu/asset build owner through its next gate.

The social follow-up is now a settled private nine-path candidate, with nine
unrun `[net][chat][delivery]` cases. Manifest SHA-256 is
09CBE794B8FF422DE942F6C570F76BB15AF03C5477D968686CBC0DE28E03C4CB in
`followups/social/transport/manifest.json`. It routes signed chat through the
discovered presence socket, adds bounded whole-message ACK/retry/receipts,
target and active-agent checks, delivery/error UI, escaped/unsigned history
handling and common atomic saves for both chat and social store. The native
fixture uses real Ed25519 and captured transport; discovery/key lookup are
fixture boundaries, and no filesystem persistence or real peer is proven.
Its CMake draft explicitly preserves root graph/menu additions, with prior
hashes retained in `cmake-upstream-rebase.json`. Strict patch applicability
passes. None of this candidate is integrated or compiled yet.

**15:39 current:** rebuilt client21C5C2966C491D59D14B666B6B1D088D4706156142E466BFF73DD01D45FD09AD
passes the new host capture14/14 assertions with the same371 inputs unchanged.
Model inspection of all three retained screenshots confirms host Agent(you),
1/32 players and bound port27320; an SDL mouse click opens Create Room, and
SDL Escape closes it back to the connected Social Lobby without a Disconnect
prompt. Receipts: `runtime/host-create-cancel-exit.json` and
`runtime/host-create-cancel-visual-review.json` under the repair directory.
This is actual SDL-event/graphical open-cancel evidence, not physical input,
main-menu hosting entry, room creation acceptance or remote-peer proof.
The separate acceptance branch fixture is prepared using observed modal
geometry and requires production commit/assignment/Room-screen witnesses.

**15:34 current:** lobby5/5 and affected36/36 pass on the joint 371-input
freeze F0A104ACC2FFBE77A1B1607BE98DEB77C4F61F96293F71D89D8AAD9C4C9B9FCA,
with test binary99B6BB23036C232DC27DC3BCB05B2F660A488EFE40B2202E7825A5AFC736A74A.
Both gates retained exact executed names, JUnit and before/after zero-drift
receipts in `followups/lobby/lobby5-exit.json` and
`followups/lobby/lobby-affected-exit.json`. Native-source guard also passes
under the asset owner's receipt. Tests FIFO is released; changed-client build
and UI checks follow. No runtime or ordinary input acceptance is inferred.

**15:30 current:** the reviewed lobby14 follow-up is integrated after the
asset owner's explicit 15:28 source-window release. All fourteen normalized
contents match the private draft; whitespace checks pass. Exact settled
hashes are in `followups/lobby/source-lobby14-settled.json` under the repair
artifact directory (receipt SHA-256
80E123629166BBB2137C7AB0345E97E41AF3BF82325E8EF19226D176865B6422).
This connects the typed player view, actual bound port, host inclusion,
identity-stable leadership and room-owner UI authority. Five focused cases
are present but not compiled or run. Root owns the next joint source freeze
and test build alongside its separate audio seed correction. Prior 13+49
passes remain evidence for the earlier freeze, not this changed cohort.
The new private `runtime/fixtures/mp_host_create_cancel.json` uses explicit
host-log routing and a seeded save directory; it has not run. Shared tasks
and session-log updates remain sequentially owned by the asset root.

Social follow-up now has private transport/ACK/retry draft code in
`followups/social/transport/`; its README records outstanding review,
UI and tests. It is neither integrated nor compilation/runtime evidence.

**15:13 current:** client build and required native-source guard pass; the
asset owner also retains its separate installed model10 pass on client
1B938609C6C8643EAEA330C4300DFDEDBB2B7ACD25607E91D740EB6C95AD3ED5.
The multiplayer host capture used that same client and unchanged 360 inputs.
The client exited0 after listen readiness, Social Lobby rendering and a real
1920x1080 GL capture. The runner reported exit1 because it looked for the
default client log instead of the host log selected by `--host`. Evaluating
the retained actual host log with the unchanged assertion engine passes11/11;
no runtime rerun was used to repair that reporting error. Exact evidence is
`.claude/mp0908-repairs/runtime/host-lobby-capture-exit.json`,
`host-lobby-capture-evaluated.json`, `host-lobby-retained.log`, and
`mp-host-social-lobby.bmp` in that directory. This proves bootstrap/rendering,
not Create/Cancel, ordinary hosting entry, remote peers or physical input.

The capture exposes F15 follow-ups: bound port27320 displays as27100, and
the listen host is excluded from Social Lobby (0 players versus1 in overlay).
Tracing that row reveals stale two-byte head/body fields in the Social Lobby
and Room C++ records, while both C bridges write the shortened record. This
misreads names/local status/state/client ID. A private14-path follow-up under
`.claude/mp0908-repairs/followups/lobby/` introduces one typed C/C++ record,
removes old body-index label reads, includes the listen host, preserves lobby
leader identity during row compaction, derives room controls from existing
authoritative room-owner data, and publishes the successfully bound port.
Five focused cases are drafted but unrun. Strict patch applicability passes;
canonical files remain frozen for the asset/menu acceptance owners.

Full room roster replication and complete custom bot/settings projection
remain open. The existing global human list is not yet a per-room roster.
The separate swarm draft remains unintegrated; social transport preparation
is under `followups/social/transport-plan.md`. None of these drafts is accepted
production or runtime evidence.

**14:50 current:** first batch native gates PASS: 13/13 production-linked room,
wire, and allocation cases plus 49/49 affected source/version/menu contracts.
The latter includes the three contracts identified by the menu owner.
Receipts are `.claude/mp0908-repairs/room-transfer13-exit.json` and
`affected-contracts-exit.json`, with exact executed names and JUnit artifacts.
Binary SHA-256 is DD5D17492D52C62620E065B72BC66363C6415AB31626DABFB8788591EF7EBF9B;
the shared 360-input source manifest is F43883E4ECE6530FFED84ACFF02D8876B79F4CA7D4ACC81E950ED4C81445C185.
Both gates verified zero source/binary drift. Prior asset-contract failures were
corrected by that owner before these gates; no unchanged passing gate was rerun.
Tests FIFO is released. Product sources remain frozen for the required guard,
client build, and later room UI/real-peer qualification. No live multiplayer,
graphical, physical-controller, audible, or performance acceptance is claimed.

**14:41 current:** joint tests build stopped at its first compiler error:
`room.c` used `true`/`false` without declaring them in its new native test
target. The owner released only that correction; `<stdbool.h>` is now included.
All other 19 multiplayer source hashes remain unchanged. The revised receipt
is `.claude/mp0908-repairs/source-room-safety-r2.json`; the room source hash is
59245B99262733F8ED790D49A1F15227D3D3E45996439CB029E581B5E5F3342C.
The shared build owner will refreeze and retry incrementally. No multiplayer
test or client gate ran. Shared tasks/session-log writes are held to prevent
concurrent context rewrites; this lane-owned record preserves the correction.

**14:33 current:** the first20-path candidate is integrated in the canonical
checkout, with every file matching its reviewed draft hash. The Agent fixture
ended at14:30:22 with8/22 assertions and no source drift; asset and menu owners
then released the edit window. Strict patch applicability and edited tracked
whitespace checks pass. No executable gate has run on the integrated code.
`source-room-safety-settled.json` under `.claude/mp0908-repairs/` has SHA-256
1F0EA26C4F3A5A37C1BFED1321E997E5E30D9A36F1F12A8FC74A7D99A546E542.
The asset owner concurrently owns10 disjoint direct-model paths; joint source
freeze and one incremental build are next. The separate swarm follow-up is
still unintegrated. All earlier preparation records below are historical;
their draft-only statements do not describe the first batch's current state.

At 14:05 ET the menu/asset common source freeze is still owned by those lanes.
No product/test/CMake edits from this program have been integrated. The first
room candidate is under `.claude/mp0908-repairs/` with baseline hashes in
`room-baseline.json`. It currently drafts the production room transaction and
snapshot APIs, local-host message projection, room request adapters, and seven
production-linked room test cases. Remote creation now drafts bounded initial
settings/playlist validation before membership changes, with a structured
operation result and a protocol-v60 draft. The lobby draft includes password
entry, access/capacity controls, pending/result text and endpoint-derived code
refresh. Room-list parsing is transactional. Reconnect leader expiry and room
reset clear their associated state. These are uncompiled drafts; invitation UI,
room chat, projection of full human/bot configuration, protocol test updates,
and ordinary UI verification remain open. No runtime, graphical or hardware
acceptance. Generator scripts are one-shot preparation scripts, not idempotent
integration tools; use baseline-aware exact diffs instead of rerunning them.

The reviewed first candidate is packaged in
`.claude/mp0908-repairs/integration.patch`, with exact old/draft SHA-256 values
in `integration-manifest.json`. It contains 20 changed/new paths. Additional
drafts connect one reliable transfer stream for BEGIN/CHUNK/END, transactional
receive-buffer growth with terminal cleanup on failure, and the F14 Y/Z
assignment correction. Three allocator fault/boundary cases and three actual
room-payload codec cases bring the new focused count to 13. The codec case
checks each possible truncated payload without changing the caller's state;
another rejects hidden bytes after an embedded terminator. Room capacity is
validated against the configured host connection limit before membership changes.
Affected lifecycle contracts and protocol pins are updated in the draft test
and CMake inputs. Original canonical baselines were unchanged when the review
patch was prepared. No executable check has run on these drafts.

Final review retained only network initialization/disconnect resets for pending
room operations, preserved modal Back ownership, and removed stale dedicated
hosting/room placeholder comments from the draft lobby. The patch generator
can be rerun for packaging, but it refuses any drift from the recorded original
canonical inputs. At 14:04:55 ET the asset owner confirmed no direct overlap
with their future character-identity hunks and confirmed that SVC_ROOM_SETTINGS
bot projection is separate. They require narrow hunk integration, with release
after the menu direct/dedicated gates and nine-case body/hand installed receipt.
That release has not yet occurred. Do not replace full source files from drafts.

Consent-before-transfer, sticky transfer-set failures, consistent wire/raw size
reservations and progress, and worker/pacing integration remain open after the
first distribution safety draft. F14's larger stale/duplicate movement contract
also remains open; the two-axis correction is not F05 authority acceptance.

The asset owner acknowledged draft-only work and noted future ownership overlap
in Room character selection and distributed typed archive ingress. Reconcile
specific functions before integrating. Current room candidate avoids character
selection and archive-admission internals. Do not start a duplicate shared build
or edit any frozen product/test/CMake source until explicit freeze release.

At 14:22 ET a separate seven-path F17 sender/encoding follow-up is prepared in
`.claude/mp0908-repairs/followups/swarm/`. It preserves the GPU texture stride
while encoding the live dispatch count, removes chunk row copies, filters
recipients by loaded active room, and retains the stage publication barrier.
Two additional unrun quantizer cases cover byte parity through 4096 bots and
invalid strides. Its netmsg/lifecycle inputs depend on the first candidate;
the first 20-path patch remains unchanged. F17 is not closed: receiver frame
ordering/reassembly, empty/shrinking pools, GL readback cost and measured
runtime convergence remain open. See the follow-up README and manifest.

Current live app handles were polled directly: asset task
01a073bd-46f6-7530-b020-8654f9571c12 and menu task
01a076c0-c5f5-7510-b1dd-81ef43ce463e remain inProgress, including a bounded
30-second wait. The asset owner's 14:14 chat records a hand9 runtime failure
after boot and first identity success, with diagnostic ownership and continued
source hold. No restart or source release is inferred from a polling timeout.

## Integration and verification order

1. Finish and review one coherent room/state-safety candidate while the prior
   shared gates run. Recheck canonical hashes before applying each draft.
2. Coordinate exact release and edit ownership, integrate without replacing
   another lane's changes, and update context with actual production state.
3. Freeze product and verifier inputs. Use the coordinated isolated build and
   smallest direct gate; stop at the first terminal red and retain valid passes.
4. Advance the other requirement rows in coherent batches. Build a dedicated
   ordinary-peer qualification matrix rather than inheriting old happy-path
   receipts or interpreting static assertions as gameplay acceptance.
5. Mark the goal complete only when every required outcome has direct evidence.
   Incomplete source work, absent WAN/audio/hardware proof and external service
   prerequisites remain explicit; never redefine the objective around a subset.

End of repair program.
