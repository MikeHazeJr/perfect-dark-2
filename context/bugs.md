# Bug Tracker — One-Off Issues

> Track individual bugs: open, in-progress, fixed. For recurring architectural patterns (MAX_PLAYERS indexing, memory aliasing, index domain confusion), see [systemic-bugs.md](systemic-bugs.md).

---

## Open Bugs

| ID | Bug | Severity | Found | Root Cause | Status |
|----|-----|----------|-------|-----------|--------|
| B-18 | **Pink sky on Skedar Ruins** — sky renders pink instead of correct color | MED | S48 | Possible missing texture or clear color issue in sky rendering path | OPEN |
| B-19 | **Bot spawn stacking on Skedar Ruins** — all bots spawn at same pad | MED | S48 | Mod stages lack `INTROCMD_SPAWN` entries in setup file; fallback picks pad 0. Fix: populate `g_SpawnPoints` from arena pad data | OPEN |
| B-21 | **Menu double-press / hierarchy issues** — Escape and other inputs register multiple times, menu state confusion | MED | S48 | Likely input not consumed by menu manager fast enough, or menu hierarchy not fully wired through menuPush/Pop | OPEN |
| B-24 | **Connect code byte-order reversal** — Client decoded "wicked spider sliding under a savanna" as 199.148.8.67 instead of 67.8.148.199. Connected to wrong IP, timed out. | CRITICAL | S49 | `pdgui_menu_mainmenu.cpp` extracted bytes MSB-first `(ip>>24, ip>>16, ip>>8, ip)` while all other decode callers and the encoder use LSB-first `(ip, ip>>8, ip>>16, ip>>24)`. Only the main menu Join path had the wrong extraction order. | **FIXED (S49)**: Changed `pdgui_menu_mainmenu.cpp` to LSB-first extraction matching the encoder. |
| B-25 | **Server max clients hardcoded to 8** — Server log "max 8 clients", should be 32. | MEDIUM | S49 | `NET_MAX_CLIENTS` was `MAX_PLAYERS` (= 8, match player slots). This conflated network connection capacity with in-match player count. | **FIXED (S49)**: `NET_MAX_CLIENTS` changed to 32 in `net.h`, independent of `MAX_PLAYERS`. `PDGUI_NET_MAX_CLIENTS` also updated to 32 in debug menu. |
| B-26 | **Player name shows "Player1" instead of profile name** — Client connected with empty name; lobby showed "Player 1". | HIGH | S49 | `netClientReadConfig()` reads name from `g_PlayerConfigsArray[0].base.name` (legacy N64 save field). Only populated by `matchsetup.c` when `g_GameFile.name` is non-empty. Fresh PC client with no save file gets empty name. Identity profile has the correct name but was never consulted. | **FIXED (S49)**: `netClientReadConfig()` in `net.c` now falls back to `identityGetActiveProfile()->name` when the legacy config name is empty. Added `#include "identity.h"` to `net.c`. |
| B-10 | **End Game crash** — ACCESS_VIOLATION selecting End Game from pause menu | HIGH | S21 | Likely endscreen/results code. New ImGui pause menu path avoids crash (S26 build test). OG endscreen still shows and is broken but escapable. | LIKELY RESOLVED — needs Custom Post-Game Menu to fully replace |
| B-11 | **rendering-trace.md stale header** — States "no ImGui game menus" but ImGui menus now exist | LOW | S25 | Documentation drift | NEEDS UPDATE |
| B-12 | **24-bot cap** — Selected 32 bots, only 24 load | HIGH | S26 | `MAX_BOTS=24` in constants.h. u32 chrslots bitmask: 8 bits players + 24 bits bots = 32 total. | Phase 1 CODED (S26): Dynamic participant pool (`participant.h`/`.c`) runs parallel to chrslots. 6 sync hooks in mplayer.c. Phase 2: migrate callsites. Phase 3: remove chrslots. See [b12-participant-system.md](b12-participant-system.md). **Needs build test.** |
| B-13 | **GE prop scaling ~10x on mod stages** — Ammo crates oversized on GEX maps (Facility, Temple, etc.) | MED | S26 | **Two-part root cause**: (1) `model.c` rendering used `model->scale` instead of `modelGetEffectiveScale()` — FIXED S26. (2) `g_ModNum` not set during catalog-loaded stage transitions (B-17 smart redirect bypasses `modConfigParseStage()`), so `objInit()` falls through to base PD `g_ModelStates[]` scale (1.0×) instead of GEX `g_GexModelStates[]` (0.1×). | **Part 1 FIXED** (S26): `modelGetEffectiveScale()` in renderer. **Part 2 ROOT CAUSE IDENTIFIED** (S36): `g_ModNum` not set → wrong scale array. **Interim**: Ensure `g_ModNum` is set during catalog stage load. **Long-term**: Model Correction Tool (D3R-7) — visual comparison + binary rewrite to fix model baselines at 1.0 scale. `model_scale` in `.ini` becomes creative modifier only. |
| B-14 | **START on controller opens/closes pause immediately** — double-fire or input passthrough | MED | S26 | Legacy path (bondmove→ingame.c) opens pause, then ImGui render sees same GamepadStart press via polling and immediately closes it — all in one frame. | FIXED (S26): Frame guard `s_PauseJustOpened` skips close checks on the open frame. Also added pause menu to `pdguiProcessEvent` input consumption. **Verified.** |
| B-15 | **OG 'Paused' text renders behind ImGui menu** — legacy overlay still drawing | LOW | S26 | Legacy pause rendering not suppressed when ImGui menu active. Will be stripped eventually. | KNOWN |
| B-16 | **Back on controller does nothing** in ImGui pause menu | MED | S26 | Pause menu render function never handled `ImGuiKey_GamepadFaceRight` (B button). Input also wasn't consumed — `pdguiProcessEvent` excluded pause menu from the consumption check. | FIXED (S26): B button now navigates back (cancels End Game confirm, or closes pause). Added `pauseActive` to event consumption. **Verified.** |
| B-17 | **Mod stages load wrong maps** — Kakariko selection loads different map, 4 garbage entries at end of stage list | HIGH | S26 | Root cause: `modConfigParseStage()` patches `g_Stages[]` with wrong file IDs. Legacy mods also shadow base game files via modmgr + `--moddir`. | **STRUCTURALLY FIXED (S32)**: Catalog smart bgdata redirect bypasses `g_Stages[]` patching. **FULLY FIXED (S37)**: Disabled legacy mod copy + launch args in `build-gui.ps1`. Base game Felicity confirmed loading correctly. |

---

## Fixed Bugs

| ID | Bug | Root Cause | Fix | Session |
|----|-----|-----------|-----|---------|
| B-22 | **Version boxes not baking into exe** — dev window version fields ignored; exe always showed 0.0.7 regardless of setting | `Get-BuildSteps` in dev-window.ps1 built cmake configure args without any `-DVERSION_SEM_*` flags. CMake used its cached value (7) or the hardcoded `set(...CACHE...)` default on clean builds. | Added `Get-UiVersion` call inside `Get-BuildSteps`; appends `-DVERSION_SEM_MAJOR=X -DVERSION_SEM_MINOR=Y -DVERSION_SEM_PATCH=Z` to both client and server configure steps. Works for Build and Release paths (both call `Get-BuildSteps`). | S49 |
| B-23 | **Quit Game button clipped on right edge** — button border cut off; "Confirm Quit" text also overflowed fixed 100px width | Fixed `quitBtnW = 100*scale` placed button's right edge flush against the content clip boundary (cursor `dialogW - WindowPadding.x - 100*scale` + width = `dialogW - WindowPadding.x` = clip edge). No right margin. Also "Confirm Quit" wider than "Quit Game" but same fixed width used. | Width now `CalcTextSize("Confirm Quit").x + FramePadding*2` (sized to widest label). Position now `dialogW - WindowPadding.x - quitBtnW - margin` where `margin = 4*scale`, keeping right edge inside clip rect. Cancel button cursor updated to use new local coords. | S49 |
| B-01 | **Camera transition crash** (24-bot combat) | S18 POS_DESYNC diagnostic in `chrUpdateGeometry` called for all nearby props, including bots with unallocated model matrices | Removed diagnostic, hardened `chrTestHit` with `model->matrices` check | S20–21 |
| B-02 | **Shots pass through bots** | `modeldef->scale > 100` clamp in body.c/modelcatalog.c destroyed valid mod model scales (~1162 → 1.0) | Removed clamp, now only rejects ≤ 0 | S20–21 |
| B-03 | **Player instant death** (1-hit kills) | `g_PlayerConfigsArray[0].handicap = 0` (BSS zero-init). `mpHandicapToDamageScale(0) = 0.1` → `damage /= 0.1` = 10× multiplier | Force `handicap = 0x80` for any player with `handicap == 0` at match start | S21 |
| B-04 | **Paradox crash** (0-bot and 24-bot) | `cheatIsUnlocked()` accessed `besttimes[85]` — OOB into 21-element array | Bounds check `stage_index >= NUM_SOLOSTAGES` in cheats.c | S22 |
| B-05 | **Paradox match hang** | `g_StageSetup.intro` pointer aliases into props data for mod stages. Intro cmd loop reads garbage, never terminates. | Intro validation at load time (proximity + cmd type range check), NULL if invalid | S23 |
| B-06 | **Uninitialized rooms[] after intro NULL** | When intro is NULL, spawn loop skipped → `rooms[8]` contains stack garbage → `cdFindGroundInfoAtCyl` reads garbage room numbers | Initialize `rooms[8]` with -1 sentinels, pad-0 fallback spawn | S23 |
| B-07 | **Divide-by-zero in spawn selection** | `playerChooseSpawnLocation` does `rngRandom() % numpads` when `numpads == 0` | Early-return guard, pad 0 fallback | S23 |
| B-08 | **Mod manager can't find mods directory** | `modmgrScanDirectory()` resolved to `./data/mods` instead of `./mods/` | Try CWD, exe dir, then base dir fallback | S23 |
| B-09 | **CI overlay corruption** with mods | `g_NotLoadMod` was BSS zero-init (false). Boot CI load and post-MP CI return both got mod-overlaid props | Init `g_NotLoadMod = true`, re-set in `lvReset()` for non-gameplay stages | S24 |
| B-09b | **Bundled mod ID mismatch** | `"darknoon"` and `"goldfinger64"` didn't match directory-derived IDs `"dark_noon"` and `"goldfinger_64"` | Fixed string literals in modmgr.c | S24 |
| B-20 | **Mission 1 objective crash** — ACCESS_VIOLATION on mission objective completion | NULL guard added in modelmgrInstantiateModel | Objective completion spawns a chr whose body filenum fails to load → NULL modeldef → crash in `modelmgrInstantiateModel`. NULL guard added. | S48 |

---

## How to Use

- When you find a bug, add it to **Open Bugs** with an ID (B-XX, sequential).
- When diagnosed, fill in Root Cause. When fixed, move to **Fixed Bugs** with the session number.
- If a bug reveals a *class* of problems (it'll happen again in similar code), also log it in [systemic-bugs.md](systemic-bugs.md).
