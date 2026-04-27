# Catalog Universality Sweep -- Final Selector Migration

> **Date**: 2026-04-27
> **Scope**: EVERY remaining Layer A iteration site that drives a user-facing selector / random pick / unlock-filtered list. Heads (dev `132a883c`), bodies (dev `e7c4e702`), maps/arenas (dev `de9098cc`) already shipped. This sweep migrates the rest.
> **Architectural directive (Mike, 2026-04-27)**: "I want ALL assets migrated, otherwise what is the point of the catalog if it is not SINGLE SOURCE OF TRUTH, why would we introduce even the possibility of reading stale data from something hard-coded when we have our live, dynamic, validated catalog?"
> **Memory anchor**: `catalog-builds-all-selector-pools` -- selector pool = catalog INTERSECT unlock-state.
> **Constraint anchor (`context/constraints.md` line 32)**: "Catalog registers ALL assets (unlock is a separate layer)."
> **Working precedents**: heads `context/audits/catalog-migration-heads-2026-04-26.md`; bodies `context/audits/catalog-migration-bodies-2026-04-26.md`; maps `context/audits/catalog-migration-maps-2026-04-26.md`.
> **Phase**: 1 of 4 -- AUDIT only. Phase 2 = migration commits, Phase 3 = verify, Phase 4 = auto-merge.

---

## Executive summary

The sweep finds **five remaining Layer A asset domains** that still drive user-facing selectors via static-array iteration:

| # | Domain | Catalog type | Layer A array | # sites | Catalog already populated? | Unlock field on ext? |
|---|---|---|---|---:|---|---|
| W | **Weapons** | `ASSET_WEAPON` | `g_MpWeapons[]` (47) | 7 | YES (47 entries via `assetcatalog_base_extended.c`) | NO -- add `requirefeature` |
| S | **Scenarios** (gamemodes) | `ASSET_GAMEMODE` | `g_MpScenarioOverviews[]` (6) | 6 | YES (6 entries) | NO -- add `requirefeature` |
| M | **Music tracks** | `ASSET_AUDIO` (cat=MUSIC) | `g_MpTracks[]` (43) | 5 | YES (43 entries) | NO -- add `unlockstage` |
| B | **Bot profiles** | (none) | `g_BotProfiles[]` (12) | 2 | NO -- needs new `ASSET_BOT_PROFILE` type or piggyback `ASSET_BOT_VARIANT` | n/a |
| C | **CI Training character bios** | `ASSET_BODY` + `ASSET_HEAD` | `g_HeadsAndBodies[]` (152) | 2 | YES (heads + bodies; SP fallback covers stragglers) | bio-unlock is a separate semantic (best-times), keep callsite predicate |

**Total: 22 migration sites** across 5 domains. Below the 30-site stop condition; one bundled session is feasible.

Two catalog mechanics need extending:

1. **`assetCatalogIterateUnlockedByType` switch** -- add `ASSET_WEAPON` and `ASSET_GAMEMODE` cases reading newly-added `ext.weapon.requirefeature` / `ext.gamemode.requirefeature`. One file, additive.
2. **Music tracks** -- the unlock semantic is best-time-based, not feature-based. Add `ext.audio.unlockstage` (mirrors `g_MpTracks[].unlockstage`) and a sibling helper `assetCatalogIterateUnlockedMusic` that consults `g_GameFile.besttimes[stageindex][]` (same predicate as `mpIsTrackUnlocked`). Keep the generic feature-based helper unchanged.

Bot profiles is the most architectural item -- `g_BotProfiles[]` has no catalog representation today. Decision below (Section F).

---

## Section A -- Layer A iteration sites by domain

### A.W -- Weapons (`g_MpWeapons[]`, 47 entries; `g_MpWeaponSets[]`, 16 entries)

Layer A array: [src/game/mplayer/mplayer.c:78](src/game/mplayer/mplayer.c:78) (post-cull 47 entries). Server stub: [port/src/server_stubs.c:406](port/src/server_stubs.c:406).

Catalog already registers all 47 via `assetcatalog_base_extended.c::s_BaseWeapons[]`. `ext.weapon` carries `weapon_id`, `name`, `model_file`, `damage`, `fire_rate`, `ammo_type`, `dual_wieldable` -- but **no `requirefeature` field**.

| # | File:line | Function | What it iterates | Filter today | Shape |
|---|---|---|---|---|---|
| W.1 | [src/game/mplayer/mplayer.c:1266](src/game/mplayer/mplayer.c:1266) | `mpGetNumWeaponOptions` | full `g_MpWeapons[]` | `challengeIsFeatureUnlocked(catalogGetMpWeaponUnlockFeature(i))` | unlocked count for slot dropdowns |
| W.2 | [src/game/mplayer/mplayer.c:1280](src/game/mplayer/mplayer.c:1280) | `mpGetWeaponLabel` | full `g_MpWeapons[]` | unlock filter, walk to N-th unlocked | display name resolution for slot dropdowns |
| W.3 | [src/game/mplayer/mplayer.c:1314](src/game/mplayer/mplayer.c:1314) | `mpSetWeaponSlot` | `g_MpWeapons[0..mpweaponnum]` | unlock filter, advance over locked | unlocked-index -> mp_index commit |
| W.4 | [src/game/mplayer/mplayer.c:1330](src/game/mplayer/mplayer.c:1330) | `mpGetWeaponSlot` | `g_MpWeapons[0..g_MpSetup.weapons[slot])` | unlock filter | mp_index -> unlocked-index for dropdown selected state |
| W.5 | [src/game/mplayer/mplayer.c:1498](src/game/mplayer/mplayer.c:1498) | `mpSetRandomWeapons` | full `g_MpWeapons[]` | unlock filter + random filter mask | builds the random-pick pool from `g_MpWeaponSetRandomFilters[]` |
| W.6 | [src/game/mplayer/mplayer.c:1377](src/game/mplayer/mplayer.c:1377) | `mpCountWeaponSetThing` | `g_MpWeaponSets[0..weaponsetindex)` | per-set unlock | weapon set picker count |
| W.7 | [src/game/mplayer/mplayer.c:1399](src/game/mplayer/mplayer.c:1399) | `func0f188f9c` | full `g_MpWeaponSets[]` | per-set unlock | weapon set picker resolution |

**Selector consumers (UI surfaces these feed):**
- 6x slot dropdowns -- [port/fast3d/pdgui_menu_mpsetup.cpp:862-890](port/fast3d/pdgui_menu_mpsetup.cpp:862) -- `menuhandlerMpWeaponSlot` -> W.1, W.2.
- "Set:" weapon-set dropdown -- same renderer -> `menuhandlerMpWeaponSetDropdown` -> W.6, W.7.
- "Auto Random" picker -- W.5 via `mpApplyWeaponSet`.
- "Select Random Weapons" submenu -- [src/game/mplayer/setup.c:1419-1537](src/game/mplayer/setup.c:1419) -- iterates `g_MpWeapons[]` to render checkbox list. **Out of scope this sweep**: this is the random filter MUTATOR (sets `g_MpWeaponSetRandomFilters[]` per-mp-index by enum value); migrating it would require redesigning the filter mask shape since the mask is sized to `NUM_MPWEAPONS`. The displayed labels already route through W.1/W.2 so name/count are catalog-correct after Step W lands.

**Migration plan W**:
1. **W.0 -- catalog-side prep**: extend `ext.weapon` with `u8 requirefeature`. Populate at registration from `g_MpWeapons[i].unlockfeature`. Extend `s_entryRequireFeature` switch in `assetcatalog_api.c` to return `e->ext.weapon.requirefeature`.
2. **W.1-W.5 -- weapon slot picker**: replace ARRAYCOUNT(g_MpWeapons) walks with `assetCatalogIterateUnlockedByType(ASSET_WEAPON, ...)`. The catalog entry's `mp_index` field carries the original mp_idx, so the unlocked-index <-> mp_index round-trip is preserved by sorting collected entries on `mp_index` (stable order matches the legacy enum order).
3. **W.6, W.7 -- weapon set picker**: `g_MpWeaponSets[]` is a SET-of-weapons table, not a weapon table. It carries `requirefeatures[4]` (a list of 4 feature gates) and `unk0c` (single-weapon fallback). It is NOT a catalog asset class today (catalog has ASSET_WEAPON for individual weapons; weapon SETS are a higher-level grouping). **Out of scope** for the universal asset migration: this is "presets for selector input", not an asset type. Note added to `constraints.md` Removed-Constraints clarification.

**Wire format**: `g_MpSetup.weapons[NUM_MPWEAPONSLOTS]` carries u8 mp_index per slot. Already stored as catalog ID strings on the wire (per heads precedent -- protocol v37 uses `weapons[NUM_MPWEAPONSLOTS]` strings); the integer `g_MpSetup.weapons[]` is local engine state that migrates through `mpSetWeaponSlot` (W.3). No protocol bump needed.

**Save format**: `mpsetupfileLoadWad` (mplayer.c:4471) reads 7-bit mp_index per slot. The file format constraint at `constraints.md` says save format is frozen -- the 7-bit slot stays an mp_index. The catalog migration is at the SELECTOR level, not at the file format level. No save migration required.

### A.S -- Scenarios (`g_MpScenarioOverviews[]`, 6 entries)

Layer A array: [src/game/mplayer/scenarios.c:256](src/game/mplayer/scenarios.c:256). 6 entries (Combat / HTB / HTM / PAC / KOH / CTC).

Catalog already registers all 6 via `assetcatalog_base_extended.c::s_BaseGameModes[]`. `ext.gamemode` carries `mode_id`, `name`, `description`, `min_players`, `max_players`, `team_based` -- but **no `requirefeature` field**.

| # | File:line | Function / case | What it iterates | Filter today |
|---|---|---|---|---|
| S.1 | [src/game/mplayer/scenarios.c:329](src/game/mplayer/scenarios.c:329) | `scenarioScenarioMenuHandler::GETOPTIONCOUNT` | full `g_MpScenarioOverviews[]` | unlock + teamonly filter |
| S.2 | [src/game/mplayer/scenarios.c:339](src/game/mplayer/scenarios.c:339) | `::GETOPTIONTEXT` | same | same |
| S.3 | [src/game/mplayer/scenarios.c:352](src/game/mplayer/scenarios.c:352) | `::SET` | same | same |
| S.4 | [src/game/mplayer/scenarios.c:367](src/game/mplayer/scenarios.c:367) | `::GETSELECTEDINDEX` | same | same |
| S.5 | [src/game/mplayer/scenarios.c:391](src/game/mplayer/scenarios.c:391) | `::GETGROUPSTARTINDEX` | `g_MpScenarioOverviews[0..startindex)` | same |
| S.6 | [src/game/mplayer/scenarios.c:275](src/game/mplayer/scenarios.c:275) | `mpOptionsMenuDialog::TICK` | `g_MpScenarios[]` to find current scenario's options dialog | none -- defensive scan |

**Selector consumers**:
- ImGui Scenario picker [port/fast3d/pdgui_menu_mpsetup.cpp:750-815](port/fast3d/pdgui_menu_mpsetup.cpp:750) -- routes through `list_GetOptionCount(scenarioScenarioMenuHandler, ...)` -> S.1-S.5.
- Quick-Team variant uses same handler with param=1.

**Migration plan S**:
1. **S.0 -- catalog-side prep**: extend `ext.gamemode` with `u8 requirefeature`. Populate at registration from `g_MpScenarioOverviews[].requirefeature` (table at scenarios.c:256). Extend `s_entryRequireFeature` switch.
2. **S.1-S.5 -- scenario picker handler**: replace ARRAYCOUNT walks with `assetCatalogIterateUnlockedByType(ASSET_GAMEMODE, ...)`. The `teamonly` filter is on `ext.gamemode.team_based` -- already a catalog field. The handler-side filter `(teamgame || !overview.teamonly)` becomes `(teamgame || !e->ext.gamemode.team_based)`.
3. **S.6 -- mpOptionsMenuDialog defensive scan**: ARRAYCOUNT(g_MpScenarios) walk is to find the legacy `optionsdialog` field on the static `g_MpScenarios[]` (which carries the menudialogdef pointer for the per-scenario options dialog). The optionsdialog is NOT a catalog field -- it's UI infrastructure. **Out of scope**: this scans for a specific menudialog hit; it's not a selector pool. Leave as-is and note in audit.

**Wire format**: `g_MpSetup.scenario` is u8. Already wired as catalog ID string in matchsetup.c:984 / netmsg.c:1164 / net.c:618 (per maps/arenas migration). No protocol bump.

**Save format**: `g_MpSetup.scenario` saves as 3-bit field. Migration only at SELECTOR level. No save migration.

### A.M -- Music tracks (`g_MpTracks[]`, 43 entries)

Layer A array: [src/game/mplayer/mplayer.c:3130](src/game/mplayer/mplayer.c:3130). 43 entries.

Catalog already registers all 43 via `assetcatalog_base_extended.c::s_BaseMusicTracks[]` as `ASSET_AUDIO` with `category = AUDIO_CAT_MUSIC` (=1). `ext.audio` carries `sound_id` (musicnum), `name`, `category`, `duration_ms`, `file_path` -- but **no `unlockstage` field**.

The unlock semantic is **distinct from `challengeIsFeatureUnlocked`**: a music track is unlocked iff the player has any best-time on its associated solo stage (`mpIsTrackUnlocked` -- mplayer.c:3181 -- reads `g_GameFile.besttimes[stageindex][i]`).

| # | File:line | Function | What it iterates | Filter today |
|---|---|---|---|---|
| M.1 | [src/game/mplayer/mplayer.c:3201](src/game/mplayer/mplayer.c:3201) | `mpGetTrackSlotIndex(tracknum)` | `g_MpTracks[0..tracknum)` | `mpIsTrackUnlocked` |
| M.2 | [src/game/mplayer/mplayer.c:3215](src/game/mplayer/mplayer.c:3215) | `mpGetTrackNumAtSlotIndex(slotindex)` | full `g_MpTracks[]` | `mpIsTrackUnlocked` |
| M.3 | [src/game/mplayer/mplayer.c:3233](src/game/mplayer/mplayer.c:3233) | `mpGetNumUnlockedTracks` | (via M.1) | (transitive) |
| M.4 | [src/game/mplayer/mplayer.c:3377](src/game/mplayer/mplayer.c:3377) | `mpGetMusicForStage` (case A) | full `g_MpTracks[]` | match by `musicnum == ar.sound_id` (catalog hit, but reverse-lookup) |
| M.5 | [src/game/mplayer/mplayer.c:3429](src/game/mplayer/mplayer.c:3429) (and adjacent indexed reads at 3242/3249) | `mpGetTrackMusicNum` / `mpGetTrackName` | direct array index reads keyed by mp-slot | n/a (resolution) |

**Selector consumers**:
- Select Tunes ImGui [port/fast3d/pdgui_menu_mpsettings.cpp:739-816](port/fast3d/pdgui_menu_mpsettings.cpp:739) -- iterates `mpGetNumUnlockedTracks()` count + per-i `mpGetTrackName / mpGetTrackMusicNum / baseCatalogIdByMusicnum` lookup. **Half-migrated**: mod tracks come from `assetCatalogIterateByType(ASSET_AUDIO, ...)`, base tracks come through Layer A helpers + a separate catalog cross-walk via `baseCatalogIdByMusicnum`. After migration this collapses to a single iteration.
- Multi-track checkbox tile body (same file) reads via `mpIsMultiTrackSlotEnabled(slot)` -> `mpGetTrackNumAtSlotIndex(slot)` -- M.2.
- Stage music auto-resolution at match start -- M.4 (different consumer; not a UI selector but a runtime pick).

**Migration plan M**:
1. **M.0 -- catalog-side prep**: extend `ext.audio` with `s16 unlockstage` (mirrors `g_MpTracks[].unlockstage`). Populate at registration from `g_MpTracks[i].unlockstage`. Mod tracks default to -1 (= always unlocked) -- mod tracks have no campaign-stage dependency.
2. **M.0b -- new helper**: `assetCatalogIterateUnlockedMusic(asset_iter_fn fn, void *userdata)` in `assetcatalog_api.c`. Iterates `ASSET_AUDIO` filtered by `category == AUDIO_CAT_MUSIC` AND (the unlock predicate from `mpIsTrackUnlocked`: `unlockstage < 0 || unlockstage > SOLOSTAGEINDEX_SKEDARRUINS || any(g_GameFile.besttimes[unlockstage][i])`).
3. **M.1-M.3 -- slot helpers**: `mpGetNumUnlockedTracks`, `mpGetTrackSlotIndex`, `mpGetTrackNumAtSlotIndex`, `mpGetTrackMusicNum`, `mpGetTrackName` -- migrate to iterate the catalog. Stable ordering = catalog `mp_index` (which mirrors the original `g_MpTracks[]` index for base tracks); mod tracks carry mp_index = -1 and are absent from the slot sequence (Select Tunes presents them in a separate "Mods" tree already).
4. **M.4 -- musicnum -> duration lookup**: replace the `g_MpTracks[]` linear scan with a catalog lookup (use `assetCatalogResolveByNetHash` or iterate ASSET_AUDIO by musicnum). Catalog ID is the canonical key; the legacy musicnum-keyed lookup is a domain-confusion artefact.

**Wire format**: track selection in lobby (`audioModPlaylist`) is already catalog-ID-keyed. No wire change.

**Save format**: `g_BossFile.multipletracknums[]` is a bitmask keyed by `tracknum` (mp-index). The migration keeps the same catalog `mp_index` ordering, so the bitmask bits map to the same entries. No save migration.

### A.B -- Bot profiles (`g_BotProfiles[]`, 12 entries)

Layer A array: [src/game/mplayer/mplayer.c:2146](src/game/mplayer/mplayer.c:2146). 12 entries (Meat / Easy / Normal / Hard / Perfect / Dark / Multi-armed / SuperSim / TurtleSim / SpeedSim / ShieldSim / RocketSim).

**Catalog has NO ASSET_BOT_PROFILE registration today.** ASSET_BOT_VARIANT exists but is a different concept (NormalSim/DarkSim base type; AI-tuning preset for an existing bot type).

| # | File:line | Function / case | What it iterates | Filter today |
|---|---|---|---|---|
| B.1 | [src/game/mplayer/setup.c:3284-3379](src/game/mplayer/setup.c:3284) | `mpAddChangeSimulantMenuHandler` (multiple cases) | full `g_BotProfiles[]` | `challengeIsFeatureUnlocked(g_BotProfiles[i].requirefeature)` |
| B.2 | [src/game/mplayer/setup.c:3466-3506](src/game/mplayer/setup.c:3466) | `mpBotDifficultyMenuHandler` | `g_BotProfiles[0..BOTDIFF_DISABLED)` | unlock filter |

**Selector consumers**:
- ImGui Add Simulant flow [port/fast3d/pdgui_menu_botsetup.cpp:691](port/fast3d/pdgui_menu_botsetup.cpp:691) -- `plain_Set(menuhandlerMpAddSimulant, 0)` pushes `g_MpAddSimulantMenuDialog` whose body uses `mpAddChangeSimulantMenuHandler` -> B.1.
- ImGui Difficulty dropdown [port/fast3d/pdgui_menu_botsetup.cpp:957-985](port/fast3d/pdgui_menu_botsetup.cpp:957) -- `dd_GetOptionCount(mpBotDifficultyMenuHandler, ...)` + `dd_Set` round-trip -> B.2.

**Decision: B-domain registration**

Per Mike's directive "single source of truth", bot profiles SHOULD be in the catalog. Two paths:

- **Path B-A (preferred)**: Add new `ASSET_BOT_PROFILE` enum value + `ext.bot_profile` union member with `{ s32 type, s32 difficulty, u8 requirefeature, s32 mp_body, s32 ai_lists[12] }` etc. Register all 12 base profiles in `assetcatalog_base_extended.c`. Migrate B.1 / B.2.
- **Path B-B (defer)**: Bot profiles ride along with `g_MpSimulants` (preset bot list) in a future "preset bots" catalog. Skip this sweep.

**Recommendation**: Path B-A. The two iteration sites are exactly the same shape as W / S; adding the catalog type is a small additive change (one enum constant, one union member, registration table, two-line `s_entryRequireFeature` extension). Out-of-scope alternative is to be honest that "single source of truth" is partial. Per Mike's directive, do the work.

**Wire format**: bot profile is u8 `bc->profilenum` on the wire (server-authoritative). String IDs would require protocol bump. **Decision**: keep the integer mp_index as wire identity for now (matches the heads/bodies pattern where `mp_index` is on the catalog entry). Only the SELECTOR pool changes; wire identity unchanged. No protocol bump.

**Save format**: `g_BotConfigsArray[i].type` and `.difficulty` are 5/3-bit save fields. Selector migration only. No save migration.

### A.C -- CI Training character bios (`g_HeadsAndBodies[]`, 152 entries)

Layer A array: [src/game/modeldata/robot.c:64](src/game/modeldata/robot.c:64). 152 entries (mixed bodies and heads).

Catalog covers this fully (heads migration Pass 1 + Pass 2 SP fallback registers every bio-eligible entry as ASSET_BODY or ASSET_HEAD with `runtime_index = bodynum`). Pass 2 explicitly registers `base:sp_body_<i>` / `base:sp_head_<i>` for every g_HeadsAndBodies[] slot not already covered by g_MpBodies/g_MpHeads.

| # | File:line | Function | What it iterates | Filter today |
|---|---|---|---|---|
| C.1 | [src/game/training.c:2352](src/game/training.c:2352) | `ciGetNumUnlockedChrBios` | `g_HeadsAndBodies[0..151)` | `ciIsChrBioUnlocked(bodynum)` |
| C.2 | [src/game/training.c:2366](src/game/training.c:2366) | `ciGetChrBioBodynumBySlot(slot)` | `g_HeadsAndBodies[0..151)` | `ciIsChrBioUnlocked(bodynum)` |

**Selector consumer**: CI Training Character Bios menu (legacy menu + ImGui shim).

**Migration plan C**:
1. **No catalog-side change required** -- runtime_index is the bodynum which matches the bio table key.
2. Iterate ASSET_BODY + ASSET_HEAD entries from the catalog. For each, get `runtime_index`, call `ciIsChrBioUnlocked(runtime_index)`. The `runtime_index` directly substitutes for the static-array bodynum.
3. Skip the (one) sentinel entry the legacy `ARRAYCOUNT - 1` bound implicitly skips.

The unlock semantic (`ciIsChrBioUnlocked`) is bio-state, NOT feature-state. Keep the predicate at the call site rather than baking into the catalog. (Adding `ext.body.bio_unlocked` is wrong -- it would put runtime save state into a static catalog entry.)

---

## Section B -- Layer B coverage status

The catalog already covers all five domains as registrations, but extension work is needed on three:

| Domain | ASSET_TYPE | Registered today? | Needs ext field add? | New helper? |
|---|---|---|---|---|
| Weapons | `ASSET_WEAPON` | YES (47, base extended) | `requirefeature` on ext.weapon | extend existing `assetCatalogIterateUnlockedByType` |
| Scenarios | `ASSET_GAMEMODE` | YES (6, base extended) | `requirefeature` on ext.gamemode | extend existing `assetCatalogIterateUnlockedByType` |
| Music tracks | `ASSET_AUDIO` (cat=1) | YES (43, base extended) | `unlockstage` on ext.audio | new `assetCatalogIterateUnlockedMusic` (different unlock semantic) |
| Bot profiles | -- | NO | new `ASSET_BOT_PROFILE` + `ext.bot_profile` | extend `assetCatalogIterateUnlockedByType` |
| CI bios | `ASSET_BODY` + `ASSET_HEAD` | YES | n/a (call-site predicate) | n/a (use `assetCatalogIterateByType` with ciIsChrBioUnlocked filter) |

Catalog ID conformance per audit J.1 of heads/bodies/maps -- catalog IDs are stable; this sweep does NOT rename existing IDs. It only adds new fields and a new type constant.

---

## Section C -- Cross-cuts

### C.1 Wire format

- Weapons: per-slot `weapons[NUM_MPWEAPONSLOTS]` already catalog-ID strings (protocol v37). No bump.
- Scenarios: `g_MpSetup.scenario` already wired as catalog ID. No bump.
- Music tracks: `audioModPlaylist` already catalog-ID-keyed. No bump.
- Bot profiles: integer mp_index (wire field on bot config). No bump (selector-only migration).
- CI bios: not networked.

**No protocol bump required for any of the five domains.**

### C.2 Save format

- Weapons: 7-bit per-slot mp_index. Stable.
- Scenarios: 3-bit. Stable.
- Music tracks: bitmask keyed by tracknum (mp_index ordering). Catalog `mp_index` mirrors original ordering. Stable.
- Bot profiles: 5/3-bit type/difficulty fields. Selector-only migration. Stable.
- CI bios: best-time fields drive unlock; save format unchanged.

**No save format change required for any of the five domains.**

### C.3 Server build

- `assetCatalogRegisterBaseGame` not called server-side (no ROM data). Catalog entries do not exist on the server.
- All five domains' selectors are pd-target only (pdgui_*) or game-code that compiles into both targets but only fires from UI/match-start paths.
- `challengeIsFeatureUnlocked` returns true for everything when no save -- safe on server.
- New helpers (`assetCatalogIterateUnlockedMusic`) iterate zero entries server-side -- no failure mode.

### C.4 Bot configs

- `g_BotConfigsArray[].type` and `.difficulty` reference profile index. Selector migration writes the same integer, just resolves it via catalog iteration. Read-back paths (`mpCreateBotFromProfile`) unchanged.

### C.5 Default selections

- Weapons: default per-slot is per-set (W.6/W.7 picks the active set). Migration honours the same defaults via catalog `requirefeature` filter.
- Scenarios: default is `MPSCENARIO_COMBAT` (mode_id=0). Catalog has it; iteration finds it.
- Music tracks: default = first unlocked. Same outcome.
- Bot profiles: default is `BOTPROFILE_NORMAL` (Easy difficulty). Catalog will have the same entry.
- CI bios: default is slot 0. Migration preserves.

### C.6 The B-235 anti-pattern

B-235 (S452) cleared the heads picker domain confusion (mp_idx vs runtime_index). All five domains here are mp_idx-keyed (single index space per domain), so the B-235 risk does not apply. Document at the top of each migration commit.

---

## Section D -- Migration sequencing

Each step is a self-contained, build-verifiable commit. Bisectable on failure.

**Step 1 -- Catalog ext extensions and helper.**
- Add `u8 requirefeature` to `ext.weapon` (assetcatalog.h).
- Add `u8 requirefeature` to `ext.gamemode`.
- Add `s16 unlockstage` to `ext.audio`.
- Add `ASSET_BOT_PROFILE` to `asset_type_e` enum + `ext.bot_profile { s32 type; s32 difficulty; s32 mp_body; u8 requirefeature; s16 ai_lists[12]; }`.
- Extend `s_entryRequireFeature` switch to cover `ASSET_WEAPON`, `ASSET_GAMEMODE`, `ASSET_BOT_PROFILE`.
- Add `assetCatalogIterateUnlockedMusic` helper (different unlock semantic).
- Add pd-tests assertions: helper count == manual count for each type (5 cases).

**Step 2 -- Populate catalog at registration.**
- `assetcatalog_base_extended.c::registerBaseWeapons` -- set `e->ext.weapon.requirefeature = g_MpWeapons[i].unlockfeature`.
- `registerBaseGameModes` -- set `e->ext.gamemode.requirefeature = g_MpScenarioOverviews[i].requirefeature`. Also `team_based = .teamonly` (already set).
- `registerBaseMusicTracks` -- set `e->ext.audio.unlockstage = g_MpTracks[i].unlockstage`.
- New `registerBaseBotProfiles` -- 12 entries from `g_BotProfiles[]`. Catalog IDs `base:bot_meat`, `base:bot_easy`, ..., `base:bot_rocket_sim`.
- pd-tests assertions: catalog count per type matches static-array count.

**Step 3 -- Migrate weapon selector helpers (W.1-W.5).**
- `mpGetNumWeaponOptions` -> `assetCatalogGetUnlockedCountByType(ASSET_WEAPON)`.
- `mpGetWeaponLabel(weaponnum)` -> iterate ASSET_WEAPON unlocked, sort on mp_index, take N-th.
- `mpSetWeaponSlot` / `mpGetWeaponSlot` -> same iteration, round-trip on mp_index.
- `mpSetRandomWeapons` -> iterate ASSET_WEAPON unlocked, intersect with `g_MpWeaponSetRandomFilters[]` (still mp_idx keyed), produce filtered pool.
- pd-tests: assert all five helpers return same values pre- and post-migration for a fresh save (sentinel weapons all unlocked, all 47 reachable).

**Step 4 -- Migrate scenario selector handler (S.1-S.5).**
- `scenarioScenarioMenuHandler` per case migrates each ARRAYCOUNT walk to `assetCatalogIterateUnlockedByType(ASSET_GAMEMODE, ...)` with `team_based` predicate.
- pd-tests: count of unlocked scenarios in single-player mode matches expected (4 of 6 by default before progression).

**Step 5 -- Migrate music track selector helpers (M.1-M.4).**
- `mpGetTrackSlotIndex` / `mpGetTrackNumAtSlotIndex` / `mpGetNumUnlockedTracks` -> `assetCatalogIterateUnlockedMusic`.
- `mpGetTrackMusicNum / mpGetTrackName` -> resolve via catalog `mp_index` (sorted iteration).
- `mpFindMusicByMusicnum` (M.4) -> `assetCatalogResolve(...)` by sound_id -- need a tiny helper `catalogResolveAudioBySoundId` since `sound_id` is a pre-mapped integer key. Linear scan of ASSET_AUDIO is acceptable (only fires on stage transitions; ~43+mods rows).
- pd-tests: M.3 unlocked count = legacy unlocked count for fresh save (= every track whose unlockstage < 0).

**Step 6 -- Migrate bot profile selector handlers (B.1, B.2).**
- `mpAddChangeSimulantMenuHandler` migrates GETOPTIONCOUNT / GETOPTIONTEXT / SET / LISTITEMFOCUS / GETSELECTEDINDEX / GETGROUPSTARTINDEX cases.
- `mpBotDifficultyMenuHandler` migrates GETOPTIONCOUNT / GETOPTIONTEXT cases.
- pd-tests: count of unlocked bot profiles for fresh save = expected (depends on default unlock state; baseline expected to match legacy).

**Step 7 -- Migrate CI training character bios (C.1, C.2).**
- `ciGetNumUnlockedChrBios` / `ciGetChrBioBodynumBySlot` iterate ASSET_BODY + ASSET_HEAD via catalog, apply `ciIsChrBioUnlocked` predicate at call site.
- pd-tests: C.1 count for fresh save = expected (depends on bio-unlock baseline; baseline matches legacy).

**Step 8 -- pd-tests assertions consolidated.**
- One pd-tests case per migration step asserting "filtered count matches legacy filter count for a known unlock-state baseline".

**Verification per step**: `source devtools/build-env.sh && ninja -C Build pd pd-server pd-tests` (full link). pd-tests count grows by 7 per the per-step assertions.

---

## Section E -- New asset types and ext fields

| Change | File | Shape |
|---|---|---|
| `ASSET_BOT_PROFILE` enum | port/include/assetcatalog.h | new constant after `ASSET_LANG`, before `ASSET_TYPE_COUNT` |
| `ext.bot_profile` union member | port/include/assetcatalog.h | `{ s32 type; s32 difficulty; s32 mp_body; u8 requirefeature; s16 ai_lists[12]; char name[64]; }` |
| `ext.weapon.requirefeature` | port/include/assetcatalog.h | `u8` after dual_wieldable |
| `ext.gamemode.requirefeature` | port/include/assetcatalog.h | `u8` after team_based |
| `ext.audio.unlockstage` | port/include/assetcatalog.h | `s16` after duration_ms |
| `assetCatalogIterateUnlockedMusic` | port/include/assetcatalog.h + assetcatalog_api.c | wraps ASSET_AUDIO + cat=MUSIC + besttime predicate |
| `s_entryRequireFeature` switch | port/src/assetcatalog_api.c | add ASSET_WEAPON, ASSET_GAMEMODE, ASSET_BOT_PROFILE cases |
| `registerBaseBotProfiles` | port/src/assetcatalog_base_extended.c | new function + s_BaseBotProfiles[12] table |
| `assetCatalogRegisterBotProfile` | port/include/assetcatalog.h + assetcatalog.c | convenience wrapper |

---

## Section F -- Decisions for Mike (logged for audit; tactical defaults applied per `make-decisions-delegation`)

Per memory `make-decisions-delegation`, tactical defaults are applied here without blocking. Architectural calls Mike should review:

| # | Decision | Default applied | Rationale |
|---|---|---|---|
| F.1 | Add `ASSET_BOT_PROFILE` as a first-class catalog type? | YES (Path B-A) | Mike's directive is "single source of truth" -- partial coverage defeats the directive. The cost is small (one enum + one union member). |
| F.2 | `ASSET_BOT_PROFILE` catalog IDs | `base:bot_<slug>` matching s_BaseGameModes pattern (`base:bot_meat`, `base:bot_easy`, `base:bot_normal`, ...) | Conforms to existing slug discipline. Reserved for future mod bot profiles. |
| F.3 | `g_MpWeaponSets` migration | OUT OF SCOPE this sweep | Weapon SETS (presets-of-weapons) are not asset-shaped today; they would need a new `ASSET_WEAPON_SET` type with composition fields. Not blocking the directive (each weapon in the set is already in the catalog). |
| F.4 | `mpSelectRandomWeaponListHandler` (`g_MpWeaponSetRandomFilters[]` mutator) | OUT OF SCOPE this sweep | This sets a static-array filter mask; it's not a SELECTOR pool. The mask is keyed by mp_idx; migrating it would require redesigning the mask shape. Display labels for the checkbox list go through `mpGetWeaponLabel` (W.2), so after migration the labels and counts are catalog-correct even though the underlying state stays mp_idx-keyed. |
| F.5 | Catalog-renames of base IDs in scope | NO -- defer | Per heads I.1 / bodies J.1 / maps precedent, no proactive renames this session. |
| F.6 | Wire / cross-client unlock validation | STATUS QUO (host authority) | Matches heads I.4. Cosmetic pickers don't validate per-client unlocks. Bot profile / scenario / weapon picks are leader-side; followers honor. |
| F.7 | New `assetCatalogIterateUnlockedMusic` lives in `assetcatalog_api.c` alongside `assetCatalogIterateUnlockedByType` | YES | Existing iterators all live there. Document with the same block-comment style. |
| F.8 | Bundle the 8 migration steps into 4-6 commits | 6 commits (Steps 1+2 / 3 / 4 / 5 / 6 / 7+8) | Bisectable; mirrors heads/bodies/maps cadence. |
| F.9 | mpOptionsMenuDialog::TICK scan (S.6) | LEAVE AS-IS | Scans `g_MpScenarios[]` (the legacy menu-defs table, not g_MpScenarioOverviews) for a UI infrastructure lookup, not an asset selector. |

---

## Section G -- Audit summary

- Layer A iteration sites in scope: **22**
- New asset types: **1** (ASSET_BOT_PROFILE)
- New ext fields: **4** (ext.weapon.requirefeature, ext.gamemode.requirefeature, ext.audio.unlockstage, ext.bot_profile.*)
- New API helpers: **2** (assetCatalogIterateUnlockedMusic, assetCatalogRegisterBotProfile)
- New pd-tests cases: **7** (one per migration step assertion)
- Wire format bumps: **0**
- Save format migrations: **0**
- Migration commits: **6** (Steps 1+2 / 3 / 4 / 5 / 6 / 7+8)

**Stop conditions met**: none. Sweep is bounded, additive, no protocol or save bumps, comparable cost to heads/bodies/maps individually.

**Out of scope (justified)**:
- `g_MpWeaponSets` -- not an asset-shaped table (preset of weapons, not a weapon).
- `mpSelectRandomWeaponListHandler` filter-mask mutator -- not a selector pool, mp_idx-keyed state mutation.
- `mpOptionsMenuDialog::TICK` defensive scan -- UI infrastructure scan, not an asset selector.
- Catalog ID renames -- deferred per heads I.1 / bodies J.1 / maps precedents.

---

## Section I -- Deferred to follow-up session: full-pipeline data migration

This session's scope is SELECTORS ONLY. Layer A static arrays (`g_MpWeapons[]`, `g_HeadsAndBodies[]`, `g_MpArenas[]`, `g_MpScenarioOverviews[]`, `g_MpTracks[]`, `g_BotProfiles[]`) continue to hold the asset DATA (weapon damage, body filenums, scenario language IDs, track durations, profile AI lists). Catalog `ext.<type>` fields stay minimal -- this sweep only adds unlock-related fields (`requirefeature`, `unlockstage`).

**Mike's directive (2026-04-27 clarification)**: "We should simply extend the catalog struct to be inclusive of that data, and correct the references to read from the catalog (or request the data from a catalog manager, not sure which would be more standard protocol) instead. ... I want the FULL pipeline using the structure, not simply using it as a translation layer."

**Deferred follow-up scope** (separate session):

- **Inline vs Manager architectural decision**: extend `asset_entry_t` ext to carry full data inline (`weapon.damage / fire_rate / model_file / animations`), OR introduce a `catalogManager` service that owns typed data tables keyed by catalog ID.
- **Data field inventory per asset type** (full enumeration of every Layer A field that today drives game logic, not just selection / display). Tables to inventory: `g_MpWeapons` (struct mpweapon: weaponnum, unlockfeature, priammotype, priammoqty, secammotype, secammoqty, ...), `g_HeadsAndBodies` (struct headorbody: ismale, type, height, filenum, scale, animscale, modeldef, handfilenum, ...), `g_MpArenas` (stagenum, requirefeature, name), `g_MpScenarioOverviews` (shortname, name, requirefeature, teamonly), `g_MpTracks` (musicnum, duration, name, unlockstage), `g_BotProfiles` (type, difficulty, requirefeature, body, name, attack/defend ai_lists, etc.).
- **Consumer audit**: every `g_MpWeapons[i].damage`-style direct access. The selector sweep covered the iteration sites; the deferred sweep needs to cover every per-element field access.
- **Layer A retirement**: once all consumers route through catalog APIs, the static arrays become registration-only (similar to today's `s_BaseArenaNames[]` slug shadow), or are removed entirely if registration moves to data files.
- **Cross-cuts**: more invasive -- wire format (some weapon stats cross network for damage authority), save format (none of this is currently saved beyond selection state, but the deferred migration has to verify), bot configs (bot AI lists currently live in `g_BotProfiles[]` and must move to catalog or a manager), mod loader (today mod weapons / bodies override the catalog entry; full data migration means mods author data fields directly without static-array shadows).

**Why deferred**: the selector sweep is bounded (22 sites, 6 commits, no protocol/save bumps). The full-pipeline data migration touches damage tables, ammo tables, AI lists, model lookup paths, mod overrides; estimated 100+ sites across the engine, with real wire/save risk. Mike confirmed it lands as its own session.

**Cohort handoff**: the deferred session reads this Section I as the starting point. The Section A iteration tables stay valid (the selectors will already be catalog-driven by then); the deferred session focuses on data fields. The catalog already has minimal ext fields per type; the deferred session expands them or builds the manager.

---

## Section H -- Phase 2 commit plan (6 commits)

1. **Step 1+2**: catalog ext extensions + helper + registration table population (single commit so build never sees missing fields).
2. **Step 3**: weapon selector migration (W.1-W.5).
3. **Step 4**: scenario selector migration (S.1-S.5).
4. **Step 5**: music track selector migration (M.1-M.4).
5. **Step 6**: bot profile selector migration (B.1, B.2).
6. **Step 7+8**: CI training bio migration (C.1, C.2) + pd-tests consolidation.
