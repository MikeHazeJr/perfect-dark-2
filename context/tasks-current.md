# Active Tasks -- Current Punch List

> Razor-thin: only what needs doing. For completed work, see [tasks-archive.md](tasks-archive.md).
> For phase status, see [infrastructure.md](infrastructure.md). For bugs, see [bugs.md](bugs.md).
> Back to [index](README.md)

---

## Critical Blockers (v0.1.0 "Foundation")

| # | Task | Details |
|---|------|---------|
| 1 | **B-49 CRITICAL: Felicity/toilet landing freeze** | JUMP_DEBUG already instrumented (S78). Needs reproduction + log capture. Freeze occurs on landing in specific geometry. |
| 2 | **B-38 CRITICAL: setupCreateProps crash** | Possible NULL deref in prop creation during stage load. Needs investigation + root cause. |
| 3 | **UI Scaling** | Not addressed yet. Required for v0.1.0. |

---

## Catalog Activation

| Step | Status |
|------|--------|
| **C-0** Wire assetCatalogInit + RegisterBaseGame + ScanComponents | **DONE** |
| **C-2-ext** source_filenum/texnum/animnum/soundnum in asset_entry_t | **DONE (S74)** |
| **catalogLoadInit** Reverse-index arrays + query functions | **DONE (S74)** |
| **C-4** `catalogGetFileOverride` intercept in `romdataFileLoad()` | **DONE (S74)** — needs playtest with a mod that declares bodyfile |
| **C-5** `catalogGetTextureOverride` intercept in `texLoad()` | **NEXT** — coded, wiring needs confirmation |
| **C-6** `catalogGetAnimOverride` intercept in `animLoadFrame/Header()` | **NEXT** |
| **C-7** `catalogGetSoundOverride` in `sndStart()` | **DONE (S80)** — `audioPlayFileSound()` via SDL_LoadWAV in audio.c, intercept wired in snd.c |
| **C-8** Re-wire `catalogLoadInit()` on mod enable/disable | **DONE (S80)** — `catalogLoadInit()` re-wired on toggle |
| **C-9** Stage diff | **DONE (S80)** — `catalogComputeStageDiff` implemented |

---

## Mod System (T-series) — ALL DONE

| Task | Status |
|------|--------|
| T-1 through T-10 | **ALL DONE (S80)** — base table expansion (anim 1207, tex 3503, audio 1545), size_bytes walker, thumbnail queue, sound intercept, mod.json content, stage reset, texture flush |

---

## Memory Modernization (D-MEM) — ALL DONE

| Task | Status |
|------|--------|
| MEM-1: Load state fields | **DONE** |
| MEM-2: assetCatalogLoad/Unload | **DONE** |
| MEM-3: ref_count + eviction | **DONE** |

---

## Awaiting Build Test / Playtest

| Item | Status |
|------|--------|
| **T-7 mod.json body/head/arena catalog registration** (S77) | **CODED (S77)** — needs playtest: enable a mod with `content.bodies/heads/arenas` in mod.json; verify entries appear in character/arena pickers in-game. |
| **T-8/T-9 Stage table restore + texture cache flush on reload** (S78) | **BUILD VERIFIED (S78)** — needs playtest: toggle a mod on/off, confirm no stale stages or textures after reload. |
| **B-46 Void spawn on MP stages** (S73) | **CODED (S73)** — needs build + Felicity playtest. |
| **B-47 Exit freeze on window close** (S73) | **CODED (S73)** — needs build + test: close window during match, should exit within 1s. |
| **Combat Sim scenario save/load** (S71) | **CODED (S71)** — needs build + smoke test. |
| **B-44/B-26 Bot names+chars + player name fix** (S72) | **CODED (S72)** — needs build + playtest. |
| **B-43 First-tick crash + first-tick safety** (S70) | **CODED (S70)** — needs build + playtest. |
| **B-39 Jump crash fix** (S68) | **BUILD VERIFIED (S68)** — needs playtest: jump on Jungle should no longer crash. |
| **B-40/41 CLC_LOBBY_START timelimit+options wiring** (S68) | **BUILD VERIFIED (S68)** — needs playtest: no alarm at match start; "Start Armed" equips weapon. |
| **B-42 Add Bot cap raised** (S68) | **BUILD VERIFIED (S68)** — needs playtest: add >7 bots in room UI. |

---

## Awaiting Build Test / Playtest (Pre-S68)

| Item | Status |
|------|--------|
| **Room interior UX + Match Start** (S57–S60) | **MILESTONE: MATCH RUNS** (confirmed S68: 7 bots, Jungle, 25s gameplay). Full playtest: Leave Room → social lobby, Start Match → match loads. |
| **netSend audit + CRIT fixes** (S61) | **BUILD VERIFIED (S61)** — 3 critical bugs fixed. Desync recovery now functional. |
| **SP-6 Null Guard Audit** (S64) | **CODED (S64)** — 14 HIGH/CRITICAL PLAYERCOUNT() loops guarded across 8 files. Needs full client build + playtest. |
| **SP-8 Prop/Obj Null Guard Audit 2 of 4** (S65) | **CODED (S65)** — 7 CRITICAL/HIGH fixes. Needs build + playtest. |
| **Bot/AI/Simulant null-guard audit 4 of 4** (S66) | **CODED (S66)** — 28 CRITICAL/HIGH bugs fixed across 6 files. Needs build + dedicated server playtest. |
| **B-36 Client crash after skyReset** (S63) | **CODED (S63+S64)** — needs full client build + playtest on stage with ambient music. |
| **2-player Combat Sim match** (S54) | Build client + server. Connect → lobby → Combat Simulator → verify match loads + both players spawn. |
| **SPF-1/3**: Hub, rooms, identity, lobby, join-by-code (S47d–S49) | Run `.\devtools\build-headless.ps1 -Target server`, then end-to-end join test (J-1) |
| **Update tab — cross-session staged version** (S50) | Build client. Download a version, close without restarting. Reopen Update tab → Switch button should appear. |
| **Player Stats** (playerstats.c) (S49) | Needs client build test. |
| **D3R-7**: Modding Hub -- 6 files (S40) | Needs client build test |
| **B-13**: Prop scale fix (S26) | Needs build test |
| **B-12 Phase 1**: Dynamic participant system (S26) | Needs build test |

---

## Bugs Still Open

| Bug | Severity | Status |
|-----|----------|--------|
| [B-49](bugs.md) Felicity/toilet landing freeze | CRITICAL | JUMP_DEBUG instrumented (S78). Needs reproduction + log capture. |
| [B-38](bugs.md) setupCreateProps crash | CRITICAL | Needs investigation. |
| [B-17](bugs.md) Mod stages load wrong maps | HIGH | Structurally fixed (S32). Needs broader testing. |
| B-18 Pink sky on Skedar Ruins | MEDIUM | Reported S48. Needs investigation. |
| B-19 Bot spawn stacking on Skedar Ruins | MEDIUM | **PARTIAL FIX (S54)** — needs test to confirm dispersal works. |
| B-21 Menu double-press / hierarchy issues | MEDIUM | Escape registering multiple times, menu state confusion. |

---

## Active Work Tracks

### Join Flow (J-series) — Next Steps

| Phase | Task | Details |
|-------|------|---------|
| J-1 | **Verify end-to-end join** | Build server target, start server, enter code in client, verify CLSTATE_LOBBY + match start. |
| J-2 | **Server GUI connect code** | Add connect code display + Copy button to server_gui.cpp Server tab. |
| J-3 | **SVC_ROOM_LIST protocol** | Broadcast room state from server to clients so lobby UI shows real room data. |
| J-4 | **Server history UI** | **DONE (S80)** — serverhistory.json + Recent Servers panel + relative timestamps implemented. |
| J-5 | **Lobby handoff polish** | Verify main menu → lobby overlay transition. Progress indicator during CONNECTING/AUTH. |

See [join-flow-plan.md](join-flow-plan.md) for full audit.

---

### Room Architecture (R-series) — See [room-architecture-plan.md](room-architecture-plan.md)

| Phase | Task | Details |
|-------|------|---------|
| R-1 | **Foundation (no protocol change)** | Hub slot pool stubs, `g_NetLocalClient = NULL` for dedicated server, IP scrub (B-28/29/30). **DONE (S52)** — needs server build test. |
| R-2 | **Room lifecycle** | Expand `HUB_MAX_ROOMS=16`, `HUB_MAX_CLIENTS=32`. Add `leader_client_id`, `room_id`. On-demand room creation. Remove permanent room 0. |
| R-3 | **Room sync (protocol)** | `SVC_ROOM_LIST 0x75`, `SVC_ROOM_UPDATE 0x76`, `SVC_ROOM_ASSIGN 0x77`. `CLC_ROOM_JOIN 0x0A`, `CLC_ROOM_LEAVE 0x0B`. |
| R-4 | **Match start (room-scoped)** | `CLC_ROOM_SETTINGS 0x0C`, `CLC_ROOM_KICK 0x0D`, `CLC_ROOM_TRANSFER 0x0E`, `CLC_ROOM_START 0x0F`. |
| R-5 | **Server GUI redesign** | New Players + Rooms panel layout. No raw IP anywhere. Replace Hub tab with Rooms panel. |

---

### Lobby / Room / Match UX Flow (L-series) — See [lobby-flow-plan.md](lobby-flow-plan.md)

> L-series = client-facing UI only. Depends on R-2 + R-3 complete before L-1/L-2 can be wired.

| Phase | Task | Details |
|-------|------|---------|
| L-1 | **Social Lobby** | Rewrite `pdgui_menu_lobby.cpp`: strip game mode selection, add room list with Join buttons and Create Room. Depends on R-3. |
| L-2 | **Room Create/Join** | Wire Create Room + Join Room buttons to `CLC_ROOM_JOIN`. Handle `SVC_ROOM_ASSIGN`. Depends on R-3. |
| L-3 | **Room Interior + Mode Selection** | New `pdgui_menu_room.cpp`. Leader: mode buttons + room player list. Non-leader: read-only. Depends on R-4. |
| L-4 | **Combat Sim Setup** | Extend `pdgui_menu_matchsetup.cpp` with network path. Depends on R-4. |
| L-5 | **Campaign + Counter-Op Setup** | New `pdgui_menu_campaign_setup.cpp`. Depends on R-4. |
| L-6 | **Drop-In / Drop-Out** | Allow joining ROOM_STATE_MATCH rooms. Depends on L-4/L-5. |

---

### B-12: Participant System

| Phase | Status |
|-------|--------|
| Phase 1: Parallel pool | CODED -- needs build test |
| Phase 2: Callsite migration | DONE (S47b) |
| Phase 3: Remove chrslots + protocol v22 | READY — depends on Phase 2 QC |

---

## Deferred

| Item | Reason |
|------|--------|
| Modding pipeline implementation | Design doc complete (S80). Deferred until matches are stable. |
| Ultrawide support | Planned — will be built properly (not OTR hacks). |
| ARM/NEON detection | Deferred until ARM target on roadmap. |
| Network benchmark → dynamic player cap | Measure bandwidth/latency at server start, call `hubSetMaxSlots()`. |
| Systemic bug audit: SP-1 remaining files | `activemenu.c`, `player.c`, `endscreen.c`, `menu.c` |
| TODO-1: SDL2/zlib still DLL | Low priority. |

---

## Prioritized Next Up

| # | Task | Details |
|---|------|---------|
| 1 | **B-49: Felicity/toilet landing freeze** | Reproduce crash with JUMP_DEBUG log, capture output, find root cause. CRITICAL for v0.1.0. |
| 2 | **B-38: setupCreateProps crash** | Investigate NULL deref in prop creation. CRITICAL for v0.1.0. |
| 3 | **C-5: Texture override wiring** | Confirm `catalogGetTextureOverride` intercept in `texLoad()` is correctly wired. |
| 4 | **C-6: Anim override wiring** | Wire `catalogGetAnimOverride` in `animLoadFrame/Header()`. |
| 5 | **J-1: End-to-end join verify** | Build server target + full join test. See join-flow-plan.md. |
| 6 | **Playtest backlog** | T-7, T-8/T-9, B-43/B-44/B-46/B-47, Combat Sim save/load. |
| 7 | **R-1: Room foundation** | Hub slot pool stubs. No protocol change. |
| 8 | **UI Scaling** | Required for v0.1.0. Not started. |

---

## Pause Menu UX (S26 feedback)

| Issue | What Mike Wants |
|-------|----------------|
| End Game confirm/cancel too small | Separate overlay dialog. B cancels to pause menu. |
| Settings B-button exits to main menu | Should back out one level only |
| OG Paused text behind ImGui (B-15) | Suppress legacy pause rendering. Low priority. |
| Scroll-hidden buttons | Prefer docked/always-visible, minimize scrolling |
