# [Catalog] - Universality Sweep (2026-05-01)

> **Date**: 2026-05-01
> **Worktree**: `claude/catalog-univ-sweep-0501`
> **Phase**: 1 of 2 (audit). Phase 2 = sequential migration commits per domain.
> **Mike's directive (activation message)**: "Every selector pool that picks from a content list MUST source from the catalog filtered by unlock state. Disabled items must NOT be in the catalog at all (dynamically constructed catalog excludes disabled content)."
> **Memory anchor**: `catalog-builds-all-selector-pools` -- selector pool = catalog INTERSECT unlock-state.
> **Constraint anchor**: `context/constraints.md` -- "Catalog registers ALL assets (unlock is a separate gameplay layer)" + "Random / Fiesta spawn-weapon pool sources from match-manifest" (S483).
> **Pillar**: `context/pillars/catalog.md`.
> **Working precedents**: prior heads/bodies/maps/scenarios/weapons/music/bot-profile/CI-bio selector migrations all in dev.

---

## Executive summary

The selector universality sweep checks every UI surface that builds a "pickable list" of asset content against the rule **selector pool = catalog INTERSECT unlock-state**. Most selectors already comply. The remaining surface area sorts into three buckets:

1. **One systemic gap (highest priority)**: `assetCatalogIterateByType` and `assetCatalogIterateUnlockedByType` do NOT filter by `enabled`. A disabled mod entry currently flows into selectors that use the unlocked iterator. Mike's directive resolves this two ways and they need to be reconciled (Section D.1). The fix is one helper line; the doc edit and behavior assertion are the rest.

2. **Two never-migrated legacy selectors**: `port/src/net/netmenu.c` co-op and join character body pickers iterate `g_MpBodies[]` directly, bypassing the catalog completely (Section C.1).

3. **Three "decision required" selectors** that intentionally use unfiltered `IterateByType`: Grid arena/scenario pickers (level editor context), `pdgui_menu_mainmenu.cpp` test-scenario combo (debug surface), and the CI training character bio iteration (uses `ciIsChrBioUnlocked` predicate, distinct unlock semantic). These need explicit decisions (Section C.3).

**Total migration surface: 2 hard migration sites + 1 systemic helper edit + 3 decision points + 1 docstring repair.** Below the 30-site threshold; one bundled session can ship the lot.

The Forge/Grid prop palette is its own static catalog (`port/src/forge/forge_core.c::s_catalog[]`) and is intentionally out of scope for this sweep (Section C.4) -- it would require a new ASSET_FORGE_PROP catalog type or extension of ASSET_PROP. Tracked as a separate future migration in Section F.

Stop conditions: none. No protocol bumps. No save-format changes. No catalog ID renames.

---

## Section A. Catalog API state (current)

### A.1 Selector iterators (the canonical helpers)

| Function | File:line | Behavior |
|---|---|---|
| `assetCatalogIterateByType(type, fn, userdata)` | [port/src/assetcatalog.c:763](port/src/assetcatalog.c:763) | Iterates all `occupied && type == X` entries. **Does NOT filter `enabled`.** |
| `assetCatalogIterateUnlockedByType(type, fn, userdata)` | [port/src/assetcatalog_api.c:751](port/src/assetcatalog_api.c:751) | Wraps `IterateByType`; runs `s_unlockFilterCb` which calls `s_entryRequireFeature(e)` and `challengeIsFeatureUnlocked`. **Does NOT filter `enabled` either.** |
| `assetCatalogIterateUnlockedMusic(fn, userdata)` | [port/src/assetcatalog_api.c:805](port/src/assetcatalog_api.c:805) | Music-track variant; gates on `g_GameFile.besttimes[unlockstage]` instead of feature flags. **Does NOT filter `enabled`.** |
| `assetCatalogGetCountByType(type)` | [port/src/assetcatalog.c (counts)](port/src/assetcatalog.c) | O(N) count. Ignores `enabled`. |
| `assetCatalogGetUnlockedCountByType(type)` | [port/src/assetcatalog_api.c:767](port/src/assetcatalog_api.c:767) | O(N) count of unlocked. Ignores `enabled`. |
| `assetCatalogGetUnlockedMusicCount()` | [port/src/assetcatalog_api.c](port/src/assetcatalog_api.c) | O(N) count of unlocked music tracks. Ignores `enabled`. |

### A.2 `s_entryRequireFeature` switch coverage (current)

[port/src/assetcatalog_api.c:723-734](port/src/assetcatalog_api.c:723):

```c
case ASSET_ARENA:       return e->ext.arena.requirefeature;
case ASSET_BODY:        return e->ext.body.requirefeature;
case ASSET_HEAD:        return e->ext.head.requirefeature;
case ASSET_WEAPON:      return e->ext.weapon.requirefeature;
case ASSET_GAMEMODE:    return e->ext.gamemode.requirefeature;
case ASSET_BOT_PROFILE: return e->ext.bot_profile.requirefeature;
default:                return 0;
```

All six selector-eligible types are covered. The docstring at [port/include/assetcatalog.h:732-740](port/include/assetcatalog.h:732) says "ASSET_ARENA / BODY / HEAD only" -- stale, needs sync.

### A.3 `enabled` field plumbing (current)

| Touch point | Behavior |
|---|---|
| `assetCatalogRegister` ([port/src/assetcatalog.c:460](port/src/assetcatalog.c:460)) | Sets `entry->enabled = 1` on new registration. |
| `assetCatalogResolve(id)` ([port/src/assetcatalog.c:697](port/src/assetcatalog.c:697)) | Returns NULL if `!entry->enabled`. (Resolve respects the flag.) |
| `assetCatalogIterateByType` ([port/src/assetcatalog.c:771](port/src/assetcatalog.c:771)) | Checks `occupied && type == X` only. **Ignores `enabled`.** |
| `assetCatalogSetEnabled(id, enabled)` ([port/src/assetcatalog.c:848](port/src/assetcatalog.c:848)) | Sets `entry->enabled = 0` or `1`. |
| `pdgui_menu_modmgr.cpp:243` | Mod manager UI commits `s_Entries[i].enabled` to catalog via `assetCatalogSetEnabled`. |
| `port/src/modmgr.c:2332` | `.modstate` persisted disable list applies `assetCatalogSetEnabled(line, 0)`. |

**Inconsistency**: resolve-by-ID respects `enabled`; iterate-by-type does not. A mod entry disabled from the Mod Manager UI cannot be resolved by ID (good) but still appears in any selector built from `IterateByType` / `IterateUnlockedByType` (bad). See Section D.1.

---

## Section B. Selector inventory (already catalog-driven, fully compliant)

These selectors source the catalog with the unlock filter. No migration work.

| Selector | File:line | Iterator used | Type |
|---|---|---|---|
| MP Setup arena picker | [port/fast3d/pdgui_menu_mpsetup.cpp:606](port/fast3d/pdgui_menu_mpsetup.cpp:606) | `IterateUnlockedByType(ASSET_ARENA)` | arena |
| MP Setup scenario picker (S.1-S.5) | [src/game/mplayer/scenarios.c:407-444](src/game/mplayer/scenarios.c:407) | `IterateUnlockedByType(ASSET_GAMEMODE)` | gamemode |
| MP Setup weapon slot picker (W.1-W.5) | [src/game/mplayer/mplayer.c:1322,1355,1373,1559](src/game/mplayer/mplayer.c:1322) | `IterateUnlockedByType(ASSET_WEAPON)` | weapon |
| MP music track picker (M.1-M.4) | [src/game/mplayer/mplayer.c:3294,3304](src/game/mplayer/mplayer.c:3294) | `IterateUnlockedMusic` | audio (cat=MUSIC) |
| Bot profile picker (B.1) | [src/game/mplayer/setup.c:3368-3429](src/game/mplayer/setup.c:3368) | `IterateUnlockedByType(ASSET_BOT_PROFILE)` | bot_profile |
| Bot difficulty picker (B.2) | [src/game/mplayer/setup.c:3572,3579](src/game/mplayer/setup.c:3572) | `IterateUnlockedByType(ASSET_BOT_PROFILE)` | bot_profile |
| Random AI bot head pick | [src/game/mplayer/mplayer.c:3670](src/game/mplayer/mplayer.c:3670) | `IterateUnlockedByType(ASSET_HEAD)` | head |
| Agent Creator head carousel | [port/fast3d/pdgui_menu_agentcreate.cpp:211](port/fast3d/pdgui_menu_agentcreate.cpp:211) | `IterateUnlockedByType(ASSET_HEAD)` | head |
| Agent Creator body carousel | [port/fast3d/pdgui_menu_agentcreate.cpp:278](port/fast3d/pdgui_menu_agentcreate.cpp:278) | `IterateUnlockedByType(ASSET_BODY)` | body |
| Bot Setup head dropdown | [port/fast3d/pdgui_menu_botsetup.cpp:530](port/fast3d/pdgui_menu_botsetup.cpp:530) | `IterateUnlockedByType(ASSET_HEAD)` | head |
| Bot Setup body dropdown | [port/fast3d/pdgui_menu_botsetup.cpp:583](port/fast3d/pdgui_menu_botsetup.cpp:583) | `IterateUnlockedByType(ASSET_BODY)` | body |
| Random meta arena picker | [src/game/mplayer/setup.c:308](src/game/mplayer/setup.c:308) | `IterateUnlockedByType(ASSET_ARENA)` + category mask | arena |
| Room screen body picker | [port/fast3d/pdgui_menu_room.cpp:2404](port/fast3d/pdgui_menu_room.cpp:2404) | `IterateUnlockedByType(ASSET_BODY)` | body |
| Random spawn weapon (host) | [port/src/net/matchsetup.c:762](port/src/net/matchsetup.c:762) | Iterates match manifest (per S483 invariant; manifest itself built from catalog at host) | weapon |

**14 selectors fully compliant with the directive.**

---

## Section C. Migration targets (gaps to close)

### C.1 HARD migration -- legacy character pickers in `netmenu.c`

Two selectors iterate `g_MpBodies[]` directly with no catalog touch. They predate the catalog migration and were missed by the prior bodies sweep.

| # | Site | Function | What it does |
|---|---|---|---|
| C.1.1 | [port/src/net/netmenu.c:184-220](port/src/net/netmenu.c:184) | `menuhandlerCoopCharacter` | Co-op character body dropdown for player 0. `GETOPTIONCOUNT = ARRAYCOUNT(g_MpBodies) + 1`; `GETOPTIONTEXT` indexes `g_MpBodies` directly via `mpGetBodyName(idx-1)`. `SET` writes through `mpchrSetBodyByIndex` + `catalogGetBodyDefaultMpHeadIdx` (correct on commit; only the iteration shape is wrong). |
| C.1.2 | [port/src/net/netmenu.c:413-445](port/src/net/netmenu.c:413) | `menuhandlerJoinCharacter` | Same shape, used by client-join lobby. |

**Migration shape**: replace the `ARRAYCOUNT(g_MpBodies) + 1` count and the integer-indexed `mpGetBodyName(idx-1)` with `assetCatalogIterateUnlockedByType(ASSET_BODY, ...)` plus a "Default (Joanna)" sentinel row prepended. Keep the integer-index commit for back-compat (matches the Agent Creator body migration shape).

**Effect**: locked SP-only / mod-disabled bodies stop appearing in co-op and client-join character pickers. Adds mod bodies automatically.

**Risk**: low. The handlers only feed an integer index forward; the commit path already has correct catalog plumbing.

### C.2 SYSTEMIC -- `enabled` filter is missing from selector iterators

This is the universal gap behind Mike's directive "Disabled items must NOT be in the catalog at all (dynamically constructed catalog excludes disabled content)."

**Today**: a mod component disabled in Mod Manager (`assetCatalogSetEnabled(id, 0)`) stays in the catalog with `enabled = 0`. `assetCatalogResolve(id)` returns NULL (good); `assetCatalogIterateByType / IterateUnlockedByType` continue to emit the disabled entry (bad). Every selector listed in Section B will show disabled mod content.

**Two reconciliation paths** (Section D.1 makes the call):

- **Path A: filter in the iterator**. Add `if (!entry->enabled) continue;` to both iterator inner loops. Selectors automatically see only enabled entries. `s_BotPresetCount`, `s_GridArenaCount`, `s_MpArenaList`, etc. all respect the toggle for free. Cross-references still resolve (resolve-by-ID returns NULL on disabled, which is the existing semantic).

- **Path B: deregister on disable**. `assetCatalogSetEnabled(id, 0)` removes the entry from the catalog entirely (frees pool slot, removes from hash table). Future re-enable requires re-registration from source. Cross-references break (intentional: a disabled entry must not resolve).

**Recommendation**: Path A. Reasons: (1) preserves cross-reference resolution for "I'm a mod that depends on base:falcon2; the user disabled base:falcon2; my mod doesn't crash when looking up the dependency, it just gets a NULL it can handle"; (2) symmetric with `assetCatalogResolve` which already filters; (3) one-line edit; (4) reversible without re-registration walking entire mod tree. The Pass-5 `disable_base:` mechanism in the rom-extraction audit also assumes Path A semantics.

### C.3 DECISION-REQUIRED -- selectors using unfiltered `IterateByType`

Three legitimate candidates plus modder UIs (which are correctly using the unfiltered iterator).

| # | Site | Why it's unfiltered today | Decision needed |
|---|---|---|---|
| C.3.1 | [port/fast3d/pdgui_menu_mainmenu.cpp:4841](port/fast3d/pdgui_menu_mainmenu.cpp:4841) `gridArenaListBuild` | Grid (level editor) lists every arena, not just unlocked, because the editor is a developer / map-author surface. | Keep as-is, OR adopt unlock filter to mirror the gameplay arena picker. **Recommend keep**: Forge / Grid is forging on top of arenas, so the user wants to see and target locked / mod arenas as canvases. |
| C.3.2 | [port/fast3d/pdgui_menu_mainmenu.cpp:4910](port/fast3d/pdgui_menu_mainmenu.cpp:4910) `gridScenarioListBuild` | Grid scenario picker. Same argument. | Same as C.3.1: keep as-is. |
| C.3.3 | [port/fast3d/pdgui_menu_mainmenu.cpp:3938](port/fast3d/pdgui_menu_mainmenu.cpp:3938) test-scenario arena combo | Debug menu. Doesn't gate by unlock so dev can pick any arena. | Keep unfiltered (debug surface). |
| C.3.4 | [src/game/training.c:2406,2407,2418,2420](src/game/training.c:2406) CI training bios | Uses `ciIsChrBioUnlocked` (best-time-based), distinct from `challengeIsFeatureUnlocked`. | Already correct; bio unlock is a different gameplay layer. Document the distinction in the docstring. |
| C.3.5 | [src/game/spawnpool.c:2041](src/game/spawnpool.c:2041) `smokeOfflineBuildArena` | Offline smoke test for spawn pool generator. Wants every arena. | Keep unfiltered (test infra). |
| C.3.6 | [port/fast3d/pdgui_menu_audiomod.cpp:219](port/fast3d/pdgui_menu_audiomod.cpp:219) Audio Mod authoring | Modder UI; needs entire catalog. | Keep unfiltered (modder surface). |
| C.3.7 | [port/fast3d/pdgui_menu_room.cpp:799](port/fast3d/pdgui_menu_room.cpp:799) Bot variant cache | Bot variants are not unlock-gated. | Keep unfiltered. |
| C.3.8 | [port/fast3d/pdgui_menu_room.cpp:427](port/fast3d/pdgui_menu_room.cpp:427) `catalogArenaCollect` (room arena picker) | Already unlock-filtered upstream by S593e flow; the iterator-by-type form here is for the count probe. **Verify**: re-read this site to confirm. | Reconfirm during Phase 2. |
| C.3.9 | [port/fast3d/pdgui_menu_modmgr.cpp:282](port/fast3d/pdgui_menu_modmgr.cpp:282), [pdgui_menu_moddinghub.cpp:335,729,961](port/fast3d/pdgui_menu_moddinghub.cpp:335) | Mod Manager / Modding Hub UIs. Need full catalog. | Keep unfiltered. |

After Path A from Section C.2 lands, all of these correctly skip `enabled = 0` entries. The decision points above are about whether to add the unlock filter (`requirefeature` gate) on top.

### C.4 OUT OF SCOPE for this sweep

- **Forge / Grid prop palette** ([port/src/forge/forge_core.c:60-220](port/src/forge/forge_core.c:60)). 160+ entries in static `s_catalog[]`. Not asset-catalog-driven. Migration would either extend ASSET_PROP to carry Forge-specific fields or introduce ASSET_FORGE_PROP. Tracked as a future Forge-data-pipeline task in Section F.
- **Solo mission selector** ([port/fast3d/pdgui_menu_solomission.cpp:731](port/fast3d/pdgui_menu_solomission.cpp:731)). Iterates `NUM_SOLOSTAGES` and `g_SoloStages[]`; `isStageDifficultyUnlocked` is the gate. ASSET_MISSION enum exists but no base-game registration is populated. Tracked as future work; the existing flow is correct under "selector pool = unlocked". Migration would require populating ASSET_MISSION at startup mirroring `g_SoloStages[]` and is large enough to be its own session.
- **Cheats picker** ([src/game/cheats.c](src/game/cheats.c)). Cheats are game-state toggles, not asset content. Out of scope.

---

## Section D. Cross-cuts

### D.1 The `enabled` reconciliation (CRITICAL DECISION)

Mike's directive in the activation message: "Disabled items must NOT be in the catalog at all (dynamically constructed catalog excludes disabled content)."

The previous sweep documented per `context/audits/rom-extraction-audit-2026-04-30.md` Section 3.16.11 (Pass 5 mechanism): "Mods declare `disable_base: [list_of_catalog_ids]`; when the mod is enabled, those base IDs are filtered from selectors. ... The catalog never rewrites a base entry; it just hides it from UI selectors when a filter says so."

**Reconciliation**: both directives can coexist under Path A. Interpretation:

- "Disabled items shouldn't be in the catalog" reads as "should not be in the catalog *as far as selectors are concerned*" -- the entry stays present so cross-reference resolution doesn't break, but the selector iterator skips it (functionally absent).
- "Dynamically constructed catalog" reads as "the catalog is rebuilt on mod toggle, not statically baked, so toggling mods reshapes the selector pool live" -- which Path A already supports because the iterator re-evaluates `enabled` every call.

**Path A change site**: add `enabled` filter to `assetCatalogIterateByType` (one line, [port/src/assetcatalog.c:771](port/src/assetcatalog.c:771)). `assetCatalogIterateUnlockedByType` and the music variant inherit the filter automatically because they wrap the base iterator.

**Side effect to flag**: modder UIs (Mod Manager, Modding Hub, Audio Mod authoring) currently rely on the unfiltered iterator to LIST disabled entries (so the user can re-enable them). Path A breaks this. Resolution: those UIs need a separate `assetCatalogIterateByTypeIncludingDisabled(type, fn, userdata)` API (or a `flags` parameter on the existing iterator). Two new functions or one parameter.

### D.2 Wire format

No wire change. Catalog ID strings are wire-stable; selectors emit catalog IDs at commit (already the case). Disabling an entry on the host doesn't change its ID.

### D.3 Save format

No save change. `g_BotConfigsArray[].body_id / head_id` are catalog ID strings; if a saved ID becomes unresolvable post-disable, existing fallback resolution (`mpchrSetBodyByIndex(0)` etc.) applies.

### D.4 Server build

Server links the catalog skeleton but does not invoke `assetCatalogRegisterBaseGame`. Iterator helpers are no-ops server-side (zero entries). No server-side risk.

### D.5 Bot configs

Bot picker, variant cache, and difficulty picker all live in client paths. No server-side selector affected.

### D.6 Default selections

Each selector in Section B has a defined fallback when the iterator returns zero entries (e.g. `g_MpSetup.scenario = MPSCENARIO_COMBAT` default; `g_MpSetup.weapons[0] = MPWEAPON_NONE`). Path A doesn't change this; if all entries become disabled, the fallback fires the same way it does when none are unlocked.

### D.7 The Mod Loader integration

Mod Manager's apply flow (`pdgui_menu_modmgr.cpp:243`) already calls `assetCatalogSetEnabled` per entry. Path A surfaces the toggle in selectors immediately on the next IterateByType call. No additional wiring needed.

---

## Section E. Phase 2 commit plan

Per `feedback_auto_merge_by_default`: each unit ships independently to dev. Per `phase5-sequential` memory: serialize merges, no parallel pillars on the same files.

**Commit 1** -- Path A: `enabled` filter in iterators + docstring sync.
- File: [port/src/assetcatalog.c](port/src/assetcatalog.c) and [port/src/assetcatalog_api.c](port/src/assetcatalog_api.c).
- Change: `assetCatalogIterateByType` skips `!entry->enabled`. The unlock variant inherits.
- Add: `assetCatalogIterateByTypeIncludingDisabled` API for modder UIs that legitimately need disabled entries.
- Doc: update [port/include/assetcatalog.h:732-740](port/include/assetcatalog.h:732) docstring to reflect WEAPON / GAMEMODE / BOT_PROFILE coverage and the `enabled` filter.
- Migrate: `pdgui_menu_modmgr.cpp:282`, `pdgui_menu_moddinghub.cpp:335,729,961`, `pdgui_menu_audiomod.cpp:219` to call the new "including disabled" API.
- Tests: add a single pd-tests case asserting "disabled entry does not appear in IterateByType".
- Build verify: `devtools\build-session.ps1 -Session uniA -Target all`.

**Commit 2** -- C.1.1 + C.1.2: migrate `netmenu.c` co-op + join character pickers.
- Files: [port/src/net/netmenu.c](port/src/net/netmenu.c) lines 184-220 and 413-445.
- Replace integer-indexed `g_MpBodies` count + `mpGetBodyName(idx-1)` with catalog iteration (`assetCatalogIterateUnlockedByType(ASSET_BODY, ...)`).
- Build verify: `devtools\build-session.ps1 -Session uniB -Target all`.

**Commit 3** (optional) -- audit verification + close docs.
- Final post-migration grep verifies no Layer A iteration remains in selector code paths outside the documented exceptions (Section C.4).
- Update `context/pillars/catalog.md` if any active-invariant text needs to match.

Each commit auto-merges to dev independently. The Gate 3 migrations (heads, bodies, arenas data move) are unblocked after Commit 1 lands because the iterator semantics are stabilised.

---

## Section F. Future work tracked (not in this sweep)

- **Forge prop palette migration**: move `port/src/forge/forge_core.c::s_catalog[]` into asset catalog. Either extend ASSET_PROP with Forge-specific fields (category / cost / name / tags) or introduce ASSET_FORGE_PROP. Enables mod-authored Forge props.
- **Solo mission selector migration**: populate ASSET_MISSION entries at startup from `g_SoloStages[]`; migrate `pdgui_menu_solomission.cpp::renderMissionSelect` to iterate the catalog filtered by `isStageDifficultyUnlocked`. Enables mod-authored campaign missions appearing in the Solo Mission picker.
- **Pass 5 `disable_base:` mod metadata**: `.pdmod` schema extension for total-conversion mods to flag base catalog IDs as disabled when the mod is enabled. Implementation: walk `disable_base:` array on mod enable, apply `assetCatalogSetEnabled(id, 0)`; restore on mod disable. Unblocked by Commit 1 above (since the selector iterator now respects the flag).

---

## Section G. New B-IDs surfaced by this audit

**B-303** (this audit): selector iterators do not filter `enabled` field. Mod entries disabled via Mod Manager continue to appear in agent creator, room screen, bot setup, MP setup pickers. Proposed fix shipped in Commit 1.

---

## Section H. Methodology gates

- File:line evidence for every claim above.
- Possibility framing: alternatives surfaced for the `enabled` reconciliation (Section D.1: Path A vs Path B); Path A recommended with rationale.
- No em-dashes.
- Build verification gates each commit via `devtools\build-session.ps1` (per `feedback_build_verify` and the standing-rule from the activation message).
- pd-tests case in Commit 1 enforces the `enabled` invariant.
- Hierarchical log channels: no new channels for this sweep (touches only iteration, no new diag surfaces).
