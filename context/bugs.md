# Bug Tracker — One-Off Issues

> Open bugs only. For recurring architectural patterns, see [systemic-bugs.md](systemic-bugs.md).
> Back to [index](README.md)

---

## Open Bugs

| ID | Severity | Description | File | Status |
|----|----------|-------------|------|--------|
| **B-129** | HIGH | AV crash at end of M1/O1 post-game transition. Sequence: Use press → ACTIONMAP activated 'menu' (prio 10, depth 2) → `imgui_menu` on_push → pushed → **FATAL AV PC=+0xc2dc1 CODE=0xc0000005**. Backtrace: `+0xc2dc1 +0x28e575 +0x2c2c9d +0x2c2eee +0x2c35a8 +0x29236a +0x28eff6 +0x205a60 +0x1fa66d +0x1e6e8d +0x300a69 +0x1e6003 +0x1e65a7 +0x1e6877 +0x1bea08`. Hypothesis: mission-complete/solo-results screen not fully migrated to ImGui — dereferences stripped legacy menu state in `on_push` callback. Same class as B-117 (context pop bug) but on push side. Log: `019d75be-pdclient.log`, debug session `local_c9a07f59-cf81-4679-bbe9-87b8125c3fe0`. | pdgui_menu_endscreen.cpp (likely), imgui_menu on_push path | OPEN — INVESTIGATING (S190, exciting-fermat). Backtrace symbolization pending. |
| **B-126** | HIGH | Silent crash after ~8 minutes in MP — process dies with no error/VEH log. 7 bots + 1 player, well under MAX_MPCHRS. Stack (8MB), VEH, H-7 all verified in build. Added heartbeat logger (every 60s) to lv.c to capture last-known-good state before next crash. | lv.c (heartbeat), crash.c (VEH) | INVESTIGATING (S187) — heartbeat instrumentation added, awaiting next repro |
| **B-125** | HIGH | Weapons not spawning in online MP — `spawn_weapon_id` catalog string never serialized in CLC_LOBBY_START or SVC_STAGE_START. Server never resolved `spawnWeaponNum`. All players/bots spawn with fists. | netmsg.c | **FIXED S187** — Added spawn_weapon_id write/read to both CLC_LOBBY_START and SVC_STAGE_START with catalog resolution |
| **B-127** | HIGH | WASD held not registering — KBM move axis synthesis in `actionmapPollFrame()` set `.value` but not `.held` flag for ACTION_AXIS_MOVE_X/Y. Movement only worked for one frame (edge-triggered), not continuous. Gamepad stick path correctly set `.held`. | actionmap.cpp:793 | **FIXED S187** — Added `.held` flag update mirroring gamepad pattern. **S188 deeper fix**: removed device gate entirely (Bug C below). |
| **B-124a** | HIGH | Escape closes pause menu immediately — parallel `inputKeyJustPressed(VK_ESCAPE)` paths in bondmove.c/player.c fired alongside `actionPressed(ACTION_PAUSE)`. `pdguiPauseMenuClose()` lacked cooldown guard, enabling double-fire across game ticks within one render frame. | bondmove.c, player.c, pdgui_menu_pausemenu.cpp | **FIXED S188** — Removed parallel paths, added cooldown guard to close. |
| **B-124b** | HIGH | Controller can't navigate menus — `pdguiDriveImGuiNav()` injected `ImGuiKey_Gamepad*` keys but `NavEnableGamepad` disabled, so ImGui ignored all gamepad nav. | pdgui_backend.cpp | **FIXED S188** — Mapped to keyboard nav keys (UpArrow/Enter/Escape etc.) which work with `NavEnableKeyboard`. |
| **B-124c** | HIGH | WASD/left stick only works when another input fires — KBM axis synthesis in `actionmapPollFrame()` gated on `s_LastDevice==KBM`. Gamepad noise events flipped device, stopping WASD synthesis while gamepad section wrote zeros. | actionmap.cpp | **FIXED S188** — Removed device gate; WASD synthesis always runs, overrides idle gamepad values when keys held. |
| **B-124d** | HIGH | Right stick moves camera while menu open — `actionmapPollFrame()` polled SDL sticks every frame regardless of input context. Context stack blocks events but not polled axis values. | actionmap.cpp | **FIXED S188** — Added input context check; zeros all gameplay axes when top context is not gameplay. |
| **B-118** | MED | Crash during CI intro cutscene loop — 56 models/characters late-added to SP manifest (missed by pre-scan), then hard crash with no shutdown sequence. Pre-scan for SP manifests needs to be more comprehensive for cutscene stages. | netmanifest.c, intro stage load path | OPEN (S163) |
| **B-117** | HIGH | Hard crash/freeze on match exit — `pdguiEndscreenExitToMainMenu()` popped legacy menus but never popped the `g_CtxImGuiMenu` input context pushed on window appear. Stale context survived stage transition → crash. | pdgui_bridge.c | **FIXED S175** — Added `inputCtxPopDeferred(&g_CtxImGuiMenu)` to `pdguiEndscreenExitToMainMenu()`. |
| **B-112** | HIGH | Chr pointer (rbx) corruption in `chraTick` during 31-bot matches — access violation at `chr->hidden`; guard + diagnostics added (S150), additional shot/damage path guards + handicap default init (S155), root cause still unknown | src/game/chraction.c, chr.c | PARTIAL — VEH guard + chrBruise/chrDamage guards + `model->definition` check in place; awaiting next crash log to identify corruption source |
| **B-18** | MED | Pink sky on Skedar Ruins — sky renders pink instead of correct color | sky rendering path | OPEN — needs investigation |
| **B-19** | MED | Bot spawn stacking on Skedar Ruins — all bots spawn at same pad | player.c | PARTIAL FIX (S125 F.1 anti-repeat) — needs Skedar-specific playtest |
| **B-21** | MED | Menu double-press / hierarchy issues — Esc registers multiple times | menumgr.c | LIKELY FIXED (S124 Phase E full-stack dedup) — needs playtest |
| **B-60** | LOW | Stray 'g'+'s' visible behind Video/Audio tabs in Settings | pdgui_menu_mainmenu.cpp | OPEN |
| **B-72** | LOW | SVC_LOBBY_STATE sends raw stagenum u8 — display-only, not a match blocker | netmsg.c:4149 | OPEN — LOW PRIORITY |
| **B-78** | MED | Chat rebroadcast without rate limiting — DoS amplification vector | netmsg.c | OPEN |
| **B-79** | MED | Mod distribution chunk ordering ignored — `chunk_idx` discarded; out-of-order delivery silently corrupts archive | netdistrib.c | **FIXED S185** — expected_chunk field validates ordering; C-5 compressed_cap overflow guard |
| **B-80** | MED | archive_bytes not validated at BEGIN time — stored without cap until END; companion to B-74 | netdistrib.c | **FIXED S185** — C-4 256MB cap on decompression buffer |
| **B-81** | MED | JSON tokenizer unbounded recursion — pathological save nesting causes stack overflow crash | savefile.c | OPEN |
| **B-82** | MED | Audio sample rate 22020 Hz — unusual (not 22050 Hz), may cause pitch shift or driver issues | audio.c:66 | **FIXED S185** — M-4 corrected to 22050 |
| **B-83** | MED | Incomplete shutdown sequence — quit path doesn't flush saves, ENet, SDL audio; remote peers left dead-connected | main.c | **FIXED S185** — H-7 reordered mempPCFreeAll after all subsystem shutdowns |
| **B-84** | LOW | Dead `tmp[1024]` in chat handler — unused stack variable, maintenance hazard | netmsg.c | OPEN |
| **B-86** | LOW | enet_peer_send return value unchecked — failed sends go undetected | netdistrib.c | **FIXED S185** — H-5 check return + destroy on failure |
| **B-119** | HIGH | stagenum=0x00 crash on solo mission start — `sm_missionconfig` shadow struct missing `stage_id[64]` field (added to real `missionconfig` for catalog migration), causing all field accesses to be at wrong offsets. `stagenum` write went to `stage_id[0]`; real `stagenum` stayed 0x00. Fix: (1) add `stage_id[64]` to shadow struct, (2) set `stage_id` from catalog in mission select, (3) resolve stagenum from stage_id in `menuhandlerAcceptMission`/`menudialog00103608`/pause restart. | pdgui_menu_solomission.cpp, mainmenu.c | **FIXED S165** |
| **B-120** | HIGH | Wrong stage loaded for solo missions — `catalogIdByRuntime(ASSET_MAP, stagenum)` passed logical stagenum instead of stage table index (runtime cache is indexed by stage table index, not stagenum). Stagenum 0x40 (decimal 64) looked up stage table index 64 (an MP arena) instead of the correct solo stage. Fix: convert stagenum → stage table index via `bgGetStageIndex()` before calling `catalogIdByRuntime()`. Same bug existed in both `pdgui_menu_solomission.cpp` and `mainmenu.c`. | pdgui_menu_solomission.cpp, mainmenu.c | **FIXED S166** |
| **B-121** | MED | Failed/complete mission endscreen menu not interactive — `renderSoloEndscreen()` released SDL mouse grab directly but never pushed `g_CtxImGuiMenu` input context, so ImGui didn't receive proper input routing. Fix: push `g_CtxImGuiMenu` on window appear (matches pause menu pattern). Also fixed in MP endscreen renderer. | pdgui_menu_endscreen.cpp | **FIXED S166** |
| **B-122** | MED | Endscreen mouse unresponsive — root cause: deferred flush in pdgui_backend.cpp checked `!g_PdguiActive && !pdguiIsPauseMenuOpen()` (only debug overlay + pause menu) but missed endscreen's `g_CtxImGuiMenu` context. When hotswap transitioned active→inactive, flush re-enabled `SDL_SetRelativeMouseMode(SDL_TRUE)`, overriding the context system. Fix: (1) deferred flush guard changed to `!pdguiIsActive()` (checks full context stack), (2) `inputCtxSyncMouseMode()` added to `inputCtxEndFrame()` for per-frame enforcement, (3) manual SDL calls removed from both endscreen renderers (context on_push handles it). | pdgui_backend.cpp, inputctx.c, pdgui_menu_endscreen.cpp | **FIXED S170 (M1.2)** |
| **B-123** | MED | "Next Mission" reloads same stage — endscreen advance used `missionSetStagenum()` which had the same B-120 bug pattern (passing stagenum as runtime_index). Fix: new `missionSetStageByCatalog()` resolves from catalog directly. | pdgui_menu_endscreen.cpp, mainmenu.c | **FIXED S167 (M0.1a)** |
| **B-124** | MED | Esc open/close race condition — pressing Esc to open menu immediately closes it. Root cause: `ImGui_ImplSDL2_ProcessEvent()` ran before context dispatch, so ImGui saw the triggering keypress as a new press on the pushed context. Fix: systemic key suppression in input context system. `push_tick` field added to `InputContext`; `inputCtxShouldSuppressKey()` returns true for KEY_DOWN events within 100ms grace period after push. `pdguiProcessEvent()` skips ImGui forwarding and dispatch for suppressed keys. | inputctx.h, inputctx.c, pdgui_backend.cpp | **FIXED S170 (M1.2)** |
| **B-90** | MED | Mission select shows all missions regardless of unlock status — should only show unlocked | pdgui_menu_solomission.cpp | **FIXED S168 (M1.1)** — Two-panel redesign shows locked missions grayed out and non-selectable; `isStageDifficultyUnlocked()` gates accessibility |
| **B-91** | HIGH | Mission detail popup shows "(No objectives)" — objectives not loading from game data | pdgui_menu_solomission.cpp | **FIXED S168 (M1.1)** — Right panel loads objectives via `soloLoadBriefingForStageId()` on mission select; filters by selected difficulty |
| **B-93** | HIGH | Pause menu missing Abort Mission, Restart Mission, objective checklist — only Resume/Options work | pdgui_menu_solomission.cpp | **FIXED S164** — 5-button menu + objectives checklist with difficulty filter + completion icons |
| **B-95** | LOW | Update notification banner persists during active gameplay — should auto-dismiss or hide during missions | pdgui_menu_update.cpp | OPEN |
| **B-96** | HIGH | Mission select difficulty flow wrong — should be pick mission → pick difficulty → see objectives → Start; currently shows minimal popup | pdgui_menu_solomission.cpp | **FIXED S168 (M1.1)** — Two-panel layout: left=mission list, right=detail with inline difficulty picker, objectives, Start button. Single-screen flow. |
| **B-97** | LOW | Special Assignments / Challenges not separated from main mission list | pdgui_menu_solomission.cpp | OPEN |
| **B-98** | HIGH | Solo mission pause menu falls back to OG rendering for empty sections — ImGui menu not fully implemented | pdgui_menu_solomission.cpp | **FIXED S164** — renderPauseMenu() fully implemented via hotswap registration |
| **B-99** | MED | Updater downloads zip but extraction may fail — needs retest with v0.0.25 fixed binaries | updater.c | OPEN — needs playtest |

---

## Fixed Bugs — Compact Reference (newest first)

| ID | Description | Fixed |
|----|-------------|-------|
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