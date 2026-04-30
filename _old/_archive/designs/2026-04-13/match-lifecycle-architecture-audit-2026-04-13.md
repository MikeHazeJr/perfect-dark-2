# Match Lifecycle Architecture Audit

> **Date**: 2026-04-13, Session S226
> **Scope**: End-to-end match lifecycle across CombatSim / Co-op / Counter-Op, online + local
> **Status**: Design document -- no code changes
> **Cross-references**:
>   [match-startup-pipeline.md](match-startup-pipeline.md) -- 8-phase startup design (design-stage)
>   [manifest-architecture.md](manifest-architecture.md) -- manifest 3-path system (implemented)
>   [state-transition-audit.md](state-transition-audit.md) -- ImGui menu state gaps (13 GAPs)
>   [mod-enablement-policy.md](mod-enablement-policy.md) -- default-enabled mod policy

---

## 1. Full Lifecycle State Machine

```
                              ┌───────────────────────────────────────────────────────┐
                              │                   MAIN MENU                           │
                              │  (STAGE_TITLE / STAGE_CITRAINING)                     │
                              │  g_NetMode = NETMODE_NONE                             │
                              │  Music: menu track  Input: g_CtxImGuiMenu             │
                              └───────────┬──────────────────────────────┬─────────────┘
                                          │                              │
                              ┌───────────▼───────────┐      ┌──────────▼──────────┐
                              │  HOST / CREATE ROOM    │      │  JOIN / CONNECT      │
                              │  netServerCreate()     │      │  netConnect()        │
                              │  g_NetMode = SERVER    │      │  g_NetMode = CLIENT  │
                              └───────────┬───────────┘      └──────────┬──────────┘
                                          │                              │
                                          ▼                              ▼
                              ┌──────────────────────────────────────────────────────┐
                              │                     ROOM / LOBBY                      │
                              │  CLSTATE_LOBBY   ROOM_STATE_LOBBY                     │
                              │  Music: menu track   Input: g_CtxImGuiMenu            │
                              │  UI: pdgui_menu_room.cpp (tabs: CS/Co-op/Counter-Op)  │
                              │                                                       │
                              │  Configure: stage, bots, weapons, scenario, team,     │
                              │             body/head, difficulty, options             │
                              └───────────┬──────────────────────────────┬─────────────┘
                                          │                              │
                           Leader clicks  │                              │ Solo mode
                           "Start Match"  │                              │ (s_IsSoloMode)
                                          │                              │
              ┌───────────────────────────┼──────────────────────┐       │
              │                           │                      │       │
              ▼                           ▼                      ▼       ▼
    ┌─────────────────┐     ┌──────────────────┐     ┌──────────────────────┐
    │ CombatSim ONLINE│     │ Co-op ONLINE     │     │ CombatSim LOCAL      │
    │                 │     │                  │     │ (also Counter-Op     │
    │ CLC_LOBBY_START │     │ CLC_LOBBY_START  │     │  local via room)     │
    │ → Server builds │     │ → Server builds  │     │                      │
    │   manifest      │     │   MissionConfig  │     │ matchStart()         │
    │ → SVC_MANIFEST  │     │                  │     │ → matchsetup.c:487   │
    │ → Ready Gate    │     │ → SKIP manifest  │     │ → mpStartMatch()     │
    │   (3s countdown)│     │ → SKIP ready gate│     │ → mainChangeToStage  │
    │                 │     │                  │     │                      │
    │ CLSTATE_PREPARING│    │ mainChangeToStage│     │ No network messages  │
    │ ROOM_PREPARING  │     │ netServerCoop-   │     └──────────┬───────────┘
    │                 │     │   StageStart()   │                │
    │ countdown → 0:  │     │                  │                │
    │ mainChangeToStage│    │ SVC_STAGE_START  │                │
    │ netServerStage- │     │ (co-op variant)  │                │
    │   Start()       │     │                  │                │
    │                 │     │                  │                │
    │ SVC_STAGE_START │     │ Client:          │                │
    │ (MP variant)    │     │ titleSetNextStage│                │
    │                 │     │ mainChangeToStage│                │
    │ Client:         │     │ NO mpStartMatch()│                │
    │ mpStartMatch()  │     │                  │                │
    │ CLC_STAGE_READY │     │ NO CLC_STAGE_    │                │
    └────────┬────────┘     │   READY          │                │
             │              └────────┬─────────┘                │
             │                       │                          │
             ▼                       ▼                          ▼
    ┌──────────────────────────────────────────────────────────────────┐
    │                        STAGE LOAD                                │
    │  mainLoop outer iteration (pdmain.c:529-606):                   │
    │    mempResetPool(MEMPOOL_STAGE)                                  │
    │    playermgrReset() → playermgrAllocatePlayers()                 │
    │    mpReset() [sets mplayerisrunning, configures contpads]       │
    │    gfxReset(), joyReset(), dhudReset(), zbufReset()              │
    │    lvReset() [stage assets, cheats, spawns, manifest rescan]    │
    │    viReset()                                                     │
    │                                                                  │
    │  inputCtxShutdown() + inputCtxInit() + push(g_CtxGameplay)      │
    └────────────────────────────────┬─────────────────────────────────┘
                                     │
                                     ▼
    ┌──────────────────────────────────────────────────────────────────┐
    │                      MATCH ACTIVE                                │
    │  CLSTATE_GAME   ROOM_STATE_MATCH                                 │
    │  Input: g_CtxGameplay   Music: stage track                       │
    │  g_Vars.mplayerisrunning = true (MP)                             │
    │                                                                  │
    │  Server: netEndFrame() broadcasts player/bot/prop/NPC state      │
    │  Client: receives + applies, runs local prediction               │
    │  Both: lvTick() → player tick → bot tick → prop tick             │
    │                                                                  │
    │  Match end triggers:                                             │
    │    MP:    score >= limit || time >= limit || team score >= limit  │
    │    Co-op: all objectives complete || all players dead             │
    │    Anti:  objectives complete || anti-player wins                 │
    │                                                                  │
    │  Server only: g_NumReasonsToEndMpMatch > 0 (client forced to 0)  │
    └────────────────────────────────┬─────────────────────────────────┘
                                     │
                      Server calls mainEndStage()
                      Server sends SVC_STAGE_END
                                     │
                                     ▼
    ┌──────────────────────────────────────────────────────────────────┐
    │                       ENDSCREEN                                  │
    │  g_MainIsEndscreen = 1 (double-entry guard)                      │
    │  g_MpSetup.paused = MPPAUSEMODE_GAMEOVER                        │
    │  Music: menu track (musicStartMenu)                              │
    │  Input: g_CtxImGuiMenu pushed                                    │
    │  HUD: still rendering (normmplayerisrunning still true)          │
    │  Timer: still ticking (lvTick still runs)                        │
    │                                                                  │
    │  MP:    mpEndMatch() → pdgui_menu_endscreen (scores, awards)     │
    │  Co-op: endscreenPushCoop() → pdgui_menu_endscreen (objectives)  │
    │  Anti:  endscreenPushAnti() → pdgui_menu_endscreen               │
    │                                                                  │
    │  Client: SVC_STAGE_END → sessionCatalogTeardown()                │
    │          CLSTATE_LOBBY restored                                   │
    │          g_ClientManifest NOT cleared (latent hazard)             │
    │                                                                  │
    │  User options:                                                   │
    │    [Rematch]       → pdguiEndscreenStartMission() or             │
    │                      pdguiSoloRoomReturn() (solo)                │
    │    [Return to Room]→ pdguiSetInRoom(1) (networked)               │
    │    [Quit to Menu]  → pdguiEndscreenExitToMainMenu()              │
    │    [Disconnect]    → netDisconnect() (hard exit)                 │
    └────────────────────────────────┬─────────────────────────────────┘
                                     │
                              ┌──────┴──────┐
                              │             │
                              ▼             ▼
              ┌───────────────────┐  ┌──────────────────┐
              │  RETURN TO ROOM   │  │  QUIT TO MENU    │
              │  (networked)      │  │                  │
              │                   │  │  mainChangeToStage│
              │  Preserves match  │  │  (STAGE_CITRAINING)
              │  config for       │  │                  │
              │  rematch          │  │  Full teardown:  │
              │                   │  │  lvStop()        │
              │  pdguiSetInRoom(1)│  │  playermgrReset()│
              │  Input: ctxImGui  │  │  inputCtxShutdown│
              │  Music: continues │  │  musicReset()    │
              │  menu track       │  │  manifestClear() │
              │                   │  │                  │
              │  ──► ROOM/LOBBY   │  │  ──► MAIN MENU   │
              └───────────────────┘  └──────────────────┘
```

---

## 2. Per-Mode Audit

### 2.1 CombatSim (NETGAMEMODE_MP)

#### 2.1.1 Online Path

| Step | Server | Client | File:Line |
|------|--------|--------|-----------|
| Start | receives CLC_LOBBY_START | sends CLC_LOBBY_START via `netLobbyRequestStartWithSims()` | netmsg.c:4176 / pdgui_bridge.c:846 |
| Manifest | `manifestBuild()` from room state + match config | receives SVC_MATCH_MANIFEST, runs `manifestCheck()` | netmanifest.c:423 / netmsg.c:5038 |
| Ready Gate | `readyGateTickCountdown()` -- 3s countdown | transitions to CLSTATE_PREPARING | netmsg.c:4084 / netmsg.c:4596 |
| Stage Start | `netServerStageStart()` sends SVC_STAGE_START | `netmsgSvcStageStartRead()` → `mpStartMatch()` → `mainChangeToStage()` | net.c:643 / netmsg.c:895 |
| Stage Load | `mainLoop` outer iteration: full reset sequence | Same | pdmain.c:529-606 |
| First Tick | `lvTick()` | `lvTick()` + sends CLC_STAGE_READY | lv.c:2247 / netmsg.c:1307 |
| Match End | `mainEndStage()` → `mpEndMatch()` → `netServerStageEnd()` → SVC_STAGE_END | receives SVC_STAGE_END → `mainEndStage()` | pdmain.c:698 / netmsg.c:1370 |
| Endscreen | func0f0f820c pushes endscreen menu | Same flow | mplayer.c:2842 |
| Return | player remains connected, CLSTATE_LOBBY | CLSTATE_LOBBY restored | net.c:838 / netmsg.c:1375 |

**Notable**: CombatSim online is the ONLY path that goes through the Ready Gate and manifest verification. All other paths skip these.

#### 2.1.2 Local/Solo Path

| Step | Action | File:Line |
|------|--------|-----------|
| Start | `pdguiSoloRoomClose()` → `matchStart()` | pdgui_menu_room.cpp:2263 / matchsetup.c:487 |
| Config | Resolves catalog IDs, builds chrslots, configures bots | matchsetup.c:502-686 |
| Match Init | `mpStartMatch()` → `mainChangeToStage()` | mplayer.c:202 / mplayer.c:548 |
| Stage Load | Same mainLoop outer iteration | pdmain.c:529-606 |
| Match End | `mainEndStage()` → `mpEndMatch()` (no network) | pdmain.c:698 |
| Return | `pdguiSoloRoomReturn()` — preserves match config for rematch | pdgui_lobby.cpp:176 |

**Notable**: Local path calls `matchStart()` directly, bypassing all network plumbing. No manifest, no ready gate, no CLC/SVC messages. The `matchStart()` function (matchsetup.c:487) is the unified config-resolver that both local and server paths use, but the server path splits it across CLC_LOBBY_START handler and `mpStartMatch()`.

### 2.2 Co-op (NETGAMEMODE_COOP)

#### 2.2.1 Online Path (only variant -- no local co-op)

| Step | Server | Client | File:Line |
|------|--------|--------|-----------|
| Start | receives CLC_LOBBY_START (GAMEMODE_COOP) | sends via `netLobbyRequestStart()` | netmsg.c:4633 / pdgui_bridge.c:875 |
| Config | Sets `g_MissionConfig` (stagenum, stage_id, difficulty, iscoop=true) | — | netmsg.c:4633-4641 |
| **NO Manifest** | **Skipped entirely** | **No SVC_MATCH_MANIFEST sent** | — |
| **NO Ready Gate** | **Skipped entirely** | **No CLSTATE_PREPARING** | — |
| Stage Start | `netServerCoopStageStart()` → SVC_STAGE_START | Receives: sets bond/coop playernums, `mainChangeToStage()` | net.c:743 / netmsg.c:1143 |
| **NO mpStartMatch()** | Calls `mpStartMatch()` only if dedicated server | **Client does NOT call mpStartMatch()** | net.c:723 / netmsg.c:1143-1178 |
| Stage Load | Same mainLoop outer iteration | Same | pdmain.c:529-606 |
| Match End | `mainEndStage()` → `endscreenPushCoop()` → SVC_STAGE_END | SVC_STAGE_END → `objectivesDisableChecking()` → `mainEndStage()` | pdmain.c:710-717 / netmsg.c:1364 |

**GAP: Co-op bypasses manifest verification.** If the co-op host uses a mod stage or mod characters, clients receive no manifest check and no asset transfer opportunity. Missing assets cause white textures or crashes.

**GAP: Co-op client skips mpStartMatch().** This means chrslots/participant configuration, weapon set randomization, handicap validation, and feature unlock checks are NOT run on the co-op client. The server handles these partially via `netServerCoopStageStart()`, but the client relies entirely on the SVC_STAGE_START payload.

#### 2.2.2 Local Path

**Does not exist.** Co-op requires networking (`g_NetMode != NETMODE_NONE`). The room UI Tab 1 (Campaign) always sends `netLobbyRequestStart()`. There is no `s_IsSoloMode` path for co-op.

### 2.3 Counter-Op (NETGAMEMODE_ANTI)

#### 2.3.1 Online Path

**Identical to Co-op** except:
- `g_MissionConfig.isanti = true` instead of `iscoop` (net.c:758-759)
- `g_Vars.antiplayernum = 1` instead of `coopplayernum` (net.c:767-768)
- `mode == NETGAMEMODE_ANTI` in client handler (netmsg.c:1149)
- Endscreen uses `endscreenPushAnti()` instead of `endscreenPushCoop()` (pdmain.c:724)

**Same gaps as Co-op**: no manifest, no ready gate, no mpStartMatch on client.

#### 2.3.2 Local Path

**Does not exist.** Counter-Op also requires networking. The original N64 game did support single-player Counter-Op (player controls an enemy AI character), but the PC port requires a network connection.

---

## 3. State Sync Audit During Match

### 3.1 Sync Model Summary

| State | Sync Model | Primary Message | Resync | Risk |
|-------|-----------|----------------|--------|------|
| **Player position** | Server-broadcast, client-predicted | SVC_PLAYER_MOVE (60Hz) | SVC_CHR_RESYNC | LOW -- well-tested |
| **Player stats** (HP/shield/weapon/ammo) | Server-authoritative | SVC_PLAYER_STATS (on damage/death) | None | LOW |
| **Scores** (kills/deaths/points) | Event-driven + resync | SVC_PLAYER_STATS triggers local mutation | SVC_PLAYER_SCORES (flag-gated) | **MEDIUM** -- see §3.2 |
| **Match timer** | Config-seeded, locally ticked | timelimit in SVC_STAGE_START | **None** | **MEDIUM** -- see §3.3 |
| **Match end** | Server-authoritative | SVC_STAGE_END | Client zeroes g_NumReasonsToEndMpMatch | LOW |
| **RNG** | Seeded once at start | Seeds in SVC_STAGE_START | **None post-seed** | **LOW-MEDIUM** -- see §3.4 |
| **Bot AI** | Delegated authority | SVC_BOT_AUTHORITY + CLC_BOT_MOVE | SVC_CHR_SYNC checksums | LOW |
| **Bot state** (HP/weapon/respawn) | Server-authoritative | SVC_CHR_STATE (4Hz) | SVC_CHR_RESYNC | LOW |
| **NPC state** (co-op) | Server-authoritative | SVC_NPC_MOVE (20Hz) / SVC_NPC_STATE (2Hz) | SVC_NPC_RESYNC | LOW |
| **Objectives** (co-op) | Server-authoritative | SVC_OBJ_STATUS | Bundled with NPC resync | LOW |
| **Stage flags** (co-op) | Server-authoritative | SVC_STAGE_FLAG | Bundled with NPC resync | LOW |
| **Alarm** (co-op) | Server-authoritative | SVC_ALARM | None | LOW |
| **Props** (doors/lifts/pickups) | Server-authoritative | SVC_PROP_DOOR/LIFT/SPAWN/DAMAGE | SVC_PROP_RESYNC | LOW |
| **Team scores** | Derived from individual scores | No direct message | Via SVC_PLAYER_SCORES | **MEDIUM** |
| **HUD** (score panel, timer, killfeed) | Derived from local state | No direct messages | N/A -- purely render-side | LOW |

### 3.2 Score Sync: Event-Driven, Not Periodic

Scores mutate via `mpstatsRecordKill()` / `mpstatsRecordDeath()` which run on both server and client when `playerDieByShooter()` fires from `SVC_PLAYER_STATS`. Both sides independently increment kill counts.

**Risk**: If a `SVC_PLAYER_STATS` death event is lost (unreliable channel) or arrives out of order, client and server scores diverge. The `SVC_PLAYER_SCORES` resync corrects this, but it only fires on `NET_RESYNC_FLAG_SCORES`, which is NOT set at match start (net.c:738 sets CHR + PROP flags only). It fires on reconnect and manual resync requests.

**Gap**: No periodic score broadcast. A lost death event causes permanent score divergence until reconnect. The HUD shows stale scores on the affected client.

### 3.3 Match Timer: No Runtime Sync

Timer starts implicitly aligned via `SVC_STAGE_START` (same g_NetTick). After that, `g_StageTimeElapsed60` increments locally by `g_Vars.lvupdate60` each frame (lv.c:2524).

**Risk**: Frame rate differences cause timer drift. A client running at 59fps accumulates ~1 second less per minute than the server at 60fps. Over a 10-minute match, this is ~10 seconds of drift.

**Gap**: No `SVC_TIMER_SYNC` message. The server-authoritative match-end guard (clients can't trigger match end) prevents gameplay impact, but the HUD timer display can show different remaining times on different machines.

### 3.4 RNG: Seeded Once, Drifts

`g_RngSeed` and `g_Rng2Seed` are seeded once from `SVC_STAGE_START` (netmsg.c:713-714/896-898). After that, both machines consume RNG independently.

**Risk**: If any code path consumes RNG calls on one machine but not the other (e.g., a client-only visual effect, a server-only AI decision), the RNG sequences diverge permanently. This affects weapon pickup randomization, bot behavior on the authority client, and random spawn selection.

**Mitigation**: Most gameplay-critical RNG consumption is server-authoritative (bot AI, damage). Client-side RNG is used for visual effects (particles, debris) where divergence is invisible.

---

## 4. Reinit-on-Return Audit

### 4.1 Subsystem Reset Matrix

| Subsystem | Setup (entering match) | Teardown (leaving match) | Symmetric? | Leak Risk |
|-----------|----------------------|------------------------|-----------|-----------|
| **Music** | `musicReset()` in lvReset (lv.c:402), `musicSetStageAndStartMusic()` (lv.c:456) | `musicStartMenu()` in mainEndStage (pdmain.c:716/727/733), `musicStop()` in lvStop (lv.c:2676), `musicReset()` in next lvReset | YES | NONE |
| **Menu stack** | `menuReset()` per player (lv.c:582) | `func0f0f8120()` pops all dialogs (mplayer.c:2842), `menuStop()` in lvStop (lv.c:2691) | YES | LOW -- fragile ctx pop paths (see state-transition-audit.md GAP-1/3) |
| **Input context** | `inputCtxShutdown()` + `inputCtxInit()` + push `g_CtxGameplay` (pdmain.c outer loop) | `inputCtxPopDeferred(&g_CtxImGuiMenu)` in endscreen exit, then `inputCtxShutdown()` in next stage load | YES (stage transition is backstop) | LOW -- relies on stage transition cleanup |
| **HUD** | Rendered when `normmplayerisrunning = true` (set in mpReset, mplayer.c:563) | `normmplayerisrunning = false` set in menutick.c:686 or netDisconnect (net.c:981) | YES | **LOW-MEDIUM** -- HUD renders during endscreen (occluded but wasteful) |
| **Network state** | `netServerStageStart()` → CLSTATE_GAME (net.c:680-688) | `netServerStageEnd()` → CLSTATE_LOBBY (net.c:838), SVC_STAGE_END → sessionCatalogTeardown | MOSTLY | **MEDIUM** -- co-op player-netclient linkages persist until playermgrReset |
| **Player data** | `playermgrReset()` + `playermgrAllocatePlayers()` (pdmain.c:543-545) | NO explicit teardown on match end. Players survive for endscreen. Cleaned by next `playermgrReset()` | ASYMMETRIC (by design) | LOW -- endscreen needs player data |
| **Match config** (g_MpSetup) | Set by room UI, NOT reset between matches | `g_MpSetup.paused = GAMEOVER` (mplayer.c:2811). Config preserved for rematch | ASYMMETRIC (by design) | **MEDIUM** -- weapon set mutation via AUTORANDOMWEAPON_END persists |
| **Manifest** | Built + shipped via SVC_MATCH_MANIFEST | sessionCatalogTeardown() in SVC_STAGE_END. g_ClientManifest NOT cleared | ASYMMETRIC | **HIGH** -- stale manifest in g_ClientManifest between matches. See §6 |
| **Timers** | `g_StageTimeElapsed60 = 0` in lvReset (lv.c:381) | Timer keeps ticking during endscreen. Reset by next lvReset | YES (reset covers it) | LOW -- cosmetic only |

### 4.2 Specific Leaks Found

**LEAK-1: g_ClientManifest not cleared on match end**
- `SVC_STAGE_END` handler (netmsg.c:1353-1397) calls `sessionCatalogTeardown()` but does NOT call `manifestClear(&g_ClientManifest)`.
- The manifest persists until the next `SVC_MATCH_MANIFEST` (which clears before deserialize) or a menu-stage transition (pdmain.c:771).
- Latent hazard: if a code path triggers `mainChangeToStage()` for a gameplay stage between matches without a new manifest, the stale match-N manifest gets applied.

**LEAK-2: Distributed mods are effectively permanent**
- Mods received via `netdistrib.c` are extracted to disk and hot-registered in the catalog.
- The `temporary` flag only affects extraction path (`mods/.temp/` vs `mods/`) and crash recovery.
- No automatic cleanup on match end. Distributed mods remain active in catalog for subsequent matches.
- No mechanism to restore prior enabled/disabled state after match end.

**LEAK-3: Co-op player-netclient linkages survive stage end**
- Server-side `netmsgSvcStageEndWrite()` for co-op/anti intentionally keeps `ncl->player` linkages (netmsg.c:1326-1329) for endscreen display.
- These stale linkages persist until `playermgrReset()` runs during the next stage load.
- If any server code between stage end and next stage load dereferences these linkages assuming live player objects, it gets stale data.

**LEAK-4: HUD renders during endscreen**
- `normmplayerisrunning` stays true during endscreen, so `pdguiHudRender()` draws score panel, timer, killfeed over the endscreen.
- Visually occluded by the endscreen's dim backdrop, but it's unnecessary GPU work and could flash during transitions.

**LEAK-5: Weapon set mutation**
- `mpEndMatch()` (mplayer.c:2834-2839) calls `mpApplyWeaponSet()` if `MPOPTION_AUTORANDOMWEAPON_END` is set, mutating the weapon config for the next match.
- This is intentional (randomize weapons between rounds) but it means the weapon config shown in the lobby after match end doesn't match what was set before match start.

---

## 5. Mode Divergence Analysis

### 5.1 Divergences Between CombatSim and Co-op/Counter-Op

| Feature | CombatSim | Co-op / Counter-Op | Should Converge? |
|---------|-----------|-------------------|-----------------|
| Manifest verification | YES (SVC_MATCH_MANIFEST → CLC_MANIFEST_STATUS) | **NO** -- skipped entirely | **YES** -- co-op mod stages will white-texture |
| Ready gate | YES (3s countdown, CLSTATE_PREPARING) | **NO** -- instant start | **YES** -- asset verification needs gate time |
| mpStartMatch() on client | YES (netmsg.c:1288) | **NO** -- bypassed (netmsg.c:1143-1178) | **INVESTIGATE** -- what does co-op lose? |
| CLC_STAGE_READY handshake | YES (netmsg.c:1307-1312) | **NO** | YES -- server needs to know clients are loaded |
| Bot authority delegation | YES (SVC_BOT_AUTHORITY) | N/A (co-op has NPCs, not bots) | N/A |
| Match end flow | mpEndMatch() (scores, awards) | endscreenPushCoop/Anti() (objectives) | Correct divergence |
| Client state on end | Unlinks player-netclient immediately | Keeps linkages for endscreen | Correct divergence |
| Session catalog teardown | YES (both sides) | YES (both sides) | Converged |

### 5.2 Divergences Between Online and Local

| Feature | Online | Local/Solo | Should Converge? |
|---------|--------|-----------|-----------------|
| Entry function | CLC_LOBBY_START → server handler | `matchStart()` directly | Correct divergence |
| Match init | `mpStartMatch()` on both server + client | `mpStartMatch()` once | Correct divergence |
| Manifest | Built + shipped + checked | **None** -- no manifest for local | **INVESTIGATE** -- mod loading for local? |
| Ready gate | YES (online CS only) | NO | Correct for local |
| Match end authority | Server sends SVC_STAGE_END | Local checks conditions directly | Correct divergence |
| Return to room | `pdguiSetInRoom(1)` (stays connected) | `pdguiSoloRoomReturn()` (preserves config) | Correct divergence |

### 5.3 Vibe-Coded Policies

These are behaviors that exist in Mike's intent but are not enforced in code:

1. **"All players see the same catalog of enabled mods during the match"** -- Not enforced. Each client has its own catalog state. Mods distributed via netdistrib arrive but there's no validation that all clients end up with the same set.

2. **"Mod that recipient HAS but has DISABLED should be transiently enabled"** -- No mechanism exists. The distribution system only handles mods the recipient DOESN'T HAVE. It has no awareness of the recipient's enabled/disabled state.

3. **"Prior disabled state restored at match end"** -- No lifecycle hook exists. There is no "save prior state / restore at match end" pattern anywhere in the mod system.

4. **"All cleanup symmetric on match end"** -- Multiple asymmetries documented above. The system relies on "next match overwrites" rather than "current match cleans up."

---

## 6. Catalog/Manifest Lifecycle Across Match

### 6.1 Current Flow

```
LOBBY
  │
  │  Server builds g_ServerManifest from room state
  │  Server broadcasts SVC_MATCH_MANIFEST
  │  Client stores in g_ClientManifest
  │  Client runs manifestCheck() against local catalog
  │  Client sends CLC_MANIFEST_STATUS (READY/NEED/DECLINE)
  │
  │  [Transfer if needed via SVC_DISTRIB_*]
  │
  ▼
STAGE LOAD (mainChangeToStage)
  │
  │  pdmain.c:764-766:
  │    if g_ClientManifest.num_entries > 0:
  │      manifestMPTransition()     ← diffs current vs new, loads delta
  │    else:
  │      manifestSPTransition()     ← SP two-phase path
  │
  │  g_CurrentLoadedManifest = applied manifest
  │
  ▼
MATCH ACTIVE
  │
  │  manifestEnsureLoaded() called on each spawn as safety net
  │  (body.c, setuputils.c)
  │
  ▼
MATCH END (SVC_STAGE_END)
  │
  │  sessionCatalogTeardown()       ← tears down session refs (u16 mappings)
  │  g_ClientManifest: NOT CLEARED  ← *** LEAK ***
  │  g_CurrentLoadedManifest: still populated
  │  Distributed mod files: still on disk
  │  Distributed catalog entries: still registered + enabled
  │
  ▼
RETURN TO LOBBY
  │
  │  Match config preserved for rematch
  │  No manifest rebuild until next CLC_LOBBY_START
  │  No mod cleanup until game restart or manual action
  │
  ▼
NEXT MATCH (CLC_LOBBY_START)
  │
  │  Server builds NEW g_ServerManifest
  │  SVC_MATCH_MANIFEST clears + replaces g_ClientManifest
  │  manifestMPTransition() diffs g_CurrentLoadedManifest vs new
  │    → unloads match-N assets not in match-N+1
  │    → loads match-N+1 assets not already loaded
  │
  ▼
(Cycle repeats)
```

### 6.2 Gaps in Lifecycle

**GAP-A**: No manifest path for Co-op/Counter-Op. These modes go through `netServerCoopStageStart()` which does NOT build or send a manifest. If the co-op host selects a mod mission stage, the client receives no asset verification.

**GAP-B**: No manifest clear between matches. The stale `g_ClientManifest` from match N persists until match N+1's `SVC_MATCH_MANIFEST`. This is functionally safe in the normal flow (the new manifest replaces it), but it's a latent hazard.

**GAP-C**: No diff-based unload between matches. When returning to lobby, `g_CurrentLoadedManifest` still holds match N's assets. The next `manifestMPTransition()` handles the diff correctly, but if the return-to-lobby path loads a menu stage first (which calls `manifestMenuTransition()` to load ALL characters), the diff is against the full character set, not the match set. This is correct behavior but means mod stage assets from match N are only unloaded when the menu transition explicitly removes them.

**GAP-D**: Distributed mods are permanent. Files extracted by netdistrib to `mods/` (permanent) or `mods/.temp/` (session) are never automatically cleaned up between matches. The `temporary` flag only matters for crash recovery on next launch.

---

## 7. Mod Transfer + Transient Enablement Protocol Design

### 7.1 Requirements (from Mike's specification)

1. If host match uses a mod a recipient does NOT have → transfer (existing via netdistrib)
2. If recipient HAS the mod but has it DISABLED → transiently ENABLE for match duration, then RESTORE prior disabled state at Match End
3. If recipient HAS the mod and has it ENABLED → no change
4. All players see the same catalog of enabled mods during the match
5. On abrupt disconnect → restore prior state (no orphaned transient enables)
6. On host changing mod set mid-match → not supported initially (lock mod set at match start)

### 7.2 Protocol Design

#### 7.2.1 New Structures

```c
// In modmgr.h
typedef struct mod_prior_state {
    char mod_id[64];        // catalog ID of the mod
    bool was_enabled;       // state before transient enable
} mod_prior_state_t;

#define MAX_TRANSIENT_MODS 32

typedef struct mod_transient_session {
    mod_prior_state_t mods[MAX_TRANSIENT_MODS];
    int count;
    bool active;            // true between match start and match end
} mod_transient_session_t;

// Global
extern mod_transient_session_t g_ModTransientSession;
```

#### 7.2.2 Wire Format Extension

Extend `SVC_MATCH_MANIFEST` to include a mod requirements list:

```
Appended to existing SVC_MATCH_MANIFEST:
  u8   num_required_mods
  [for each required mod]:
    u8   mod_id_len
    char mod_id[]           // e.g., "mod:dark_noon"
    u8   action_hint        // 0: HAVE_AND_ENABLED (no action)
                            // 1: HAVE_BUT_DISABLED (transient enable)
                            // 2: MISSING (need transfer)
    u32  content_hash       // for transfer verification
```

The server computes `action_hint` per-client by cross-referencing the host's mod set against each client's `CLC_CATALOG_DIFF` response.

#### 7.2.3 Client-Side Flow

```
On SVC_MATCH_MANIFEST receive:
  1. manifestCheck() -- existing asset check
  2. FOR each required mod:
     a. If mod NOT in local catalog → add to missing list (existing NEED_ASSETS flow)
     b. If mod in local catalog but disabled:
        → Save prior state: g_ModTransientSession.mods[n] = {mod_id, was_enabled=false}
        → Call modmgrSetEnabled(mod_id, true)
        → Mark g_ModTransientSession.active = true
     c. If mod in local catalog and enabled → no action
  3. Send CLC_MANIFEST_STATUS as before

On SVC_STAGE_END receive (or netDisconnect):
  1. IF g_ModTransientSession.active:
     FOR each entry in g_ModTransientSession.mods:
        IF !entry.was_enabled:
           modmgrSetEnabled(entry.mod_id, false)
     g_ModTransientSession.active = false
     g_ModTransientSession.count = 0
```

#### 7.2.4 Server-Side Flow

The server needs to know each client's mod state to compute `action_hint`. Extend the existing `SVC_CATALOG_INFO` / `CLC_CATALOG_DIFF` exchange:

```
Current CLC_CATALOG_DIFF reports: [list of missing net_hashes]
Extend to also report: [list of disabled mod IDs that are locally present]

New field in CLC_CATALOG_DIFF:
  u8   num_disabled_mods
  [for each]:
    u8   mod_id_len
    char mod_id[]
```

The server builds the manifest's `action_hint` per-client:
- For each mod in host's match set:
  - If mod appears in client's `num_disabled_mods` list → hint = 1 (HAVE_BUT_DISABLED)
  - If mod appears in client's missing list → hint = 2 (MISSING)
  - Otherwise → hint = 0 (HAVE_AND_ENABLED)

#### 7.2.5 Integration with Server Manifest Model

The manifest already tracks all assets required for the match. Mod requirements are a subset. The `manifestBuild()` function (netmanifest.c:423) already iterates `modmgrGetCount/GetMod` to include enabled mods. The extension adds per-client diff information.

#### 7.2.6 Failure Modes

| Failure | Handling |
|---------|----------|
| Client disconnects mid-match with transient enables | `netDisconnect()` runs restore logic. If crash kills process, `pd.ini` has stale enabled state — crash recovery on next launch should prompt |
| Host changes mod set mid-match | Not supported. Mod set locked at manifest build time |
| Transfer fails for a missing mod | Existing retry logic in netdistrib. Client stays in NEED state. If timeout: kick from room |
| Client declines manifest | DECLINE = spectate. No transient enables applied. No restore needed |
| Server restarts mid-match | All clients disconnect → `netDisconnect()` restore runs on each |

#### 7.2.7 pd.ini Persistence

Transient enables are NOT written to `pd.ini`. The `modmgrSetEnabled()` call mutates the in-memory state only. If the game crashes during a match with transient enables, the crash recovery system detects the `.temp` mods and prompts.

**Alternative (safer)**: Write a `[TransientMods]` section to `pd.ini` on match start, clear it on match end. On startup, if `[TransientMods]` exists, restore prior states and remove the section. This survives crashes without the generic crash recovery prompt.

---

## 8. Cross-Mode Gap Summary

### Gaps Where Modes Diverge Without Reason

| # | Gap | Affected Mode | Severity | Root Cause |
|---|-----|--------------|----------|------------|
| G-1 | No manifest verification | Co-op, Counter-Op | HIGH | `netServerCoopStageStart()` bypasses manifest pipeline entirely |
| G-2 | No ready gate | Co-op, Counter-Op | HIGH | Same: instant start, no CLSTATE_PREPARING |
| G-3 | No CLC_STAGE_READY handshake | Co-op, Counter-Op | MEDIUM | Server doesn't know when co-op clients are loaded |
| G-4 | Co-op client skips mpStartMatch() | Co-op, Counter-Op | MEDIUM | Different SVC_STAGE_START handler path (netmsg.c:1143 vs 1179) |
| G-5 | g_ClientManifest not cleared on match end | All online modes | MEDIUM | Missing `manifestClear()` in SVC_STAGE_END handler |
| G-6 | No transient mod enable/disable | All modes | HIGH | No mechanism exists |
| G-7 | Distributed mods permanent after match | All online modes | MEDIUM | No automatic cleanup; temporary flag is crash-recovery only |
| G-8 | No periodic score broadcast | All online modes | MEDIUM | Scores event-driven; lost events cause permanent divergence |
| G-9 | No timer sync | All online modes | LOW | Timer locally ticked; server-auth match end prevents gameplay impact |
| G-10 | HUD renders under endscreen | All modes | LOW | normmplayerisrunning not cleared during endscreen |
| G-11 | Co-op player-netclient linkage leaks | Co-op, Counter-Op | LOW | Intentional for endscreen display, but stale data risk |

### Gaps Inherited From State Transition Audit

The existing `state-transition-audit.md` documents 13 GAPs in ImGui menu state management. The most relevant to match lifecycle:
- **GAP-1** (HIGH): Endscreen input context push without explicit pop
- **GAP-3** (MEDIUM): Game-over screen context push without explicit pop
- **GAP-13** (MEDIUM): `g_PlayersWithControl` not managed in MP pause

These interact with the match lifecycle because they affect the return-to-room transition's input state.

---

## 9. Recommendation: Targeted Fix Set vs Top-Down Rework

### Assessment

The match lifecycle has two architecturally distinct halves:

1. **CombatSim online** — has a manifest pipeline, ready gate, stage-ready handshake, and proper session catalog lifecycle. This is ~80% correct. The gaps are incremental (score broadcast, timer sync, manifest clear on match end).

2. **Co-op/Counter-Op online** — bypasses the entire manifest pipeline. This is not a bug in the existing pipeline; it's a **missing integration**. The co-op path was built before the manifest system existed and was never retrofitted.

### Recommendation: TARGETED FIXES with one FOCUSED REWORK

**Do NOT do a full top-down rework.** The CombatSim pipeline is sound. The Co-op/Counter-Op paths need to be PLUMBED INTO the existing pipeline, not rebuilt from scratch.

**Layered approach** (per `feedback_layered_design.md`):

**Layer 1 (safety -- no protocol changes):**
- Clear `g_ClientManifest` in `SVC_STAGE_END` handler
- Add periodic score broadcast (every 300 frames = 5 seconds)
- Clear `normmplayerisrunning` during endscreen (or gate HUD on `!g_MainIsEndscreen`)
- Clear co-op player-netclient linkages at endscreen exit (not at stage end)

**Layer 2 (co-op manifest integration -- protocol bump required):**
- Route Co-op/Counter-Op through the manifest pipeline
- `netServerCoopStageStart()` → call `manifestBuild()` → send `SVC_MATCH_MANIFEST` → ready gate → then `SVC_STAGE_START`
- Co-op client receives manifest, runs check, sends status
- Add `CLC_STAGE_READY` to co-op client path

**Layer 3 (transient mod enablement -- new feature):**
- Implement `mod_transient_session_t` with save/restore
- Extend `CLC_CATALOG_DIFF` with disabled mod list
- Add `action_hint` to `SVC_MATCH_MANIFEST` mod entries
- Add restore hooks in `SVC_STAGE_END` handler and `netDisconnect()`

**Layer 4 (polish):**
- Timer sync message (every 600 frames = 10 seconds)
- Automatic `.temp` mod cleanup on match end
- `pd.ini` `[TransientMods]` crash-safe persistence

Each layer is independently shippable and testable. Layer 1 can ship immediately. Layer 2 requires a protocol bump. Layer 3 requires Layer 2. Layer 4 is polish.
