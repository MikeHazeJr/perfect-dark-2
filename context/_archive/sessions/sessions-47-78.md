# Session Log Archive: Sessions 47–78

> Period: 2026-03-24 to 2026-03-29
> Focus: Server Platform Foundation (SPF), join flow, room architecture, catalog activation (C-series), mod system (T-series), bug fixes (B-27 through B-53), network audit, null-guard audits, init-order audit, player count audit, dedicated server, NAT traversal groundwork
> Back to [index](README.md)

> **Note**: A duplicate 'Session S79' entry appears at the bottom of this archive (T-1/T-2 mod system work, 2026-03-29). The canonical S79 entry (C-7 SFX, beginning of this file) supersedes it.

## Session 78 -- 2026-03-29

**Focus**: T-8 + T-9 — Stage table restore and texture cache flush on mod reload (D3e)

### What Was Done

**T-8: `stageTableReset()` added to `src/game/stagetable.c`:**
- New function truncates `g_Stages` back to the base count (from `s_StagesInit`) and re-memcpy's
- Declared in `src/include/game/stagetable.h`
- Called from `modmgrUnloadAllMods()` — replaces the D3e TODO comment
- On reload, any mod-appended stage entries are discarded before fresh scan

**T-9: Texture cache flush + title return added to `modmgrReload()`:**
- `videoResetTextureCache()` called at end of modmgrReload() — clears all stale texrefs from fast3d's LRU
- `mainChangeToStage(MODMGR_STAGE_TITLE)` called immediately after — forces clean load of title screen under new mod state
- `#include "game/stagetable.h"` and `#include "video.h"` added to modmgr.c

**Build**: `stagetable.c` and `modmgr.c` compile clean (individual file builds, exit 0).

### Files Modified
- `src/game/stagetable.c` — new `stageTableReset()` after `stageTableAppend()`
- `src/include/game/stagetable.h` — declaration added
- `port/src/modmgr.c` — two includes added; TODOs replaced with live calls

### Decisions Made
- Used `s_StagesInit` (the static base array) as the restore source — no snapshot needed, source of truth already exists
- Used `videoResetTextureCache()` from `video.h` (the port's public C API for the fast3d cache) rather than calling `gfx_texture_cache_clear()` directly
- Both T-8 and T-9 are now complete; D3e is fully closed

### Next Steps
- Playtest: enable a mod, disable it, re-enable — verify no stale textures or ghost stages
- Continue T-7 playtest (mod.json body/head/arena catalog)
- Continue C-5 tex intercept

---

## Session 77 -- 2026-03-29

**Focus**: T-7 — Parse mod.json body/head/arena sections into catalog (D3b)

### What Was Done

**T-7 implemented — `modmgrRegisterModJsonContent()` added to `modmgr.c`:**

- New static function `modmgrRegisterModJsonContent(modinfo_t *mod)` re-reads mod.json and parses the `content.bodies`, `content.heads`, and `content.arenas` arrays
- For each item, calls `assetCatalogRegisterBody/Head/Arena()` with parsed fields; sets `category = mod->id`, `bundled = 0`, `enabled = 1`
- `runtime_index` is computed as `assetCatalogGetCountByType(type)` + sequential offset — slots new entries after all existing base + prior mod entries
- `modmgrLoadMod()` TODO (D3b) replaced with call to `modmgrRegisterModJsonContent()`
- Forward declaration added in the forward-declarations section

**mod.json content schema defined:**
```json
"bodies":  [ { "id": "...", "bodynum": N, "name_langid": N, "headnum": N, "requirefeature": N } ]
"heads":   [ { "id": "...", "headnum": N, "requirefeature": N } ]
"arenas":  [ { "id": "...", "stagenum": N, "name_langid": N, "requirefeature": N } ]
```
Catalog IDs are `"{modid}:{item_id}"`. Parser handles `0x`-prefixed hex values (existing json_tok_int uses `strtol(…,0)`).

**Build**: `modmgr.c` compiles clean (individual file build, exit 0). Full parallel build blocked by pre-existing TEMP dir permission issue (same as S76).

### Files Modified
`port/src/modmgr.c` (new function + modmgrLoadMod wiring + forward decl)

### Decisions Made
- Re-parse mod.json at load time (not scan time) — scanning stays cheap; loading is where registration happens
- Component-based content (maps, characters via `_components/` dirs) handled by `assetCatalogScanComponents()`; mod.json bodies/heads/arenas are the complementary path for assets declared by global index
- `runtime_index` assignment: query catalog count before starting each type's loop, then increment local counter — avoids races and works correctly across multiple mods

### Next Steps
- Playtest: enable a mod with mod.json bodies/heads/arenas, verify they appear in character/arena pickers
- Continue C-5 tex intercept (see S74 next steps)
- B-49: get crash log from toilet/Carrington Institute scenario

---

## Session 76 -- 2026-03-29

**Focus**: B-49 diagnostic logging — post-landing freeze investigation

### What Was Done

**B-49 instrumented — JUMP_DEBUG: logging added to `bwalkUpdateVertical()`:**

All logging uses `LOG_NOTE` with a `JUMP_DEBUG:` prefix (filterable in log output).

Checkpoints added (in order of execution on a landing tick):
1. **NOCOLLISION branch entry** — logs `newManground`, `newVelY`, `isfalling` immediately when CDRESULT_NOCOLLISION is taken
2. **COLLIDED branch** — logs `isfalling` cleared with `manground`/`ground` state
3. **Landing block entry** (line ~1443) — logs `bdeltaY`, `manground`, `ground`, `floortype`, `floorflags`; fires whenever `bdeltapos.y < 0 && vv_manground <= vv_ground`
4. **Landing sound block entry** — logs `bdeltaY`, `floortype`, `chr` pointer
5. **footstepChooseSound (1st)** — before and after, with `footstep` and returned `sound` value
6. **psCreate footstep1** — before and after
7. **footstepChooseSound (2nd)** — before and after
8. **psCreate footstep2** — before and after (conditional on sound != -1)
9. **Landing grunt check** — logs `mplayerisrunning`, `headnum`, `fallframes`
10. **psCreate landing grunt** — before and after (conditional)
11. **Landing block complete** — before `bdeltapos.y = 0`
12. **Pre-crouchloop** — logs `lvupdate240`, `crouchtime240`, `crouchfall` (confirms the 4-iter cap)
13. **func0f065e74 entry** — logs old pos + new pos (room traversal — key suspect for movable-prop freeze)
14. **func0f065e74 done** — immediately after return
15. **Function complete** — logs final `manground`, `ground`, `bdeltaY`, `isfalling`

Also fixed misleading log comment: `"JUMP_MOVE: ... result=%d (0=nocol)"` → `(1=nocol)` (CDRESULT_NOCOLLISION=1).

Syntax check (`gcc -fsyntax-only`) passed clean.

### Files Modified
`src/game/bondwalk.c`

### Decisions Made
- Logging is unconditional (fires every relevant tick, not just on landing) — provides context even for non-landing ticks showing the grounded loop state
- All casts use `(s32)` for enum/u8 fields to avoid format-string warnings

### Next Steps
- Reproduce B-49 (fall from vent in Felicity, or landing on toilet in Carrington Institute), filter log for `JUMP_DEBUG:`
- The last `JUMP_DEBUG:` line before silence identifies the exact hang location
- Key suspects: `psCreate` calls (sound system deadlock) vs `func0f065e74` (room traversal loop on movable prop)
- If freeze is in `func0f065e74`: check whether player is in a room-0 or no-room state on landing; the toilet's room membership may confuse the traversal

---

## Session 75 -- 2026-03-29

**Focus**: Arena picker garbled names (B-48 fixed) + Felicity freeze investigation (B-49)

### What Was Done

**B-48 fixed — arena picker garbled/duplicate names:**
- Root cause: `catalogArenaCollect()` in `pdgui_menu_room.cpp` called `langGet(e->ext.arena.name_langid)` directly. For langids 0x5126–0x5152 (GoldenEye X, Bonus, Random arena groups), the AIO mod's runtime language file maps those IDs to old PerfectHead/Game Boy Camera UI strings (e.g., "Load A Saved Head" instead of "Frigate"). These returned non-empty garbage strings that passed the `!name[0]` filter and polluted the dropdown.
- `pdgui_menu_matchsetup.cpp` already had `arenaGetName()` — a wrapper with a 43-entry override table for the broken range that falls back to `langGet()` for non-broken IDs.
- Fix: removed `static` from `arenaGetName()` in `pdgui_menu_matchsetup.cpp`; added forward declaration in `pdgui_menu_room.cpp`; changed `catalogArenaCollect` to call `arenaGetName((u16)e->ext.arena.name_langid)`.
- Both files pass syntax check (`-fsyntax-only`). Full build blocked by TEMP dir permission issue on this machine.

**B-49 investigated — Felicity/toilet freeze:**
- Log analyzed: freeze on Felicity (stagenum=0x43) after long fall from vent. Last log: `JUMP_MOVE: tryMove=-3.30 result=1` — result=1 = CDRESULT_NOCOLLISION (comment label was misleading).
- **Important**: Felicity MP setup (mp_setupmp11.c) has NO toilets — only weapons and ammo crates. The toilet freeze described by user (landing on toilet in bathroom, Carrington Institute) is a SEPARATE scenario from this log.
- Freeze point is within same tick as JUMP_MOVE NOCOLLISION, before next tick starts. After NOCOLLISION: `vv_manground=-480`, `bdeltapos.y=fallspeed` (negative) → landing block triggers → `psCreate` for landing sounds. `lvupdate240` capped at 4, no loop risk.
- Cannot isolate exact hang point without debugger or more targeted logging. Registered as B-49.

### Files Modified
`port/fast3d/pdgui_menu_matchsetup.cpp` (removed `static` from `arenaGetName`),
`port/fast3d/pdgui_menu_room.cpp` (forward decl + use `arenaGetName` in `catalogArenaCollect`)

### Decisions Made
- Arena name lookup uses `arenaGetName()` as single source of truth; no duplicate override table in room.cpp.
- B-49 toilet/fall freeze: needs reproduction log from Carrington Institute (not Felicity) to investigate the toilet scenario specifically.

### Next Steps
- Test arena picker in-game to confirm names are correct (especially GEX arenas: Frigate, Archives, Bunker, etc.)
- Get crash log from toilet scenario (Rit/Carrington Institute stage) for B-49
- Continue C-5 tex intercept from S74 roadmap

---

## Session 74 -- 2026-03-28

**Focus**: Catalog activation — C-2-ext, catalogLoadInit, C-4 file intercept

### What Was Done

**C-0 verified** — `assetCatalogInit()`, `assetCatalogRegisterBaseGame()` (which internally calls `assetCatalogRegisterBaseGameExtended()`), `assetCatalogScanComponents()`, and `assetCatalogScanBotVariants()` were already wired in `main.c`. No work needed.

**C-2-ext: source numeric ID fields added to `asset_entry_t`**
- Four new fields: `source_filenum`, `source_texnum`, `source_animnum`, `source_soundnum` (all initialized to -1 in `assetCatalogRegister()`)
- `assetCatalogGetByIndex(s32 index)` added for O(1) pool access
- `assetcatalog_base.c`: base body/head entries now carry their ROM filenum from `g_HeadsAndBodies[bodynum/headnum].filenum`
- `assetcatalog_scanner.c`: ASSET_CHARACTER entries resolve `bodyfile` basename to ROM filenum via `romdataFileGetNumForName()` — mod characters get `source_filenum` set during scan

**catalogLoadInit (new `assetcatalog_load.c/h`)**
- Four static reverse-index arrays: `s_FilenumOverride[2048]`, `s_TexnumOverride[4096]`, `s_AnimnumOverride[2048]`, `s_SoundnumOverride[4096]`
- `catalogLoadInit()` scans non-bundled enabled entries, populates arrays
- `catalogGetFileOverride/TextureOverride/AnimOverride/SoundOverride()` answer in O(1)
- Called from `main.c` after catalog population and before `modmgrLoadComponentState()`

**C-4: file intercept in `romdataFileLoad()`**
- Checks `catalogGetFileOverride(fileNum)` before the `files/` directory lookup
- Hit: loads from mod path, logs `C-4: file N loaded from catalog mod`
- Miss: falls through to existing `files/` + ROM path unchanged
- All 16+ `fileLoadToNew` callers benefit transparently — zero call site changes

**Build**: both `pd` and `pd-server` clean. Server stub added for `romdataFileGetNumForName`.

**Commit**: `b01084b` — "Catalog activation: C-2-ext + catalogLoadInit + C-4 file intercept"

### Files Modified
`port/include/assetcatalog.h`, `port/include/assetcatalog_load.h` (new), `port/src/assetcatalog.c`, `port/src/assetcatalog_base.c`, `port/src/assetcatalog_load.c` (new), `port/src/assetcatalog_scanner.c`, `port/src/main.c`, `port/src/romdata.c`, `port/src/server_stubs.c`

### Decisions Made
- `catalogGetFileOverride` returns `ext.character.bodyfile` for ASSET_CHARACTER entries — this is the mod's body model path, which is what C-4 needs for character overrides
- Server has `romdataFileGetNumForName` stubbed to return -1 (server never loads ROM files; source_filenum on server catalog entries stays -1, override arrays stay empty)
- `assetCatalogGetByIndex` returns `const asset_entry_t *` (read-only) to prevent load module from accidentally mutating catalog entries

### Next Steps
- **C-5 tex intercept**: locate `texLoad()` / `texDecompress()` bottleneck and add `catalogGetTextureOverride(texnum)` check
- **C-6 anim intercept**: find `animLoadFrame`/`animLoadHeader`, add `catalogGetAnimOverride(animnum)`
- **C-7 snd intercept**: find `sndStart` path, add `catalogGetSoundOverride(soundnum)`
- **C-8 mod enable/disable**: wire `catalogLoadInit()` re-call into `modmgrSetComponentEnabled()` so override arrays rebuild when mods are toggled
- **Playtest C-4**: install a mod character that declares a `bodyfile` matching a ROM filenum; load a stage with that character; verify `C-4:` log line appears

---

## Session 73 -- 2026-03-28

**Focus**: B-46 void spawn (Felicity 0x2b) + B-47 exit freeze on window close

### What Was Done

**B-46 fixed — void spawn on MP stages:**
- `setup.c`: intro validator no longer nulls intro for official MP setup files. MP setups have a 1-word intro section (`[INTROCMD_END]`, dist=4 bytes from props). The 64-byte distance heuristic was added for corrupt mod files — skipped when `filenum == mpsetupfileid`. firstCmd validity check still applies for all files.
- `playerreset.c`: B-19 pads fallback condition expanded from `g_NetMode != NETMODE_NONE` to `g_NetMode != NETMODE_NONE || g_Vars.normmplayerisrunning`. Covers local Combat Sim where netmode=NETMODE_NONE but normmplay=true. Added `LOG_WARNING` when fallback finds 0 valid pads (numpads + netmode + normmplay logged for diagnosis).

**B-47 fixed — exit freeze on window close:**
- `system.h/c`: Added `bool g_AppQuitting` global. Added `#include <stdbool.h>` to system.h.
- `main.c`: Set `g_AppQuitting = true` at start of `cleanup()` atexit, before `netDisconnect()`.
- `net.c`: Stage-transition block in `netDisconnect()` gated on `!g_AppQuitting`. Prevents deadlock from calling `mainEndStage()` + `mainChangeToStage()` with no render loop.
- `netupnp.c`: `netUpnpTeardown()` skips `UPNP_DeletePortMapping` when `g_AppQuitting`. The synchronous HTTP call blocked 10–30 s on unreachable routers; port mapping expires naturally.

**Files modified:** `src/game/setup.c`, `src/game/playerreset.c`, `port/include/system.h`, `port/src/system.c`, `port/src/main.c`, `port/src/net/net.c`, `port/src/net/netupnp.c`

### Decisions Made
- Used `filenum == mpsetupfileid` (not `normmplayerisrunning`) for the dist check in setup.c — more precise, independent of runtime state that may not be set at load time.
- `g_AppQuitting` lives in system.h/c so both net.c and netupnp.c can check it without circular includes.
- UPnP port mapping is NOT removed on quit — mappings expire naturally; blocking HTTP on exit is worse.

### Next Steps
- Build: `build-headless.ps1`
- Playtest Felicity: no more void spawns. Watch for "populated N spawn points from pad file (B-19 fallback)" log.
- Test window X close during match: should exit cleanly within <1 s. Watch for "UPNP: skipping port mapping removal (app quitting)" in log.
- If B-19 still finds 0 pads: check new diagnostic "B-19 fallback found 0 valid pads (numpads=X netmode=Y normmplay=Z)".

---

## Session 72 -- 2026-03-28

**Focus**: Bot name/char sync in CLC_LOBBY_START + player identity name fallback (worktree merge from serene-margulis)

### What Was Done

**B-44 fixed** — `netmsgClcLobbyStartWrite` now appends a per-bot config payload from `g_MatchConfig.slots[]` for each `SLOT_BOT` entry up to `numSims`. Each bot entry encodes: name (32 chars, NUL-padded), bodynum, headnum, difficulty, type. `netmsgClcLobbyStartRead` on the server side decodes and populates `g_BotConfigsArray[bi]` with all five fields. Bots now arrive with the names and appearance the room host configured.

**B-26 re-fixed** — `netClientReadConfig` now uses `g_GameFile.name` as a middle fallback between the identity profile (always empty on clients that never call `identityInit`) and the legacy N64 name. The original fix (S49) relied on `identityGetActiveProfile()`, which is always null on clients. The new fallback uses the agent name already loaded from the save file, which is correct.

**Y-axis inversion (B-43)** — No code change needed. `inputControllerSetInvertRStickY` + `configSave` are already correctly wired from the checkbox in `pdgui_menu_mainmenu.cpp`.

**Header unification** — `netmsg.c` was updated to `#include "scenario_save.h"` (the canonical matchslot/matchconfig source, established in S71) instead of the new `matchsetup.h` that serene-margulis created. `matchsetup.h` was not imported — would have duplicated types already in `scenario_save.h`. Dev's `matchsetup.c` already uses `scenario_save.h` from S71.

**Files modified:**
- `port/src/net/net.c` — `netClientReadConfig` player name fallback via `g_GameFile.name`
- `port/src/net/netmsg.c` — per-bot payload in `netmsgClcLobbyStartWrite/Read`; include swapped to `scenario_save.h`

### Decisions Made
- Serene-margulis created `port/include/net/matchsetup.h` with the same types as `scenario_save.h`. Not merged — scenario_save.h is canonical (S71 decision). Only the functional changes (netmsg.c, net.c) were brought over.
- Commit went to `dev` branch directly. Session number bumped to S72 since S71 was already taken by the scenario save commit from youthful-goldstine.

### Next Steps
- Build: `build-headless.ps1`
- Playtest: Start match with bots → verify bot names/appearances match room config
- Server log check: after CLC_AUTH, verify name shows agent name not "Player 1"
- Smoke test S71 scenario save too — both changes need first build validation

---

## Session 71 -- 2026-03-28

**Focus**: Combat Simulator scenario save/load system

### What Was Done

**New feature: scenario save/load** — players can save and reload complete Combat Simulator match configurations (arena, game mode, limits, options, weapon set, bot roster) as JSON files.

**Files created:**
- `port/include/scenario_save.h` — Canonical header for `struct matchslot`, `struct matchconfig`, constants (MATCH_MAX_SLOTS, SLOT_*, NUM_MPWEAPONSLOTS, MAX_PLAYER_NAME), and API declarations for both matchsetup.c and scenario_save.c. Safe for C and C++ (no types.h).
- `port/src/scenario_save.c` — Full implementation: JSON writer (fprintf + manual escaping), minimal JSON reader (strstr + sscanf based, handles flat objects + bots array), directory create, file listing via POSIX opendir/readdir.

**Files modified:**
- `port/src/net/matchsetup.c` — Added `#include "scenario_save.h"`, removed ~37 lines of now-redundant local struct/constant definitions. Consolidates ownership to single source.
- `port/fast3d/pdgui_menu_room.cpp` — Replaced duplicate extern "C" struct block with `#include "scenario_save.h"`. Added Save/Load UI to Combat Sim tab: "Save Scenario" button opens popup with name input; "Load Scenario" button shows list of saved files with click-to-load. Popups follow the same modal pattern as bot settings. Status message shows after save/load. `pdguiRoomScreenReset()` clears new state.

**Dynamic player count handling (spec):**
- `scenarioLoad(filepath, humanCount)` — calls `matchConfigInit()` first (sets up local player), applies saved settings, then adds bots up to `MATCH_MAX_SLOTS - humanCount` in order; excess silently dropped.
- humanCount passed from `lobbyGetPlayerCount()` in the UI at load time.

**Save path:** `$S/scenarios/<name>.json` — directory auto-created on first save.

**MPOPTION_NODOORS** also added to the options toggles in the Combat Sim tab (was missing).

**New helper `syncSpawnWeaponFromConfig()`** — syncs `s_SpawnWeaponIdx` dropdown from `g_MatchConfig.spawnWeaponNum` after a load.

### Decisions Made
- No external JSON library — hand-rolled writer (fprintf) and reader (strstr/sscanf). The format is self-generated and predictable; no general-purpose parser needed.
- `scenario_save.h` is the single source of truth for matchslot/matchconfig types. Both C and C++ callers include it; C++ inside `extern "C" {}`.
- `MATCH_MAX_SLOTS` in the header uses `#ifndef` guard so matchsetup.c (which defines it as `PARTICIPANT_DEFAULT_CAPACITY`) is not conflicted.
- Human player slots are NOT saved in scenario files — they're session-specific. Only bot slots are serialized.

### Next Steps
- Build: `build-headless.ps1`
- Smoke test: Launch → Room → Combat Sim tab → Save Scenario "test1" → verify `$S/scenarios/test1.json` created
- Load test: Load Scenario → select test1 → arena/bots/options should match what was saved
- Multi-human test: Save with 1 human; load with 4 humans in room — only 28 bots should populate

---

## Session 70 -- 2026-03-28

**Focus**: First-tick crash after stage load: g_MpAllChrPtrs NULL dereference in lvTick + deep investigation of scenarioTick and bot AI first-tick safety

### What Was Done

**B-43 fixed** -- Game crashed on the first game tick after a successful stage load (Ravine, 1 player + 5 bots).

**Root cause**: `lv.c:2391` iterates `for (i = 0; i < g_MpNumChrs; i++) { g_MpAllChrPtrs[i]->actiontype }` with no NULL check.
`mpReset()` increments `g_MpNumChrs` for player slots (1 for 1 human) and then NULLS the entire `g_MpAllChrPtrs[0..MAX_MPCHRS]` array.
Bot spawns (`botmgrAllocateBot`) then insert chrs at indices `g_MpNumChrs..g_MpNumChrs+5` (= 1..6 for 5 bots).
The player chr is ONLY written to `g_MpAllChrPtrs[0]` lazily by `playerTickChrBody()`, which runs from `playerTick` inside `propsTick` -- which hasn’t run yet on the first tick.
So `g_MpAllChrPtrs[0] == NULL` when the loop runs. Deref crashes.

**Propagation check -- same class found in two more places:**
- `bot.c:2358` `botGetTeamSize()` -- iterated from i=0, no NULL guard. Would crash on first bot AI tick in same frame.
- `mplayer.c:712` `mpCalculateTeamIsOnlyAi()` -- inner player-loop `g_MpAllChrPtrs[j]` with no NULL guard (benign today because teams are disabled by default, but latent for teams-enabled matches).

**Three files changed (B-43):**
- `src/game/lv.c:2391` -- added `if (g_MpAllChrPtrs[i] && ...)` guard in numdying loop
- `src/game/bot.c:2358` -- added `if (!g_MpAllChrPtrs[i]) continue;` in botGetTeamSize
- `src/game/mplayer/mplayer.c:712` -- added `if (!g_MpAllChrPtrs[j]) continue;` in mpCalculateTeamIsOnlyAi team loop

**scenarioTick() first-tick safety investigation:**
- `scenarioTick()` checks `g_Vars.normmplayerisrunning`, then dispatches to `g_MpScenarios[scenario].tickfunc`.
- For Combat Sim (scenario 0), `tickfunc` is **NULL** -- the dispatch is gated by `if (tickfunc)`. **SAFE on tick 0.**
- Tick-0 risk for other scenarios (HTB/HTM/PAC/KOH/CTC) would be their `tickfunc` bodies -- not audited as they are not tested yet.
- Added first-call trace log to `scenarioTick()`: logs `lvframe60/scenario/normmplayerisrunning/tickfunc`.

**botTick() / botApplyMovement() first-tick safety investigation:**
- `botTick()`: the full AI block (`botTickUnpaused`, angle calc, `g_MpAllChrPtrs[followingplayernum]` deref) is guarded by `if (updateable && g_Vars.lvframe60 >= 145)`. **SAFE on tick 0.**
- `botApplyMovement()` runs UNCONDITIONALLY every tick. It calls `playerChooseThirdPersonAnimation(chr, ...)` and then `modelSetChrRotY(chr->model, ...)`. Both crash if `chr->model == NULL`.
- `playerChooseThirdPersonAnimation` directly dereferences `chr->model` for `modelGetAnimNum(chr->model)` without a guard.
- **Fixed**: added `if (!chr->model) return false;` guard in `botApplyMovement()` after the existing `!chr || !chr->aibot` check.
- Added first-call trace log to `botTick()`: logs `lvframe60/chr/model/rooms[0]/aibot`.

**Trace logging added (all sessions):**
- `lv.c:2151` -- first-call log `tick=%d stagenum=0x%02x g_MpNumChrs=%d`
- `lv.c:2536` `lvTickPlayer` -- per-call `playernum/prop/MpAllChr`
- `scenarios.c:scenarioTick` -- first-call `lvframe60/scenario/normmplayerisrunning/tickfunc`
- `bot.c:botTick` -- first-call `lvframe60/chr/model/rooms[0]/aibot`

**Build**: Awaiting. All changes are NULL guards + trace logs -- no new functions, no struct changes.

### Decisions Made
- `g_MpAllChrPtrs[0..PLAYERCOUNT-1]` being NULL on first tick is by design (lazy init via playerTickChrBody); any loop iterating `g_MpAllChrPtrs[0..g_MpNumChrs-1]` must NULL-guard, not assume all populated
- Do not eagerly call playerTickChrBody during playerSpawn to avoid changing spawn timing; NULL guard is the correct minimal fix
- `botApplyMovement()` model guard is defense-in-depth: bots spawned at level setup should have models by first tick, but a NULL model is undefined behavior and the guard costs nothing
- `scenarioTick()` for Combat Sim is architecturally safe (no tickfunc); risk only exists for non-Combat-Sim scenarios which are untested

### Next Steps
- Build: `build-headless.ps1`
- Playtest: 1 player + 5 bots Combat Sim on Ravine -- match should now start and run through tick 1
- Watch for "TICK: lvTick enter tick=0" + "TICK: lvTickPlayer playernum=0" in log -- confirms tick and player path both reached
- Watch for "TICK: scenarioTick first call lvframe60=0 scenario=0 tickfunc=0x0" -- confirms no tickfunc dispatched tick 0
- Watch for "TICK: botTick first call lvframe60=..." -- confirms bot chr props are being ticked
- If still crashing: suspect `chrTick()` path (called unconditionally from botTick) -- investigate `chraTick(chr)` which is called from `chrTick` and may access chr state

---

## Session 68 — 2026-03-28

**Focus**: Combat Sim post-milestone fixes: jump crash, time limit alarm, spawn weapon, Add Bot cap

### What Was Done

**MILESTONE**: Combat Sim match running (confirmed in playtest — 7 bots on Jungle, 25+ seconds gameplay).

**B-39 fixed** — Jump crash in `bmoveFindEnteredRoomsByPos` (bondmove.c:2424).
Root cause: `g_Vars.players[playermgrGetPlayerNumByProp(player->prop)]->vv_eyeheight/headheight` — same players[-1] OOB class as S66 audit. Crash at specific Jungle map geometry during ceiling probe binary search. Fix: read `player->vv_eyeheight/player->vv_headheight` directly (same struct, no lookup).

**B-40 fixed** — Time limit alarm fires at match start.
Root cause: `netmsgClcLobbyStartRead` hardcoded `g_MpSetup.timelimit = 0` — comment said "unlimited" but timelimit=0 means 1 minute (mpApplyLimits: `timelimit >= 60` = unlimited). Fix: added `u8 timelimit` + `u32 options` to CLC_LOBBY_START payload; `g_MatchConfig.timelimit` now flows from room UI to server.

**B-41 fixed** — Spawn weapon not auto-equipping.
Root cause: `g_MpSetup.options = 0` hardcoded in CLC_LOBBY_START handler stripped `MPOPTION_SPAWNWITHWEAPON`. player.c:1186 gates equip on that flag. Fix: `g_MatchConfig.options` also wired through CLC_LOBBY_START.

**B-42 fixed** — Add Bot button limited to 7 bots.
Root cause: `maxBots = MAX_PLAYERS - humanCount` (= 7 with 1 human). Fix: changed to `MATCH_MAX_SLOTS - humanCount` (up to 31 bots with 1 human).

**Protocol change**: CLC_LOBBY_START gains 5 bytes (u8 timelimit + u32 options). All callers updated: pdgui_bridge.c, pdgui_menu_room.cpp, netmenu.c. `netLobbyRequestStart` (co-op path) defaults timelimit=60, options=0.

**Build**: client + server both clean. Commit: `169d5ab` S68.

### Decisions Made
- bmoveFindEnteredRoomsByPos: direct `player->` field access is canonical; no reason to go through `g_Vars.players[]` lookup when the player struct is already available
- timelimit=60 chosen as "unlimited" sentinel (matching mpApplyLimits convention)
- options wired alongside timelimit so all match settings flow together; future R-4 wiring can extend this pattern

### Next Steps
- Playtest B-39: jump should no longer crash on Jungle (or any map)
- Playtest B-40: time limit alarm should not fire immediately; match should run until the configured limit
- Playtest B-41: enable "Start Armed" in room UI; player should spawn holding the weapon
- Playtest B-42: add more than 7 bots in room UI; button should stay enabled up to 31
- If jump still crashes: check `bmoveFindEnteredRooms` (line 2440) — also accesses `player->prop->pos` which could be NULL on dedicated server

---

## Session 67 — 2026-03-28

**Focus**: bodiesReset crash fix — Combat Sim stage load

### What Was Done

**B-37 fixed** — client crash in `bodiesReset()` during Combat Sim stage load (stagenum=0x1f, Ravine).

**Root cause**: `bodiesReset` was not MP-aware. It calls `rngRandom() % g_NumBondBodies` first (div-by-zero if `g_NumBondBodies == 0`) then iterates guard head model lists. In a networked Combat Sim match there are no guards — only players and bots — so the entire guard-body randomization pass is both useless and unsafe. The init-order audit (S64) had previously classified bodiesReset as "safe", but that audit was done when the crash was manifesting further down in `setupCreateProps`. After the S64/S65 fixes, the crash surfaced here.

**Fix** (`src/game/bodyreset.c`):
- Added `#include "system.h"` for logging
- Entry log: dumps `normmplay`, `g_NumBondBodies`, `g_NumMaleGuardHeads`, `g_NumFemaleGuardHeads`
- After modeldef-clear loop: log with count
- Early return when `g_Vars.normmplayerisrunning` — zeroes both head indices, returns
- Remaining SP-path logs at each crash-candidate site (rng, male heads, female heads, done)

**Server stubs verified**: `server_stubs.c` `mainChangeToStage()` already sets `g_MainChangeToStageNum = stagenum` — fix was committed in a prior session.

**Build**: client + server both compile clean.
**Commit**: `22c7861` S67

### Decisions Made
- MP path skips guard body randomization entirely — no guards in any MP scenario, data never used
- Trace logs left in permanently for stage-load diagnostic coverage

### Next Steps
- Playtest: 1 player + 1 bot Combat Sim on Ravine (stagenum=0x1f) — confirm B-37 resolved
- Watch for "BODIES: normmplay active — skipping guard body randomization" in the log
- If still crashing: logs now identify exactly which line fails

---

## Session 65 — 2026-03-27

**Focus**: Audit 3 — Stage Load Initialization Order (fresh start)

### What Was Done

Produced `context/init-order-audit.md` — full documentation of the networked Combat Sim stage load sequence.

**Three-phase sequence documented:**
- **Phase 0** (`netmsgSvcStageStartRead`): SVC_STAGE_START parsing, g_MpSetup population, `mpParticipantsFromLegacyChrslots` (malloc pool, survives MEMPOOL_STAGE reset), `mpStartMatch` (queues async stage change, sets `g_Vars.perfectbuddynum = 1`)
- **Phase 1** (`pdmain.c`): Pool reset (MEMPOOL_STAGE wiped), `playermgrReset` (players[] = NULL), `playermgrAllocatePlayers` (players non-null, no props yet), `mpReset` (sets `normmplayerisrunning`)
- **Phase 2** (`lvReset`): Full init sequence from `bgReset` through `playerSpawn` — first valid `->prop` access

**Key dependency graph produced:**
- `mpParticipantsFromLegacyChrslots` MUST run before `mpReset` and before `setupLoadFiles`
- `setNumPlayers` MUST run before `playermgrAllocatePlayers`
- `mpReset` (setting normmplayerisrunning) MUST run before `lvReset`
- `setupLoadFiles` MUST run before `varsReset` (sets g_Vars.maxprops)
- `playerSpawn()` is the FIRST point where `players[i]->prop` is valid

**N64 vs PC differences documented:**
- N64 `mainChangeToStage` is synchronous; PC is async (queued, fires next frame)
- Participant pool must use `malloc` (not `mempAlloc`) to survive MEMPOOL_STAGE reset
- `musicSetStageAndStartMusic` called pre-spawn on PC (B-36 fix required)

**Current crash analysis (§4.2):**
- Crash is after "setupLoadFiles done" log, in `setupCreateProps` → `chrmgrConfigure` or props iteration
- H1: chrslots inconsistency between setupLoadFiles and setupCreateProps numchrs counts
- H2: model slot under-allocation → OOB in modelmgrInstantiateModel
- H3: bad g_Vars.roomcount before varsReset
- Instrumentation recommendations: add log after chrmgrConfigure call to isolate crash location

### Decisions Made
- `init-order-audit.md` added to context domain files (README.md updated)
- Three-phase model (Network → Frame Boundary → lvReset) is the canonical mental model for stage load debugging

### Next Steps
- Build verify SP-6 + B-36 fixes (build-headless.ps1)
- Use instrumentation recs from §5.1 to isolate current crash in setupCreateProps
- Playtest: 2-player Combat Sim end-to-end

---

## Session 66 — 2026-03-27

**Focus**: Deep Audit 4 of 4 — Bot / Simulant / AI System null-guard sweep

### What Was Done

**28 critical/high bugs fixed across 6 files** — all instances of the same 4 bug classes:

**CAT-1 — `g_Vars.currentplayer` NULL on dedicated server** (5 fixes):
- `chraction.c:4416` — invincibility check: added `currentplayer != NULL` guard
- `botcmd.c:205–230` — all 4 AI commands (FOLLOW/PROTECT/DEFEND/HOLD): wrapped with `currentplayer != NULL`

**CAT-2 — PLAYERCOUNT()=0 on server → bot loops hit human chrs (aibot=NULL)** (3 fixes):
- `mplayer/mplayer.c:688` — `mpCalculateTeamIsOnlyAi`: added `!g_MpAllChrPtrs[i]->aibot` guard
- `bot.c:2364` — `botGetCountInTeamDoingCommand`: added aibot NULL guard
- `bot.c:2407` — `botGetNumTeammatesDefendingHill`: added aibot NULL guard

**CAT-3 — g_MpAllChrPtrs[] accessed without bounds or NULL check** (9 fixes):
- `botcmd.c:61,81` — followingplayernum / attackingplayernum: added `< g_MpNumChrs && != NULL` guards
- `botact.c:241` — Farsight loop: added `if (!oppchr) continue`
- `bot.c:1685` — attackingplayernum: added bounds/NULL guard
- `bot.c:3009,3056` — POPACAP victim index: full bounds-checked lookup with NULL-result handling
- `bot.c:3311` — MA_AIBOTFOLLOW: added `>= g_MpNumChrs || == NULL` guards
- `bot.c:2434` — `botGetNumOpponentsInHill`: added `!g_MpAllChrPtrs[i]` guard
- `mplayer/scenarios.c:504` — `scenarioCreateMatchStartHudmsgs`: added `!= NULL` before `->aibot`

**CAT-5 — chrGetTargetProp()/target->chr dereferenced without NULL check** (11 fixes):
- `botcmd.c:67–80` — follow-distance: `if (target != NULL)` wrapper
- `botact.c:366` — throw: `target != NULL` + `target->chr` fallback
- `botact.c:547` — Slayer rocket: `target == NULL` → immediate detonate
- `bot.c:3193` — fallback attack: `chtarget != NULL && ->chr != NULL` guard
- `bot.c:3303–3305` — MA_AIBOTATTACK inline: guard added
- `bot.c:3317–3339` — canbreakfollow: refactored to `fbtarget` local with full guard
- `bot.c:3379–3387` — defend/hold canbreak: refactored to `dhtarget` local with full guard
- `bot.c:2741` — KazeSim attack: `kazetarget` local with ternary fallback (-1)
- `bot.c:2899–2902` — KotH hill attack: `kohtarget` local with full guard
- `bot.c:3680` — firing check: added `!= NULL && ->chr != NULL` before `chrIsDead`

**CAT-4 — playermgrGetPlayerNumByProp() → players[-1] OOB** (5 fixes):
- `chraction.c:4462–4512` — healthscale/armourscale (3 branches): `chrpnum >= 0` guard
- `chraction.c:4689` — isdead check: guard + remote fallback via `ACT_DEAD`
- `botact.c:248` — Farsight speed prediction: guard + remote fallback via `ACT_STAND`
- `bot.c:823` — `botGetWeaponNum`: guard + return `WEAPON_NONE`
- `bot.c:3166` — weakest player health: guard + remote fallback via `maxdamage - damage`

**Context created**: `context/null-guard-audit-bots.md` — full findings, CAT labels, code before/after

**Build**: Code changes syntactically correct. Direct build blocked by pre-existing DevkitPro/mingw64
environment conflict in Claude shell (not a code issue — same as S63/S64/S65). Needs `build-headless.ps1`.

### Decisions Made
- `chrGetTargetProp(chr)->chr` inline calls are always unsafe — must extract to local variable first
- Remote player fallbacks: `ACT_DEAD` for isdead, `maxdamage - damage` for health, `ACT_STAND` for speed
- Slayer rocket with NULL target detonates immediately (`timer240 = 0`) — safe fallback
- `attackingplayernum` / `followingplayernum` must always be checked: `>= 0 && < g_MpNumChrs && != NULL`

### Next Steps
- **Build verify**: `powershell -File devtools/build-headless.ps1 -Target all` from PowerShell
- **Playtest**: Combat Sim, dedicated server — bots should spawn and fight without crash
- B-36 playtest (client crash on stage load) still pending from S63
- R-2 / Room lifecycle or J-1 end-to-end join verification

---

## Session 64 — 2026-03-27

**Focus**: SP-6 Systemic Null-Guard Audit — Full PLAYERCOUNT() loop sweep across src/game/

### What Was Done

**Trigger**: S63 fixed `music.c::musicIsAnyPlayerInAmbientRoom` (B-36) and identified SP-6:
PLAYERCOUNT() counts non-null entries but loops iterate by index — sparse slots crash.
Full audit of all PLAYERCOUNT() loops across 29 files in src/game/.

**CRITICAL fix (pre-spawn call path)**:
- `src/game/roomreset.c:33` — `roomsReset()` called via `bgReset()` at `lvReset:364`, BEFORE
  players are spawned (init loop is at `lvReset:483`). Added guard.

**HIGH fixes — 13 loops across 8 files**:
- `src/game/bondgun.c` (`bgunSetPassiveMode`) — guard added
- `src/game/lv.c:2184` (`lvTick` hasdotinfo) — guard added
- `src/game/lv.c:2194` (`lvTick` joybutinhibit) — guard added
- `src/game/lv.c:2217` (`lvTick` smart slowmo outer) — player + prop guard (`->prop->rooms`)
- `src/game/lv.c:2225` (`lvTick` smart slowmo inner otherplayernum) — guard added
- `src/game/lv.c:2371` (`lvTick` numdying) — guard added
- `src/game/bondcutscene.c:15` (`bcutsceneInit`) — guard added
- `src/game/bondgunstop.c:15` (`bgunStop`) — guard added
- `src/game/chr.c:1476` (`chrNoteLookedAtPropRemoved`) — guard added
- `src/game/chraction.c:3033` (`chractionDestroyEyespy`) — guard added
- `src/game/chraction.c:5438` (`chrIsOffScreen`) — player + prop guard (`->prop->pos`, `->prop->rooms`)
- `src/game/chraicommands.c:1952` (`aicommand_if_eyespy_at_pad`) — guard added
- `src/game/radar.c:338` (`radarRender`) — full 3-level chain guard (player + prop + chr)

**Already had guards (confirmed safe)**:
- `src/game/lv.c:227` (slayer rocket), `lv.c:483` (player init), `camera.c` (all 4 loops)

**Context created**: `context/systemic-null-guard-audit.md` — full instance table, risk levels,
fix patterns, and future-prevention rules.

**Compile check**: All 7 edited files pass `-fsyntax-only` with project include paths.

### Decisions Made
- All PLAYERCOUNT() loops that dereference `players[i]` MUST have `if (!g_Vars.players[i]) continue;`
  as their first loop-body statement. Canonical rule documented in audit file.
- `->prop` access additionally needs `!players[i]->prop` guard.
- `->prop->chr` chain needs all three guards.

### Next Steps
- Build verify via PowerShell build-headless.ps1 (sandbox TMP issue prevented automated build).
- Playtest: Combat Sim stage load — confirm no crash on stage with ambient music (B-36 scenario).
- **Audit 2**: `g_ChrSlots[i]` and `g_MpAllChrPtrs[i]` access patterns (chr.c, chraction.c).
- **Audit 3**: `mplayer/*.c` participant system interactions during match start.

---

## Session 65 — 2026-03-27

**Focus**: Deep Audit 2 of 4 — Prop + Object System null-guard sweep

### What Was Done

**Files audited**: prop.c, propobj.c, propsnd.c, propanim.c, explosions.c, smoke.c, bg.c, objectives.c. door.c / lift.c / weapon.c do not exist in src/game/ — door/lift/weapon logic is in propobj.c.

**New systemic pattern documented**: SP-8 — `prop->chr` accessed without NULL check after PROPTYPE_CHR/PROPTYPE_PLAYER type check.

**7 critical fixes applied to main working copy**:

1. `propobj.c:4455` — `parent->chr->hidden` in weapon drop/throw — added `if (parent->chr)` guard (CRITICAL)
2. `propobj.c:8392` — `playerprop->chr->hidden` in `cctvTick()` — added `playerprop->chr &&` short-circuit (CRITICAL)
3. `propobj.c:9334-9350` — `hitchr` (hitprop->chr) in laser fence damage block — added `hitchr &&` to team check; wrapped damage calls with `if (hitchr)` (CRITICAL)
4. `propobj.c:9462` — `chrDamageByImpact(targetprop->chr, ...)` in enemy autogun — added `if (targetprop->chr)` wrapper (HIGH)
5. `explosions.c:379` — `g_Rooms[exproom]` OOB when rooms[0] = -1 — extended condition to `|| exproom < 0` (HIGH)
6. `explosions.c:1004` — `chrDamageByExplosion(chr, ...)` with possibly-NULL chr — added `if (chr)` guard (HIGH)
7. `smoke.c:210` — `roomGetFinalBrightnessForPlayer(rooms[0])` with rooms[0] = -1 — added ternary validity guard (HIGH)

**4 files clean** (zero findings): propanim.c, propsnd.c, objectives.c, bg.c.

**New context file**: `context/null-guard-audit-props.md` (full findings, line refs, risk levels, deferred items).

### Decisions Made
- PROPTYPE_PLAYER chr = NULL is valid transitional state; PROPTYPE_CHR chr = NULL is a coding error. Guard player, trust chr.
- `exproom < 0` explosions → numbb=0 (same as HUGE25 path) — visual explosion still plays, no damage BBoxes.
- `rooms[0] = -1` smoke → brightness 0 fallback (invisible but no crash).

### Next Steps
- Build: full client + server build verify
- Playtest: CCTV level with cloak active, laser fence in MP, grenade drops, explosions at map edge, smoke near map seam
- Deep Audit 3 of 4: bot.c, botinv.c, chr.c (g_ChrSlots iteration + target->chr access)

---

## Session 63 — 2026-03-27

**Focus**: B-36 — Client crash on Combat Sim stage load (after skyReset, in music init)

### What Was Done

**Root cause analysis** (`src/game/music.c`, `src/game/lv.c`):
- Trace: `lvReset` → `skyReset` done → `musicSetStageAndStartMusic` (because `normmplayerisrunning=true`) → `musicStartPrimary` then `musicStartAmbient` for stages with ambient tracks → `musicIsAnyPlayerInAmbientRoom()` → `g_Vars.players[i]->prop` without NULL check on `players[i]` → crash.
- During stage load, `PLAYERCOUNT()` can return >0 while `players[0]` is NULL (e.g., prior match cleaned up slot 0 but not slot 1, or player slots in partial state). `PLAYERCOUNT()` macro counts non-null entries but loop iterates by index — slot 0 can be NULL while count is 1.
- `musicStartPrimary` also called `PRIMARYTRACK()` twice, running `mpChooseTrack()` twice (double side-effects on `g_MpLockInfo` and `g_MusicLife60`).

**Fixes applied**:
- `musicIsAnyPlayerInAmbientRoom`: added `g_Vars.players[i]` NULL check before `->prop` dereference. Players not yet spawned correctly return "not in any room".
- `musicStartPrimary`: cache `PRIMARYTRACK()` in local `track` var, call `mpChooseTrack` once only.
- `musicSetStageAndStartMusic`: added step-by-step log lines (enter, before primary, before ambient, done).
- `lv.c` skyReset block: added log with `players[0..3]` null-ness to confirm the scenario.
- Added `#include "system.h"` to `music.c` for `sysLogPrintf`.

**Build result**: Both `lv.c` and `music.c` compile cleanly (verified via direct gcc invocation). Full link blocked by TEMP directory environment issue in current shell context — not a code issue (same build env limitation seen before).

### Decisions Made
- NULL check in `musicIsAnyPlayerInAmbientRoom` is the correct fix: if no player is spawned, no player is in an ambient room, so returning `false` is semantically correct. Ambient track will start naturally once player is in-room during the first tick.
- This is a class of bug: any code that iterates `for (i = 0; i < PLAYERCOUNT(); i++)` and then accesses `players[i]->anything` without a NULL check can crash if slots are sparse. See systemic-bugs.md.

### Next Steps
- Build and playtest: run Combat Sim match on a stage with ambient music (MBR/DD Tower, Maian SOS, Skedar Ruins). Confirm "MUSIC: calling musicStartAmbient" appears in log without crash.
- If crash still occurs: check the new logs for which specific call in `musicSetStageAndStartMusic` is the last one seen — may indicate a different path (e.g., `musicStartPrimary` for stages with no ambient).

---

## Session 61 — 2026-03-27

**Focus**: netSend usage audit + three critical netcode bug fixes (CLC_RESYNC_REQ dropped, g_Lobby.inGame always 0, NPC broadcast guard)

### What Was Done

**Audit — `context/netsend-audit.md` created (NEW)**:
- Full audit of every buffer write + netSend call across 6 files: `net.c`, `netmsg.c`, `netlobby.c`, `netmenu.c`, `pdgui_bridge.c`, `netdistrib.c`.
- Documented 4 message send patterns (per-client out, accumulate-flush, immediate standalone, local buffer).
- Found 3 bugs (CRIT-0, CRIT-1, CRIT-2) — all fixed this session.
- Documented polling patterns: `lobbyUpdate()` called 2–3x/frame (intentional, S55 race fix), `g_Lobby.inGame` always 0 (bug — fixed).
- Finding: SVC_LOBBY_LEADER IS sent (network-audit.md said "NEVER SENT" — corrected).

**Bug-1 (CRITICAL) — CLC_RESYNC_REQ silently dropped** (`port/src/net/netmsg.c`, `net.c`, `net.h`):
- Root cause: handlers for SVC_CHR_SYNC, SVC_PROP_SYNC, SVC_NPC_SYNC wrote resync requests directly to `g_NetMsgRel`. But `netStartFrame()` calls `netbufStartWrite(&g_NetMsgRel)` *after* the event dispatch loop — silently wiping any write made during dispatch. Desync recovery was completely non-functional.
- Fix: Added `g_NetPendingResyncReqFlags` (mirrors server-side `g_NetPendingResyncFlags` pattern). Handlers now set flags; `netEndFrame()` writes the CLC_RESYNC_REQ message before flush.
- Files changed: `net.h` (extern), `netmsg.c` (global + 3 handler fixes), `net.c` (consumer in netEndFrame CLSTATE_GAME block).

**Bug-2 (CRIT-2) — `g_Lobby.inGame` always 0 on dedicated server** (`port/src/net/netlobby.c`):
- Root cause: `g_Lobby.inGame` was set from `g_NetLocalClient->state`. On dedicated server `g_NetLocalClient == NULL`, so check was always false → `inGame` always 0.
- Fix: Walk `g_NetClients[]` array, set `inGame` if any client is `>= CLSTATE_GAME`. Used in `hub.c` room state machine and server GUI.

**Bug-3 (CRIT-1) — NPC broadcast guard never fires on dedicated server** (`port/src/net/net.c`):
- Root cause: Co-op path checked `if (g_NetLocalClient && ...)` before broadcasting SVC_NPC_SYNC. Always false on dedicated server.
- Fix: Changed guard to `if (g_NetNumClients > 0)`.

**`context/network-audit.md` updated**:
- §8 Recommendations: marked CRIT-0 (new, CLC_RESYNC_REQ), CRIT-1, CRIT-2 as Fixed S61. Updated HIGH-1 (SVC_LOBBY_LEADER) — IS sent on join via per-client out buffer; audited correctly.

### Build Result
- **Client**: PASS (`pd` target, 0 errors, verified via `make -C build/client`)
- **Server**: PASS (`pd-server` target, 0 errors, verified via `make -C build/client`)

### Decisions Made
- **`g_NetPendingResyncReqFlags` pattern** is now the canonical way for client recv handlers to send messages back to the server. Direct writes to `g_NetMsgRel` inside event handlers are silently dropped (netStartFrame resets after dispatch).
- **`g_NetLocalClient == NULL` guard is a systemic hazard**: dedicated server has NULL local client, so any `g_NetLocalClient &&` check is a silent bug on server. Use `g_NetNumClients > 0` or walk `g_NetClients[]` instead.

### Next Steps
- Playtest: verify desync recovery actually fires (requires a desync to trigger) — hard to test directly.
- Continue with J-1 end-to-end join verification or R-2 room lifecycle.

---

## Session 60 — 2026-03-27

**Focus**: Five playtest fixes: Leave Room, Start Match (netSend bug), bot modal UX, score slider, lobby player count

### What Was Done

**Fix 1 — Leave Room returns to social lobby** (`port/fast3d/pdgui_menu_room.cpp`):
- Leave Room button was calling `netDisconnect()` instead of `pdguiSetInRoom(0)`.
- Added forward declaration for `pdguiSetInRoom()` in the extern "C" block.
- Now correctly returns to social lobby while staying connected.

**Fix 2 — Start Match actually sends CLC_LOBBY_START** (`port/fast3d/pdgui_bridge.c`, `port/src/net/netmenu.c`):
- Root cause: `netLobbyRequestStartWithSims` wrote to `g_NetLocalClient->out` but never called `netSend()`. On the client, only `g_NetMsgRel`/`g_NetMsg` are auto-flushed by `netFlushSendBuffers()`; per-client `->out` buffers are only sent when `netSend(cl, NULL, ...)` is called explicitly. The packet sat unsent every time.
- Fix: added `netbufStartWrite(&g_NetLocalClient->out)` before the write, then `netSend(g_NetLocalClient, NULL, true, NETCHAN_CONTROL)` after.
- Same bug in legacy `menuhandlerCoopConfigStart` (`netmenu.c`) — patched there too (propagation check).

**Fix 3 — Bot modal left-aligned labels** (`port/fast3d/pdgui_menu_room.cpp`):
- Name / Difficulty / Character labels now sit on the LEFT with `ImGui::Text(...)` + `ImGui::SameLine(110.0f * scale)` before the control.
- Controls use `SetNextItemWidth(-1)` to fill the remaining width. No clipping.

**Fix 4 — Score limit slider 1-based** (`port/fast3d/pdgui_menu_room.cpp`):
- Slider now shows 1–100; `g_MatchConfig.scorelimit` still stored 0-based (val-1) for wire compatibility.
- Label shows `sl` directly (no +1) — "9 kills" slider = label "9 kills", not "10 kills".
- "No limit" triggered at scorelimit ≥ 99 (displayed as 100 on slider).

**Fix 5 — Social lobby player count** (`port/fast3d/pdgui_menu_lobby.cpp`):
- Added "Players: X / Y" next to "Connected to dedicated server" in the header.
- X = `lobbyGetPlayerCount()`, Y = `netGetMaxClients()`. Yellow text for visibility.

### Build Result
- **Client**: PASS (PerfectDark.exe, 0 errors)
- **Server**: PASS (PerfectDarkServer.exe, 0 errors)

### Decisions Made
- The `g_NetLocalClient->out` pattern for sending client→server is now documented: **must call `netSend(g_NetLocalClient, NULL, reliable, chan)` explicitly** — the auto-flush path only covers `g_NetMsgRel`/`g_NetMsg`.

### Next Steps
- **Playtest**: Leave Room → verify stays in social lobby. Start Match → verify CLC_LOBBY_START reaches server + match loads. Bot modal → verify labels readable.

---

## Session 59 — 2026-03-27

**Focus**: Match Start bug root cause (SVC_STAGE_START) + UX status audit

### What Was Done

**Root cause identified and fixed** (`port/src/net/netmsg.c`):
- `netmsgSvcStageStartWrite()` was reading `g_StageNum` directly, but the server
  idles at `STAGE_CITRAINING` and `mainChangeToStage()` is deferred (g_StageNum
  doesn't update until next frame). Result: SVC_STAGE_START always encoded
  `STAGE_CITRAINING`, clients interpreted it as "server returned to lobby" and
  dropped it — matches never started.
- Fix: read `g_MainChangeToStageNum` when >= 0 (pending stage set by `mainChangeToStage()`),
  fall back to `g_StageNum` mid-game (resets to -1 once stage loads).
- Note: `netServerStageStart()` already had its outer STAGE_CITRAINING guard removed
  in a prior session — the bug was one level deeper inside the write function.

**UX audit (main project already complete)**:
- UX Ref 1 (bot list with X buttons, Add Bot, double-click modal): FULLY IMPLEMENTED in main project (`pdgui_menu_room.cpp` lines 517–608, modal at lines 1160–1239).
- UX Ref 2 (spawn weapon dropdown): FULLY IMPLEMENTED in main project (lines 741–758, `s_SpawnWeaponIdx`).
- No additional code changes needed for these features.

### Build Result
- Headless build still cannot run from Claude's Bash tool (MSYS2 TEMP dir permission issue, known since S58). Must run from Mike's MSYS2 terminal.

### Decisions Made
- The async `g_MainChangeToStageNum` pattern is now used in two places: `netServerStageStart()` (comment explaining the issue) and `netmsgSvcStageStartWrite()` (the actual fix).

### Next Steps
- **Build + playtest** from Mike's MSYS2 terminal.
- **J-1**: End-to-end test — connect client, enter code, verify CLSTATE_LOBBY, click Start Match in Combat Sim tab, verify match loads.

---

## Session 58 — 2026-03-27

**Focus**: Headless build pipeline architecture + merge S57 worktree changes

### What Was Done

**`devtools/build-headless.ps1`** (updated):
- Added `-Version "X.Y.Z"` parameter — parsed into major/minor/patch and injected as `-DVERSION_SEM_*` cmake flags, mirroring `Get-BuildSteps` in `dev-window.ps1` exactly.
- Added worktree safety guard: if `$ProjectDir` contains `.claude\worktrees\`, strips the path to redirect to the real project root. Prevents AI sessions from building worktree source.
- Added version auto-read from `CMakeLists.txt` when `-Version` is omitted (same as GUI's `Get-ProjectVersion`).
- Version shown in build header output.

**`context/CRITICAL-PROCEDURES.md`** (updated):
- Added §3 Build Tool Architecture: documents canonical cmake flags, the sync rule between `build-headless.ps1` and `dev-window.ps1 Get-BuildSteps`, and the worktree prohibition.

**`port/fast3d/pdgui_lobby.cpp`** (merged from lucid-lamport worktree):
- Added forward decl for `pdguiRoomScreenRender()`.
- Changed NETMODE_CLIENT lobby path: now calls `pdguiRoomScreenRender(winW, winH)` instead of `pdguiLobbyScreenRender()`. Game clients see the tab-based room interior; dedicated server keeps `pdguiLobbyScreenRender`.

### Build Result
- **Client**: PASS (PerfectDark.exe, 0 errors)
- **Server**: PASS (PerfectDarkServer.exe, 0 errors)
- `pdgui_menu_room.cpp` (1108 lines, first build test) compiles clean.

### Decisions Made
- `build-headless.ps1` duplicates `Get-BuildSteps` logic (no dot-source — dev-window.ps1 loads WinForms at module scope). SYNC RULE: if cmake flags change in one, change both.
- Subprocess threading in `Invoke-BuildStep` hangs when called through Claude's Bash tool (PowerShell process spawning limitation). Works from Mike's terminal normally (verified session 56). No change to the threading approach.

### Next Steps
- **Playtest**: connect client to server, verify room interior tab screen appears (pdguiRoomScreenRender), all 3 tabs visible.
- **J-1**: End-to-end join test — build server target, start server, enter code in client, verify CLSTATE_LOBBY + match start.
- **R-2**: Room lifecycle — expand hub slot pool, on-demand room creation.

---

## Session 57 — 2026-03-27

**Focus**: Room interior UX — tab-based match setup screen replacing flat lobby for game clients

### What Was Done

**`context/lobby-flow-plan.md`** (created):
- New plan file documenting the full lobby flow: connection phase → lobby overview → room interior.
- §3 Room Interior tab-based UX spec (Combat Simulator / Campaign / Counter-Operative).
- §4 Mode-specific settings: full MPOPTION_* catalog, bot settings, campaign missions §4.2a, counter-op assignment.
- §5 Network protocol: current CLC_LOBBY_START payload vs. future CLC_ROOM_SETTINGS (R-4).
- §6 Implementation status table.

**`port/fast3d/pdgui_menu_room.cpp`** (created, ~570 lines):
- Entry point: `extern "C" void pdguiRoomScreenRender(s32 winW, s32 winH)`
- Tab bar: Combat Simulator | Campaign | Counter-Operative. Non-leaders see all tabs read-only.
- Left panel (55%): mode-specific settings. Right panel (40%): room player list.
- **Combat Simulator**: Scenario combo, arena picker, time/score sliders, weapon set, 10 MPOPTION toggles (including inverted flags), scenario-specific sub-options (HTB/CTC/KOH/HTM/PAC), bot count slider 0–31, per-bot TreeNode rows (name, difficulty MeatSim–DarkSim, body combo).
- **Campaign**: Mission picker (17 missions, §4.2a), difficulty (Agent/SA/PA), friendly fire toggle.
- **Counter-Op**: Mission picker, counter-op player assignment dropdown (from lobbyGetPlayerInfo), difficulty.
- Player panel: leader=gold, local=green, others=white; body name muted; state indicator.
- Bottom bar: **Start Match** (leader only) → `netLobbyRequestStartWithSims` (MP) / `netLobbyRequestStart` (COOP/ANTI); **Leave Room** → `netDisconnect()`.
- Connect code display for server instances.

**`port/fast3d/pdgui_lobby.cpp`** (modified):
- Added forward decl for `pdguiRoomScreenRender`.
- In `pdguiLobbyRender()`: NETMODE_CLIENT + `netLocalClientInLobby()` path now calls `pdguiRoomScreenRender` instead of `pdguiLobbyScreenRender`. Dedicated server path unchanged.

### Decisions Made
- Game clients see room interior (`pdguiRoomScreenRender`). Dedicated server keeps `pdguiLobbyScreenRender`.
- Full settings sync (time limit, score, options bitmask, per-bot config) deferred to CLC_ROOM_SETTINGS (R-4). Today only stagenum/numSims/simType are sent via CLC_LOBBY_START.
- Campaign mission stage constants 0x35–0x40 are tentative — need verification from `src/include/constants.h` when wiring mission picker fully.

### Next Steps
- **Build test**: verify `pdgui_menu_room.cpp` compiles clean; check for ODR issues with `struct matchconfig` redeclaration vs. `pdgui_menu_matchsetup.cpp`.
- **Playtest**: connect → room interior shows → leader can change settings → Start Match launches.
- **R-4**: CLC_ROOM_SETTINGS full sync (time limit, score limit, options bitmask, weapon set, per-bot config).

---

## Session 56 — 2026-03-27

**Focus**: Audit and fix 5 lobby issues found in Mike's playtest of the S55 build

### What Was Done

**Issue 1 & 5 — Client lobby empty + no leader** (`port/src/net/netlobby.c`):
- Root cause: `lobbyUpdate()` skip guard `if (cl == g_NetLocalClient)` ran on clients too. After SVC_AUTH, `g_NetLocalClient = &g_NetClients[id]`, so the client's own slot was being skipped → 0 players found → "Waiting for players..." and `lobbyIsLocalLeader()` always returned 0.
- Fix: Changed guard to `if (g_NetMode != NETMODE_CLIENT && cl == g_NetLocalClient)` — clients always include their own slot. With the slot visible, eager leader election fires immediately and `lobbyIsLocalLeader()` returns 1 for the first connected client. (B-32)

**Issue 2 — Player name shows "Player 1" not agent name** (`port/src/net/netmsg.c`):
- Root cause: `netmsgClcSettingsWrite()` sent `g_NetLocalClient->settings.name` directly without the identity override check that `netmsgClcAuthWrite()` had. CLC_SETTINGS is processed after CLC_AUTH on the server and overwrites the correct identity name with the stale/legacy `settings.name` from `netClientReadConfig()`.
- Fix: Added identity profile check to `netmsgClcSettingsWrite()` — same pattern as `netmsgClcAuthWrite()`. Also syncs `settings.name` so future sends are consistent. (B-33)

**Issue 3 — Server SDL window title shows raw IP** (`port/src/server_main.c`, `port/src/video.c`):
- Two locations, two fixes:
  - `server_main.c`: Added `#include "connectcode.h"`. Parse `sscanf` into u32, call `connectCodeEncode()`. Show connect code if available, else fall back to port number. (B-35)
  - `video.c`: Same connect code logic (inline `extern` declaration since video.c doesn't include `connectcode.h`). (B-29 secondary, same session)

**Issue 4 — Server GUI shows "0/32 connected"** (`port/src/server_main.c`):
- Root cause: `displayClients = g_NetNumClients > 0 ? g_NetNumClients - 1 : 0` — pre-B-28 compensation that subtracts 1 for the server's own slot. After B-28, dedicated server has `g_NetLocalClient = NULL` and `g_NetNumClients` counts only real players. `1 - 1 = 0` with one player connected. `server_gui.cpp` was already corrected; `server_main.c` was not.
- Fix: `g_NetDedicated ? g_NetNumClients : (g_NetNumClients > 0 ? g_NetNumClients - 1 : 0)`. (B-34)

**SVC_LOBBY_LEADER broadcast after auth** (`port/src/net/netmsg.c`):
- Added: after successful `netmsgClcAuthRead`, call `lobbyUpdate()` then broadcast `SVC_LOBBY_LEADER` to all CLSTATE_LOBBY+ clients. Ensures new joiners and existing players all get the authoritative leader assignment from the server, not just client-side inference.

**Build verified**: All 4 modified files compile clean in both client and server headless builds.

### Decisions Made
- SVC_LOBBY_LEADER broadcast is the canonical source of truth for leader identity; client-side inference in `lobbyUpdate()` serves as a fallback for single-player/offline mode.
- `video.c` uses inline `extern` for `connectCodeEncode` rather than adding a new `#include` — avoids pulling in unnecessary headers in a large file.

### Bugs Fixed
- B-32: `lobbyUpdate()` client skip guard
- B-33: CLC_SETTINGS name override
- B-34: `server_main.c` player count off-by-one (B-28 missed location)
- B-35: `server_main.c` raw IP in SDL window title (B-29 missed location)

### Next Steps
- Playtest: connect one client, verify lobby shows player name (not "Player 1"), client sees themselves as leader, server title shows connect code and correct count.
- Playtest: two clients, verify leader broadcast reaches both and the non-first-joined client is not shown as leader.

---

## Session 55 — 2026-03-27

**Focus**: Harden lobby leader assignment + room display fixes (follow-up to S54)

### What Was Done

**Eager leader assignment in lobbyUpdate()** (`port/src/net/netlobby.c`):
- Added in-loop eager assignment: when `g_Lobby.leaderSlot == 0xFF` and the first CLSTATE_LOBBY+ client is seen, assign them immediately rather than waiting for the post-loop election block.
- Closes a same-frame race: if CLC_AUTH and CLC_LOBBY_START arrive in the same ENet batch, CLC_LOBBY_START is processed before the post-loop election has any chance to run.

**lobbyUpdate() refresh before leader validation** (`port/src/net/netmsg.c`):
- `netmsgClcLobbyStartRead()` now calls `lobbyUpdate()` before checking `leaderSlot`.
- Added fallback: if `leaderSlot == 0xFF` (still unset after refresh), scan `g_NetClients` for first CLSTATE_LOBBY+ client and accept if it matches `srccl`.
- Better rejection log: includes `leaderSlot` value for debugging.

**Empty room display fix** (`port/fast3d/pdgui_menu_lobby.cpp`):
- Room sidebar now skips rooms with `client_count == 0` — prevents permanent "Lounge" (Room 0) from appearing before any players join.
- Added `roomsShown` counter; "No active rooms" shows when `roomsShown == 0`.

### Decisions Made
- These are belt-and-suspenders hardening on top of S54's fixes; the root causes were already addressed but edge cases remained.
- Not removing Room 0 creation yet — hub.c still depends on roomGetById(0) for state sync in hubTick().

### Next Steps
- Build and playtest the 2-player Combat Sim flow end-to-end.
- If match starts but players don't spawn, check g_SpawnPoints (B-19 partial fix in S54).

---

## Session 54 — 2026-03-27

**Focus**: Full implementation to get two players into a working Combat Simulator match

### What Was Done

**lobbyUpdate() B-28 regression fixed** (`port/src/net/netlobby.c`):
- `i == 0` skip guard replaced with `cl == g_NetLocalClient` — which is `NULL` on dedicated servers, so no slot is ever skipped. First real player (slot 0) now appears in `g_Lobby.players[]`.
- Off-by-one: `i <= NET_MAX_CLIENTS` → `i < NET_MAX_CLIENTS`.
- Root cause: B-28 (S52) set `g_NetLocalClient = NULL` for dedicated servers but didn't update lobbyUpdate's hardcoded slot-0 skip. Leader validation in `netmsgClcLobbyStartRead()` always failed because the leader (slot 0) was invisible.

**Duplicate Room 0 display fixed** (`port/src/room.c`):
- Added `if (!s_Initialised) return 0/NULL;` guards to `roomGetActiveCount()` and `roomGetByIndex()`.
- Root cause: `s_Rooms[]` is a C static array zero-initialized to `ROOM_STATE_LOBBY=0`, so all 4 slots appeared "active" on the client (which never calls `hubInit()`).

**g_MpSetup configured for Combat Sim** (`port/src/net/netmsg.c`):
- `netmsgClcLobbyStartRead()` now sets `g_MpSetup.stagenum`, `scenario=0` (MPSCENARIO_COMBAT), `timelimit=0`, `chrslots` with bits 0..n-1 for n connected players.
- Assigns sequential `playernum` values to each connected client before calling `netServerStageStart()`.
- Without this, `SVC_STAGE_START` broadcast `chrslots=0` and `playernum=0` for everyone, so `mpStartMatch()` never spawned players.

**Off-by-one in netServerStageStart()** (`port/src/net/net.c`):
- Two loops `i <= NET_MAX_CLIENTS` → `i < NET_MAX_CLIENTS`.

**Simulant settings in lobby UI** (`port/fast3d/pdgui_menu_lobby.cpp`, `port/fast3d/pdgui_bridge.c`, `port/include/net/netmsg.h`, `port/src/net/netmsg.c`):
- CLC_LOBBY_START payload extended: `gamemode, stagenum, difficulty, numSims, simType` (added 2 bytes).
- Server reads numSims/simType, populates `g_BotConfigsArray[]` and bits 8+ of `g_MpSetup.chrslots`.
- Lobby UI: arena selector (20 maps), simulant count slider (0-8), difficulty dropdown (Meat/Easy/Normal/Hard/Perfect/Dark).
- `netLobbyRequestStartWithSims()` bridge function added; original `netLobbyRequestStart()` wraps it with zeros.

**Spawn point fallback for MP maps without INTROCMD_SPAWN** (`src/game/playerreset.c`):
- After intro cmd loop, if `g_NumSpawnPoints == 0` and `g_NetMode != NETMODE_NONE`, scans `g_PadsFile` for pads with valid room numbers and populates `g_SpawnPoints`.
- Fixes B-19 (bot spawn stacking) for mod stages and MP maps without proper setup sequences.
- Added `#include "net/net.h"` to playerreset.c.

### Decisions Made
- CLC_LOBBY_START protocol extension is backward-compatible within a single session (client/server always built together). No protocol version bump needed yet.
- Spawn fallback uses `g_NetMode != NETMODE_NONE` guard so solo missions are unaffected.
- Arena selector uses hardcoded stage numbers (0x1f..0x32) matching known PD MP maps. No stage table dependency.

### Bugs Fixed
- **B-28 regression in lobbyUpdate()** (unlabeled): `i == 0` skip broke leader detection after B-28.
- **B-19** (partial): spawn stacking reduced by pre-populating g_SpawnPoints from pad data.

### Next Steps
- Build and run end-to-end 2-player test: start dedicated server, both clients connect, leader hits Combat Simulator button.
- Verify `mpStartMatch()` fires on both clients with correct playernum and chrslots.
- R-2: Room lifecycle expansion if first match test passes.

---

## Session 53 — 2026-03-26

**Focus**: Two bugs blocking clients from reaching lobby after connecting to dedicated server

### What Was Done

**B-31 FIXED — SVC_AUTH malformed on client** (`port/src/net/netmsg.c`):
- Root cause: `netmsgSvcAuthRead` had guard `|| id == 0` that was correct pre-B-28 (slot 0 = server's own local client, never assigned to remote clients). After B-28 (S52), dedicated servers start slot search at `i=0`, so the first real client legitimately gets `g_NetClients[0]`, making `authcl - g_NetClients = 0`. The old guard rejected this valid ID.
- Fix: removed `|| id == 0` from the malformed-message check in `netmsgSvcAuthRead`. Applied to both main repo and worktree.
- Secondary fix: `netmsgClcAuthRead` (server side) previously called `netDistribServerSendCatalogInfo` BEFORE sending `SVC_AUTH`. Client was in `CLSTATE_AUTH` when catalog info arrived, which is incorrect ordering. Reordered so `SVC_AUTH` is sent first (client transitions to `CLSTATE_LOBBY`), then catalog info follows. Applied to both repos.

**B-26 fully fixed — Player name sends identity profile name** (`port/src/net/netmsg.c`, `port/src/net/net.c`):
- S49 fix was incomplete: identity was only used as empty-name fallback. "Player 1" (the N64 default) is non-empty, so identity was never consulted.
- Fix 1: `netClientReadConfig()` in `net.c` — identity profile is now the PRIMARY source; legacy N64 config is fallback only. (Main repo already had this; applied to worktree.)
- Fix 2: `netmsgClcAuthWrite()` in `netmsg.c` — directly reads `identityGetActiveProfile()->name` when available, so the wire packet uses the profile name regardless of what's in `settings.name`. Applied to both repos.
- Added `#include "identity.h"` to `netmsg.c`.

### Decisions Made
- `id == 0` guard removal is correct and safe: `NET_NULL_CLIENT = 0xFF` remains the "no client" sentinel. Slot 0 being valid is the correct post-B-28 state.
- Identity profile name is authoritative on PC; legacy N64 config name is a fallback only.

### Next Steps
- Build and run end-to-end join test to confirm client reaches lobby (B-31 and B-26 are testable together)
- R-2: Room lifecycle after lobby is confirmed working

---

## Session 52 — 2026-03-26

**Focus**: Phase R-1 implementation — hub slot pool API, dedicated server slot fix, IP scrubbing

### What Was Done

**R-1a: Hub slot pool API implemented** (`port/src/hub.c`):
- Added `#include "net/net.h"` so hub.c can read `g_NetMaxClients` / `g_NetNumClients`
- Implemented all 4 stubs declared in `hub.h` but missing in `hub.c`:
  - `hubGetMaxSlots()` → returns `g_NetMaxClients`
  - `hubSetMaxSlots(s32)` → clamps to [1, NET_MAX_CLIENTS], writes `g_NetMaxClients`
  - `hubGetUsedSlots()` → returns `g_NetNumClients`
  - `hubGetFreeSlots()` → returns `max - used`, clamped to 0

**R-1b: Dedicated server no longer occupies slot 0** (`port/src/net/net.c`, B-28 FIXED):
- `netStartServer()`: when `g_NetDedicated`, sets `g_NetLocalClient = NULL` and `g_NetNumClients = 0` (slot 0 stays free). When not dedicated, existing listen-server path unchanged.
- `netServerEvConnect()` slot search: changed from always starting at `i=1` to `i = g_NetDedicated ? 0 : 1`, so slot 0 is assignable to real players on dedicated servers.
- NULL guards added to `netServerStageStart()` for lines that unconditionally wrote `g_NetLocalClient->state` and called `netClientReadConfig(g_NetLocalClient, 0)` (two sites).
- NULL guard added to `netServerStageEnd()` for `g_NetLocalClient->state = CLSTATE_LOBBY`.
- Verified `netServerEvConnect()` line 942 already had NULL guard: `const bool ingame = (g_NetLocalClient && ...)`.

**R-1c: Raw IP removed from server GUI status bar** (`port/fast3d/server_gui.cpp`, B-29 FIXED):
- Line 695: Replaced `ImGui::TextColored(..., "%s:%u", ip, g_NetServerPort)` with `"Port %u"` (port only, no IP).
- Line 707: Fixed `displayClients` — dedicated servers now show `g_NetNumClients` directly (no `-1` compensation needed since slot 0 is no longer occupied by the server).

**R-1d: IP-bearing log lines replaced** (`port/src/net/net.c`, B-30 FIXED):
- `netServerEvConnect()`: Removed `addrstr = netFormatPeerAddr(peer)`. Connection event logs show "incoming connection", rejection logs show reason only (no IP).
- `netServerEvDisconnect()`: `"disconnect event from %s"` → `"disconnect event from client %u"` using `cl->id`.
- Spurious-peer logs (no attached client): replaced `netFormatPeerAddr(ev.peer)` with generic "unknown peer" messages.
- UPnP IP logs in `netupnp.c` left intact (internal infrastructure — not user-facing).

### Decisions Made
- `g_NetNumClients = 0` set when dedicated (not left at 1 from `netClientResetAll()`), so player count is accurate from the start.
- `hubSetMaxSlots` clamps to `[1, NET_MAX_CLIENTS]` — no silent negative or overflow.
- UPnP log lines (`UPNP: [thread] External IP: ...`) are classified as internal infrastructure and left as-is per R-1 design.

### Next Steps
- Build dedicated server target and run end-to-end join test (J-1) to verify R-1 changes
- Confirm slot 0 is now available (expect `Players: 1/32` with one client vs old `1/32` that was really `0/31`)
- R-2: Room lifecycle (expand HUB_MAX_ROOMS/CLIENTS, add `leader_client_id`, demand-driven rooms)

---

## Session 51 — 2026-03-26

**Focus**: Room architecture plan — code audit, struct corrections, message IDs, phase file refs

### What Was Done

**Code audit against draft `context/room-architecture-plan.md`** (created S50):

**Key findings from reading hub.h/hub.c, room.h/room.c, net.h, net.c, netmsg.h, server_gui.cpp, netlobby.c**:

1. **hub.h slot pool API not implemented**: `hubGetMaxSlots/SetMaxSlots/GetUsedSlots/GetFreeSlots` are declared but have no implementation in `hub.c`. Phase R-1 must add these.

2. **g_NetLocalClient = &g_NetClients[0] on server CONFIRMED**: `netStartServer()` lines 519-521 unconditionally claims slot 0 for the server. `lobbyUpdate()` already has a dedicated-server guard skipping slot 0, and `server_gui.cpp` compensates with `g_NetNumClients - 1`. Fix in R-1: set `g_NetLocalClient = NULL` when `g_NetDedicated`. (B-28)

3. **IP in server GUI status bar CONFIRMED**: `server_gui.cpp:695` shows raw `"%s:%u"` IP/port in gray below the connect code. Connect code display already exists (lines 689-693). Remove gray IP line. (B-29)

4. **IPs in log output CONFIRMED**: `netFormatClientAddr()` returns raw `"IP:port"` strings used in connection log calls. (B-30)

5. **hub_room_t struct**: `id` is `u8` (not `s32`). No `leader_client_id` field — needs to be added. `creator_client_id` exists. Types verified. Draft plan struct was close but types wrong.

6. **HUB_MAX_CLIENTS = 8** in room.h stale — `NET_MAX_CLIENTS` is 32. Must expand.

7. **HUB_MAX_ROOMS = 4** — needs expansion (plan: 16).

8. **Message ID ranges confirmed**: SVC free from `0x75`, CLC free from `0x0A`. Plan assigns SVC 0x75-0x77, CLC 0x0A-0x0F.

9. **roomsInit() creates room 0 permanently** — conflicts with demand-driven design. Phase R-2 removes this.

10. **Draft B-28 (player name)** = already B-26 (fixed). Removed from plan. Bugs renumbered: B-28 = slot 0, B-29 = server GUI IP, B-30 = log IPs.

**Plan revised**: `context/room-architecture-plan.md` rewritten with all corrections, specific code locations, and phase-level file references.

**Context files updated**:
- `context/room-architecture-plan.md`: full code-verified rewrite
- `context/tasks-current.md`: R-1 through R-5 added to Active Work Tracks + Prioritized Next Up
- `context/bugs.md`: B-28, B-29, B-30 added (all OPEN, part of R-1)
- `context/session-log.md`: this entry
- `context/infrastructure.md`: SPF section updated with R-series
- `context/README.md`: last-updated bumped

### Decisions Made
- `room_id` on `struct netclient` uses `s32` with `-1` as sentinel (not 0 — room IDs are 0-based)
- `CLC_LOBBY_START (0x08)` remains for backward compat; `CLC_ROOM_START (0x0F)` is the new primary; deprecated in R-4 but not removed until tested
- B-28/29/30 grouped into Phase R-1 (no protocol change required for any of them)

### Next Steps
- R-1: Start with hub slot pool stubs + `g_NetLocalClient = NULL` fix in `net.c`
- Continue J-1: Build server target, verify end-to-end join flow

---

## Session 50 — 2026-03-26/27

**Focus**: Server crash fix (B-27, 9 fixes), multiplayer regressions, build system hardening, v0.0.7 release

### What Was Done

**B-27 FIXED — Dedicated server crash on first client connect (9 fixes, 6 files)**:

Nine separate bugs all in the server connect path, discovered via real cross-machine playtest:

1. **`g_RomName` type mismatch** (`port/src/server_stubs.c`): stub declared `char g_RomName[64]` but `port/include/versioninfo.h` declared `const char *g_RomName`. Fix: changed stub to `const char *g_RomName = "pd-server"`.

2. **ROM/mod check not gated on dedicated** (`port/src/net/net.c`): The ROM hash + mod check ran unconditionally in `CLC_AUTH`, rejecting all real clients connecting to a dedicated server (which has no valid ROM). Fix: wrapped behind `!g_NetDedicated` guard.

3. **`SVC_AUTH` rejecting `id == 0`** (`port/src/net/net.c`): Handler had `if (id == 0) reject`. Now that dedicated servers assign slot 0 to real players (not reserved for server), this guard wrongly rejected the first player. Fix: removed the `id == 0` check.

4. **Hardcoded `g_NetClients[0].state` assumption** (`port/src/net/netmsg.c`): `netmsgClcAuthRead()` unconditionally read `g_NetLocalClient->state` instead of the connecting client's state. Fix: use `cl->state` directly.

5. **NULL guard missing on `g_NetLocalClient`** (`port/src/net/netmsg.c`): Dedicated server has `g_NetLocalClient = NULL`; dereference in `netmsgClcAuthRead` crashed. Fix: NULL guard added.

6. **`ev.packet` NULL check missing** (`port/src/net/net.c`): ENet receive callback could deliver an event with `ev.packet = NULL`; crash on `enet_packet_destroy`. Fix: NULL check added.

7. **`LOBBY_MAX_PLAYERS = 8` mismatch** (`port/src/net/netlobby.c`): Lobby capacity was still 8 while `NET_MAX_CLIENTS` was 32. Fix: `LOBBY_MAX_PLAYERS` updated to 32.

8. **Stale `#define NET_MAX_CLIENTS 8`** (`port/fast3d/server_gui.cpp`): Server GUI had its own local define shadowing the updated value in `net.h`. Fix: removed local define.

9. **GUI ping/kick used loop index instead of `clientId`** (`port/fast3d/server_gui.cpp`): Server action commands (ping/kick) passed the iteration index `i` instead of `cl->id`. Fix: use `cl->id`.

**B-22 FIXED — Version not baking into exe (third report)**:
- Root cause: `Get-BuildSteps` in `devtools/dev-window.ps1` built cmake configure args with no `-DVERSION_SEM_*` flags
- CMake used its cached value (from prior run) or the hardcoded CACHE default — Dev Window version boxes had no effect
- Fix: added `Get-UiVersion` call inside `Get-BuildSteps`; appends `-DVERSION_SEM_MAJOR=X -DVERSION_SEM_MINOR=Y -DVERSION_SEM_PATCH=Z` to both Configure steps (client + server)

**B-23 FIXED — Quit Game button clipped on right edge**:
- Root cause: fixed `quitBtnW = 100 * scale` placed button's right edge at the ImGui clip boundary with no margin; "Confirm Quit" label also wider than 100px
- Fix in `port/fast3d/pdgui_menu_mainmenu.cpp`: width now `CalcTextSize("Confirm Quit").x + FramePadding*2`; position now `dialogW - WindowPadding.x - quitBtnW - 4*scale`; Cancel button cursor updated

**F8 hotswap badge removed from main menu** (`port/fast3d/pdgui_menu_mainmenu.cpp`):
- Removed the F8 indicator badge from the main menu corner. The toggle (F8 / R3) still works — badge was visual noise.

**Always-clean build enforced** (`devtools/dev-window.ps1`):
- "Clean Build" toggle removed from Dev Window GUI. Every build now unconditionally deletes build directories before configure.
- Rationale: stale CMake CACHE caused B-22 and an entire class of version-baking/config-drift bugs. Clean builds eliminate this class.

**Auto-commit version from UI boxes** (`devtools/dev-window.ps1`):
- Auto-commit message (triggered before release builds) now reads version from the Dev Window boxes, not from CMakeLists.txt defaults.
- Ensures the auto-commit label always matches the actual binary being built.

**Update tab — cross-session staged version persistence** (`port/src/updater.c`, `port/include/updater.h`):
- Downloads now write a `.update.ver` sidecar file alongside the staged binary.
- `updaterGetStagedVersion()` reads this sidecar on startup.
- "Switch" button now appears immediately on reopen without requiring a re-download.

**Update tab button sizing** (`port/fast3d/pdgui_menu_update.cpp`):
- Download/Rollback/Switch buttons now use `CalcTextSize`-based widths instead of fixed pixel values.
- Per-row layout: each version row gets its own Download/Rollback/Switch button inline.

**v0.0.7 released to GitHub**:
- Built and tested as v0.0.6, released as v0.0.7.
- Includes all changes from S27–S50 (component mod architecture, room system, connect codes, participant system, update tab, all multiplayer regression fixes from S49d).

### Decisions Made
- Version boxes in the Dev Window are the single source of truth for ALL builds (not just releases). `Get-BuildSteps` is the authoritative cmake path.
- All builds are clean builds. No toggle. No exceptions. Stale CMake CACHE is eliminated by design.
- ROM/mod check is skipped on dedicated server via `!g_NetDedicated`. No hack guards.
- `id == 0` is a valid player slot on dedicated servers. The SVC_AUTH guard that rejected slot 0 is gone.

### Next Steps
- SPF-2b: verify SPF-1 server build end-to-end (J-1)
- SPF-3a: lobby ImGui screen
- Wire remaining menus through menu manager
- Collision Phase 2 design (HIGH PRIORITY)

---

## Session 49b — 2026-03-26

**Focus**: SPF-3 lobby+join, catalog audit, plan docs, stats, connect codes, IP fallback, updater

### What Was Done

**SPF-2a Build Pass**: menumgr.h was missing `extern "C"` guards → undefined reference errors in C++ TUs. Fix applied (`5e55e62`). SPF-2a (menumgr.c/h, 100ms cooldown) now builds.

**Release Pipeline**: `-Nightly` flag added to release.ps1: nightly builds use `nightly-YYYY-MM-DD` tag. Fixed post-batch-addin path (Split-Path parent traversal).

**SPF-3 — Lobby + Join by Code** (commit `3b588c1`): `pdgui_menu_lobby.cpp` integrated hub.h/room.h — lobby shows server state, room list with color-coded states and player counts. `pdgui_menu_mainmenu.cpp`: new menu view 4 "Join by Code" with phonetic code input + decode via `phoneticDecode()` (falls back to direct IP). Wired through menu manager (MENU_JOIN push/pop).

**Asset Catalog Audit Phase 1** (commit `3b588c1`): Failure logging at all critical asset load points: `fileLoadToNew`, `modeldefLoad`, `bodyLoad`, `tilesReset`, setup pad loading, lang bank loading.

**New Plan Documents** (commit `636b404`): `context/catalog-loading-plan.md` (C-1–C-9 phases). `context/menu-replacement-plan.md` (240 legacy menus → 9 ImGui groups, Group 1 highest priority).

**Player Stats System**: New `port/include/playerstats.h` + `port/src/playerstats.c`. `statIncrement(key, amount)` — named counter system, JSON persistence.

**Connect Code System Rewrite**: Sentence-based codes ("fat vampire running to the park") replace phonetic syllables as primary connect method. 256 words per slot × 4 slots = 32-bit IPv4.

**HTTP Public IP Fallback**: `netGetPublicIP()` tries UPnP first, then `curl` → `api.ipify.org`. Result cached after first success.

**Updater Unified Tag Format**: `versionParseTag()` now handles `"v0.1.1"` (unified) in addition to `"client-v0.1.1"` (legacy).

### Decisions Made
- Sentence-based connect codes are primary (phonetic module remains for lobby display)
- Menu replacement: Group 1 (Solo Mission Flow, 11 menus) first
- Stats: named counters (not fixed schema) for forward compatibility

### Next Steps
- SPF-3 playtest: lobby rooms, join-by-code
- Catalog Phase C-1/C-2; Menu Replacement Group 1

---

## Session 49c — 2026-03-26

**Focus**: Join flow audit, S49 architecture documentation, context hardening

### What Was Done

**Context audit — S49 architectural decisions captured**: Sentence-based connect codes, menu replacement plan, rooms + slot allocation, asset catalog as single source of truth (C-1–C-9 phases), campaign as co-op, player stats, HTTP IP fallback, updater unified tag format.

**Join flow audit — `context/join-flow-plan.md` created**: Full end-to-end flow mapped: code input → decode → netStartClient → ENet → CLC_AUTH → SVC_AUTH_OK → CLSTATE_LOBBY → lobby UI → netLobbyRequestStart → match. Gaps found: room state not synced to clients (SVC_ROOM_LIST needed), server GUI missing connect code display, recent server history stubbed.

**Plan: J-1 verify end-to-end, J-2 server GUI code display, J-3 SVC_ROOM_LIST protocol, J-4 server history UI, J-5 lobby handoff polish.**

Context files updated: networking.md (protocol v21, HTTP IP fallback), update-system.md (unified tag format), constraints.md (no raw IP in UI), infrastructure.md, tasks-current.md.

### Decisions Made
- Recent server history MUST encode IPs to codes, not store raw IP
- Server GUI should display connect code (currently only in logs)

### Next Steps
- J-1: Build server target, verify end-to-end join → match flow
- J-2: Add connect code display to server_gui.cpp

---

## Session 49d — 2026-03-26

**Focus**: Cross-machine multiplayer bug fixes (3 regressions from real playtest)

### What Was Done

**B-24 (was B-22) — Connect code byte-order reversal (CRITICAL, FIXED)**: `pdgui_menu_mainmenu.cpp` extracted bytes MSB-first `(ip>>24, ip>>16, ip>>8, ip)` while encoder + all other decode callers use LSB-first `(ip, ip>>8, ip>>16, ip>>24)`. Fix: 3-line change to LSB-first extraction.

**B-25 (was B-23) — Server max clients hardcoded to 8 (FIXED)**: `NET_MAX_CLIENTS` was `MAX_PLAYERS` (=8). Fixed: `NET_MAX_CLIENTS 32` in `net.h`, independent of `MAX_PLAYERS`. `PDGUI_NET_MAX_CLIENTS 32` in debug menu.

**B-26 (was B-24) — Player name shows "Player1" (FIXED)**: `netClientReadConfig()` reads from legacy N64 save field; empty on fresh PC client. Fix: identity profile fallback in `netClientReadConfig()` — copies from `identityGetActiveProfile()->name` when legacy name is empty.

### Decisions Made
- `NET_MAX_CLIENTS` = 32, decoupled from `MAX_PLAYERS` = 8. Server accepts 32 connections; match caps at 8 active slots.
- Identity profile is the authoritative source of local player display name. Legacy g_PlayerConfigsArray is fallback only.

---

## Session 49e — 2026-03-26

**Focus**: Version system full audit + fix

### What Was Done

**Root cause found**: CMake's `CACHE` variable behavior — when `CMakeCache.txt` exists, `set(VERSION_SEM_PATCH N CACHE STRING ...)` is silently ignored. `Set-ProjectVersion` edited CMakeLists.txt correctly but cmake configure didn't override the stale cache.

**Fixes**: `Get-BuildSteps` accepts `$ver` param, appends `-DVERSION_SEM_MAJOR/MINOR/PATCH` flags to BOTH configure steps. `Start-PushRelease` passes `$ver` to `Get-BuildSteps`. `port/src/video.c:91`: replaced hardcoded `"Perfect Dark 2.0 - Client (v0.0.2)"` with `"Perfect Dark 2.0 - v" VERSION_STRING`.

**`context/build.md`**: Added full Version System section documenting the CACHE pitfall and fix.

### Decisions Made
- (Note: later superseded by S49i — ALL builds now use version flags, not just releases)

---

## Session 49f — 2026-03-26

**Focus**: Updater UI — banner fix, per-row actions, server update mechanism

### What Was Done

**Client update banner (`pdgui_menu_update.cpp`)**: Replaced `SmallButton` with `Button` sized via `pdguiScale`; right-aligned via `SameLine(GetContentRegionMax().x - totalW)`. Added `s_DownloadingIndex` + `s_StagedReleaseIndex` state for per-release tracking.

**Settings > Updates tab**: 5-column table (added Action column). Per-row buttons: Download, Switch (staged), % (in-progress). Error message moved below table. Table shown during active download.

**Server update mechanism**: `server_main.c` added `updaterTick()` per frame, logs update availability. `server_gui.cpp`: "Updates (*)" tab with per-row Download/Switch buttons, progress display, Restart & Update button.

### Decisions Made
- `SameLine(GetContentRegionMax().x - totalW)` is the canonical ImGui right-align pattern
- Server headless update path: log URL + manual restart

---

## Session 49g — 2026-03-26

**Focus**: F8 hotswap hint removal

### What Was Done
- Removed deprecated F8 footer hint ("F8: toggle OLD/NEW") from `pdgui_menu_mainmenu.cpp` (footer block at bottom of `renderMainMenu`).

---

## Session 49h — 2026-03-26

**Focus**: Update tab button sizing audit

### What Was Done

**`pdgui_menu_update.cpp` button sizing overhaul**:
- `renderNotificationBanner`: `CalcTextSize()`-based widths for "Update Now", "Details", "Dismiss". Explicit `btnH = GetFontSize() + FramePadding.y * 2` — descender-safe.
- `renderVersionPickerContent`: `CalcTextSize("Check Now")` for "Check Now" button. Action column width from `CalcTextSize("Download")`.
- `TableSetupScrollFreeze(0, 1)` — header stays visible on scroll. Column widths use `pdguiScale()`. `ImGuiSelectableFlags_AllowOverlap` so per-row buttons receive input.
- Removed below-table "Download & Install" button (was off-screen, invisible).
- Download = green, Rollback = amber styling.

### Decisions Made
- Action buttons live in table rows (always visible), not below table (was off-screen)
- `AllowOverlap` is the correct pattern for interactive items in `SpanAllColumns` rows

---

## Session 49i — 2026-03-26

**Focus**: Build pipeline overhaul — always-clean, version baking on every build

### What Was Done

**`devtools/dev-window.ps1` overhaul**:
- **Always-clean builds**: `Start-Build` unconditionally deletes `build/client` + `build/server` before every build. No stale CMakeCache possible.
- **Version from UI on every build**: `Start-Build` reads `Get-UiVersion` → `$script:BuildVersion`, passes to `Get-BuildSteps $script:BuildVersion`. Version boxes are single source of truth.
- **Get-BuildSteps**: Accepts `$ver` parameter, injects `-DVERSION_SEM_MAJOR/MINOR/PATCH` flags into BOTH configure steps.
- **CMakeLists.txt updated after build**: On successful completion, `Set-ProjectVersion` called from `$script:BuildVersion` — file always reflects what was actually built.
- **`Start-PushRelease` updated**: Also cleans before queuing, sets `$script:BuildVersion = $ver`, passes to `Get-BuildSteps`.
- **Removed**: `$script:CleanBuildActive`, `$script:BtnCleanBuild` toggle, associated handler. BUILD button now full hero height.

### Decisions Made
- All builds are clean builds. "Incremental" option removed entirely.
- Version boxes initialize from CMakeLists.txt at startup (reflects last built state).
- CMakeLists.txt updated END of build; -D flags are authoritative during build, file updated after.
- For releases: CMakeLists.txt still updated BEFORE build (pre-release auto-commit).

### Next Steps
- Test full build to verify version bakes correctly
- Verify Release flow (clean → configure with -D → build → release.ps1)

---

## Session 50 — 2026-03-26

**Focus**: Update tab — cross-session staged version persistence

### What Was Done

**Staged version sidecar** (`updater.c`, `updater.h`):
- Added `versionPath` field to state (`exePath.update.ver`)
- `detectExePath()` now computes `versionPath` for both Win32 and Unix paths
- `writeStagedVersionFile()` / `readStagedVersionFile()` helpers — tiny text file, one version string
- `downloadThread()`: on DOWNLOAD_DONE, writes version sidecar outside mutex, then sets `stagedVersion` + `stagedVersionValid` in state
- `updaterInit()`: if `.update` file exists on disk, reads sidecar to restore staged version
- `updaterApplyPending()`: removes `.update.ver` after successful rename (both Win32 + Unix paths)
- New public API: `updaterGetStagedVersion()` — returns `&stagedVersion` if valid, NULL otherwise

**UI fix** (`pdgui_menu_update.cpp`):
- `isStaged` check now queries `updaterGetStagedVersion()` in addition to `s_StagedReleaseIndex`
- Cross-session staged version: if `.update.ver` matches this row's version, shows amber "Switch to this version" button immediately on launch
- Syncs `s_StagedReleaseIndex` from disk-persisted version so same-session Switch/restart flow works

**context/update-system.md**: Updated Self-Replacement section with sidecar file details and cross-session staged version note.

### Why This Matters
Before: if you downloaded a version then closed the game without restarting, reopening the Update tab showed no "Switch" button — you had to re-download. After: the sidecar file persists the staged version across sessions; the Switch button appears immediately.

### Decisions Made
- Sidecar is cleaned up by `updaterApplyPending()` so it's never stale post-apply
- `updaterGetStagedVersion()` is the cross-session source of truth; `s_StagedReleaseIndex` remains for same-session download tracking

### Next Steps
- Build test: Download a version, close without restarting, reopen — verify Switch button appears
- (Unchanged from S49) SPF-3 playtest, catalog C-1/C-2, menu replacement Group 1

---

## Session 48 -- 2026-03-25

**Focus**: Dev Window overhaul, project cleanup, infrastructure hardening

### What Was Done

**Dev Window (dev-window.ps1)**:
- Fixed UI thread hang: git status moved to background runspace, then to Activated event
- NotesSaveTimer race condition fixed (no more dispose-in-tick)
- Font caching in paint handlers (no per-frame allocation)
- Tab background white strip eliminated (dark panel wrapper)
- Auth label: clickable button, opens `gh auth login` if unauthenticated
- GitHub + Folder buttons moved to main UI (bottom of Build tab)
- Two font size settings (Button + Detail) with live refresh
- Stable/Dev toggle checkbox for releases
- Documentation tab (split pane: file list + content reader, 30/70 ratio)
- Clean Build toggle button (beneath BUILD, wipes build dirs before configure)
- Post-build copy list configurable via settings
- Client/server status labels show exe existence on startup
- Latest release label shows tag + dev/stable + color
- Background runspaces now pass PATH for gh CLI access

**Release pipeline (release.ps1)**:
- All 7 PS7-only syntax violations fixed for PS5 compatibility
- All em dashes replaced with ASCII
- Unified release: single tag (v0.0.1) with both client + server attached
- Auto-overwrite existing releases (delete + recreate with sound notification)
- GIT_TERMINAL_PROMPT=0 in subprocess environment

**Project cleanup**:
- Deleted: 6 runbuild scripts, fix_endscreen, phase3 docs, context-recovery.skill, mods folder info, PROMPT.md, context.md (106KB monolithic), ROADMAP.md, pd-port-director-SKILL.md, CHANGES.md, old devtools (build-gui, playtest-dashboard, doc-reader + .bat launchers)
- Deleted: 4.3GB of abandoned Claude Code worktrees
- Created: UNRELEASED.md (player-facing changelog), dist/windows/icon.ico + icon.rc
- Session log archived (S22-46 to sessions-22-46.md, active trimmed to 229 lines)
- tasks-current.md cleaned (completed items removed)
- COWORK_START.md rewritten as lean bootstrap pointer

**Code fixes**:
- fs.c: data directory search priority fixed (exe dir first, then cwd, then AppData)
- romdata.c: creates data/ dir + README.txt when ROM missing, then opens correct folder
- .build-settings.json: ROM path updated to new project location

**Skill + context**:
- game-port-director skill updated with Sections 8-9 (design principles, tool patterns)
- Skill packaged as .skill for reinstallation
- Context canonical location documented in CLAUDE.md
- 6 memories saved (profile, event-driven, clean structure, no worktrees, ACK messages, no ambiguous intent)

### Decisions Made
- Event-driven over polling (standing principle)
- Unified release tag (v0.0.1) replaces split client/server tags
- context/ is canonical location, parent copies are convenience mirrors
- No worktrees: all code changes in working copy

### Bugs Noted
- B-18: Pink sky on Skedar Ruins (possible texture/clear color issue)
- B-19: Bot spawn stacking on Skedar Ruins (all bots spawn at same pad)

### Session 48 continued -- Collision Rewrite + Debug Vis

**Collision system** (meshcollision.c + meshcollision.h):
- Triangle extraction from model DL nodes (G_TRI1, G_TRI4) -- WORKING
- Room geometry extraction (geotilei, geotilef, geoblock) -- WORKING, 7,110 tris on Skedar
- Static world mesh with spatial grid (256-unit cells) -- WORKING
- Dynamic mesh attachment via colmesh* field on struct prop -- CODED
- capsuleSweep: mesh primary, legacy fallback -- ACTIVE
- capsuleFindFloor: mesh primary -- ACTIVE, confirmed in logs
- capsuleFindCeiling: mesh primary -- FIXED slack formula, needs retest
- Stage lifecycle hooks in lv.c -- ACTIVE on all gameplay stages

**Debug visualization** (meshdebug.c):
- F9 toggles surface tinting in the GBI vertex pipeline
- Green=floor, Red=wall, Blue=ceiling based on vertex normals
- Zero overhead when off (cached flag check per frame)

**Data path fixes**:
- fs.c: exe dir searched first for data/ folder
- romdata.c: creates data/ dir + README.txt when ROM missing, opens correct folder
- dev-window.ps1: Copy-AddinFiles server guard removed (was blocking all copies)
- release.ps1: unified tag, auto-overwrite, PS5 compat, all em dashes fixed

### Session 48 continued -- Collision Disabled + Multiplayer Planning

**Collision rewrite DISABLED**: original system fully restored. Mesh collision code preserved
in meshcollision.c/h for Phase 2 redesign. Needs proper design accounting for: no original
ceiling colliders, jump-from-prop detection (simple downward raycast), slope behavior,
Thrown Laptop Gun as ceiling detection reference. HIGH PRIORITY return.

**ASSET_EFFECT type** added to catalog: 6 effect types (tint, glow, shimmer, darken, screen,
particle), 6 targets (scene, player, chr, prop, weapon, level). First effect mod pending.

**Live console**: backtick toggle, 256-line ring buffer, color-coded ImGui window.

**Multiplayer infrastructure vision confirmed (Mike)**: server = social hub with persistent
connections. Players connect and exist as presence regardless of activity (solo campaign,
MP match, co-op, splitscreen, level editor). Rooms for concurrent activities. Server mesh/
federation for load distribution. Player profiles with stats/achievements/shared content.
Menu system audit needed (double-press issues, hierarchy).

### Session 48 continued -- Menu Manager + Multiplayer Plan

**Menu State Manager (SPF-2a)**:
- New files: `port/src/menumgr.c` + `port/include/menumgr.h`
- Stack-based (8 deep), 2-frame input cooldown on push/pop
- Initialized in main.c, ticks in mainTick() (src/lib/main.c)
- pdguiProcessEvent blocks all key/button input during cooldown
- Pause menu wired: open checks cooldown, pushes MENU_PAUSE; close pops
- Modding hub wired: open pushes MENU_MODDING, back pops
- End Game confirm button now uses pdguiPauseMenuClose() instead of direct flag set
- Legacy PD menus (g_MenuData.root) not yet wrapped -- separate task

**Multiplayer Plan** (context/multiplayer-plan.md):
- Full design doc written covering server-as-hub, rooms, federation, profiles, phonetic
- Confirmed decisions: all MP through dedicated server, campaign = co-op (offline OK),
  automatic federation routing, stats framework first, editor pre-1.0 but lower priority
- Splitscreen works offline, treated as group when connected to server
- Campaign has dual authority: local (offline) or server (online)

**ASSET_EFFECT** added to catalog enum (12th asset type). Effect types + targets defined.
Release script updated: only zip attached (no separate exe files).
Collision mesh system disabled, original restored. Code preserved for Phase 2.

### Next Steps
- SPF-2b: verify SPF-1 build (hub/room/identity/phonetic)
- SPF-3a: lobby ImGui screen design + implementation
- ASSET_EFFECT mod creation + mods copy pipeline
- Wire remaining menus through menu manager (settings, etc.)
- B-19, B-20, B-18 bug investigation
- Collision Phase 2 design (HIGH PRIORITY)

---

## Session 47d — 2026-03-24

**Focus**: SPF-1 — Server Platform Foundation (hub lifecycle, room system, identity, phonetic encoding)

### What Was Done

Implemented the server platform foundation layer on top of the existing ENet dedicated server.
Four new module pairs + wiring into server_main.c + server_gui.cpp tab bar.

**New files (8):**

1. **`port/include/phonetic.h`** / **`port/src/phonetic.c`** — CV syllable IP:port encoding.
   16 consonants × 4 vowels = 6 bits/syllable × 8 syllables = 48 bits (IPv4 + port).
   Format: `"BALE-GIFE-NOME-RIVA"` — shorter than word-based connect codes. Both coexist.
2. **`port/include/identity.h`** / **`port/src/identity.c`** — `pd-identity.dat` persistence.
   Magic `PDID`, version byte, 16-byte UUID (xorshift128 seeded from SDL perf counter + time),
   up to 4 profiles (name/head/body/flags). Validates on load, rebuilds default on corruption.
3. **`port/include/room.h`** / **`port/src/room.c`** — Room struct + 5-state lifecycle.
   Pool of 4 rooms. Room 0 permanently wraps the existing match lifecycle (never truly closes).
   States: LOBBY→LOADING→MATCH→POSTGAME→CLOSED. Transitions logged via `sysLogPrintf`.
4. **`port/include/hub.h`** / **`port/src/hub.c`** — Hub singleton owning rooms + identity.
   `hubTick()` reads `g_Lobby.inGame` each frame → drives room 0 state machine.
   One-frame POSTGAME bridge on match end. Derives hub state from aggregate room states.

**Modified files (3):**

5. **`port/src/server_main.c`** — Added `hubInit()` / `hubTick()` / `hubShutdown()` calls.
6. **`port/fast3d/server_gui.cpp`** — Middle panel converted to tabbed layout.
   "Server" tab: existing player list + match controls. "Hub" tab: hub state + room table
   with color-coded states. Log panel: HUB: prefix highlighted purple.
7. **`context/server-architecture.md`** — SPF-1 section added (hub/room diagram, phonetic,
   GUI changes, new file table).

**Commit**: `fb5450b feat(SPF-1): hub lifecycle, room system, player identity, phonetic encoding`

### Decisions Made

- **Backward compatibility**: Room 0 driven by `g_Lobby.inGame` observation — zero changes
  to `net.c` or `netlobby.c`. Existing single-match path unchanged.
- **Protocol**: v21 unchanged. No new ENet messages. Both phonetic and word connect codes
  remain available.
- **`HUB_MAX_CLIENTS`**: Defined directly in `room.h` (= 8) rather than including `net/net.h`
  to keep hub modules standalone and avoid the full game header chain.
- **Boolean fields**: Used `int` not `_Bool`/`bool` in new C modules (port/ files, but
  matching the project convention of `s32` for boolean-like values).
- **Room 0 persistence**: `roomDestroy()` on room 0 resets to LOBBY instead of CLOSED —
  room 0 is the permanent lounge for the existing server lifecycle.

### Dev Build Status

**UNVERIFIED** — Build environment broken in session (GCC TEMP path issue in sandbox).
`build-headless.ps1` TEMP/TMP fix committed. User to verify build from local environment.

### Session 47e Follow-up — 2026-03-24

**Focus**: Fix server build — SPF-1 symbols undefined in pd-server

**Root cause**: SRC_SERVER in CMakeLists.txt is a manually curated list; the 4 new SPF-1
files (hub.c, room.c, identity.c, phonetic.c) were not added when coded in S47d.
Client uses GLOB_RECURSE so it picked them up automatically; server did not.

**Fix**: Added 4 entries to SRC_SERVER block in CMakeLists.txt (lines 478–482).
Commit `c788486`. Pushed to dev.

**Build status**: Cannot verify in sandbox (GCC DLL loading issue — cc1.exe needs
libmpfr-6.dll via Windows PATH, not POSIX PATH). Run `.\devtools\build-headless.ps1 -Target server`
from PowerShell to confirm.

### Next Steps

- Run `.\devtools\build-headless.ps1 -Target server` from PowerShell to confirm fix
- Build and QC test SPF-1 modules (see qc-tests.md)
- SPF-2: Room federation / multi-room support
- D5: Settings persistence for server configuration

---

## Session 47b — 2026-03-24

**Focus**: B-12 Phase 2 — Migrate chrslots callsites to participant API

### What Was Done

Completed the Phase 2 migration of all chrslots bitmask read/write sites across 5 files.
Phase 1 bulk-sync calls (`mpParticipantsFromLegacyChrslots`) replaced with targeted
`mpAddParticipantAt`/`mpRemoveParticipant` at each write site.

**Key design established:**
- Pool capacity is `MAX_MPCHRS` (40), not the Phase 1 default 32
- Pool slot `i` == chrslots bit `i` (players 0–7, bots 8–39)
- `mpIsParticipantActive(i)` is a direct drop-in for `chrslots & (1ull << i)`
- New `mpAddParticipantAt(slot, type, ...)` API for exact-slot placement

**Files changed (7):**

1. **`src/include/game/mplayer/participant.h`** — Added `mpAddParticipantAt()` declaration
2. **`src/game/mplayer/participant.c`** — Added `mpAddParticipantAt()` impl; rewrote
   `mpParticipantsToLegacyChrslots` (slot index IS bit index) and
   `mpParticipantsFromLegacyChrslots` (use `mpAddParticipantAt` for exact placement)
3. **`src/game/mplayer/mplayer.c`** — ~25 sites: mpInit, match lifecycle, bot create/copy/
   remove, score, team assignment, name generation, save/load config and WAD
4. **`src/game/mplayer/setup.c`** — 10 sites: handicap CHECKHIDDEN, team loop ×3,
   bot slot UI, simulant name display, player file availability
5. **`src/game/challenge.c`** — Read check + fix `1u`→`1ull` write bug + add participant
   calls alongside chrslots writes in `challengePerformSanityChecks`
6. **`src/game/filemgr.c`** — 2 player-file presence checks
7. **`port/src/net/matchsetup.c`** — `mpClearAllParticipants()` + `mpAddParticipantAt`
   at each player/bot write site

**Commit**: `94a2b1e feat(B-12-P2): migrate chrslots callsites to participant API`

### Dev Build Status

**PASS** — `cmake --build --target pd` clean (exit 0). All 7 files compiled without errors.

### Decisions Made

- `challengeIsAvailableToAnyPlayer` reads `chrslots & 0x000F` as a bitmask for challenge
  availability computation — left as-is (no clean participant API equivalent, chrslots
  still dual-written in Phase 2)
- `mp0f18dec4` VERSION guard retained (PC builds are >= JPN_FINAL, always included)
- `setup.c` fixes applied via line-by-line PowerShell replace (Edit tool had CRLF mismatch)

### Next Steps

- B-12 Phase 3: Remove `chrslots` field + legacy shims + BOT_SLOT_OFFSET
- Protocol version bump to v21 (SVC_STAGE_START uses participant list)
- QC: in-game bot add/remove, match start/end, save/load bot config

---

## Session 47c — 2026-03-24

**Focus**: Stage Decoupling Phase 2 (Dynamic stage table) + Phase 3 (Index domain separation)

### What Was Done

**Phase 2 — Dynamic stage table** (7 files):

1. **`src/game/stagetable.c`** — Renamed static array to `s_StagesInit[]`, added heap pointer `g_Stages` + `g_NumStages`. `stageTableInit()` mallocs+memcpys. `stageGetEntry(index)` bounds-checked accessor. `stageTableAppend(entry)` realloc-based. Both `stageGetCurrent()` and `stageGetIndex()` rewritten to use `g_NumStages`. `soloStageGetIndex(stagenum)` iterates `g_SoloStages[0..NUM_SOLOSTAGES-1]`.
2. **`src/include/data.h`** — `extern struct stagetableentry *g_Stages` + `extern s32 g_NumStages` (was array).
3. **`src/include/game/stagetable.h`** — Full declaration set for all Phase 2 + 3 functions.
4. **`src/game/bg.c`** — `ARRAYCOUNT(g_Stages)` replaced with `g_NumStages` (2 occurrences).
5. **`port/src/assetcatalog_base.c`** — Removed local `extern struct stagetableentry g_Stages[]` (conflicted with pointer decl). Bounds check `idx >= 87` → `idx >= g_NumStages`.
6. **`port/src/main.c`** — Added `stageTableInit()` call before `assetCatalogRegisterBaseGame()`.

**Phase 3 — Index domain guards** (2 files):

7. **`src/game/endscreen.c`** — 9 guard sites: `endscreenMenuTitleRetryMission`, `endscreenMenuTitleNextMission`, `endscreenMenuTitleStageCompleted`, `endscreenMenuTextCurrentStageName3`, `endscreenMenuTitleStageFailed`, `endscreenHandleReplayPreviousMission` (underflow), `endscreenAdvance()` (overflow), `endscreenHandleReplayLastLevel`, `endscreenContinue` DEEPSEA (2 paths, both guarded).
8. **`src/game/mainmenu.c`** — 4 guard sites: `menuTextCurrentStageName`, `soloMenuTitleStageOverview`, `soloMenuTitlePauseStatus`, `isStageDifficultyUnlocked` (top guard returns true for out-of-range — mod stages treated as unlocked).

**Bonus fix**: Restored `src/game/mplayer/setup.c` and `src/game/setup.c` from commit `4704eab` after auto-commit `0a36981` corrupted them (all tabs replaced with literal `\t`). Pre-existing bug revealed by full rebuild.

### Decisions Made

- `soloStageGetIndex()` lives in `stagetable.c` (iterates `g_SoloStages[]`). It is the Phase 3 domain translation function.
- `isStageDifficultyUnlocked(stageindex < 0 || >= NUM_SOLOSTAGES)` returns `true` — mod stages are "unlocked" by definition (no solo-stage-based unlock system applies to them).
- `ARRAYCOUNT(g_Stages)` was eliminated. Any future code must use `g_NumStages`.

### Dev Build Status

**PASS** — `build-headless.ps1 -Target client` clean (exit 0). All modified files compiled without errors. Warnings in bg.c are pre-existing.

### Next Steps

- MEM-2: `assetCatalogLoad()` / `assetCatalogUnload()`
- MEM-1 build test: full cmake pass confirms `assetcatalog.h` struct changes are stable
- S46b: Full asset catalog enumeration (animations, SFX, textures)

---

## Session 47a — 2026-03-24

**Focus**: MEM-1 — Asset Catalog load state tracking fields

### What Was Done

Added lifecycle state tracking to `asset_entry_t` as the foundation for Phase D-MEM
memory management. This is purely additive — no existing behavior changes.

**Files changed (4 files):**

1. **`port/include/assetcatalog.h`** — Added `asset_load_state_t` enum
   (`REGISTERED`/`ENABLED`/`LOADED`/`ACTIVE`). Added `#define ASSET_REF_BUNDLED 0x7FFFFFFF`.
   Added 4 fields to `asset_entry_t`: `load_state`, `loaded_data`, `data_size_bytes`,
   `ref_count`. Added `assetCatalogGetLoadState()` and `assetCatalogSetLoadState()`
   declarations in new "Load State API (MEM-1)" section.

2. **`port/src/assetcatalog.c`** — `assetCatalogRegister()` initializes new fields:
   `ASSET_STATE_REGISTERED`, `loaded_data=NULL`, `data_size_bytes=0`, `ref_count=0`.
   `assetCatalogSetEnabled()` now advances `REGISTERED→ENABLED` on first enable.
   Added `assetCatalogGetLoadState()` and `assetCatalogSetLoadState()` implementations.

3. **`port/src/assetcatalog_base.c`** — All 4 bundled registration sites (stages, bodies,
   heads, arenas) now set `load_state=ASSET_STATE_LOADED` and `ref_count=ASSET_REF_BUNDLED`.

4. **`port/src/assetcatalog_base_extended.c`** — All 7 bundled registration sites (weapons,
   animations, textures, props, gamemodes, audio, HUD) now set `ASSET_STATE_LOADED` and
   `ref_count=ASSET_REF_BUNDLED`.

### Decisions Made

- `ASSET_REF_BUNDLED = 0x7FFFFFFF` (S32_MAX) as documented in MEM-1 spec.
- `REGISTERED→ENABLED` transition happens in `setEnabled(id, 1)`. If load_state is already
  LOADED or ACTIVE (bundled assets), setEnabled does not downgrade state.
- `assetCatalogSetLoadState()` is a raw setter — callers own the validity of transitions.
  Future eviction logic will use `ref_count` to guard bundled assets.
- `loaded_data` / `data_size_bytes` fields left at NULL/0 for all existing entries —
  wired for the future loader, not populated yet.

### Dev Build Status

- Syntax-check (MinGW gcc -fsyntax-only): **PASS** on all 3 modified .c files
- Full cmake build: needs Mike's `build-headless.ps1` run (cmake env not available in session)

### Next Steps

- MEM-2: Implement `assetCatalogLoad()` / `assetCatalogUnload()` (allocate/free loaded_data)
- MEM-3: ref_count acquire/release + eviction policy (skip if `ref_count == ASSET_REF_BUNDLED`)
- Wire load state into mod manager UI (show loaded/active indicators)

---

## Session S79 — 2026-03-29

### Focus

T-1 and T-2 from mod-system-features-and-todos.md (nice-to-have TODOs).

### What Was Done

1. **T-1: Map mode string parsing** (`port/src/assetcatalog_scanner.c`, `port/include/assetcatalog.h`)
   - Added `MAP_MODE_MP`, `MAP_MODE_SOLO`, `MAP_MODE_COOP` bitmask constants to `assetcatalog.h`
   - Added `parseModeString()` static helper in `assetcatalog_scanner.c` — parses pipe-separated
     tokens ("mp|solo", "coop", etc.) into the bitmask
   - Replaced `e->ext.map.mode = 0; /* TODO: parse mode string */` with
     `e->ext.map.mode = parseModeString(iniGet(ini, "mode", ""));`

2. **T-2: Weapon table coverage audit** (`port/src/assetcatalog_base.c`)
   - Verified: active registration table in `assetcatalog_base_extended.c` covers all 47
     MPWEAPON_* constants (0x01 FALCON2 through 0x2f SHIELD). Full coverage confirmed.
   - MPWEAPON_NONE (0x00) and MPWEAPON_DISABLED (0x30) are sentinels, not real weapons —
     correctly excluded.
   - `s_BaseWeapons[]` in `assetcatalog_base.c` is dead code (37 entries, never iterated).
     Updated its comment to clarify it is superseded; removed misleading TODO.

### Decisions Made

- MAP_MODE_* flags are bitmask (not enum) to allow combinations like `mp|solo`.
- 0 means "no mode restriction specified" (same as before — callers treat 0 as "all modes").
- Dead weapon table in base.c left in place (removal would be a separate clean-up task);
  comment updated to prevent future confusion.

### Build Status

- Syntax-check (MinGW gcc -fsyntax-only): **PASS** on both modified .c files
- Full cmake build: blocked by pre-existing `C:\WINDOWS\` temp-dir permission issue (unrelated
  to these changes)

### Next Steps

- T-3 through T-5 (animation/texture/SFX enumeration) — marked Important in audit
- T-10: size_bytes in mod manifest for download estimation
