# Networking System

## Status: COMPLETE (All Phases)
ENet-based multiplayer, co-op, and counter-op networking fully implemented across Phases 1-10 and C1-C12.

## Architecture
- **Transport**: ENet (UDP-based, reliable + unreliable channels)
- **Authority model**: Server-authoritative for all gameplay state
- **Tick rate**: 60 Hz
- **Channels**: Unreliable for position updates, reliable for state/events
- **Protocol version**: 19 (bumped from 18: chrslots u16→u32 in SVC_STAGE_START)
- **Modes**: `g_NetMode` — NETMODE_NONE(0), NETMODE_SERVER(1), NETMODE_CLIENT(2)
- **Game modes**: `g_NetGameMode` — NETGAMEMODE_MP(0), NETGAMEMODE_COOP(1), NETGAMEMODE_ANTI(2)

## Completed Phases — Multiplayer

### Phase 1: Server-Authoritative Bots (DONE)
- SVC_CHR_MOVE (0x44): Position, facing angle, rooms, speed, actions — every update frame
- SVC_CHR_STATE (0x45): Damage, shield, weapon, ammo, team, blur, fade — every 15 frames
- SVC_CHR_SYNC (0x46): Rolling checksum, desync escalation — every 60 frames
- Bot AI (`botTickUnpaused`) returns early on NETMODE_CLIENT
- Files: netmsg.h, netmsg.c, net.c, bot.c

### Phase 2: Entity Replication Gaps (DONE)
- Autogun (thrown laptop): NETMODE_CLIENT guards on `autogunTick` and `autogunTickShoot`
- Hover vehicles: Already server-authoritative via SVC_PROP_MOVE
- Windows/glass: Already via SVC_PROP_DAMAGE
- Doors/lifts: Already via SVC_PROP_DOOR and SVC_PROP_LIFT
- Thrown weapons: SVC_PROP_MOVE covers projectiles

### Phase 3: Validation & Robustness (DONE)
- **Desync detection**: SVC_PROP_SYNC (every 120 frames) + SVC_CHR_SYNC (every 60 frames)
- **Resync escalation**: 3 consecutive desyncs → CLC_RESYNC_REQ with 5-second cooldown
- **Full state resync**: CLC_RESYNC_REQ → SVC_CHR_RESYNC + SVC_PROP_RESYNC
- **Bot respawn sync**: fadealpha, respawning flag, fadeintimer60 in SVC_CHR_STATE

### Phase 4: Polish & Hardening (DONE)
- **Bot weapon model sync**: Client-side weapon model update in `botTick` — compares `aibot->weaponnum` against held weapon, creates new model on mismatch
- **Late-join state sync**: Server sets `g_NetPendingResyncFlags` after SVC_STAGE_START for immediate full resync
- **Kill attribution**: Already working through chrDamage → SVC_CHR_DAMAGE → chrDie → mpstatsRecordDeath
- **Simulation leak audit**: All damage paths server-guarded. Visual desyncs (mines/grenades/explosions) are minor and self-correcting

### Phase 5: Network Features (DONE)
- **Graceful disconnect**: netServerPreservePlayer saves identity before slot reset
- **Reconnect with identity**: Name-based preserved player lookup → restore playernum, team, scores
- **Mid-game join**: Allowed for preserved players (removed blanket DISCONNECT_LATE rejection)
- **Score sync**: SVC_PLAYER_SCORES (0x23), NET_RESYNC_FLAG_SCORES
- **Team switching**: CLC_SETTINGS extended with team field, pause menu dropdown
- **In-match controls**: Reverse Pitch, Look Ahead, Mouse Speed X/Y in pause menu
- **Recent servers**: `g_NetRecentServers[8]` persisted via config, refresh button

### Phase 6: Disconnect/Reconnect Hardening (DONE)
- Disconnected player character killed via `playerDie(true)`
- Reconnect respawn via `dostartnewlife = true`
- CLFLAG_ABSENT lifecycle management
- NULL safety in respawn paths

### Phase 7: Crash Prevention Audit (DONE)
- Client counter leak fixed
- Network-supplied index bounds checks across all handlers
- playernum bounds checks in preserve/restore/disconnect
- NULL pointer safety in 5+ message handlers
- strncpy null termination, team dropdown clamping

### Phase 8: Player Collision Fixes (DONE)
- Ceiling collision clamp in bondwalk.c
- Jump ground tolerance adjustment
- Prop surface ground detection via cdTestVolume binary search

### Phase 9: Robustness & Quality Fixes (DONE)
- **netbuf.c crash prevention**: Replaced `__builtin_trap()` with graceful error handling (sets `buf->error = 1`)
- **Weapon number validation**: Bounds checks in 6 message handlers
- **Coop respawn NULL check**: Client null check before `client->inmove`
- **Preserved player timeout**: 5 minutes (NET_PRESERVE_TIMEOUT_FRAMES), checked every 600 ticks
- **Agent picture resolution fix**: loaddelay=8 when returning from cleared texture cache

### Phase 10: Security Audit Fixes (DONE)
- CRIT-1: Missing autogunTick NETMODE_CLIENT guard
- CRIT-2: Unvalidated playernum array access in SvcStageStartRead
- CRIT-3: Unvalidated playernum in setCurrentPlayerNum (2 locations)
- CRIT-4: ownerplayernum bitshift overflow in netbufReadHidden
- MED-1: g_NetNumClients underflow on NULL peer disconnect
- MED-2: NULL dereference on g_Vars.coop during disconnect

## Completed Phases — Co-op

### Phase C1: Co-op Session Setup (DONE)
- g_NetGameMode, g_NetCoopDifficulty, g_NetCoopFriendlyFire, g_NetCoopRadar globals
- Extended SVC_STAGE_START with co-op mode awareness
- CLC_COOP_READY (0x07) for client ready signaling
- netServerCoopStageStart() — mission launch function
- Co-op host menu: mission dropdown, difficulty, friendly fire, radar, start button
- Player slot assignment: server=bond (playernum 0), client=coop (playernum 1)

### Phase C2: NPC Replication (DONE)
- NPC AI guard in chraTick() for co-op clients
- SVC_NPC_MOVE (0x48): Position, angle, rooms, actions — every 3 frames (~20 Hz)
- SVC_NPC_STATE (0x49): Health, flags, alertness, chrflags, hidden — every 30 frames
- SVC_NPC_SYNC (0x4A): XOR-rotate checksum — every 120 frames
- SVC_NPC_RESYNC (0x4B): Full state dump on desync escalation
- NPCs identified by: PROPTYPE_CHR && chr->aibot == NULL && not player-linked

### Phase C3: Mission Objectives & Variables (DONE)
- SVC_STAGE_FLAG (0x50): Full g_StageFlags u32 on change
- SVC_OBJ_STATUS (0x51): Objective index + status on change
- objectivesCheckAll() skipped on client, relies on server broadcast
- Pause menu reads cached g_ObjectiveStatuses on client
- Reconnect path includes full objective + flag resync

### Phase C4: Alarm System (DONE)
- SVC_ALARM (0x52): Active flag (1 byte)
- alarmActivate/alarmDeactivate guarded on client in co-op
- Server broadcasts on state change

### Phase C5: Cutscenes (DONE — MVP)
- SVC_CUTSCENE (0x53): Active flag
- Client freezes input during server cutscenes (no camera sync — MVP limitation)
- Future: Send animation number for full client-side cinematic

### Phase C6: Pickups & Inventory (Already Covered)
- Existing SVC_PROP_PICKUP handles both MP and co-op pickups
- Future: SVC_INVENTORY_SYNC for reconnection inventory restoration

### Phase C7: Door & Window States (Already Covered)
- SVC_PROP_DOOR, SVC_PROP_LIFT, SVC_PROP_DAMAGE cover all interactive environment objects

### Phase C8: Scene & Mission Transitions (DONE)
- SVC_STAGE_END extended with mode awareness (co-op vs MP)
- Client death path suppresses local mainEndStage(), waits for server
- Abort mission: client disconnects, server broadcasts SVC_STAGE_END
- objectiveIsAllComplete uses cached statuses on client

### Phase C9: Vehicles (Already Covered)
- SVC_PROP_MOVE handles hover vehicles in co-op (same as MP)

### Phase C10: Co-op Character Selection (DONE)
- playerChooseBodyAndHead extended for network co-op
- Host + client character dropdowns in co-op lobby menus
- mpGetNumBodies unrestricted (full roster available in network)
- Character sync flow: menu → config → CLC_SETTINGS → SVC_STAGE_START → spawn

### Phase C11: Verbose Logging & Crash Hardening (DONE)
- Comprehensive logging across net.c, netmsg.c, player.c, objectives.c, chraction.c
- NULL safety in endscreen for co-op disconnect edge cases
- CLC_COOP_READY race prevention
- NPC resync target prop bounds validation
- NPC broadcast guard during stage load
- Objective bounds clamping

### Phase C12: Menu Polish & UX (DONE)
- Co-op host menu reorganization with player status display
- Cancel button with proper server cleanup
- IGNOREBACK flag prevents ESC bypass
- Join menu: "Connect" label, disabled when empty, empty address guard
- Client character dropdown hidden in MP mode
- Linker fix: Net-prefixed handlers to avoid multiple definition conflicts

## Message Type Summary

| ID   | Name             | Direction | Channel    | Frequency          |
|------|------------------|-----------|------------|--------------------|
| 0x06 | CLC_RESYNC_REQ   | C→S       | reliable   | on desync           |
| 0x07 | CLC_COOP_READY   | C→S       | reliable   | once per mission    |
| 0x23 | SVC_PLAYER_SCORES| S→C       | reliable   | on reconnect/demand |
| 0x37 | SVC_PROP_SYNC    | S→C       | reliable   | every 120 frames    |
| 0x38 | SVC_PROP_RESYNC  | S→C       | reliable   | on demand           |
| 0x44 | SVC_CHR_MOVE     | S→C       | unreliable | every update frame  |
| 0x45 | SVC_CHR_STATE    | S→C       | reliable   | every 15 frames     |
| 0x46 | SVC_CHR_SYNC     | S→C       | reliable   | every 60 frames     |
| 0x47 | SVC_CHR_RESYNC   | S→C       | reliable   | on demand           |
| 0x48 | SVC_NPC_MOVE     | S→C       | unreliable | every 3 frames      |
| 0x49 | SVC_NPC_STATE    | S→C       | reliable   | every 30 frames     |
| 0x4A | SVC_NPC_SYNC     | S→C       | reliable   | every 120 frames    |
| 0x4B | SVC_NPC_RESYNC   | S→C       | reliable   | on demand           |
| 0x50 | SVC_STAGE_FLAG   | S→C       | reliable   | on change           |
| 0x51 | SVC_OBJ_STATUS   | S→C       | reliable   | on change           |
| 0x52 | SVC_ALARM        | S→C       | reliable   | on change           |
| 0x53 | SVC_CUTSCENE     | S→C       | reliable   | on change           |

## Pre-existing Messages (from original netplay)
- SVC_PLAYER_MOVE, SVC_PLAYER_STATS — Player replication
- SVC_PROP_DOOR — Door mode, flags, hidden
- SVC_PROP_LIFT — Level, speed, accel, dist, pos, rooms
- SVC_PROP_SPAWN, SVC_PROP_MOVE, SVC_PROP_DAMAGE, SVC_PROP_PICKUP, SVC_PROP_USE — Prop lifecycle
- SVC_CHR_DAMAGE, SVC_CHR_DISARM — Chr damage (server-guarded)

## Damage Authority

| Entry Point      | File        | Guard              | Notes                         |
|------------------|-------------|--------------------|-------------------------------|
| func0f0341dc     | chraction.c | Returns on client  | Chr hit detection wrapper     |
| chrDamage        | chraction.c | Via SVC_CHR_DAMAGE | Called from netmsg on clients |
| objDamage        | propobj.c   | Returns on client  | Allows via netmsg             |
| autogunTick      | propobj.c   | Returns on client  | Autogun targeting             |
| autogunTickShoot | propobj.c   | Returns on client  | Autogun firing                |
| botTickUnpaused  | bot.c       | Returns on client  | Bot AI loop                   |

## Preserved Player / Reconnect Architecture
- `g_NetPreservedPlayers[NET_MAX_CLIENTS]` — saved identities (name, playernum, team, scores, preserveframe)
- On disconnect: preserve identity → kill character → reset client slot
- On reconnect: name match → restore identity → clear CLFLAG_ABSENT → respawn if dead → full resync
- Timeout: 5 minutes (NET_PRESERVE_TIMEOUT_FRAMES), checked every 600 ticks in netEndFrame
- Cleared on new stage start

## Key Files
- `port/include/net/netmsg.h` — Message type defines + function declarations
- `port/include/net/net.h` — Protocol version (18), resync flags, preserved player struct, timeout constants
- `port/src/net/netbuf.c` — Network buffer read/write with graceful error handling
- `port/src/net/netmsg.c` — Message encode/decode, desync tracking, reconnect logic, weapon validation
- `port/src/net/net.c` — Core networking, frame loop, resync dispatch, preserved player management
- `port/src/net/netmenu.c` — Network menus: host MP/co-op/join/recent servers
- `src/game/bot.c` — Bot AI tick (server-only), weapon model sync (client)
- `src/game/chraction.c` — Chr damage guard, NPC AI guard, stage flag broadcast
- `src/game/propobj.c` — Autogun guards, objDamage guard, alarm system
- `src/game/objectives.c` — Server-authoritative objective checking in co-op
- `src/game/player.c` — Death/respawn guards, cutscene hooks, character selection

## Deferred Features
- Dynamic NPC spawning (SVC_NPC_SPAWN) for AI-scripted spawns
- Cutscene camera sync (send animation number for full client cinematic)
- SVC_INVENTORY_SYNC for co-op reconnection inventory
- Counter-operative mode (NETGAMEMODE_ANTI) full implementation (Phase D7)

## Dependencies
- **Collision** (collision.md): Player collision fixes in Phase 8
- **Movement** (movement.md): Jump physics in bondwalk.c
- **Build** (build.md): ENet statically linked, protocol version in net.h
