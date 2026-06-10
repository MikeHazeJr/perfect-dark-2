# Migration / Utilization Measurement (2026-06-10)

> Answer to Mike's "Are we at 100% migration and utilization?" — measured in source
> by a 4-agent audit (workflow wpmhh5y6g), every claim file:line-grounded.
> **Verdict: NO.** Migration (public source + catalog identity) is high (~90% by
> family). Utilization (normal play actually consuming public source with loud-fail
> discipline, and authored data actually driving behavior) is substantially lower.

## The four measured surfaces

| Surface | Measured | One-line meaning |
|---------|----------|-----------------|
| Per-family public-source utilization (normal play) | ~90% | 12/13 families load public-source-first; FONT is 0% utilized |
| ROM/extraction-cache fallback closure | ~35% closed | Only mesh/model, body/head, weapon-data, lang refuse loudly; ~55% silently fall back; ~10% fully open |
| Integer-bridge catalog ownership | ~65% | 7/13 bridges closed (weapon, mpweapon, body, head, model, lang + music nonstandard); soundnum/stagenum/animnum/texnum have NO allocator |
| Authored-IR / graph-data consumption | ~35% | ~50 of ~165 weapon-graph IR fields consumed; whole families dead; .pdeffect 0%; bindings 0%; ALL of it behind Debug.WeaponGraphRuntime (default OFF) |

## Load-bearing facts (each verified at file:line in the workflow output)

1. **FONT is the only family with zero in-game catalog routing.** `textLoadFont`
   (`src/game/game_1531a0.c:228-379`, dmaExec at :322) reads native font segments
   unconditionally; `.pdfont` archives are emitted and walker-bound but nothing
   consumes them. Not even debug-enforceable (no ASSET_FONT check in src/).
2. **Six families are parity-by-fallback, not closed**: texture, animation,
   sfx, music/song, scenario (bg/pads/setup/tiles), and the bondgun held-gun model
   bytes consult public source first but silently (LOG_WARNING at most) fall back
   to extraction-cache reads in normal play. Loud refusal exists only under
   `Debug.AssetSourceOnlyType` — which enforces ONE family at a time
   (`asset_source_debug.c:15-23,135-138`), so full enforcement is impossible even
   in debug. Per the program's own rule (post-extraction ROM fallback = asset-chain
   failure) these are NOT at 100%.
3. **Most base textures have no public source at all**: there is no base texture
   image emitter (no `romextract_pdtex`), so the ~3503 base texture rows read the
   `texturesdata` segment cache every time (`texdecompress.c:2354`).
4. **Net-new content is impossible in four families**: no allocator exists for
   soundnum, stagenum, animnum, or texnum — custom sounds get `sound_id=-1` and are
   silently unplayable; `stageTableAppend` has zero callers; `g_NumAnimations` is
   fixed at the ROM table size. Custom content cannot enter those families at
   runtime today.
5. **The entire weapon-graph behavior layer defaults OFF**:
   `Debug.WeaponGraphRuntime` (`weapon_graph_runtime.c:148-152`, default 0) gates
   every gameplay consumer — production-default behavior is 100% legacy. The
   B-911/B-912/B-914 work is correct and verified but dormant until cutover.
6. **~115 of ~165 authored IR fields are dead data**: whole authored families with
   zero production consumers — homing detail params, impact refs (explosion/spark),
   trails, wall-hugger, sticky-attach, transition-to-entity (its sole accessor has
   zero callers), remote-detonatable, nbomb-storm, sticky-device, owner-cleanup,
   interaction, fly-by-wire params beyond the flag, weapon settings/variables,
   material_slots/grip_sockets/presentation (no engine parser), `.pdeffect` node
   kinds (no compiler/executor at all).
7. **The 11 meta families** (hud/effect/material/theme/gamemode/botprofile/skin/
   prop/vehicle/mission/character) are bound + validated at activation, but
   `assetRuntimeFind*` has zero game-code consumers — gameplay reads native data.
8. **Residual fully-open raw readers**: MP challenge mpstrings
   (`challenge.c:438-466`), firingrange segment (`training.c:422,1014`), boot
   images (`src/lib/main.c:542-544`), crash font (`crash.c:402`).
9. **Closed-and-loud (the good list)**: mesh/model (`modeldefRefuseRomSource`
   unconditional fatal), body/head, weapon data (pool is sole source), lang banks
   (unconditional `ASSET.CHAIN` fatal), the five custom-slot allocator families
   (weapon/mpweapon/body/head/model) with `CATALOG.*.CUSTOM_SLOT_FAIL` discipline,
   and boot ordering.

## What "getting to 100%" would require (ranked by leverage)

1. Per-family normal-play loud-fail cutover for the six parity-by-fallback
   families (each needs its per-stage proof first — the gate-1/B-801 live program).
2. FONT runtime consumer (`.pdfont` -> `textLoadFont` replacement).
3. Base texture public-source emitter (the largest unmined family by count).
4. Allocators (or direct catalog-ID consumption) for soundnum/stagenum/animnum/
   texnum so net-new content can exist in those families.
5. Weapon-graph runtime cutover (retire `Debug.WeaponGraphRuntime`) + consuming
   the dead IR families per the cutover plan.
6. Meta-family consumers (`assetRuntimeFind*` into gameplay) + `.pdeffect` runtime.
7. Aggregate normal-play fallback telemetry ("N assets fell back to native this
   stage") so regressions are visible without source-only mode.

Full per-row evidence: workflow `wpmhh5y6g` output (4 surveys, 328 tool uses).
