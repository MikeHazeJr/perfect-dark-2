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

## Steps

- **S1 Park, don't kill (co-op).** In the disconnect path, a co-op partner is parked instead of killed; the
  host's game ignores a parked partner (guards, objectives that count players, the mission-failed rule when both
  players are dead). Static pins; a smoke where the partner times out and the host plays on to the mission end.
- **S2 Rejoin into a co-op mission.** Reuse the reconnect path; add what co-op needs (NPC snapshot, objective
  statuses, cutscene state, timers) and the un-park. New `network_coop_reconnect_smoke`: the client times out
  mid-mission (`network_timeout_client`), reconnects (`network_reconnect`), and both reach the mission end with
  the partner's character back.
- **S3 Drop-in.** Admit a fresh client into a running co-op mission when the partner slot is free (instead of
  `DISCONNECT_LATE`); create the partner player, spawn next to the host. New `network_coop_drop_in_smoke`: the
  host starts alone, a client joins mid-mission and both reach the mission end.
- **S4 Room.** Show a co-op mission in progress with a free slot, let a player join it, and fix the Campaign
  tab's text.
- Counter-op: the same questions apply when the counter-operative drops; not decided, ask Mike before S1 covers it.
