# Session Log (Active)

> Recent sessions only. Archives: [1-6](sessions-01-06.md) . [7-13](sessions-07-13.md) . [14-21](sessions-14-21.md) . [22-46](sessions-22-46.md) . [47-78](sessions-47-78.md) . [79-86](sessions-79-86.md)
> Back to [index](README.md)

## Session S91 -- 2026-03-31

**Focus**: SA-1 — Session Catalog Infrastructure (Phase 1 of session-catalog-and-modular-api.md)

### What Was Done

1. **SA-1 COMPLETE — Session catalog infrastructure** (`port/include/net/sessioncatalog.h`, `port/src/net/sessioncatalog.c`):
   - Implemented full per-match translation layer: manifest entries → u16 session IDs
   - Server-side: `sessionCatalogBuild(manifest)` assigns sequential session IDs 1..n from manifest; `sessionCatalogBroadcast()` sends SVC_SESSION_CATALOG (opcode 0x67) reliable on NETCHAN_CONTROL; `sessionCatalogGetId()`/`sessionCatalogGetIdByHash()` for lookup
   - Client-side: `sessionCatalogReceive(buf)` parses wire, resolves each entry via `assetCatalogResolveByNetHash` + fallback `assetCatalogResolve`; logs `[SESSION-CATALOG-ASSERT]` for unresolved entries (= pipeline bug); `sessionCatalogLocalResolve(session_id)` for O(1) lookup
   - Lifecycle: `sessionCatalogTeardown()` zeros all state at match end; `sessionCatalogIsActive()` / `sessionCatalogLogMapping()` for debug
   - SESSION_CATALOG_MAX = 256; SESSION_ID_NONE = 0 (reserved)

2. **Wiring** (existing files modified):
   - `netmsg.h`: added `SVC_SESSION_CATALOG 0x67` opcode and `netmsgSvcSessionCatalogRead()` declaration
   - `netmsg.c`: added `#include "net/sessioncatalog.h"`; after `manifestBuild()` + `manifestLog()`: calls `sessionCatalogBuild()` + `sessionCatalogLogMapping()`; after SVC_MATCH_MANIFEST broadcast: calls `sessionCatalogBroadcast()`; in `netmsgSvcStageEndRead()`: calls `sessionCatalogTeardown()` (client-side match end); added `netmsgSvcSessionCatalogRead()` implementation delegating to `sessionCatalogReceive()`
   - `net.c`: added `#include "net/sessioncatalog.h"`; added `case SVC_SESSION_CATALOG` to client dispatch switch; added `sessionCatalogTeardown()` in `netServerStageEnd()` (server-side match end)
   - `CMakeLists.txt`: added `port/src/net/sessioncatalog.c` to `SRC_SERVER` explicit list (client auto-discovered via GLOB_RECURSE)

3. **Design adherence**:
   - No `bool`/`stdbool.h` — all booleans are `s32` as required
   - Wire format uses `netbufWriteStr`/`netbufReadStr` consistent with SVC_MATCH_MANIFEST pattern
   - Session IDs are 1-based; 0 = SESSION_ID_NONE (reserved)
   - Client translation table is directly indexed by session_id: O(1) resolve

### Build Status

Both targets (client + server) build clean (S91, tools/build.sh --target both, ~52s).

### Next Steps

- SA-2: Modular catalog API layer — typed result structs + `catalogResolveBody/Head/Stage/Weapon()` functions in assetcatalog.h/c
- Playtest: at match start, verify log output shows "SESSION-CATALOG: built N entries for match" + per-entry mapping table on server, and "SESSION-CATALOG: received N entries" on client
- C-5/C-6: Texture + anim override wiring (still pending)

---

## Session S90 -- 2026-03-31

**Focus**: Playtest bug triage (B-51/B-52/B-53), bot config transmission fix, codebase asset reference audit, session catalog design

### What Was Done

1. **B-51 FIXED — Bot configs not transmitted via SVC_STAGE_START**:
   - Root cause: `SVC_STAGE_START` did not include bot configuration (body/head/team/name) in the wire payload. Clients received no bot config — bots spawned with default/zero geometry, appearing invisible or stuck under the map.
   - Fix: Extended `SVC_STAGE_START` payload to transmit bot config block. Uses catalog net_hash values (not raw N64 body/head indices) on the wire. Clients resolve hash → local index via their catalog.
   - Protocol bumped to **v25**.

2. **B-52 FIXED — `scenarioInitProps` not called on clients**:
   - Root cause: Client-side stage load path (`SVC_STAGE_START` handler) did not call `scenarioInitProps()`. This function initializes all interactive props — weapon pickups, ammo, doors, keys, switches. Without it, all props were non-interactive.
   - Fix: Added `scenarioInitProps()` call to client-side `SVC_STAGE_START` handler.

3. **B-53 FIXED — End match hang / door non-interactive**:
   - Root cause: Same `scenarioInitProps()` omission as B-52. Interactive props (doors, keys) were not initialized on clients.
   - Fix: Covered by the B-52 fix.

4. **Full codebase asset reference audit**:
   - Audited ~180 call sites across ~20 patterns where raw N64 indices cross interface or protocol boundaries (raw `filenum`, `bodynum`, `headnum`, `stagenum`, `texnum`, `animnum` in wire messages, save files, public APIs).
   - Audit findings captured in design doc: `context/designs/session-catalog-and-modular-api.md`.

5. **Session catalog + modular API design doc created** (`context/designs/session-catalog-and-modular-api.md`):
   - Foundational architecture covering: modular catalog API (per-system typed query functions), network session catalog translation layer (catalog IDs ↔ wire net_hash), and load manifest system for both MP and SP.
   - **Key principle established**: The catalog replaces the **entire** legacy loading pipeline, not just networking. All asset references at interface boundaries use catalog IDs — never raw N64 indices.
   - Discussion of mobile mod creation webapp as a future community feature (deferred, no implementation).

6. **Context maintenance**: Constraints updated (v25 protocol, catalog-at-boundaries constraint). Bugs B-51/52/53 moved to Fixed. Session catalog track added as highest infrastructure priority.

### Key Decisions

- **Catalog replaces entire legacy pipeline**: Session catalog is not a networking-only concern. It is the authoritative identity system for all asset references at all interface boundaries.
- **Bot configs on wire use catalog hashes**: Raw N64 body/head indices never travel over the network. Wire format uses catalog net_hash — clients resolve to local index via their own catalog.
- **Session catalog is #1 infrastructure priority**: Match startup pipeline (Phases B–F), mod distribution (Phase D), and anti-cheat all require catalog-based identity at boundaries.

### Protocol

- **NET_PROTOCOL_VER bumped to 25**: SVC_STAGE_START now includes bot config block with catalog hashes.

### Build Status

Both targets build clean.

### Next Steps

- Implement session catalog + modular API (SA-1 through SA-5 — see design doc)
- C-5/C-6: Texture + anim override wiring
- R-2: Room lifecycle (expand hub slots, room_id, leader_client_id)
- Playtest B-51/B-52/B-53 fixes in live networked session to confirm

---

## Session S88 -- 2026-03-30

**Focus**: Match Startup Pipeline Phases D, E, F — completing the full pipeline

### What Was Done

1. **Phase D: Mod Transfer Gate** (`netmsg.c`):
   - In `netmsgClcManifestStatusRead()`, when client reports `NEED_ASSETS`: resolves each missing hash via asset catalog, queues component for chunked delivery via `netDistribServerHandleDiff`
   - No-op when all clients report READY (base game assets always present locally)

2. **Phase E: Ready Gate** (`netmsg.c`, `net.c`):
   - Added `s_ReadyGate` static struct: bitmask-based tracker (`expected_mask`, `ready_mask`, `declined_mask`, `deadline_ticks`, `stagenum`, `total_count`)
   - Three helpers: `readyGatePopcount()`, `readyGateBroadcastCountdown()`, `readyGateCheck()`
   - `netmsgClcLobbyStartRead()`: after manifest broadcast, transitions clients to `CLSTATE_PREPARING`, initializes ready gate with 30s timeout, enters `ROOM_STATE_PREPARING`
   - `netmsgClcManifestStatusRead()`: READY → marks bit + broadcasts countdown + checks gate; NEED_ASSETS → queues transfer; DECLINE → resets client to CLSTATE_LOBBY (spectator)
   - `netServerStageStart()` (net.c): now transitions CLSTATE_PREPARING → CLSTATE_GAME
   - Replaced fire-and-forget launch with proper handshake

3. **Phase F: Sync Launch Countdown** (`netmsg.c`, `netmsg.h`, `net.c`):
   - Extended `s_ReadyGate` with countdown fields (`countdown_active`, `countdown_next_tick`, `countdown_secs`)
   - Added `g_MatchCountdownState` global for client-side UI display
   - `readyGateCheck()`: when all ready, arms 3-second countdown instead of immediate launch
   - `readyGateTickCountdown()`: called in `netEndFrame()`, decrements at 60-tick intervals, broadcasts SVC_MATCH_COUNTDOWN with MANIFEST_PHASE_LOADING, fires stage start at 0
   - `netmsgSvcMatchCountdownRead()`: populates g_MatchCountdownState for room screen UI
   - Countdown sequence: 3→2→1→0 with broadcasts, then mainChangeToStage + netServerStageStart

### Build Status

Both targets build clean: client (31s) + server (9s).

### Next Steps

- Phase C.5 remaining: navmesh spawn for SP maps, unlock gating in selection UI, character picker categories
- UI: room screen reading g_MatchCountdownState to display "Match starting in 3..."
- Playtest: full pipeline end-to-end (manifest → check → ready gate → countdown → launch)

---

## Session S87 -- 2026-03-30

**Focus**: Phase C.5 — Full Game Catalog Registration (SP bodies/heads from g_HeadsAndBodies[152])

### What Was Done

1. **Full game catalog expansion** (`port/src/assetcatalog_base.c`):
   - Added SP body/head registration after the arena loop in `assetCatalogRegisterBaseGame()`
   - Builds a 152-entry boolean coverage mask from `g_MpBodies[0..62]` (bodynum) and `g_MpHeads[0..75]` (headnum)
   - Iterates all 152 `g_HeadsAndBodies[]` entries — skips covered entries, null sentinel (filenum==0), and BODY_TESTCHR (0x70)
   - Uses `unk00_01` to distinguish heads (==1) from bodies (==0)
   - Registers as `base:sp_head_N` / `base:sp_body_N` with category "sp"
   - Populates source_filenum from the entry's filenum for asset resolution
   - ~12 new entries: Eyespy, ChiCroBot, Mini Skedar, President Clone, Skedar King, The King, Grey, Beau variants

2. **Pipeline design doc update** (`context/designs/match-startup-pipeline.md`):
   - Added Phase C.5 "Full Game Catalog Registration" section (§6.5)
   - Added C.5 implementation checklist between C and D
   - Updated §10 "What This Replaces" and §11 "Estimated Effort"

3. **Task list update** (`context/tasks-current.md`):
   - Added Phase C.5 to Match Startup Pipeline table
   - Updated pipeline description from 7-phase to 8-phase

### Decisions Made

- SP characters registered with `requirefeature = 0` (no unlock gating yet) — unlock gating is a UI/selection concern, not a catalog concern.
- Category "sp" distinguishes SP-only models from "base" MP models.
- Stage registration (all mission maps) already in place via `s_BaseStages[]` — no additional work needed.

### Build Status

Both targets build clean: client + server (42s total).

### Next Steps

- Phase D: Transfer Gate — wire netdistrib for missing mod component delivery
- Phase E: Ready Gate — gate stage start on all clients READY
- UI exposure: character picker showing full roster with SP/unlock categories
