# Bug Tracker — One-Off Issues

> Open bugs only. For recurring architectural patterns, see [systemic-bugs.md](systemic-bugs.md).
> Back to [index](README.md)

---

## Open Bugs

| ID | Severity | Description | File | Status |
|----|----------|-------------|------|--------|
| **B-18** | MED | Pink sky on Skedar Ruins — sky renders pink instead of correct color | sky rendering path | OPEN — needs investigation |
| **B-19** | MED | Bot spawn stacking on Skedar Ruins — all bots spawn at same pad | player.c | PARTIAL FIX (S125 F.1 anti-repeat) — needs Skedar-specific playtest |
| **B-21** | MED | Menu double-press / hierarchy issues — Esc registers multiple times | menumgr.c | LIKELY FIXED (S124 Phase E full-stack dedup) — needs playtest |
| **B-60** | LOW | Stray 'g'+'s' visible behind Video/Audio tabs in Settings | pdgui_menu_mainmenu.cpp | OPEN |
| **B-72** | LOW | SVC_LOBBY_STATE sends raw stagenum u8 — display-only, not a match blocker | netmsg.c:4149 | OPEN — LOW PRIORITY |
| **B-78** | MED | Chat rebroadcast without rate limiting — DoS amplification vector | netmsg.c | OPEN |
| **B-79** | MED | Mod distribution chunk ordering ignored — `chunk_idx` discarded; out-of-order delivery silently corrupts archive | netdistrib.c | OPEN |
| **B-80** | MED | archive_bytes not validated at BEGIN time — stored without cap until END; companion to B-74 | netdistrib.c | OPEN |
| **B-81** | MED | JSON tokenizer unbounded recursion — pathological save nesting causes stack overflow crash | savefile.c | OPEN |
| **B-82** | MED | Audio sample rate 22020 Hz — unusual (not 22050 Hz), may cause pitch shift or driver issues | audio.c:66 | OPEN |
| **B-83** | MED | Incomplete shutdown sequence — quit path doesn't flush saves, ENet, SDL audio; remote peers left dead-connected | main.c | OPEN |
| **B-84** | LOW | Dead `tmp[1024]` in chat handler — unused stack variable, maintenance hazard | netmsg.c | OPEN |
| **B-86** | LOW | enet_peer_send return value unchecked — failed sends go undetected | netdistrib.c | OPEN |
| **B-90** | MED | Mission select shows all missions regardless of unlock status — should only show unlocked | pdgui_menu_solomission.cpp | OPEN |
| **B-91** | HIGH | Mission detail popup shows "(No objectives)" — objectives not loading from game data | pdgui_menu_solomission.cpp | OPEN |
| **B-93** | HIGH | Pause menu missing Abort Mission, Restart Mission, objective checklist — only Resume/Options work | pdgui_menu_pausemenu.cpp | OPEN |
| **B-95** | LOW | Update notification banner persists during active gameplay — should auto-dismiss or hide during missions | pdgui_menu_update.cpp | OPEN |
| **B-96** | HIGH | Mission select difficulty flow wrong — should be pick mission → pick difficulty → see objectives → Start; currently shows minimal popup | pdgui_menu_solomission.cpp | OPEN |
| **B-97** | LOW | Special Assignments / Challenges not separated from main mission list | pdgui_menu_solomission.cpp | OPEN |
| **B-98** | HIGH | Solo mission pause menu falls back to OG rendering for empty sections — ImGui menu not fully implemented | pdgui_menu_pausemenu.cpp | OPEN |
| **B-99** | MED | Updater downloads zip but extraction may fail — needs retest with v0.0.25 fixed binaries | updater.c | OPEN — needs playtest |

---

## Fixed Bugs — Compact Reference (newest first)

| ID | Description | Fixed |
|----|-------------|-------|
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
| B-34 | server_main.c shows "0/N connected" with 1 player | S56 |
| B-33 | CLC_SETTINGS overwrites identity name on server | S56 |
| B-32 | Client lobby empty + no leader | S56 |
| B-31 | SVC_AUTH malformed on client after B-28 | S53 |
| B-30 | Raw IPs in server log output | S52 |
| B-29 | Raw IP visible in server GUI status bar | S52 |
| B-28 | Server occupies player slot 0 on dedicated | S52 |
| B-27 | Dedicated server crash on first client connect (9 bugs) | S50 |
| B-26 | Player name shows "Player1" instead of profile name | S53 |
| B-25 | Server max clients hardcoded to 8 | S49 |
| B-24 | Connect code byte-order reversal | S49 |
| B-23 | Quit Game button clipped on right edge | S49 |
| B-22 | Version boxes not baking into exe | S49 |
| B-17 | Mod stages load wrong maps | S32/S37 |
| B-16 | Back on controller does nothing in pause menu | S26 |
| B-14 | START on controller opens/closes pause immediately | S26 |
| B-13 | GE prop scaling ~10x on mod stages (Part 1) | S26 |
| B-12 | 24-bot cap — Phase 1 coded (dynamic participant system) | S26 |
| B-10 | End Game crash | S21/S26 |
| B-09 | CI overlay corruption with mods | S24 |
| B-08 | Mod manager can't find mods directory | S23 |
| B-07 | Divide-by-zero in spawn selection | S23 |
| B-06 | Uninitialized rooms[] after intro NULL | S23 |
| B-05 | Paradox match hang | S23 |
| B-04 | Paradox crash (besttimes OOB) | S22 |
| B-03 | Player instant death (handicap zero-init) | S21 |
| B-02 | Shots pass through bots (model scale clamp) | S20 |
| B-01 | Camera transition crash | S20 |
