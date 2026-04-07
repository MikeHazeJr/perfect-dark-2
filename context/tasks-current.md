# Active Tasks -- Current Punch List

> Razor-thin: only what needs doing. For completed work, see [tasks-archive.md](tasks-archive.md).
> For phase status, see [infrastructure.md](infrastructure.md). For bugs, see [bugs.md](bugs.md).
> Back to [index](README.md)

---

## Recently Completed (S157–S170 — 2026-04-07)

| Item | Status |
|------|--------|
| **M1.2 — Solo Mission Flow (S170)** | **DONE** — B-122 fixed (endscreen mouse: deferred flush guard → `pdguiIsActive()`, per-frame `inputCtxSyncMouseMode()`, manual SDL calls removed from endscreen). B-124 fixed (Esc race: `push_tick` + `inputCtxShouldSuppressKey()` + 100ms grace period). Next Mission flow verified (catalog-first pattern confirmed working, B-123 fix solid). 5 files changed. |
| **M1.1 — Campaign Mission Select Redesign (S168)** | **DONE** — Two-panel layout: left=mission list (unlock filter, blip dots, chapter headings), right=detail (inline difficulty picker, objectives from game data, briefing preview, Start button). Single-screen flow replaces 3-dialog chain. New `soloLoadBriefingForStageId()` helper. B-90, B-91, B-96 all fixed. Also fixed missing `<string.h>` in bg.c/bodyreset.c from M0.1a. Build clean. |
| **D5 Phase 1 — Input Context Stack COMPLETE (S158–S161)** | **DONE** — Full pushdown automaton replacing binary INPUTMODE system. `inputctx.h` (103 lines) + `inputctx.c` (451 lines). 4 built-in contexts (Gameplay, ImGuiMenu, PauseMenu, DebugOverlay). `pdguiProcessEvent()` rewritten (110→44 lines). All 15 `pdmainSetInputMode()` callers migrated. `InputOwnerMode`/`g_InputMode`/`pdmainSetInputMode()` stripped. Lifecycle wired: init after inputInit, endFrame in gfx_sdl2 event loop, shutdown before pdguiShutdown. Build clean. |
| **D5 Phase 3 S1 — Solo Pause Menu (B-93, B-98) (S162)** | **DONE** — `pdgui_menu_solomission.cpp`: 5-button pause menu (Resume/Restart Mission/Inventory/Options/Abort), objectives checklist with difficulty-filtered completion icons (✓/✗/●), B-button/Escape cancel, D-pad wrap. Fixed objective loop starting at index 1 (not 0). `mainChangeToStage()` for restart. Build clean. |
| **B-119: stagenum=0x00 crash + catalog-first pattern (S165)** | **DONE** — `sm_missionconfig` shadow struct fixed (added `stage_id[64]`); mission select sets catalog ID only; `menuhandlerAcceptMission` + `menudialog00103608` resolve stagenum from stage_id at point of use. Universal catalog-first constraint added. Committed 5be1216. |
| **B-120: Wrong stage loaded for solo missions (S166)** | **DONE** — `catalogIdByRuntime(ASSET_MAP, X)` was given stagenum instead of stage table index. Fixed in both `pdgui_menu_solomission.cpp` and `mainmenu.c` via `bgGetStageIndex()` conversion. |
| **B-121: Endscreen menu not interactive (S166)** | **DONE** — Push `g_CtxImGuiMenu` on window appear in both solo and MP endscreen renderers. |
| **Networking: Client hole punch wired in (S157)** | **DONE** — All 3 client join sites use `netStartClientWithHolePunch()`. Waterfall confirmed working in playtest (direct→punch→retry). |
| **Server stage log cleanup (S157)** | **DONE** — Stage registration gated behind `g_NumStages > 0`. Server no longer logs "0 stages". |
| **extern "C" guards: fs.h + config.h (S157)** | **DONE** — Fixed linker errors from D5.0 commit. |
| **QUICKSTART.md created (S157)** | **DONE** — Comprehensive cold-start onboarding doc. Updated throughout session with constraints discovered. |
| **D5 Full Menu Overhaul design doc (S157)** | **DONE** — `context/designs/d5-full-menu-overhaul.md`. 5 phases, 25 sessions, 6500 LOC. |
| **Dev window: git identity auto-config + release auto-commit fix (S157)** | **DONE** — Startup sets user.email/user.name repo-level if missing. Auto-commit fails pipeline properly instead of silent swallow. |

---

## ACTIVE: D5 Full Menu Overhaul

**Master design doc**: `context/designs/d5-full-menu-overhaul.md` (includes binding UX guidelines)

---

## HIGH PRIORITY: System Design Guidelines

**Goal**: Each major infrastructure system gets a guideline document covering UX, architecture rules, and design constraints. These feed into a comprehensive game design document.

| System | File | Status |
|--------|------|--------|
| **Menu/UI** | `designs/d5-full-menu-overhaul.md` (UX Guidelines section) | **DONE (S161)** — controller nav, layout patterns, visual feedback, accessibility |
| **Input** | `designs/input-system-guidelines.md` | PLANNED — SSOT input system: tap/hold/double-tap recognition, per-context action maps, fully rebindable, replaces CK_* + ImGui hardcoded gamepad nav, absorbs inputmodes.c |
| **Networking** | `designs/networking-ux-guidelines.md` | PLANNED — connection flow, error UX, lobby behavior, NAT transparency |
| **Mod System** | `designs/mod-system-guidelines.md` | PLANNED — browser UX, creation workflow, theme customization, catalog integration |
| **Audio** | `designs/audio-guidelines.md` | PLANNED — menu sounds, feedback cues, music transitions, spatial audio |
| **Rendering/Visual** | `designs/visual-guidelines.md` | PLANNED — theme system, palette rules, PD-authentic styling, resolution scaling |
| **Collision/Physics** | `designs/physics-guidelines.md` | PLANNED — capsule sweep, ground detection, coyote time, movement feel |
| **Level Editor (Forge)** | `designs/forge-guidelines.md` | PLANNED — tool layout, creation workflow, testing loop, sharing |

---

## Backlog: Dev Window Redesign

**Goal**: Modernize the dev window layout following the same UX guidelines as in-game menus. Keep release/version functionality unchanged.

| Item | Priority | Detail |
|------|----------|--------|
| **Visual layout redesign** | MED | Follow menu UX guidelines: clear hierarchy, consistent sizing, labels left/controls right, adequate padding. Current layout is functional but organic/cluttered. |
| **Smart builds** | MED | Incremental builds when clean isn't required. Detect when CMake cache is stale (source changes, CMakeLists.txt modified, compiler version changed) vs valid. Only clean when necessary. Skip configure if nothing changed since last configure. |
| **Clean configure** | MED | Ensure CMake configure doesn't leave stale cache values. Currently the "clean build" toggle was removed (S50) — every build deletes build dirs. Smart builds would restore incremental as default, clean on demand. |
| **PRUNE WORKTREES button** | **DONE (S161)** | Gold-bordered button in link panel. Prunes registry, removes orphaned dirs, deletes claude/* branches. |

**Next action**: Create these as sessions allow, before implementation of each system. Menu/UI is the template.

---

| Phase | Status | Detail |
|-------|--------|--------|
| **Phase 1 — Input Context Stack** | **DONE (S158–S161)** | Stack API, 4 contexts, lifecycle wired, old system stripped. Playtest confirmed working. |
| **Phase 2 — Controller Navigation** | **DONE (S162–S163)** | Nav module (pdgui_nav.h/c): device detection, wrap callback, A/B via ImGui nav. LB/RB tab switching wired into main menu + room menu. Safe area (pdguiGetSafeArea) with per-edge margins, ultrawide auto-detect, pd.ini persistence. Input SSOT design spec committed for future unification (tap/hold/double-tap, per-context action maps, replaces CK_* + ImGui gamepad nav). |
| **Phase 3 — Full Menu Roster Port** | **IN PROGRESS** | 120 screens total, 61 remaining. **S162 (2026-04-06): Solo Pause Menu (B-93, B-98) DONE** — 5 buttons (Resume/Restart/Inventory/Options/Abort), objectives checklist with completion icons, difficulty filtering, B-button cancel, D-pad wrap. Build clean. |
| **Phase 4 — Theme System** | PLANNED | Auto-extract base-ui textures at runtime (no CLI flag). Mod themes selectable in settings. Debug menu rebuild. ~3 sessions. |
| **Phase 5 — Planned Features** | PLANNED | Player portraits, lobby scene with connected players, character preview in selection. ~4 sessions. |

### Playtest Findings (S161 — 2026-04-06 evening)

| Finding | Severity | Detail |
|---------|----------|--------|
| **B-117: Crash on match exit** | **HIGH** | Hard crash — no shutdown sequence in log. Log ends at `CHAT: NET: disconnected` with pause context still pushed. Possibly B-112 related or match→lobby transition crash. |
| **Menu opacity stacking** | **MED** | Main menu background gets more opaque after repeated open/close cycles. Haze overlay likely compositing additively without full reset. |
| **JUMP_LANDING log spam** | **LOW** | Every frame during pause logs ground clamp. Gate behind verbose mode. |
| **First hole punch attempt fails, second direct succeeds** | **INFO** | UPnP mapping wasn't complete during first attempt. Waterfall logic correct — direct→punch→fail→retry. Second attempt connected via direct in 50ms. |
| **base-ui textures missing** | **KNOWN** | mods/base-ui/ not in build output. Phase 4 auto-extract fix planned. Procedural fallbacks working. |

---

## ACTIVE: Catalog ID Deep Migration (M0.1)

**Goal**: Zero integer-to-catalog-ID conversion anywhere. Catalog ID is sole identity for all asset types.

| Sub-phase | Status | Detail | Unblocks |
|-----------|--------|--------|----------|
| **M0.1a — Stage signatures** | **DONE (S167)** | `g_SoloStages[]` catalog-native, endscreen, bg.c, mplayer.c, ingame.c, bodyreset.c all converted. Commit `270d57c`. | M1 (Campaign) |
| **M0.1b — Body/Head signatures** | **DONE (S169)** | All 6 named conversion wrappers eliminated. 5 already deleted, 3 safe-body/head functions made static (zero external callers). String-based validators are the public API. ~30 `catalogMpBodyId/HeadId` enumeration calls remain (display helpers, not identity wrappers — tracked under Gameplay state). | M2 (Combat Sim chars) |
| **M0.1c — Weapon signatures** | NOT STARTED | Weapon select, equip, fire — catalog ID at boundaries. | M2 (Combat Sim weapons) |
| **M0.1d — Remaining asset types** | NOT STARTED | Texture, audio, animation, gamemode, lang, prop, HUD (Phases 9–14). | M4 (Mod Platform) |
| **M0.1e — Catalog as data provider** | NOT STARTED | Absorb ROM arrays; catalog serves weapon/body/head data directly. | M5 (Forge) |
| **Phase 7 — Wrapper caller elimination** | **DONE (S169)** | All conversion wrappers eliminated or internalized. Zero external callers of any integer-based body/head conversion function. |
| **Gameplay state — category-based** | NOT STARTED | Runtime integer identity in match/bot/weapon state. |

---

## ACTIVE: Catalog ID Deep Migration

**Goal**: Zero integer-to-catalog-ID conversion anywhere in the codebase. Catalog ID is sole identity for all asset types.

| Phase | Status | Detail |
|-------|--------|--------|
| **Phases 0–6** | **DONE** | Identity layer: generation counter, hot-reload API, catalog ID fields, function APIs, integer comparisons, UI shadow structs, save paths, lobby accessors. 41 files, 837 insertions. |
| **Phase 7 — Conversion function wrapper elimination** | **DONE (S169)** | All conversion wrappers eliminated. `catalogBodynumToMpBodyIdx`/`catalogHeadnumToMpHeadIdx`/`catalogResolveBodyByMpIndex`/`catalogResolveHeadByMpIndex`/`catalogResolveWeaponByGameId` deleted (prior sessions). `catalogGetSafeBody`/`catalogGetSafeHead`/`catalogGetSafeBodyPaired` made static in modelcatalog.c (zero external callers). String-based validators (`catalogValidateBodyId`/`catalogValidateHeadId`/`catalogValidateBodyIdPaired`) are the public API. |
| **Phase 8 — O(n) conversion elimination** | **DONE (S157)** | All linear-scan conversion functions eliminated. |
| **Triple audit** | **PASSED (11/11)** | All original audit findings verified. 1 gap fixed. |
| **Deep audit — direct array access** | **DONE (S157)** | All 15 bypass items fixed. 2 hidden reimplementations removed. Zero gaps. |
| **Phases 9–14** | **NOT STARTED** | Texture, audio, animation, gamemode, lang, prop, HUD migration. |
| **Catalog as data provider** | **NOT STARTED** | Absorb ROM arrays; catalog serves weapon/body/head data directly. |
| **Gameplay state — category-based** | **NOT STARTED** | Match state, bot config, weapon slots use integer identity at runtime. |

**Next action**: M0.1c (weapon signatures) or Gameplay state migration (PlayerConfig/BotConfig structs to store catalog ID strings natively, eliminating `catalogMpBodyId`/`catalogMpHeadId` enumeration calls).

---

## Previously Completed (S130–S153 — 2026-04-02/05)

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
| **S131 cleanup: strcpy→strncpy (input.c), MATCH_MAX_SLOTS canon, field renames** | **DONE (S153)** — 10 bare strcpy in `port/src/input.c` converted; local `#define MATCH_MAX_SLOTS 32` removed from 3 UI files + `scenario_save.h`; all now use canonical 40 from `matchsetup.h`; stale `headnum`/`bodynum` → `body_id`/`head_id` fixed in mpsettings + teamsetup. Commit 05d5f1d. |
| **U-7: matchsetup.cpp retired (Lobby Unification Phase 2)** | **DONE (S153)** — Steps A–D complete: `arenaGetName()` + override table relocated to room.cpp; advanced bot trait sliders added to bot modal; 3D char preview ported (rotating, two-column layout, pdguiCharPreview pipeline); 4 `g_MatchSetupMenuDialog` push points redirected to `g_CombatSimulatorMenuDialog` + `pdguiSoloRoomOpen()`; file renamed `.cpp.retired`. Commit 9fe169e. |
| **U-10: Deferred bot authority (Lobby Unification Phase 4)** | **DONE (S153)** — `g_NetPendingBotAuthority` flag added in `net.h`/`net.c`; `netmsgSvcBotAuthorityRead` sets pending instead of active; `botTick` promotes to active when `g_PadsFile != NULL && g_NumSpawnPoints > 0`; reset on disconnect and match-end. Commit 6f471a7. |
| **B-116: Bot body/head catalog ID resolution in SVC_STAGE_START + netmanifest** | **DONE (S153)** — SVC_STAGE_START writer used wrong slot index (`botidx + MAX_PLAYERS`) for catalog ID lookup; fix: pre-built `botSlotMap[]` from actual `SLOT_BOT` entries in `g_MatchConfig`; server manifest builder reads `body_id`/`head_id` from `g_MatchConfig.slots[]` directly. Committed (not pushed). |
| **Git repo recovery + cleanup** | **DONE (S153)** — `.git` was missing `objects/`; full history fetched from GitHub; `.git.broken` (200MB) + 7 orphaned worktrees (~23GB) cleaned up; `dev` pushed to origin. |

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
| ~~All bots get dark_combat body (invisible)~~ | ~~CRIT~~ | **Fixed S151** — g_MatchConfig.slots not populated in CLC_LOBBY_START. MATCH_MAX_SLOTS 32→40. |
| ~~Prop resync spam (0 props every 6s)~~ | ~~HIGH~~ | **Fixed S151** — Desync counter reset on 0-prop receive. Full event-driven sync TBD. |
| ~~Death-in-hub crash (stagenum=0x00)~~ | ~~HIGH~~ | **Fixed S151** — titleSetNextStage guards against 0x00, redirects to CI. |
| ~~Bot HP too low in local (maxdamage=4)~~ | ~~MED~~ | **Fixed S151** — botmgrAllocateBot sets maxdamage=8.0f. |
| ~~B-114: CI crash frame 1 after mission fail exit~~ | ~~HIGH~~ | **Fixed S152** — screenManifestTick deferred until lvframe60>=2 (catalogUnloadAsset during catalog reinit). SDL flush deferred to lvframe60>0. |
| ~~B-116: Bot body/head wrong catalog IDs on dedicated server~~ | ~~HIGH~~ | **Fixed S153** — SVC_STAGE_START used wrong slot index; pre-built `botSlotMap[]` + direct `slots[]` read in netmanifest.c. |
| ~~B-115: Post-game menu mouse unresponsive~~ | ~~MED~~ | **Fixed S170** — Same root cause as B-122: deferred flush guard missed imgui_menu context. Systemic fix covers both. |
| **Prop sync not event-driven** | **MED** | Current prop sync uses CRC polling. Should fire on pickup/door events per game director direction. |
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

## Lobby Unification (Solo + Online) — **COMPLETE (S153)**

See full task list: [tasks-lobby-unification.md](tasks-lobby-unification.md)

All 10 items (U-1 through U-10) complete. Feature gaps closed, `pdgui_menu_matchsetup.cpp` retired, network sync verified on-wire, bot spawn race root-caused and fixed with client-side deferred bot authority. B-116 (bot catalog ID fix in SVC_STAGE_START + netmanifest) also landed as companion work.

---

## Phase D5 — Full Menu System Replacement (D5 + D9 Merged)

**Settings half** (D5a–D5d): DONE. Audio sliders, video settings, controls rebinding — see [d5-settings-plan.md](d5-settings-plan.md).

**Menu system replacement** (D5.0–D5.8): PLANNED. Full plan: [designs/d5-ui-polish-plan.md](designs/d5-ui-polish-plan.md)

Infrastructure-first: build visual layer + input boundary before any individual screens.

### D5 Sub-phases

| Sub-phase | Description | Status |
|-----------|-------------|--------|
| **D5.0a** | Technical Spike — `pdguiGetUiTexture()` bridge, synthetic test pattern, `ImGui::Image()` in Catalog tab | **DONE (S135)** — compile clean, both targets. Playtest: open Settings > Catalog tab to see PASS label. |
| **D5.0** | Menu Visual Layer — `pdgui_theme` module, OG ROM textures via catalog, scan-line pass, haze overlay, multi-palette | **DONE (S157)** — Init ordering fix, ROM extraction tool, base-ui mod (13 textures), haze overlay, CRT scanlines, all 7 palettes drive theme. Procedural modern-UI mod. Commit `a040275`. Awaiting build verification. |
| **D5.1** | Input Ownership Boundary — MENU/GAMEPLAY modes in `pdmain.c`, Esc edge-detect, single canonical transition function; eliminates double-push, Tab conflicts, mouse capture timing | **DONE (S136)** — builds clean, commit 001dba8. Playtest: Tab no longer double-pushes menus, mouse captured on mission start. |
| **D5.3** | Pause Menu + Sub-screens — full ImGui pause (Objectives, Inventory, Restart, Abort), real renderer for `g_SoloMissionInventoryMenuDialog`, `##id` sweep; unblocks gameplay | PLANNED |
| **D5.2** | Mission Select Redesign — two-panel (list + detail), unlock filter, OG briefing images, star indicators from catalog, inline difficulty rows | PLANNED |
| **D5.4** | End Game Flow — MP match end scoreboard (S139: accuracy col, team sort, dual exit buttons, mouse fix). Endscreen lobby/quit buttons done (S144). Mission complete screen still PLANNED | PARTIAL (S144) |
| **D5.5** | Combat Sim Polish — bot head/body picker fixed (S138: `catalogGetBodyDefaultHead`); **bot name dictionary DONE** (S144: 256-entry Adj+Noun word lists, mod-overridable). Multi-select bot list done (S144). Arena/weapon set verification still open | PARTIAL (S144) |
| **D5.6** | Settings & QoL — layout sweep (zero hardcoded pixel offsets), update banner fix (B-95), scroll indicator UX | PLANNED |
| **D5.7** | Online Lobby Polish — disable unsupported tabs (Co-Op/Counter-Op/Solo), room nav cleanup, Quick Play button | PLANNED |
| **D5.8** | OG Menu Removal — systematic removal of all legacy screen render paths once ImGui replacements are verified | PARTIAL — `pdgui_menu_matchsetup.cpp` retired (S153, renamed `.cpp.retired`); remaining legacy C menu paths still PLANNED |

**Execution order**: D5.0 → D5.1 → D5.3 → D5.2 → D5.4 → D5.5 → D5.6 → D5.7 → D5.8

---

## Movement / Physics Backlog (Post-Menu Stability)

| Item | Priority | Detail |
|------|----------|--------|
| **Coyote time + jump buffering** | MED | Allow ~250ms jump buffer (queue jump before landing) + ~250ms coyote time (jump briefly after leaving edge). Event-driven, not polling. Reference: Celeste's implementation (in