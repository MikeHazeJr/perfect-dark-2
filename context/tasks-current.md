# Active Tasks -- Current Punch List

> Razor-thin: only what needs doing. Completed work archived in `_archive/tasks-archive.md`.
> For phase status, see [infrastructure.md](infrastructure.md). For bugs, see [bugs.md](bugs.md).
> Back to [index](README.md)

---

## v0.1.0 "Foundation" Release Prep

### Input System

#### Done (S189 + pre-S189)

| Item | Status | Detail |
|------|--------|--------|
| **B-button opens doors** | FIXED (S189) | bondmove.c:1836 — PC usemask = BUTTON_ACCEPT_USE only. BUTTON_CANCEL_USE==B_BUTTON was included, causing B to trigger door open. |
| **Gamepad layout: A=jump, Y=use, B=crouch** | FIXED (S189) | actionmap.cpp: Y→USE, A→JUMP, B→CROUCH (dual with CANCEL_USE). RSTICK unbound from crouch. |
| **MP 1-3 default gamepad binds removed** | DONE (S189) | setupGameplayDefaults else block removed. MP slots start unbound, rebind UI still works. |
| **CrouchMode** | ALREADY DONE (pre-S189) | Game.Player%d.CrouchMode: 0=hold (default), 1=analog, 2=toggle, 3=toggle+analog. bondmove.c:1925. |

#### Completed in S190 (confirmed in clean build)

| Item | Status | Detail |
|------|--------|--------|
| **unk14 gate removed** | DONE (S190) | bondmove.c:~1551 — `unk14 = true` unconditional in CONTROLMODE_PC. Was gated on c2stick, breaking left-stick strafe when right stick idle. |
| **canlookahead gate removed (ADS)** | DONE (S190) | bondmove.c:~1633 — `canlookahead = true` unconditional in ADS block. Was c2stick-gated. |
| **canlookahead gate removed (non-ADS)** | DONE (S190) | bondmove.c:~1642 — `canlookahead = !insightaimmode` only; stick gate removed. |
| **FarSight strafe — left stick** | DONE (S190) | bondmove.c:~2104 — FarSight strafe reads `c1stickxsafe` (left stick), not `c2stickx` (aim stick). |
| **LSTICK=Sprint define + bind** | DONE (S190) | actionmap.cpp — `JBTN_LSTICK`/`JBTN_RSTICK` defines added; LSTICK click → ACTION_SPRINT. |
| **_dev-window.ps1 restored** | DONE (pre-S190) | Restored from 68c0b186 after truncation (2311→2232 lines). Em-dashes → hyphens to prevent re-truncation. |

#### Completed in S189

| Item | Status | Detail |
|------|--------|--------|
| **usemask cleanup** | DONE (S189) | bondmove.c:1836 — PC usemask = `BUTTON_ACCEPT_USE` only. Confirmed: BUTTON_CANCEL_USE == B_BUTTON, both excluded. |
| **P0-only binding refactor** | DONE (S189) | All setup*Defaults: Player 0 only, no p=0..3 loops. Init loop replaced with single `setupGameplayDefaults(0)` / `setupVehicleDefaults(0)`. |
| **Bind 1 / Bind 2 collapse** | DONE (S189) | Not structural — flat `triggers[4]` array. No struct change needed. Each action now has 1 kbd + 1 gamepad default max. Dead MENU_UP/DOWN/LEFT/RIGHT binds removed from menu IMCs. |
| **Menu IMC 3-action reduction** | DONE (S189) | g_ImcMenu + g_ImcPauseMenu: ACTION_USE (Return/A), ACTION_CANCEL_USE (Escape/B), ACTION_PAUSE (Start only). 5 triggers total, player 0 only. |
| **crouch_mode in pd.ini** | ALREADY DONE (pre-S189) | `Game.Player%d.CrouchMode`: 0=hold (default). bondmove.c:1925 handles toggle/hold/analog. No changes needed. |

#### TODO — Verification Pass (Mike, post-reset)

| Item | Priority | Detail |
|------|----------|--------|
| **A jumps, does NOT open doors** | HIGH | Verify A=JUMP binding and usemask fix. B should crouch, not open doors. |
| **Y opens doors / interacts** | HIGH | Y=USE is the new interact button. |
| **B crouches, does NOT open doors, exits FarSight/scope** | HIGH | Dual-bind CROUCH + CANCEL_USE. usemask fix ensures B excluded from door mask. |
| **R3 does nothing** | HIGH | RSTICK click is unbound. |
| **LSTICK click sprints** | HIGH | New LSTICK → ACTION_SPRINT binding. |
| **Rebind UI: single-column, MP slots 1–3 unbound** | HIGH | No Bind1/Bind2 split; MP players 1–3 have no default gamepad binds. |
| **CrouchMode=2 toggle works, resets on respawn** | MED | `Game.Player0.CrouchMode=2` in pd.ini. bondmove.c:1925 handles it. |
| **Mission 1 completion: no crash, JSON save written** | HIGH | B-129 fix. `saves/agent_<name>.json` must be updated on mission end. |
| **Sky tearing gone on outdoor stages** | HIGH | B-128 fix. Test on Dark Noon, Goldfinger 64 exteriors. |
| **B-18 check on Skedar Ruins** | MED | Does sky still show pink? B-128 fix may or may not cover B-18. Report result. |

### Must-Have

| Item | Priority | Status | Detail |
|------|----------|--------|--------|
| **D5 Phase 3 -- Remaining menu screens** | HIGH | IN PROGRESS | Full audit complete (S188): 254 dialogs found, 79 screens across 11 batches, ~13-18 sessions. Plan in `context/designs/menu-replacement-plan.md`. No split-screen (2P cancelled). CI Options absorbed into unified Settings. **Batch 0 DONE (S192)**: pdgui_layout primitive (docked action bar + popup scrim), pdguiModelPreview generalized to CHARACTER/WEAPON/VEHICLE/PROP, Mission Select Start Mission docked, Mission Difficulty dialog text-missing regression fixed, Challenges Accept Challenge docked. **Batch 1 DONE (S193)**: seven "low-hanging fruit" dialogs — ExitGame (literal-text fix), PdModeSettings (slider support in fallback), MpEndGame (scrim upgrade), and four filemgr pak-era placeholders redirecting to Agent Select.  1080p scaling baseline flipped in same session (foundation-layer fix). Foundation primitives in place for Batches 2-12. |
| **1080p scaling baseline flip** | HIGH | DONE (S193) | `pdgui_scaling.h` now uses `displayH / 1080.0f` (was 720p).  312 `pdguiScale()` literals across 17 menu files rewritten ×1.5 to preserve visual output.  Action-bar metrics in pdgui_layout.cpp updated to 1080p values (64px button, 84px bar).  `pdgui_backend.cpp` safe-area fallback flipped to 1920×1080.  Migration note in `context/designs/scaling-baseline-1080.md`.  Client builds green. |
| **Mission Select "Start Mission" scrolls off-screen** | HIGH | FIXED (S192) | Root cause: Start button rendered inside `##ms_right` BeginChild with fragile hardcoded layout math. Fix: split right panel into pinned header + difficulty picker + scroll body + docked action bar using new `pdguiBeginActionBar` primitive. Start button always visible regardless of scroll state. |
| **Mission Difficulty dialog "text missing" regression** | HIGH | FIXED (S192) | Root cause: Selectable used invisible `##diff_row` label and drew text via `dl->AddText` overlay; when `langSafe()` returned "" the row appeared blank. Fix: real `ImGui::Selectable(labelStr, ...)` with hardcoded English fallbacks ("Agent", "Special Agent", "Perfect Agent", "PD Mode", "Cancel"). |
| **Challenges "Accept Challenge" scrolls off-screen** | MED | FIXED (S192) | Same class of bug as Mission Select. Fix: split `##chal_detail` into inner scroll body + `##chal_action_bar` docked region. |
| **D5 Phase 4 -- Theme System** | HIGH | PLANNED | Auto-extract base-ui textures at runtime (no CLI flag). Mod themes selectable in settings. ~3 sessions. |
| **B-112 root cause** | HIGH | INVESTIGATING (S191) | Chr pointer corruption in 31-bot matches. S191: entry guard added at top of chraTick (CHR.GUARD channel); g_ChrLastTickedIndex slot-tracker in chr.c; SIGABRT handler reads index. Guards in place; awaiting next 31-bot crash log with chr slot ID. |
| **B-126 silent crash** | HIGH | INVESTIGATING (S191) | Silent crash ~8min into MP. S191: heartbeat 60s→30s; NET.WATCHDOG per-peer dump via netHeartbeatLog(); SIGABRT handler logs chr index. Awaiting next repro to confirm SIGABRT vs other kill path. |
| **B-129 mission-end AV crash** | HIGH | FIXED (S190) | Root: `endscreen.c` called `filemgrSaveOrLoad` → no Pak on PC → fn-ptr cast to lang index → AV. Fixed: `saveSaveAgent()` replaces all three calls; three filemgr dialogs registered as noop. Side benefit: saves now actually written to disk. |
| **D13 -- Update System build test** | MED | BLOCKED | Code written (S11). Needs: libcurl MSYS2 static link, compile test, first GitHub release for E2E. |
| **Build verification + QC pass** | MED | PLANNED | Clean build on dev, all QC tests from qc-tests.md passing, no known crash bugs. |

### Should-Have

| Item | Priority | Status | Detail |
|------|----------|--------|--------|
| **M3 -- Online MP flow** | MED | PLANNED | Lobby polish, room list UX, leader election, Quick Play button. R-3 done unblocks this. |
| **Prop sync event-driven** | MED | PLANNED | Current CRC polling. Should fire on pickup/door events per game director. |
| **B-78 chat rate limiting** | MED | OPEN | DoS amplification vector in netmsg.c rebroadcast. |
| **B-81 JSON recursion guard** | MED | OPEN | Crafted save nesting -> stack overflow crash in savefile.c. |
| **B-118 CI intro cutscene crash** | MED | OPEN | 56 models missed by SP manifest pre-scan. |

---

## Open Bugs (by severity)

### HIGH
| Bug | Description | File |
|-----|-------------|------|
| **B-112** | Chr pointer corruption in 31-bot matches (guards in place, root cause unknown) | chraction.c, chr.c |
| **B-126** | Silent crash ~8min into MP — process dies silently, no VEH log. Heartbeat instrumentation added (S187); awaiting next repro. | lv.c (heartbeat), crash.c |

### FIXED S190 (awaiting visual confirmation from Mike)
| Bug | Description | Fix |
|-----|-------------|-----|
| **B-128** | Sky tearing / transparent sky tris on outdoor stages | sky.c:1244 `gDPSetRenderMode(OPA_SURF)` — inheriting previous frame blend state was root cause |
| **B-129** | AV on mission end — filemgrSaveOrLoad + no Pak + fn-ptr cast to lang index → crash | endscreen.c +14 lines; pdgui_menu_warning.cpp +12 lines; saves now write correctly |

### MEDIUM
| Bug | Description | File |
|-----|-------------|------|
| **B-18** | Pink sky on Skedar Ruins — may be resolved by B-128 sky fix; needs Skedar playtest | sky rendering |
| **B-19** | Bot spawn stacking on Skedar Ruins (partial fix S125) | player.c |
| **B-78** | Chat rebroadcast without rate limiting -- DoS amplification | netmsg.c |
| **B-81** | JSON tokenizer unbounded recursion -- crafted save crash | savefile.c |
| **B-99** | Updater extraction may fail -- needs retest | updater.c |
| **B-118** | CI intro cutscene crash -- 56 models missed by manifest | netmanifest.c |
| Menu opacity stacking | Main menu BG gets more opaque after repeated open/close | pdgui haze overlay |
| Prop sync not event-driven | CRC polling instead of event-driven | net sync |
| Some maps don't spawn enemies | Navmesh/pad coverage gaps post-AIDROP fix | various |
| Killfeed only shows player kills | Bot kills not in killfeed | netdistrib.c |

### LOW
| Bug | Description | File |
|-----|-------------|------|
| **B-60** | Stray 'g'+'s' behind Video/Audio tabs | pdgui_menu_mainmenu.cpp |
| **B-72** | SVC_LOBBY_STATE raw stagenum (display-only) | netmsg.c |
| **B-95** | Update banner persists during gameplay | pdgui_menu_update.cpp |
| **B-97** | Special Assignments not separated from missions | pdgui_menu_solomission.cpp |
| JUMP_LANDING log spam | Every frame during pause logs ground clamp | movement |

---

## Backlog (Post v0.1.0)

| Item | Target | Detail |
|------|--------|--------|
| **D5 Phase 5 -- Lobby scene** | v0.1.0+ | Player portraits, connected player avatars, character preview |
| **B-12 Phase 3 -- Remove chrslots** | v0.2.0 | Protocol bump, participant system replaces bitmask entirely |
| **D14a -- Counter-Op mode** | v0.6.0 | NPC possession mechanic |
| **D15 -- Map Editor / Forge** | v0.5.0 | Level editor, character creator, skin system |
| **D16 -- Master Server** | v0.4.0 | Server registry, heartbeat, browser |
| **D6 -- Persistent Stats** | v1.0.0 | JSON/SQLite stats, post-game scorecard, lifetime viewer |
| **D7 -- Discord Rich Presence** | v1.0.0 | Activity API, join button |
| **D10 -- Spectator Mode** | v0.6.0 | Free-cam, follow-cam, HUD overlay |

---

## Design Guidelines (Planned)

| System | Status |
|--------|--------|
| Menu/UI | DONE (in d5-full-menu-overhaul.md) |
| Input | IMPLEMENTED (M0.2 action maps). Guidelines doc PLANNED. |
| Networking | PLANNED |
| Mod System | PLANNED |
| Audio | PLANNED |
| Visual/Theme | PLANNED |
| Collision/Physics | PLANNED |
| Level Editor (Forge) | PLANNED |
