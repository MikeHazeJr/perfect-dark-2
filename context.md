# Perfect Dark Mike - Network Replication Context

## Project
Merged PC port: `perfect_dark-mike` combining AllInOneMods (GEX, Kakariko, Goldfinger 64, Dark Noon, extra stages) + netplay (ENet). PC only.

### IMPORTANT: Modern Hardware — No N64 Constraints
This is a **PC-only port running on modern x86_64 hardware**. The original N64's computational
constraints **do not apply**. When implementing new features or fixing bugs:
- **Do not avoid solutions because they would have been "expensive" on N64.** Per-triangle mesh
  collision, spatial acceleration structures (BVH/octree), per-frame raycasts against prop geometry,
  runtime physics calculations — all of this is trivially cheap on modern CPUs.
- **Prefer correctness and robustness over micro-optimization.** If the right solution involves
  iterating over triangles, building runtime data structures, or doing work the N64 couldn't afford,
  that's the solution to use.
- **Legacy collision/physics workarounds exist because of N64 limits, not because they're good design.**
  The simplified bounding-box collision, wall-flag-only vertical checks (`cdTestVolume` ignoring floor
  geo), and the lack of proper ceiling detection are all artifacts of hardware constraints. When fixing
  or extending these systems, consider replacing them with proper geometric solutions rather than
  layering more workarounds on top.
- **Future direction:** Mesh-based prop collision (standing on tables, proper ceiling detection,
  accurate obstacle interaction) should use actual geometry queries rather than the current
  GEOFLAG_WALL/FLOOR flag system where possible.

## Completed Work

### Phase 1: Server-Authoritative Bots (DONE)
- **netmsg.h**: Added `SVC_CHR_MOVE` (0x44), `SVC_CHR_STATE` (0x45), `SVC_CHR_SYNC` (0x46)
- **netmsg.c**: Implemented Write/Read for all three messages
  - CHR_MOVE: position, facing angle (chrGetInverseTheta/chrSetLookAngle), rooms, speedmult fwd/side/theta, myaction, actiontype
  - CHR_STATE: damage, shield, weaponnum, gunfunc, loadedammo[2], team, blurdrugamount, fadealpha, respawning (flag bit 3), fadeintimer60
  - CHR_SYNC: rolling checksum of bot positions+health+action (reliable, every 60 frames) with desync escalation
- **net.c**: Registered all 3 in client receive handler. Server broadcasts bots in netEndFrame.
- **bot.c**: Added `net/net.h` include. `botTickUnpaused` returns early on NETMODE_CLIENT. `botTick` skips AI/movement/angle on client, still calls `chrTick` + `scenarioTickChr`.

### Phase 2: Entity Replication Gaps (DONE)
- **Autogun (thrown laptop) AI tick**: Added NETMODE_CLIENT guards to both `autogunTick` and `autogunTickShoot` in `propobj.c`.
- **Hover vehicles**: Already server-authoritative via SVC_PROP_MOVE with hover-specific physics fields.
- **Windows/breakable glass**: Already server-authoritative via objDamage → SVC_PROP_DAMAGE.
- **Doors/lifts**: Already have SVC_PROP_DOOR and SVC_PROP_LIFT.
- **Thrown weapon trajectories**: SVC_PROP_MOVE covers projectiles with speed, flags, bouncecount.

### Phase 3: Validation & Robustness (DONE)

#### Desync Detection
- **SVC_PROP_SYNC (0x37)**: Prop-level checksum for autoguns, doors, lifts, hover vehicles, glass. Every 120 frames.
- **SVC_CHR_SYNC (0x46)**: Bot-level checksum. Every 60 frames.
- Both track consecutive desyncs. After 3 consecutive (NET_DESYNC_THRESHOLD), client auto-sends CLC_RESYNC_REQ with 5-second cooldown (NET_RESYNC_COOLDOWN = 300 frames).

#### Full State Resync
- **CLC_RESYNC_REQ (0x06)**: Client→Server request (flags: bit 0 = chrs, bit 1 = props).
- **SVC_CHR_RESYNC (0x47)**: Full bot state dump: position, angle, rooms, speed, actions, state flags, damage, shield, weapon, ammo, team, blur, fadealpha, fadeintimer60, target.
- **SVC_PROP_RESYNC (0x38)**: Full prop state dump with type-specific fields (doors: frac/mode, autoguns: rotation/firing/ammo/target, lifts: dist/speed/level).

#### Bot Respawn Sync
- Extended SVC_CHR_STATE with fadealpha, respawning, fadeintimer60 for client-side fade-in visual.
- Bot death→respawn lifecycle fully captured: chrDie via SVC_CHR_DAMAGE triggers chrDie on client → mpstatsRecordDeath runs on both sides → actiontype synced via CHR_MOVE → state reset synced via CHR_STATE.

### Phase 4: Polish & Hardening (DONE)

#### Bot Weapon Model Sync
- **Problem**: `botTickUnpaused` skipped on clients → weapon timer never fires → `chrGiveWeapon` never called → bots appear unarmed visually.
- **Fix**: Added weapon model sync to `botTick`'s client path in `bot.c`. Every frame, compares `aibot->weaponnum` (synced via SVC_CHR_STATE) against what's actually held in `chr->weapons_held[HAND_RIGHT]`. On mismatch: deletes old weapon models, creates new one via `playermgrGetModelOfWeapon()` → `chrGiveWeapon()`.

#### Late-Join State Sync
- **Fix**: Server sets `g_NetPendingResyncFlags = NET_RESYNC_FLAG_CHRS | NET_RESYNC_FLAG_PROPS` in `netServerStageStart()` right after sending SVC_STAGE_START. This triggers a full CHR_RESYNC + PROP_RESYNC broadcast on the next frame, ensuring all clients converge to server state immediately after stage load.

#### Kill Attribution
- **Status**: Already working correctly through existing pipeline.
- **Flow**: Server calls `chrDamage()` → sends SVC_CHR_DAMAGE → client receives → calls `chrDamage()` → if lethal, calls `chrDie()` → `mpstatsRecordDeath()` updates killcounts/numdeaths on both sides.

#### Simulation Leak Audit
- **Damage authority**: Solid. `objDamage()` returns on NETMODE_CLIENT, `func0f0341dc()` returns on NETMODE_CLIENT. All damage paths are server-only.
- **Mines/grenades**: Timer ticking and detonation run on clients, but resulting damage is blocked. Visual-only desync, self-correcting via deterministic RNG seeds and periodic checksums.
- **Explosions**: `explosionTick()` runs on clients for visual effects, but damage goes through guarded `objDamage()`/`func0f0341dc()`. No gameplay impact.
- **Cloaking**: Bot cloak state synced via SVC_CHR_STATE flag bit 1. Player cloaking is local input → expected behavior.
- **Conclusion**: All gameplay-affecting state changes are server-authoritative. Remaining visual desyncs are minor and self-correcting.

### Phase 5: Network Features (DONE)

#### Graceful Client Disconnect
- `netServerEvDisconnect` calls `netServerPreservePlayer()` before resetting the client slot. Preserves name, playernum, team, killcounts, numdeaths, numpoints.
- **DISCONNECT_LEAVE (9)**: New disconnect reason for voluntary leave.
- **CLFLAG_ABSENT (1 << 0)**: New client flag for temporarily disconnected players.

#### Client Reconnect with Identity Preservation
- **netServerPreservePlayer()**: Saves client identity (name → score mapping) to preserved player array.
- **netServerFindPreserved()**: Name-based lookup (case-insensitive) against preserved players.
- **netServerRestorePreserved()**: Restores playernum, team, scores to the reconnecting client's slot.
- **Flow**: Client connects → CLC_AUTH → server checks name against preserved players → if match, restores identity and sends SVC_STAGE_START + full resync (CHR_RESYNC + PROP_RESYNC + PLAYER_SCORES).

#### Mid-Game Join (Reconnect Only)
- Removed blanket DISCONNECT_LATE rejection. Allows connections during game IF preserved players exist.

#### Score Sync
- **SVC_PLAYER_SCORES (0x23)**: Syncs all player scores.
- **NET_RESYNC_FLAG_SCORES (1 << 2)**: New resync flag for reconnection.

#### Team Switching (In-Match)
- CLC_SETTINGS extended with team field (u8).
- Pause menu dropdown for team selection (network + teams enabled only).
- Protocol version bumped to 16.

#### In-Match Control Settings
- Pause menu Controls dialog: Reverse Pitch, Look Ahead, Mouse Speed X/Y.

#### Recent Servers List
- `g_NetRecentServers[8]` persisted via config system. Refresh button queries all servers.

### Phase 6: Disconnect/Reconnect Hardening (DONE)

#### Disconnected Player Character Kill
- `netServerEvDisconnect` calls `playerDie(true)` on the disconnected player's character after preserving scores.
- Uses `setCurrentPlayerNum` for proper player context. Dead player stays dead until reconnect.

#### Reconnect Respawn
- `netServerRestorePreserved` checks if player is dead and sets `dostartnewlife = true`.

#### CLFLAG_ABSENT Lifecycle
- `netmsgClcAuthRead` clears `CLFLAG_ABSENT` after successful restore.

#### Null Safety in Respawn Paths
- Added `client != NULL` checks in both respawn input paths in `player.c`.

### Phase 7: Crash Prevention Audit (DONE)

- Client counter leak fixed (moved `++g_NetNumClients` after validation).
- Network-supplied index bounds checks added across all message handlers.
- playernum bounds checks in preserve/restore/disconnect.
- NULL pointer safety in SvcChrDamageRead, SvcPropSpawnRead, SvcPropSpawnWrite, SvcStageStartWrite, SvcPlayerStatsRead.
- strncpy null termination in netmenu.c.
- Team dropdown clamping in ingame.c.

### Phase 8: Player Collision Fixes (DONE)

All changes in `src/game/bondwalk.c`, guarded by `#ifndef PLATFORM_N64`.

- **Ceiling collision clamp**: After upward movement, calls `cdFindCeilingRoomYColourFlagsAtPos` and clamps player to ceiling - headheight.
- **Jump ground tolerance**: Changed strict `<=` to `<= vv_ground + 5.0f` with `bdeltapos.y < 1.5f` guard.
- **Prop surface ground detection**: Secondary `cdTestVolume` probe with binary search (8 iterations) finds prop surfaces between feet and floor.

### Phase 9: Robustness & Quality Fixes (DONE)

#### netbuf.c Crash Prevention
- **Problem**: `__builtin_trap()` in `netbufCanRead` caused instant crash on any malformed packet (truncated, corrupted, or malicious). This was the single worst crash vector in the network code.
- **Fix**: Replaced `__builtin_trap()` with graceful error handling — sets `buf->error = 1` and returns false. Callers already check `src->error`, so malformed packets now safely abort message processing instead of crashing. Added rp/wp diagnostic info to the warning log.

#### Weapon Number Validation
- **Problem**: Six network message handlers accepted weapon numbers from the network without validation. Out-of-range weapon numbers could cause array out-of-bounds access in weapon lookup tables, leading to undefined behavior or crashes.
- **Fix**: Added `>= WEAPON_UNARMED && <= WEAPON_SUICIDEPILL` bounds checks in:
  - `SvcPlayerStatsRead` — guards `bgunEquipWeapon()` call
  - `SvcPropDamageRead` — guards `objDamage()` call
  - `SvcChrDisarmRead` — guards `objDamage()` call on grenade explosion path
  - `SvcChrStateRead` — guards `aibot->weaponnum` assignment
  - `SvcChrResyncRead` — guards `aibot->weaponnum` assignment
  - `SvcPropSpawnRead` — weapon objects created with validated weaponnum (no change needed, weaponnum is stored as-is but only used later through validated paths)

#### Coop Respawn NULL Check
- **Problem**: Coop respawn path in `player.c` (~line 4801) accessed `g_Vars.currentplayer->client->inmove` without null-checking `client`. During disconnect/reconnect transitions, `isremote` can be true while `client` is temporarily NULL.
- **Fix**: Added `&& g_Vars.currentplayer->client` before accessing `client->inmove`.

#### Preserved Player Timeout
- **Problem**: Preserved player slots never expired. If a player disconnected and never returned, their slot permanently blocked new players from joining with that playernum.
- **Fix**: Added `preserveframe` field to `struct netpreservedplayer`. Set to `g_NetTick` on preserve. Server checks every 10 seconds (600 frames) and expires slots older than 5 minutes (`NET_PRESERVE_TIMEOUT_FRAMES = 60*60*5`).
- **Files**: `port/include/net/net.h` (struct + constant), `port/src/net/net.c` (timestamp + expiry in netEndFrame)

#### Agent Picture Resolution Fix
- **Problem**: When first entering the character select screen, the agent picture renders at high resolution. When returning to character select after playing, the picture is very low-res. Root cause: `menutick.c` sets `unk5d5_01 = true` and calls `bgunFreeGunMem()` → `videoResetTextureCache()` (nukes ALL textures). When re-entering, the `MENUOP_FOCUS` handler in `mpCharacterBodyMenuHandler` set `loaddelay = 3`, which was insufficient for a full texture reload from a cleared cache.
- **Fix**: In both `mpCharacterBodyMenuHandler` (body carousel) and `mpCharacterHeadMenuHandler` (head carousel) in `setup.c`, check if `menumodel.allocstart == NULL` (indicating gunmem was freed / returning after cache clear). If so, use `loaddelay = 8` (matching the initial SET operation delay) instead of 3. This gives textures enough frames to fully reload at native resolution.
- **Files**: `src/game/mplayer/setup.c`

### Phase C1: Co-op Session Setup (DONE)

#### New Constants & Globals (net.h, net.c)
- `NETGAMEMODE_MP (0)`, `NETGAMEMODE_COOP (1)`, `NETGAMEMODE_ANTI (2)` — game mode defines
- `CLFLAG_COOPREADY (1 << 1)` — client ready flag for co-op launch
- `g_NetGameMode` — current game mode (defaults to MP, reset on disconnect)
- `g_NetCoopDifficulty`, `g_NetCoopFriendlyFire`, `g_NetCoopRadar` — co-op session options
- Protocol version bumped to 17 (from 16) for co-op support

#### Extended SVC_STAGE_START (netmsg.c)
- **Write side**: Mode byte now writes `g_NetGameMode` instead of hardcoded 0. For co-op modes, sends difficulty, friendly fire, radar instead of MP setup fields (scenario, scorelimit, etc.).
- **Read side**: Reads mode byte into `g_NetGameMode`. For co-op, configures `g_MissionConfig` (stagenum, difficulty, iscoop/isanti), sets up player numbers (`bondplayernum = 0`, `coopplayernum = 1`), and launches the mission via `titleSetNextStage` → `setNumPlayers` → `lvSetDifficulty` → `mainChangeToStage`. For MP, existing `mpStartMatch()` path unchanged.

#### CLC_COOP_READY (0x07) (netmsg.c)
- **Write**: Sends ready signal from client.
- **Read**: Sets `CLFLAG_COOPREADY` on the client. Checks if ALL connected clients are ready. If so, auto-calls `netServerCoopStageStart()`.
- Registered in server message dispatch table in net.c.

#### netServerCoopStageStart (net.c)
- New function that sets up `g_MissionConfig`, player variables, broadcasts `SVC_STAGE_START`, clears preserved players and ready flags, then launches the mission on the server via the same `titleSetNextStage` → `setNumPlayers` → `lvSetDifficulty` → `mainChangeToStage` flow used by local co-op.

#### Co-op Host Menu (netmenu.c)
- **"Host Co-op" button** added to main Network Game dialog. Starts a 2-player server and opens the co-op host dialog.
- **g_NetCoopHostMenuDialog**: Mission dropdown (all 21 solo stages via `langGet(g_SoloStages[].name3)`), Difficulty dropdown (Agent/Special Agent/Perfect Agent), Friendly Fire checkbox, Radar checkbox, Start Mission button.
- Start Mission is disabled until at least 2 clients connected. On start, calls `netServerCoopStageStart()`.

#### Player Slot Assignment
- Existing `netPlayersAllocate` handles co-op correctly: server = playernum 0 (bond), client = playernum 1 (coop). `playermgrAllocatePlayer` then links `g_Vars.bond` and `g_Vars.coop` based on `bondplayernum`/`coopplayernum`.

### Already Working (from original netplay)
- **Players**: SVC_PLAYER_MOVE, SVC_PLAYER_STATS, force teleport anti-cheat
- **Doors**: SVC_PROP_DOOR (mode, flags, hidden)
- **Lifts**: SVC_PROP_LIFT (level, speed, accel, dist, pos, rooms)
- **Props**: SVC_PROP_SPAWN, SVC_PROP_MOVE, SVC_PROP_DAMAGE, SVC_PROP_PICKUP, SVC_PROP_USE
- **Chr damage**: SVC_CHR_DAMAGE, SVC_CHR_DISARM (server-guarded in chraction.c)

---

## Co-op Networking Plan

### Overview
Extend the existing multiplayer networking to support cooperative campaign play. The server runs the authoritative game simulation; the client receives state updates and sends input. The existing netplay infrastructure (ENet, message types, resync system) is reused and extended.

### Design Principles
1. **Server-authoritative**: All game logic (NPC AI, objectives, alarms, cutscenes) runs on the server only. Clients receive state.
2. **Extend, don't replace**: New SVC_/CLC_ message types for co-op state. Existing prop/chr/player replication is reused.
3. **Phased implementation**: Each subsystem is independent and testable.
4. **Graceful degradation**: If a co-op message is lost or late, the next periodic sync corrects it.

### Phase C1: Co-op Session Setup
**Goal**: Allow a second player to join a campaign mission as the co-op buddy.

**New state**:
- `g_NetCoopMode` flag (0 = MP, 1 = co-op, 2 = counter-op)
- Server sets `g_Vars.coopplayernum` / `g_Vars.antiplayernum` based on connected clients
- Client 0 = bond, Client 1 = coop buddy

**New messages**:
- Extend `SVC_STAGE_START` with co-op mode flag and difficulty
- `CLC_COOP_READY` — client signals ready for mission start

**Changes**:
- `net.c`: Add `g_NetCoopMode`, set in host menu, sent in `SVC_STAGE_START`
- `netmenu.c`: Add co-op host menu (mission select, difficulty, options like friendly fire/radar)
- `game.c` / `setup.c`: Server initializes co-op player slots based on connected clients
- `player.c`: Second player initialization uses `g_Vars.coopplayernum` path

**Key files**: `net.c`, `net.h`, `netmsg.c`, `netmenu.c`, `game.c`, `setup.c`

### Phase C2: NPC Replication (DONE)
**Goal**: All NPCs (guards, civilians, scripted characters) appear and behave identically on all clients in co-op mode.

**Approach**: Separate message types from bots (SVC_NPC_* vs SVC_CHR_*) because NPCs lack the `aibot` struct — different data payload. NPCs are identified by `prop->type == PROPTYPE_CHR && chr->aibot == NULL`.

#### NPC AI Guard (chraction.c)
- Added `NETMODE_CLIENT` guard in `chraTick()` (line ~13370) for co-op/anti modes.
- On clients: NPCs skip `chraiExecute()` and all action dispatch/dodge/darkroom logic.
- Timer updates (soundtimer, talktimer) still run for animation/audio timing.

#### New Message Types (netmsg.h)
- `SVC_NPC_MOVE (0x48)`: NPC position update (co-op only, server-authoritative)
- `SVC_NPC_STATE (0x49)`: NPC state update (co-op only: health, flags, alertness)
- `SVC_NPC_SYNC (0x4A)`: NPC sync checksum for desync detection (co-op only)
- `SVC_NPC_RESYNC (0x4B)`: Full NPC state correction (co-op only)
- `NET_RESYNC_FLAG_NPCS (1 << 3)`: New resync flag in net.h
- Protocol version bumped to 18

#### NPC Move (netmsg.c: netmsgSvcNpcMoveWrite/Read)
- Payload: propptr, position (coord), facing angle (chrGetInverseTheta), rooms (8), myaction (s8), actiontype (u8)
- No aibot speed multipliers (unlike SVC_CHR_MOVE)
- Client applies position, angle, rooms, action state directly

#### NPC State (netmsg.c: netmsgSvcNpcStateWrite/Read)
- Payload: propptr, flags (dead/cloaked/target), damage (f32), maxdamage (f32), alertness (u8), team (s8), chrflags (u32), hidden (u32), fadealpha (u8)
- More comprehensive than bot state — includes chrflags, hidden, maxdamage, alertness (needed for NPC behavior display)

#### NPC Sync (netmsg.c: netmsgSvcNpcSyncWrite/Read)
- Payload: tick (u32), npc count (u16), checksum (u32)
- `netNpcSyncChecksum()`: XOR-rotate of position + damage + myaction for all NPCs
- `netNpcCount()`: Counts active NPCs via `g_ChrSlots` iteration with `netIsNpc()` helper
- After 3 consecutive desyncs, client sends CLC_RESYNC_REQ with NET_RESYNC_FLAG_NPCS
- Desync tracking: `g_NetNpcDesyncCount`, `g_NetNpcResyncLastReq` (static in netmsg.c)

#### NPC Resync (netmsg.c: netmsgSvcNpcResyncWrite/Read)
- Full state dump: tick, count, then per-NPC: propptr, position, angle, rooms, actions, flags, damage, maxdamage, alertness, team, chrflags, hidden, fadealpha, target
- Resets `g_NetNpcDesyncCount` on receipt

#### Server Broadcast (net.c netEndFrame)
- Co-op NPC broadcast added inside the `g_NetNextUpdate` block (server only, co-op/anti only):
  - NPC_MOVE: every 3 frames (~20 Hz) — lower than bots (every frame) since NPCs move less frequently
  - NPC_STATE: every 30 frames (~2/sec) — lower than bots (every 15 frames) for bandwidth efficiency
  - NPC_SYNC: every 120 frames (~0.5/sec) — same as prop sync interval
- Iterates `g_ChrSlots[0..g_NumChrSlots-1]`, filtering by `PROPTYPE_CHR && !aibot`
- NPC resync dispatch added to `g_NetPendingResyncFlags` handler

#### Reconnection & Stage Start
- `netServerCoopStageStart()`: Now sets `g_NetPendingResyncFlags = NET_RESYNC_FLAG_NPCS | NET_RESYNC_FLAG_PROPS` for post-load resync
- `netmsgClcAuthRead()` reconnection path: Adds `NET_RESYNC_FLAG_NPCS` for co-op modes

#### Client Dispatch (net.c netClientEvReceive)
- All 4 new message types registered in the SVC message switch

#### Not Yet Implemented (deferred to future phases)
- `SVC_NPC_SPAWN`: Dynamic NPC spawning (from AI scripts). Currently, only level-load NPCs are replicated. AI-scripted spawns need a spawn message so the client can create NPCs that weren't in the level data.
- `SVC_NPC_DESPAWN`: NPC removal. Currently deferred — dead NPCs are handled via state sync (actiontype ACT_DIE/ACT_DEAD).
- NPC weapon model sync (less critical than bot weapons since NPC weapons are part of their body model from level data)

**Key files**: `port/include/net/netmsg.h`, `port/include/net/net.h`, `port/src/net/netmsg.c`, `port/src/net/net.c`, `src/game/chraction.c`

### Phase C3: Mission Objectives & Variables (DONE)
**Goal**: Objective completion, stage flags, and mission state synchronized between server and clients.

#### New Message Types (netmsg.h)
- `SVC_STAGE_FLAG (0x50)`: Full `g_StageFlags` u32 bitfield. Sent on change.
- `SVC_OBJ_STATUS (0x51)`: Objective index (u8) + status (u8). Sent when objective status changes.

#### Stage Flag Sync (chraction.c)
- `chrSetStageFlag()` and `chrUnsetStageFlag()`: Hooked to broadcast `SVC_STAGE_FLAG` when on co-op server and flags actually changed. Uses `g_NetMsgRel` (accumulated, flushed at frame end).
- Stage flags drive COMPFLAGS/FAILFLAGS objective types. Syncing flags ensures flag-dependent objectives evaluate correctly on the client.

#### Objective Status Sync (objectives.c)
- `objectivesCheckAll()`: Added NETMODE_CLIENT guard for co-op — client skips local evaluation entirely, relies on server-broadcast statuses.
- Server-side: After detecting a status change (`g_ObjectiveStatuses[i] != status`), broadcasts `SVC_OBJ_STATUS(i, status)` via `g_NetMsgRel`.
- Added `net/net.h` and `net/netmsg.h` includes.

#### Client Receive (netmsg.c)
- `netmsgSvcStageFlagRead()`: Sets `g_StageFlags` directly from server value.
- `netmsgSvcObjStatusRead()`: Validates index (<MAX_OBJECTIVES), updates `g_ObjectiveStatuses[index]`, shows HUD message (mirrors the server's `objectivesCheckAll` display logic with correct objective numbering).
- Added `game/objectives.h`, `game/hudmsg.h`, `game/lang.h` includes to netmsg.c.

#### Pause Menu Fix (menuitem.c)
- Objective display (line ~1815): On client in co-op, reads from `g_ObjectiveStatuses[index]` instead of calling `objectiveCheck(index)`. This avoids re-evaluating criteria that may not be in sync (e.g., room entry triggered by bond on server).
- Added `net/net.h` include to menuitem.c.

#### Reconnection/Resync (net.c)
- NPC resync path (`NET_RESYNC_FLAG_NPCS`): Also sends `SVC_STAGE_FLAG` + all `SVC_OBJ_STATUS` messages, ensuring reconnecting clients get full mission state.
- Registered `SVC_STAGE_FLAG` and `SVC_OBJ_STATUS` in client dispatch switch.

#### Design Rationale
- **Server-authoritative objectives**: AI scripts run only on server (guarded in Phase C2). Stage flags change from AI commands → broadcast to clients. `objectivesCheckAll()` evaluates on server → broadcasts changes. Client receives and displays.
- **No SVC_MISSION_VAR needed**: AI script variables are internal to the AI engine and drive stage flags + NPC behavior. Syncing flags + NPC state + objectives covers all observable mission state.
- **Criteria-based objectives** (room entry, holograph, throw in room): These evaluate correctly on the server because both players' actions are processed server-side. Client gets the result via SVC_OBJ_STATUS.

**Key files**: `port/include/net/netmsg.h`, `port/src/net/netmsg.c`, `port/src/net/net.c`, `src/game/chraction.c`, `src/game/objectives.c`, `src/game/menuitem.c`

### Phase C4: Alarm System (DONE)
**Goal**: Alarm activation/deactivation synchronized across clients in co-op.

#### SVC_ALARM (0x52) (netmsg.h, netmsg.c)
- **Write**: 1 byte active flag (0 = deactivated, 1 = activated)
- **Read**: On client, directly sets `g_AlarmTimer` (1 for active, 0 + `alarmStopAudio()` for deactivate). Bypasses the guarded functions to avoid recursive guard checks.

#### Alarm Guards (propobj.c)
- `alarmActivate()`: Added NETMODE_CLIENT guard in co-op (returns early, alarm activation is server-authoritative). On server, after changing `g_AlarmTimer`, broadcasts `SVC_ALARM(1)` via `g_NetMsgRel`.
- `alarmDeactivate()`: Same guard pattern. Server broadcasts `SVC_ALARM(0)` after deactivation.
- Entry points covered:
  - AI scripts (`aiActivateAlarm`/`aiDeactivateAlarm`) — AI is server-only (Phase C2 guard)
  - CCTV camera detection — runs on both sides but client guard prevents local activation
  - NPC alarm animation — NPC actions are server-only (Phase C2 guard)
  - Player button interaction — server-authoritative player actions
  - `alarmTick` auto-deactivation — also server-authoritative via the deactivate guard

#### Client Dispatch (net.c)
- Registered `SVC_ALARM` in client receive switch.

**Key files**: `port/include/net/netmsg.h`, `port/src/net/netmsg.c`, `port/src/net/net.c`, `src/game/propobj.c`

### Phase C5: Cutscenes (DONE — MVP)
**Goal**: Cutscene start/stop synchronized. Both players lock input during cutscenes.

#### SVC_CUTSCENE (0x53) (netmsg.h, netmsg.c)
- **Write**: 1 byte active flag (1 = cutscene started, 0 = cutscene ended)
- **Read**: Sets `g_InCutscene` flag. When starting, clears `contkeyflag` for all player hands to inhibit input. When ending, clears `g_InCutscene` (input resumes naturally next frame).

#### Server Hooks (player.c)
- `playerStartCutscene2()`: After setting `g_InCutscene = 1`, broadcasts `SVC_CUTSCENE(1)` to co-op clients.
- `playerEndCutscene()`: After restoring TICKMODE_NORMAL and MOVEMODE_WALK, broadcasts `SVC_CUTSCENE(0)`.

#### Client Dispatch (net.c)
- Registered `SVC_CUTSCENE` in client receive switch.

#### MVP Limitations
- **No camera sync**: Client player freezes during server cutscenes — doesn't see the cinematic camera angles. NPC positions are still synced via SVC_NPC_MOVE, so the client sees NPC movement but without the scripted camera work.
- **Future enhancement**: Send animation number in SVC_CUTSCENE start, allowing client to enter full cutscene mode with matching camera animation. Would need `g_CutsceneAnimNum` sync.

**Key files**: `port/include/net/netmsg.h`, `port/src/net/netmsg.c`, `port/src/net/net.c`, `src/game/player.c`

### Phase C6: Pickups & Inventory (Already Covered)
**Status**: Existing `SVC_PROP_PICKUP` handles pickups in both MP and co-op. Campaign items (key cards, gadgets) are props that go through the same pickup path. The server validates pickups server-side and broadcasts the result. First-come-first-served resolution is inherent in the server-authoritative model.

**Future enhancement**: `SVC_INVENTORY_SYNC` for full inventory dump on reconnection. Not critical for initial co-op — reconnecting players start with default loadout.

### Phase C7: Door & Window States (Already Covered)
**Status**: `SVC_PROP_DOOR` (mode, flags, hidden), `SVC_PROP_LIFT` (level, speed, dist), and `SVC_PROP_DAMAGE` (glass breakage) already handle all interactive environment objects. Campaign doors opened by AI scripts trigger on the server (AI is server-only from C2) and the door state change is broadcast via existing `SVC_PROP_DOOR`. Lock states are part of the door's obj flags which are synced in prop resync.

### Phase C8: Scene & Mission Transitions (DONE)
**Goal**: Level changes, mission complete/fail, and stage transitions synchronized.

#### SVC_STAGE_END Mode Awareness (netmsg.c)
- **Write**: Now sends `g_NetGameMode` as a mode byte. For co-op/anti, preserves player linkages (endscreen needs `g_Vars.bond`, `g_Vars.coop`). For MP, keeps existing unlink behavior.
- **Read**: Mode-aware handler. For co-op/anti, calls `objectivesDisableChecking()` + `mainEndStage()` (no MP-specific CLSTATE cleanup). For MP, existing behavior unchanged.

#### Server Broadcast Path (pdmain.c — existing)
- `mainEndStage()` in `pdmain.c` already calls `netServerStageEnd()` which broadcasts `SVC_STAGE_END` to all clients. No additional broadcast hooks needed — all `mainEndStage()` calls on the server automatically notify clients.
- Covers all ending paths: `aiEndLevel()` → `func0000e990()` → `mainEndStage()`, player death, GE fadeout, abort.

#### Client Guards (player.c)
- **Death path**: On client in co-op, `mainEndStage()` is suppressed for both the solo death path (`mplayerisrunning == false`) and the GE fadeout path (`TICKMODE_GE_FADEOUT`). Client waits for `SVC_STAGE_END` from server.
- Server detects "both players dead" condition (bond's player tick checks `g_Vars.coop->isdead`) and calls `mainEndStage()` → broadcasts `SVC_STAGE_END`.

#### Abort Mission (mainmenu.c, ingame.c)
- **Client abort**: Calls `netDisconnect()` (existing MP pattern). Server detects disconnect via event handler.
- **Server abort**: Calls `mainEndStage()` which broadcasts `SVC_STAGE_END` to clients via `netServerStageEnd()`.
- Both `menuhandlerAbortMission` (solo/co-op pause) and `menuhandlerMpEndGame` (MP pause) handle network co-op correctly.

#### objectiveIsAllComplete Fix (objectives.c)
- On client in co-op, uses cached `g_ObjectiveStatuses[i]` (synced via SVC_OBJ_STATUS from C3) instead of calling `objectiveCheck(i)`. Ensures `endscreenPushCoop()` correctly determines mission success/failure using server-authoritative objective states.
- Same pattern as the pause menu fix from C3.

#### Design Rationale
- **No new message types needed**: `SVC_STAGE_END` already existed; only needed mode-awareness extension (1 byte).
- **No explicit broadcasts needed**: `pdmain.c`'s `mainEndStage()` already calls `netServerStageEnd()` at the end, handling all server→client notification automatically.
- **Endscreen works correctly**: Each machine runs `endscreenPushCoop()` for its local player only (`LOCALPLAYERCOUNT()` = 1 per machine in network co-op). Both players' death/abort states are accurate via existing replication.

**Key files**: `port/src/net/netmsg.c`, `src/game/player.c`, `src/game/objectives.c`, `src/game/mainmenu.c`, `src/game/mplayer/ingame.c`

### Phase C10: Co-op Character Selection (DONE)
**Goal**: Players can choose their multiplayer character model (including modded characters) in co-op, with models reflected in-game and cutscenes.

#### playerChooseBodyAndHead Extension (player.c)
- Added network co-op path before the default outfit switch. When `g_NetMode != NETMODE_NONE` and co-op is active, reads from `g_PlayerConfigsArray[mpindex].base.mpheadnum/mpbodynum` (same data path as MP).
- If both mpbody and mphead are 0, falls through to default mission outfit (Joanna Dark).
- Non-zero body selection uses `mpGetHeadId()`/`mpGetBodyId()` to convert MP indices to game body/head IDs.
- Handles extended head indices (modded characters) via the `mpGetNumHeads2()` offset path.

#### Co-op Host Character Dropdown (netmenu.c)
- Added "Character" dropdown to `g_NetCoopHostMenuItems` with `menuhandlerCoopCharacter`.
- Index 0 = "Default (Joanna)", indices 1+ map to full `g_MpBodies[]` list (63 entries on PC).
- Auto-assigns matching head via `mpGetMpheadnumByMpbodynum()`.
- Updates `g_PlayerConfigsArray[0]` (host's config) which is read by `netClientReadConfig()` → `CLC_SETTINGS` → `SVC_STAGE_START`.

#### Client Character Selection (netmenu.c)
- Added "Character" dropdown to `g_NetJoiningMenuItems` with `menuhandlerJoinCharacter`.
- Same body list and auto-head logic as host.
- Hidden via `MENUOP_CHECKHIDDEN` until client reaches CLSTATE_LOBBY (connected to server).
- On selection, calls `netClientSettingsChanged()` to send updated `CLC_SETTINGS` to server immediately.

#### mpGetNumBodies Fix (mplayer.c)
- Removed the network body restriction entirely. Originally returned 1 for MP netgames (only Joanna).
- All clients in this merged port share identical mod content, so the full character roster is always available in both MP and co-op network games.
- Function now unconditionally returns `ARRAYCOUNT(g_MpBodies)` (61+ characters including mods).

#### Character Sync Flow
1. Player selects character in lobby → `g_PlayerConfigsArray[0].base.mpbodynum/mpheadnum` updated
2. `netClientReadConfig()` copies into `netclient.settings.bodynum/headnum`
3. `CLC_SETTINGS` sends to server (or `SVC_STAGE_START` broadcasts to all clients)
4. `netPlayersAllocate()` copies received settings into `g_PlayerConfigsArray[playernum]`
5. `playerChooseBodyAndHead()` reads config during spawn and applies game body/head IDs
6. Character model appears in-game (3rd person) and cutscenes (NPC-facing scenes use chr model)

**Key files**: `src/game/player.c`, `port/src/net/netmenu.c`, `src/game/mplayer/mplayer.c`

### Phase C11: Verbose Logging & Crash Hardening (DONE)
**Goal**: Robust logging for debugging and crash prevention for all join/quit/disconnect/rejoin scenarios.

#### Verbose Logging (net.c, netmsg.c, player.c, objectives.c, chraction.c)
- **net.c**: Player allocation (playernum/body/head/name), NPC broadcast first activation (one-time), NPC resync count, stage end broadcast
- **netmsg.c**: SVC_STAGE_END mode, NPC resync counts, desync detection (checksum mismatches), objective status changes, stage flag changes, alarm/cutscene state, CLC_COOP_READY client tracking, CLC_SETTINGS character changes
- **player.c**: Death path client guard suppression, GE fadeout client guard suppression, custom character application
- **objectives.c**: Client objective checking skip, client cached status usage
- **chraction.c**: NPC AI suppression (one-time), stage flag broadcast events

#### Crash Prevention Fixes
- **endscreenPushCoop/endscreenPushSolo** (endscreen.c): NULL checks for `g_Vars.bond`, `g_Vars.coop`, `g_Vars.currentplayerstats` before dereference. Prevents crash when co-op player disconnects near mission end.
- **CLC_COOP_READY race** (netmsg.c): Ignores late ready messages if server is already in CLSTATE_GAME. Prevents double stage initialization.
- **NPC resync target prop** (netmsg.c): Bounds validation on targetprop pointer arithmetic (`targetprop - g_Vars.props`). Prevents crash on malformed resync messages.
- **NPC broadcast guard** (net.c): Only broadcasts NPCs when `g_NetLocalClient->state >= CLSTATE_GAME` and `g_NumChrSlots > 0`. Prevents crash during stage load phase.
- **Objective bounds** (objectives.c): Clamped `g_ObjectiveLastIndex` loop to `MAX_OBJECTIVES` in `objectivesCheckAll()`. Prevents out-of-bounds access on corrupted objective setup.

**Key files**: `port/src/net/net.c`, `port/src/net/netmsg.c`, `src/game/player.c`, `src/game/objectives.c`, `src/game/chraction.c`, `src/game/endscreen.c`

### Phase C12: Menu Polish & UX (DONE)
**Goal**: Clean, logical, and robust menu flows for all network dialogs.

#### Co-op Host Menu Reorganization (netmenu.c)
- **Logical grouping**: Mission → Difficulty → Character → [sep] → Friendly Fire → Radar → [sep] → Player Status → Start Mission → Cancel
- **Player status label**: Dynamic `menutextCoopPlayerStatus()` shows "Waiting for Player 2..." or "Player 2: [name]" with live client name lookup via `g_NetClients` iteration
- **Cancel button with cleanup**: `menuhandlerCoopHostBack()` calls `netDisconnect()` before `menuPopDialog()`. Prevents orphaned server when leaving the host dialog.
- **IGNOREBACK flag**: Added to `g_NetCoopHostMenuDialog` so ESC can't bypass server cleanup — user must use Cancel button.
- **Dead code removal**: Removed unused `g_NetCoopStageText[64]`

#### Join Menu Improvements (netmenu.c)
- **"Connect" label**: Changed from `L_MPMENU_036` ("Start Game") to literal "Connect" — clearer intent for the join flow
- **Disabled state**: `menuhandlerJoinStart` now has `MENUOP_CHECKDISABLED` that grays out Connect when address is empty
- **Empty address guard**: Added `g_NetJoinAddr[0] != '\0'` check before `netStartClient()` call

#### Client Character Dropdown Guard (netmenu.c)
- **MP mode hiding**: `menuhandlerJoinCharacter` `MENUOP_CHECKHIDDEN` now also returns true when `g_NetGameMode == NETGAMEMODE_MP`. Previously, character dropdown would incorrectly appear when joining an MP game (MP uses Combat Simulator menus for character selection instead).

#### menuhandlerCoopStart Tightening (netmenu.c)
- Changed `g_NetNumClients > 0` check to `g_NetNumClients >= 2` in SET handler — matches the CHECKDISABLED logic

#### Linker Fix: Multiple Definition Resolution (netmenu.c)
- **Problem**: `menuhandlerCoopDifficulty`, `menuhandlerCoopFriendlyFire`, `menuhandlerCoopRadar` were defined in both `mainmenu.c` (local co-op, original game) and `netmenu.c` (network co-op, our additions). Both needed — different backends (`g_Vars.*` vs `g_NetCoop*` variables).
- **Fix**: Renamed the `netmenu.c` versions to `menuhandlerNetCoopDifficulty`, `menuhandlerNetCoopFriendlyFire`, `menuhandlerNetCoopRadar`. Updated all 3 menu item array references in `g_NetCoopHostMenuItems[]`. Original `mainmenu.c` implementations untouched — they satisfy the `mainmenu.h` declarations for local co-op.

**Key files**: `port/src/net/netmenu.c`

### Phase C9: Vehicles (Already Covered)
**Status**: `SVC_PROP_MOVE` handles hover vehicle physics with hover-specific fields (already server-authoritative in MP). Campaign hover bikes and vehicles go through the same prop movement system. Enter/exit events are implicit in the player's movement mode changes which are synced.

**Key files**: `netmsg.h`, `netmsg.c`, `propobj.c`, `bondwalk.c`

### Implementation Priority & Dependencies

```
C1 (Session Setup) ─────────────────────────────────┐
    │                                                │
    ├── C2 (NPC Replication) ◄── CRITICAL PATH       │
    │       │                                        │
    │       ├── C3 (Objectives & Variables)           │
    │       │       │                                │
    │       │       ├── C8 (Scene Transitions)        │
    │       │       │                                │
    │       │       └── C4 (Alarm System)             │
    │       │                                        │
    │       └── C5 (Cutscenes)                        │
    │                                                │
    ├── C6 (Pickups & Inventory)                      │
    │                                                │
    ├── C7 (Doors & Windows) ◄── mostly done          │
    │                                                │
    └── C9 (Vehicles) ◄── last priority              │
```

**Recommended order**: C1 → C2 → C3 → C4 → C5 → C6 → C8 → C7 → C9

C2 (NPC replication) is the largest and most critical piece. Without it, nothing else works because NPCs are central to every campaign mission. C7 is "mostly done" because the MP door/lift replication already covers most cases.

### New Message ID Allocation

| ID   | Name               | Direction | Channel    | Purpose                  |
|------|--------------------|-----------|------------|--------------------------|
| 0x48 | SVC_NPC_SPAWN      | S→C       | reliable   | NPC created              |
| 0x49 | SVC_NPC_MOVE       | S→C       | unreliable | NPC position update      |
| 0x4A | SVC_NPC_STATE      | S→C       | reliable   | NPC health/flags         |
| 0x4B | SVC_NPC_DEATH      | S→C       | reliable   | NPC died                 |
| 0x4C | SVC_NPC_DESPAWN    | S→C       | reliable   | NPC removed              |
| 0x4D | SVC_NPC_SYNC       | S→C       | reliable   | NPC checksum             |
| 0x4E | SVC_NPC_RESYNC     | S→C       | reliable   | Full NPC state dump      |
| 0x50 | SVC_OBJECTIVE_STATUS| S→C      | reliable   | Objective changed        |
| 0x51 | SVC_STAGE_FLAG     | S→C       | reliable   | Stage flag changed       |
| 0x52 | SVC_MISSION_VAR    | S→C       | reliable   | AI script variable       |
| 0x53 | SVC_ALARM          | S→C       | reliable   | Alarm state              |
| 0x54 | SVC_CUTSCENE_START | S→C       | reliable   | Enter cutscene           |
| 0x55 | SVC_CUTSCENE_END   | S→C       | reliable   | Exit cutscene            |
| 0x56 | SVC_INVENTORY_SYNC | S→C       | reliable   | Full inventory           |
| 0x57 | SVC_MISSION_COMPLETE| S→C      | reliable   | Mission result           |
| 0x58 | SVC_TELEPORT       | S→C       | reliable   | Force player teleport    |
| 0x59 | SVC_VEHICLE_ENTER  | S→C       | reliable   | Player entered vehicle   |
| 0x5A | SVC_VEHICLE_EXIT   | S→C       | reliable   | Player exited vehicle    |
| 0x07 | CLC_COOP_READY     | C→S       | reliable   | Client ready for mission |

---

## Remaining Work

### Testing
- [ ] Compile all changes — Phase 10 audit fixes + D2a character select screen
- [ ] Test character select: scrollable list shows all bodies, selection updates 3D preview
- [ ] Test character select: head carousel still works below the body list
- [ ] Test character select: locked characters properly indicated
- [ ] Test character select: modded characters (GE, Kakariko etc.) appear in list
- [ ] Test autogun desync: throw laptop in netplay, verify both sides see same targeting
- [ ] Compile all changes from C8/C10/C11/C12 phases (linker fix applied: Net-prefixed handlers in netmenu.c)
- [ ] Test co-op character selection: host picks character → appears in-game with correct model
- [ ] Test client character selection: client picks in lobby → server sees updated settings → appears in-game
- [ ] Test "Default" character: both players select Default → Joanna Dark outfit per mission
- [ ] Test modded characters: select GE/mod bodies → correct model in-game
- [ ] Test co-op full flow: host co-op → NPC replication → objectives → alarm → cutscene → mission complete/fail endscreen
- [ ] Test co-op abort: server abort → client transitions, client abort → disconnect
- [ ] Test co-op disconnect mid-mission → endscreen doesn't crash (NULL safety)
- [ ] Test co-op reconnect: disconnect mid-mission → rejoin → NPC resync
- [ ] Test late CLC_COOP_READY: verify no double stage init
- [ ] Test Phase 9 fixes: compile and verify netbuf graceful error, weapon validation, preserved timeout
- [ ] Test reconnect flow: disconnect, rejoin, verify scores preserved
- [ ] Test co-op host menu: player status shows "Waiting..." → shows name on connect
- [ ] Test co-op host Cancel: server stops, returns to Network Game menu
- [ ] Test co-op host ESC: blocked by IGNOREBACK, must use Cancel
- [ ] Test Join Connect button: disabled when address empty, enabled when typed
- [ ] Test Join → MP server: Character dropdown stays hidden in joining dialog
- [ ] Test Join → Co-op server: Character dropdown appears in joining dialog at CLSTATE_LOBBY
- [ ] Test Host MP flow: unchanged, no Character dropdown, Start Game enters Combat Simulator

### Deferred Features (from co-op phases)
- [ ] Dynamic NPC spawning (SVC_NPC_SPAWN) for AI-scripted spawns during mission
- [ ] Cutscene camera sync enhancement (send animation number for full client-side cinematic)
- [ ] SVC_INVENTORY_SYNC for co-op reconnection inventory restoration

### Other
- [ ] Blue geometry investigation at Paradox center pit
- [ ] Async recent server queries (current approach blocks briefly during refresh)

---

## Recently Completed Work (Session 2026-03-16)

### Phase 10: Security Audit Fixes (DONE)
**Goal**: Fix vulnerabilities found during full codebase audit.

#### CRIT-1: Missing autogunTick NETMODE_CLIENT Guard (propobj.c)
- **Problem**: `autogunTick` had no client guard despite `autogunTickShoot` having one. Clients ran independent autogun targeting AI, causing visual desync.
- **Fix**: Added `if (g_NetMode == NETMODE_CLIENT) { return; }` at top of `autogunTick()`.
- **Files**: `src/game/propobj.c`

#### CRIT-2: Unvalidated playernum Array Access (netmsg.c — SvcStageStartRead)
- **Problem**: `playernum` from network state used directly as `g_PlayerConfigsArray[]` index without bounds checking. Out-of-range value causes out-of-bounds write.
- **Fix**: Added `if (playernum >= MAX_PLAYERS) { continue; }` with warning log before array access.

#### CRIT-3: Unvalidated playernum in setCurrentPlayerNum (netmsg.c — SvcPlayerStatsRead, SvcPropDoorRead)
- **Problem**: `actcl->playernum` passed to `setCurrentPlayerNum()` without validation in two locations.
- **Fix**: Added `if (actcl->playernum >= MAX_PLAYERS) { return 1; }` before each call.

#### CRIT-4: ownerplayernum Bitshift Overflow (netmsg.c — netbufReadHidden)
- **Problem**: `ownerplayernum` shifted left by 28 into 4-bit field without validation. Values >= 16 corrupt the `hidden` field.
- **Fix**: Added `if (ownerplayernum < MAX_PLAYERS)` guard around the bitshift.

#### MED-1: g_NetNumClients Underflow on NULL Peer Disconnect (net.c)
- **Problem**: Spurious disconnect from peer with no attached client decremented `g_NetNumClients`. Counter could underflow, breaking co-op Start button and server queries.
- **Fix**: Removed the `--g_NetNumClients` in the NULL peer path — counter was never incremented for these peers.

#### MED-2: NULL Dereference on g_Vars.coop During Disconnect (player.c)
- **Problem**: Death path and respawn path dereference `g_Vars.coop` after only checking `coopplayernum >= 0`. During disconnect window, `coop` can be NULL.
- **Fix**: Added `g_Vars.coop != NULL` (and `g_Vars.bond != NULL` in respawn path) before member access.

**Key files**: `src/game/propobj.c`, `port/src/net/netmsg.c`, `port/src/net/net.c`, `src/game/player.c`

### Phase D2a: Character Select Screen Redesign (DONE)
**Goal**: Replace the arrow-based carousel for body/head with a scrollable list showing all 63+ characters at once. PC has no N64 memory constraints.

#### New Character Body List (setup.c)
- **`mpCharacterBodyListHandler()`**: New `MENUITEMTYPE_LIST` handler replacing the carousel. Shows all body names in a scrollable list with instant selection.
- Uses `MENUOP_GETOPTIONTEXT` → `mpGetBodyName()` for each entry.
- `MENUOP_SET` → auto-assigns matching head via `mpGetMpheadnumByMpbodynum()`, then triggers 3D model preview update.
- `MENUOP_21` → locked character check via `challengeIsFeatureUnlocked()`.
- `MENUOP_GETSELECTEDINDEX` → tracks current `mpbodynum`.

#### New Dialog Handler (setup.c)
- **`mpCharacterSelectDialogHandler()`**: Replaced `menudialog0017a174`. Initializes 3D model on `MENUOP_OPEN`, continuously updates rotation/animation/zoom on `MENUOP_TICK`.

#### Updated Menu Items (setup.c)
- `g_MpCharacterMenuItems[]` now contains:
  - `MENUITEMTYPE_LIST` (body selection, width=120px, height=160px, MENUITEMFLAG_LIST_WIDE)
  - `MENUITEMTYPE_CAROUSEL` (head customization, kept for optional head-only changes)
- Old items removed: body name label, body carousel.

#### Design Rationale
- With 63+ characters (including mods), a one-at-a-time carousel is painful UX.
- The scrollable list shows ~10 names at once with instant scroll to any character.
- Head carousel kept as secondary option — most players just want to pick a body and go.
- 3D model preview continues to render on the right side of the dialog, same as before.
- Co-op lobby dropdowns (`netmenu.c`) left unchanged — different UX context (compact lobby UI).

**Key files**: `src/game/mplayer/setup.c`

---

## Future Roadmap

### Phase D1: PC-Only Cleanup — Remove N64 Guards (DONE)
**Goal**: Strip all `#ifdef PLATFORM_N64` / `#ifndef PLATFORM_N64` guards. PC is the only target.

**Scope**: 672 guards across 114 files. Three categories:
1. **Dead code removal** (~186 `#ifdef PLATFORM_N64` blocks): Delete the N64 branch entirely. These contain N64-specific DMA, VI, RMON, audio SDK calls, controller polling, etc.
2. **Unconditional promotion** (~485 `#ifndef PLATFORM_N64` blocks): Remove the `#ifndef`/`#endif` wrapper, keeping the PC code unconditional.
3. **Careful review** (~170 in propobj.c, player.c, bondmove.c, bondgun.c, bg.c): Both branches have meaningful logic. Verify the PC branch is complete before deleting N64 branch. Check control mode handling (LOOKAHEAD vs FORWARDPITCH), rendering paths, and type definitions.

**Priority order**:
1. `src/include/types.h` (18 guards) — type consolidation, foundational
2. `src/include/constants.h`, `src/include/data.h` — constant/struct cleanup
3. `src/lib/` (35 guards) — DMA, audio, memory, RMON (simple removals)
4. `include/PR/` (13 guards) — Nintendo platform headers
5. `src/game/` (566 guards) — gameplay files, largest batch, do in sub-groups:
   - Menu files first (menu.c, menuitem.c, mainmenu.c, menugfx.c)
   - Then multiplayer (mplayer.c, setup.c, ingame.c)
   - Then gameplay core (player.c, bondmove.c, bondgun.c, bondview.c, bondwalk.c)
   - Then rendering/world (bg.c, propobj.c, sight.c, lv.c)
   - Then character/AI (chraction.c, bot.c)

**Risk**: Some N64 codepaths may be referenced by function pointers or macros. Grep for any remaining references after each batch.

**Result**: All 672 guards removed across 114+ files. Zero `PLATFORM_N64` references remain in `src/`, `port/`, `include/`, and build system. Cleanup categories executed:
- `src/include/types.h` (18 guards) — type consolidation to PC types (uintptr_t, stdint.h)
- `src/include/constants.h` (5 guards) — weapon enum, button mappings, co-op constants
- `src/lib/` (35 guards across 14 files) — DMA→bcopy, audio SDK→PC audio, RMON→stdio, memp→heap
- `src/game/` (566 guards across ~70 files) — all gameplay, menu, MP, rendering, input files
- `include/PR/` — Nintendo platform headers cleaned
- All `.bak` files removed after verification

**D1 Post-Fix (compilation)**: Comprehensive cleanup of orphaned preprocessor directives left behind by the N64 guard strip:

1. **types.h**: Two orphaned `#endif` (lines 4911, 6145) prematurely closed the include guard → hundreds of "redefinition of struct" errors. Fixed by removing both.

2. **Orphaned `#endif`** (12 across 9 files): bg.c, credits.c, menu.c, menugfx.c, title.c, trainingmenus.c (×2), general.c (×2), mplayer.c, setup.c — each was a leftover closer from a removed `#if PLATFORM_N64` guard. Removed all.

3. **Orphaned `#else` with dead N64 code** (3 files): credits.c, menugfx.c, title.c — each had `#else` + dead N64 code + `#endif`. Removed the `#else`, the N64 dead code, and the `#endif`, keeping only the PC code path.

4. **bg.c function corruption**:
   - `bgGarbageCollectRooms`: Entire body was N64-only (mema garbage collection not needed on PC). Emptied the function body.
   - `bgTestHitOnObj` and `bgTestHitInVtxBatch`: Broken `if` conditions missing PC branch `|| (imggdl->words.w1 & 1)` and `|| (tmpgdl->words.w1 & 1)` respectively. Restored from reference.
   - `menuResolveText` in menu.c: Leftover N64 crash path `return NULL; }` removed.

5. **Orphaned `#else` directives** (14 across 11 files): bondeyespy.c, bondgun.c, bondwalk.c, botinv.c, challenge.c, chr.c, gunfx.c, lang.c, playerreset.c, propobj.c (×2), sight.c (×2), weatherreset.c — each had an `#else` that lost its parent `#if PLATFORM_N64`, plus dead N64 code blocks. All removed, keeping only PC code paths.

**Final verification**: Zero `PLATFORM_N64` references remain. All `#if`/`#endif` pairs balanced. Zero orphaned `#else`/`#elif` directives. Codebase preprocessor-clean.

### Phase D2: Jump Polish & Bot Jump AI
**Goal**: Ensure jump works reliably for players; teach simulants to jump.

#### Player Jump Verification
- **Input**: `BUTTON_JUMP` (CONT_4000) → spacebar via `joyGetButtonsPressedOnSample()` in bondmove.c:1863
- **Impulse**: `DEFAULT_JUMP_IMPULSE = 6.5f`, overridable by `g_MpSetup.jumpimpulse` (match limits) or `PLAYER_EXTCFG().jumpheight` (profile)
- **Ceiling clamp**: Already in place (Phase 8) — `cdFindCeilingRoomYColourFlagsAtPos` in bondwalk.c
- **Network**: `UCMD_JUMP` (1 << 11) in `netplayermove.ucmd` — already transmitted over network
- **Testing**: Verify jump locally (not via TeamViewer — held-key detection unreliable over remote desktop)

#### Bot Jump AI (NEW)
- **Reactive jumping**: Bots detect foot-level obstacles (short walls, railings, crates) via forward raycast at knee height. If obstacle height < max jump height and clear space above, trigger jump.
- **Evasive jumping**: When under fire (recently took damage + attacker visible), random chance to jump strafe. Varies by difficulty (0% on Agent, 15% on SA, 30% on PA).
- **Pathfinding integration**: Tag nav nodes with "jump required" flag where vertical gap exists. Bot pathfinder prefers walkable paths but allows jump paths when no alternative exists.
- **Implementation files**: `src/game/bot.c` (AI decision), `src/game/botact.c` (action execution), `src/game/bondwalk.c` (jump mechanics shared with player)
- **Network**: Bot jump state transmitted via existing SVC_CHR_MOVE (position + action). Client sees smooth vertical movement from position interpolation.

#### Character Select Screen Redesign (NEW)
- **Current state**: Arrow-based carousel for head and body with name displayed above. Dated UX for 60+ characters.
- **New design**: Split layout — scrollable list of body/head names on one side, live 3D model preview on the other. Player scrolls through the list, preview updates in real time.
- **Implementation**: Rework `mpCharacterBodyMenuHandler`/`mpCharacterHeadMenuHandler` in `setup.c`. Replace MENUITEMTYPE_CAROUSEL with a scrollable list widget (MENUITEMTYPE_LIST or custom). Preview pane reuses existing model rendering from the current carousel but at larger size.
- **Applies to**: Combat Simulator character select, co-op lobby character dropdown, and any future character selection UI.
- **Key files**: `src/game/mplayer/setup.c`, `src/game/menu.c` (list widget), `src/game/menuitem.c`

#### Custom Simulant Types (NEW)
- **Goal**: Allow creation of new simulant personality types with custom stats, traits, and character models.
- **Current system**: Hardcoded simulant types (MeatSim, EasySim, NormalSim, HardSim, PerfectSim, DarkSim, PeaceSim, ShieldSim, RocketSim, KazeSim, FistSim, PreySim, CowardSim, JudgeSim, FeudSim, SpeedSim, TurtleSim, VengeSim). Each has stat profiles (accuracy, speed, health, aggression, reaction time) and behavioral traits (flee threshold, weapon preferences, movement patterns).
- **New feature**: Custom simulant definitions loadable from mod packs or a `simulants/` config directory.
- **Simulant definition format** (in mod.json or standalone):
  ```json
  {
    "id": "skedar_warrior",
    "name": "SkedarSim",
    "description": "Aggressive Skedar warrior with high health and melee preference",
    "body": "base:skedar",
    "head": "base:skedar_head",
    "scale": 1.0,
    "stats": {
      "health": 2.0,
      "speed": 0.8,
      "accuracy": 0.6,
      "aggression": 1.0,
      "reaction_time": 0.7
    },
    "traits": ["aggressive", "melee_preference", "no_flee"]
  }
  ```
- **Boss simulants**: Larger scale (e.g., 1.5x) with scaled hitbox, much higher health, custom behaviors. Example: "Skedar Broodmother" — high health, large scale, periodically triggers NPC spawns (ties into SVC_NPC_SPAWN from deferred features).
- **Scenario possibilities**: Challenge maps where players fight boss simulants that summon minions — e.g., broodmother Skedar spawning fast/low-health baby Skedars. Requires Phase D8 map editor for full realization but the simulant system can be built independently.
- **Implementation**: Extend `struct simtype` or create new `struct customsim`. Dynamic simulant registry (like dynamic body/head tables from D3). Menu integration for selecting custom sims in Combat Simulator bot setup.
- **Key files**: `src/game/bot.c` (sim type stats), `src/game/botact.c` (behavior), `src/game/mplayer/setup.c` (sim selection menu), `port/src/modmgr.c` (loading from mods)

### Phase D3: Mod Manager Framework (PLANNED — detailed plan in docs/MOD_LOADER_PLAN.md)
**Goal**: Dynamic mod loading with runtime-extensible asset tables, in-game mod manager menu, hot-toggle, network mod sync with auto-download. Convert existing hardcoded mods to mod.json format.

**User decisions (2026-03-17)**:
- Menu system: Extend PD's existing menu framework (no Dear ImGui/SDL overlay)
- Network sync: Auto-download missing mods from server (not just refuse+error)
- Mod toggle: Persist + hot-toggle (enable/disable at runtime, soft reload to title)
- Legacy mods: Convert all 5 to standard mod.json format (toggleable, not hardcoded)

#### Sub-phases:
- **D3a**: Foundation — `modmgr.c/h`, cJSON bundling, mod scanning, mod.json parsing, config persistence (~400 LOC)
- **D3b**: Dynamic asset tables — shadow arrays for bodies/heads/arenas, accessor functions, network hash IDs (~300 LOC)
- **D3c**: fs.c refactor — replace `g_ModNum`/per-mod globals with registry iteration, multi-mod file resolution (~200 LOC)
- **D3d**: Mod Manager menu — main menu "Mods" button, list with enable/disable, description panel, apply button (~250 LOC)
- **D3e**: Hot-toggle — pristine stage table backup, `modmgrReload()`, texture cache flush, return to title (~150 LOC)
- **D3f**: Network mod sync — SVC_MOD_MANIFEST, SVC_MOD_CHUNK, CLC_MOD_REQUEST, CLC_MOD_ACK, LZ4 transfer, progress bar, protocol v19 (~500 LOC)
- **D3g**: Cleanup — dead code removal, load order UI, conflict warnings (~100 LOC)

#### Architecture Summary:
- **Shadow arrays**: Base game arrays (`g_MpBodies[63]`, `g_MpHeads[76]`, `g_MpArenas[75]`) stay static. Mod-added content goes in separate `g_ModBodies/Heads/Arenas[]` arrays. Accessor functions (`mpGetBody(index)`) transparently handle base+mod ranges.
- **Network IDs**: CRC32 of `"modid:assetid"` strings for stable network references (array indices shift when mods toggle)
- **File resolution**: `fsFullPath()` iterates enabled mods in load-order priority instead of checking `g_ModNum`
- **Hot-toggle flow**: Toggle in menu → Apply → save config → restore pristine stage table → re-register enabled mods → flush texture cache → return to title
- **Network flow**: Client connects with manifest hash → mismatch → server sends manifest → client requests missing mods → LZ4 chunk transfer → client installs + reloads → reconnects

**Key files**: `port/src/modmgr.c` (new), `port/include/modmgr.h` (new), `port/src/modmenu.c` (new), `port/src/cjson.c` (bundled), `port/src/mod.c` (refactor), `port/src/fs.c` (refactor), `src/include/constants.h` (remove MOD_* defines), `src/game/mplayer/mplayer.c` (accessors), `src/game/mplayer/setup.c` (accessors), `src/game/mainmenu.c` (Mods button), `port/src/net/netmsg.c` (mod sync messages), `CMakeLists.txt`

### Phase D4: NAT Traversal & Direct Connect
**Goal**: Players connect directly without Hamachi or port forwarding.

#### Approach: STUN + UDP Hole Punching
- Use public STUN servers (free, no infrastructure needed): `stun.l.google.com:19302`, `stun.stunprotocol.org:3478`
- **STUN query**: On server start and client connect, send STUN binding request to discover external IP:port
- **Signaling**: Exchange external endpoints via a lightweight mechanism:
  - Option A: Manual exchange (paste address in join menu — simplest, works now)
  - Option B: Tiny relay server (Flask/Node on free-tier Render/Railway) that holds "room codes"
  - Option C: Discord Rich Presence integration (join via Discord invite)
- **Hole punch**: Both sides send UDP packets to each other's external address. ENet's connect/accept sits on top of the punched hole.
- **Fallback**: If hole punch fails (symmetric NAT), show error with instructions for port forwarding as last resort.

#### LAN Discovery
- Server broadcasts UDP announcement packet on port 27101 every 2 seconds (game name, map, player count, version)
- Client listens on 27101, populates "LAN Games" list in join menu
- Zero configuration required for same-network play

**Key files**: `port/src/net/natstun.c` (new), `port/src/net/netlan.c` (new), `port/src/net/netmenu.c` (LAN browser UI)

### Phase D5: Dedicated Server
**Goal**: Headless server mode for solo testing and persistent hosting.

#### Architecture
- `--dedicated` CLI flag sets `g_Dedicated = 1` global
- Skip initialization: video (`videoInit`), audio (`audioInit`), input (local player), texture cache, menu rendering
- Auto-start server on configured port (default 27100)
- Read config from `server.cfg` or CLI args: map, mode (mp/coop/anti), max players, game settings, mod list, map rotation

#### Server Console
- stdin command loop (separate thread or polled in main loop):
  - `map <name>` — change map
  - `kick <player>` — disconnect player
  - `status` — show connected players, current map, uptime
  - `rotate` — advance to next map in rotation
  - `say <msg>` — broadcast server message
  - `quit` — clean shutdown
- stdout logging: player connects/disconnects, map changes, kills, match results

#### GUI Server Manager (later)
- Optional GUI wrapper around the dedicated server process
- Player list, map rotation editor, mod pool manager, log viewer
- Could be a separate executable or a web interface (localhost HTTP server)

#### Stub Systems
- Menu system: no-op all render calls when `g_Dedicated`
- Player: no local player allocated (server-only slots)
- Input: no controller/keyboard polling
- Video: stub `videoInit()`, `videoBeginFrame()`, `videoEndFrame()`
- Audio: stub all audio playback

**Key files**: `port/src/main.c` (--dedicated flag), `port/src/video.c` (stub), `port/src/audio.c` (stub), `port/src/net/netdedicated.c` (new — console, config, rotation)

### Phase D6: Update System
**Goal**: Clients can check for and apply updates seamlessly, preserving save data.

#### Version Tracking
- `#define BUILD_VERSION "2026.03.16.1"` in `port/include/version.h`
- Baked into executable at compile time
- Displayed in main menu corner and server info

#### Update Check
- On launch, fetch `https://raw.githubusercontent.com/<user>/<repo>/main/version.txt` (or GitHub API releases endpoint)
- Compare remote version against local `BUILD_VERSION`
- If newer: show notification in main menu — "Update available: v2026.03.20.1 — Press [key] to update"
- If same or offline: proceed normally, no notification

#### Update Application
- Download release zip from GitHub (via libcurl or WinHTTP — already a PC port, native HTTP is fine)
- Extract to temp directory
- Compare file hashes: only replace changed files (delta-style, at file granularity)
- Preserve: `saves/` directory, `config.cfg`, `mods/` directory (user-installed mods)
- Replace: executable, DLLs, base game assets, built-in mods
- Restart game after update

#### Save Data Versioning
- Add `u32 save_version` header to save files
- Write migration functions for format changes (e.g., if struct layout changes between versions)
- Save data location: `%APPDATA%/PerfectDarkMike/` (or configurable)

**Key files**: `port/include/version.h` (new), `port/src/update.c` (new), `src/game/gamefile.c` (save versioning)

### Phase D7: Mod Distribution & Counter-Op
**Goal**: Server sends missing mods to clients. Counter-operative mode fully functional.

#### Mod Transfer Protocol
- Dedicated ENet channel (channel 3) for file transfer — separate from gameplay
- Server packs missing mod into LZ4-compressed archive, streams in 16KB chunks
- Client reassembles, verifies SHA-256 hash, writes to `mods/` directory
- Progress bar in joining UI
- Size limit: 50MB per mod (configurable), total 200MB per session
- After all mods received: reload mod registry, reconnect to server

#### Counter-Operative Mode
- `NETGAMEMODE_ANTI (2)` path: already stubbed in session setup (Phase C1)
- Player 2 possesses NPCs instead of controlling a co-op buddy
- **NPC authority transfer**: Server tells all clients "NPC X is now player-controlled" — route player 2's `ucmd` to NPC movement instead of AI
- **Respawn into new NPC**: When possessed NPC dies, player 2 selects (or auto-assigned) a new NPC body
- **Scoring**: Counter-op player scores for killing bond, bond scores for completing objectives and killing possessed NPCs
- Extends existing NPC replication from Phase C2

**Key files**: `port/src/net/netmod.c` (new — transfer protocol), `port/src/net/net.c` (counter-op authority), `src/game/chraction.c` (NPC possession)

### Phase D8: Map Editor & Custom Characters
**Goal**: Standalone tools for creating content that exports as mod packs.

#### Map Editor (Standalone Application)
- Import geometry from standard formats (Quake .map → BSP compile, or direct PD BSP editing)
- Visual prop placement: spawn points, weapons, ammo, objectives, NPC patrol routes
- AI path editor: nav node placement, jump-required tags, patrol waypoints
- Export to PD stage format as a mod pack (`mod.json` + stage data + textures)
- Could be built with Dear ImGui + OpenGL for rapid prototyping

#### Character Creator
- Import body/head models from standard 3D formats (OBJ, FBX → convert to PD model format)
- Texture assignment and preview
- Export as mod pack body/head entry
- Example use case: James Bond from GoldenEye as an unlockable mod character

#### Content Pipeline
- `tools/modpacker.py` — script to validate and package a mod directory into distributable format
- `tools/modconvert.py` — convert raw assets (OBJ, PNG) to PD-native formats (model Z files, texture bins)

**Key files**: `tools/mapeditor/` (new — standalone), `tools/modpacker.py` (new), `tools/modconvert.py` (new)

## Key Files
- `port/include/net/netmsg.h` — Message type defines + function declarations
- `port/include/net/net.h` — Protocol version (18), resync flags, preserved player struct, recent server struct, preserve timeout
- `port/src/net/netbuf.c` — Network buffer read/write with graceful error handling (no more __builtin_trap)
- `port/src/net/netmsg.c` — Message encode/decode, desync tracking, reconnect logic, weapon validation
- `port/src/net/net.c` — Core networking, frame loop, resync dispatch, preserved player management + timeout, recent servers
- `port/src/net/netmenu.c` — Network menus: host MP/co-op/join/recent servers, co-op player status, in-match controls dialog
- `src/game/mplayer/ingame.c` — MP pause menu with team switch and controls button (network only)
- `src/game/mplayer/setup.c` — Character select menus, loaddelay fix for texture re-entry
- `src/game/bot.c` — Bot AI tick (server-only), weapon model sync (client), client bypass
- `src/game/bondwalk.c` — Player vertical movement, jump, gravity, ceiling clamp, prop ground probe (PC)
- `src/game/player.c` — Player tick, death/respawn, null-safe client checks in respawn paths (incl. coop)
- `src/game/chraction.c` — Chr damage (server guard), chrDie → mpstatsRecordDeath
- `src/game/propobj.c` — Prop/obj tick, autogun guards, objDamage server guard, alarm system
- `src/game/objectives.c` — Mission objective checking, server-authoritative in co-op (C3/C8)
- `src/game/bondcutscene.c` — Cutscene init/tick (no net awareness yet — co-op Phase C5)

## Architecture Notes
- `g_NetMode`: NETMODE_NONE(0), NETMODE_SERVER(1), NETMODE_CLIENT(2)
- `g_BotCount` / `g_MpBotChrPtrs[MAX_BOTS]` — bot tracking (chrdata reused across respawns)
- Props identified by `syncid` across network (offset 0x48 on prop struct, PC-only)
- Bots are PROPTYPE_CHR with `chr->aibot != NULL`
- NPCs are PROPTYPE_CHR with `chr->aibot == NULL` and not player-linked
- Autoguns (laptop) are OBJTYPE_AUTOGUN — targeting/shooting guarded on clients
- Prop iteration: `g_Vars.activeprops` linked list via `prop->next`
- 60 Hz tick rate, unreliable for position, reliable for state/events
- Desync detection: chr checksum every 60 frames, prop checksum every 120 frames
- Resync escalation: 3 consecutive desyncs → auto-request, 5-second cooldown
- Late-join: auto full resync on stage start
- Reconnect: preserved player name matching → SVC_STAGE_START + full resync + score sync
- Preserved player timeout: 5 minutes (NET_PRESERVE_TIMEOUT_FRAMES), checked every 10 seconds
- Protocol version: 18
- CLC_SETTINGS includes team field (breaking change from v15)
- Co-op mode: `g_NetGameMode` selects MP/COOP/ANTI, SVC_STAGE_START branches accordingly
- Co-op player management: `g_Vars.coopplayernum`, `g_Vars.bond`, `g_Vars.coop`
- Mission system: `objectiveCheck()` evaluates objectives, `g_Objectives[]` array, stage flags
- Alarm: `g_AlarmTimer`, `alarmActivate()`/`alarmDeactivate()` in propobj.c
- Cutscenes: `bcutsceneInit()`/`bcutsceneTick()`, `cutsceneStart()` in chraction.c
- NPC AI: `chraTickBg()` background AI script execution per chr

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

## Damage Authority Summary
| Entry Point      | File        | NETMODE_CLIENT Guard | Notes                          |
|------------------|-------------|----------------------|--------------------------------|
| func0f0341dc     | chraction.c | Returns early        | Chr hit detection wrapper       |
| chrDamage        | chraction.c | (via SVC_CHR_DAMAGE) | Called from netmsg on clients  |
| objDamage        | propobj.c   | Returns early        | Prop damage (allows via netmsg)|
| autogunTick      | propobj.c   | Returns early        | Autogun targeting               |
| autogunTickShoot | propobj.c   | Returns early        | Autogun firing effects          |
| botTickUnpaused  | bot.c       | Returns early        | Bot AI loop                     |

## Preserved Player / Reconnect Architecture
- `g_NetPreservedPlayers[NET_MAX_CLIENTS]` — array of preserved player identities
- Fields: name, playernum, team, killcounts[MAX_MPCHRS], numdeaths, numpoints, active, preserveframe
- On disconnect during game: `netServerPreservePlayer()` saves identity + timestamp → `playerDie(true)` kills character → `netClientReset()` unlinks client
- On reconnect: `netServerFindPreserved()` matches by name → `netServerRestorePreserved()` re-links player struct, config, scores → clears `CLFLAG_ABSENT` → schedules respawn if dead → sends `SVC_STAGE_START` + full resync
- Timeout: expired after 5 minutes via `NET_PRESERVE_TIMEOUT_FRAMES`, checked every 600 ticks in netEndFrame
- Cleared on new stage start in `netServerStageStart()`

---

## Session: 2026-03-16 — Post-Build Runtime Fixes

### Status: IN PROGRESS

The N64 platform strip (Phase D1) is complete. Build compiles successfully. First multiplayer test revealed several runtime issues.

### Completed Fixes This Session

#### FIX-1: libgcc_s_seh-1.dll Missing (CMakeLists.txt) ✅
- **Problem**: Friend couldn't run the exe — `libgcc_s_seh-1.dll was not found`
- **Root cause**: CMakeLists.txt line 247 had `-static-libstdc++` but was missing `-static-libgcc`
- **Fix**: Added `-static-libgcc` to `EXTRA_LIBRARIES` on Windows (line 247)
- **File**: `CMakeLists.txt`

#### FIX-2: Bot Weapon Preference Table Misaligned (botinv.c) ✅
- **Problem**: Bots not moving or respawning in multiplayer. Server wasn't issuing AI commands properly.
- **Root cause**: N64 strip deleted the `WEAPON_COMBATKNIFE` (0x1a) entry from `g_AibotWeaponPreferences[]` array. This was inside a `#if (VERSION == VERSION_JPN_FINAL) && defined(PLATFORM_N64)` guard that the strip tool didn't handle. Result: empty line at index 0x1a, all subsequent weapon entries shifted down by one position (CROSSBOW at 0x1a instead of 0x1b, etc.)
- **Impact**: Every bot weapon lookup by index returned wrong configuration — wrong targeting distances, ammo management, reload delays. Bots would malfunction, refuse weapons, or use them incorrectly.
- **Fix**: Restored PC version of WEAPON_COMBATKNIFE entry: `{ 20, 40, 24, 40, 1, 1, BOTDISTCFG_CLOSE, BOTDISTCFG_DEFAULT, 0, 5, 0, 1, 1, 0 }`
- **File**: `src/game/botinv.c` line 60

#### FIX-3: Jump Height Config Registration (main.c) ✅
- **Problem**: Jump doesn't work in Carrington Institute (solo). Menu slider "Match" default = 0, changing to 10 does nothing.
- **Root cause**: `jumpheight` field never registered with config system in `main.c`. The field exists in `struct extplayerconfig` (types.h:6129) and has menu handler (optionsmenu.c:1492-1516), impulse code (bondwalk.c:864-909), and input handling (bondmove.c:1826-1833) — but the value was never persisted.
- **Note**: The DEFAULT_JUMP_IMPULSE (6.5f) fallback in bondwalk.c should still allow jumping even with jumpheight=0, so there may be an additional issue (possibly input binding or `wantsjump` not being set). The config registration fix ensures the menu slider value persists correctly.
- **Fix**: Added `configRegisterFloat("Game.Player%d.JumpHeight", ..., 0.f, 20.f)` to the player config loop in main.c
- **File**: `port/src/main.c` line 213

#### FIX-4: Default Mod Directories Compiled into EXE (fs.c) ✅
- **Problem**: Mods only loaded when launched via BAT file with `--moddir` args. Running EXE directly = no mods.
- **Fix**: Added fallback defaults in `fsInit()` for all 5 mod directories when CLI args not provided:
  - `--moddir` → `"mods/mod_allinone"`, `--gexmoddir` → `"mods/mod_gex"`, `--kakarikomoddir` → `"mods/mod_kakariko"`, `--darknoonmoddir` → `"mods/mod_dark_noon"`, `--goldfinger64moddir` → `"mods/mod_goldfinger_64"`
- **File**: `port/src/fs.c` (5 locations, each after `sysArgGetString()` call)
- **Effect**: BAT file is no longer needed for basic operation. CLI args still override defaults.

#### FIX-5: Static Link libwinpthread (CMakeLists.txt) ✅
- **Problem**: `libwinpthread-1.dll` had to be distributed alongside the EXE
- **Fix**: Added `-Wl,-Bstatic -lwinpthread -Wl,-Bdynamic` to Windows `EXTRA_LIBRARIES`
- **File**: `CMakeLists.txt` line 247

#### FIX-6: Character Screen Layout (setup.c) ✅
- **Problem**: Character list and 3D preview overlapped — both centered
- **Fix**: Narrowed list width from 0x78 (120px) to 0x54 (84px), pushed 3D model from curposx=8.2f to 15.0f
- **File**: `src/game/mplayer/setup.c` (menu items + mpCharacterBodyMenuHandler)
- **Status**: NEEDS TESTING — values may need fine-tuning visually

#### FIX-7: Missing g_NetMode Guard in mpEndMatch (mplayer.c) ✅
- **Problem**: `mpApplyWeaponSet()` called on clients (should be server-only). N64 strip removed `#ifndef PLATFORM_N64` which also contained `if (g_NetMode != NETMODE_CLIENT)`.
- **Fix**: Wrapped in `if (g_NetMode != NETMODE_CLIENT)` guard
- **File**: `src/game/mplayer/mplayer.c` (mpEndMatch function)

#### FIX-8: Missing g_NetMode Guard in mpHandicapToDamageScale (mplayer.c) ✅
- **Problem**: In netplay, handicap damage should always return 1.0f (disabled). The `if (g_NetMode) { return 1.f; }` was inside `#ifndef PLATFORM_N64` and got stripped.
- **Fix**: Restored `if (g_NetMode) { return 1.f; }` at top of function
- **File**: `src/game/mplayer/mplayer.c`

#### FIX-9: Missing Server Chrslots Setup in mpStartMatch (mplayer.c) ✅
- **Problem**: When server starts a match, it needs to set `g_MpSetup.chrslots` based on connected clients. This block was inside `#ifndef PLATFORM_N64` and got deleted entirely. Also, `mpApplyWeaponSet` at match start was unguarded for clients.
- **Fix**: Restored server chrslots setup block + `if (g_NetMode != NETMODE_CLIENT)` guard on weapon set randomization
- **File**: `src/game/mplayer/mplayer.c` (mpStartMatch function)

#### FIX-10: Misplaced Chrslots Loop + Missing Condition (menutick.c) ✅
- **Problem**: Network client slot assignment was incorrectly inside the per-player `for` loop (running N times instead of once), and was missing `&& g_MenuData.prevmenuroot == -1` condition.
- **Fix**: Moved block outside the loop, added missing condition
- **File**: `src/game/menutick.c`

#### FIX-11: Missing Save Dialog Guard in ingame.c ✅
- **Problem**: `menuPushDialog(&g_MpEndscreenSavePlayerMenuDialog)` called in netplay (should be local-only). Guard `if (!g_NetMode)` was inside `#ifndef PLATFORM_N64`.
- **Fix**: Restored `if (!g_NetMode)` before the menuPushDialog call
- **File**: `src/game/mplayer/ingame.c`

#### FIX-12: Join Screen Button Overlap (netmenu.c) ✅
- **Problem**: Join screen had 5 items (Address, Connect, Recent Servers, Separator, Back) — too many for the dialog height, causing buttons to overlap and the bottom button to be cut off or crash when clicked.
- **Fix**: Removed the Separator item to reduce dialog height. Now 4 items: Address, Connect, Recent Servers, Back.
- **File**: `port/src/net/netmenu.c` (g_NetJoinMenuItems)

### Known Issues (Still Need Testing)

#### ISSUE-1: Bots Not Moving/Respawning 🔧
- **Root causes found and fixed**: botinv.c weapon table misalignment (FIX-2) + missing mpStartMatch server chrslots setup (FIX-9) + missing mpHandicapToDamageScale netmode guard (FIX-8)
- **User's insight**: "Is it possible the bots weren't moving because it was running us both as clients, rather than me, the host, as the authoritative server?" — YES, this is very likely. FIX-9 restores the server chrslots setup that tells the game which slots are human players vs bots. Without it, the server may not have been properly asserting authority.
- **Status**: Multiple fixes applied. NEEDS TESTING.

#### ISSUE-2: End-Game Crash 🔧
- **Likely fixed by**: FIX-7 (mpEndMatch netmode guard), FIX-2 (bot weapon table), FIX-9 (server authority)
- **Status**: NEEDS TESTING.

#### ISSUE-3: Jump Not Working 🔧
- **Config fix applied** (FIX-3). The DEFAULT_JUMP_IMPULSE fallback should still provide 6.5f. May be input-related.
- **Status**: NEEDS TESTING.

#### ISSUE-4: Character Screen Layout 🔧
- **Layout fix applied** (FIX-6). List narrowed, model pushed right. Values may need visual tuning.
- **Status**: NEEDS TESTING.

#### ISSUE-5: Mod Character Models (Skedar/Dr Carroll) 🔧
- **Mod dirs now auto-load** (FIX-4). If the issue was mods not loading, this should fix it.
- **Remaining concern**: Body ID mapping (`BODY_PRESIDENT_CLONE` for Skedar, `BODY_TESTCHR` for Dr Carroll) may need verification against mod asset files.
- **Status**: NEEDS TESTING after mod dirs auto-load.

### Pending Work

#### TODO-1: Investigate SDL2/zlib Static Linking
- SDL2 requires static `libSDL2.a`, zlib needs `libz.a`
- May need to build from source for MinGW
- Lower priority (distributing 2 DLLs is acceptable short-term)
- **Files**: `CMakeLists.txt`, `cmake/FindSDL2.cmake`

#### TODO-2: Comprehensive N64 Strip Netmode Audit
- The 4 missing netmode guards found so far were critical. There may be more.
- Systematic audit: grep reference for all `g_NetMode` within 3 lines of `PLATFORM_N64`, verify each exists in mike.
- 72 of 77 patterns were confirmed correct; 5 were broken (4 fixed above, 1 was already correct).

#### TODO-3: Phase D3 — Dynamic Mod Loader (see Roadmap section above)
- The current mod system uses hardcoded arrays (`g_MpBodies[63]`, `g_MpHeads[75]`, `g_MpArenas[71]`) and hardcoded CLI args.
- Phase D3 roadmap calls for dynamic asset tables, mod.json manifests, runtime scanner, and network compatibility checks.
- **Priority**: After all runtime bugs are resolved.

### Reference Versions
- **Netplay reference**: `perfect_dark-netplay/perfect_dark-port-net/` — unmodified netplay port
- **AllInOneMods reference**: `perfect_dark-AllInOneMods/perfect_dark-allinone-latest/` — mod content source
- **Mike (working)**: `perfect_dark-mike/` — merged port, PC-only

### Build System
- CMake-based, MinGW/GCC on Windows
- User builds on their Windows machine (not in this VM sandbox)
- `cmake -B build && cmake --build build`
- Output: `build/pd.x86_64.exe` + DLLs

---

## Session: 2026-03-16 (continued)

### Completed Fixes

#### FIX-7: Dr Carroll Head Crash Protection (setup.c)
- **Root cause**: Dr Carroll (`BODY_TESTCHR`, mpbody index 0x3a) has `headnum=1000` and the g_HeadsAndBodies entry has `unk00_01=1` (integrated head — the body model includes its head, like a floating robot). The head carousel allowed scrolling through all heads, and changing heads on a body with an integrated head caused crashes.
- **Fix**: Added `mpBodyHasIntegratedHead()` helper function that checks `g_HeadsAndBodies[bodyid].unk00_01`. When true, the head carousel is locked to 1 option and SET operations are ignored.
- **Also**: Added bounds checking on carousel SET — `data->carousel.value` is now validated against `mpGetNumHeads2()`.
- **File**: `src/game/mplayer/setup.c` (lines ~2331-2377)

#### FIX-8: Null Model Protection (menu.c, player.c)
- **Root cause**: When a character model file doesn't exist or fails to load, `modeldefLoad()` returns NULL. Subsequent calls to `modelAllocateRwData(NULL)` and `modelInit(NULL, ...)` cause 0xc0000005 crashes.
- **Fix**: Added NULL checks after `modeldefLoad()` in three locations:
  1. `menu.c` `menuRenderModel()` — head+body model loading path: if body model is NULL, sets `curparams=newparams`, clears `newparams`, returns early. If head model is NULL, proceeds with body only (headnum=-1).
  2. `menu.c` `menuRenderModel()` — generic model loading path: same NULL check and early return.
  3. `player.c` — single-player and multi-player model loading: NULL check with `sysLogPrintf(LOG_WARNING)` and early return to prevent crash. Both 1-player path and 2-4 player path protected.
- **Files**: `src/game/menu.c` (lines ~1901-1948), `src/game/player.c` (lines ~1484-1535)

#### FIX-9: Simulant Spawning Debug Logging (setup.c)
- **Added**: Verbose logging to the simulant spawning path in `setupCreateProps()`:
  - Logs `chrslots`, `maxsimulants` when spawning starts
  - Logs each successful `botmgrAllocateBot()` call with chrnum and slot
  - Logs each skipped slot with the reason (chrslots bit state, mpIsSimSlotEnabled result)
  - Logs total spawned when done
  - Logs when spawning is skipped entirely (with normmplayerisrunning and mpHasSimulants states)
- **File**: `src/game/setup.c` (lines ~2051-2094)

#### FIX-10: Character Select Screen Redesign (setup.c)
- **Layout**: Body list (MENUITEMTYPE_LIST, 120px wide, 136px tall) + head carousel (MENUITEMTYPE_CAROUSEL) stacked vertically on left. 3D model preview pushed further right (curposx=20.0).
- **Preview on scroll**: Added static `s_PreviewBodyNum` and `s_PreviewHeadNum` variables. Body list handler responds to `MENUOP_LISTITEMFOCUS` (fires on cursor movement) to update 3D preview without committing. Only `MENUOP_SET` (A press) commits the selection.
- **Head carousel**: For integrated-head bodies (unk00_01=1), carousel shows 1 option and ignores changes. For normal bodies, carousel fires SET on each scroll which naturally updates the preview.
- **Dialog handler**: MENUOP_TICK uses preview state variables instead of committed config for live model updates.
- **Files**: `src/game/mplayer/setup.c` (mpCharacterBodyListHandler, menuhandlerMpCharacterHead, mpCharacterSelectDialogHandler, g_MpCharacterMenuItems)

#### FIX-11: In-Match Pause Menu Scrolling (ingame.c)
- **Root cause**: The pause menu has 11 items (challenge name, scenario, time/score/team limits, separator, game time, pause/unpause, team, controls, end game) but no scrolling enabled. On some resolutions the Controls button is cut off.
- **Fix**: Added `MENUDIALOGFLAG_SMOOTHSCROLLABLE` to `g_MpPauseControlMenuDialog` flags.
- **File**: `src/game/mplayer/ingame.c` (line ~426)

### Known Issues (Updated)

#### ISSUE: Invisible Character in Combat Simulator
- User reports: "camera went around my invisible character" and then crash after camera orbit.
- **Analysis**: If the player selected a character whose model file doesn't exist (like BODY_TESTCHR for Dr Carroll), the model loading fails → character is invisible. The crash happens when the camera/rendering code dereferences null model data. FIX-8 adds null checks that should prevent the crash, but the character will still be invisible if the model file doesn't exist.
- **Root cause**: BODY_TESTCHR uses FILE_CTESTCHR (file ID 0x0055). This may be a test/development model not included in the retail game data, or it may require mod data to be properly loaded. The mod directory defaults (FIX-4 from previous session) should help, but the actual model file availability needs verification.
- **Status**: FIX-8 prevents crash. Invisibility needs investigation of game data files.

#### ISSUE: Simulants Not Spawning
- User reports simulants don't appear in combat simulator.
- **Analysis**: g_NetMode audit found no missing guards. The spawning code at setup.c:2051-2084 checks `g_Vars.normmplayerisrunning && mpHasSimulants()`. `mpHasSimulants()` checks `g_MpSetup.chrslots & 0xfff0`.
- **Possible causes**: (a) chrslots bits 4-15 not being set from menu config, (b) `normmplayerisrunning` is false, (c) bot allocation fails silently.
- **Status**: FIX-9 adds comprehensive logging. User needs to test and provide log output.

#### ISSUE: Jump Not Working
- Previous session added verbose logging to bondmove.c and bondwalk.c.
- User reported jump still doesn't work even with jumpheight set to 20.
- **Status**: Waiting for log output from user's next test session.

### Pending Work (Updated)

#### TODO-1: Investigate SDL2/zlib Static Linking
- Still pending. User explicitly wants no DLL dependencies.

#### TODO-4: Verify Model Files for Mod Characters
- Need to verify that BODY_TESTCHR (FILE_CTESTCHR=0x0055) and BODY_PRESIDENT_CLONE (Skedar) have valid model data in the game files or mod pack.
- Check if the mod's modconfig.txt provides these files, or if they're in the base game data.

---

## Session: 2026-03-17 — Phase D3a: Mod Manager Foundation

### Status: IN PROGRESS

### Design Decisions
- **Menu system**: Extend PD's existing menu framework (no Dear ImGui/SDL overlay)
- **Network sync**: Auto-download missing mods from server
- **Mod toggle**: Persist + hot-toggle (enable/disable at runtime, soft reload to title)
- **Legacy mods**: Convert all 5 to standard mod.json format (toggleable, not hardcoded)
- **Unified lobby**: Multiplayer lobby controlled by party leader (not necessarily the host). Party leader's mod list = session's required mod list.

### Completed Work

#### D3a-1: modmgr.h — Mod Manager API
- `modinfo_t` struct with id, name, version, author, description, dirpath, contenthash, enabled/loaded/bundled flags, content counts
- Full API: init/shutdown/reload lifecycle, registry queries, enable/disable, config persistence, network manifest, filesystem integration
- **File**: `port/include/modmgr.h`

#### D3a-2: modmgr.c — Core Implementation (~600 LOC)
- **Minimal JSON parser**: Custom read-only parser for mod.json (objects, arrays, strings, numbers, bools). ~180 lines. Handles our schema without external dependencies.
- **Directory scanner**: `modmgrScanDirectory()` enumerates `mods/` subdirectories, checks for mod.json or modconfig.txt
- **mod.json parser**: `modmgrParseModJson()` extracts id, name, version, author, description, content counts
- **Legacy fallback**: `modmgrCreateLegacyManifest()` generates synthetic manifest from directory name for mods with only modconfig.txt
- **Bundled detection**: Auto-detects the 5 shipped mods by ID
- **Config persistence**: `PD_CONSTRUCTOR` registers `Mods.EnabledMods` in pd.ini. Comma-separated mod ID list. Parsed on load, built on save.
- **Mod loading**: Calls existing `modConfigLoad()` for each enabled mod's modconfig.txt
- **Filesystem integration**: `modmgrResolvePath()` iterates enabled mods in load order to find files
- **Network manifest**: Serialize/deserialize enabled mod lists with content hashes for compatibility checking
- **Registry sort**: Bundled mods first, then alphabetical
- **File**: `port/src/modmgr.c`

#### D3a-3: mod.json Manifests for Existing Mods
- `build/mods/mod_allinone/mod.json` — Perfect Dark Plus & All Solos in Multi v13
- `build/mods/mod_gex/mod.json` — GoldenEye X Multiplayer v10
- `build/mods/mod_kakariko/mod.json` — Kakariko Village v4
- `build/mods/mod_dark_noon/mod.json` — Dark Noon v3
- `build/mods/mod_goldfinger_64/mod.json` — Goldfinger 64 v1

#### D3a-4: main.c Integration
- Added `#include "modmgr.h"`
- Replaced single `modConfigLoad(MOD_CONFIG_FNAME)` with `modmgrInit()` call
- Legacy fallback: if modmgr finds 0 mods, falls back to old single-mod path
- Added `modmgrShutdown()` to cleanup

#### Full Plan Document
- Comprehensive implementation plan at `docs/MOD_LOADER_PLAN.md`
- Covers sub-phases D3a through D3g (foundation, dynamic tables, fs refactor, menu, hot-toggle, network sync, cleanup)
- ~1900 LOC estimated across all sub-phases

### D3b: Dynamic Asset Tables (DONE — 2026-03-17)
- **Shadow arrays** added to `modmgr.c`: `g_ModBodies[64]`, `g_ModHeads[64]`, `g_ModArenas[64]` with counts
- **Accessor functions** in `modmgr.c/h`: `modmgrGetBody(index)`, `modmgrGetHead(index)`, `modmgrGetArena(index)` — transparently handle base array (index < base count) vs mod shadow array (index >= base count) with safe fallback
- **Total count functions**: `modmgrGetTotalBodies()`, `modmgrGetTotalHeads()`, `modmgrGetTotalArenas()` return base + mod counts
- **mplayer.c**: All body/head accessor functions converted — `mpGetNumBodies()`, `mpGetNumHeads()`, `mpGetNumHeads2()`, `mpGetHeadId()`, `mpGetHeadRequiredFeature()`, `mpGetBodyId()`, `mpGetBodyName()`, `mpGetBodyRequiredFeature()`, `mpGetMpbodynumByBodynum()`, `mpGetMpheadnumByMpbodynum()`, random body/head selection
- **setup.c**: All arena accesses converted — `mpGetNumStages()`, `mpChooseRandomStage()`, `mpChooseRandomMultiStage()`, `mpChooseRandomSoloStage()`, `mpChooseRandomGexStage()`, `mpArenaMenuHandler()` (all 6 MENUOP cases), `mpLoadSetupFileOverview()`, `mpMenuTextArenaName()`, `mpBodyHasIntegratedHead()`
- **challenge.c**: All 6 direct array accesses converted — arena stage lookup, simulant body/head feature checks, bot config body/head feature checks
- **Note**: Dr. Carroll sentinel (`ARRAYCOUNT(g_MpBodies) + 1`) in `mpGetBodyId`/`mpGetMpbodynumByBodynum` left using base array count intentionally — needs proper sentinel design in future cleanup
- **No remaining direct `g_MpBodies[i]`, `g_MpHeads[i]`, or `g_MpArenas[i]` index accesses** outside of data definitions and accessor implementations

### D3c: fs.c Refactor (DONE — 2026-03-17)
- **`fsFullPath()` refactored**: Replaced hardcoded per-mod if/else chain (`g_ModNum == MOD_GEX`, etc.) with single call to `modmgrResolvePath(relPath)` which iterates all enabled mods in load order
- **Removed**: 4 per-mod directory statics (`gexModDir`, `kakarikoModDir`, `darknoonModDir`, `goldfinger64ModDir`)
- **Removed**: 4 per-mod CLI arg blocks (`--gexmoddir`, `--kakarikomoddir`, `--darknoonmoddir`, `--goldfinger64moddir`) from `fsInit()`
- **Removed**: Per-mod directory log lines (now logged by `modmgrInit()`)
- **`fsGetModDir()` simplified**: Now iterates modmgr registry for first enabled mod's dirpath, falls back to legacy `modDir`
- **Preserved**: `modDir` static (for `$M` path expansion and `--moddir` CLI arg), `g_ModNum` global (still used by romdata.c, mplayer.c, propobj.c, menutick.c — those will be addressed later)
- **No circular dependency**: `modmgrResolvePath` passes absolute paths to `fsFileSize`, which bypasses `fsFullPath`'s mod resolution on absolute paths
- **Multi-mod support**: File resolution now checks ALL enabled mods in registry order (first match wins), not just one selected mod

### D3d: Menu System Modernization + Mod Manager Menu (RENDERING VERIFIED)

**Design decisions made (2026-03-17):**
- **Rendering**: Dear ImGui (SDL2 + OpenGL3 backends) replaces PD's N64-era menu renderer
- **Authoring**: Fluent builder API (`dynmenu`) for procedural menu construction
- **Scope**: Full replacement — all menus will be converted to ImGui, with layouts reworked during migration
- **Style**: Custom PD-themed ImGui style (dark + blue/cyan accent, semi-transparent panels)
- **Mod menu**: List + toggle MVP first (checkboxes, apply button), accessible from Options
- **3D previews**: Render PD models to OpenGL FBO, display as ImGui image widget
- **Future screens planned**: Post-game scorecard with rich stats, character browser with carousel + 3D preview, achievement gallery, custom simulant builder, unified multiplayer lobby

**Integration architecture:**
- ImGui renders as 2D overlay AFTER PD's GBI scene rendering, BEFORE buffer swap
- SDL2 events forwarded to ImGui via `ImGui_ImplSDL2_ProcessEvent()` in `gfx_sdl2.cpp`
- When ImGui wants input focus, PD's input is suppressed
- Both menu systems coexist during incremental migration

**Rendering stack**: OpenGL (2.1+ compat, 3.x preferred) via fast3d GBI translator, SDL2 window (`gfx_sdl2.cpp`), `videoGetWindowHandle()` returns `SDL_Window*`

**New files planned**: `port/src/pdgui.c`, `port/include/pdgui.h`, `port/src/dynmenu.c`, `port/include/dynmenu.h`, `port/src/modmenu.c`, `port/src/pdgui_style.c`, `port/fast3d/imgui/` (vendored)

**Detailed plan**: See `docs/MOD_LOADER_PLAN.md` Sub-Phase D3.4

**Implementation (2026-03-17):**
- **GitHub repo created**: https://github.com/MikeHazeJr/perfect-dark-2 (public, initial commit pushed)
- **Dear ImGui v1.91.8 vendored** into `port/fast3d/imgui/` (core + SDL2 + OpenGL3 backends + LICENSE)
- **pdgui_backend.cpp** created in `port/fast3d/` — C++ implementation with `extern "C"` linkage:
  - `pdguiInit()` — creates ImGui context, initializes SDL2+OpenGL3 backends with PD-themed style
  - `pdguiNewFrame()` / `pdguiRender()` — frame lifecycle, renders demo window as placeholder
  - `pdguiProcessEvent()` — forwards SDL events to ImGui, suppresses PD input when overlay captures
  - `pdguiWantsInput()` / `pdguiIsActive()` / `pdguiToggle()` — state queries and F12 toggle
- **pdgui.h** created in `port/include/` — public C header using `s32` types (not stdbool.h)
- **gfx_sdl2.cpp** modified — event loop calls `pdguiProcessEvent()` before PD's handler
- **gfx_pc.cpp** modified — ImGui renders inside `gfx_run()` after `rapi->end_frame()` and before `swap_buffers_begin()` (NOT in videoEndFrame — the buffer swap happens inside gfx_run)
- **gfx_opengl.cpp** modified — `gfx_opengl_reset_for_overlay()` resets GL state (framebuffer, viewport, scissor, depth, cull, stencil, blend, active texture) between PD's scene and ImGui. Does NOT unbind PD's VAO/VBO/program (ImGui's backend saves and restores these).
- **main.c** modified — `pdguiInit(videoGetWindowHandle())` after `videoInit()`, `pdguiShutdown()` in cleanup
- **CMakeLists.txt** modified — added `port/fast3d/imgui` to include paths; GLOB_RECURSE auto-discovers `.cpp` files
- **imgui_impl_opengl3.cpp** modified — replaced ImGui's built-in imgl3w loader with glad (`#include "../glad/glad.h"`) to use the same GL loader as the rest of the port
- **GLSL version**: `#version 130` (GLSL 1.30 / OpenGL 3.0) — matches port's default GL 3.0 compat context
- **GL context**: 3.0 compatibility profile (default fallback in gfx_sdl2.cpp)
- **Style**: PD-themed dark + blue/cyan accent, semi-transparent panels, rounded corners
- **F12 toggle**: Handled in `pdguiProcessEvent()` which consumes the event so PD never sees F12
- **Input isolation**: When overlay is active, ALL keyboard events are consumed (prevents game actions while using ImGui)
- **Rendering verified**: 2026-03-17 — fullscreen overlay + positioned windows render correctly over game content at 60fps

**Key architecture lesson**: ImGui must render inside `gfx_run()` between `rapi->end_frame()` and `wapi->swap_buffers_begin()` because `SDL_GL_SwapWindow` is called by `swap_buffers_begin`, not by `gfx_end_frame`. CMake's `GLOB_RECURSE` may not detect changes to vendored files — always do a clean rebuild (`rm -rf build`) when modifying vendored code.

### Immediate Next Step
- Replace demo window with mod manager screen (modmenu.c)
- Create dynmenu.c builder API and pdgui_style.c as separate files
