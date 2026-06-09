# Networking And Match-Flow Sweep - 2026-05-21

Card: `c3813` - Gameplay stability: local and network drop-in/drop-out

## Scope

This sweep checks whether current networking and match flow infrastructure still lines up after recent improvements. The target player path is:

1. Add or connect through social/friend/connect-code surfaces.
2. Join a listen-host session and land in the social lobby or room.
3. Sync room membership, match settings, playlist, and ready state.
4. Build and distribute the match manifest, including required mod/content assets.
5. Start the match only after manifest readiness gates pass.
6. Play locally or together with friends while syncing players, bots/NPCs, props, projectiles, entities, jumping/surface state, killfeed, and score.
7. End the match, show the endscreen, and return to the connected room/social lobby.
8. Handle leave, disconnect, reconnect, and drop-in/drop-out without stale state.

## Immediate Corrections Made

- Live context reported protocol v49 at the time of this 2026-05-21 sweep. Current protocol state is tracked in `context/constraints.md` and `context/pillars/connectivity.md`.
- Connectivity and save/wire pillars now document v49 `CLC_LOBBY_RESYNC` as the latest bump.
- Live test docs no longer describe `pd-server` as an active peer target; active build coverage is `pd`, `pd-tests`, and `pd-updater`.
- `port/include/net/netmanifest.h` no longer carries stale SA-6 TODO wording for stage-spawn bodies/heads and prop models; the implementation now points to the post-setup scanners.
- c3813-s5 corrected the live v49 return-to-room path: `CLC_LOBBY_RESYNC` now replays room assignment/settings/playlist, listen-host local return no longer broadcasts a client opcode to peers, and listen-host settings/playlist local loops consume the CLC opcode before invoking server handlers.
- c3813-s6 closed a match-prep transfer failure loop: failed active distribution now sends `MANIFEST_STATUS_DECLINE` and returns the client to lobby state instead of re-running `manifestCheck` and requesting the same failed assets again.
- c3813-s7 closed the card-level verification gap: the live two-process listen-host/client smoke now has enough clean-install budget for typed-archive validation/extraction, early `--host` logging routes to `pd-host.log` before normal system init, and static guards pin the smoke fixture, runner multi-process orchestration, mid-match disconnect preservation, room leave before client reset, mid-game fresh-join rejection, cookie reconnect, score restore, stage-start replay, and full chr/prop/score resync scheduling.

## Flow Matrix

| Flow | Current evidence | Gap / next action |
|------|------------------|-------------------|
| Connect code and friend entry | `test_connectcode.cpp` statically pins connect-code UI and no raw IP labels. Network menu and main menu decode connect codes before `netStartClientWithHolePunch`. Presence/group session can hand P2P success to `netStartClient`. | NAT/ICE/TURN/UPnP hardening is tracked by existing networking backlog cards (`c054`, `c055`, `c057`, `c058`, `c059`, `c060`) and is not blocking c3813 gameplay-stability closure. |
| Listen-host room and social lobby | `netStartServer` is the shipping server path. `SVC_ROOM_ASSIGN`, room create/join/leave handlers, and `pdgui_menu_lobby.cpp`/`pdgui_menu_room.cpp` are wired. `listen_host_init_smoke` pins network-stack init and `listen_host_peer_smoke` pins a two-process host/client ENet auth handshake on loopback; c3813-s7 extends the smoke timing for clean installs, fixes early host log routing, and statically guards fixture shape plus runner orchestration. | Covered for c3813. Future UI-deeper room smokes can expand the same harness without reopening this card. |
| Room settings and playlist sync | `pdgui_menu_room.cpp` uses dirty flags and end-of-frame `netSendRoomSettingsUpdate`. `netmsg.c` handles `CLC_ROOM_SETTINGS_UPDATE`, validates leader/ranges, broadcasts `SVC_ROOM_SETTINGS` / `SVC_ROOM_PLAYLIST`, and c3813-s5 statically pins listen-host local-loop opcode consumption. | Covered for c3813 by source guards. A richer two-peer settings mutation smoke is useful follow-up coverage, not a discovered blocker. |
| Manifest and distribution | `SVC_MATCH_MANIFEST` and `CLC_MANIFEST_STATUS` parse/validate before ready gate commit. `netdistrib.c` hot-registers delivered assets, and c3813-s6 makes failed active match-prep transfer decline the manifest instead of looping. `ASSET_WEAPON`, `ASSET_PROJECTILE`, and `ASSET_ENTITY` are in catalog/manifest/distribution paths. | c3813 manifest lifecycle is covered. Custom `.pdprojectile` / `.pdentity` runtime behavior remains c3814-owned and should close there. |
| Ready gate and launch | `readyGateCheck`, `readyGateTickCountdown`, `netReadyGateAbortForRoom`, and `netReadyGateOnClientLeft` gate launch and abort on room teardown/client leave. `roomLeave` calls the ready-gate cleanup hooks, with c3813-s5 static coverage pinning abort-before-destroy ordering. c3813-s6 statically pins countdown cancel gates and `SVC_MATCH_CANCELLED` broadcast. | Covered for c3813. Additional host/client countdown-cancel smoke can be added after deeper room automation grows. |
| Stage load and manifest lifecycle | `manifestMPTransition` uses `g_ClientManifest` for MP stage load, and teardown paths call `manifestClear(&g_ClientManifest)` before returning to lobby/main menu. | Keep SP/MP manifest-clear checks in future match-end and reconnect work. |
| Live player, score, and killfeed sync | `SVC_PLAYER_STATS` carries `attacker_id`; `SVC_PLAYER_SCORES` exists; `mpstats.c` triggers event-driven score resync and `net.c` has a periodic score resync. c3813-s7 pins reconnect score restore and resync scheduling. | Covered for c3813. End-to-end killfeed smoke remains a future harness expansion. |
| Bots, NPCs, and jump/surface state | `SVC_CHR_MOVE`, `SVC_NPC_MOVE`, `CLC_BOT_MOVE`, and v47 `surface_up` cover bot/NPC locomotion state. `SVC_GPUSWARM_STATE` covers listen-host GPU swarm snapshots. | Bot-jump behavioral playtest is tracked outside c3813 through the active Combat Sim/bot stability work and does not block this card. |
| Props, projectiles, and entities | `SVC_PROP_MOVE`, `SVC_PROP_SPAWN`, damage/pickup/use/door/lift handlers, projectile fields, `.pdprojectile`, and `.pdentity` manifest/distribution plumbing exist. | Runtime behavior for custom `.pdprojectile` motion/guidance/impact and `.pdentity` armed/deployed behavior remains c3814-owned. |
| Match end and lobby return | `SVC_STAGE_END` teardown exists. `pdguiEndscreenExitToRoom` preserves connected lobby/room state and `netSendLobbyResync` fires v49 `CLC_LOBBY_RESYNC`, which replays `SVC_ROOM_ASSIGN`, settings, and playlist. c3813-s5 statically pins the endscreen return ordering and listen-host local return behavior. | Covered for c3813. B-356/manual UI retest remains its own post-match interaction gate. |
| Disconnect and reconnect | `netDisconnect` clears MP manifest before lobby/main-menu return. Reconnect authority uses identity cookie, and preserved scores are restored on reconnect paths. c3813-s7 pins preserve-before-reset, room-leave-before-reset, mid-game fresh-join rejection, cookie reconnect, score restore, stage-start replay, and chr/prop/score resync scheduling. | Covered for c3813. |

## Remaining Gaps Owned Elsewhere

1. **NAT handoff split**: `p2p.c` and `netholepunch.c` both own pieces of connection escalation. The handoff is visible through `groupSessionTick` calling `netStartClient`, but the systems are not unified.
2. **ICE/STUN exchange incomplete**: `p2pIceAddPeerCandidate` exists but is not wired from presence/invite, and local STUN reflexive results are not clearly transmitted to the peer.
3. **TURN and bandwidth selection incomplete**: TURN has no public fallback, and group-session kbps uses a placeholder local value until measurement is wired.
4. **UPnP partial mapping**: UPnP maps the ENet port, while direct probe, ICE, and TURN helper sockets remain unmapped.
5. **Legacy manifest serializer**: `modmgrWriteManifest` / `modmgrReadManifest` remain dead legacy code beside the modern match-manifest path.
6. **Projectile/entity gameplay parity**: Custom `.pdprojectile` and `.pdentity` catalog/manifest/distribution plumbing exists, but deeper runtime execution remains open under `c3814`.
7. **Deeper two-peer gameplay smokes**: c3813 now pins the existing two-process listen-host/client handshake smoke plus source-level lifecycle invariants. Future smokes for settings mutation, match end, killfeed, and countdown cancel should build on the same harness, but they are no longer unresolved c3813 blockers.

## Closeout

c3813 closes on the current verified surface: host/client connection is smoke-harnessed and live-verified on loopback, return-to-room resync is fixed, ready-gate/manifest failure loops are fixed, disconnect/reconnect/drop-in/drop-out source invariants are pinned, and external NAT / projectile-entity runtime gaps have explicit owning cards.

## Where To Look

- `port/include/net/net.h` - current wire version and protocol ledger.
- `port/include/net/netmsg.h` and `port/src/net/netmsg.c` - room, manifest, ready gate, lobby resync, player, prop, bot, NPC, and GPU swarm messages.
- `port/src/net/net.c` - server/client startup, handshake, dispatch, disconnect, score resync, and end-frame broadcasts.
- `port/src/room.c` - room membership and ready-gate cleanup hooks.
- `port/fast3d/pdgui_menu_room.cpp` - room settings dirty flags and settings broadcast.
- `port/fast3d/pdgui_menu_endscreen.cpp` and `port/fast3d/pdgui_bridge.c` - match-end return paths.
- `context/pillars/connectivity.md` - NAT traversal gaps and active online invariants.
