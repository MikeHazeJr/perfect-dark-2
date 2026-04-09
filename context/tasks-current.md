# Active Tasks -- Current Punch List

> Razor-thin: only what needs doing. Completed work archived in `_archive/tasks-archive.md`.
> For phase status, see [infrastructure.md](infrastructure.md). For bugs, see [bugs.md](bugs.md).
> Back to [index](README.md)

---

## v0.1.0 "Foundation" Release Prep

### Must-Have

| Item | Priority | Status | Detail |
|------|----------|--------|--------|
| **D5 Phase 3 -- Remaining menu screens** | HIGH | IN PROGRESS | 61/120 screens need ImGui ports. Solo pause done, options done. Many remaining are simple stubs/dialogs. |
| **D5 Phase 4 -- Theme System** | HIGH | PLANNED | Auto-extract base-ui textures at runtime (no CLI flag). Mod themes selectable in settings. ~3 sessions. |
| **B-112 root cause** | HIGH | INVESTIGATING | Chr pointer corruption in 31-bot matches. VEH guard + chrBruise/chrDamage guards in place. Awaiting next crash log. |
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
