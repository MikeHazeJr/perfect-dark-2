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

#### Landed, awaiting rebuild (unruffled-kalam worktree, S190)

| Item | Status | Detail |
|------|--------|--------|
| **unk14 gate removed** | LANDED | bondmove.c:~1551 — `unk14 = true` now unconditional in CONTROLMODE_PC. Was gated on c2stick, breaking left-stick strafe when right stick idle. |
| **canlookahead gate removed (ADS)** | LANDED | bondmove.c:~1633 — `canlookahead = true` unconditional in ADS block. Was c2stick-gated. |
| **canlookahead gate removed (non-ADS)** | LANDED | bondmove.c:~1642 — `canlookahead = !insightaimmode` only; stick gate removed. |
| **FarSight strafe — left stick** | LANDED | bondmove.c:~2104 — FarSight strafe reads `c1stickxsafe` (left stick), not `c2stickx` (aim stick). |
| **LSTICK=Sprint define + bind** | LANDED | actionmap.cpp — `JBTN_LSTICK`/`JBTN_RSTICK` defines added; LSTICK click → ACTION_SPRINT. |
| **_dev-window.ps1 restored** | LANDED | Restored from 68c0b186 after truncation (2311→2232 lines). Em-dashes → hyphens to prevent re-truncation. |

#### Completed in S189

| Item | Status | Detail |
|------|--------|--------|
| **usemask cleanup** | DONE (S189) | bondmove.c:1836 — PC usemask = `BUTTON_ACCEPT_USE` only. Confirmed: BUTTON_CANCEL_USE == B_BUTTON, both excluded. |
| **P0-only binding refactor** | DONE (S189) | All setup*Defaults: Player 0 only, no p=0..3 loops. Init loop replaced with single `setupGameplayDefaults(0)` / `setupVehicleDefaults(0)`. |
| **Bind 1 / Bind 2 collapse** | DONE (S189) | Not structural — flat `triggers[4]` array. No struct change needed. Each action now has 1 kbd + 1 gamepad default max. Dead MENU_UP/DOWN/LEFT/RIGHT binds removed from menu IMCs. |
| **Menu IMC 3-action reduction** | DONE (S189) | g_ImcMenu + g_ImcPauseMenu: ACTION_USE (Return/A), ACTION_CANCEL_USE (Escape/B), ACTION_PAUSE (Start only). 5 triggers total, player 0 only. |
| **crouch_mode in pd.ini** | ALREADY DONE (pre-S189) | `Game.Player%d.CrouchMode`: 0=hold (default). bondmove.c:1925 handles toggle/hold/analog. No changes needed. |

#### TODO — Verification Pass (post-unruffled-kalam rebuild)

| Item | Priority | Detail |
|------|----------|--------|
| **In-game verification: A/B/Y input** | HIGH | A=jump, B=crouch (no door open), Y=use (door opens), LSTICK=sprint, R3 unbound. |
| **Single-column rebind UI** | HIGH | Confirm Bind1/Bind2 collapse shows in rebind screen. No MP pre-binds visible. |
| **crouch_mode=1 toggles correctly** | MED | Verify toggle mode in pd.ini works; hold mode (0) is default. |
| **Surface crouch_mode in ImGui options** | MED | Add toggle to Options/Controls panel if not already present. |
| **File truncation safeguard in build pipeline** | DONE (S190) | SP-9 guard implemented in `devtools/build-headless.ps1`: pre-commit `git diff HEAD --numstat` check aborts auto-commit on net < -20 lines with additions < 1/3 of deletions. Commit-time net, not prevention. Mode B (AI output truncation) remains ONGOING_RISK within a session before first commit. (See SP-9.) |
| **Mission-end crash fix (B-129)** | HIGH | Pending diagnosis in exciting-fermat worktree. Fix after backtrace symbolization. |
| **ActionBinding struct refactor** | LOW | If Bind1/Bind2 collapse requires structural change, track as separate item. |

### Must-Have

| Item | Priority | Status | Detail |
|------|----------|--------|--------|
| **D5 Phase 3 -- Remaining menu screens** | HIGH | IN PROGRESS | Full audit complete (S188): 254 dialogs found, 79 screens across 11 batches, ~13-18 sessions. Plan in `context/designs/menu-replacement-plan.md`. No split-screen (2P cancelled). CI Options absorbed into unified Settings. Batch 0 (model preview generalization) is first. |
| **D5 Phase 4 -- Theme System** | HIGH | PLANNED | Auto-extract base-ui textures at runtime (no CLI flag). Mod themes selectable in settings. ~3 sessions. |
| **B-112 root cause** | HIGH | INVESTIGATING | Chr pointer corruption in 31-bot matches. VEH guard + chrBruise/chrDamage guards in place. Awaiting next crash log. |
| **B-126 silent crash** | HIGH | INVESTIGATING | Silent crash ~8min into MP. Heartbeat logger added (S187). Awaiting next repro. |
| **B-129 mission-end AV crash** | HIGH | INVESTIGATING | AV in `imgui_menu on_push` at end of M1/O1. Backtrace captured. Assigned to exciting-fermat for symbolization and fix. |
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
| **B-129** | AV at end of M1/O1 post-game transition — `imgui_menu on_push` crashes with CODE=0xc0000005. Hypothesis: legacy menu state deref on push. Assigned exciting-fermat. | pdgui_menu_endscreen.cpp (likely) |

### MEDIUM
| Bug | Description | File |
|-----|-------------|------|
| **B-18** | Pink sky on Skedar Ruins | sky rendering |
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
