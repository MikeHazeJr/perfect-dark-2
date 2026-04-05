# Active Tasks -- Current Punch List

> Razor-thin: only what needs doing. For completed work, see [tasks-archive.md](tasks-archive.md).
> For phase status, see [infrastructure.md](infrastructure.md). For bugs, see [bugs.md](bugs.md).
> Back to [index](README.md)

---

## Recently Completed (S130–S150 — 2026-04-02/05)

| Item | Status |
|------|--------|
| **Catalog Universality Migration Phases A–G** | **DONE (S119–S130)** — wire protocol v27, all net_hash removed, SAVE-COMPAT stripped, catalog-ID-native data model, server manifest model, menu stack arch, spawn hardening. Playtest verification pending. |
| **Comprehensive bug audit** (19 findings) | **DONE (S130)** — 4 critical/high fixed immediately. See `audit-comprehensive-bugs.md`. |
| **Systemic sweep 1: sprintf→snprintf** (344 sites, 36 files) | **DONE (S131)** |
| **Systemic sweep 2: network array bounds** | **DONE (S131)** — one unguarded site fixed in netmsgSvcAuthRead (id/maxclients). |
| **Systemic sweep 3: fread/fwrite, strcpy→strncpy, realloc NULL** | **DONE (S131)** — B-77/B-87/B-88/B-89/B-85 all fixed. |
| **v0.0.25 released as pre-release** | **DONE (S131)** — version bump, update tab column fix, title intro alignment fix. |
| **Context system cleanup** | **DONE (S131)** — archived completed work, trimmed stale backlog. |
| **Propagation scan — 5 bug patterns** | **DONE (S132)** — dynamic arena buf, inputLockMouse siblings (4 paths), no other propagations found. |
| **Static array audit — dynamic/growable data** | **DONE (S134)** — s_DepTable dynamic, s_ManifestTypeNames "Lang" added. All other static arrays verified as protocol/ROM constants or already dynamic. |
| **D5.0a Technical Spike** | **DONE (S135)** — `pdguiGetUiTexture()` + `ImGui::Image()` pipeline validated, builds clean. D5.0 unblocked. |
| **D5.1 Input Ownership Boundary** | **DONE (S136)** — `InputOwnerMode` enum, `pdmainSetInputMode()`, Tab dedup, mouse capture unified. |
| **B-103 match start fix** | **DONE (S137)** — `g_MpSetup.stage_id` sync on both CLC send and receive paths. |
| **Server bot sync + match startup** | **DONE (S137+)** — server-authoritative bot sync, bot AI on client, ASSET_ARENA resolver fix, match trace. |
| **D5.4 MP scoreboard** | **DONE (S139)** — accuracy col, team sort, dual exit buttons, mouse capture fix. |
| **Network + bot stabilization** | **DONE (S142)** — CLC_LOBBY_START overflow, bot freeze, server broadcast, auth client desync storm. |
| **R-3 Room Networking** | **DONE (S143)** — clients see/create/join rooms, room-scoped match start. |
| **Endscreen UI + name dictionaries** | **DONE (S144)** — endscreen buttons, multi-select bot list, 256-entry name dicts, B-104 fix. v0.0.32. |
| **Post-playtest spawn stability sprint** | **DONE (S145–S150)** — room leave CLC_ROOM_LEAVE, botSpawnAll failsafe, server catalog IDs for bot bodies, AIDROP root-cause removal, 31-bots-on-24-pads fallback hardening, underground ground-clamp, CMakeLists.txt repair, credits update (smarch added), bot stuck-detect init (B-111), chr corruption guard (B-112 partial), 8MB stack + VEH (B-113). v0.0.32→v0.0.38. |

---

## Phase G — Playtest Verification (In Progress)

Playtest was conducted post-S144 and triggered a crash-stability sprint (S145–S150). Many spawn issues have been resolved. The remaining stability concern is **B-112** (chr pointer corruption in 31-bot matches — root cause unknown, guard applied in S150).

**Success criteria**: zero CATALOG-ASSERT in logs, zero type=16, all MP game modes run to completion with bots, menu transitions clean.

### Known Playtest Issues (current codebase)

| Issue | Severity | Notes |
|-------|----------|-------|
| End match → lobby transition broken | ~~HIGH~~ | **Fixed S139** — Return to Lobby calls pdguiSetInRoom(1); Quit to Menu calls netDisconnect/mainChangeToStage |
| Post-match menus janky (buttons non-interactive) | ~~HIGH~~ | **Fixed S139** — pdmainSetInputMode(INPUTMODE_MENU) on window appear |
| Bot spawn void geometry / underground | ~~HIGH~~ | **Fixed S145–S149** — AIDROP root cause removed, room==-1 hardened, ground-clamp, stuck-detect init (B-110, B-111 fixed) |
| Stack overflow → silent crash (31 bots) | ~~HIGH~~ | **Fixed S150** — 8MB stack + VEH (B-113 fixed) |
| **B-112: Chr pointer corruption (31-bot crashes)** | **HIGH** | Guard + diagnostics added S150; root cause unknown. Awaiting next VEH crash log. |
| Killfeed only shows player kills | MED | Bot kills not appearing in killfeed |
| Some maps don't spawn enemies | MED | Likely navmesh/pad coverage gaps — may still exist on some maps post-AIDROP fix |
| Room/menu navigation janky | MED | Back/Esc behavior inconsistent |
| Settings text overlaps tabs | MED | Relative positioning needed in settings screen |
| Scroll indicators too small / scrollbox-in-scrollbox UX | LOW | Scrollable lists hard to navigate |
| B-19: Bot spawn stacking on Skedar Ruins | MED | Partial fix (S125 F.1 anti-repeat) — needs Skedar-specific test |
| B-21: Menu double-press / hierarchy | MED | Likely fixed Phase E (S124) — needs playtest |
| B-60: Stray 'g'+'s' behind Video/Audio tabs | LOW | Visual glitch in Settings |
| B-90: Mission select shows all missions (no unlock filter) | MED | S131 playtest |
| B-91: Mission detail popup "(No objectives)" | HIGH | Objectives not loading from game data |
| B-92: Mouse not captured on solo mission start | HIGH | Solo path fixed (S131 menuhandlerAcceptMission). Co-op/MP/challenge siblings fixed S132. |
| B-93: Pause menu mostly empty | HIGH | Missing Abort, Restart, objective checklist |
| B-94: ImGui duplicate ID on pause menu hover | MED | Resume/Options need ##id suffixes |
| B-95: Update banner persists during gameplay | LOW | Should auto-dismiss during missions |
| B-96: Difficulty flow wrong in mission select | HIGH | Should be: pick mission → difficulty → objectives → Start |
| B-97: Special Assignments / Challenges not separated | LOW | Mixed into main mission list |
| B-98: Pause menu OG rendering fallback | HIGH | ImGui pause menu not fully implemented |
| B-99: Updater extraction may fail | MED | Needs retest with v0.0.25 binaries |

---

## Open Bug Fixes — Tier 2 (Fix Before Public Release)

| Bug | Description | File | Effort |
|-----|-------------|------|--------|
| **B-78** | Chat rebroadcast without rate limiting — DoS amplification | netmsg.c | S |
| **B-79** | Chunk ordering ignored in mod distribution — silent data corruption | netdistrib.c | M |
| **B-80** | archive_bytes not validated at BEGIN time (companion to B-74) | netdistrib.c | XS |
| **B-81** | JSON tokenizer unbounded recursion on deep nesting — crafted save crash | savefile.c | S–M |

---

## Open Bug Fixes — Tier 3 (Quality Pass)

| Bug | Description | File | Effort |
|-----|-------------|------|--------|
| **B-72** | SVC_LOBBY_STATE raw stagenum (display-only, LOW priority) | netmsg.c | S |
| **B-82** | Audio sample rate 22020 Hz (should be 22050?) | audio.c | S |
| **B-83** | Incomplete shutdown sequence on quit | main.c | M |
| **B-84** | Dead `tmp[1024]` variable in chat handler | netmsg.c | XS |
| **B-86** | enet_peer_send return value unchecked | netdistrib.c | XS |

---

## D3R Backlog

| Item | Status |
|------|--------|
| **D3R-7**: Modding Hub — 6 files | OPEN — needs build + playtest |
| **D3R-8**: Bot customizer Advanced toggle | OPEN |
| **Bot name dictionary** | **DONE (S144)** — 256-entry Adj+Noun dictionaries, mod-overridable (b92a421) |

---

## NEXT UP: Phase D5 — Full Menu System Replacement (D5 + D9 Merged)

**Settings half** (D5a–D5d): DONE. Audio sliders, video settings, controls rebinding — see [d5-settings-plan.md](d5-settings-plan.md).

**Menu system replacement** (D5.0–D5.8): PLANNED. Full plan: [designs/d5-ui-polish-plan.md](designs/d5-ui-polish-plan.md)

Infrastructure-first: build visual layer + input boundary before any individual screens.

### D5 Sub-phases

| Sub-phase | Description | Status |
|-----------|-------------|--------|
| **D5.0a** | Technical Spike — `pdguiGetUiTexture()` bridge, synthetic test pattern, `ImGui::Image()` in Catalog tab | **DONE (S135)** — compile clean, both targets. Playtest: open Settings > Catalog tab to see PASS label. |
| **D5.0** | Menu Visual Layer — `pdgui_theme` module, OG ROM textures via catalog (`ui/panels`, `ui/fx`, `ui/stars`, `ui/briefing`), scan-line pass; all menus use this as foundation | PLANNED — implement N64 decode in `buildTestPattern` replacement, then `pdguiThemeDrawPanel` etc. |
| **D5.1** | Input Ownership Boundary — MENU/GAMEPLAY modes in `pdmain.c`, Esc edge-detect, single canonical transition function; eliminates double-push, Tab conflicts, mouse capture timing | **DONE (S136)** — builds clean, commit 001dba8. Playtest: Tab no longer double-pushes menus, mouse captured on mission start. |
| **D5.3** | Pause Menu + Sub-screens — full ImGui pause (Objectives, Inventory, Restart, Abort), real renderer for `g_SoloMissionInventoryMenuDialog`, `##id` sweep; unblocks gameplay | PLANNED |
| **D5.2** | Mission Select Redesign — two-panel (list + detail), unlock filter, OG briefing images, star indicators from catalog, inline difficulty rows | PLANNED |
| **D5.4** | End Game Flow — MP match end scoreboard (S139: accuracy col, team sort, dual exit buttons, mouse fix). Endscreen lobby/quit buttons done (S144). Mission complete screen still PLANNED | PARTIAL (S144) |
| **D5.5** | Combat Sim Polish — bot head/body picker fixed (S138: `catalogGetBodyDefaultHead`); **bot name dictionary DONE** (S144: 256-entry Adj+Noun word lists, mod-overridable). Multi-select bot list done (S144). Arena/weapon set verification still open | PARTIAL (S144) |
| **D5.6** | Settings & QoL — layout sweep (zero hardcoded pixel offsets), update banner fix (B-95), scroll indicator UX | PLANNED |
| **D5.7** | Online Lobby Polish — disable unsupported tabs (Co-Op/Counter-Op/Solo), room nav cleanup, Quick Play button | PLANNED |
| **D5.8** | OG Menu Removal — systematic removal of all legacy screen render paths once ImGui replacements are verified | PLANNED |

**Execution order**: D5.0 → D5.1 → D5.3 → D5.2 → D5.4 → D5.5 → D5.6 → D5.7 → D5.8

---

## Backlog (priority order)

| Phase | Description | Status |
|-------|-------------|--------|
| **D13** | Update System — GitHub Releases API, SHA-256, self-replace | Code written (S50), needs libcurl + build test |
| **D14a** | Counter-Operative Mode — NPC possession mechanic | PLANNED |
| **D15** | Map Editor, Character Creator, Skin System | PLANNED |
| **D16** | Master Server / Server Pool | PLANNED (after content tools) |
| **R-2 through R-5** | Room Architecture — demand-driven rooms, protocol | R-1 done, **R-3 done (S143: clients see/create/join rooms, room-scoped match start)**, R-2/R-4/R-5 planned |
| **L-series** | Lobby/Room UX — social lobby, room create/join, interior | Depends on R-2/R-3 |
| **B-12 Phase 3** | Remove chrslots — dynamic participant system | Phase 1 coded (S26), next protocol bump |
