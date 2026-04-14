# Bug Tracker — One-Off Issues

> Open bugs only. For recurring architectural patterns, see [systemic-bugs.md](systemic-bugs.md).
> Back to [index](README.md)

---

## Open Bugs

Only entries that still need work or playtest verification. Fixed bugs live in
the compact reference below with commit SHAs.

| ID | Severity | Status | File | Verify |
|----|----------|--------|------|--------|
| **B-141** | MED | **OPEN — INSTRUMENTED S251** (commit `5a42f234`). Audio skips / pauses, not reproducible on demand. `audioEndFrame()` now counts drops (queue full) / underruns / hitches (>50 ms inter-frame gap); always on, low overhead. Per-event log opt-in via `Audio.VerboseLog=1` in pd.ini. Auto 30-second summary when any counter moved. Accessor: `audioGetB141Counters()`. | `port/src/audio.c` | Next repro: tail `pd.log` for `AUDIO[B-141]`. Summary gives count; verbose log gives per-event timestamps. Then narrow hitch-dominated vs underrun-dominated vs drop-dominated. |
| **B-126** | HIGH | **MITIGATED S234 (FIX-A)**. Silent crash ~8 min in MP. Root cause: deep AI chains exhaust 8 MB stack → `-fstack-protector-strong` canary smash → SIGABRT → handler double-faults on corrupt stack → silent death. A.1 stack depth cap, A.2 generation tokens, A.3 SIGABRT handler hardened, A.4 per-chr stack watermark. | `chr.c`, `chraction.c`, `crash.c`, `main.c` | 31-bot repro test to confirm crash eliminated. |
| **B-112** | HIGH | **MITIGATED S234 (FIX-A)**. Chr rbx corruption in `chraTick` during 31-bot matches. Two classes addressed: (1) stack corruption from deep AI chains (A.1 cap), (2) freed-slot-reallocated-at-same-address (A.2 generation tokens). | `chraction.c`, `chr.c`, `types.h`, `crash.c` | 31-bot repro test to confirm. |
| **B-142** | HIGH | **FIXED-PENDING-PLAYTEST 2026-04-13** (merge `4d1e13c1`). False kill counter at match start — root cause: `mpPlayerGetIndex(NULL)` returned 0 when `g_MpAllChrPtrs[0]==NULL` (NULL==NULL match at index 0). Fix: `if (chr == NULL) return -1;` early guard. Defensively correct regardless of whether it is the sole root cause. | `src/game/mplayer/mplayer.c:3734` | Fresh 32-bot Chicago match, idle 30 s, open pause → kill counter should be 0/0. |
| **B-18** | MED | **LIKELY FIXED S243 (FIX-C.1)**. Pink sky on Skedar Ruins — cross-frame GBI env-color leakage into cloud LERP. FIX-C.1 canonical state reset at frame start resets env color to white before `skyRender()`. | `lv.c` | Skedar Ruins playtest — sky should be blue-purple, not pink. |
| **B-19** | MED | **PARTIAL FIX (S125 F.1 anti-repeat)**. Bot spawn stacking on Skedar Ruins — all bots at same pad. | `player.c` | Skedar-specific playtest. |
| **B-21** | MED | **CLOSED-TENTATIVE 2026-04-13**. Menu double-press / Esc registers multiple times. Two-layer fix: S124 push_tick 100 ms grace in `inputCtxShouldSuppressKey`, S208 150 ms `MAIN_MENU_CLOSE_GRACE_MS` guard + explicit `io.AddKeyEvent(Escape, false)` on appear. | `inputctx.c`, `pdgui_menu_mainmenu.cpp` | Next playtest; promote to FIXED if no recurrence. |

### Open without B-number (2026-04-13 Chicago playtest, carried forward)

These are in `tasks-current.md` punch list and `context/scratch/archive/2026-04-13/README.md` — promotion to formal B-numbers deferred until the 2026-04-13 drop is playtest-verified (several may have already cleared).

- **Bug B (countdown-cancel-on-room-close)** — server countdown lingers after the leader closes a room; fires into the next room. Fix lives in `readyGateTickCountdown()` / `netmsg.c`.
- **Bug C (post-game endscreen partial render)** — scrim + title-bar rectangle render but content body is invisible. Six hypotheses in `scratch/archive/2026-04-13/session-state-endgame-crash.md` §4c; needs instrumentation in `renderMpEndscreen`.
- **Bug D (invisible networked bots on Chicago)** — bots visible on minimap and audible but not rendered in world. Likely chr generation token mismatch (FIX-A.2 area); may have cleared with today's drop.
- **Chicago silent crash ~9 s** — needs VEH log + symbolify.
- **Airbase Start-Match 0xc0000005 / no-response** — log shows manifest OK but no SVC_STAGE_START. May share root cause with Bug A (fix shipped `d37e9677`); needs post-drop repro.

---

## Fixed Bugs — Compact Reference (newest first)

| ID | Description | Fixed |
|----|-------------|-------|
| B-143 | End-Game-Crash 0xc0000005 — `netDisconnect()` called `mainChangeToStage(STAGE_CITRAINING)` while `g_ClientManifest` still held the match manifest → `manifestMPTransition()` diffed torn-down entries → AV. Fix: `manifestClear(&g_ClientManifest)` before stage change (pattern-match to F-0.4 / L1-1). Also: controller-friendly modal confirm UX. See systemic-bugs.md SP-13. | 2026-04-13 — d37e9677 — net.c, pdgui_menu_warning.cpp |
| B-134 | Chicago MP fire-escape railing trap — `spawnPoolRaycastBudget()` 5-unit near-hit threshold passed railings inside player capsule (30 units). Fix: threshold = `SPAWNPOOL_CAPSULE_RADIUS` (30.0f). L1 rejection log upgraded to LOG_WARNING. | 2026-04-13 — 0b44b2b8 — spawnpool.c, spawnpool.h |
| B-140 | Mod music playlist auto-advance never broadcast — `NETMODE_SERVER_AUDIO` was `#define`d 2 (= NETMODE_CLIENT) instead of 1 (= NETMODE_SERVER); `audioNetworkMusicTick()` fired on clients not host. | S249 / 2026-04-13 — e13c2d1f — audio.c:17 |
| B-139 | Chicago countdown 3-2-1 overlay stayed visible on main menu after disconnect. Fix: `pdguiCountdownReset()` called from mode→NONE in `pdgui_lobby.cpp`. | S248 / 2026-04-13 — 16de65e6 — pdgui_lobby.cpp, pdgui_bridge.c |
| B-138 | Mod Manager Apply Changes disabled when only mod-level toggles dirty; no unsaved-changes guard on close. Fix: `modmgrIsDirty()` check + `Apply Changes*` label + `Unsaved Changes` modal. | S248 / 2026-04-13 — 16de65e6 — pdgui_menu_modmgr.cpp |
| B-137 | Room screen title bar always static "Room" regardless of room name. Fix: `g_RoomCache`/`g_LocalRoomId` lookup; title shows "Room: <name>". | S248 / 2026-04-13 — 16de65e6 — pdgui_menu_room.cpp |
| B-136 | All mods had to be re-enabled every run — `modmgrSetEnabled()` never followed by `modmgrSaveConfig()` in mod-level toggle paths. Fix: save after each toggle site. | S248 / 2026-04-13 — 16de65e6 — pdgui_menu_modmgr.cpp |
| B-135 | `base-ui` mod shows [INVALID] — `mod.json` missing required `"id"` field. Fix: added `"id": "base-ui"`. | S248 / 2026-04-13 — 16de65e6 — mods/base-ui/mod.json |
| B-132 | Saved action bindings populated UI but not live input — `actionmapLoadBinds` applied saved string to every IMC with `has_mapping[a]` set, cross-contaminating menu/pause/text-input IMCs. Fix: `break` after first parseBindStr match, matching first-match-wins serialiser semantics. | 2026-04-11 (Opus 1M playtest batch) — actionmap.cpp |
| B-131 | Double-menu reopen in CI — reopening main menu after back-out immediately closed on next frame. Fix: 150 ms `MAIN_MENU_CLOSE_GRACE_MS` guard on ESC/B close + explicit `io.AddKeyEvent(Escape/GamepadFaceRight, false)` on appear to force prev=false/cur=false. | 2026-04-11 (Opus 1M playtest batch) — pdgui_menu_mainmenu.cpp |
| B-130 | Theme editor X / Close / click-outside all ineffective — hand-rolled overlay + per-frame `SetNextWindowFocus()` fought ImGui. Fix: full rewrite with native `BeginPopupModal`; X via `&p_open`, click-outside via rect-test (suppressed by IsAnyItem{Active,Hovered}), Escape via modal default. Side effect: fixes ColorEdit4 sub-popup dismissal. | 2026-04-11 (Opus 1M playtest batch) — pdgui_menu_theme_editor.cpp |
| B-129 | Agent save path broken — `saveInit()` never called; `s_SaveDir` stayed empty; saves wrote to `/agent_smarch.json` (drive root). `titleSetNextStage(0x30)` retried current stage because `besttimes[]` never persisted. Fix: `saveInit()` call after `saveMigrateInit()` in main.c and after `configInit()` in server_main.c. | S198 / 2026-04-10 — 3fc345bf — main.c, server_main.c, savefile.c |
| B-128 | Sky tearing / transparent sky tris — cloud rendering inherited stale `G_RM_AA_XLU_SURF` from sun flares. Point fix: `gDPSetRenderMode(OPA_SURF)` at sky.c:1244. Systemic defense: FIX-C.1 canonical GBI state reset at frame start in lv.c eliminates SP-10 cross-frame state leakage class. | 2026-04-10 + S243 (FIX-C.1) — sky.c:1244, lv.c:1407 |
| B-127 | WASD held not registering — KBM axis synthesis set `.value` but not `.held`. S188 deeper fix removed device gate entirely. | S187 / S188 — actionmap.cpp:793 |
| B-125 | Weapons not spawning in online MP — `spawn_weapon_id` catalog string never serialized in CLC_LOBBY_START or SVC_STAGE_START. | S187 — netmsg.c |
| B-124a | Escape closes pause menu immediately — parallel `inputKeyJustPressed(VK_ESCAPE)` paths in bondmove/player fired alongside `actionPressed(ACTION_PAUSE)`; no cooldown guard. Fix: removed parallel paths, added close-cooldown guard. | S188 — bondmove.c, player.c, pdgui_menu_pausemenu.cpp |
| B-124b | Controller can't navigate menus — `pdguiDriveImGuiNav()` injected `ImGuiKey_Gamepad*` but `NavEnableGamepad` disabled. Fix: map to keyboard-nav keys (UpArrow/Enter/Escape etc.) which work with `NavEnableKeyboard`. | S188 — pdgui_backend.cpp |
| B-124c | WASD/left stick only works when another input fires — KBM synthesis gated on `s_LastDevice==KBM`; gamepad noise events flipped device. Fix: removed device gate; WASD synthesis always runs. | S188 — actionmap.cpp |
| B-124d | Right stick moves camera while menu open — `actionmapPollFrame()` polled SDL sticks regardless of context. Fix: context check; zero gameplay axes when top ctx != gameplay. | S188 — actionmap.cpp |
| B-123 | "Next Mission" reloads same stage — endscreen advance used `missionSetStagenum()` (passing stagenum as runtime_index, B-120 pattern). Fix: new `missionSetStageByCatalog()` resolves from catalog. | S167 (M0.1a) — pdgui_menu_endscreen.cpp, mainmenu.c |
| B-122 | Endscreen mouse unresponsive — deferred flush in `pdgui_backend.cpp` only checked `pauseMenuOpen`, missed endscreen's `g_CtxImGuiMenu`. Fix: flush guard → `!pdguiIsActive()`; `inputCtxSyncMouseMode()` per-frame in endFrame; manual SDL calls removed from endscreen. | S170 (M1.2) — pdgui_backend.cpp, inputctx.c, pdgui_menu_endscreen.cpp |
| B-121 | Failed/complete mission endscreen not interactive — `renderSoloEndscreen()` released SDL grab but never pushed `g_CtxImGuiMenu`. Fix: push context on appear, matches pause pattern. Same fix for MP endscreen. | S166 — pdgui_menu_endscreen.cpp |
| B-120 | Wrong stage loaded for solo — `catalogIdByRuntime(ASSET_MAP, stagenum)` passed stagenum instead of stage-table index. Fix: convert via `bgGetStageIndex()` first. | S166 — pdgui_menu_solomission.cpp, mainmenu.c |
| B-119 | stagenum=0x00 crash on solo mission — `sm_missionconfig` shadow struct missing `stage_id[64]` field, all field accesses at wrong offsets; `stagenum` write went to `stage_id[0]`. Fix: add field + set from catalog + resolve stagenum via `catalogResolveStage()` at consumption. | S165 — pdgui_menu_solomission.cpp, mainmenu.c |
| B-117 | Hard crash on match exit — `pdguiEndscreenExitToMainMenu()` never popped `g_CtxImGuiMenu`; stale context survived stage transition. Fix: `inputCtxPopDeferred(&g_CtxImGuiMenu)`. | S175 — pdgui_bridge.c |
| B-99 | Updater extraction failure / parse failure — FIX-F.1: `curlGet()` returns HTTP code; 403 rate-limit path with user-visible message. FIX-F.2: `fsFullPath("$E/")` fallback when `installDir` empty. FIX-F.3: 1 MB minimum size check on extracted `PerfectDark.exe`. | S245 / 2026-04-13 — 205c74a7 — updater.c |
| B-98 | Solo mission pause menu fell back to OG rendering — ImGui menu not fully implemented. Fix: `renderPauseMenu()` fully implemented via hotswap registration. | S164 — pdgui_menu_solomission.cpp |
| B-97 | Special Assignments not separated from main mission list. Fix: sections existed from M1.1 (S168); S242/S245 added completion counters (done/total) + SeparatorText headers. | S242 / S245 — pdgui_menu_solomission.cpp |
| B-96 | Mission select difficulty flow wrong. Fix: two-panel (list / detail with inline difficulty + objectives + Start), single-screen flow. | S168 (M1.1) — pdgui_menu_solomission.cpp |
| B-95 | Update notification banner persists during active gameplay. Fix: `pdguiPauseGetNormMplayerIsRunning()` suppresses banner during combat sim / solo missions; reappears on menu/lobby. | S221 / 2026-04-13 — pdgui_menu_update.cpp |
| B-93 | Pause menu missing Abort/Restart/objectives. Fix: 5-button menu + objectives checklist with difficulty filter + completion icons. | S164 — pdgui_menu_solomission.cpp |
| B-91 | Mission detail popup "(No objectives)". Fix: right panel loads objectives via `soloLoadBriefingForStageId()` on select; filters by difficulty. | S168 (M1.1) — pdgui_menu_solomission.cpp |
| B-90 | Mission select shows all missions regardless of unlock. Fix: locked missions grayed out / non-selectable; `isStageDifficultyUnlocked()` gates accessibility. | S168 (M1.1) — pdgui_menu_solomission.cpp |
| B-86 | enet_peer_send return unchecked — failed sends undetected. Fix: check return + destroy on failure. | S185 — netdistrib.c |
| B-83 | Incomplete shutdown sequence — quit didn't flush saves / ENet / SDL audio. Fix: H-7 reorder `mempPCFreeAll` after subsystem shutdowns. | S185 — main.c |
| B-82 | Audio 22020 Hz (should be 22050). Fix: M-4 corrected. | S185 — audio.c:66 |
| B-80 | `archive_bytes` not validated at BEGIN time — stored without cap until END. Fix: C-4 256 MB cap on decompression buffer. | S185 — netdistrib.c |
| B-79 | Mod distribution chunk ordering ignored — out-of-order delivery silently corrupted archive. Fix: `expected_chunk` validates ordering; C-5 compressed_cap overflow guard. | S185 — netdistrib.c |
| B-72 | SVC_LOBBY_STATE raw stagenum u8 on wire. | CLOSED 2026-04-13 — confirmed fixed by v27 protocol refactor (catalog ID string + `assetCatalogResolve(arena_id)`) — netmsg.c |
| B-60 | Stray 'g'+'s' behind Video/Audio tabs in Settings — title text drawn without clipping; font descenders extended below title bar. Fix: `dl->PushClipRect()` around title text + glow. | S223 — pdgui_menu_mainmenu.cpp |
| B-118 | CI intro cutscene 56 late-added models — manifestSPTransition ran before setupLoadFiles, g_StageSetup.props NULL, CHR/prop scan missed all setup characters. Fix: manifestSPRescanSetup post-load hook + manifestMenuTransition for title stage. | 2026-04-13 — netmanifest.c, lv.c, pdmain.c |
| B-133 | Inline Vp in display list caused GBI crash — charpreview embedded Vp data inline in GBI display list; interpreter tried to execute it as opcode 0x02 (vscale bytes), fatal error. Fix: static Vp. | S210 — pdgui_charpreview.c: s_PreviewVp static + gfx_pc.cpp: PRIxPTR format fix |
| B-81 | JSON tokenizer unbounded recursion — crafted save nesting → stack overflow | S200 — savefile.c: S_MAX_DEPTH 64 guard + 256KB file size cap |
| B-78 | Chat rebroadcast DoS amplification — no message size cap; rate limiter alone allowed 160KB/s per attacker | 2026-04-11 — netmsg.c: CHAT_MSG_MAX_LEN 255 + length check before rate-limit ring (cb6f4763) |
| B-84 | Dead `tmp[1024]` in `netmsgSvcChatRead` | 2026-04-11 — removed alongside B-78 fix |
| B-115 | Post-game mouse unresponsive — legacy Save Player + Confirm Name dialogs rendered native, stealing input from ImGui endscreen. Suppressed both as noop. | S176 — pdgui_menu_mpingame.cpp, pdgui_menu_warning.cpp |
| B-114 | CMakeLists.txt corruption — ~30MB of garbage bytes injected at lines 181 and 532 by devtools encoding bug; broke all builds | S148 — CMakeLists.txt (b84c6ba) |
| B-113 | Stack overflow → silent crash in 31-bot matches — 2MB default stack exhausted in deep AI/collision chains; UEF double-faulted (8KB on stack); process died with no log. Fixed: 8MB reserve + VEH with static buffers | S150 — CMakeLists.txt / crash.c / system.c (85928d9) |
| B-111 | Bot stuck-detection all 31 bots fire simultaneously at frame 180 — `s_BotStuck` zero-initialized, bogus distance-from-origin comparison on first check → all bots marked stuck at frame 180 | S150 — bot.c (87b3388) |
| B-110 | Bot spawn void geometry crashes — bots spawned at underground/invalid positions (y=-634 on Chicago) due to AIDROP filter collapsing all valid pads to padnum=0; then 31-bots-on-24-pads fallback stacking; then room==-1 persistence. Root-cause fix: remove AIDROP filter (S149); pad-scoring skip of unspawned bots + jitter fallback (S149); room resolution hardening + botSpawnAll failsafe (S148) | S148–S149 — playerreset.c / navspawn.c / botSpawn / botSpawnAll (59818e3, d2e558e, e03a990, a81926e) |
| B-109 | Stale `s_SelectedBotSlot` reference in room screen reset — crash hazard on room screen revisit | S144 — pdgui_menu_room.cpp |
| B-108 | Authority client chr desync detection active on dedicated server — triggered continuous resync storm with many bots | S142 — net.c (skip desync detection when not authority) |
| B-107 | Dedicated server broadcast blocked — `g_NetLocalClient` guard prevented relay to all clients; no state updates reached players | S142 — net relay path (3645e28) |
| B-106 | Bot rooms=-1 freeze — `botmgrAllocateBot` set rooms[0]=-1; prop ticked once then never again; bots invisible and frozen after frame 0 | S142 — botmgr.c/bot.c (PROPFLAG_NOTYETTICKED gate, 41431a3) |
| B-105 | CLC_LOBBY_START buffer overflow — used 1440-byte struct field for match config; overflowed at ~23 bots (of 31), declaring numSims=31 but only ~23 valid entries; caused bot count mismatch and corrupt chrslots | S142 — pdgui_bridge.c (256KB static buffer, 2de61ab) |
| B-104 | Solo/MP endscreen buttons unclickable — direct SDL_SetRelativeMouseMode calls left g_InputMode=GAMEPLAY, allowing re-lock and blocking ImGui keyboard events | S144 — pdgui_menu_endscreen.cpp (pdmainSetInputMode(INPUTMODE_MENU)) |
| B-103 | Online match doesn't start when countdown hits zero — g_MpSetup.stage_id never set; stage missing from manifest/session catalog; SVC_STAGE_START writes stage_session=0; client silently bails ("malformed or unknown message 0x10") | S137 — port/src/net/netmsg.c (184922a) |
| B-102 | Catalog tab (Settings → Catalog) crashes on open — NULL `s_AssetTypeNames[ASSET_LANG]`; 68 base lang banks registered but array had only 24 entries (ASSET_LANG = index 24, uninitialized = NULL) | S133 — port/fast3d/pdgui_menu_mainmenu.cpp (7fb1831) |
| B-101 | Updater Download/Rollback button clickable when no binary asset (assetSize=0 or empty assetUrl) — clicking starts a download that will fail | S132 — port/fast3d/pdgui_menu_update.cpp |
| B-94 | Dear ImGui duplicate ID on pause menu hover — Resume/Options buttons missing ##id suffixes | S132 (accdfb4) — pdgui_menu_pausemenu.cpp |
| B-92 | Mouse not captured on solo mission start — cursor visible during gameplay | S132 (accdfb4) — pdmain.c / input.c |
| B-100 | Combat Sim crash on match start — modmgr body/head cache indexed by g_HeadsAndBodies runtime_index instead of mpbodynum/mpheadnum; s_CatalogBodies[0]/s_CatalogHeads[0] always zero, mpGetBodyId(0)=0/mpGetHeadId(0)=0, catalogResolveByRuntimeIndex(HEAD,0) fails | S132 — port/src/modmgr.c |
| B-77 | fread unchecked in savefile load — silent save corruption | S131 sweep3 |
| B-85 | buildArchiveDir stale pointer on realloc failure | S131 sweep3 |
| B-87 | strcpy in input.c VK names — no size guard | S131 sweep3 |
| B-88 | strcpy in mpsetups.c — three strcpy calls | S131 sweep3 |
| B-89 | strcpy in fs.c homeDir — copy without explicit bounds | S131 sweep3 |
| B-75 | SVC_PLAYER_MOVE OOB array access from network id | S131 sweep2 |
| B-73 | ChrResync null-prop buffer desync — CRITICAL | S130 |
| B-74 | Unbounded malloc from network archive_bytes — CRITICAL | S130 |
| B-76 | sprintf buffer overflow in objective HUD | S130 |
| B-63 | catalogResolveByRuntimeIndex type=16 failure — all bots invisible | S121 Phase B |
| B-64 | MP crash on bot model access (dependent on B-63) | S121 Phase B |
| B-65 | Server catalog gap — SVC_STARTGAME sent raw hex stagenum | S123 Phase D |
| B-66 | Mouse capture not activating on match start | S125 Phase F |
| B-67 | Post-mission menu buttons non-interactive | S124 Phase E |
| B-68 | Menu green tint bleeds to main menu | S124 Phase E |
| B-69 | Esc key spawning duplicate menus | S124 Phase E |
| B-70 | Bot spawn weapons missing | S125 Phase F |
| B-71 | Spawn point not randomizing in MP | S125 Phase F |
| B-62 | manifestEnsureLoaded dedup always misses — 31+ log entries per spawn | S116 |
| B-61 | Difficulty select screen missing text — langSafe() fixes | S116 |
| B-59 | Obj 1 crash — SP manifest overflow (MANIFEST_MAX_ENTRIES 128→1024) | S101 |
| B-58 | catalogResolveByRuntimeIndex assert type=16 on scenario save | S109 |
| B-57 | Scenario save: weaponset index only, not individual weapon picks | S109 |
| B-56 | ImGui duplicate ID in arena dropdown | S109 |
| B-55 | White textures + crash from shared head file loadedsize reset | S84 |
| B-54 | Online MP crash on Felicity intro camera / white textures | S83 |
| B-53 | Can't open doors in networked match | S90 |
| B-52 | Can't pick up weapons/ammo in networked match | S90 |
| B-51 | Bot stuck/invisible under map in networked match | S90 |
| B-50 | Dedicated server match-end freeze (timelimit expiry) | S81 |
| B-49 | Felicity/toilet freeze after long fall | S79/S81 |
| B-47 | Exit freeze on window close (UPnP blocking) | S74 |
| B-46 | Void spawn on MP stages (Felicity 0x2b) | S74 |
| B-45 | Match-end freeze (MPPAUSEMODE_GAMEOVER no return path) | S73 |
| B-42 | Add Bot button limited to 7 bots | S68 |
| B-41 | Spawn weapon not auto-equipping | S68 |
| B-40 | Time limit alarm fires immediately at match start | S68 |
| B-39 | Jump crash in capsule ceiling path (players[-1] OOB) | S68 |
| B-38 | Possible setupCreateProps crash — FALSE ALARM, all hypotheses verified safe | S80 |
| B-37 | Client crash in bodiesReset() during Combat Sim stage load | S67 |
| B-36 | Client crash after skyReset — ambient music NULL deref | S63 |
| B-35 | server_main.c SDL window title shows raw IP | S56 |
| B-34 | server_main.c shows "0/N connected" with 1 player | S56 