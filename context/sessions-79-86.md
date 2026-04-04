# Session Log Archive: Sessions 79–86

> Period: 2026-03-29 to 2026-03-30
> Focus: C-7 audio override, TODO sweep, NAT traversal (D8), Solo Room screen, match startup pipeline (Phases A–D), full game catalog registration
> Back to [index](README.md)

---

## Session S86 -- 2026-03-30

**Focus**: Match Startup Pipeline Phase C — client manifest receive, catalog check, status response

### What Was Done

1. **Phase C: `manifestCheck()` in `netmanifest.c`**:
   - Added `g_ClientManifest` global (client-side received manifest)
   - `manifestCheck(manifest)` iterates all entries:
     - Tries `assetCatalogResolveByNetHash(net_hash)` first; falls back to `assetCatalogResolve(id)`
     - `MANIFEST_TYPE_COMPONENT` entries: missing → added to `missing_hashes[]` list
     - All other types (BODY, HEAD, STAGE, WEAPON): if not in catalog, assumed base game asset (always present locally) — logs NOTE, not error
   - Sends `CLC_MANIFEST_STATUS(READY)` if no missing components; `NEED_ASSETS` with hash list otherwise
   - Added `#include "net/netmsg.h"` to netmanifest.c

2. **`netmsgSvcMatchManifestRead()` (netmsg.c)**:
   - Now populates `g_ClientManifest` as entries are read from wire
   - Calls `manifestCheck(&g_ClientManifest)` after parsing all entries

3. **Server broadcast in `netmsgClcLobbyStartRead()` (netmsg.c)**:
   - After `manifestBuild()` + `manifestLog()`: sends `SVC_MATCH_MANIFEST` to all clients via `netSend(NULL, &g_NetMsgRel, true, NETCHAN_CONTROL)`

4. **Dispatch wiring in `net.c`**:
   - Server: `CLC_MANIFEST_STATUS → netmsgClcManifestStatusRead()`
   - Client: `SVC_MATCH_MANIFEST → netmsgSvcMatchManifestRead()`

5. **`port/include/net/netmanifest.h`** updated: added `extern match_manifest_t g_ClientManifest`, `manifestCheck()` declaration.

### Decisions Made

- `manifestCheck()` lives in `netmanifest.c` — same file as `manifestBuild()`, keeps manifest logic co-located.
- Base game non-component assets assumed present if not found in catalog — avoids false NEED_ASSETS for synthetic IDs.
- Only `MANIFEST_TYPE_COMPONENT` can trigger `NEED_ASSETS`.

### Build Status

Both targets build clean: client (30s) + server (9s).

### Next Steps

- Playtest: client logs should show "MANIFEST: checking" and "sending READY/NEED_ASSETS"
- Phase D: server receives NEED_ASSETS, queues missing components via netdistrib
- B-51/B-52/B-53: Networked MP verification

---

## Session S85 -- 2026-03-30

**Focus**: Match Startup Pipeline Phase B — server-side manifest build + log

### What Was Done

1. **Phase B** (`port/src/net/netmanifest.c`, `port/include/net/netmanifest.h`, `port/src/net/netmsg.c`, `CMakeLists.txt`):
   - Created `netmanifest.c` with `manifestBuild()`, `manifestComputeHash()`, `manifestClear()`, `manifestAddEntry()`, `manifestLog()`
   - `manifestBuild(out, room, cfg)` reads from: `g_MpSetup` (stage, weapons), `g_NetClients[]` (player body/head), `g_BotConfigsArray[]` (bot body/head), `g_Lobby.settings.numSimulants`, `modmgrGetCount/GetMod` (enabled mods)
   - Asset IDs as `"body_%d"`, `"head_%d"`, `"stage_0x%02x"`, `"weapon_%d"` — tries `assetCatalogResolve()` first, falls back to FNV-1a
   - `manifestComputeHash()`: FNV-1a over all (net_hash, type, slot_index) tuples
   - `manifestLog()`: dumps entry count + hash + one line per entry to LOG_NOTE
   - Added `g_ServerManifest` global; wired into `netmsgClcLobbyStartRead()` before `netServerStageStart()`
   - Added `netmanifest.c` to CMakeLists.txt server sources

### Decisions Made

- `room`/`cfg` params accepted but unused for Phase B (NULL from call site)
- Dedup in `manifestAddEntry` by `net_hash`
- Synthetic FNV-1a fallback for dedicated server where base catalog isn't populated

### Build Status

Both targets build clean.

### Next Steps

- Playtest: start networked match, check logs for "MANIFEST: built" entries
- Phase C: client-side manifest receive + catalog check + CLC_MANIFEST_STATUS send

---

## Session S84 -- 2026-03-30

**Focus**: Branch cleanup, WorktreeCreate hook, J-2 server GUI connect code, J-4 relative timestamps, body.c white texture/crash fix, bundled mod removal, match startup pipeline design doc, build dir cleanup

### What Was Done

1. **Branch cleanup**: Switched to `dev` branch; deleted `main` locally + on GitHub; changed GitHub default branch to `dev`. Created `stable` from `dev` for releases.

2. **WorktreeCreate hook** (`.claude/settings.local.json`): Blocks Claude Code from creating worktrees (exit code 2). Forces all work to happen in main working copy.

3. **J-2: Server GUI connect code via STUN** (commit 895af4f): `server_gui.cpp:~684` IP waterfall — UPnP→STUN→empty. Shows "discovering..." while either working; "LAN only" only when both settled. `pdgui_bridge.c`: `netGetPublicIP()` checks STUN between UPnP and HTTP fallback.

4. **J-4: Recent Servers relative timestamps + persistence** (commit 1c3354e): `fmtRelTime` lambda for "5s ago", "12m ago". Subtitle: "ABC-DEF · 5m ago". `net.c`: `lastresponse = (u32)time(NULL)` for unix timestamps. Expanded config: `Net.RecentServer.N.Host` and `Net.RecentServer.N.Time`.

5. **B-55 fixed: White textures + crash in body.c** (commit fe64adc): Unified head-loading path — removed destructive `g_FileInfo[...].loadedsize = 0` (root cause). Removed dead `normmplayerisrunning && !IS4MB()` branch.

6. **Bundled mod removal** (modmgr.c, fs.c, modmgr.h, assetcatalog files): Removed `g_BundledModIds[]` array (allinone, gex, kakariko, dark_noon, goldfinger_64), `modmgrIsBundled()`, bundled-first sort priority, hardcoded `"mods/mod_allinone"` default. Mod manager now purely scans `mods/` directory.

7. **Match startup pipeline design doc** (`context/designs/match-startup-pipeline.md`): Unified design merging B-12 Phase 3, R-2/R-3, J-3, C-series, distribution. 7-phase pipeline: Gather→Manifest→Check→Transfer→Ready Gate→Load→Sync. New messages: `SVC_MATCH_MANIFEST (0x62)`, `CLC_MANIFEST_STATUS (0x0E)`, `SVC_MATCH_COUNTDOWN (0x63)`. 6 implementation phases (A–F).

8. **Build directory cleanup**: Build test directories now go in `ClaudeBuilds/`; fully rm'd after completion.

9. **B-56 identified**: Arena dropdown in Room screen needs `PushID()`/`PopID()`. Not yet implemented.

### Decisions Made

- `dev` replaces `main` as active development branch. `stable` is for releases.
- `g_FileInfo[...].loadedsize = 0` removal: the destructive reset was the root cause of shared-head white texture bugs.
- Bundled mods removed entirely — cleaner scan-only mod manager.
- Match startup pipeline establishes integration point for B-12 Phase 3, R-series, C-series, and mod distribution.

### Build Status

All changes build clean: client + server via `tools/build.sh --target both`.

### Next Steps

- Playtest B-55 fix: shared-head textures.
- Implement B-56: PushID/PopID arena dropdown.
- Match Startup Pipeline Phase A: define + stub new protocol messages.
- B-51/B-52/B-53: Networked MP verification.

---

## Session S83 -- 2026-03-30

**Focus**: NAT traversal (all 4 phases), protocol v23, connect code port encoding, mouse capture, online MP crash fix

### What Was Done

1. **NAT Traversal — Phase D8 complete**:
   - **STUN client** (`netstun.c`): RFC 5389 Binding Request; parses XOR-MAPPED-ADDRESS; populates `g_StunPublicIP`/`g_StunPublicPort`
   - **Query advertising** (`netlobby.c`): `SVC_ADDR_QUERY` / `CLC_ADDR_REPORT` — server broadcasts STUN external IP+port to all clients
   - **Hole punch** (`netholepunch.c`): `CLC_PUNCH_REQ` / `SVC_PUNCH_REPLY` symmetric handshake; 5 probe packets per peer; 3s timeout; fallback to relay
   - **NAT diagnostics** (`pdgui_debugmenu.cpp`): "NAT" section shows STUN result, punch status per peer, relay fallback

2. **Protocol version bumped to 23** (`net.h`): `PROTOCOL_VERSION 23`.

3. **Connect code port encoding** (`connectcode.c`, `connectcode.h`): Extended 6-word sentence encoding for non-default ports. Default-port servers still produce 4-word codes (backwards-compatible).

4. **Mouse capture fix** (`gfx_sdl2.cpp`): `SDL_SetRelativeMouseMode` gated on game state. Captured during gameplay; released in menus/lobby/room screen.

5. **B-54 fixed**: `catalogGetSafeBodyPaired()` was missing from client-side `SVC_STAGE_START` handler → white textures/crash on Felicity intro camera. Fixed: call added to client path.

6. **89 stale worktrees cleaned**. **3 scheduled tasks created** (nightly build, context maintenance, context briefing).

7. **Spawn weapon logging added** (player.c, bot.c) — log lines for weapon equip and no-weapon paths.

### Decisions Made

- STUN server: public `stun.l.google.com:19302` with compile-time override.
- Hole-punch: 3s timeout with relay fallback — avoids blocking on symmetric NAT.
- 6-word connect code extension is backwards-compatible.

### Build Status

All changes build clean.

### Next Steps

- Playtest NAT: verify hole punch succeeds between two NATted peers.
- B-51/B-52/B-53: Networked MP verification.
- C-5/C-6: Texture + anim override wiring.

---

## Session S82 -- 2026-03-30

**Focus**: Solo Room screen — route "Combat Simulator" to Room screen (offline mode)

### What Was Done

- `pdgui_lobby.cpp`: Added `s_SoloRoomActive` flag, `pdguiSoloRoomOpen()` / `pdguiSoloRoomClose()` (extern "C"). `pdguiLobbyRender()` NETMODE_NONE branch calls `pdguiRoomScreenRender()` when `s_SoloRoomActive`.
- `pdgui_menu_room.cpp`: Added `s_IsSoloMode` flag and `pdguiRoomScreenSetSolo()`. Solo mode: hides network UI, shows local player name, renames title to "Combat Simulator", "Leave Room" → "Back to Menu", Start Match calls `matchStart()` directly.
- `pdgui_menu_mainmenu.cpp`: "Combat Simulator" button calls `pdguiSoloRoomOpen()` instead of pushing `g_MatchSetupMenuDialog`.

### Key Decisions

- `matchStart()` in `matchsetup.c` is the correct solo path — already handles `g_MpSetup` config, participant setup, `mpStartMatch()` + `menuStop()`.
- Room screen rendered as opaque overlay in `pdguiLobbyRender()` NETMODE_NONE branch.
- `pdguiRoomScreenReset()` called before `pdguiRoomScreenSetSolo(1)` to avoid reset wiping the solo flag.

### Build Status

Client + server build clean.

---

## Session S81 -- 2026-03-30

**Focus**: Networked MP playtest fix cycle — B-49 verified, B-50 fix, weapon spawn fix, server build guards, J-1/J-5, 6 branch merges

### What Was Done

- **B-49 VERIFIED FIXED** — footstepChooseSound infinite loop confirmed resolved. Mike's playtest: all surfaces (including toilet/vent shaft) no longer freeze.

- **Weapon spawn on dedicated server fixed** — Added 6-element `weapons[]` array to `CLC_LOBBY_START` payload. Protocol bumped to **v22**.

- **Server build errors fixed**: `pdgui_backend.cpp` PD_SERVER guard; `netmsg.c` struct redefinition fix; `netlobby.c` `mpSetWeaponSet` link error (direct array copy).

- **B-50 FIXED** — Dedicated server match-end freeze: SDL wall-clock timer added to `hubTick()` in `hub.c`. Records `s_MatchStartMs` on match start; fires `netServerStageEnd()` when elapsed ≥ timelimit.

- **Options/chrslots sync verified** — both already wired in prior sessions. No changes needed.

- **J-1 VERIFIED** — Full join cycle: connect code → CLSTATE_LOBBY → match loads → match runs → match ends.

- **J-5 DONE** — `pdguiMainMenuReset()` called on `SVC_AUTH` receipt. Menu stack cleared on lobby join.

- **6 branches merged** into dev: sweet-bouman (S80 context), youthful-robinson (S74 audit), stupefied-lalande (S59 social lobby), serene-booth (connectcode), suspicious-jones (S69 player count audit), serene-margulis (S72 bot names).

- **Playtest backlog cleanup** — B-43/B-44/B-26/B-40/B-41/B-42/B-46 confirmed working in live networked play. Closed.

### Decisions Made

- Protocol bumped to v22 (weapon array added to CLC_LOBBY_START)
- hub.c timer uses wall-clock so it fires even if stage hasn't fully loaded on server side

### Build Status

VERIFIED clean — client + server build with no errors or warnings.

### Still Needs Playtest

- B-51/B-52/B-53: Networked MP (bots visible, weapons/doors interactive)

---

## Session S80 -- 2026-03-29

**Focus**: Full codebase TODO sweep + enet ABA vulnerability fix + modding pipeline design

### What Was Done

- **T-3/T-4/T-5**: Base table expansion — 1207 animations, 3503 textures, 1545 audio entries in `assetcatalog_base_extended.c`.
- **T-10**: `modmgrComputeDirSize()` recursive directory walker.
- **T-6**: `catalogRequestThumbnail()` / `catalogPollThumbnails()` circular buffer.
- **C-7**: `audioPlayFileSound()` via `SDL_LoadWAV` in `audio.c`; intercept in `snd.c`.
- **C-8**: `catalogLoadInit()` re-wired on mod toggle.
- **C-9**: `catalogComputeStageDiff` implemented.
- **D5 thumbnail batch render**: `pdguiCharPreviewBakeToTexture()` FBO readback.
- **Mixer buffer fix**: MP3 decode staging buffer prevents overrun.
- **Port TODO batch**: main.c, video.c, pdsched.c, input.c, mpsetups.c (6 items).
- **Net + savefile batch** (7 items): O(1) syncid prop map, ROM CRC validation, `SVC_PROP_USE` removal, etc.
- **Game + lib batches** (30+ items documented/implemented).
- **Renderer + fast3d batch** (12 items): `gfx_destroy` cleanup implemented; server history fully implemented.
- **`n_resample` minimum ratio clamp IMPLEMENTED** — real divide-by-zero bug fix.
- **Enet ABA fix** — `ENET_ATOMIC_CAS` returns `bool` on all 4 paths.
- **All project-owned TODO/HACK/FIXME markers resolved** — only upstream/third-party remain.
- **Server history**: `serverhistory.json` + Recent Servers panel + relative timestamps.
- **Modding pipeline design doc** created: `PD2_Modding_Pipeline_Design.docx`.

### Decisions Made

- `n_resample` ratio clamp: divide-by-zero was a real latent bug, not just a TODO — fixed in place.
- `gfx_destroy`: implemented cleanup rather than documenting as intentional leak.
- Modding pipeline deferred to implementation until matches are stable.

### Build Status

VERIFIED clean — `PerfectDark.exe` 43.3 MB, `PerfectDarkServer.exe` 21.1 MB.

---

## Session S79 -- 2026-03-29

**Focus**: C-7 — File-based SFX playback for mod sound overrides

### What Was Done

- **`audioPlayFileSound(const char *path, u16 volume, u8 pan)`** added to `port/src/audio.c`:
  - Uses `SDL_LoadWAV` → `SDL_BuildAudioCVT` / `SDL_ConvertAudio` → `SDL_QueueAudio`
  - Volume: engine scale 0–0x7fff. Pan: 0=full left, 64=centre, 127=full right
  - On failure returns 0 (caller falls back to ROM sound)
  - Bypasses N64 ADPCM pipeline — mod files are standard WAV
- **C-7 wired in `src/lib/snd.c`**: `audioPlayFileSound()` call in `r.is_mod_override` branch. On success: `*handle = NULL`. On failure: log + fall through to ROM path.

### Files Modified

- `port/include/audio.h` — `audioPlayFileSound()` declaration
- `port/src/audio.c` — implementation
- `src/lib/snd.c` — `#include "audio.h"` + C-7 TODO replaced

### Decisions Made

- WAV playback through SDL_QueueAudio (not N64 audio emulator) — mod files are standard PCM.
- Existing SDL audio device reused — no new device needed.
- Pitch not yet supported — deferred (SRC work too complex).

### Next Steps

- Playtest: mod sound override triggers correct WAV file; fallback works when file missing.
