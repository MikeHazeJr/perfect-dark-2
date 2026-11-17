# Co-op drop-in / drop-out (T-NETWORKING-007)

Status: design, 2026-11-17 (MULTIPLAYER). Not started in code.

**Decision (Mike, 2026-11-16, `context/constraints.md`):** a player who drops from a networked co-op mission
does not end it. The mission keeps running and the player can rejoin into it. The goal is full drop-in/drop-out:
a player can also join a co-op mission already in progress, and leave it, without ending it for the others.

## What happens today (read from the code, 2026-11-17)

- **Drop-out kills the partner.** When a client in a match disconnects for a retryable reason (a timeout), the
  host keeps a reconnect record (`netServerPreservePlayer`: identity cookie, player number, prop sync id,
  settings, config) and then kills the player's character (`playerDie`, in the disconnect path of `net.c`) "so
  it doesn't stand idle as a free target". In the Combat Simulator the player respawns when they come back. In
  co-op a dead player stays out for the rest of the mission, so a drop ends the mission for the partner.
- **No drop-in.** A peer with no reconnect record is refused at a live match (`CLC_AUTH`, `DISCONNECT_LATE`).
- **Rejoin was never run in co-op.** The reconnect machinery (the reconnect snapshot of props, player state and
  inventory) has no co-op branch; the mode table's co-op Rejoin cell is UNRUN. A co-op client also needs the NPC
  snapshot (`SVC_NPC_RESYNC`, exists), the objective statuses (`SVC_OBJ_STATUS`, exists), the cutscene authority
  state and the mission's timers.
- **The host leaving ends the mission.** The mission runs on the host's machine (listen hosting); moving the host
  role mid-mission is post-1.0 (T-NETWORKING-008). Drop-in/drop-out is for the partner.
- The Room's Campaign tab already says "Drop-in co-op: players join at next safe spawn. Up to 4 players." Neither
  is true today; the text changes with this work.

## Target behaviour

1. **Drop-out.** The partner times out or quits: the mission carries on for the host. The partner's character is
   parked, not killed: out of the world (not drawn, no collision, not a target for guards), with its health,
   shield, weapons, ammo, position and facing kept. Objectives it completed stay complete (the host owns them).
   The host sees "<name> dropped out".
2. **Rejoin.** The same player comes back (reconnect credential): it receives the stage, the NPC snapshot, the
   objective statuses, the prop state and its own inventory, and its character returns where it was parked, or
   next to the host if that spot is no longer safe (a door closed on it, a guard stands there).
3. **Drop-in.** A new player connects while the partner slot is free: it is admitted into the running mission as
   the partner, loads the stage, receives the same snapshots as a rejoin, starts with the mission's starting
   loadout and appears next to the host at a safe spot. The Room shows the mission in progress with a free slot.
4. **Leaving on purpose** (Quit from the pause menu): as drop-out, but the slot is free at once (no reconnect
   hold), so another player can drop in.

## Questions, answered by Mike 2026-11-17: both as recommended

- **Player count.** The game's co-op has exactly one partner slot, as in the original. Recommended: drop-in/
  drop-out for two-player co-op. Three or four players would be a separate, much bigger project (co-op rules,
  cutscenes and objectives assume one partner).
- **AI buddies and a friend joining.** Recommended: a mission started with AI buddies stays an AI-buddy mission
  (a friend joins the next one); a mission started with a free partner slot is the one friends drop into. The
  original never mixed a human partner with AI buddies.

## AI buddies (built 2026-11-17, branch week-pd/multiplayer-coop, 26557b90; not run yet)

Mike's 2026-11-16 call (the co-op AI-buddy entry comes back) and the 2026-11-17 answer above, as built:

- **The Room's Campaign tab** has a Partner section under Difficulty. With a second player in the Room, that player
  is the partner. With three or more, the tab says co-op is for two players (the lobby start already refuses more).
  Alone, a Perfect Buddy choice offers 1 to N simulants, where N is the original's limit for the difficulty shown:
  one buddy, plus more once the whole co-op campaign is finished on harder difficulties, at most 4
  (`getMaxAiBuddiesForDifficulty`, split out of `getMaxAiBuddies`). The choice lives in `g_NetCoopAiBuddies`.
- **Hosted Room, host alone:** `netServerCoopStageStart` sets `g_Vars.numaibuddies` from the choice only when the
  co-op roster is the host alone, clamps it after the mission difficulty is set, and puts the old value back on each
  of its three rollbacks. With a partner it is 0, so the original never-mixed rule holds. The buddies spawn at the
  player as in the original (`playerTick`, `GAILIST_INIT_DEFAULT_BUDDY`). Log: `COOP.AIBUDDIES: host alone=1
  buddies=N requested=R max=M difficulty=D`, then `COOP.AIBUDDIES: spawned N of N at lvframe F`.
- **A client** clears `g_Vars.numaibuddies` in its stage start: its partner is the host, and a count left from a
  mission it hosted alone would spawn buddies on that machine only.
- **Offline Room** (Play, Combat Simulator, no network): the Campaign tab's Start used to fail (it asked the network
  lobby). It now runs the original's one-player co-op through the Solo Missions start (`pdguiRoomStartOfflineCoop`,
  `menuhandlerAcceptMission`: one player, no co-op player, the clamped buddies).
- The Campaign tab's old text ("Drop-in co-op: players join at next safe spawn. Up to 4 players.") is gone; S4 still
  owns showing a mission in progress with a free slot.
- Smokes: `network_coop_ai_buddies_smoke` (one listen host, `--host-autostart-peers 0`, `--coop-ai-buddies 2`; a
  fresh profile clamps to 1) and `coop_ai_buddies_offline_smoke` (Agent Select, the real Main Menu and Play graph
  into the offline Room, `--debug-room-tab campaign`, the Room's own Start Match). Static pins: `[aibuddies]` in
  `tests/test_net_mission_end_static.cpp`.

## S1 research (2026-11-17, read-only; line numbers at 5454c4fb)

- **B-1274 first:** two-player networked co-op ran with `normmplayerisrunning` set (fixed on this branch). Several
  answers below assume the fixed flag.
- **What `playerDie(true)` does to the partner on the host** (net.c `netServerEvDisconnect`, player.c 7264-7402):
  the live-death event (menus closed, HUD messages removed, an MP death recorded, uncloak); with `mplayerisrunning`
  set it drops every droppable weapon as a world prop (`currentPlayerDropAllItems`, SVC_PROP_SPAWN); dead flags,
  death camera, off a vehicle, guns thrown; later frames play the death sequence and the co-op restart rule (half
  the host's health), and after `netClientReset` the partner's respawn input is read from local slot 1.
- **Already covered by `isdead`:** damage, pickups, collision (with `mplayerisrunning`), auto-aim, `chrIsDead`, the
  guards' p1p2 player choice, autoguns, radar. **Not covered:** automatic doors (`doorIsRangeEmpty` counts any player
  prop in the rooms), CCTV cameras (they alternate between the players and turn the target's collision back on),
  level scripts that target CHR_COOP directly or clear its hidden flag in cutscenes, explosions and bullets (room
  lists), and the results loop (`mainEndStage` pushes results for every non-remote player; after a drop the
  partner is no longer remote, so the host would get a second dialog).
- **Taking the prop out and back:** the original's chr pair is the template: `aiDisableChr` (propDeregisterRooms,
  propDelist, propDisable) and `aiEnableChr` (propActivate, propEnable, room rebuild). A delisted prop is neither
  ticked nor drawn. The PC cutscene layer's `cutscene_protect` already makes a player unseen and undamageable.
- **Plan for S1:** at the disconnect, for the co-op partner, park instead of `playerDie`: off any vehicle, hands
  idle, dead flags plus a new parked flag (so a host death ends the mission as both-dead does, and the restart rule
  cannot take health), hidden, rooms deregistered, delisted, disabled, guards' references cleared; no drops, no
  stats. Skip the parked player in the player loops (pdmain 1031-1042, lv.c's render loop: tick accounting only),
  in the results loop, and for CCTV and autoguns; keep `cutscene_protect` on while parked; chat "<name> dropped
  out". Items the partner holds stay with it (a throw or plant objective waits for its return: ask Mike whether the
  host should get them).
- **Un-park in S2:** inside `netServerCompleteReconnect`, before the reconnect snapshot: the parked spot if clear
  (`spawnPoolFindClearPosition`), else next to the host; activate, enable, rooms, hidden flag back, dead and parked
  cleared. The snapshot already carries co-op's NPC resync, stage flags and objective statuses; it lacks chrs made
  after the stage start (SVC_NPC_SPAWN replay), the alarm and the mission timers.

## Steps

- **S1 Park, don't kill (co-op). Built 2026-11-17 (9f06ecae, week-pd/multiplayer-coop), not run yet:** `playerParkForNetwork` from `netServerEvDisconnect` for the co-op partner; the parked tick, HUD, CCTV and results skips; smoke `network_coop_partner_drop_smoke`; pins `[s1]`. Known gaps: a laptop gun the partner deployed keeps working (the death path's owner cleanup does not run), and a reconnect into a parked partner is not handled until S2.
  Original text: In the disconnect path, a co-op partner is parked instead of killed; the
  host's game ignores a parked partner (guards, objectives that count players, the mission-failed rule when both
  players are dead). Static pins; a smoke where the partner times out and the host plays on to the mission end.
- **S2 Rejoin into a co-op mission. Host side built 2026-11-17 (week-pd/multiplayer-coop), not run yet:** `netServerCompleteReconnect` un-parks the partner (`playerUnparkForNetwork`) before the dead-player respawn check and the snapshot, and parks it again if the send fails; it comes back where it was parked (the spot's clearance is only logged so far). Smoke `network_coop_reconnect_smoke`; pins `[s2]`. First run 2026-11-17: the host side worked (parked, un-parked, reconnect committed) but the client refused the reconnect prop set's end (B-1275, diagnostics added). S1's smoke passed 49/49. Still open: the next-to-the-host fallback, chrs made after the stage start, the alarm and the mission timers.
  Original text: Reuse the reconnect path; add what co-op needs (NPC snapshot, objective
  statuses, cutscene state, timers) and the un-park. New `network_coop_reconnect_smoke`: the client times out
  mid-mission (`network_timeout_client`), reconnects (`network_reconnect`), and both reach the mission end with
  the partner's character back.
- **S3 Drop-in.** Admit a fresh client into a running co-op mission when the partner slot is free (instead of
  `DISCONNECT_LATE`); create the partner player, spawn next to the host. New `network_coop_drop_in_smoke`: the
  host starts alone, a client joins mid-mission and both reach the mission end.
- **S4 Room.** Show a co-op mission in progress with a free slot, let a player join it, and fix the Campaign
  tab's text.
- Counter-op: the same questions apply when the counter-operative drops; not decided, ask Mike before S1 covers it.
