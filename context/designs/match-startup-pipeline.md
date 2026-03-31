# Match Startup Pipeline — Unified Design

**Status**: Design — awaiting game director approval
**Date**: 2026-03-30
**Author**: Session S84 (audit + synthesis)

**Unifies**: B-12 Phase 3, R-2/R-3, J-3, C-5/C-6, D3R-9 distribution
**Dependencies**: Participant pool (B-12 Phase 2 — done), Asset Catalog (C-0 through C-4 — done), PDCA distribution (D3R-9 — done), Room structs (R-1 — done)

**Cross-references**:
  [room-architecture-plan.md](../room-architecture-plan.md) — room lifecycle, state machine, message IDs
  [component-mod-architecture.md](../component-mod-architecture.md) — asset catalog, component filesystem
  [b12-participant-system.md](../b12-participant-system.md) — participant pool, chrslots removal
  [join-flow-plan.md](../join-flow-plan.md) — J-series task definitions
  [nat-traversal-architecture.md](../nat-traversal-architecture.md) — connection establishment

---

## 1. Problem Statement

The current match startup is fire-and-forget: host clicks "Start Match" → server immediately broadcasts `SVC_STAGE_START` → clients load whatever they can. No handshake, no asset verification, no ready gate. This causes:

- **White textures / crashes** when clients lack models the server expects (B-54, B-55)
- **Character mismatch** when server and client resolve body/head indices differently
- **Missing mod content** when a player joins mid-lobby without the host's mods
- **No graceful degradation** — problems manifest as hard crashes, not user-facing errors

Every major multiplayer game solves this with a **match startup pipeline**: a negotiated, phased handshake between host and clients that ensures everyone has what they need before gameplay begins.

**Goal**: Replace the current instant-start with a standard lobby → manifest → check → transfer → ready → load → sync pipeline that uses the existing catalog and distribution systems.

**Non-goals**: No peer-to-peer asset sharing (server is always the source). No streaming assets mid-match. No change to the in-match netcode (SVC_PLAYER_MOVE etc.).

---

## 2. Architecture Overview

```
HOST PRESSES START MATCH
         │
         ▼
┌─────────────────────────────┐
│  Phase 1: GATHER            │  Server collects participant info
│  Server reads room state:   │  from all clients in the room.
│  - Each client's body/head  │
│  - Bot configs from leader  │  Duration: instant (data already
│  - Stage + scenario + opts  │  in netclient.settings + matchconfig)
└────────────┬────────────────┘
             │
             ▼
┌─────────────────────────────┐
│  Phase 2: MANIFEST          │  Server builds the union of all
│  For each participant:      │  required assets and sends it to
│  - Catalog body entry       │  every client in the room.
│  - Catalog head entry       │
│  For match:                 │  ──► SVC_MATCH_MANIFEST
│  - Stage assets             │      (reliable, to room members)
│  - Weapon set assets        │
│  - Mod components (hashes)  │
└────────────┬────────────────┘
             │
             ▼
┌─────────────────────────────┐
│  Phase 3: CHECK             │  Each client checks local catalog
│  Client receives manifest   │  against manifest. Reports status.
│  For each entry:            │
│  - catalogLookup(hash)      │  ──► CLC_MANIFEST_STATUS
│  - present? → OK            │      READY | NEED_ASSETS | DECLINE
│  - missing? → add to list   │
│                             │
│  If all present → READY     │  Client may DECLINE (spectate
│  If missing → NEED_ASSETS   │  from lobby instead of download)
└────────────┬────────────────┘
             │
             ▼
┌─────────────────────────────┐
│  Phase 4: TRANSFER          │  Server distributes missing assets
│  Uses existing PDCA system: │  to clients that reported NEED.
│  - SVC_DISTRIB_BEGIN        │
│  - SVC_DISTRIB_CHUNK (16KB) │  Skipped entirely if all clients
│  - SVC_DISTRIB_END          │  reported READY.
│                             │
│  Client installs via        │  Duration: 0s (all have assets)
│  assetCatalogRegister()     │  to ~30s (large mod transfer)
│  with temporary=1           │
│                             │
│  On complete:               │
│  ──► CLC_MANIFEST_STATUS    │
│      READY                  │
└────────────┬────────────────┘
             │
             ▼
┌─────────────────────────────┐
│  Phase 5: READY GATE        │  Server waits for ALL room members
│  Track per-client:          │  to report READY (or DECLINE).
│  - ready_count / total      │
│                             │  Timeout: 60s → auto-kick clients
│  Broadcast progress:        │  that haven't responded.
│  ──► SVC_MATCH_COUNTDOWN    │
│      "4/5 ready" updates    │  Clients that DECLINE stay in
│                             │  CLSTATE_LOBBY as spectators.
└────────────┬────────────────┘
             │
             ▼
┌─────────────────────────────┐
│  Phase 6: LOAD              │  Server sends SVC_STAGE_START
│  Same as today, but now:    │  (existing message, enhanced).
│  - Uses participant pool    │
│    (B-12 Phase 3 format)    │  All clients load in parallel.
│  - No chrslots bitmask      │  Server waits for all to reach
│  - Each slot has catalog    │  CLSTATE_GAME.
│    body/head hash, not idx  │
│                             │  Room transitions:
│  catalogGetSafeBodyPaired() │  LOBBY → LOADING → MATCH
│  is now a safety net, not   │
│  the primary resolution     │
└────────────┬────────────────┘
             │
             ▼
┌─────────────────────────────┐
│  Phase 7: SYNC              │  First game tick. All participants
│  Existing netcode takes     │  are spawned with validated models.
│  over:                      │
│  - SVC_PLAYER_MOVE          │  No white textures. No crashes.
│  - SVC_PROP_SPAWN           │  No model mismatches.
│  - SVC_CHR_SPAWN            │
└─────────────────────────────┘
```

---

## 3. New Network Messages

All new messages use the existing `netmsg.c` read/write pattern with `netbufWrite*` / `netbufRead*`.

### 3.1 SVC_MATCH_MANIFEST (Server → Room Clients)

Sent after host presses Start Match. Contains the complete asset requirements for the match.

```
Opcode: 0x62 (next available SVC in the 0x60 lobby range)
Channel: NETCHAN_CONTROL (reliable, ordered)

Wire format:
  u8   opcode          = 0x62
  u32  manifest_hash   = FNV-1a of the full manifest (for quick compare)
  u8   stage_id_len
  char stage_id[]      = catalog string ID of the stage (e.g., "felicity")
  u8   scenario        = scenario type
  u8   num_participants
  [for each participant]:
    u8   slot_index
    u8   type          = PARTICIPANT_LOCAL/REMOTE/BOT
    u8   team
    u32  body_hash     = asset catalog net_hash for body
    u32  head_hash     = asset catalog net_hash for head
    char body_id[64]   = catalog string ID (for human-readable fallback)
    char head_id[64]   = catalog string ID
  u8   num_weapons
  [for each weapon slot]:
    u8   weapon_num
  u8   num_components  = number of mod components required
  [for each component]:
    u32  component_hash = contenthash from modmgr
    u8   id_len
    char component_id[] = mod ID string (for logging/UI)
```

### 3.2 CLC_MANIFEST_STATUS (Client → Server)

Client's response after checking (and optionally receiving) assets.

```
Opcode: 0x0A (repurpose reserved CLC_ROOM_JOIN — see §5 for renumbering)
Channel: NETCHAN_CONTROL (reliable, ordered)

Wire format:
  u8   opcode          = 0x0A
  u32  manifest_hash   = echo back (server validates match)
  u8   status          = 0: READY, 1: NEED_ASSETS, 2: DECLINE
  u8   num_missing     = count of missing components (0 if READY)
  [for each missing]:
    u32  component_hash
```

### 3.3 SVC_MATCH_COUNTDOWN (Server → Room Clients)

Progress broadcast during ready gate.

```
Opcode: 0x63
Channel: NETCHAN_CONTROL (reliable, ordered)

Wire format:
  u8   opcode          = 0x63
  u8   ready_count
  u8   total_count
  u8   phase           = 0: CHECKING, 1: TRANSFERRING, 2: WAITING, 3: LOADING
  u8   countdown_secs  = 0 = no countdown; >0 = seconds until force-start
```

### 3.4 SVC_STAGE_START v24 (Enhanced)

Replaces the current v23 format. Removes the chrslots bitmask entirely. Uses catalog hashes instead of numeric body/head indices.

```
Existing fields retained:
  u8   opcode
  u8   stagenum
  u32  rng_seed
  u32  options
  u8   scenario, scorelimit, timelimit
  u16  teamscorelimit
  u8   weapons[6]
  s8   weaponSetIndex
  u8   spawnWeaponNum

Replaced section (was: u64 chrslots):
  u8   num_participants
  [for each participant]:
    u8   slot_index
    u8   type           = PARTICIPANT_LOCAL/REMOTE/BOT
    u8   team
    u32  body_hash
    u32  head_hash
    u8   bot_type       = (0 for players)
    u8   bot_difficulty  = (0 for players)
    u8   name_len
    char name[]

Retained:
  checksum at end
```

Protocol version bumps to **24**.

---

## 4. State Machine

Room state during the pipeline:

```
ROOM_STATE_LOBBY
    │
    │  CLC_ROOM_START (leader only)
    ▼
ROOM_STATE_PREPARING  ← NEW STATE
    │
    │  [Phase 1-2: Gather + Manifest]
    │  SVC_MATCH_MANIFEST sent
    │
    │  [Phase 3-4: Check + Transfer]
    │  CLC_MANIFEST_STATUS received
    │  SVC_DISTRIB_* if needed
    │
    │  [Phase 5: Ready gate passed]
    ▼
ROOM_STATE_LOADING    ← existing
    │
    │  SVC_STAGE_START sent
    │  All clients reach CLSTATE_GAME
    ▼
ROOM_STATE_MATCH      ← existing
```

New enum value:
```c
ROOM_STATE_PREPARING = 5  // Between LOBBY and LOADING
```

Client state additions:
```c
CLSTATE_PREPARING = 5     // Received manifest, checking assets
```

---

## 5. Message ID Renumbering

The reserved CLC_ROOM_* range (0x0A–0x0F) needs assignment. Proposed:

```c
// Client → Server
CLC_ROOM_JOIN       = 0x0A  // (R-3, future)
CLC_ROOM_LEAVE      = 0x0B  // (R-3, future)
CLC_ROOM_SETTINGS   = 0x0C  // (R-3, future)
CLC_ROOM_START      = 0x0D  // replaces CLC_LOBBY_START for room-scoped matches
CLC_MANIFEST_STATUS = 0x0E  // NEW — client manifest response
CLC_ROOM_KICK       = 0x0F  // (R-3, future)

// Server → Client
SVC_MATCH_MANIFEST  = 0x62  // NEW — asset manifest
SVC_MATCH_COUNTDOWN = 0x63  // NEW — ready gate progress
SVC_ROOM_ASSIGN     = 0x64  // (R-3, future)
SVC_ROOM_LIST       = 0x65  // (R-3, future — J-3)
SVC_ROOM_UPDATE     = 0x66  // (R-3, future)
```

---

## 6. Integration with Existing Systems

### 6.1 Asset Catalog

The manifest is built from catalog queries:

```c
// Server-side manifest construction (Phase 2)
for each participant:
    asset_entry_t *body = assetCatalogLookup(participant->body_id);
    asset_entry_t *head = assetCatalogLookup(participant->head_id);
    manifest_add(body->net_hash, body->id);
    manifest_add(head->net_hash, head->id);

asset_entry_t *stage = assetCatalogLookupStage(matchconfig->stagenum);
manifest_add(stage->net_hash, stage->id);
```

Client-side check (Phase 3):
```c
// Client receives SVC_MATCH_MANIFEST
for each entry in manifest:
    asset_entry_t *local = assetCatalogLookupByHash(entry->net_hash);
    if (!local || local->status == MODELSTATUS_INVALID)
        missing_list_add(entry);

if (missing_count == 0)
    send CLC_MANIFEST_STATUS(READY);
else
    send CLC_MANIFEST_STATUS(NEED_ASSETS, missing_list);
```

### 6.2 Model Catalog + catalogGetSafeBodyPaired()

With the manifest pipeline, `catalogGetSafeBodyPaired()` changes role:

- **Before (current)**: Primary resolution. Called blindly during matchStart(). If a model is missing, silently substitutes a random base-game body. No one knows what happened.
- **After (with pipeline)**: Safety net only. The manifest check ensures models exist before load. `catalogGetSafeBodyPaired()` still runs as a defense-in-depth guard, but it should never trigger during normal flow. If it does, log a warning — it means the pipeline missed something.

### 6.3 PDCA Distribution (netdistrib)

Phase 4 reuses the existing distribution system exactly as-is:

- Server calls `netDistribQueueComponent()` for each missing component per client
- Existing `SVC_DISTRIB_BEGIN/CHUNK/END` flow handles transfer
- Client `assetCatalogRegisterComponent(temporary=1)` installs on receipt
- After all components received, client sends `CLC_MANIFEST_STATUS(READY)`

No changes to the distribution protocol. The manifest just tells it *what* to send.

### 6.4 Participant Pool (B-12)

Phase 6 (SVC_STAGE_START v24) completes B-12 Phase 3:

- The chrslots bitmask is removed from the wire format
- Each participant is sent individually with catalog hashes
- `mpParticipantsFromLegacyChrslots()` is retired
- `mpAddParticipantAt()` is called directly from the v24 reader
- `mpParticipantsToLegacyChrslots()` compatibility shim is removed

### 6.5 Room Architecture (R-2/R-3)

The pipeline is room-scoped:

- `CLC_ROOM_START` replaces `CLC_LOBBY_START` (room leader only)
- All pipeline messages are sent to room members, not the whole server
- Room state transitions through PREPARING → LOADING → MATCH
- Other rooms on the same server are unaffected

---

## 6.5 Phase C.5 — Full Game Catalog Registration

**Goal**: Register ALL base game content in the asset catalog, not just the MP subset. Every character model, every mission map, every weapon — the full game library available to the manifest pipeline. This is a catalog/data concern that does not touch the network protocol; it can land any time after Phase C and before Phase F ships.

### 6.5.1 Characters

The original catalog registration covers only the N64 MP character select pool:
- 63 MP bodies (`s_BaseBodies[]` in `port/src/assetcatalog_base.c`)
- 76 MP heads (`s_BaseHeads[]` in `port/src/assetcatalog_base.c`)

**Expansion**: Register all 152 entries in `g_HeadsAndBodies[]`.

SP-only characters get `"base:sp_*"` prefixed catalog IDs:

| Character Group | Example IDs | Available By Default |
|-----------------|-------------|----------------------|
| Common NPCs (guards, scientists, technicians) | `base:sp_guard_datadyne`, `base:sp_scientist` | Yes |
| Named SP characters (Joanna SP skin, etc.) | `base:sp_joanna_sp` | Yes |
| Mr Blonde | `base:sp_mr_blonde` | Locked → beat dataDyne Central: Defection |
| Skedar warriors | `base:sp_skedar_*` | Locked → beat Skedar Ruins |
| Maians (Elvis, etc.) | `base:sp_maian_*` | Locked → beat Area 51: Escape |

Unlock gating uses the existing `MPFEATURE_*` flag system — no new infrastructure needed.

### 6.5.2 Maps

Current registration: 13 NTSC + 14 PAL MP arenas (`s_BaseStages[]`).

**Expansion**: Register ALL playable mission maps as potential arenas:

| Map Group | Notes |
|-----------|-------|
| dataDyne Central: Defection | Corridors + open areas, viable for deathmatch |
| dataDyne Research: Investigation | Large open facility |
| dataDyne Central: Extraction | |
| Carrington Villa: Hostage One | Outdoor + interior mix |
| Chicago: Stealth | Street-level urban |
| G5 Building: Reconnaissance | Office building, good for TDM |
| Area 51: Infiltration / Rescue / Escape | Three distinct maps |
| Carrington Institute: Defense | Home base layout |
| Air Base: Espionage | Runway + hangar |
| Air Force One: Antiterrorism | Aircraft interior |
| Crash Site: Confrontation | |
| Pelagic II: Exploration | Underwater base |
| Deep Sea: Nullify Threat | |
| Carrington Institute: Device Test (co-op) | |
| Chicago: Wave Goodbye (co-op) | |
| Skedar Ruins: Battle Shrine | |
| Mr. Blonde's Revenge (counter-op) | |
| Maian SOS (co-op) | |
| WAR! (counter-op) | |
| The Duel | |

SP maps do **not** have MP spawn pads placed by designers. See §6.5.3.

### 6.5.3 Spawn Point Fallback for SP Maps

SP maps lack the `PROP_PAD_SPAWN` pads that MP maps have. Without a fallback, selecting an SP map in MP would crash or stack all players at world origin.

**Interim (C.5 scope)**: Navmesh random point selection with wall-facing avoidance.
- At match start, if the selected arena has zero `PROP_PAD_SPAWN` entries, call `spawnGetNavmeshPoint(playerIdx, &out)` per player
- Samples N random walkable navmesh nodes (or collision-valid floor positions)
- Filters out points within X units of a wall or ceiling
- Distributes players across the map using spatial hashing to prevent stack spawning
- Orientation: face toward map centroid to avoid spawning directly into walls
- Degrades gracefully if navmesh unavailable (returns stage center with warning)

**Future**: Map editor / forge tool for custom spawn point placement. Placed spawns override the navmesh fallback entirely.

Implementation touch points:
- `src/game/game_mpsetup.c` — `setupDetermineSpawnPads()` or equivalent; check pad count, call fallback if zero
- New `port/src/navspawn.c` — `spawnGetNavmeshPoint(int playerIdx, struct coord3d *out)`, ~100 lines

### 6.5.4 Audio / Textures / Animations

Already globally registered at init:
- 1207 animations
- 3503 textures
- 1545 audio entries

No changes needed for this phase.

### 6.5.5 Implementation Plan

1. **Expand name tables** in `port/src/assetcatalog_base.c`:
   - `s_BaseBodies[]` → all 152 entries; SP entries use `"base:sp_"` prefix
   - `s_BaseHeads[]` → all 152 entries, matching
   - `s_BaseStages[]` → all mission maps appended

2. **Add unlock gating** in `assetcatalog_base.c`:
   - Add a `flags` field to table entry structs
   - `CATALOG_FLAG_LOCKED_SP` — entry hidden until save data unlocks it
   - Query via `MPFEATURE_*` flags at registration time (already used for MP character unlocks)

3. **Navmesh spawn fallback** — new `port/src/navspawn.c`:
   - `spawnGetNavmeshPoint(int playerIdx, struct coord3d *out)`
   - Wired into `setupDetermineSpawnPads()` when pad count == 0

4. **UI exposure** — character select and arena picker:
   - Group roster into categories: MP Characters / SP Characters / Unlockable
   - Arena picker: MP Arenas / Mission Maps (grouped by mission)
   - Locked entries shown grayed with unlock condition tooltip

---

## 7. Implementation Phases

Each phase is independently shippable and testable. No phase breaks what's already working.

### Phase A: Foundation (Protocol + State Machine)
**Scope**: New message types, room state additions, manifest struct
**Files**: `netmsg.h`, `netmsg.c`, `room.h`, `hub.c`, `net.h`
**Test**: Compile + existing match flow still works (backward compat)

- [ ] Define `SVC_MATCH_MANIFEST`, `CLC_MANIFEST_STATUS`, `SVC_MATCH_COUNTDOWN` opcodes
- [ ] Add `ROOM_STATE_PREPARING` and `CLSTATE_PREPARING` enum values
- [ ] Define `match_manifest_t` struct (hash table of required assets)
- [ ] Bump `NET_PROTOCOL_VER` to 24
- [ ] Add manifest read/write functions to `netmsg.c`

### Phase B: Manifest Construction (Server-Side)
**Scope**: Server builds manifest from room state + match config
**Files**: `matchsetup.c`, `net.c`, new `netmanifest.c`
**Test**: Log the manifest contents on match start, verify correctness

- [ ] `manifestBuild(room, matchconfig)` → iterates participants, queries catalog
- [ ] Include stage, weapons, mod components in manifest
- [ ] `manifestComputeHash()` → FNV-1a of all entries
- [ ] Wire into `matchStart()` — build manifest before sending `SVC_STAGE_START`

### Phase C: Client Check (Manifest Receive + Verify)
**Scope**: Client receives manifest, checks local catalog, reports status
**Files**: `netmsg.c` (client handler), new `netmanifest.c` (client-side)
**Test**: Client with all assets reports READY; client missing a mod reports NEED

- [ ] `netmsgSvcMatchManifestRead()` → parse manifest, store locally
- [ ] `manifestCheck()` → iterate entries, query local catalog
- [ ] Send `CLC_MANIFEST_STATUS` with result
- [ ] UI: show "Checking assets..." in room screen during PREPARING state

### Phase C.5: Full Game Catalog Registration
**Scope**: Register all 152 characters and all mission maps in the base catalog; add navmesh spawn fallback for SP maps
**Files**: `port/src/assetcatalog_base.c`, `port/src/navspawn.c` (new), `src/game/game_mpsetup.c`, `port/fast3d/pdgui_menu_matchsetup.cpp`, `port/fast3d/pdgui_menu_agentselect.cpp`
**Test**: All SP characters visible in character select (grouped); all mission maps visible in arena picker; starting a match on an SP map spawns players at valid floor positions
**Dependency**: Can proceed independently after Phase C. Does not block D/E/F.

- [ ] Expand `s_BaseBodies[]` and `s_BaseHeads[]` to all 152 entries with `"base:sp_"` prefix for SP characters
- [ ] Expand `s_BaseStages[]` to include all mission maps
- [ ] Add `flags` field + `CATALOG_FLAG_LOCKED_SP` to table entry structs; gate unlock via `MPFEATURE_*`
- [ ] Implement `spawnGetNavmeshPoint()` in new `port/src/navspawn.c`
- [ ] Wire navmesh fallback in `setupDetermineSpawnPads()` when pad count == 0
- [ ] UI: group character select roster (MP / SP / Unlockable) and arena picker (MP Arenas / Mission Maps)

### Phase D: Transfer Gate
**Scope**: Server queues missing assets for distribution, waits for completion
**Files**: `net.c`, `netdistrib.c`, `hub.c`
**Test**: Client missing a mod component receives it and reports READY

- [ ] Server receives `CLC_MANIFEST_STATUS(NEED_ASSETS)`
- [ ] Queue missing components via existing `netDistribQueueComponent()`
- [ ] Track per-client transfer completion
- [ ] On all transfers complete, client sends `CLC_MANIFEST_STATUS(READY)`

### Phase E: Ready Gate + SVC_STAGE_START v24
**Scope**: Server waits for all READY, then sends enhanced stage start
**Files**: `hub.c`, `netmsg.c`, `matchsetup.c`, `participant.c`
**Test**: Match starts only after all clients ready; participant data correct

- [ ] Ready gate tracker in room struct
- [ ] `SVC_MATCH_COUNTDOWN` broadcast during wait
- [ ] 60s timeout → kick unresponsive clients
- [ ] `SVC_STAGE_START` v24 with participant array (catalog hashes, no chrslots)
- [ ] Client-side: `mpAddParticipantAt()` from v24 data directly
- [ ] Remove `mpParticipantsToLegacyChrslots()` and `mpParticipantsFromLegacyChrslots()`

### Phase F: UI Polish
**Scope**: User-facing feedback during the pipeline
**Files**: `pdgui_menu_room.cpp`, `pdgui_lobby.cpp`, `server_gui.cpp`
**Test**: Visual confirmation of each phase in the UI

- [ ] Room screen shows pipeline state: "Building manifest..." → "Checking..." → "Downloading 2 components..." → "4/5 ready" → "Loading..."
- [ ] Progress bar for asset transfers
- [ ] DECLINE option with "Spectate" button
- [ ] Server GUI shows per-client pipeline status

---

## 8. Backward Compatibility

- Protocol version 24 is a hard break from 23. Clients on v23 cannot join v24 servers and vice versa. This is acceptable for a pre-1.0 project.
- If backward compat is ever needed, the server can detect protocol version during `CLC_AUTH` and fall back to the old instant-start path for v23 clients. Not recommended — just require the update.

---

## 9. Failure Modes

| Failure | Handling |
|---------|----------|
| Client disconnects during PREPARING | Remove from room, rebuild manifest if participant count changed |
| Transfer fails mid-stream | Existing PDCA retry logic. Client stays in NEED state until complete |
| Client never responds to manifest | 60s timeout → kick from room, remaining clients proceed |
| All clients DECLINE | Cancel match, return room to LOBBY |
| catalogGetSafeBodyPaired triggers during load | Log warning — pipeline should have caught this. Investigate as bug |
| Manifest hash mismatch (client echoes wrong hash) | Reject CLC_MANIFEST_STATUS, send fresh SVC_MATCH_MANIFEST |

---

## 10. What This Replaces

| Current (v23) | New (v24) |
|----------------|-----------|
| `CLC_LOBBY_START` → immediate `SVC_STAGE_START` | `CLC_ROOM_START` → manifest → check → transfer → ready → `SVC_STAGE_START` |
| chrslots bitmask (u64, 4 bits per slot) | Participant array with catalog hashes |
| Silent model substitution via catalogGetSafeBodyPaired | Explicit pre-match asset verification |
| No mod verification before match | Full manifest check with distribution |
| No ready gate | All-or-nothing ready gate with timeout |
| Room 0 only, no scoping | Room-scoped pipeline, multi-room capable |
| Only 63 MP bodies + 76 MP heads registered | All 152 `g_HeadsAndBodies[]` entries with SP characters under `"base:sp_*"` IDs |
| Only 13/14 MP arenas available in arena picker | All mission maps available as arenas (navmesh spawn fallback for SP maps) |
| SP maps in MP: crash or no spawn pads | `spawnGetNavmeshPoint()` fallback; future forge tool for custom placement |
| Character unlock gating: MP-only `MPFEATURE_*` checks | Same `MPFEATURE_*` system, extended to SP characters (Mr Blonde, Skedar, Maians) |

---

## 11. Estimated Effort

| Phase | Size | Dependencies |
|-------|------|-------------|
| A: Foundation | Small (message defs, enums) | None |
| B: Manifest Construction | Medium (catalog queries, struct building) | A |
| C: Client Check | Medium (manifest parsing, catalog lookup) | A, B |
| C.5: Full Game Catalog | Medium–Large (table expansion + navmesh fallback + UI grouping) | C (independent of D/E/F) |
| D: Transfer Gate | Small (wiring existing distrib) | C |
| E: Ready Gate + v24 | Medium (state machine, B-12 Phase 3) | D |
| F: UI Polish | Medium (ImGui work) | E |

Total: ~8 focused sessions (A–C done, C.5 adds ~2–2.5 sessions). Each phase produces a working build.

C.5 can run in parallel with D/E planning since it has no protocol dependencies. Recommend shipping C.5 before F so the full roster is available at match launch.
