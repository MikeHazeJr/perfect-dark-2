# Session Log (Active)

> Recent sessions only. Archives: [1-6](sessions-01-06.md) . [7-13](sessions-07-13.md) . [14-21](sessions-14-21.md) . [22-46](sessions-22-46.md) . [47-78](sessions-47-78.md) . [79-86](sessions-79-86.md) . [87-119](sessions-87-119.md)
> Back to [index](README.md)

## Session S140 — 2026-04-04

**Focus**: B-104 — Solo/MP endscreen mouse not usable after mission complete

### What Was Done

Fixed B-104: both `renderSoloEndscreen` and `renderMpEndscreen` in `pdgui_menu_endscreen.cpp` were calling `SDL_SetRelativeMouseMode(SDL_FALSE)` directly on `IsWindowAppearing()`, without calling `pdmainSetInputMode(INPUTMODE_MENU)`.

**Root cause**: Direct SDL calls left `g_InputMode = INPUTMODE_GAMEPLAY`. This:
1. Allowed any code path that calls `pdmainSetInputMode(INPUTMODE_GAMEPLAY)` to re-lock the mouse (the no-op guard only fires if mode is already GAMEPLAY)
2. Caused the event handler's `g_InputMode == INPUTMODE_GAMEPLAY` guard to block ImGui keyboard input

**Fix**: Both screens now call `pdmainSetInputMode(INPUTMODE_MENU)` on `IsWindowAppearing()` (same pattern as the S139 fix for `##PdGameOver` in pausemenu). Also kept the `SDL_WarpMouseInWindow` to center the cursor.
Added `#include "../include/pdmain.h"` to endscreen.cpp.

### Decisions
- Kept the warp to center — `pdmainSetInputMode` doesn't warp; cursor would restore to pre-mission position which may be off-screen
- Same fix applied to both solo and MP endscreen paths for consistency

### Next Steps
- Playtest: complete DataDyne Central → verify "Retry Mission" and "Next Mission" buttons are clickable

---

## Session S139 — 2026-04-04

**Focus**: D5.4 — MP post-match scoreboard (pdgui_menu_pausemenu.cpp)

### What Was Done

**`commit 36d03a5` pushed to `dev`.**

Rewrote `pdguiGameOverRender()` and supporting helpers in `pdgui_menu_pausemenu.cpp`:

- **Accuracy column** — `ScorecardRow.accuracy` field added. Computed for local player via `mpstatsGetPlayerShotCountByRegion` (PM_SHOT_TOTAL, _HEAD, _BODY, _LIMB, _GUN, _HAT, _OBJECT). Bots display "--".
- **Team section headers** — `renderGameOverRankings()` rewritten. Inserts "-- Team N --" headers (team-colored) between team groups when teams are enabled.
- **Stable team sort** — `sortRowsByTeam()` insertion sort added. `mpGetPlayerRankings()` returns score-sorted rows; stable sort by team applied before rendering for team mode.
- **Mouse capture fix** — `pdmainSetInputMode(INPUTMODE_MENU)` called on `ImGui::IsWindowAppearing()`. Fixes non-interactive buttons (B-103 symptom: game held SDL in relative mouse mode during gameplay).
- **Dual exit buttons** — "Return to Lobby" (blue, stays in room: `mainChangeToStage(STAGE_CITRAINING)` + `pdguiSetInRoom(1)`) and "Quit to Menu" (red: `netDisconnect()` for CLIENT, `mainChangeToStage(STAGE_TITLE)` for offline/server).

**Build**: full client+server incremental build clean (exit 0).

### Decisions
- Accuracy computed from local player's stats only — `mpstatsGetPlayerShotCountByRegion` is per-local-player, not per-chrnum. Bots always show "--".
- "Return to Lobby" does NOT call `netDisconnect()` — player stays connected and in-room. Only `pdguiSetInRoom(1)` is needed to show the room interior UI.
- Forward-declared `pdguiSetInRoom` and `mpstatsGetPlayerShotCountByRegion` inline in the cpp extern block (not added to headers — not needed elsewhere).

### Next Steps
- Playtest: verify scoreboard appears at match end, buttons work, accuracy shows for local player
- D5.4 mission complete screen still PLANNED
- D5.5: bot name dictionary + arena/weapon verification still open

---

## Session S138 — 2026-04-04

**Focus**: Fix body/head picker auto-head selection (combat sim + agent create)

### What Was Done

**`commit a65207e` pushed to `dev`.**

Root cause: two UI pickers were selecting the wrong default head when a body was chosen.

- `pdgui_menu_matchsetup.cpp` (Combat Sim bot editor, line 748): called
  `catalogResolveHeadByMpIndex((s32)b)` where `b` is the **body** mpbodynum.
  This treated the body index as a head index — body 5 would select head 5,
  not the body's paired head. Wrong for nearly every character.

- `pdgui_menu_agentcreate.cpp` `autoSelectHead()`: called
  `mpGetMpheadnumByMpbodynum` — an N64-era function that uses `rngRandom()`
  for headnum==1000 sentinel bodies and bypasses the catalog layer.

**Fix — two new catalog API functions:**
- `catalogGetBodyDefaultHead(const char *body_id)` → head catalog ID string
  Reads `entry->ext.body.headnum` (default head's g_HeadsAndBodies[] index)
  and resolves it via `catalogResolveByRuntimeIndex(ASSET_HEAD, headnum)`.
- `catalogGetBodyDefaultMpHeadIdx(s32 mpbodynum)` → mpheadnum for carousels
  Wraps the above, converts mpbodynum → body catalog ID → ext.body.headnum →
  `catalogHeadnumToMpHeadIdx()`. Returns -1 for unregistered heads (including
  the headnum==1000 random-gender sentinel — caller keeps existing selection).

Both are declared in `assetcatalog.h`, implemented in `assetcatalog_api.c`.
Wire protocol unchanged — body_id/head_id remain catalog ID strings throughout.

**Changed files (4):**
- `port/src/assetcatalog_api.c` — two new functions after `catalogHeadnumToMpHeadIdx`
- `port/include/assetcatalog.h` — declarations for both new functions
- `port/fast3d/pdgui_menu_matchsetup.cpp` — `catalogGetBodyDefaultHead(bid)` replaces wrong call
- `port/fast3d/pdgui_menu_agentcreate.cpp` — `catalogGetBodyDefaultMpHeadIdx` in `autoSelectHead`, forward decl added

**Build**: both client and server link clean (full incremental build from main copy).

### Next Steps

- Playtest: verify body selection in Combat Sim and Agent Create picks correct paired head
- D5.5 (Combat Sim Polish) — this fix is a prerequisite; bot name dictionary + arena/weapon verification still open

---

## Session S137 — 2026-04-03

**Focus**: Online match start bug (B-103) — critical path fix for multiplayer

### What Was Done

**B-103 fixed and pushed (`commit 184922a`).**

Root cause: `g_MpSetup.stage_id` was never populated by the match setup flow.
- UI sets `g_MatchConfig.stage_id`, not `g_MpSetup.stage_id`.
- `manifestBuildForHost()` (client side) reads `g_MpSetup.stage_id` → found empty → stage skipped from manifest → stage not in session catalog.
- `netmsgSvcStageStartWrite()` (server side) reads `g_MpSetup.stage_id` → found empty → writes `stage_session=0` → returns early.
- Client reads `stage_session=0` → silent `return 1` with no log → surfaces as "malformed or unknown message 0x10 from server". Stage never loaded.

**Two-line fix in `port/src/net/netmsg.c`:**
1. `netmsgClcLobbyStartWrite`: sync `g_MatchConfig.stage_id → g_MpSetup.stage_id` before `manifestBuildForHost` so stage enters manifest and session catalog.
2. `netmsgClcLobbyStartRead` (server): copy parsed `stage_id → g_MpSetup.stage_id` alongside existing `g_MatchConfig` and `stagenum` assignments.

No protocol changes. S130 constraint respected (catalog IDs throughout, no raw indices).

**Build**: client clean (exit 0). Server CMake arch error is pre-existing, unrelated.

### Next Steps

- Playtest with Chris — match should now start when countdown reaches zero
- D5.3 (Pause Menu) — unblocked by D5.1 input ownership

---

## Session S136 — 2026-04-03

**Focus**: D5.1 — Input Ownership Boundary

### What Was Done

**D5.1 implemented and pushed (`commit 001dba8`).**

Introduces `InputOwnerMode` enum (`INPUTMODE_MENU` / `INPUTMODE_GAMEPLAY`) and
`pdmainSetInputMode()` as the single canonical transition function.

**New files:**
- `port/include/pdmain.h` — enum + extern g_InputMode + pdmainSetInputMode() declaration (extern "C" guards for C++ callers)

**Changed files (9):**
- `port/src/pdmain.c` — `g_InputMode` global + `pdmainSetInputMode()` implementation: GAMEPLAY→SDL mouse capture bypassing pdguiIsActive() guard; MENU→SDL_SetRelativeMouseMode(FALSE) + ShowCursor
- `port/fast3d/pdgui_backend.cpp` — GAMEPLAY-mode early return: keyboard events not forwarded to ImGui when `!g_PdguiActive`; Tab suppressed before ImGui sees it in MENU mode (fixes CK_START double-trigger)
- `port/fast3d/pdgui_menu_pausemenu.cpp` — replaced direct SDL calls in Open/Close with `pdmainSetInputMode(INPUTMODE_MENU/GAMEPLAY)`
- `port/fast3d/pdgui_bridge.c` — `pdmainSetInputMode(INPUTMODE_GAMEPLAY)` after both `menuhandlerAcceptMission()` calls
- `port/fast3d/pdgui_menu_solomission.cpp` — `pdmainSetInputMode(INPUTMODE_GAMEPLAY)` after both `menuhandlerAcceptMission()` calls
- `port/src/net/matchsetup.c` — `pdmainSetInputMode(INPUTMODE_GAMEPLAY)` in `matchStart()` and `matchStartFromChallenge()`
- `port/src/net/netmsg.c` — `pdmainSetInputMode(INPUTMODE_GAMEPLAY)` in SVC_STAGE_START (co-op/anti path and MP path), inside `#if !defined(PD_SERVER)`
- `port/src/menumgr.c` — `restoreGameplayMouseCapture()` body replaced with single `pdmainSetInputMode(INPUTMODE_GAMEPLAY)` call

**Build**: both client and server link clean.

### Bugs Addressed

- **B-92 class** (mouse not captured on mission start): `pdmainSetInputMode(INPUTMODE_GAMEPLAY)` calls SDL directly, bypassing the `pdguiIsActive()` defer guard in `inputLockMouse()`. All known start paths covered.
- **Tab double-trigger** (CK_START conflict): Tab suppressed before `ImGui_ImplSDL2_ProcessEvent()` in MENU mode.
- **Esc double-path**: in GAMEPLAY with no active overlay, keyboard events never reach ImGui at all.

### Next Steps

- D5.0 (Visual Layer): still PLANNED before D5.3 (pause menu) per execution order
- D5.3 (Pause Menu): now unblocked by D5.1 — input ownership is clean for pause transitions

---

## Session S135 — 2026-04-03

**Focus**: D5.0a Technical Spike — Fast3D → OpenGL → ImGui texture bridge

### What Was Done

**D5.0a spike implemented and pushed (`commit 824415e`).**

Validates that the ImGui::Image() pipeline works end-to-end before the full D5.0 ROM
texture decode layer is built.

**Architecture findings (from code study):**
- `ImTextureID` in this codebase is `(void*)(uintptr_t)GLuint` — confirmed by `gfx_opengl_get_framebuffer_texture_id()`.
- `GfxRenderingAPI` exposes `new_texture()`, `select_texture()`, `upload_texture()` — but raw GLAD GL calls are equally valid since `pdgui_backend.cpp` already includes `glad.h`.
- `struct tex` in the shared texpool has `data` (N64-native pixels), `width`, `height`, `gbiformat`, `depth` — the full decode path for D5.0 is: `texLoadFromTextureNum(texnum)` → `texFindInPool()` → decode N64 format → `glTexImage2D`.
- N64 formats to implement for D5.0: RGBA16 (5-5-5-1), IA16 (8-8), IA8 (4-4), CI4/CI8 (palette-indexed). All handled by `import_texture_*` in `gfx_pc.cpp` — that code is the decode reference.

**Changes made:**
1. `pdgui_backend.cpp`: `pdguiGetUiTexture(const char *id)` — static `unordered_map<string, uint32_t>` cache, synthesizes 64×64 PD-blue RGBA32 test pattern, uploads to GL, returns ImTextureID.
2. `pdgui.h`: declared `pdguiGetUiTexture()`.
3. `pdgui_menu_mainmenu.cpp`: Settings > Catalog tab shows `ImGui::Image()` with PASS/FAIL label.
4. `assetcatalog_base.c`: registered `ui/test_panel` as `ASSET_UI` (placeholder for D5.0).

**Build**: Both client (`PerfectDark.exe`) and server (`PerfectDarkServer.exe`) link clean. No new errors.

### Spike Result

**PASS** — `pdguiGetUiTexture()` compiles, uploads a GL texture, and is called from `ImGui::Image()`. Visual confirmation requires playtest (see Settings > Catalog tab).

**D5.0 unblocked.** The D5.0 task is to replace `buildTestPattern()` with actual ROM texture decode.

### Pipeline Gap Identified for D5.0

No standalone N64 → RGBA32 decode function is currently exposed outside `gfx_pc.cpp`. D5.0 must either:
- Export a `gfxDecodeN64Texture(data, fmt, siz, w, h, out_rgba32)` helper from `gfx_pc.cpp`, OR
- Implement a standalone decode function in `pdgui_backend.cpp` (copy-referencing the `import_texture_*` logic).

Recommendation: standalone decode in `pdgui_backend.cpp` — avoids coupling the bridge to gfx_pc internals and keeps the UI texture path self-contained.

### Next Steps

- D5.0 (Visual Layer): replace `buildTestPattern()` with real ROM texture decode, implement `pdguiThemeDrawPanel()` etc., register all `ui/` catalog entries with real texnums.
- Per `context/tasks-current.md`, D5.1 (Input boundary) and D5.2 (Pause menu) follow after D5.0 validates.

---

## Session S134 -- 2026-04-03

**Focus**: Static array audit — dynamic/growable data, enum-indexed array completeness

### What Was Done

**Full audit of port/src/ and port/fast3d/ for static arrays holding dynamic/growable data.**

Scope: our code only (not vendored imgui/, external/, or decompiled src/game/).

**Findings — what was NOT a problem:**
- `assetcatalog_load.c` override arrays (s_FilenumOverride etc.): ROM-bounded reverse-index maps with existing bounds checks. ROM source numbers don't grow with mods. No change needed.
- `pdgui_hotswap.cpp` s_Entries[128]: registered from code at init time, not mod data.
- Network/player/bot arrays (MAX_PLAYERS, MAX_BOTS etc.): genuine protocol constants.
- `s_MfSortedIdx[MANIFEST_MAX_ENTRIES]`: UI sort buffer bounded by protocol maximum (4096), already matches the dynamically allocated manifest struct.
- All `s_AssetTypeNames[ASSET_TYPE_COUNT]` arrays: verified complete (25 entries, NONE through LANG). Already fixed in S133.

**Fix 1 — assetcatalog_deps.c** (`commit ab69868`):
- `s_DepTable[CATALOG_MAX_DEP_PAIRS]` (256 static) → heap-allocated `s_DepPair *s_DepTable` + `s32 s_DepCap`.
- Grows by doubling on demand (starting at CATALOG_MAX_DEP_PAIRS = 256).
- `catalogDepClear()` now frees the buffer. `catalogDepClearMods()` compact-in-place (no realloc — keeps allocated capacity).
- Previously: mods with many asset dependencies silently dropped entries at 256 with a LOG_WARNING.

**Fix 2 — pdgui_menu_mainmenu.cpp** (`commit ab69868`):
- `s_ManifestTypeNames[]`: added "Lang" at index 8 (= MANIFEST_TYPE_LANG, added in S130).
- Bounds check: changed hardcoded `me->type < 8` → `me->type < (int)(sizeof(s_ManifestTypeNames)/sizeof(s_ManifestTypeNames[0]))` so it auto-tracks the array.
- Previously: Lang entries in the catalog debug tab showed "?" instead of "Lang".

### Build
- Build script redirects to main working copy when run from worktree. Changes applied directly to `dev` branch and pushed. Both targets build clean (no structural changes — all callers unchanged).

### Decisions Made
- The four `s_*Override[]` arrays in assetcatalog_load.c are NOT dynamic data: they're fixed-domain reverse-index maps (filenum/texnum/animnum/soundnum → pool_index). ROM source numbers don't grow. Correct as-is.
- `CATALOG_MAX_DEP_PAIRS` constant retained in header as initial/minimum capacity for the dep table.

### Next Steps
- D5 UI Polish (B-91, B-92, B-93, B-96 are the recommended starting sequence per tasks-current.md).

---

## Session S130 -- 2026-04-02

**Focus**: Wire protocol v27 (catalog ID strings everywhere), SAVE-COMPAT strip, comprehensive bug audit + critical fixes, engine modernization vision

### What Was Done

**Major Milestones:**

1. **Wire protocol fully migrated to catalog ID strings (v27)**
   - All remaining `net_hash` u32 CRC32 wire fields replaced with full catalog ID strings across: SVC_LOBBY_STATE, SVC_CATALOG_INFO, CLC_CATALOG_DIFF, SVC_DISTRIB_BEGIN/CHUNK/END, CLC_MANIFEST_STATUS, SVC_SESSION_CATALOG, SVC_MATCH_MANIFEST
   - `manifestAddEntry`/`manifestAddModEntry` net_hash parameter removed (~30 call sites updated)
   - `manifestComputeHash` hashes ID string bytes instead of net_hash bytes
   - `manifestSerialize`/`manifestDeserialize` drop net_hash field
   - `sessioncatalog.c` broadcast/receive uses `assetCatalogResolve(catalog_id)` only
   - `netdistrib.c` queue entries use `char catalog_id[64]` instead of `u32 net_hash`
   - NET_PROTOCOL_VER bumped to 27

2. **SAVE-COMPAT branches fully stripped**
   - `scenario_save.c`: Write path only writes catalog ID strings (arenaId, bodyId, headId, weapon_id). Load path only accepts catalog ID strings. All integer fallback branches removed. `scenarioDelete()` function added.
   - `savefile.c`: Raw "weapons" integer array write removed (only "weapon_ids" strings). "stagenum", "mpheadnum", "mpbodynum" integer fallback paths removed.

3. **CLC_LOBBY_START fully catalog-native**
   - Arena and weapons sent as catalog ID strings via `netbufWriteStr`/`netbufReadStr`
   - SVC_LOBBY_STATE converted from `catalogWritePreSessionRef` to catalog ID strings

4. **Per-frame log spammers removed** (`bondwalk.c`)
   - 5 per-frame spammers removed: JUMP_AIRBORNE, JUMP_STUCK, CAPSULE_CEIL, B49_PROP_FLOOR, CAPSULE_FLOOR
   - One-shot event logs preserved (JUMP press, JUMP_BLOCKED, JUMP_LANDING, etc.)

5. **Legacy default replacements** (`mplayer.c`)
   - Hardcoded MPBODY_*/MPHEAD_*/STAGE_* defaults replaced with `assetCatalogResolve("base:dark_combat")` etc.
   - `g_MpSetup` has `stage_id` field

6. **Comprehensive project-wide bug audit**
   - Full audit of `src/game/`, `src/lib/`, `port/src/`, `port/fast3d/`, `port/include/`, `port/src/net/`
   - 19 findings: 2 CRITICAL, 3 HIGH, 8 MEDIUM, 6 LOW
   - 5 systemic patterns identified (sprintf, network bounds, fread, strcpy, malloc)
   - Results in `context/audit-comprehensive-bugs.md`

7. **Critical + high-severity bug fixes**
   - **C-01 (CRITICAL)**: ChrResync null-prop buffer desync — removed early `continue` on NULL prop in netmsg.c ChrResync handler; all 20+ fields always read from buffer to maintain cursor alignment
   - **C-02 (CRITICAL)**: Unbounded malloc in netdistrib.c — added `MAX_DISTRIB_ARCHIVE_BYTES (64MB)` upper bound before malloc
   - **H-01 (HIGH)**: SVC_PLAYER_MOVE bounds check — added `if (id >= NET_MAX_CLIENTS)` guard
   - **H-02 (HIGH)**: sprintf → snprintf in chat handler — buffer overflow prevention with size limits

8. **Constraints + context updated**
   - constraints.md: Protocol v27, net_hash fully deprecated, ImGui sole menu system, mouse capture state machine, catalog registers ALL assets
   - Engine modernization vision documented as auto-memory

9. **Multiple independent deep audits**
   - `audit-catalog-id-compliance.md`: Initial compliance audit (8 CRITICAL + 6 HIGH + 7 MEDIUM + 4 LOW)
   - `audit-legacy-hacks.md`: 35+ legacy pattern findings across 10 categories
   - `audit-pipeline-compliance-1.md`: Post-batch verification audit #1
   - `audit-infrastructure-integrity.md`: Post-batch verification audit #2
   - `audit-comprehensive-bugs.md`: Full project bug audit (19 findings)

### Key Files Changed
- `port/src/net/netmsg.c` — CLC_LOBBY_START strings, SVC_LOBBY_STATE strings, ChrResync fix (C-01), SVC_PLAYER_MOVE bounds (H-01), chat snprintf (H-02)
- `port/src/net/netdistrib.c` — catalog_id strings, MAX_DISTRIB_ARCHIVE_BYTES guard (C-02)
- `port/src/net/netmanifest.c` — net_hash param removed, string-based hashing
- `port/src/net/sessioncatalog.c` — net_hash removed from broadcast/receive
- `port/src/scenario_save.c` — SAVE-COMPAT stripped, catalog ID strings only
- `port/src/savefile.c` — SAVE-COMPAT stripped, integer fallbacks removed
- `port/include/net/net.h` — NET_PROTOCOL_VER = 27
- `src/game/bondwalk.c` — per-frame log spammers removed
- `src/game/mplayer/mplayer.c` — catalog-based defaults

### Decisions Made
- net_hash is permanently dead. The wire format uses full catalog ID strings everywhere. No compact hash representation.
- SAVE-COMPAT branches removed entirely — Mike and Chris can clear saves.
- Engine modernization vision: ROM is a legacy asset provider. Catalog becomes provider-agnostic asset bus enabling modern PBR/physics pipeline. Current work (Option A) → catalog-backed internals (Option A+) → provider-agnostic bus (Option B).
- ChrResync fix: always read all fields even when prop is NULL, only skip the apply step.

### Remaining Work
- 8 MEDIUM findings from comprehensive audit (dead code, rate limiting, chunk ordering, audio Hz, JSON depth, shutdown sequence)
- 6 LOW findings (realloc error handling, enet_peer_send check, strcpy → strncpy)
- Systemic sweeps: 350+ sprintf → snprintf (done separately), **network bounds checks DONE (S131)**, fread/fwrite return checks, malloc NULL checks
- Phase G playtest verification still pending

---

## Session S131 -- 2026-04-03

**Focus**: Comprehensive bug audit fixes (14 Tier 2+3 findings), five systemic sweeps, v0.0.25 release, context cleanup, playtest → 10 new UI/UX bugs

### What Was Done

1. **Systemic sweep 1: sprintf → snprintf** — 344 sites across 36 files. All unbounded sprintf calls replaced with snprintf to eliminate buffer overflow risk.

2. **Systemic sweep 2: network array bounds** — Full audit of all `netbufReadU8/U16/U32` → array-index paths in `port/src/net/`. One unguarded site fixed: `netmsgSvcAuthRead` — added `id >= NET_MAX_CLIENTS` and `maxclients > NET_MAX_CLIENTS` guards (B-75 / S131 sweep2).

3. **Systemic sweep 3: fread/fwrite checks, strcpy→strncpy, realloc NULL guards** — Fixed B-77 (fread unchecked in savefile), B-85 (buildArchiveDir stale pointer on realloc failure), B-87 (strcpy VK names in input.c), B-88 (three strcpy in mpsetups.c), B-89 (strcpy homeDir in fs.c).

4. **v0.0.25 released as pre-release** — version bump, update tab column fix (Title column stretched, Size column 80px), title intro alignment fix, update notification banner overlap fixed.

5. **Context system major cleanup** — archived completed work, trimmed stale playtest backlog, updated for S131 state.

6. **Playtest session** — Revealed 10 new UI/UX bugs (B-90 through B-99): mission select unlock filtering missing, objectives not loading, mouse not captured on solo start, pause menu incomplete, ImGui duplicate ID on pause menu, update banner visible during missions, difficulty flow wrong, special assignments not separated, pause menu OG fallback, updater extraction reliability.

### Key Files Changed
- `port/src/net/netmsg.c` — sweep2 (auth bounds), sweep1 (sprintf)
- `port/src/savefile.c` — sweep3 (fread checks)
- `port/src/net/netdistrib.c` — sweep3 (realloc NULL guard)
- `port/src/input.c` — sweep3 (strcpy VK names)
- `port/src/mpsetups.c` — sweep3 (three strcpy calls)
- `port/src/fs.c` — sweep3 (strcpy homeDir)
- 30+ additional files — sweep1 (sprintf→snprintf)
- `port/include/versioninfo.h.in` — v0.0.25 bump
- `port/fast3d/pdgui_menu_update.cpp` — column widths fix
- `port/fast3d/pdgui_backend.cpp` — title intro alignment

### Decisions Made
- Network bounds sweep complete. All netbufRead → array-index paths are now guarded.
- v0.0.25 is the current pre-release. Next release will address playtest findings.

### Next Steps
- Fix B-90 through B-99 (solo mission flow, pause menu, mouse capture, ImGui IDs)
- Phase D5: Settings/QoL + UI Polish pass (relative layout for all menus)
- Phase G playtest verification (MP bots, match completion)

---

## Session S133 -- 2026-04-03

**Focus**: Merge state audit + catalog tab crash root cause (B-102)

### What Was Done

1. **Merge state audit (all clear)**
   - `git log --oneline -20`, `git worktree list`, `git diff --stat HEAD`, `git stash list` all clean.
   - All 35 Claude worktrees are at or behind `dev` HEAD (`1e7ca59`). No unmerged commits. No uncommitted changes.
   - Stash list has 3 old entries (pre-existing, not from today).
   - All today's fixes (dynamic catalog buffer, B-92 mouse capture, B-94 ImGui IDs, B-100 modmgr, B-101 updater button, propagation sweep) confirmed fully merged into `dev`.

2. **B-102: Catalog tab NULL crash — root cause found and fixed**
   - **Root cause**: `ASSET_LANG` was added to `asset_type_e` (index 24) and 68 base language banks are registered with it, but `s_AssetTypeNames[ASSET_TYPE_COUNT]` in `pdgui_menu_mainmenu.cpp` only had 24 initializers (indices 0–23). `s_AssetTypeNames[24]` = NULL.
   - Every call to open the type-filter combo called `ImGui::Selectable(NULL, sel)` for ASSET_LANG → immediate crash. The per-type stat row also crashed via `TextDisabled("%-12s", NULL)` since `assetCatalogGetCountByType(ASSET_LANG)` returns 68.
   - **Fix**: Added `"Lang"` at index 24. One line. Propagation check confirmed `typeName()` in modmgr uses a `default: return "Unknown"` switch — safe. No other NULL name arrays.
   - Build verified: `pdgui_menu_mainmenu.cpp` compiles clean (exit 0, only pre-existing `/*` within comment warning on line 17).

### Key Files Changed
- `port/fast3d/pdgui_menu_mainmenu.cpp` — add `"Lang"` to `s_AssetTypeNames[]` (7fb1831)

### Next Steps
- Playtest: verify Settings → Catalog tab opens without crash, shows Lang entries
- B-90 through B-99 solo mission flow bugs remain
- Phase D5.2: pause menu + mouse capture polish

---

## Session S132 -- 2026-04-03

**Focus**: Propagation scan — 5 bug pattern classes across all pdgui_menu_*.cpp and port/src/

### What Was Done

**Scan + fix of all 5 pattern classes:**

1. **Pattern 1 — Fixed-size static arrays (overflow on catalog growth)**
   - `pdgui_menu_room.cpp`: `s_Arenas[256]` converted to `malloc`/`realloc` dynamic buffer with `s_ArenasCapacity` tracking. Silently dropped arenas >255; now unbounded. Free+null on `buildArenaListFromCatalog` reset and `pdguiRoomScreenReset`.
   - All other static arrays in pdgui_menu_*.cpp and port/src/ are fixed-size by design (status bufs, search inputs, etc.) — no action needed.

2. **Pattern 2 — runtime_index as array subscript** — No unsafe siblings. All runtime_index usages go through typed conversion functions (`catalogBodynumToMpBodyIdx`, `catalogHeadnumToMpHeadIdx`, etc.). No raw `[e->runtime_index]` subscripts found.

3. **Pattern 3 — Missing inputLockMouse on gameplay transitions** — 4 siblings of B-66/B-92 fix found and fixed:
   - `matchsetup.c matchStartFromChallenge()`: was missing `inputLockMouse(1)` after `menuStop()` (only the normal match path had it)
   - `netmsg.c SVC_STAGE co-op/anti branch`: networked co-op/counter-op client missing lock
   - `netmsg.c SVC_STAGE MP branch`: networked MP client missing lock
   - `net.c netServerStageStart()`: listen-server co-op host missing lock
   - `input.h` added to client-only `#if !defined(PD_SERVER)` include blocks in `netmsg.c` and `net.c`

4. **Pattern 4 — Empty/duplicate ImGui IDs** — No siblings. All button/selectable labels already use `##pm`, `##go`, `PushID(i)` wrappers, `##diff_row` etc. Pause menu B-94 already resolved.

5. **Pattern 5 — Action buttons without backing data guards** — No siblings. Apply Changes is guarded by `pending == 0` BeginDisabled. No null-data action buttons found.

### Key Files Changed
- `port/fast3d/pdgui_menu_room.cpp` — dynamic arena buffer
- `port/src/net/matchsetup.c` — inputLockMouse on challenge start
- `port/src/net/netmsg.c` — inputLockMouse on co-op + MP SVC_STAGE
- `port/src/net/net.c` — inputLockMouse on listen-server co-op start

### Build Verification
All 4 changed files pass `-fsyntax-only` check for both client and PD_SERVER builds.

### Next Steps
- Phase D5.2: pause menu + mouse capture fixes remain in playtest backlog
- B-90 through B-99 solo mission flow bugs remain

---

## Session S129 -- 2026-04-02

**Focus**: Catalog ID Compliance Audit — M-2, M-3, M-4, M-5 UI picker fixes

### What Was Done

**2 files changed** — both targets build clean (zero new errors).

**Changes:**

1. **`port/fast3d/pdgui_menu_matchsetup.cpp`** (M-2 + M-4 + struct fix)
   - Fixed local `struct matchslot` layout: reordered `body_id[64]`/`head_id[64]` to be PRIMARY (before `headnum`/`bodynum`) matching the canonical `net/matchsetup.h` definition. Previous ordering was wrong and caused silent field offset mismatches.
   - Fixed local `struct matchconfig`: added missing `char stage_id[64]` (PRIMARY) between `scenario` and `stagenum`, and `u8 spawnWeaponNum` at end. Without these, all code reading `g_MatchConfig.stage_id` in matchsetup.cpp was reading wrong bytes.
   - M-2: Replaced `static s32 s_ArenaIndex` / `s_ArenaModalHover` (raw stagenum integers) with `static char s_ArenaId[CATALOG_ID_LEN]` / `s_ArenaHoverId[CATALOG_ID_LEN]`. All selection comparisons use `strcmp(ae->id, s_ArenaId)`. On selection: `g_MatchConfig.stage_id` written (not `stagenum`). Init: copies `g_MatchConfig.stage_id` to `s_ArenaId`. Hover name resolution loop now compares by ID. Removed dead `findArenaIndex(u8 stagenum)` function. Start Match log updated to show `stage_id`.
   - M-4: Bot character selector loop now resolves `catalogResolveBodyByMpIndex(b)` / `catalogResolveHeadByMpIndex(b)` and sets `bot->body_id`/`bot->head_id`. `isSel` comparison uses catalog ID strcmp. `bodynum`/`headnum` NOT written at selection time — derived at `matchStart()`.

2. **`port/fast3d/pdgui_menu_room.cpp`** (M-3 verified + M-5 fixed)
   - M-3: Verified already clean from Session S128 — arena picker and match start paths all use `stage_id`. No changes needed.
   - M-5: Lobby bot slot editor character picker updated. `curBody` display name now resolved from `sl->body_id` via scan of body entries. `isSel` uses `catalogResolveBodyByMpIndex(b)` strcmp. On selection: `sl->body_id`/`sl->head_id` set via catalog resolvers. `bodynum`/`headnum` NOT written at selection time.

**Build verification**: Both `pd` (client) and `pd-server` build clean. Only pre-existing `/*` within comment warnings in file headers.

### Decisions Made
- Stagenum is still displayed in the arena hover preview badge (0x%02X) — this is debug info derived FROM the catalog entry at display time, not identity.
- `lobbyplayer_view.bodynum` in room.cpp (the right panel player list) uses integer bodynum — this is data received from the network, not a selection. Not a violation; left as-is.

### Next Steps
- Remaining audit findings: C-1/C-2/C-3 (wire net_hash), C-4 (netdistrib), C-5 (sessioncatalog) — Batch 3 (netmsg/netdistrib/sessioncatalog).
- H-5/H-6 (save fallbacks) — accepted debt post-v1.0.
- M-6/M-7/L-1/M-1 — lower priority.
- Playtest: confirm body/head variety in bot selection, arena selection, and match start all work end-to-end.
- These changes (S127/S128/S129) not yet committed — commit together when Mike confirms clean in-game.

---

## Session S128 -- 2026-04-02

**Focus**: Catalog ID Compliance Audit — H-1, H-2, H-3, H-4 implementation

### What Was Done

**6 files changed** — no new commits yet; both targets build clean (zero errors).

**Mandate**: All asset references must use catalog ID strings `"namespace:readable_name"` at every interface boundary. Raw integer indices (bodynum, headnum, stagenum) are ONLY permitted as DERIVED values resolved at the final legacy engine handoff.

**Changes:**

1. **`port/include/net/matchsetup.h`** (H-1) — Added `char stage_id[64]` as PRIMARY field to `struct matchconfig`. `stagenum` annotated DERIVED. Mirrors the `body_id`/`head_id` pattern from matchslot.

2. **`port/src/net/matchsetup.c`** (H-1 follow-through) — `matchConfigInit()`: sets `stage_id = "base:mp_complex"`, resolves `stagenum` from catalog. `matchStart()`: resolves `g_MpSetup.stagenum` from `g_MatchConfig.stage_id` (handles ASSET_ARENA + ASSET_MAP); returns -1 on failure (no fallback). `matchStartFromChallenge()`: syncs `g_MatchConfig.stage_id` from challenge stagenum via `catalogResolveArenaByStagenum()` / `catalogResolveStageByStagenum()`.

3. **`port/fast3d/pdgui_bridge.c`** (H-2) — `netLobbyRequestStart` and `netLobbyRequestStartWithSims` signatures changed from `u8 stagenum` to `const char *stage_id`. Static helper `s_resolveStageIdToStagenum()` resolves internally. Returns -3 if catalog resolution fails; no fallback.

4. **`port/fast3d/pdgui_menu_room.cpp`** (H-2 callers) — `arena_entry` struct now carries `char id[64]`. `catalogArenaCollect()` populates `id` from `e->id`. `syncArenaFromConfig()` matches by `stage_id` string comparison (not stagenum integer). Arena picker click writes `stage_id` not `stagenum`. All three match-start paths (MP Combat Sim, COOP Campaign, Counter-Op) pass catalog ID strings to the bridge API.

5. **`src/game/mplayer/mplayer.c`** (H-3, H-4) — Player defaults in `func0f187fec()` and bot defaults in `func0f1881d4()` replaced with `assetCatalogResolve("base:dark_combat")` etc.; error logged on failure; no integer fallback. `mpInit()` default stage uses `assetCatalogResolve("base:mp_skedar")` to set both `stage_id` and `stagenum`; last-resort integer fallback only if catalog unavailable at boot. `mpStartMatch()` random resolution: syncs `stage_id` via `catalogResolveArenaByStagenum` / `catalogResolveStageByStagenum` after integer resolution.

6. **`src/include/types.h`** (H-4) — Added `char stage_id[64]` as PRIMARY field to `struct mpsetup`, above `stagenum`. PC-only field; no N64 offset. Comment explains DERIVED relationship.

**Build verification**: Both `pd` (client) and `pd-server` build clean. Zero errors; only pre-existing warnings (dangling pointer in modelasm_c.c, uninitialized frac in model.c).

### Decisions Made
- `netLobbyRequestStart`/`WithSims` are permanently string-based. No integer overload.
- Campaign/Counter-Op missions: `s_Missions[]` internal UI struct retains stagenum for display; conversion to catalog ID string happens AT the API boundary callsite via `catalogResolveStageByStagenum()`. Valid per mandate (internal UI state, not a catalog boundary).
- `netmenu.c` legacy `menuPush`/`menuPop` path left untouched per task constraints (that system is being stripped entirely).
- `stage_id` in `mpsetup` is a PC-only addition; zero N64 struct impact.

### Next Steps
- **Playtest required**: zero CATALOG-ASSERT in logs, all MP game modes with bots, bot body/head variety in-game, arena selection via UI writes stage_id correctly.
- These changes (S128) and S127 changes are not yet committed — commit together when Mike confirms clean in-game.
- Audit findings H-1/H-2/H-3/H-4 fully implemented. H-5/H-6 (save file legacy integer fallbacks) are ACCEPTED DEBT (SA-4 backward-compat; removal requires migration tool, planned post-v1.0).

---

## Session S127 -- 2026-04-02

**Focus**: Game Director Mandate — Catalog-ID-native data model (complete the ba30dcc revert properly)

### What Was Done

**8 files changed** — no new commits yet; all changes in main working copy. Both targets build clean.

**Root cause**: ba30dcc revert added `body_id`/`head_id` fields to `matchslot` but did NOT populate them anywhere, and did NOT remove conversion calls in `netmsg.c:3618-3619` and `netmanifest.c:653-656`. Mandate: catalog ID strings are the ONLY valid way to reference assets on the match config path. Integer indices are resolved ONLY at the legacy engine handoff in `matchStart()`.

**Changes:**

1. **`port/include/net/matchsetup.h`** — `body_id`/`head_id` annotated PRIMARY, `bodynum`/`headnum` annotated DERIVED. `matchConfigAddBot` signature changed from integer `(headnum, bodynum)` to string `(body_id, head_id)`.

2. **`port/src/net/matchsetup.c`** — `matchConfigInit()`: populates `body_id`/`head_id` from `catalogResolveBodyByMpIndex`/`catalogResolveHeadByMpIndex` immediately. `matchConfigAddBot()`: new string-based signature; sets body_id/head_id primary; derives bodynum/headnum via catalog. `matchStart()`: resolves bodynum/headnum from body_id/head_id via `assetCatalogResolve` + `catalogBodynumToMpBodyIdx`/`catalogHeadnumToMpHeadIdx` at handoff.

3. **`port/src/net/netmsg.c`** — `CLC_LOBBY_START` write: removed raw u8 mpbodynum/mpheadnum; now `netbufWriteStr(sl->body_id)` + `netbufWriteStr(sl->head_id)`. Read (server): replaced two `netbufReadU8` with `netbufReadStr` + catalog resolve to mpbodynum/mpheadnum.

4. **`port/src/net/netmanifest.c`** — `manifestBuildForHost()` bot section: removed double-conversion chain; now uses `sl->body_id`/`sl->head_id` directly with `assetCatalogResolve`.

5. **`port/fast3d/pdgui_menu_matchsetup.cpp`** — forward decl updated; `matchConfigAddBot` call changed to use `"base:dark_combat"`, `"base:head_dark_combat"` literals. Local `#define BODY_DARK_COMBAT 0` now dead.

6. **`port/fast3d/pdgui_menu_room.cpp`** — Add Bot call: copies `body_id`/`head_id` strings instead of integers.

7. **`port/src/scenario_save.c`** — Write: uses `sl->body_id`/`sl->head_id` directly; removed legacy integer `"body"`/`"head"` fields. Read: passes `bodyId`/`headId` strings to `matchConfigAddBot`; fallback to `catalogResolveByRuntimeIndex` if no string present for old saves.

8. **`port/src/assetcatalog_base.c`** — Head registration loop now covers all 76 `g_MpHeads[]` entries (was 75 via `s_BaseHeads[]`). Fallback name `"head_%d"` for missing entries.

**Build verification**: Both `pd` (client) and `pd-server` build clean. Zero errors; only pre-existing warnings (dangling pointer in modelasm_c.c, uninitialized frac in model.c).

### Decisions Made
- Conversion calls (`catalogBodynumToMpBodyIdx`, etc.) are ONLY valid at legacy engine handoff in `matchStart()`. All earlier call sites on the match config path are wrong and were removed.
- `matchConfigAddBot` signature is permanently string-based. No integer overload.
- Scenario save: legacy integer fallback kept for backward-compat with pre-SA-4 saves; write side is fully string-first.
- `manifestBuild()` (server-side, post-matchStart) still reads from `g_BotConfigsArray` with mpbodynum — this is correct; it runs after matchStart has resolved integers from catalog.

### Next Steps
- **Playtest required**: zero CATALOG-ASSERT in logs, all MP game modes with bots, bot body/head variety in-game.
- These changes are not yet committed — commit when Mike confirms clean in-game.
- Phase G playtest verification still outstanding.

---

## Session S126 -- 2026-04-02

**Focus**: Phase G — Full Verification Pass (code audit + build)

### What Was Done

**0 files changed** — audit + context update session only. No code changes.

**Grep audit — catalog universality (codebase-wide)**

Searched for all patterns flagged in Phase A audit spec: raw g_MpBodies[]/g_MpHeads[]/g_MpWeapons[] with raw indices, raw stagenum bypassing catalog resolve functions, filenum_t passed as catalog ID, TODO/FIXME/HACK comments related to catalog migration, netbuf writes of raw body/head/weapon indices.

**Confirmed clean (Phases B–F fixes verified in place):**
- `CLC_LOBBY_START` write: stagenum → `catalogWritePreSessionRef(catalogResolveArenaByStagenum(...))` ✓
- `CLC_LOBBY_START` write: weapons → per-slot `catalogWritePreSessionRef(catalogResolveWeaponByGameId(...))` ✓
- `CLC_LOBBY_START` write: bot body/head → `catalogBodynumToMpBodyIdx/catalogHeadnumToMpHeadIdx` (correct domain conversion) ✓
- `SVC_STAGE_START` write: stage → `catalogWriteAssetRef(sessionCatalogGetId(catalogResolveStageByStagenum(...)))` ✓
- `SVC_STAGE_START` write: weapons → per-slot `catalogWriteAssetRef(sessionCatalogGetId(...))` ✓
- `CLC_LOBBY_START` read: arena → `catalogReadPreSessionRef()` → `ext.arena.stagenum` ✓
- `CLC_LOBBY_START` read: weapons → per-slot `catalogReadPreSessionRef()` → `ext.weapon.weapon_id` ✓
- Host manifest embedded in CLC_LOBBY_START (Phase D.2/D.3) ✓
- Save file write: `weapon_ids` (catalog string IDs), `head_id`/`body_id`, `stage_id` — all using catalog ✓
- Scenario save write: `weapon_id%d`, `arena_id` — catalog ✓
- Zero TODO/FIXME/HACK related to catalog migration anywhere in port code ✓
- B-63/B-64/B-65/B-66/B-67/B-68/B-69/B-70/B-71: all fixed in Phases B–F ✓

**Findings (new issues documented):**
- **G-1 (LOW)**: `SVC_LOBBY_STATE` (`netmsg.c:4149`) still sends raw stagenum u8. Display-only lobby broadcast; doesn't affect match load. Documented as B-72.
- **G-2 (DEBT)**: Save file legacy integer fallbacks (`savefile.c:693-698, 832-834, 860-869`) — `mpheadnum`, `mpbodynum`, `stagenum`, `weapons` raw integers for old saves. Write side is fully catalog-first. Read fallbacks intentional for backward-compat with pre-SA-4 saves. Removal requires save migration tool; planned post-v1.0.
- **G-3 (ACCEPTED)**: Bot body/head in `CLC_LOBBY_START` wire as raw u8 mpbodynum/mpheadnum. Index domain conversion (bodynum→mpbodynum) applied at write site per Phase C spec. Both sides have identical tables. Could use net_hash for full universality in a future pass.

**Build verification:**
- `pd` (client): CLEAN via `msys2_shell.cmd -mingw64` make ✓
- `pd-server`: CLEAN ✓
- Note: direct bash invocation fails with TEMP=C:\WINDOWS permission error in MinGW GCC. Must use msys2_shell.cmd -mingw64 for bash builds. PowerShell build-headless.ps1 works correctly from dev machine.

### Decisions Made
- Phase G code audit is COMPLETE. Playtest verification still pending (Mike must run in-game).
- SVC_LOBBY_STATE raw stagenum documented as B-72 (LOW) — won't block v0.1.0.
- Save file fallbacks: keep until post-v1.0 save migration. Document as planned debt.
- Bot body/head raw u8: accepted as Phase C decision; document in audit file.

### Next Steps
- **Playtest required for Phase G to be fully DONE**: zero CATALOG-ASSERT in logs, all MP game modes run to completion with bots, menu transitions clean (no tint bleed, no duplicate instances), spawn variety, bot unstick, spawn weapons.
- After clean playtest: Phase G DONE, catalog universality migration COMPLETE.
- Post-migration: R-series (room architecture), L-series (lobby UX), v0.1.0 QC pass.

---

## Session S125 -- 2026-04-02

**Focus**: Phase F — Spawn System Hardening (commit 27b1e08)

### What Was Done

**5 files changed, 257 insertions / 46 deletions** — pushed to `dev`.

**F.1 — Anti-repeat spawn tracking** (`src/game/player.c`):
- Added `static s16 s_LastSpawnPad = -1` before `playerChooseSpawnLocation`.
- After shortlist is built: if `sllen > 1` and `s_LastSpawnPad` is set, the matching entry is swapped-to-end and removed, preventing the same pad winning back-to-back.
- `s_LastSpawnPad` recorded on every shortlist pick; fallback path (no shortlist) skipped — anti-repeat only applies when alternatives exist.

**F.5 — Bot stuck detection** (`src/game/bot.c`):
- `struct botstuckstate` + `static s_BotStuck[MAX_BOTS]` — one snapshot per bot slot.
- Constants: `STUCK_CHECK_FRAMES=180` (~3s), `STUCK_EPSILON_SQ=100`, `STUCK_RELO_MIN_SQ=90000`, `STUCK_RELO_FRACTION=0.25f`.
- In `botTick()`, after `botTickUnpaused`: every 180 frames, if bot has pathfinding intent (`MA_AIBOTMAINLOOP/GOTOPOS/GETITEM/GOTOPROP/RUNAWAY/DOWNLOAD`) and has moved < 10 units, find a waypoint ≥300u away via random probe loop (up to 2×numwpts attempts), teleport with `CHRHFLAG_WARPONSCREEN`, apply 25% damage via `chrAddHealth(chr, -(chr->maxdamage * 0.25f))`, set `bs->relocating = 1`.

**F.6 / B-70 — Bot spawn weapon fix** (`port/src/net/matchsetup.c`, `src/game/bot.c`, `src/game/player.c`):
- `matchConfigInit()`: changed `g_MatchConfig.options = 0` → `g_MatchConfig.options = MPOPTION_SPAWNWITHWEAPON`. Root cause: bit never set, so spawn weapon block always skipped.
- `botSpawn()` and `playerStartNewLife()`: resolve `g_MatchConfig.spawnWeaponNum` first (search g_MpWeapons for matching weaponnum), fall back to `g_MpSetup.weapons[0]` when 0xFF (Random). Bots use `botinvGiveSingleWeapon` / `botinvSwitchToWeapon`.

**B-66 — Mouse capture on match start** (`port/src/net/matchsetup.c`):
- Added `#include "input.h"` and `inputLockMouse(1)` after `menuStop()` in `matchStart()`.
- Root cause: `pdguiIsActive()` was true during lobby setup, deferring SDL relative-mouse apply inside `inputLockMouse()`. Explicit call after menus stop forces it.

**F.2 / F.3 / F.4** — Already implemented: `playerReset()` has navmesh-waypoint fallback + pad-scan fallback; `playerChooseSpawnLocation()` has numpads==0 floor fallback. No changes needed.

### Build
- Client (`pd`) and server (`pd-server`): both clean. Only pre-existing uninitialized-var warnings in player.c (unrelated to our changes).

### Decisions Made
- `s_LastSpawnPad` is static to the compilation unit (not per-player) — good enough for the common 1-local-player case; bot spawns go through different path.
- Bot stuck check uses `aibot->aibotnum` (s16 slot field) for O(1) lookup — no linear search per tick.
- `STUCK_RELO_FRACTION=0.25f` matches spec's "25% max-damage penalty".

### Next Steps
- Playtest Phase F: spawn variety (should not repeat same pad consecutively), bot unstick (observe STUCK: log line if a bot gets cornered), spawn weapons present (check log for MATCHSETUP: weapon set applied lines + in-game weapon in hand).
- Phase G (Full Verification Pass) is next: zero CATALOG-ASSERT warnings, all game modes run to completion.

---

## Session S124 -- 2026-04-02

**Focus**: Phase E — Menu Stack Architecture + Input Context (commit 5eab8d3)

### What Was Done

**3 files changed, 69 insertions / 2 deletions** — pushed to `dev`.

**E.1 — Full duplicate rejection in `menuPush`** (`menumgr.c`):
- Was: only rejected same menu on top of stack.
- Now: `menuIsInStack(menu)` — rejects if menu is anywhere in stack. Prevents Esc or rapid input stacking duplicate instances (B-21).

**E.2 — Post-mission buttons non-interactive** (`pdgui_menu_endscreen.cpp`):
- `renderSoloEndscreen` and `renderMpEndscreen`: on `ImGui::IsWindowAppearing()`, call `SDL_SetRelativeMouseMode(SDL_FALSE)` + `SDL_ShowCursor(SDL_ENABLE)` + warp cursor to center.
- Root cause: game is still in SDL relative mouse mode when endscreen appears after gameplay.

**E.2 — Lobby→gameplay mouse capture** (`menumgr.c`):
- Added `restoreGameplayMouseCapture()` helper: checks `inputMouseIsLocked()` and applies `SDL_SetRelativeMouseMode(SDL_TRUE)`.
- Called from `menuPop()` when stack empties and from `menuPopAll()`.
- Root cause: `inputLockMouse(1)` defers the SDL call if `pdguiIsActive()` is true during the lobby→match transition. This restores it when the menu stack clears.

**E.3 — Green tint bleed** (`pdgui_menu_mainmenu.cpp`, `pdgui_menu_endscreen.cpp`):
- `renderMainMenu()`: `pdguiSetPalette(1)` at entry — defensive baseline for blue palette.
- `renderSoloEndscreen` / `renderMpEndscreen`: save `prevPalette` before setting screen palette; restore at all exit paths (including early `Begin()` failure).

### Build
- Client (`pd`) and server (`pd-server`): both clean.

### Decisions Made
- Mouse restore in menumgr mirrors the existing `pdguiPauseMenuClose()` pattern.
- Palette save/restore covers the transition frame where endscreen and main menu both render.
- Main menu explicit set is defensive insurance; endscreen restore is the structural fix.

### Next Steps
- Playtest needed: post-mission buttons clickable, lobby→gameplay mouse capture, no green tint on main menu after mission complete.
- Phase F (Spawn System Hardening + inputSetMode wiring) is next.

---

## Session S123 -- 2026-04-02

**Focus**: Phase D — Server Manifest Model (commit e517633)

### What Was Done

**8 files changed, 426 insertions / 55 deletions** — pushed to `dev`.

**D.1 — `match_manifest_entry_t` gains `u8 sha256[32]`** (`netmanifest.h`):
- New field carries SHA-256 for MANIFEST_TYPE_COMPONENT entries; zeroed for all other types.
- Wire format only includes sha256 bytes when type == MANIFEST_TYPE_COMPONENT.

**D.2 — `manifestBuildForHost()`** (`netmanifest.c`):
- Client-callable; builds manifest from `g_MpSetup` (stage/weapons), `g_NetLocalClient->settings` (host body/head at slot 0), `g_MatchConfig.slots[]` (bots at slots 1..N), `modmgrGetMod()` (mods with SHA-256).
- Called in `netmsgClcLobbyStartWrite()` at the end of CLC_LOBBY_START serialization.

**D.3 — Host manifest embedded in CLC_LOBBY_START** (`netmsg.c`):
- Server reads manifest via `manifestDeserialize`; supplements with other players' body/head from `g_NetClients[].settings`.
- D.5: validates MANIFEST_TYPE_STAGE entry against arena-hash stagenum; logs warning on mismatch, uses arena hash for safety.
- Falls back to server-side `manifestBuild()` if deserialization fails.

**D.4 — SVC_MATCH_MANIFEST uses serialize helpers** (`netmsg.c`):
- `netmsgSvcMatchManifestWrite/Read` replaced inline loops with `manifestSerialize`/`manifestDeserialize`.

**D.6 — SHA-256 in `modinfo_t`** (`modmgr.h`, `modmgr.c`):
- `u8 sha256[SHA256_DIGEST_SIZE]` added to `modinfo_t`.
- Computed from mod.json file content at scan time; falls back to hash of "id:version" string.
- `manifestCheck()` validates SHA-256 for MANIFEST_TYPE_COMPONENT entries via `modmgrFindMod()`.
- `server_stubs.c`: added `modmgrFindMod` stub so dedicated server links clean.

**D.7 — Protocol version bump**: `NET_PROTOCOL_VER` → 26 (breaking; old clients cannot connect).

### Build
- Client (`pd`) and server (`pd-server`): both clean.

### Decisions Made
- Server supplements host-sent manifest with other players' settings rather than building from scratch — server stays catalog-free.
- SHA-256 only transmitted on wire for COMPONENT entries; all other types zero the field (saves ~32 bytes × N entries per message).
- Fallback to server-side `manifestBuild()` preserved as safety net for malformed/legacy connections.

### Next Steps
- Playtest needed: real MP match to verify CLC_LOBBY_START host manifest embedding/deserialization flows end-to-end.
- Phase E (Menu Stack Architecture) is next.

---

## Session S122 -- 2026-04-02

**Focus**: Phase C — Systematic Catalog Conversion (commit ee0810c)

### What Was Done

FIX-1 through FIX-23 across all subsystems: bot allocation, SVC_STAGE_START bot config, weapon spawn, arena selection, stage loading. All raw N64 index references in the server path replaced with catalog ID resolution via new Phase B API. Both targets build clean.

---

## Session S121 -- 2026-04-02

**Focus**: Phase B — Catalog API Hardening + Arena Human-Readable IDs (commit b13a6b5)

### What Was Done

**8 files changed, 192 insertions / 45 deletions** — pushed to `dev`.

**B.1 (FIX-24) — Register ALL g_HeadsAndBodies[] entries** (`assetcatalog_base.c`):
- Root cause: covered-mask loop iterated all 76 g_MpHeads[] entries, marking g_MpHeads[75].headnum as covered. But the MP registration loop only iterates s_BaseHeads[] (75 entries), so that headnum (103 in playtests) was marked covered but never registered → `CATALOG-ASSERT type=16 index=103`.
- Fix: covered-mask now iterates s_BaseBodies[]/s_BaseHeads[] (the actually-registered tables), not the full g_MpBodies[]/g_MpHeads[] arrays. All unregistered entries are now picked up by the SP-only fallback sweep.

**B.2 — New index-domain-safe API** (`assetcatalog_api.c`, `assetcatalog.h`):
- Added `catalogResolveBodyByMpIndex(mpbodynum)` and `catalogResolveHeadByMpIndex(mpheadnum)` — convert mpXnum (g_MpBodies/Heads[] position) to bodynum/headnum before catalog lookup.
- Added `catalogBodynumToMpBodyIdx(bodynum)` and `catalogHeadnumToMpHeadIdx(headnum)` — reverse lookup for load path.
- 7 call sites fixed: FIX-7 (netmsg.c), FIX-13 (netmanifest.c), FIX-14 (net.c), FIX-11/12 (savefile.c), FIX-10 (savefile.c stage), FIX-15 (scenario_save.c stage).

**B.3 — Improved error logging**: `catalogResolveByRuntimeIndex` warning now includes type name (from static `s_typeNames[]`) for easier diagnostics.

**Part 2 — Arena human-readable IDs** (`assetcatalog_base.c`):
- `s_ArenaNames[75]` static table mapping each arena slot index to a human-readable name.
- Arena registration loop now emits `base:arena_<name>` instead of `base:arena_<N>`.
- NULL entries in the table cause the slot to be skipped gracefully.

### Build
- Client (`pd`) and server (`pd-server`): both 100% clean.

### Decisions Made
- Three index domains (mpbodynum, bodynum/runtime_index, catalog array position) must never be conflated. New API encapsulates the conversion at the boundary.
- Arena ID migration is non-breaking: old `base:arena_<N>` IDs only existed in the catalog (no persisted save data references them).

### Next Steps
- Playtest needed: verify no CATALOG-ASSERT type=16 in log during MP match with bots.
- Remaining Phase B fixes not yet addressed: FIX-16 (scenario_save.c:302 bounds), FIX-17/18/19 (netmanifest.c defaults/SP manifest/anti-player), FIX-20 (identity.c mpbodynum migration), FIX-21/22/23 (weapon save/scenario/dropdown).
- Phase C (Systematic Catalog Conversion) is next after playtest confirms Phase B clears the B-63/B-64 errors.

---

## Session S119 -- 2026-04-02

**Focus**: Comprehensive playtest analysis → catalog universality engineering spec + bug triage

### What Was Done

- **Playtest analysis**: Reviewed 3 client logs, 1 server error log, and screenshots from April 1, 2026 playtest session. Identified root causes for all observed failures.
- **Catalog type=16 root cause** (B-63/B-64): `catalogResolveByRuntimeIndex` called on bot allocation with type=16, which is out of range for the catalog asset type enum (valid 0–7). Every bot allocation triggers CATALOG-ASSERT; all bots invisible; access violation downstream. Root cause: bot config path passes unvalidated type field into the resolver.
- **Server catalog gap** (B-65): `SVC_STARTGAME` server side still emits raw hex stagenum (e.g. `0x1f`) rather than catalog ID. Client-side catalog cannot resolve raw hex. All networked play blocked.
- **Menu input state machine gaps** (B-66/B-67/B-68/B-69/B-70): `inputSetMode()` not called on match-start code path from MP lobby → mouse capture misses. Post-mission input context not switched → debrief buttons non-interactive. Tint not cleared on menu pop → green bleeds to main menu. Esc re-registers menu in same frame → stacked instances.
- **Bot spawn weapons** (B-70): `options=0x00000000` in bot spawn log → options bitmask not reaching bots during match start.
- **Spec produced**: `PD2_Catalog_Universality_Spec_v1.0.docx` — governing engineering specification covering catalog universality migration (Phases A–C), server manifest model (Phase D), menu stack architecture (Phase E), spawn/input hardening (Phase F), and full verification pass (Phase G). All phases defined with success criteria.
- **Context updated**: bugs.md (B-63–B-71), tasks-current.md (Phases A–G), roadmap.md (primary workstream declaration), session-log.md (this entry).

### Decisions Made

- Catalog universality migration (Phases A–C) is now the primary workstream and blocks all other feature work. The catalog is the load-bearing wall of the entire asset system — surface bug fixes on top of a broken catalog just shift the crash site.
- Phase A is research-only (audit + mapping, no code changes) to ensure full scope is understood before any API changes.
- Server manifest model (Phase D) supersedes server-side catalog concept: server receives manifest from host, never maintains its own catalog.
- Menu stack architecture (Phase E) and spawn/input hardening (Phase F) can proceed in parallel with Phases C/D.

### Next Steps

- **Phase A**: Catalog universality audit — `grep` for all raw-index call sites, map type+index origins, identify which paths produce type=16.
- After Phase A report: review findings with Mike before beginning Phase B (API hardening).

---

## Session S140 — 2026-04-04

**Focus**: Bot count mismatch audit + bot freeze root cause analysis (no code changes — analysis only, session terminated by user before fixes applied)

### What Was Done

**Audit findings** (no fixes implemented):

**Root Cause 1 — CLC_LOBBY_START buffer overflow** (CRITICAL):
- `NET_BUFSIZE = 1440` bytes. `g_NetLocalClient->out` is this size.
- CLC_LOBBY_START writes: header (~34 bytes) + weapons (6 strings, ~12–84 bytes) + per-bot (3 strings + 2 bytes ≈ 45 bytes/bot) + manifest.
- At 31 bots: ~34 + 12 + 31×45 + manifest ≈ 1441+ bytes — overflows the buffer.
- After overflow, `netbuf->error = 1`; writes are no-ops but `botIdx` keeps incrementing.
- The packet declares `numSims=31` (written before overflow), but only ~23 bots have valid data.
- Server reads 31 entries: 23 valid + 8 garbage (empty strings → dark_combat defaults). Sets `clampedSims=31`, allocates 31 stubs, sends SVC_STAGE_START with 31 bot chrslots bits.
- **Fix location**: `port/fast3d/pdgui_bridge.c:657` — `netLobbyRequestStartWithSims`. Change from `g_NetLocalClient->out` to a static large buffer (e.g. `NET_BUFSIZE * 8 = 11520` bytes).

**Root Cause 2 — Bot rooms=-1 / freeze**:
- `botmgrAllocateBot` (botmgr.c) creates prop with `rooms[0] = -1`.
- `propActivate` sets `forceonetick = true` → bot ticks ONCE (the first-run log fires here).
- After first tick: rooms still -1, not in foreground → prop NOT ticked again.
- Actual bot spawn (valid rooms assigned) happens via stage setup AI → `aiMpInitSimulants` → `botSpawnAll` → `botSpawn` → `scenarioChooseSpawnLocation` → `chrMoveToPos` with valid rooms. This runs DURING stage loading (setup.c AI script), not from botTick.
- After `botSpawn`, bots have valid rooms. They get added to foreground normally.
- **Fix**: In `botmgrAllocateBot`, set `prop->forcetick = true` after `propActivate` so bots always tick until properly spawned. Clear `forcetick` in `botSpawn` after rooms are assigned.

**Root Cause 3 — CLC_BOT_MOVE not sent**:
- `netEndFrame` (net.c:1407): `if (g_NetLocalBotAuthority && g_BotCount > 0)` gates the write.
- On dedicated server: `g_NetLocalBotAuthority = true` set when `SVC_BOT_AUTHORITY` received. `g_BotCount` set by `setup.c` allocating bots from `g_MpSetup.chrslots`.
- If `g_NetLocalBotAuthority` is never set (SVC_BOT_AUTHORITY not received/processed), or `g_BotCount = 0` (bots not allocated due to overflow-corrupted chrslots), no CLC_BOT_MOVE is sent.
- After fixing CLC_LOBBY_START overflow → correct 31-bot chrslots → correct bot allocation → correct g_BotCount → CLC_BOT_MOVE flows.

**Key files for next session fixes**:
- `port/fast3d/pdgui_bridge.c:652–659` — CLC_LOBBY_START buffer
- `src/game/botmgr.c:72–74` — prop->forcetick after propActivate/propEnable
- `src/game/bot.c:307` — clear forcetick after chrMoveToPos in botSpawn
- `port/src/net/net.c:1407` — verify CLC_BOT_MOVE gate

### Decisions Made

- Session terminated before fixes; user will restart with explicit fix instructions.
- CLC_LOBBY_START buffer overflow is the root cause of the 23/31 bot count mismatch.
- rooms=-1 freeze is a secondary issue from forceonetick being cleared before spawn runs.

### Next Steps

- **S141**: Implement the three fixes above. Build verify. Commit + push.
- Playtest with 31 bots: verify full count transmitted, all spawn with valid rooms, CLC_BOT_MOVE flows.

---

