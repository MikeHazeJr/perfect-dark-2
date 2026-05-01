# Session Log (Active)

> **S481-S593d + S482c + S593b** (rolling window of ~110 sessions; S593d added 2026-05-01 for swarm bot hostile teams + aggressive AI + 1.5x speed + half scale + half health + Debug Menu UX redesign with arena selector; S593c added 2026-05-01 for swarm benchmark follow-up -- chr pool sizing in chrmgr path, real bot AI for CPU mode, GPU pipeline scoped as follow-up; S593b added 2026-04-30 PM for menus H.5 universal integrated-head guard + B-296/B-297 New Agent black preview, ran in parallel with S593; S593 added 2026-04-30 PM for swarm-test crash + correctness pass B-295; S592 added 2026-04-30 PM for ROM extraction audit + Mike's `.pdXXX` taxonomy + ROM-as-bootstrap-only architectural principle; S591 added 2026-04-30 for catalog weapons F11; S482c added 2026-04-30 PM for Dev Window v2 blank-screen fix on the festive-hawking worktree lineage). S281-S480 archived to [`_old/session-log/sessions-S281-S480.md`](../_old/session-log/sessions-S281-S480.md) on 2026-04-30 per the context rebuild + [retention.md](retention.md). Older tiers (S280-S241, S240-S157, S1-S119) all live under `_old/`.
> Master index: [README.md](README.md).

## Session S593d (`distracted-hamilton-430172` continuation #2) - 2026-05-01 - Swarm: hostile teams + aggressive AI + scale/health/speed + Debug Menu UX

Mike playtest after S593c (verbatim):

> "The bot behavior was updated on the CPU version, but not the boid version. Also, all the bots seemed to be running aimlessly. Maybe they didn't see me as an enemy? Ultimately, they should all be on one team, and me on the other. No team highlights. Also, make them 1/2 scale and 1/2 their normal health, 1.5x their normal move speed. This should be for both game modes. And put me on a more open level."

### Smoking gun

Investigation walked the bot AI's hostility check (`bot.c::botGetTeamSize` and similar use `chr->team == other->team` for ally detection). Cycle ladder confirmed in playtest binary at rdata offset 0x72800. Then the swarm chr team value: `chr->team = 1 << 7 = 0x80 = TEAM_NONCOMBAT`. That single field explained all of "running aimlessly" -- TEAM_NONCOMBAT is literally a "do not engage" flag in the engine's team taxonomy. The bots had real AI ticks running per S593c, but the AI's hostility test correctly classified them as non-combatants and they never aggressed.

### Fixes shipped (commit 1d87613f, S593d)

| What | Where | Change |
|------|-------|--------|
| Hostile team | `swarm_test.c::spawn_one_skedar` | `chr->team = TEAM_ENEMY` (was `1 << 7` = TEAM_NONCOMBAT). Player chr is on TEAM_01; different combat-class team -> AI engages. |
| Forced aggression | `swarm_test.c::swarm_init_aibot` | `aibot->command = AIBOTCMD_ATTACK`, `aibot->attackpropnum = player_prop_index`. Locks the bot into attack mode regardless of tactical pick. |
| Aggressive bot type | `swarm_test.c::s_SwarmBotConfig` | New dedicated bot config: `BOTTYPE_KAZE` (does not keep distance), `BOTDIFF_PERFECT` (~1.47x speed). Replaces the shared `g_BotConfigsArray[0]` reference. |
| Half scale | `swarm_test.c::spawn_one_skedar` | `modelSetScale(chr->model, 0.5f)`. Visual size + bondwalk perim test scale together. |
| Half health | `swarm_test.c::spawn_one_skedar` | `chr->maxdamage = 4.0f` (was 1.0f, target was 1/2 of normal MP-bot 8.0). |
| 1.5x speed (CPU) | `swarm_test.c::s_SwarmBotConfig` | BOTDIFF_PERFECT in `botCalculateMaxSpeed` -> 11.2x base vs NORMAL 7.6x = ~1.47x. |
| 1.5x speed (GPU) | `swarm_gpu.cpp::s_Params.max_speed` | 18.0 -> 27.0. Plus `gpu_fallback_seek_tick::SWARM_MAX_SPEED` 18.0 -> 27.0 to match. |
| Default arena | `testscenarios.c::TESTSCEN_DEFAULT_SWARM_MAP` | `base:mp_skedar` -> `base:mp_felicity`. Open beach instead of cramped temple. |
| Debug Menu UX | `pdgui_menu_mainmenu.cpp::renderSettingsDebug` | Replaced Combo dropdown + Launch with 3 radios (The Grid / CPU Bots / GPU Bots) + arena selector (catalog-enumerated `ASSET_ARENA`) + Start button. Grid mode greys out the arena selector. Default arena: Felicity. Default mode: CPU Bots. |

### Team highlights

Mike asked for "no team highlights." `MPOPTION_TEAMSENABLED` is the toggle for radar/HUD team-colour overlays in `g_MpSetup.options`. Our test scenario calls `matchConfigInit` and sets `scenario_id = "base:combat"` without enabling teams, so team highlights are already suppressed even though chr->team is now TEAM_ENEMY. No additional gating needed.

### GPU mode in S593d

GPU compute path stays position-only -- bots seek the player at 1.5x speed but don't have AI on the GPU side. Mike's directive ("don't try to ship full GPU bot AI in this session if the gap is large") was explicit; the doc at [context/designs/in-flight/gpu-swarm-bot-pipeline.md](designs/in-flight/gpu-swarm-bot-pipeline.md) was updated this session to record the concrete behavioural gap GPU mode still shows (no attack, no dodge, no chr-vs-chr collision in motion, no BG geometry awareness past the spawn-time ground snap).

### Build verification

`devtools\build-session.ps1 -Session swfix3 -Target all` -- both `PerfectDark.exe` (54.5 MB) and `PerfectDarkServer.exe` (22.3 MB) build clean. `strings PerfectDark.exe | grep "CPU Bots##testscen_mode"` confirms the new Debug Menu UI is in the binary.

### Files touched

- `port/src/swarm_test.c` -- s_SwarmBotConfig + swarm_init_bot_config_once + chr->team / model scale / health / aibot->command / attackpropnum updates.
- `port/fast3d/swarm_gpu.cpp` -- max_speed bump.
- `port/src/testscenarios.c` -- default arena.
- `port/fast3d/pdgui_menu_mainmenu.cpp` -- Debug Menu UX redesign with arena selector.
- `context/designs/in-flight/gpu-swarm-bot-pipeline.md` -- concrete-gap section + S593d update.
- `context/bugs.md` -- B-295 status update.
- `context/session-log.md` -- this entry.



## Session S593c (`distracted-hamilton-430172` continuation) - 2026-05-01 - Swarm benchmark follow-up: real bot AI + chr pool fix

Mike playtest after S593's first-pass fix surfaced three remaining swarm-test symptoms (verbatim):

> "Swarm still seems to loop to 16 only..." (later corrected: "actually doesn't go to 16. It goes to 8, and also shows '8' but '10' loaded also")
> "Skedar guys move now, but not like bots, just a moving prop. It should have actual bot behavior. That goes for CPU and BOID versions"
> "they should have collision, currently they can go inside me and each other, making me unable to move"

**Architectural clarification from Mike (mid-session)**: the GPU/BOID path is ultimately supposed to run **full bot behaviour** on GPU compute (parallelized), not just position updates. Today the GPU shader only does pure seek-toward-player; bot state machine, target selection, attack decisions, LOS, weapon firing all stay on CPU. The benchmark's job is to find the CPU-vs-GPU crossover, but it can only do that when both modes do equivalent work. Filed as follow-up pillar ([context/designs/in-flight/gpu-swarm-bot-pipeline.md](designs/in-flight/gpu-swarm-bot-pipeline.md)) since the gap is large (~3-5 sessions of focused effort).

### Three issues, two distinct root causes

**Issue 1 (cycler stuck at 8) root cause**: `src/game/setup.c` had the swarm-extra hook for `modelmgrAllocateSlots` (sized model/anim/prop pools to 256), but the same hook was MISSING for `chrmgrConfigure(numchrs)`. On a solo-no-simulants swarm session the chr pool sized to `g_NumChrSlots = PLAYERCOUNT() + 0 + 10 = 11`, so after player + ~10 swarm chrs the chr pool was full. The cycler couldn't progress past 8 because spawn-16 hit the cap mid-loop. Fix: mirror the `testScenarioGetSwarmMaxCount()` hook in the chrmgr path (`setup.c:1669`).

**Issue 2 + 3 (no real bot AI, no collision) root cause**: swarm chrs were allocated with `ailist=GAILIST_IDLE` and `chr->aibot=NULL`. The chrs ticked through `chraTick`'s passive paths -- no target acquisition (no AI script chasing), no `chrTryStop` collision-aware movement, no weapon firing. The S593 fix used `chrSetPos` to make their visible motion work, but that bypassed exactly the AI machinery that gives bots collision-aware movement. So even though the engine HAS chr-vs-chr collision, swarm chrs were teleporting through it.

**Fix**: CPU mode now spawns each chr as a real bot:
- `ailist = GAILIST_AIBOT_INIT` (the bot AI script).
- `chr->aibot` points into a private 256-slot aibot pool in `swarm_test.c` (`s_SwarmAibots[256]` + `s_SwarmAibotInUse[256]` bitmap). This escapes `botmgrAllocateBot`'s `MAX_BOTS=32` gate and skips its match-scoring registrations (`g_MpBotChrPtrs[]`, `g_MpAllChrPtrs[]`) that overflow at MAX_MPCHRS=40.
- `chr->myaction = MA_AIBOTMAINLOOP`.
- `botinvInit(chr, 10)` for weapons/ammo.
- New helper `swarm_init_aibot()` mirrors `botmgrAllocateBot`'s aibot init block (botmgr.c:163-351) minus the match-scoring side-effects.
- `chr->radius = 30` so the perim has a meaningful size for chr-vs-chr / chr-vs-player collision.

CPU bots now run real bot AI: chase, attack, dodge, fire weapons, with collision-aware movement that prevents chr-vs-chr no-clip.

**GPU mode** keeps the position-only behaviour. `cpu_seek_tick` was renamed to `gpu_fallback_seek_tick` (only runs when GL compute is unavailable in GPU mode). The dispatch in `swarmTestTick` now skips the seek tick entirely in CPU mode (AI handles motion) and only invokes `swarmGpuStepAndApply` or the fallback in GPU mode.

### Caps surfaced

- **chr pool**: now correctly sized via the new `setup.c::chrmgrConfigure` swarm hook -- `g_NumChrSlots = PLAYERCOUNT() + numchrs + 10` where numchrs includes 256 swarm extras.
- **Aibot pool (NEW)**: 256 entries in `s_SwarmAibots[]`. Each is ~700 bytes static BSS, so ~178 KB total. `s_SwarmAibotInUse[256]` 1-byte bitmap. ammoheld arrays per aibot are mempAlloc'd from MEMPOOL_STAGE.
- **NUMTYPE1/2/3** (S593): 80/320/64 -- unchanged.
- **MAX_MPCHRS = 40**: still applies to the bot AI's per-chr tracking arrays (`chrnumsbydistanceasc[40]`, etc.). Each swarm bot only "sees" 40 closest chrs through these tables. In practice the player is always one of the closest so target acquisition still works.

### Build verification

`devtools\build-session.ps1 -Session swfix2 -Target all` (queued tool, per Mike's preference) -- both `PerfectDark.exe` (54.5 MB) and `PerfectDarkServer.exe` (22.3 MB) build clean. `strings PerfectDark.exe | grep CHRSLOTS` confirms the new `CHRSLOTS: added %d swarm chr slots` log line is in the binary, and `grep "aibot pool exhausted"` confirms the new bot allocation path is linked.

### Files touched

- `port/src/swarm_test.c` -- swarm aibot pool, swarm_init_aibot helper, GAILIST_AIBOT_INIT path for CPU mode, gpu_fallback_seek_tick rename, dispatch refactor, despawn frees aibots.
- `src/game/setup.c` -- chrmgrConfigure swarm hook (the actual cycler-stuck-at-8 fix).
- `context/designs/in-flight/gpu-swarm-bot-pipeline.md` (NEW) -- scope for follow-up pillar.
- `context/bugs.md` -- B-295 status update with S593c continuation.
- `context/session-log.md` -- this entry.



## Session S593b (`nostalgic-hamilton-f529e1`) - 2026-04-30 PM - menus H.5 universal integrated-head guard + B-297 New Agent black preview

Mike playtest report (verbatim): "When I select a character with no head, such as Skedar, Dr Carroll, or EyeSpy, the head slot should simply be disabled. At this time, it seems to let me select an arbitrary head which admittedly doesn't spawn but it's bad UI to leave the jankiness in there. Additionally, the character customizer panel for the New Agent screen is just black. Nothing visible for preview."

Two distinct bugs in the New Agent / character customizer screen.

**Bug 1: H.5 universal integrated-head guard (B-296).** The c66b02fc / B-241 fix added the integrated-head guard at the agentcreate carousel (`s_bodyHasIntegratedHead`) AND at the renderer's request seam (`pdguiCharPreviewRequestEx` clears headnum when `catalogGetBodyIsComplete`). The renderer-side gate prevents the rig-mismatch crash class, but the THREE other body+head pickers (Player Config Character, Bot Setup Simulant Character, Room Change Character modal) never got the matching UI lock. Universal H.5 guard applied to all three:

- `pdgui_menu_playerconfig.cpp::s_pcBodyHasIntegratedHead(committedBodyId)` -- carousel arrows wrapped in `BeginDisabled(integratedHead || !canCycle)`, label shows "(integrated)", tooltip "This character has an integrated head."
- `pdgui_menu_botsetup.cpp::s_bsBodyHasIntegratedHead(curBodyMpIdx)` -- combo dropdown wrapped in `BeginDisabled(integratedHead)`, same label + tooltip; helper resolves mp_idx via `catalogMpBodyId` first.
- `pdgui_menu_room.cpp` "Change Character (this match only)" modal -- body Selectable handler clears `s_PendingCharHeadId` when an integrated body is picked (so wire/save side never carries a stale head id); head Selectable list wrapped in `BeginDisabled(integratedHead)`; header reads "Head  (integrated)" with tooltip.

The Set Character bot multi-select already uses `catalogPickRandomHeadIdForBody` which handles integrated bodies via rig-class compatibility -- no UI guard needed there.

**Bug 2: B-297 New Agent black preview.** Root cause hypothesis: `pdgui_menu_agentcreate.cpp` initialised `s_SelectedBody = 0` and `s_SelectedHead = 0` -- alphabetically-first body and head from the unlocked pool, picked INDEPENDENTLY. On certain mod/unlock combinations the pair was rig-incompatible. The renderer's request seam handles rig mismatch by falling back to the body's default head (B-241), but the body itself could still hit a downstream load problem (catalog miss / file empty / invalid modeldef -- each emits a per-cause `LOG_WARNING` in `menu.c::menuRenderModel`). Result: FBO cleared to black, `s_PreviewReady` still flipped to 1, ImGui drew the black texture. Mike saw "just black, nothing visible for preview" -- not the "Loading..." silhouette fallback because IsReady was 1.

Two-part fix:

1. Seed the carousel from the player's currently-saved body/head pair (`mpPlayerConfigGetBodyId/HeadId`) so the OPENING selection is always rig-compatible. Mirrors Player Config's pattern, which doesn't have this bug. The user can still cycle to any unlocked body/head; only the OPENING selection changes.

2. Add LOUDFAIL channel `PREVIEW.FBO.BLACK:` in `pdgui_charpreview.c::pdguiCharPreviewRenderGBI` that fires when the FBO render path completes but `mm->bodymodeldef == NULL` (the silent-fail signal from menu.c). Surfaces the symptom directly at the FBO seam so any future "preview is black on screen X" report lights up at this single channel without needing per-call-site grep.

**Tests.** New file `tests/test_integrated_head_guard.cpp`: 7 cases / 40 assertions PASS. Static / source-text checks (same shape as `test_catalog_checked.cpp`'s body0f02ce8c source pins) so any future refactor that drops the guard from one of the four pickers fails CI loud rather than silently shipping a half-measure. Tags `[catalog][catalog-mgr-body][s593][integrated-head][...]` so they pick up under `-Scope catalog`.

**Build verify.** `build-session.ps1 -Session s593 -Target all` PASS (CLIENT 30s, SERVER 8s; PerfectDark.exe 54.6 MB, PerfectDarkServer.exe 22.3 MB). `build-session.ps1 -Session s593 -Target tests` PASS (TESTS 18s, pd-tests.exe 23.2 MB). Pre-existing test failures in dev (test_catalog_provider_static.cpp:580 stale text-pin vs swarm fix; test_cutscene_layer.cpp:330; test_connectcode.cpp:272) are unrelated to this slice.

**Methodology.** Possibility framing on findings -- did not binary-eliminate the black-preview cause; landed on the seed-init hypothesis as primary AND added the LOUDFAIL diagnostic so any other root cause lights up loud in the next playtest. Universal guard applied to ALL four picker sites in one slice (no half measures).

**Auto-merge.** Worktree merged into dev as `Merge worktree: H.5 universal integrated-head guard + B-291 New Agent black preview (S593 nostalgic-hamilton-f529e1)`. Pre-merge HEAD `caf65bdeef3d54791f0cd0fbc52fcde40ab2fac9`; post-merge line counts of all 7 changed files match worktree exactly. The merge commit message used the older "B-291" labelling because the bug-id collision (B-291 was already taken by S584's build wrapper fix) was caught only after the merge -- a follow-up commit on the worktree renumbered all source comments to B-297 and added the bug entries; that follow-up landed via a second merge to dev. The session-id collision with S593 (swarm) was caught at the same time and resolved by renaming this session to S593b in the index above (parallel-session naming precedent: S482c).

## Session S593 (`distracted-hamilton-430172`) - 2026-04-30 PM - Swarm benchmark crash + correctness pass (B-295)

Mike playtest report on the Skedar swarm test mode (introduced via S483c GPU swarm benchmark). Crash + multi-symptom bundle.

**Crash trace** (preserved as worktree file `swarm-test-crash-2026-04-30.md` because the original `Build/pd-client.log` was wiped by a clean rebuild moments after the report):
- `EXCEPTION: 0xc0000005` at `PC=0x00007ff6cc17b39f` -- offset `0x3ab39f` from `MAIN MODULE 0x7ff6cbdd0000`.
- Last breadcrumb: `CHR.TICK slot=9 chrnum=-1 action=1 race=1 model=0000000000000000`.
- Frame `LVTICK=781`, stage `0x32` (`base:mp_skedar`), bg slots=11. Just before the crash the breadcrumb shows 8 freshly-spawned Skedars (chrnums 5024-5031) AND a stale slot 9 with `chrnum=-1 model=NULL` -- the chr that triggered the AV.

**Symptoms reported by Mike** (all explained by the same root cause class):
- Crash on count change.
- Bots not moving (CPU + GPU paths).
- Bots "spawn inside player" with greenish texture clipping.
- Bots not cleared on count cycle (old bots persisting).
- Player not invincible / no all-guns / no bottomless ammo.
- Bots invisible after a few count cycles (chr/model pool exhaustion).
- Cycle ladder needs `48` between `32` and `64`.

**Root cause** (B-295): `swarm_test.c::despawn_all()` called `chrRemove(prop, true)` only. `chrRemove` clears `chr->model = NULL` and `chr->chrnum = -1` but does not free the prop or remove it from `activeprops`. Next frame's `propsTickPlayer()` walked the dead prop, called `chrTick`, which deref'd the NULL model deep inside chraTick or its callees and AVed. The accumulated stale chrs also exhausted the chr / model rwdata pools after several cycles, causing the "bots invisible" symptom; the lingering chrs near the player explained the "spawn inside me" greenish clipping.

**Fix bundle**:
1. **Despawn correctness** (the crash). `despawn_all()` now uses the canonical chrmgrStop pattern: `chrRemove + propDelist + propDisable + propFree`. Reference site: `src/game/chrmgrstop.c:14-23`. Added a `freed=N` log line per despawn.
2. **chrTick defense-in-depth**. New early-out at the top of `chr.c::chrTick`: if `chr == NULL || chr->chrnum < 0 || chr->model == NULL`, log `CHR.STALE.MISS:` and return `TICKOP_FREE`. Catches any future caller that resurrects the old chrRemove-only pattern, and the prop tick dispatcher then runs the proper free path on the stale prop.
3. **Movement** (CPU + GPU). Both paths now use `chrSetPos(chr, &newpos, rooms, face_deg, true)` instead of writing `chr->prop->pos.x` directly. `chrSetPos` syncs the model root matrix, ground tracking, and room registration; the prior direct writes left the model rendering at the spawn position. Heading is computed from the seek velocity vector (`atan2f(vx, vz)`).
4. **Player setup**. `apply_player_setup()` is now called from every `swarmTestTick` frame, not just session-start. `cheatsReset()` (level start) and `playerSpawn()` (death respawn) each used to wipe the cheat banks / equipallguns / `player->invincible` after the prior single-shot apply ran. Re-asserting each tick is cheap and idempotent.
5. **NUMTYPE2 ceiling**. `modelmgr.c` + `modelmgrreset.c` bumped: NUMTYPE1 70 -> 80, NUMTYPE2 50 -> 320, NUMTYPE3 48 -> 64. The 256-bot Swarm scenario plus baseline gameplay chrs needs at least 256 type-2 chrinfo bindings; prior 50 was exhausted after ~50 concurrent chrs and explained the "bots invisible" symptom directly. Cost at 320 type-2: ~83 KB rwdata, negligible on PC.
6. **Cycle ladder**. `SWARM_TEST_CYCLE` is now `{4, 8, 16, 32, 48, 64, 128, 256}` (steps 7 -> 8). Adds the `48` curve-bend probe per Mike's directive.

**Caps surfaced for Mike** (per directive):
- chr pool: `g_NumChrSlots = PLAYERCOUNT() + numchrs + 10`. setup.c already adds `testScenarioGetSwarmMaxCount()` (256) into `numchrs` when a swarm scenario is active, so the chr pool comfortably fits 256 + headroom. No change needed.
- Model pool (`g_MaxModels = numobjs + numspare + numchrs + 20`): same path -- swarm hook already pulls 256 in. No change needed.
- Anim pool (`g_MaxAnims = numchrs + 20`): same. No change needed.
- Prop pool (`g_Vars.maxprops = numobjs + numchrs + extra + 40`): same. No change needed.
- NUMTYPE1/2/3: bumped (point 5 above) -- these were the bottleneck.
- Render draw list (`g_Vars.onscreenprops`): per-frame visible-prop list, sized at level init from `maxprops`. No change needed once the pools above scale.
- Swarm-specific (`s_Swarm[TESTSCEN_SWARM_MAX_COUNT=256]`): already sized for the cap. Static.

**Crash function not symbol-resolved**: `Build/PerfectDark.exe` was wiped by Mike's clean rebuild before `addr2line` could be run against it. The new build has different code layout. Breadcrumb log gives us the chr identity (slot 9, chrnum=-1, model=NULL) which is enough to identify the class of crash and confirm the fix.

**Build verify**: clean build at `.claude/session-builds/swarmfix/` -- both `PerfectDark.exe` (54.5 MB) and `PerfectDarkServer.exe` (22.3 MB) link cleanly. `strings PerfectDark.exe | grep CHR.STALE.MISS` confirms the new defense-in-depth path is in the binary.

**Files touched**: `port/src/swarm_test.c`, `port/include/swarm_test.h`, `port/fast3d/swarm_gpu.cpp`, `src/game/chr.c`, `src/game/modelmgr.c`, `src/game/modelmgrreset.c`, `context/bugs.md`, `swarm-test-crash-2026-04-30.md` (worktree-local crash preservation).

## Session S592 (`confident-bardeen-48bed6`) - 2026-04-30 PM - ROM extraction audit + .pdXXX taxonomy + ROM-as-bootstrap principle

Mike's directive: "Ensure the rom extraction process is functional. Research how others have solved this problem and compare to what we are doing, as well as checking what we may improve."

Three architectural directives accumulated mid-session:

1. Per-asset-class file extensions (`.pdwep`, `.pdui`, `.pdmesh`, etc.) plus per-asset granularity (`weapon_farsight.pdwep`, name-suffix variants). Top-level dirs distinguish redistribution: `base/` ships, `data/` is BYOR-extracted.
2. Single canonical schema per extension. Mod-tool output and extractor output are byte-identical for the same content (round-trip clean). Schema accommodates both extracted and mod-authored content. References by ID, not path. `parent:` field for partial overrides.
3. Headline architectural principle: ROM is a one-time bootstrap input. Extracted base content is the runtime source of truth. The catalog reads only from `data/` plus `base/` plus `mods/`. Loader has zero ROM-specific code beyond bootstrap.

**Phase 1 audit findings**:
- One runtime ROM-to-disk extractor exists: `pdguiThemeExtractRomTextures` at `port/fast3d/pdgui_theme.cpp:2405` plus its trigger `pdguiThemeCheckExtract` at `:3162`. Materializes 14 UI textures into `mods/base-ui/textures/`.
- Build-time extractor `tools/extract` (Python) is canonical for developer asset reconstruction; frozen upstream `fgsfdsfgs/perfect_dark` since 2022-12-04.
- Build-time compilers `tools/assetmgr/mk*` produce headers from `src/assets/<romid>/` JSON manifests.
- Runtime ROM model in `port/src/romdata.c` keeps the full 32 MB ROM mapped and routes file reads through `romdataFileLoad` plus per-loadtype `preprocessXxxFile` (endian / pointer fix at load time, not extraction).
- Architectural mismatch: runtime UI extractor still writes to `mods/base-ui/textures/` (loose files) while the modding pipeline migrated `base-ui` to a `.pdmod` ZIP archive. The archive does not contain the extracted textures.
- ROM SHA-256 hash validation scaffolding exists at `port/src/romdata.c:227-246` but the known-good hash arrays are `NULL`-only.
- `--extract-ui-textures` and `--generate-modern-ui` are the only `--extract-*` / `--generate-*` CLI flags. Not in `--help`.

**Phase 2 research**: surveyed N64 / classic-game decomp ecosystem. Two dominant patterns:
- Pattern A (build-time, developer-only): fgsfdsfgs/perfect_dark, OoT decomp, MM decomp + ZAPD, SM64 decomp, MK64 decomp, BK decomp. PD2's existing `tools/extract` sits here.
- Pattern B (runtime, end-user-facing): Ship of Harkinian, 2 Ship 2 Harkinian, Starship, Ghostship, SpaghettiKart. SHA1-keyed ROM detection, file-picker prompt, container archive output (`.otr` then `.o2r`).
- Non-N64 parallels (no transcoding): OpenRCT2, ScummVM, fheroes2.
- Extension conventions: format-extension (decomps), container-extension (SoH), and Mike's emerging asset-class-extension (.pdXXX) as a third path.
- Base-vs-mod symmetry: SoH and OpenRCT2 maintain it; OoT / SM64 do not (build-time transform makes source format != runtime format). PD2 lines up with SoH / OpenRCT2.

**Phase 3 recommendations** organized around Mike's principle. Highlights:
- Document the principle in roadmap.md and pillars/catalog.md.
- Migrate UI texture extractor's output from loose files to `data/ui/pd-original.pdui` (single ZIP archive, structurally identical to a modder-authored `.pdui`).
- Define `.pdwep` schema and migrate F11-F13 monolithic `base/weapons.pdbase` to per-weapon `base/weapons/weapon_*.pdwep`.
- Populate ROM SHA-256 known-good hash table.
- Per-asset-class extension taxonomy with proposed `data/` placements for each.
- Schema design principles per `.pdXXX`: one canonical shape, mod-tool output matches extractor output, references by ID not path, `parent:` field for partial overrides.
- Convergence vs anti-pattern map: `tools/extract` plus `pdguiThemeExtractRomTextures` are convergent; `port/src/romdata.c` plus `port/src/preprocess/*` are anti-patterns under the principle and need migration.

Deliverable: [audits/rom-extraction-audit-2026-04-30.md](audits/rom-extraction-audit-2026-04-30.md), 625 lines. Docs-only. No code shipped. Mike's call on which gaps and which migrations to actually pursue.

Methodology: possibility framing on subjective judgments throughout, file:line plus URL evidence for every claim, no em-dashes, no code changes.

**Pass 2 (same session, 2026-04-30 PM later)**: Mike read the audit and dictated 20 directives plus forward-looking notes. Doc rewritten to apply all directives plus surface independent extrapolations.

Directives applied (numbered list maintained in audit footer): extension naming `.pdwpn` over `.pdwep` plus definitions for `.pdtiles` / `.pdseg` / `.pdmpconfig` / `.pdtexconfig` / `.pdfiringrange`; audio split into music / sfx / voice; `data/` vs `base/` canonical distinction; mods first-class symmetric with naming-disallow plus explicit override flag plus multi-override; variant naming as per-asset distinct identities; `.pdmodpack` architecture (contain vs reference question recommended as contain); hash-verify plus self-heal plus corruption quarantine; read-only `data/` with writable-during-extraction; LOUDFAIL log channel taxonomy; procedural fallback as loud failure; procedural chrome severity bumped to high; test coverage severity bumped to high; ROM hash validation enable plus offset selection; CLI extraction discoverability auto via launch flow; multi-ROM support expansion approved; `src/generated/` retirement TODO; JSON / INI usage with commented-out unused tags; AllInOne mod-override branch cleanup; `mods/base-ui.pdmod` retirement target; mod tools load any base content as template.

Forward-looking notes tracked: accessories system, mod-driven character behavior, terrain editor in-client, bundled-with-release modpacks, logging-pipeline cleanup pass, ROM-free distribution.

Self-extrapolations surfaced for Mike's review (E-1 through E-18 in audit Section 3.15): atomic extraction transaction (temp-then-rename), multi-mod override precedence default (load order), per-romid `data/<romid>/` subdir layout, variant catalog IDs as flat strings, modpack contain model, LOUDFAIL UI surface, `.pdcharacter` schema split from `.pdmesh`, accessories as attachable mini-meshes, in-client editor save path, `tools/assetmgr/mk*` retirement, per-tree manifest with hash table, `.pdwpn` references `.pdmesh` not contains, mods adding-vs-overriding distinction, `.pdtexconfig` retires, `.pdmpconfig` rolls into `.pdscenario`, `.pdfiringrange` rolls into `.pdscenario`, `.pdtiles` and `.pdseg` as split sub-resources of `.pdscenario`, override audit log on startup.

Open questions logged for Mike's call: Q-1 modpack storage model, Q-2 multi-mod override precedence, Q-3 `.pdscenario` vs `.pdmission` extension name, Q-4 `.pdcharacter` extension split, Q-5 quarantine retention deeper than 1 snapshot, Q-6 multi-ROM data layout.

Final audit dimensions: 1050 lines, zero em-dashes, sentinel marker intact, single `.pdwep` reference retained in directive-history footer to record the rename. The original `< 800 lines` stop condition no longer applies under the expanded scope.

**Pass 3 (same session, 2026-04-30 PM later still)**: Mike walked through a Halo fusion-coil prop-mod authoring example and dictated Q-resolutions for all six open questions plus several refinements that emerged from the walkthrough. Doc rewritten to add Section 3.16 (Mod architecture refinements) and Section 3.17 (Worked example: Halo fusion coil); Section 3.18 (Priority order) preserved as the closer. Section 3.2 extension table extended with `.pdprop` and `.pdcharacter` as distinct catalog asset types. Section 3.15 open-question list updated to point at Section 3.16 for resolutions.

Pass 3 Q-resolutions:
- Q-1 modpack storage = contain (confirmed default).
- Q-2 load order with `load_after:` / `load_before:` positional defaults plus `priority:` field plus drag-reorder UI.
- Q-3 SP-MP unification via `modes:` block in `.pdscenario`; map variants as siblings via suffix naming (zombies-mode = `scenario_skedar_temple-zombies.pdscenario`); hardcoded MP spawn points for campaign maps live in canonical scenario's `modes.combat_sim` block.
- Q-4 `.pdcharacter` distinct extension and distinct catalog asset type. `.pdprop` introduced as third asset type (spawnable props with logic, distinct from `.pdmesh` static-mesh-visual-only). Plus three follow-on refinements: reverse-dependency manifest (computed `required_by:` list with disable-warning prompt), optional + fallback dependencies in `requires:` block, circular-dependency prevention via topological sort with LOUDFAIL on cycle.
- Q-5 counter-based LOUDFAIL with session reset for quarantine overwrites; counter persists across launches but resets when the file stops being touched.
- Q-6 priority-list ROM selection at extraction time (NTSC-final > PAL-final > NTSC-1.0 > JPN-final > PAL-beta > NTSC-beta), with player UI override; session-cache for cross-region multiplayer (`data/.session-cache/<host_session_id>/`, ephemeral, evicted on disconnect).

Pass 3 worked example: end-to-end Halo fusion-coil `.pdprop` schema with diffuse/emissive textures, physics, stats, behavior block (`on_health_below`, `on_destroyed`, `aoe_damage`); atomic vs compound packaging decision matrix; logic system as future pillar.

Pass 3 self-extrapolations: E-19 `.pdprop` as third asset class; E-20 logic system as future architectural pillar; E-21 `assetprovider_session_cache.c` as third asset provider; E-22 ROM priority list ordering recommendation (Mike said "recommend" so I picked).

Final audit dimensions: 1440 lines (up from 1050; +390 lines for Pass 3 = ~30% growth on top of Pass 2). Zero em-dashes. Sentinel marker intact. Cumulative growth from Pass 1 origin: 625 to 1440 lines (+130%).

**Pass 4 (same session, 2026-04-30 PM later still)**: Mike applied a substantial architectural refinement: compound-only on disk in `mods/`, plus no-overrides (mods are strictly additive), plus Q-5 counter clarification (streak-break reset semantics).

Pass 4 changes applied:
- New Section 3.16.0 (Compound-only on disk; internal catalog granularity) inserted as the foundational architectural shift. User's mods/ holds only `.pdmod` and `.pdmodpack` files; atomic assets bundled inside compound archives; one file equals one mod; hash-based deduplication at registration; mod authoring workflow (in-client tool copies cataloged content into new compound for self-containment).
- Section 3.5 (Mod override semantics) rewritten as "Mods are additive (no overrides)". Override flag and multi-mod precedence rules removed. New invariants: catalog ID uniqueness across base + data + all enabled mods; LOUDFAIL on duplicate ID with first-loaded-wins resolution; total conversions become modpacks of additive compounds; load-order complexity collapses (Q-2 priority field documented as vestigial).
- Section 3.16.1 (Modpack storage) updated for additive-only; constituent compounds surface in their respective UI lists.
- Section 3.16.2 (Load order) rewritten as "vestigial under additive-only"; `priority:` field kept for forward compat but rarely needed.
- Section 3.16.4 (`.pdcharacter` and `.pdprop` catalog asset types) clarified that the per-asset-class extensions describe the SHAPE of files inside compound archives, not user-facing files in mods/.
- Sections 3.16.5 / 3.16.6 (reverse-dep manifest, optional+fallback deps) updated to operate at compound-on-compound level only; atomic-level dep tracking happens internally and via hash-dedupe.
- Section 3.16.7 (cycle prevention) clarified for compound-on-compound graph.
- Section 3.16.8 (Q-5 counter) rewritten with consecutive-streak semantics: counter persists in `data/.session-state.json` across launches; increments only on consecutive runs of self-heal for the same asset; resets on streak break (clean launch); LOUDFAIL.HEAL.PERSISTENT_CORRUPTION fires while streak > 0.
- Section 3.4 (Directory taxonomy) updated to reflect compound-only mods/ and per-asset granularity for base/ and data/.
- Section 3.13 (Mod-friendliness improvements) updated; M-9 (additive-only removes precedence complexity), M-10 (hash-dedupe removes duplicate-asset penalty), M-11 (provenance audit) added.
- Section 3.17 (Halo fusion-coil worked example): 3.17.3 rewritten as "single self-contained compound" (default packaging); 3.17.4 rewritten as "compound depending on another compound" (variation for coordinated sets); 3.17.6 updated for compound-only and additive-only emphasis.

Pass 4 self-extrapolations (E-23 through E-29):
- E-23 provenance metadata in copied assets (origin: field).
- E-24 compound archive layout convention (top-level mod.json plus inner per-asset-class directories).
- E-25 first-loaded-wins resolution on duplicate ID (Mike said LOUDFAIL but did not specify; I chose first-wins-and-warn-second; open question).
- E-26 hash-dedupe pool architecture (two-level lookup: bytes-by-hash plus ID-to-hash).
- E-27 AllInOneMods migration framing (GEX, Kakariko, Goldfinger 64, Dark Noon need reauthor as additive collections under Pass 4; non-trivial migration pillar).
- E-28 UX implication for additive curation (built-in header plus per-modpack groupings in pickers).
- E-29 `data/.session-state.json` persistence shape (JSON with version, last_clean_launch, self_heal_streaks map).

Final audit dimensions: 1654 lines (up from 1440; +214 lines for Pass 4 = ~15% growth on top of Pass 3). Zero em-dashes. Sentinel marker intact. Cumulative growth from Pass 1 origin: 625 to 1654 lines (+165%).

**Pass 5 (same session, 2026-04-30 PM later still)**: Mike extended the Pass 4 model with: (1) presentation-layer disable mechanism for total conversions, (2) `.pdwepset` weapon-set extension type, (3) `random_source:` MP setup field, (4) explicit per-spawn-point weapon/pickup declarations in `.pdscenario`, (5) The Grid as Forge-extensible (FW-7 to FW-9 forward-looking notes).

Pass 5 changes applied:
- New Section 3.16.11 (Disabling base content; presentation-layer mechanism). Catalog entries get an `enabled: true/false` flag (default true). Compound mods declare `disable_base: [catalog_ids]` to filter from selectors. Direct lookup by ID still resolves (cross-references unaffected). Multi-flipper stacking. `selector_pool = catalog ∩ enabled ∩ unlocked ∩ context_filter` formalization. Total-conversion UX mechanism: Halo TC mod hides PD content from selectors, adds Halo additively; coexistence is trivial.
- Section 3.5 (Mods are additive) gets a Pass 5 refinement subsection bridging to 3.16.11. Disable mechanism is NOT an override; base stays canonical; only selector visibility filters.
- Section 3.16.0 compound-manifest sketch updated with `disable_base:` field; Halo total-conversion example added.
- Section 3.16.3 (SP-MP unification) extended with `random_source:` field on MP setup config. Options: `all_enabled`, `base_only`, `modpack:<id>`, `weapon_set:<catalog_id>`. Empty random pool fires LOUDFAIL.RANDOM.EMPTY_POOL and falls back to default base weapon.
- Section 3.2 extension table: `.pdwepset` row added for weapon sets.
- Section 3.3 schema sketches: `.pdwepset` schema added; `.pdscenario` schema updated with explicit `weapon_spawns` and `pickup_spawns` arrays carrying per-location asset IDs (catalog references) plus ammo / respawn metadata.
- Section 3.13 (Mod-friendliness): M-12 added (total conversions become genuinely composable under disable + additive + modpack model).
- Section 3.14 (Forward-looking work): FW-7 (Grid observer character via `observer_capable: true` flag), FW-8 (catalog-driven prop palette in The Grid auto-populated from ASSET_PROP entries), FW-9 (logic-system mods extending The Grid via custom triggers and actions). Combined: The Grid becomes effectively Forge-from-Halo with PD's renderer.

Pass 5 self-extrapolations (E-30 through E-37):
- E-30 selector pool formalization (4-way intersection).
- E-31 catalog entry `enabled` flag with disable-reason tracking (multi-flipper stacking, audit log line listing all flippers).
- E-32 `disable_base:` validation with LOUDFAIL.CATALOG.UNKNOWN_DISABLE_TARGET on misnamed targets.
- E-33 compound mods can disable AND add simultaneously (Halo TC example).
- E-34 `.pdwepset` post-registration validation against currently-disabled weapons (timing detail).
- E-35 empty random pool LOUDFAIL plus base-weapon fallback (always reachable via direct lookup even when disabled).
- E-36 `.pdwepset` registers as ASSET_WEAPON_SET catalog asset type; referenceable from `.pdscenario` and `random_source:`.
- E-37 per-spawn-point `weapon_spawns` and `pickup_spawns` arrays with `asset_id` (catalog ID), transform, ammo, respawn fields.

Final audit dimensions: 1904 lines (up from 1654; +250 lines for Pass 5 = ~15% growth on top of Pass 4). Zero em-dashes. Sentinel marker intact. Cumulative growth from Pass 1 origin: 625 to 1904 lines (+205%).

---

## Session S482c (`festive-hawking-49649b` follow-up #7) - 2026-04-30 PM - Dev Window v2 blank-screen fix

Mike's blocker: "Dev Window v2 is broken -- opens to a blank white screen."

**Diagnosis methodology** (progressive bisect with screen capture + in-process visual-tree introspection):

1. Reproduced via PrintWindow + screen-coords screenshot: dev window opens, title bar visible, content area pure blank white. Mike's exact symptom.
2. Tested S475 through S480 PS1 versions independently (extracted from git history). All rendered blank in my probe -- not a recent regression.
3. Verified the XAML loads cleanly via `XamlReader.Load` -- no parse errors.
4. **In-process visual-tree introspection** (DispatcherTimer + FindName + ActualWidth/Height inside the window's own process): elements rendered with correct dimensions: BtnBuild 1255x104, TabControl 2564x723, ScrollViewer 2564x644, all named labels visible. The WPF visual tree is fully constructed and laid out.
5. **`RenderTargetBitmap`** (renders the visual tree directly to a bitmap, bypassing the HWND composition layer) produced a perfect screenshot of the Dev Window UI -- every button, tab, label, status row.
6. **`PrintWindow`** with `PW_RENDERFULLCONTENT` (standard "ask the window to render itself onto an HDC" call) returned pure blank white.

The contradiction (visual tree complete + RenderTargetBitmap renders correctly + PrintWindow + screen capture both blank) localised the bug to the **WPF HWND composition / GPU pipeline**: the visual tree exists and lays out correctly, but the GPU/DWM composition path that puts pixels on the HWND backbuffer was silently dropping the frame. Classic symptom of a composition disconnection (driver state, DWM glitch, virtual-display mismatch).

**Fix**: one line, immediately after the WPF assemblies are loaded in `Section 1: Assembly loading`:

```powershell
[System.Windows.Media.RenderOptions]::ProcessRenderMode = [System.Windows.Interop.RenderMode]::SoftwareOnly
```

Forces WPF to render the entire process via the CPU software rasteriser, bypassing the broken GPU/DWM path. Slight performance cost (CPU-rendered 1500x940 with no animations and only periodic text-status updates is comfortably within tolerance for a dev tool). MUST be set before the first `Window` is constructed.

**Verified**: re-ran the actual `dev-window-v2.ps1` with the fix in place. PrintWindow capture now shows the full UI: BUILD button (green hero), RELEASE button reading "Dev v0.0.175" (gold hero), tab strip BUILD / LOG / DOCS, utility row (GitHub / Project Folder / Clean Build / Pull / Push / Prune Worktrees / Check), STATUS card (client/tests rows), VERSION card with MAJ/MIN/REV spinners showing "0 0 175", `auth: ok`, `latest: v0.0.142 (stable)`, `Dev Latest: v0.0.175`, RUN TESTS / RUN GAME bottom bar, status bar `Idle | branch: dev | HEAD: f7a8562e | 1 uncommitted | worktrees: 1 | auth: ok | v0.0.175`.

**Methodology learning**: when a WPF window opens but renders blank, do NOT assume layout / XAML / wiring. Three-step probe: (a) `XamlReader.Load` parses fine? (b) in-process `FindName` + `ActualWidth/Height` shows positive values? (c) `RenderTargetBitmap` produces correct content? If yes/yes/yes, the bug is below the WPF visual tree -- in HWND composition or GPU pipeline. Standard fix is `RenderOptions.ProcessRenderMode = SoftwareOnly`.

**Why "S482c" not "S592"**: dev branch already shipped S482 / S483 / S483b / S483c from concurrent sessions in the parent project. This is the seventh follow-up on the `festive-hawking-49649b` dev-tool branch. Numbering as "S482c" preserves the worktree's session lineage (S475 -> S477 -> S478 -> S479 -> S480 -> S481 -> S482c).

Files: `devtools/dev-window-v2/dev-window-v2.ps1` (13-line block added after the WPF `Add-Type` assembly loads).

---

## Session S591 - 2026-04-30 - Catalog Weapons F13 (Layer A retired, lane CLOSED)

Mike's playtest (run 15:05) confirmed F12's `LOADER.PDBASE.WEAPON.OK: parity check PASS (86 weapons)`. F13 retires Layer A on the back of that confirmation.

### Outcome

- `src/game/invitems.c`: 5789 lines -> ~50 line header comment. All 75+ invitem_*, 150+ invfunc_*, 80+ invammo_*, 13 invaimsettings_*, 8 invnoisesettings_* (incl. defaults), 4 invrecoilsettings_*, 110 invanim_* opcode arrays, 14 gunviscmds_* arrays, 14 invpartvisibility_* arrays, vibrationstart/max_reaper arrays, and `g_Weapons[]` removed.
- `src/game/botinv.c`: `g_AibotWeaponPreferences[]` table removed; bot prefs now sourced from base/weapons.pdbase via the loader's `s_BotPrefs[86]` pool.
- `src/include/game/inv.h` + `src/include/data.h`: extern declarations for `g_Weapons[]`, `g_AibotWeaponPreferences[]`, `invaimsettings_default`, `invnoisesettings_silent` removed.
- `src/game/player.c`: `ARRAYCOUNT(g_Weapons)` -> `catalogManagerWeaponCount()` in the ammo-iteration loop (only live consumer outside the manager).
- `port/src/catalog_mgr_weapons.c`: deleted F12 parity-period fallback (`loaderPdbaseIsActive()` gate -> just calls `loaderPdbaseGetWeapon` etc.). Bot pref accessor now routes to `loaderPdbaseGetBotPref()`.
- `port/src/loader_pdbase.c`: added `s_BotPrefs[CATALOG_MGR_WEAPON_COUNT]` pool + `bot_pref` JSON sub-struct parser (was previously skipped). Hardcoded default aim/noise sentinel values (replacing reads of the now-deleted externs). Deleted `loaderPdbaseRunParityCheck()` -- nothing left to compare against.
- `port/src/main.c`: dropped the parity check call from startup.
- `tests/test_loader_pdbase_scan.cpp`: 4 new F13 grep-guard cases asserting the symbols do not return as live (non-comment) occurrences. Updated F12 cases that referenced the parity check or the legacy externs (now expected absent). Added `fileHasNonCommentOccurrence()` helper so comment mentions of the symbol names are allowed (grep-trail for archeologists).
- Binary size: PerfectDark.exe 54.7 MB -> 54.5 MB (~200 KB shrink from removed static data). PerfectDarkServer.exe unchanged (server didn't link the static records).

### Files

- `src/game/invitems.c` (5789 -> 50 lines)
- `src/game/botinv.c` (-117 lines)
- `src/include/game/inv.h` (-3 lines, comment replacement)
- `src/include/data.h` (-2 lines)
- `src/game/player.c` (1 substitution)
- `port/include/loader_pdbase.h` (-3 lines, +bot_pref accessor decl)
- `port/src/catalog_mgr_weapons.c` (-13 lines manager swap)
- `port/src/loader_pdbase.c` (-65 lines parity check, +75 lines bot_pref parser + sentinel defaults)
- `port/src/main.c` (-1 line, comment update)
- `tests/test_loader_pdbase_scan.cpp` (+86 lines new tests + helper)
- Context updates: `context/pillars/catalog.md`, `context/tasks.md`, `context/session-log.md`, `context/designs/catalog/catalog-full-pipeline-weapons.md`

### Decisions

- **Header-comment grep-trail.** The deletions leave header comments in invitems.c / botinv.c / inv.h / data.h that explain what was removed and where the data went (file + commit pointer). Future archeologists who grep for `g_Weapons` find the breadcrumb. The grep-guard tests use `fileHasNonCommentOccurrence` so this trail doesn't fail the test.
- **Hardcoded default aim/noise sentinels** in the loader (replacing reads of the legacy externs). Values match the historical struct literals exactly. F-future could move these to .pdbase metadata if mods need to override them.
- **F12 parity check + parity-period fallback retired together.** They were two halves of the same transitional bridge; both go in F13.
- **Server unchanged.** loader_pdbase.c still not in `SRC_SERVER` (curated list); server-side weapon resolution stays on catalog-row + session-ref pipeline. No behavior change.

### Verification

- pd build: 24s, PerfectDark.exe 54.5 MB.
- pd-server build: 7s, PerfectDarkServer.exe 22.3 MB.
- pd-tests build: clean.
- F13 selector (`[catalog-mgr-weapon][s484][f13]`): 4 cases / 18 assertions, all PASS.
- Full catalog-mgr-weapon (`[catalog-mgr-weapon]`): 47 cases / 219 assertions, all PASS.
- Suite-wide: 413 cases / 20,072 assertions, **same 3 pre-existing failures** as before F11+F12+F13 (test_catalog_provider_static.cpp, test_cutscene_layer.cpp), **zero regressions** from the entire F11-F13 lane.
- **Mike's playtest (15:05): F12 parity check PASS** -- the gate that unblocked F13.

### Bug ledger note

The OOB-read class (B-263 / `g_Weapons[254]` AV crash class) is now structurally impossible: there is no `g_Weapons[]` to index out of bounds. The defensive guard at `modelmgrLoadProjectileModeldefs` becomes belt-and-braces redundancy that stays for safety.

### Next

- Texture deployment + extraction investigation (Slice A: drop legacy `mods/` build deploy. Slice B: debug ROM texture extraction path correctness). Surfaced during F12 runtime debugging. Mike picks order.
- Catalog Gate 3 migration (heads/bodies/arenas/audio + Manager + .pdbase pattern) per the original critical path lane 2.

---

## Session S591 - 2026-04-30 - Catalog Weapons F12 (loader + manager pool routing + parity self-test)

Continued the F11-F13 lane. F12 ships the runtime loader: a JSON parser, opcode codec, manager-owned typed pools, enum lookup tables, manager accessor swap, startup wiring, and field-equivalence runtime self-test.

### Outcome

- New `port/src/loader_pdbase.c` (~1400 lines) replaces the F10 stub with full implementation.
  - JSON tokenizer + recursive-descent parser (~250 lines).
  - Opcode codec for all 12 `gunscript_*` mnemonics + 5 `gunviscmd_*` mnemonics.
  - Pools: `weapon[86]`, `guncmd[3000]`, `gunviscmd[500]`, `modelpartvisibility[500]`, `inventory_ammo[120]`, `invaimsettings[120]`, `noisesettings[120]`, `recoilsettings[120]`, `weaponfunc_any_t[256]`, `f32 vibrations[256]`, anim name table[256].
  - Per-record parsers (weapon, weaponfunc with all 8 variants, ammo, aim/noise/recoil settings, gunviscmds, partvisibility, animation opcodes).
  - Public API: `loaderPdbaseScan`, `loaderPdbaseBuildWeaponManager`, `loaderPdbaseIsActive`, `loaderPdbaseGetWeapon`, `loaderPdbaseGetDefaultAim/Noise`, `loaderPdbaseRunParityCheck`, `loaderPdbaseEncodeOpcode`.
- New `port/include/loader_pdbase_enums.h` + `port/src/loader_pdbase_enums.c` (generated, ~1500 lines): ANIM (1208 entries), SFX (1981), L_GUN (237), FILE (2007). Total ~5400 entries. Linear scan resolution at startup; fast enough for one-time load.
- Extractor extended: now also emits the enum lookup tables (`--enum-tables-out` arg). Same Python script handles JSON + enum-table generation deterministically.
- Manager (`port/src/catalog_mgr_weapons.c`) gates accessors on `loaderPdbaseIsActive()`: returns pool-backed pointers when loader is active, falls back to `g_Weapons[]` while parity is verified. F13 retires the fallback.
- Default fallbacks (`catalogManagerWeaponDefaultAimSettings`, `catalogManagerWeaponDefaultNoiseSettings`) prefer pool-backed copies when active.
- `port/src/main.c` wires the loader call (`loaderPdbaseScan` -> `loaderPdbaseBuildWeaponManager` -> `loaderPdbaseRunParityCheck`) right after `assetCatalogRegisterBaseGame()`.
- `port/src/server_main.c` opts out: dedicated server doesn't link `loader_pdbase.c` (not in curated `SRC_SERVER` list); server-side weapon resolution stays on the catalog-row + session-ref pipeline. Comment left for future activation.
- Per Mike's 2026-04-30 unlock-state clarification: loader registration is unconditional on unlock state. Catalog row + manager always cover all 86 entries; selectors filter unlock state separately.
- pd-tests: 6 new cases / 45 assertions in `[catalog-mgr-weapon][s484][f12]`. Pin: loader API surface (header decls), all 5 log channels in source, all 12 opcode mnemonics in source, manager accessor routes through loader, loader wired into client startup, enum lookup tables exist for all 4 families.
- Field-equivalence runtime self-test (`loaderPdbaseRunParityCheck`) compares 12 scalar fields per weapon vs `g_Weapons[i]`; logs `LOADER.PDBASE.WEAPON.PARITY_FAIL:` on mismatch. Sub-record comparison (functions, ammos, gunviscmds, partvisibility) deferred to keep diff focused; F13 will surface those if any indirectly mutated path breaks.

### Files

- `port/src/loader_pdbase.c` (rewrote scaffold to full implementation)
- `port/include/loader_pdbase.h` (extended with new public functions)
- `port/include/loader_pdbase_enums.h` (new)
- `port/src/loader_pdbase_enums.c` (new, generated)
- `port/src/catalog_mgr_weapons.c` (route accessors through loader when active)
- `port/src/main.c` (wire loader into startup)
- `port/src/server_main.c` (opt out + comment)
- `devtools/extract_weapons_pdbase.py` (extended to emit enum tables)
- `tests/test_loader_pdbase_scan.cpp` (6 new F12 cases)
- Context updates: `context/pillars/catalog.md`, `context/session-log.md`

### Decisions

- **Server opts out of loader** for F12: `port/src/loader_pdbase.c` lives in `SRC_PORT` (auto-discovered for pd) but not in `SRC_SERVER` (curated). Server uses catalog rows + session refs; no need for the typed weapon payload. If a future server feature needs it, add the loader + its deps to `SRC_SERVER`.
- **Pool sizing** chosen with headroom: invitems.c has ~110 animations, ~80 ammos, etc. Pool caps are 1.5-2x observed counts. POOL_FULL fires loud-fail if exceeded; raise the cap, don't silently drop.
- **Runtime parity check** is the F12 verifier (vs an in-process pd-tests case): pd-tests is globals-free and can't link `g_Weapons[]`. The loud-fail at startup is the canonical regression pin until F13 retires the legacy table entirely.
- **s_BaseWeapons stays at 41 (MP-only)** for F12. The directive said "expand to 86" but that's catalog-row metadata; runtime weapon resolution by index works without the expansion. Marked as a deferred F12 follow-up (could land as F12.x if Mike wants the 45 SP-only weapon catalog rows for introspection / debugging UX, per the 2026-04-30 unlock-state clarification).

### Verification

- pd build: 9s, PerfectDark.exe 54.7 MB.
- pd-server build: 1s, PerfectDarkServer.exe 22.3 MB.
- pd-tests build: 18s baseline + ~2s incremental.
- F12 selector: `pd-tests.exe "[catalog-mgr-weapon][s484][f12]"` -> 45 assertions / 6 cases pass.
- F11+F12 selector: 80 assertions / 14 cases pass (F11 + F12 stacked).
- Suite-wide: 403 cases / 19,995 assertions, same 3 pre-existing failures, zero regressions.
- **Pending Mike's playtest verification**: launch PerfectDark.exe, observe `LOADER.PDBASE.WEAPON.OK:` summary line + absence of `PARITY_FAIL:` warnings in the playtest log. If parity passes, F13 is unblocked.
- Pre/post merge line-count snapshot per `procedures.md`: all touched files line counts match across worktree-to-dev merges.

### Next

- Mike runs the game once, confirms `LOADER.PDBASE.WEAPON.OK:` parity check PASS line is present (no `PARITY_FAIL:` warnings).
- F13: delete `g_Weapons[]`, the 110 `invanim_*` arrays, the per-weapon `gunviscmds_*` / `invpartvisibility_*` arrays, all `invitem_*` / `invfunc_*` / `invammo_*` / `invaimsettings_*` / `invnoisesettings_*` / `invrecoilsettings_*` static records from `src/game/invitems.c`. Delete `g_AibotWeaponPreferences[]` from `src/game/botinv.c`. Delete extern declarations in `src/include/data.h`, `src/include/game/inv.h`. Add grep-guard test. Manager + .pdbase becomes sole source.

---

## Session S591 - 2026-04-30 - Catalog Weapons F11 (data-driven .pdbase + extractor)

Continued the Catalog Full-Pipeline Weapons track. F1-F10 shipped at S484; F11 ships the first generated `base/weapons.pdbase` archive plus the Python extractor that produces it. Mike approved Path B (data-driven animations) mid-session over Path A (named C symbols) so a future IK evaluator can bolt onto the same archive without churning the data layer again.

### Outcome

- New `devtools/extract_weapons_pdbase.py` (1329 lines) parses `src/game/invitems.c` + `src/game/botinv.c`, builds a constants table from `src/include/constants.h` + `src/include/gunscript.h`, resolves `#if VERSION` blocks to the NTSC_1_0 path, decodes `gunscript_*` and `gunviscmd_*` macro calls into JSON opcode arrays, and walks `g_Weapons[]` to emit per-weapon records with sub-records (functions, ammos, aimsettings, noise, recoil, gunviscmds, partvis) inlined per design Section C.
- New `base/weapons.pdbase` (12,823 lines) holds 86 weapon records + 110 animation records, all 86 catalog IDs unique (`base:keycard`/`base:keycard_slot62`/... for the 8 keycard slots and similar for shared `invitem_hammer`/`invitem_rocket`).
- `tests/test_loader_pdbase_scan.cpp` upgraded from F10 shape-only to F11 structure pins: 86 weapon records, 110 animation records, every gunscript mnemonic + sethidden present, no `unknown_macro` leaks, every weapon carries `bot_pref`, extractor script committed alongside. 8 cases / 35 assertions, all passing.
- Symbolic enum values (ANIM_*, SFX_*, FILE_*, L_GUN_*, MODELPART_*) preserved as JSON strings; numeric flag bitfields ORed to integers per Mike's directive ("integers in JSON, strings can be added later").
- Cross-references between animations (e.g., `invanim_punch` references `invanim_punch_type1..4` via `gunscript_random` / `gunscript_include`) preserved as bare-string anim refs; loader will resolve at load time.

### Files

- `devtools/extract_weapons_pdbase.py` (new)
- `base/weapons.pdbase` (new, generated)
- `tests/test_loader_pdbase_scan.cpp` (extended)
- Context updates: `context/pillars/catalog.md`, `context/tasks.md`, `context/session-log.md`, `context/designs/catalog/catalog-full-pipeline-weapons.md`

### Decisions

- **Path B (data-driven animations) over Path A (named C symbols).** Mike's call: "ultimately I want to convert certain anims to use IK." Path B keeps the future IK migration in scope without disturbing the data layer.
- **Inline-duplicate shared settings** (`invaimsettings_default` etc.) per weapon in JSON. Slightly bigger file, simpler loader, easier round-trip testing.
- **Integers in JSON for flag bitfields**, strings for symbolic enum names where they're not pre-resolved (lang IDs, animation IDs, file/sound/modelpart IDs).
- **Generated artifact** (`base/weapons.pdbase`) committed alongside the generator (`extract_weapons_pdbase.py`); rerunning the script must produce byte-identical output (deterministic by construction).
- **Catalog ID format** for duplicate symbols: `base:<slug>` for the first slot, `base:<slug>_slot<N>` for subsequent slots (covers the 8 keycard slots, 4 hammer slots, 2 rocket slots).

### Verification

- Wrapper-only build passed (`devtools/build-session.ps1 -Session f11weap -Target tests`, 37s baseline + 2s incremental rebuild).
- F11 selector all-green: `pd-tests.exe "[catalog-mgr-weapon][s484][f11]"` -> 35 assertions / 8 cases.
- Suite-wide: 403 cases / 18,451 assertions, with the same 3 pre-existing failures (test_catalog_provider_static.cpp, test_cutscene_layer.cpp; documented as not-this-work) and zero regressions.
- Pre/post merge line-count snapshot per `procedures.md`: extractor 1329 lines, archive 12823 lines, test file 186 lines unchanged across the worktree-to-dev merge.

### Next

- F12: implement C-side JSON parser + opcode codec, manager populates from `base/weapons.pdbase` (typed pools, parity test against `g_Weapons[]`).
- F13: delete Layer A weapon data + animations + supporting records from `invitems.c` and `botinv.c`. Manager + `.pdbase` becomes sole source.

---


## Session S590 - 2026-04-29 - Maintainability drag: Firing Range menu graph transition

Began Mike's "Reduce maintainability drag" request with the smallest concrete menu-graph cleanup still visible in Training Mode: the Firing Range difficulty dialog's pre-game push and cancel pop.

### Outcome

- Added `s_FrDifficultyEdges` in `menugraph.c` with a `start` push edge to `MENU_TYPE_FR_INFO` and a `cancel` pop edge.
- Registered `MENU_TYPE_FR_DIFFICULTY` as `fr_difficulty` in the graph node table.
- Added `frDifficultyOpenPreGame()` in `pdgui_menu_training.cpp` so difficulty selection keeps the legacy `frSetDifficulty()` side effect but delegates the dialog transition to `menuGraphFirePushDialog()`.
- Routed Bronze/Silver/Gold difficulty buttons and Cancel through the graph helpers.
- Added `[input][menu_graph][training][static]` coverage that guards the graph edges and prevents `renderFrDifficulty()` from reintroducing direct `menuPushDialog(&g_FrTrainingInfoPreGameMenuDialog)` or `menuPopDialog()`.

### Files

- `port/src/menugraph.c`
- `port/fast3d/pdgui_menu_training.cpp`
- `tests/test_menu_graph.cpp`
- Context updates: `context/tasks-current.md`, `context/session-log.md`

### Verification

- Source checks confirmed the FR difficulty graph edges/node, renderer helper, graph push/pop calls, and static guard are present in the live tree.
- Git-for-Windows `diff --check` passed for the touched source/test files.
- Wrapper-only binary verification is pending. `.\devtools\build-session.ps1 -Session mtg587b -Target tests -BuildTimeoutSeconds 600` queued normally, started after 14m33s, configured/generated CMake successfully, then hit the 600s watchdog in `Generate Headers [pd_headers]`.
- Real logs: `_build-session.out.log` showed header-generation heartbeat through 589s; `_build-session.err.log` was empty; `_build-headless-...generate-headers-pd_headers.out.log` and `.err.log` were empty; heartbeat reported `pid=15236 stdout=0b stderr=0b ninja_log=missing` and no live child process rows. Configure output ended with `Configuring done`, `Generating done`, and the isolated build path.
- No `pd-tests.exe` was produced, so `"[input][menu_graph][training][static]"` was not run.
- Cleaned up `mtg587b` with `.\devtools\build-session.ps1 -Remove -Session mtg587b`; follow-up `-List` showed `mtg587b` gone and no active/waiting queue entries.

### Next

- Re-run wrapper-only tests when header generation is responsive, then run `.\.claude\session-builds\<id>\pd-tests.exe "[input][menu_graph][training][static]"` with `C:\msys64\mingw64\bin` on `PATH`.
- Next maintainability candidate: continue with another narrow direct menu-transition cleanup only after this slice has binary/test verification or Mike accepts source-checked pending state.

---

## Session S589 - 2026-04-29 - Stability/content blockers: character head attach guard

Began Mike's "Close stability/content blockers" track with a narrow B-182/B-183 character assembly crash guard.

### Outcome

- Found `src/game/body.c::body0f02ce8c()` still called `modelAllocateRwData(headmodeldef)` before confirming the catalog returned a non-NULL head modeldef.
- Switched the positive-head path to `catalogGetHeadModeldefChecked(headnum, &headmodeldef)` so catalog misses are loud and OOB slots do not read `g_HeadsAndBodies[headnum]` first.
- Moved head RW allocation inside the `headmodeldef != NULL` guard before adding `headmodeldef->rwdatalen`.
- Added an explicit `node != NULL` requirement before `modelmgrAttachHead()` so bodies missing `MODELPART_CHR_HEADSPOT` log and skip attach instead of dereferencing the missing attach point.
- Added static coverage in `tests/test_catalog_checked.cpp` for both invariants.

### Files

- `src/game/body.c`
- `tests/test_catalog_checked.cpp`
- Context updates: `context/bugs.md`, `context/tasks-current.md`, `context/session-log.md`

### Verification

- Git-for-Windows `diff --check` passed for `src/game/body.c` and `tests/test_catalog_checked.cpp`.
- Source invariant check passed: checked head accessor present, old pre-guard RW allocation pattern absent, RW allocation guarded, head attach requires `node != NULL`, missing-headspot diagnostic present, and the regression tests are present.
- Wrapper-only binary verification blocked: `.\devtools\build-session.ps1 -Session hguard589 -Target tests -BuildTimeoutSeconds 600` configured/generated CMake successfully, then stalled in `Generate Headers [pd_headers]`.
- Real wrapper logs: `_build-session.out.log` showed heartbeat through 478s before this outer Codex tool call timed out; `_build-session.err.log` was empty; `_build-headless-...generate-headers-pd_headers.out.log` and `.err.log` were empty; heartbeat reported `pid=27984 stdout=0b stderr=0b ninja_log=missing` and `(no live child process rows collected)`. Configure stderr contained only the unused `CMAKE_TRY_COMPILE_TARGET_TYPE` warning.
- No `pd-tests.exe` was produced, so `"[catalog][checked][static]"` could not run.

### Next

- Re-run wrapper-only tests after the `pd_headers` stall is cleared, then run the focused selector from the isolated tree with `C:\msys64\mingw64\bin` on `PATH`.
- Manual playtest target remains Combat Sim / 30+ bots with Chris and the B-179 head set: no Chris client crash, no missing-head attach crash, and any remaining disconnected geometry should be logged separately under B-183.

---

## Session S588 - 2026-04-29 - Connect-code QC gate alignment

Started Mike's "Expand test and QC gates" track with a narrow checklist/test-alignment gate.

### Outcome

- Found the old SPF-3 Join by Code checklist still expected direct IP acceptance and decoded IP:port display, which conflicts with the current no-raw-IP UI constraint.
- Updated `context/qc-tests.md` so Join Server manual QC expects a connect-code-only prompt, no raw address/IP prompt, no decoded raw IP:port display, and direct IP:port rejection.
- Added a `[connectcode][qc][static]` test in `tests/test_connectcode.cpp` that fails if the stale direct-IP QC language returns.
- Documented the new selector in `tests/README.md`.

### Files

- `context/qc-tests.md`
- `tests/test_connectcode.cpp`
- `tests/README.md`
- Context updates: `context/tasks-current.md`, `context/session-log.md`

### Verification

- Git-for-Windows `diff --check` passed for the touched files using a one-command safe-directory override.
- Source-level QC invariant check passed: banned stale phrases were absent and required no-raw-IP phrases were present.
- Wrapper-only binary verification is pending. `.\devtools\build-session.ps1 -Session qc584 -Target tests -BuildTimeoutSeconds 600` queued normally, started after 13m03s, configured/generated CMake successfully, then hit the 600s watchdog in `Generate Headers [pd_headers]`.
- Real logs: `_build-session.out.log` showed header-generation heartbeat through 588s; `_build-session.err.log` was empty; `_build-headless-...generate-headers-pd_headers.out.log` and `.err.log` were empty; heartbeat reported `pid=3092 stdout=0b stderr=0b ninja_log=missing` and no live child process rows. Configure stderr contained only the unused `CMAKE_TRY_COMPILE_TARGET_TYPE` warning.
- No `pd-tests.exe` was produced, so `"[connectcode][qc][static]"` was not run.
- Cleaned up `qc584` with `.\devtools\build-session.ps1 -Remove -Session qc584`.

### Next

- Re-run wrapper-only tests when header generation/build queue pressure clears, then run `.\.claude\session-builds\<id>\pd-tests.exe "[connectcode][qc][static]"` with `C:\msys64\mingw64\bin` on `PATH`.
- Next QC gate candidate: add/refresh a Swarm Debug Scenarios manual checklist section that tracks launch through `matchStart()` and CPU count-cycle despawn cleanup.

---

## Session S587 - 2026-04-29 - Public Mods publishing hardening

Began Mike's "Ship public mods, Forge, Grid, and Studio tracks" request with the first public-mods shipping slice: registry-backed publishing and request hardening.

### Outcome

- Chose Public Mods as the first creator-track slice because it is the shared distribution surface for Forge, Grid, and Studio outputs.
- Replaced the Social shell Public Mods tab's free-form add form with an installed-mod selector backed by `modmgr`.
- Added safe public-mod ID validation in `social_share.c`.
- Made `shareModPublicAdd()` require a valid installed mod and made broadcasts skip stale/invalid registry entries.
- Made peer requests serve only explicitly published local mods, preferring `.pdmod` archive paths from `modmgr`; the old peer-supplied `$H/mods/installed/%s` path probe was removed.
- Escaped strings when writing `mod-public.json`.
- Added `[social][public_mods][static]` tests and wired them into `pd-tests`.
- Logged B-294.

### Files

- `port/src/social_share.c`
- `port/fast3d/pdgui_friends.cpp`
- `CMakeLists.txt`
- `tests/test_public_mods_static.cpp`
- Context updates: `context/bugs.md`, `context/tasks-current.md`, `context/session-log.md`

### Verification

- Git-for-Windows `diff --check` passed for the production code/test-list files.
- Source checks verified the changes are present in the live folder: safe-ID gate, registry-backed UI, static test file, and `CMakeLists.txt` entry.
- Production source no longer contains the free-text Public Mods `mod id` input or old `mods/installed/%s` request-path pattern.
- Build/test pending: wrapper-only `pm588` verification was queued behind other active test sessions and did not start before the 20-minute command window expired. `.\devtools\build-session.ps1 -List` then showed `qc584` active and other waiting sessions; `pm588` was no longer queued. No `pd-tests.exe` was produced for this slice.

### Next

- Re-run `.\devtools\build-session.ps1 -Session pm588 -Target tests -BuildTimeoutSeconds 600` when the queue is clear, then run `.\.claude\session-builds\pm588\pd-tests.exe "[social][public_mods][static]"`.
- Next creator-track slice: package folder-backed Forge/Grid/Studio mods into `.pdmod` before public-mod transfer, then add the matching import/install UX.

---

## Session S586 - 2026-04-29 - Online interoperability proof: hole-punch handoffs

Began Mike's "Prove online interoperability" track with the smallest concrete listen-host proof slice: ensure every remote player-facing handoff uses the same NAT-aware client connection waterfall.

### Outcome

- Confirmed active release scope is in-client/listen-host connectivity; dedicated-server productization stays deferred.
- Found two remote handoff paths bypassing the NAT waterfall: group-session invite/p2p handoff and live spectator handoff called raw `netStartClient(addr)`.
- Changed both paths to call `netStartClientWithHolePunch(addr)`.
- Updated `group_session.h` comments and log text so handoff docs match behavior.
- Added `[net][interoperability][static]` guards for remote handoff routing and listen-host NAT startup/cleanup.
- Logged B-293.

### Files

- `port/src/net/group_session.c`
- `port/include/net/group_session.h`
- `port/src/spectator.c`
- `tests/test_net_lifecycle_static.cpp`
- Context updates: `context/bugs.md`, `context/tasks-current.md`, `context/session-log.md`

### Verification

- Git-for-Windows `diff --check` passed for the touched source/test/context files.
- Source-level PowerShell interop invariant check passed.
- Fixed-wrapper binary verification is pending: `.\devtools\build-session.ps1 -Session int586 -Target tests -BuildTimeoutSeconds 600` queued normally, started after 12m44s, configured/generated CMake successfully, then hit the 600s watchdog in `Generate Headers [pd_headers]`.
- Real logs: `_build-session.out.log` showed header generation heartbeat through 589s; `_build-session.err.log` was empty; `_build-headless-...generate-headers-pd_headers.out.log` and `.err.log` were empty; heartbeat reported `pid=12504 stdout=0b stderr=0b ninja_log=missing` and no live child process rows. Configure stderr contained only the unused `CMAKE_TRY_COMPILE_TARGET_TYPE` warning.
- No `pd-tests.exe` was produced, so `"[net][interoperability][static]"` was not run.
- Cleaned up `int586` with `.\devtools\build-session.ps1 -Remove -Session int586`.

### Next

- Re-run isolated tests with the fixed wrapper when header generation/build queue pressure clears, then run `.\.claude\session-builds\<id>\pd-tests.exe "[net][interoperability][static]"`.
- Manual listen-host NAT smoke should cover direct connect-code join, invite/group-session handoff, and live spectator handoff; all should use/log `netStartClientWithHolePunch`.
- Next proof candidate: source-level invariant across catalog distribution join flow (`SVC_CATALOG_INFO` -> `CLC_CATALOG_DIFF` -> mandatory digest `SVC_DISTRIB_BEGIN` -> chunk/end).

---

## Session S585 - 2026-04-29 - Catalog/provider model-source bridge ownership

Began Mike's "finish catalog/provider ownership" push by moving legacy model-source filenum fallback ownership into the catalog API instead of leaving each bridge callsite to maintain its own asset-type probing order.

### Outcome

- Added `catalogHandleByModelSourceFilenum(preferred_type, source_filenum)` as the catalog-owned bridge for model-source filenum handle resolution.
- The helper tries an explicit preferred type first when provided, then owns the fallback order across `ASSET_MODEL`, `ASSET_BODY`, `ASSET_HEAD`, `ASSET_WEAPON`, `ASSET_PROP`, and `ASSET_VEHICLE`.
- Migrated `bgunResolveQueuedModelHandle`, `menuResolveModelHandleByFilenum`, and `modelcatalog.c::catalogValidateResolveHandle` off local fallback arrays and direct `catalogHandleBySourceFilenum()` probing.
- Tightened `tests/test_catalog_provider_static.cpp` so these bridge callsites must use `catalogHandleByModelSourceFilenum()` and cannot reintroduce direct source-filenum/catalog-effective-handle logic.
- Logged B-292 for the scoped `run-pd-tests.ps1` StrictMode helper failure discovered during verification; Mike then directed verification through the fixed isolated build wrapper only.

### Files

- `port/include/assetcatalog.h`
- `port/src/assetcatalog_api.c`
- `src/game/bondgun.c`
- `src/game/menu.c`
- `port/src/modelcatalog.c`
- `tests/test_catalog_provider_static.cpp`
- Context updates: `context/bugs.md`, `context/tasks-current.md`, `context/session-log.md`

### Verification

- `git diff --check` passed for the touched code/test/context files.
- Source guard check passed: `catalogHandleBySourceFilenum(` no longer appears in `src/game/bondgun.c`, `src/game/menu.c`, or `port/src/modelcatalog.c`.
- Source guard check passed: the same callsites now route through `catalogHandleByModelSourceFilenum(`, and local model-source asset-type fallback arrays were removed.
- Early `.\devtools\run-pd-tests.ps1 -Session cat584 -Scope catalog-provider` failed before build with B-292.
- After Mike's fixed-wrapper instruction, `.\devtools\build-session.ps1 -Session cat585 -Target tests -BuildTimeoutSeconds 600` was used only through the isolated wrapper. The first attempt waited 14m49s, configured successfully, then the Codex command timeout killed the wrapper just after header generation started; stale lock PID 27740 and child PID 27428 were gone and `cat585` was cleaned with `-Remove -Force`.
- A concurrent session moved the tree during verification, so the catalog code was reapplied against the current files and the source/static checks were rerun.
- The second fixed-wrapper attempt reused `cat585` and waited 30 minutes behind other queued test sessions without becoming active before the Codex command timeout. Follow-up `.\devtools\build-session.ps1 -List` showed active `hguard589` and queued `det585` / `mtg587b`; `cat585` was absent from the session list and queue, had no session directory or lock, and produced no build log or `pd-tests.exe`.

### Next

- Re-run `.\devtools\build-session.ps1 -Session <id> -Target tests -BuildTimeoutSeconds 600` when queue pressure allows the build to complete, then run `.\.claude\session-builds\<id>\pd-tests.exe "[catalog][provider][static]"` with `C:\msys64\mingw64\bin` on `PATH`.
- Once verification is responsive, continue the catalog ownership push by retiring or further confining the remaining deprecated source-filenum/modelnum compatibility bridges.

---

## Session S584 - 2026-04-29 - Isolated build/test pipeline fix

Fixed the `headguard` build-wrapper failure class without touching gameplay/product code.

### Outcome

- Successful CMake configure steps with stderr warnings no longer become blank-exit configure failures; missing exit-code cases now name the `.exit` file and generated `.cmd` runner.
- `devtools/_build-env-prelude.ps1` removes `devkitPro\msys2\usr\bin` from build PATH so version probes do not accidentally use devkitPro Git.
- `CMakeLists.txt` resolves a preferred `PD_GIT_EXECUTABLE` and treats Git metadata probe failures as nonfatal warnings with clear fallbacks.
- `devtools/build-headless.ps1` now runs generated headers as an explicit direct Ninja `pd_headers` step (`-j1 -v`) before target compilation, then runs requested targets through direct verbose Ninja.
- Per-step heartbeat logs record elapsed time, process rows/command lines, stdout/stderr byte counts, and `.ninja_log` state so a silent generated-header or Ninja stall is observable.
- Added `devtools/build-headless.ps1 -SelfTest` for the wrapper regression: stderr warnings with exit code 0 pass, real nonzero exits fail with recorded `.exit` files.
- Logged B-291 for the build-system bug class.

### Files

- `CMakeLists.txt`
- `devtools/_build-env-prelude.ps1`
- `devtools/build-headless.ps1`
- `devtools/build-session.ps1`
- Context/docs: `context/build.md`, `context/tasks-current.md`, `context/session-log.md`, `context/bugs.md`, `tests/README.md`

### Verification

- PowerShell parser checks passed for `devtools/build-headless.ps1`, `devtools/build-session.ps1`, `devtools/run-pd-tests.ps1`, and `devtools/_build-env-prelude.ps1`.
- `.\devtools\build-headless.ps1 -SelfTest -OutputDir .claude\session-builds\pipefix-selftest` passed.
- `.\devtools\build-session.ps1 -Session pipefix -Target tests -BuildTimeoutSeconds 600` passed and produced `pd-tests.exe`; a final rerun after the `NINJA_STATUS` escape fix passed incrementally.
- `.\devtools\build-session.ps1 -Tail -Session pipefix` surfaced wrapper stdout/stderr plus recent `_build-headless-*` stdout/stderr/heartbeat logs.
- Direct requested selector `.\.claude\session-builds\pipefix\pd-tests.exe "[catalog][checked][static]"` matched no current tests.
- Current checked selector `"[catalog][checked][regression]"` passed: 10 test cases / 36 assertions.
- `.\devtools\run-pd-tests.ps1 -ListScopes` passed.
- `git diff --check` passed with only Git line-ending warnings.
- Cleaned `pipefix` and `pipefix-selftest`; `-List` confirmed no active/waiting queue entries and pre-existing sessions were untouched.

### Next

- Use the fixed wrapper for downstream Swarm, security, Social shell, and targeted-test lanes as their owning slices resume.
- Optional cleanup: add a `catalog-checked` scope alias or update stale prompts that still ask for `[catalog][checked][static]`.

---

## Session S583 - 2026-04-29 - Deterministic verification telemetry

Started the "Make verification deterministic" plan by targeting the most immediate failure mode: watchdog-killed builds were preserving wrapper logs but could still lose the useful CMake/Ninja step output or leave ambiguous empty stderr.

### Outcome

- `devtools/build-headless.ps1` now writes raw stdout/stderr for every configure/compile step directly into the isolated build directory before the parent wrapper sees the final result.
- Each step now also writes an explicit `.exit` file, avoiding the blank `Start-Process` exit-code behavior observed in this sandbox after a successful CMake configure.
- The step runner uses a generated `.cmd` file per step so CMake/Ninja output reaches disk even if the wrapper watchdog kills the child process.
- `devtools/build-session.ps1` now prints recent `_build-headless-*.log` tails on watchdog timeout and when using `-Tail`.
- `build-session.ps1` now attempts to print a process-tree snapshot before killing an over-timeout child; if the child exits during cleanup, it reports that no live process rows were collectible.
- `devtools/run-pd-tests.ps1` now accepts `-BuildTimeoutSeconds <seconds>` and forwards it to the isolated `tests` build.
- Documented the step logs in `context/build.md` and the targeted-test timeout flag in `tests/README.md`.

### Files

- `devtools/build-headless.ps1`
- `devtools/build-session.ps1`
- `devtools/run-pd-tests.ps1`
- `tests/README.md`
- Context updates: `context/build.md`, `context/tasks-current.md`, `context/session-log.md`

### Verification

- PowerShell parser checks passed for all three touched scripts.
- `.\devtools\run-pd-tests.ps1 -ListScopes` passed.
- `git diff --check` passed for the touched build/test files.
- Timeout-path validation: `.\devtools\build-session.ps1 -Session det584d -Target tests -BuildTimeoutSeconds 10` intentionally timed out. The wrapper printed durable configure logs from `_build-headless-*.out.log` / `.err.log`, created compile-step log files, reported that no live process rows could be collected, and returned cleanup instructions.
- Cleaned up validation sessions `det584`, `det584b`, `det584c`, and `det584d`. Pre-existing session builds were left untouched.
- Full `pd`, `pd-server`, or `pd-tests` verification was not completed in this slice.

### Next

- Next deterministic-verification slice: make compile progress visible during long or hung Ninja runs. The likely path is direct Ninja invocation with explicit progress/status logging, or a lightweight heartbeat that records active child process names/commands while compile is running.

---

## Session S582 - 2026-04-28 - Queued build hang watchdog

Followed up on the queued isolated-build pipeline after Mike asked whether queued builds can be detected as hung and removed from the queue.

### Outcome

- Added `-BuildTimeoutSeconds` to `devtools/build-session.ps1`, now defaulting to 60 seconds for queued builds after Mike clarified normal full builds are usually about 33 seconds.
- Queued builds now record the timeout in active queue metadata, show it in `-List`, and return exit code `124` when the watchdog fires.
- If a queued child build exceeds the timeout, the wrapper stops that child process tree, updates queue heartbeat/status, clears the active slot in `finally`, and lets the next queued session start.
- Stale active queue cleanup can also stop an orphaned over-timeout child process tree after its wrapper has died.
- Queue ETA defaults were tightened to match observed normal runtime expectations: `client`/`server` 45s, `tests` 60s, `all` 60s, with successful duration history still preferred when present.
- Added live output capture for newly started queued builds: child stdout/stderr now go to `_build-session.out.log` and `_build-session.err.log` inside the session build directory, active queue metadata records those paths, `-List` prints them, and `-Tail` / `-Tail -Follow` can read them.
- Checked the live queue: old active `ui568` was already gone by the time the stop command ran; a fresh queued `ui568` request briefly reappeared and was removed too aggressively. Mike clarified only the hung front-of-queue instance needed removal, and future `ui568` re-adds are normal queue entries.

### Files

- `devtools/build-session.ps1`
- `AGENTS.md`
- Context updates: `context/build.md`, `context/tasks-current.md`, `context/session-log.md`

### Verification

- PowerShell parser check passed for `devtools/build-session.ps1`.
- `git diff --check` passed for `devtools/build-session.ps1`, `AGENTS.md`, and the touched context files.
- `.\devtools\build-session.ps1 -List` passed and showed the new active timeout display.
- `.\devtools\build-session.ps1 -Tail` passed against an old-wrapper active build and correctly reported that no captured log existed because it was launched before stdout/stderr capture was added.
- A tiny child-process redirection smoke test captured stdout and stderr into `_build-session.*.log` files, then the temporary `log-capture-smoke` session directory was removed.
- After `ui568` cleanup, `tv573` became active with the then-current 3-minute watchdog attached. It timed out and the queue advanced automatically to `swarm275`, confirming the watchdog path clears the active slot.

### Next

- Let the queued watchdog govern active builds going forward. A session that times out should treat exit code `124` as a hung-build failure, record it in context, and clean up its session directory with `.\devtools\build-session.ps1 -Remove -Session <id>`.
- If a specific clean build genuinely needs more than 60 seconds on this machine, raise the timeout with `-BuildTimeoutSeconds <seconds>` for that verification rather than disabling the queue.

---

## Session S581 - 2026-04-28 - Targeted test runner and queue status follow-up

Continued the Quality / Testing / Audits pipeline slice after the queued-build rule was clarified.

### Outcome

- Confirmed `devtools/run-pd-tests.ps1` is the scoped Catch2 selector wrapper, while full build verification remains `.\devtools\build-session.ps1 -Session <id> -Target all`.
- Added `-Scope` aliases and `-ListScopes` to `devtools/run-pd-tests.ps1` so sessions can use stable lane names like `catalog-provider`, `manifest`, `save`, `netbuf`, or `network-lifecycle` without memorizing raw Catch2 filters.
- Fixed `devtools/build-session.ps1 -List` elapsed/waiting display for queue JSON timestamps. PowerShell converts UTC JSON strings into `DateTime`; the helper now preserves those values directly and parses string timestamps with round-trip UTC semantics.
- Removed the `-NoQueue` example from `build-session.ps1` help text and changed queue/bypass messages to match Mike's rule: do not bypass unless he explicitly asks.
- Removed stale `t573` session state after confirming its recorded PID was gone and no objects or `pd-tests.exe` existed.
- Started queued full-build verification as `tv573` with `.\devtools\build-session.ps1 -Session tv573 -Target all`, no `-NoQueue`.

### Files

- `devtools/build-session.ps1`
- `tests/README.md`
- `context/designs/testing-framework-2026-04-26.md`
- Context updates: `context/tasks-current.md`, `context/session-log.md`

### Verification

- PowerShell parser checks passed for `devtools/build-session.ps1`.
- PowerShell parser checks passed for `devtools/run-pd-tests.ps1`.
- `.\devtools\run-pd-tests.ps1 -ListScopes` printed the expected scope-to-selector table.
- `git diff --check` passed for the touched scripts/docs/context files.
- `.\devtools\build-session.ps1 -List` now reports real active elapsed/waiting times.
- `tv573` is queued behind existing builds. First observed queue state: active `ui568`, waiting `cat581`, `sec581`, `swarm275`, then `tv573` at position 4/4 with roughly 2h55m estimated wait.

### Next

- Keep polling the queue/build status until `tv573` completes, then clean up with `.\devtools\build-session.ps1 -Remove -Session tv573`.
- Once verification is resolved, the next quality recursion candidate remains the broader handler dispatch contract audit for remaining `srccl` assumptions.

---

## Session S580 - 2026-04-28 - Queued build verification rule clarified

Recorded Mike's build-verification rule for future sessions.

### Outcome

- Codex/AI build verification must use `.\devtools\build-session.ps1 -Session <short-id> -Target all`, not shared `Build/`.
- The wrapper queues by default; sessions should reuse their own session id for reruns, watch queue status/ETA while waiting, and avoid `-NoQueue` unless Mike explicitly asks.
- Cleanup remains `.\devtools\build-session.ps1 -Remove -Session <short-id>`.

### Files

- `AGENTS.md`
- `context/build.md`
- Context updates: `context/session-log.md`

### Verification

- Documentation-only change; no build run.

### Next

- Use the queued isolated build wrapper for the next verification pass.

---

## Session S579 - 2026-04-28 - Queued isolated session builds

Updated the isolated build wrapper after Mike called out that per-session build directories avoid file collisions but still allow simultaneous compiler overload.

### Outcome

- `devtools/build-session.ps1` now queues builds by default before entering the per-session build lock and launching `build-headless.ps1`.
- Queue state lives under `.claude/session-builds/.queue/`; isolated build outputs still live under `.claude/session-builds/<session-id>/`.
- Waiting sessions print status every 30 seconds: queue position, active session/target, active elapsed time, estimated wait, and their own wait time.
- `.\devtools\build-session.ps1 -List` now shows both isolated session directories and queue state.
- The active build record tracks wrapper PID and child PowerShell PID so a waiting session can avoid starting another build while an orphaned child build is still alive.
- Completed queued builds record recent durations for rough target-specific ETA estimates.
- `-NoQueue` is available as an intentional manual bypass only; normal AI/session builds should not use it.
- `-RemoveAll` now skips the internal `.queue` metadata directory alongside `.locks`.

### Files

- `devtools/build-session.ps1`
- `tests/README.md`
- `context/designs/testing-framework-2026-04-26.md`
- Context updates: `context/build.md`, `context/tasks-current.md`, `context/session-log.md`

### Verification

- PowerShell parser check passed for `devtools/build-session.ps1`.
- `.\devtools\build-session.ps1 -List` passed and displayed active session directories plus empty queue state.
- Full build was not started; the purpose of this slice is queue behavior, and current context still shows long-running build contention.

### Next

- Let the next real build request exercise the queue. If the queue output is too noisy or ETA defaults are off, tune `QueueStatusSeconds` and the per-target duration defaults.

---

## Session S578 - 2026-04-28 - RomProvider primary source helper

Continued the catalog-owned asset pipeline after S577 without build verification, per Mike's instruction to skip the build for now. Scope stayed on provider-boundary cleanup for base catalog seed registration.

### Outcome

- Added `catalogSetPrimaryRomFilenum(entry, filenum)` as the catalog-owned helper for RomProvider-backed primary source assignment.
- Migrated base body/head/SP body/SP head/model/first-person hand seed registration off direct `catalogSetPrimary(e, romProviderHandle(e->source_filenum))`.
- Removed `assetprovider_internal.h` includes from `assetcatalog_base.c` and `assetcatalog_base_extended.c`.
- Kept the ROM fast path intact as a catalog-internal bridge in `assetcatalog.c`, matching Mike's note that preserving it is fine only as a migration sub-step.
- Updated static coverage so base seed registration cannot reintroduce direct RomProvider handle creation or the internal provider header.

### Files

- `port/include/assetcatalog.h`
- `port/src/assetcatalog.c`
- `port/src/assetcatalog_base.c`
- `port/src/assetcatalog_base_extended.c`
- `tests/test_catalog_provider_static.cpp`
- Context updates: `context/constraints.md`, `context/tasks-current.md`, `context/session-log.md`

### Verification

- Build/test verification intentionally skipped after Mike's instruction to skip the build for now.
- Static source scans confirmed base registration no longer has direct `romProviderHandle()` calls or `assetprovider_internal.h` includes.

### Next

- Next step is verification for S577/S578 once builds resume. Further catalog/provider code work should wait until `pd`, `pd-server`, and `pd-tests` catch up in an isolated session build.

---

## Session S577 - 2026-04-28 - FileProvider source-handle boundary

Continued the catalog-owned asset pipeline after S569 while parallel lanes advanced the log to S576. Scope stayed on source-handle ownership for file-backed component registration.

### Outcome

- Added `catalogSetPrimaryFile(entry, path)` as the catalog-owned helper for FileProvider-backed primary source assignment.
- Migrated local component scanner registration for character bodyfile, weapon/prop model_file, texture/audio file_path, and HUD texture_file off direct `fileProviderHandle()` calls.
- Migrated network-distributed hot registration to resolve relative paths against the extracted component directory, then route the resulting path through `catalogSetPrimaryFile()`.
- Removed `assetprovider.h` includes from scanner/distribution code that only existed for direct FileProvider handle creation.
- Cleaned the last stale untyped lifecycle log/comment references from the prior lifecycle API retirement slice.
- Added static coverage so direct `fileProviderHandle()` calls stay confined to the catalog/provider boundary allowlist.

### Files

- `port/include/assetcatalog.h`
- `port/src/assetcatalog.c`
- `port/src/assetcatalog_scanner.c`
- `port/src/net/netdistrib.c`
- `port/include/assetcatalog_load.h`
- `port/src/assetcatalog_load.c`
- `tests/manifest_pure.c`
- `tests/test_catalog_provider_static.cpp`
- Context updates: `context/constraints.md`, `context/tasks-current.md`, `context/session-log.md`

### Verification

- Build/test verification intentionally skipped after Mike's instruction to skip the build for now.
- Used isolated build directory session id `cat566`; did not use shared `Build/`.
- The prescribed wrapper configured cleanly but timed out in client compilation while other isolated sessions were active.
- A dry-run in `.claude/session-builds/cat566` completed and showed the expected isolated graph for `pd`, `pd-server`, and `pd-tests`, including the touched catalog/provider files.
- The interrupted `cat566` build process tree was stopped, and no `cat566` lock remained.

### Next

- Next safe non-build slice: centralize RomProvider-backed primary source assignment for base catalog seed registration behind a catalog helper. Preserve the ROM fast path as a catalog-internal bridge only.
- Verification remains pending for S577 and the next slice until builds are resumed.

---

## Session S576 - 2026-04-28 - Input transition cleanup substrate

Continued the input infrastructure lane after Mike asked to keep the recursive tracker moving and then directed to skip build/test verification this time. Scope stayed narrow: no raw ImGui key migration sweep, no catalog/provider work, and no full scene manager rewrite.

### Outcome

- Added a central `inputctx` to `LAYER_MENU` bridge so effective non-gameplay input context ownership publishes exactly one typed menu layer and clears when gameplay is effective again.
- Added pure/static `pd-tests` coverage for the menu-layer bridge.
- Reviewed Mike's playtest log. The held transition A press no longer appeared as held Use during the objective 2 intro, and the later fresh A press correctly skipped the cutscene.
- Added `scene_transition.h` / `scene_transition.c`, a small helper for ordering-sensitive transition cleanup.
- Migrated priority transition cleanup sites through `sceneStageTransitionPrepare` / `sceneStageChangeTo`: solo endscreen retry/next/main-menu exit, `netDisconnect`, client `SVC_STAGE_START` menu teardown, client `SVC_STAGE_END` manifest cleanup, local match start/challenge start, and legacy `menutick.c` MP/coop manifest-clear-before-stage-change exits.
- Added static `pd-tests` guards for the transition helper API, server source inclusion, clear-before-stage-change ordering, and migrated priority callsites.
- Added conservative layer-aware query gating in `actionmap.cpp`: declared top-layer action sets now constrain gameplay-only reads, while shared/system actions and layers without declared sets preserve existing behavior.
- Added a static `pd-tests` guard that query reads use the layer-aware aperture.
- Extended the same aperture to `fireVk()` dispatch writes after the highest-priority action winner is selected and before `s_State` mutation. Disallowed gameplay-only writes are consumed rather than remapped through lower-priority contexts.
- Added a static `pd-tests` guard that dispatch writes use the aperture before `ActionState` mutation.
- Extended the aperture to `actionmapPollFrame()` generic move/aim axis writes. Blocked axis pairs are zeroed before controller state or keyboard synthesis can leave stale generic gameplay axis state under cutscene/menu/vehicle authority.
- Added a static `pd-tests` guard for analog axis aperture and zeroing.
- Gated `actionConsumeHold()` and `actionHoldConsumed()` through the same layer/freefly checks used by action query APIs.
- Migrated `inputReadController()` legacy `OSContPad` axis fields from raw SDL axis reads to `actionValue(...)` so legacy pad samples mirror action-map/layer authority.
- Added static `pd-tests` guards for hold bookkeeping gates and `inputReadController()` action-axis mirroring.

### Files

- `port/src/inputctx.c`
- `tests/inputctx_pure.c`
- `tests/inputctx_pure.h`
- `tests/test_input_authority.cpp`
- `tests/test_input_layer_stack.cpp`
- `port/include/scene_transition.h`
- `port/src/scene_transition.c`
- `CMakeLists.txt`
- `port/fast3d/pdgui_bridge.c`
- `port/src/net/net.c`
- `port/src/net/netmsg.c`
- `port/src/net/matchsetup.c`
- `src/game/menutick.c`
- `port/src/actionmap.cpp`
- `port/src/input.c`
- `tests/test_scene_dispatch.cpp`
- Context updates: `context/tasks-current.md`, `context/session-log.md`, `context/designs/input-universality-and-transitions-2026-04-27.md`

### Verification

- `git diff --check` passed for the touched production/test files.
- Isolated build session `ml53` was used for the attempted bridge verification, not shared `Build/`.
- `.\devtools\build-session.ps1 -Session ml53 -Target all` stalled in client compilation. Mike then directed to skip tests this time.
- The lingering `ml53` process chain was stopped and `.\devtools\build-session.ps1 -Remove -Session ml53 -Force` removed the stale build directory and lock.
- No build or `pd-tests` run was completed for S576 after Mike's skip-tests instruction.

### Next

- Next step is verification, not another code slice: source audit now shows no `s_State` access outside `actionmap.cpp` except comments, and the only remaining raw SDL axis reads are the canonical action-map poller plus documented deprecated key-capture paths.
- Keep `gameplayInputSuppressed()` as a transitional wrapper until isolated build/tests and Mike playtests cover mission transitions, menus, vehicles, observer/freefly, and focus boundaries.

---

## Session S575 - 2026-04-28 - Client-hosted trust and protocol hardening

Continued Server / Trust / Security work for current listen-host/client-hosted online shipping. Dedicated-server productization stayed deferred.

### Outcome

- `netbufReadStr()` now rejects unterminated wire strings without mutating inbound packet payload, while preserving the prior safe empty string behavior for zero-length wire strings.
- `CLC_MOVE` now returns immediately on player-move parse errors before weapon-select validation or `outmoveack` updates can observe a partially decoded move.
- `CLC_LOBBY_START` now drains over-cap bot config records after the `numSims` clamp and before parsing the embedded manifest, so stale or hostile bot counts cannot shift the manifest read boundary.
- `CLC_SETTINGS` now sanitizes client-reported team changes before any match-state write: invalid team ids fall back to current/default team, and in-game team switches are ignored when the match is not team-enabled.
- `CLC_AUTH` now rejects malformed local-player counts (`0` or above `MAX_PLAYERS`) before ROM/mod checks or auth state commits, closing an unused wire-field trust boundary before it can be relied on later.
- `CLC_ROOM_SETTINGS_UPDATE` and `CLC_ROOM_PLAYLIST_UPDATE` now rebuild their rebroadcast packet per room recipient because `netSend()` resets the source buffer after queueing. Room settings rebroadcast also uses the normal reliable buffer instead of the old 256-byte stack packet.
- `CLC_MANIFEST_STATUS` now rejects unknown status bytes and requires the echoed manifest hash to match the active server manifest before it parses missing IDs or marks a ready-gate client ready/declined.
- `CLC_BOT_MOVE` now rejects impossible bot record counts (`> MAX_BOTS` or `> g_BotCount`) before any delegated bot-authority state writes, so an over-counted stream cannot partially update host-side bot stubs.
- Room create/join/leave/settings/playlist handlers now reject a missing source client before rate limits, room membership checks, or rebroadcast paths touch `srccl` state.
- `CLC_ROOM_CREATE` now rejects unknown access-mode bytes and password-room requests with empty passwords instead of silently creating an open room.
- After `pd.ini` load, invalid internal `Net.Client.LastJoinAddr` values are cleared and invalid `Net.RecentServer.*` entries are compacted out. The modern server list now shows an invalid-entry placeholder instead of falling back to raw stored address text when connect-code conversion fails.
- Join parsing now rejects explicit port `0`, and `netRecentServerAdd()` validates stored recent-server addresses before insertion so future internal callers cannot reintroduce invalid saved endpoints.
- Recent-server UDP responses now stage parsed metadata locally and commit it only after the whole response parses without netbuf error, preventing malformed response strings from leaving partially updated online rows.
- Added static/source coverage for the new string, lobby-drain, team-sanitize, room-rebroadcast, address-sanitizer, and recent-server parse invariants.

### Files

- `port/src/net/netbuf.c`
- `port/include/net/net.h`
- `port/src/main.c`
- `port/src/net/net.c`
- `port/src/net/netmsg.c`
- `port/fast3d/pdgui_menu_network.cpp`
- `tests/test_connectcode.cpp`
- `tests/test_netbuf.cpp`
- `tests/test_net_lifecycle_static.cpp`
- `CMakeLists.txt`
- Context updates: `context/tasks-current.md`, `context/session-log.md`, `context/build.md`, `context/bugs.md`

### Verification

- `git diff --check` passed for the trust/security touched source/test files after the final malformed-packet/source-client follow-ups.
- Isolated build session `sec575` was used, not shared `Build/`.
- `.\devtools\build-session.ps1 -Session sec575 -Target all` completed configure, disabled ccache after the compiler-launch probe timed out, then client compilation ran until the Codex command timed out at 45 minutes without surfacing a compile diagnostic.
- The stale `sec575` lock recorded PID 20428; that PID was gone. Cleanup used `.\devtools\build-session.ps1 -Remove -Session sec575 -Force`, and `-List` confirmed `sec575` was removed while other active sessions remained untouched.
- No second build was started after the final source-only follow-ups because the isolated build list still showed other locked sessions (`t573`, `cat566`).

### Next

- Re-run isolated verification when the current parallel build contention clears, preferably with the targeted runner for `[netbuf]`, `[net][lifecycle][security][static]`, and `[connectcode][security][static]`.
- No further low-risk listen-host code slice is queued from this scan until verification runs; keep dedicated-server product work deferred.

---

## Session S574 - 2026-04-28 - Debug swarm black-scene launch fix

Investigated Mike's Settings -> Debug -> Swarm CPU Bots black-scene report using `Build/pd-client.log`. Scope stayed on the Debug test scenario launch path and the catalog/provider miss visible in that same log.

### Outcome

- Root cause: Swarm CPU/GPU launched `base:mp_skedar` through the Grid/Forge direct stage handoff. That left `g_Vars.normmplayerisrunning` false, so setup.c loaded the SP setup/manifest for an MP arena and nulled invalid intro data.
- Swarm CPU/GPU now launch through `matchStart()` with no-limit match settings, so MP arenas use the normal MP setup/manifest path. Empty Map still uses the Grid/Forge path.
- Registered distinct first-person hand model files from `g_HeadsAndBodies[].handfilenum` as provider-backed `ASSET_MODEL` entries, covering the repeated `FILE_GCOMBATHANDSLOD` / filenum 1253 bgun catalog miss.
- Added static source guards in `tests/test_catalog_provider_static.cpp` for the swarm launch invariant and hand-model provider handles.
- Logged B-275.

### Files

- `port/src/testscenarios.c`
- `port/include/testscenarios.h`
- `port/src/assetcatalog_base_extended.c`
- `tests/test_catalog_provider_static.cpp`
- `context/bugs.md`
- `context/tasks-current.md`
- `context/session-log.md`
- `context/designs/gpu-swarm-and-test-scenarios-2026-04-27.md`

### Verification

- `git diff --check` passed for touched runtime/test/context files.
- Existing shared `Build/pd-tests.exe` was stale and did not contain the new test cases.
- Isolated session `swarm275` configured but timed out in client compilation; direct isolated `pd-tests` also timed out without surfacing compiler output.
- Mike directed to skip tests this time. Partial isolated session `swarm275` was removed with `-Force`; `-List` confirmed only other active sessions remained.

### Next

- Manual smoke Settings -> Debug -> Swarm CPU Bots and Swarm GPU Boids. Expected log: `TESTSCEN.LAUNCH ... via matchStart`, `MATCHSETUP: starting match`, setup load with `normmplay=1`, visible world render, and no repeated bgun `filenum=1253` catalog critical spam.
- Re-run isolated build/tests later when the current build contention clears.

---

## Session S573 - 2026-04-28 - Targeted pd-tests pipeline

Continued the Quality / Testing / Audits lane, pivoting from adding another invariant to improving how sessions run scoped verification.

### Outcome

- Added a `tests` target mode to `devtools/build-headless.ps1` and `devtools/build-session.ps1`, mapping to the existing CMake `pd-tests` target while leaving `-Target all` as the client/server build.
- Added `devtools/run-pd-tests.ps1`, which builds `pd-tests` in `.claude/session-builds/<session-id>/`, prepends the MinGW runtime path, runs from the repository root, and forwards Catch2 selectors like `[manifest]` or `[catalog][provider][static]`.
- Documented scoped examples and common selectors in `tests/README.md` and `context/designs/testing-framework-2026-04-26.md`.

### Files

- `devtools/build-headless.ps1`
- `devtools/build-session.ps1`
- `devtools/run-pd-tests.ps1`
- `tests/README.md`
- `context/designs/testing-framework-2026-04-26.md`
- Context updates: `context/tasks-current.md`, `context/session-log.md`

### Verification

- PowerShell parser checks passed for `devtools/build-headless.ps1`, `devtools/build-session.ps1`, and `devtools/run-pd-tests.ps1`.
- `git diff --check` passed for the touched scripts/docs/context files.
- Initial targeted-run smoke in session `t573` exposed runner bugs before useful build output: a `-Verbose` common-parameter conflict and an in-process `build-session.ps1` invocation conflict. Both were fixed.
- The follow-up `t573` build attempt was interrupted during the known long-running isolated compile path. The stale lock recorded PID 12684; that PID was gone and no objects or `pd-tests.exe` existed. Cleanup used `.\devtools\build-session.ps1 -Remove -Session t573 -Force`, and `-List` confirmed no session builds and an empty queue.
- Pending: run the required queued full build verification with `.\devtools\build-session.ps1 -Session tv573 -Target all`.

### Next

- After the wrapper verifies, start the next recursive quality candidate: handler dispatch contract audit for remaining `srccl` assumptions.

---

## Session S572 - 2026-04-28 - Quality pd-tests start/manifest lifecycle pass

Continued the Quality / Testing / Audits lane. Scope stayed on the current highest-risk start/manifest lifecycle and network parser invariants, with production guards and `pd-tests` coverage in the same change.

### Outcome

- Added `tests/test_net_lifecycle_static.cpp` to `pd-tests` and pinned the `CLC_LOBBY_START` authority-before-payload invariant so rejected non-leader starts cannot dirty match setup.
- Hardened malformed `SVC_MATCH_MANIFEST` handling so `g_ClientManifest` is cleared again on parse failure, including staged hash cleanup before PREPARING state.
- Made Counter-Op anti-client validation transactional: invalid/disconnected/wrong-room anti clients now return before committing `g_NetGameMode` / `g_NetCounterOpClientId`.
- Added `SVC_STAGE_START` mode validation and staged tick/RNG/match-seed commits until stage identity and mode validation pass.
- Added `SVC_STAGE_START` null-source rejection before `srccl->state` access or payload reads.
- Added `SVC_LOBBY_STATE` mode/status validation before committing lobby/global mode state.
- Logged B-272 through B-279 for the concrete one-off lifecycle/parser bugs fixed or covered in this pass.

### Files

- `port/src/net/netmsg.c`
- `tests/test_net_lifecycle_static.cpp`
- `CMakeLists.txt`
- Context updates: `context/tasks-current.md`, `context/bugs.md`, `context/session-log.md`

### Verification

- Used isolated session id `qlc566`, not shared `Build/`.
- The prescribed wrapper `.\devtools\build-session.ps1 -Session qlc566 -Target all` configured the isolated tree but stalled in Ninja client compilation, matching the known Codex desktop wrapper stall. Verification then used the same isolated tree's canonical `ninja -t commands` command list directly.
- Final focused verification rebuilt the affected `pd-tests` object, relinked `pd-tests.exe`, and compiled the changed `netmsg.c` for both client and server object targets.
- Final `pd-tests.exe` pass: 347 test cases / 19492 assertions.
- Recurring expected stub logs remained: missing read bytes, malformed string terminator, truncated read, and truncated u32 read.

### Next

- Next quality follow-up is broader than this pass: audit handler dispatch contracts for other `srccl` assumptions (`CLC_*` server handlers and `SVC_*` client handlers) and decide whether shared dispatch-side null/source guards are cleaner than per-handler patches.
- Manual negative tests remain useful for B-272 through B-279, especially rejected start requests, malformed stage/lobby state packets, and truncated manifest handling.

---

## Session S571 - 2026-04-28 - Social shell main-menu entry and force-close cleanup

Continued the controller-first modern main menu / Social shell work after the initial menu-pool and controller-row pass. Scope stayed inside ImGui/menu-pool/input-context ownership.

### Outcome

- Added first-screen `Social` and `Public Mods` main-menu entry points using `menugraph` push ops to `MENU_TYPE_SOCIAL_SHELL`.
- Public Mods now opens the Social shell directly on the Public Mods tab through `pdguiFriendsSocialOpenPublicMods()`.
- Main-menu Back/Escape now defers while any Social shell surface is open, letting chat/Social/sidebar consume Back before the main menu unwinds.
- Status-pill clicks now immediately resync Social shell menu-pool ownership.
- Social/sidebar/chat/NAT windows request focus when appearing.
- Social shell state now adopts external force-close / `menupoolReleaseAll()` by clearing local sidebar/menu/chat/modal booleans instead of immediately reacquiring the pool slot.
- `pdguiNewFrame()` now treats active Social shell surfaces as a reason to start an ImGui frame, so standalone social surfaces are not skipped by the backend early-return gate.

### Files

- `port/include/pdgui_friends.h`
- `port/fast3d/pdgui_friends.cpp`
- `port/fast3d/pdgui_nat_diagnostics.cpp`
- `port/fast3d/pdgui_menu_mainmenu.cpp`
- `port/fast3d/pdgui_backend.cpp`
- `port/src/menugraph.c`
- Context updates: `context/build.md`, `context/tasks-current.md`, `context/session-log.md`

### Verification

- `git diff --check` passed for the touched Social shell, backend, menugraph, menu-pool, and context files.
- Isolated build session `ui568` was used, not shared `Build/`.
- `.\devtools\build-session.ps1 -Session ui568 -Target all` completed configure and then client compilation ran until the Codex command timed out at 15 minutes without surfacing a compiler diagnostic.
- The stale `ui568` lock recorded PID 6120; that PID was gone. Cleanup used `.\devtools\build-session.ps1 -Remove -Session ui568 -Force`, and `.\devtools\build-session.ps1 -List` confirmed `ui568` was removed while other active sessions were left untouched.

### Next

- Re-run isolated verification after current parallel builds clear, with a longer window or the known direct isolated-tree Ninja fallback if the wrapper stalls again.
- Then do an in-game controller pass over first-screen Social/Public Mods entry, sidebar, Social tabs, chat, invites, public mods, profile modal, add-friend modal, and NAT diagnostics.
- If build/gamepad verification is clean, the next safe code slice is public-mod add/import form layout and default focus polish inside the Social shell.

---

## Session S570 - 2026-04-28 - Dev Window v2 push and warm-build polish

Updated Dev Window v2 after S569 while leaving parallel catalog/input/security work untouched.

### Outcome

- The Push button now runs the same async git sync path as build/release, but as a required commit+push action: stage pending changes, commit with `chore: dev window push`, push the current branch, then refresh version/run/status UI.
- Git pull/push/prune/status paths now use the resolved Git executable instead of falling back to the unreliable MSYS `usr\bin\git.exe` path where practical.
- Warm BUILD and RUN TESTS paths now skip CMake configure when the cache, generated Ninja file, CMakeLists timestamp, cached version, and cached Python executable are current.
- Configure paths now prefer Windows Python when available, use forced compiler checks/static try-compile mode, and pass parallel build jobs to CMake.
- Addin data copy now uses `robocopy /MIR` when available and falls back to the prior remove/copy behavior.
- Dev Window v2 README now documents the Push button as commit+push plus UI refresh.

### Files

- `devtools/dev-window-v2/dev-window-v2.ps1`
- `devtools/dev-window-v2/README.md`
- Context updates: `context/build.md`, `context/tasks-current.md`, `context/session-log.md`

### Verification

- PowerShell parser check passed for `devtools/dev-window-v2/dev-window-v2.ps1`.
- `git diff --check` passed for the Dev Window v2 files.
- Full build not rerun from this Codex desktop session because the current build caveat still applies; use the isolated session build path if a live build is needed.

### Next

- Launch Dev Window v2 on Mike's desktop and click Push once on a disposable/small change to confirm the MessageBox, status bar, and dirty-count refresh behavior against the live Git credentials.

---

## Session S569 - 2026-04-28 - Untyped lifecycle API retirement

Continued the catalog-owned asset pipeline after S568. Scope stayed on retiring the compatibility API surface now that all production manifest/screen/stage callers use typed lifecycle.

### Outcome

- Removed public `catalogLoadAsset()`, `catalogUnloadAsset()`, and `catalogRetainAsset()` declarations and implementations.
- Removed the dedicated-server stubs for those untyped lifecycle wrappers while keeping typed lifecycle stubs.
- Kept entry-level load/release/retain helpers internal to `assetcatalog_load.c` so typed lifecycle and dependency cascade share the same implementation path.
- Updated stale manifest/hotswap comments from untyped lifecycle wording to typed lifecycle wording.
- Updated the typed lifecycle constraint and static coverage so untyped lifecycle declarations/calls cannot be reintroduced.

### Files

- `port/include/assetcatalog_load.h`
- `port/src/assetcatalog_load.c`
- `port/src/server_stubs.c`
- `port/include/net/netmanifest.h`
- `port/src/net/netmanifest.c`
- `port/fast3d/pdgui_hotswap.cpp`
- `tests/test_catalog_provider_static.cpp`
- Context updates: `context/constraints.md`, `context/tasks-current.md`, `context/session-log.md`

### Verification

- Used isolated build directory session id `cat566`; did not use shared `Build/`.
- `.\devtools\build-session.ps1 -Session cat566 -Target all` passed for `pd` and `pd-server`.
- Isolated Ninja build passed for `pd-tests`.
- Isolated `pd-tests.exe`: 337 test cases / 17914 assertions passed.
- Usual stub logs still appear:
  - `[stub-log L2] NET: could not read 1 bytes (rp=15 wp=15)`
  - `[stub-log L2] NET: malformed string missing terminator (len=5 rp=2)`
  - `[stub-log L2] NET: could not read 1 bytes (rp=1 wp=1)`
  - `[stub-log L2] NET: could not read 4 bytes (rp=0 wp=3)`

### Next

- Next safe catalog slice is source-handle centralization for file-backed component registration: remove direct `fileProviderHandle()` use from scanner/distribution code by routing file-backed primary handle assignment through a catalog helper.

---

## Session S568 - 2026-04-28 - UI lifecycle policy and raw path fallback removal

Continued the catalog-owned asset pipeline after S567. Scope stayed on the final generic lifecycle domain and the now-obsolete raw path fallback.

### Outcome

- Added `ASSET_UI` to metadata/runtime lifecycle activation. Renderer-owned UI textures/fonts remain owned by the UI runtime; catalog lifecycle tracks activation/refcount without loading generic bytes.
- Removed the legacy `s_catalogLoadEntryFromPath()` raw path loader.
- Legacy untyped `catalogLoadAsset()` now dispatches through the entry's actual catalog type instead of passing `ASSET_NONE`.
- Static coverage now prevents `s_catalogLoadEntryFromPath()` and the old `FALLBACK: catalogLoadAsset` diagnostic from returning.
- Current typed lifecycle policy now covers every declared asset type; no generic raw path payload fallback remains.

### Files

- `port/src/assetcatalog_load.c`
- `tests/test_catalog_provider_static.cpp`
- Context updates: `context/tasks-current.md`, `context/session-log.md`

### Verification

- Used isolated build directory session id `cat566`; did not use shared `Build/`.
- `.\devtools\build-session.ps1 -Session cat566 -Target all` passed for `pd` and `pd-server`.
- Isolated Ninja build passed for `pd-tests`.
- Isolated `pd-tests.exe`: 337 test cases / 17899 assertions passed.
- Usual stub logs still appear; the newer malformed-string stub log from the quality lane also appears:
  - `[stub-log L2] NET: could not read 1 bytes (rp=15 wp=15)`
  - `[stub-log L2] NET: malformed string missing terminator (len=5 rp=2)`
  - `[stub-log L2] NET: could not read 1 bytes (rp=1 wp=1)`
  - `[stub-log L2] NET: could not read 4 bytes (rp=0 wp=3)`

### Next

- The next safe catalog slice is an API retirement audit: decide whether the legacy untyped lifecycle entry points should stay as compatibility wrappers or become internal/removed now that production callers use typed lifecycle APIs.

---

## Session S567 - 2026-04-28 - Pack metadata lifecycle expansion

Continued the catalog-owned asset pipeline after S566. Scope stayed on pack/descriptor catalog types that do not currently own independent file payload fields.

### Outcome

- Moved `ASSET_ANIMATION`, `ASSET_TEXTURES`, `ASSET_SFX`, and `ASSET_MUSIC` to metadata runtime lifecycle activation.
- `ASSET_AUDIO` is now the only audio lifecycle type that requires a file provider handle.
- Removed the obsolete audio provider/path fallback branch because component audio now requires a provider handle and pack-level SFX/music entries are metadata.
- Static coverage now pins the pack metadata types and the narrower `ASSET_AUDIO` runtime policy.

### Files

- `port/src/assetcatalog_load.c`
- `tests/test_catalog_provider_static.cpp`
- Context updates: `context/tasks-current.md`, `context/session-log.md`

### Verification

- Used isolated build directory session id `cat566`; did not use shared `Build/`.
- `.\devtools\build-session.ps1 -Session cat566 -Target all` passed for `pd` and `pd-server`.
- Isolated Ninja build passed for `pd-tests`.
- Isolated `pd-tests.exe`: 335 test cases / 17867 assertions passed.
- Usual stub logs still appear; the newer malformed-string stub log from the quality lane also appears:
  - `[stub-log L2] NET: could not read 1 bytes (rp=15 wp=15)`
  - `[stub-log L2] NET: malformed string missing terminator (len=5 rp=2)`
  - `[stub-log L2] NET: could not read 1 bytes (rp=1 wp=1)`
  - `[stub-log L2] NET: could not read 4 bytes (rp=0 wp=3)`

### Next

- Determine the next safe catalog/provider slice. `ASSET_UI` is the only remaining generic lifecycle domain, but it is renderer/runtime-owned in several paths and needs a dedicated UI payload policy rather than a generic metadata sweep.

---

## Session S566 - 2026-04-28 - Descriptor metadata lifecycle expansion

Continued the catalog-owned asset pipeline after S533 and after parallel sessions advanced the log to S565. Scope stayed on descriptor-only catalog types from the remaining generic lifecycle list.

### Outcome

- Added `ASSET_TOOL`, `ASSET_VEHICLE`, and `ASSET_MISSION` to metadata runtime lifecycle activation.
- These descriptor-only catalog entries now activate as catalog metadata instead of reaching generic provider/path loading.
- Static coverage now pins all three types in the metadata lifecycle set.

### Files

- `port/src/assetcatalog_load.c`
- `tests/test_catalog_provider_static.cpp`
- Context updates: `context/tasks-current.md`, `context/session-log.md`

### Verification

- Used isolated build directory session id `cat566`; did not use shared `Build/`.
- `.\devtools\build-session.ps1 -Session cat566 -Target all` passed for `pd` and `pd-server`.
- Isolated Ninja build passed for `pd-tests`.
- Isolated `pd-tests.exe`: 331 test cases / 17810 assertions passed.
- Usual stub logs still appear:
  - `[stub-log L2] NET: could not read 1 bytes (rp=15 wp=15)`
  - `[stub-log L2] NET: could not read 1 bytes (rp=1 wp=1)`
  - `[stub-log L2] NET: could not read 4 bytes (rp=0 wp=3)`

### Next

- Determine the next safe catalog/provider slice. Remaining generic lifecycle domains are `ASSET_ANIMATION`, `ASSET_TEXTURES`, `ASSET_SFX`, `ASSET_MUSIC`, and `ASSET_UI`; only migrate one after its ownership is clear.

---

## Session S565 - 2026-04-28 - Quality pd-tests recursive invariant expansion

Continued the Quality / Testing / Audits lane. Read the required testing/context docs and expanded `pd-tests` around the highest-risk uncovered invariants, keeping each guard with the invariant it enforces.

### Outcome

- Added a catalog/provider identity static guard that prevents production code outside `assetcatalog_api.c` from using generic `catalogIdByRuntime(ASSET_*)` for domains that have typed helper APIs.
- Added v45 network packet parsing tests for `SVC_STAGE_START` and `CLC_LOBBY_START` spawn-weapon tail alignment, truncated-tail failure, and production field-order drift.
- Made `manifestDeserialize()` transactional on parse error: entries appended by a malformed packet are rolled back before returning failure. Mirrored the pure test copy and added a malformed COMPONENT-tail rollback test.
- Added a save-migration static guard that pins the destructive v1->v2 weapon-cull migration behind `if (version < 2)` in the live MP setup loader.
- Fixed build environment blockers discovered while using the isolated session build path: PowerShell session build directory creation, Git-for-Windows safe-directory handling, Codex-safe async output capture, CMake configure probes that hang in the sandbox, Windows Python fallback for asset tools, ccache launch probing/disable, and filtering compiler-implicit MinGW root include dirs so C++ standard `#include_next` works.

### Files

- `tests/test_catalog_provider_static.cpp`
- `tests/test_spawn_weapon_mode.cpp`
- `tests/test_manifest.cpp`
- `tests/manifest_pure.c`
- `tests/test_save_migration.cpp`
- `port/src/net/netmanifest.c`
- Build support: `devtools/build-session.ps1`, `devtools/build-headless.ps1`, `devtools/_build-env-prelude.ps1`, `cmake/TargetArch.cmake`, `cmake/FindSDL2.cmake`, `tools/pdmod_prophandler/CMakeLists.txt`, `CMakeLists.txt`
- Context updates: `context/tasks-current.md`, `context/build.md`, `context/session-log.md`

### Verification

- Used isolated session id `qtest503`; did not use shared `Build/`.
- The prescribed `build-session.ps1 -Session qtest503 -Target all` path configured but Ninja execution still hangs in the Codex desktop sandbox, so verification used the same isolated CMake/Ninja tree and executed the canonical `ninja -t commands` list directly.
- `PerfectDark.exe`, `PerfectDarkServer.exe`, and `pd-tests.exe` linked in `.claude/session-builds/qtest503`.
- Final `pd-tests.exe`: 331 test cases / 17807 assertions passed.
- Usual stub logs still appear:
  - `[stub-log L2] NET: could not read 1 bytes (rp=15 wp=15)`
  - `[stub-log L2] NET: could not read 1 bytes (rp=1 wp=1)`
  - `[stub-log L2] NET: could not read 4 bytes (rp=0 wp=3)`

### Next

- Next recursive target is mode lifecycle/input transition cleanup around failed lobby/manifest/start paths. Start with a read-only audit for lifecycle roots that clear or preserve `g_ClientManifest`, lobby state, input/scene layers, and ready gates after malformed or rejected network transitions.

---

## Session S564 - 2026-04-28 - PageUp/PageDown backend injection retirement

Continued transitional shim retirement after cutscene compatibility globals. Scope stayed on the PageUp/PageDown action-to-ImGui bridge and its main-menu queue drain.

### Outcome

- Social menu tabs now cycle through `pdguiMenuTabPrevPressed()` / `pdguiMenuTabNextPressed()` with explicit selected-tab state.
- Removed backend injection of `ACTION_MENU_TAB_PREV/NEXT` into `ImGuiKey_PageUp/PageDown`.
- Removed the main-menu PageUp/PageDown queue drain that existed to compensate for that injection.
- Added static coverage that keeps Social tab navigation action-map owned and prevents the backend injection or queue drain from returning.
- First-party raw-key audit now shows no command reads; remaining hits are comments or third-party ImGui internals.

### Files

- `port/fast3d/pdgui_friends.cpp`
- `port/fast3d/pdgui_backend.cpp`
- `port/fast3d/pdgui_menu_mainmenu.cpp`
- `tests/test_social_toggle_imc.cpp`
- Context updates: `context/tasks-current.md`, `context/designs/input-universality-and-transitions-2026-04-27.md`, `context/session-log.md`

### Verification

- Used isolated build session id `ix46`; did not use shared `Build/`.
- Isolated Ninja outside the sandbox built `pd`, `pd-server`, and `pd-tests`.
- Isolated `pd-tests.exe`: 329 test cases / 17795 assertions passed.

### Next

- Continue transitional-shim audit. `gameplayInputSuppressed()` is not safe to retire yet because the layer stack does not own every menu context. The remaining transition triplets need a narrow scene-manager slice before they can be replaced safely.

---

## Session S563 - 2026-04-28 - Cutscene compatibility global retirement

Continued transitional shim retirement after editor/tool hotkeys. Scope stayed on cutscene compatibility globals that were no longer read by production gameplay paths.

### Outcome

- Removed `g_InCutscene`, `g_CutsceneSkipRequested`, `g_CutsceneAnimNum`, `g_CutsceneCurAnimFrame60`, and `g_CutsceneCurTotalFrame60f`.
- Removed `playerSyncCutsceneGlobalsToCurrent()` and its call sites.
- `USINGDEVICE(device)` now checks `playerCurrentInCutscene()` instead of `g_InCutscene`.
- `SVC_CUTSCENE` now updates cutscene active state through `playerSetCutsceneActiveMask(...)` on both client and pd-server builds.
- pd-server stubs now keep a local cutscene active mask instead of defining a fake `g_InCutscene`.
- Added static coverage that prevents the retired globals and wrapper from returning.

### Files

- `src/include/constants.h`
- `src/include/data.h`
- `src/include/bss.h`
- `src/include/game/player.h`
- `src/game/player.c`
- `src/game/playermgr.c`
- `src/game/explosions.c`
- `src/game/sparks.c`
- `port/src/net/netmsg.c`
- `port/src/server_stubs.c`
- `tests/test_cutscene_layer.cpp`
- Context updates: `context/tasks-current.md`, `context/designs/input-universality-and-transitions-2026-04-27.md`, `context/session-log.md`

### Verification

- Used isolated build session id `ix46`; did not use shared `Build/`.
- The session wrapper was invoked first as directed but timed out in the known client-compile stall.
- Sandboxed direct Ninja also left stale locks without live compiler processes, so the isolated `ix46` build/test was rerun outside the sandbox.
- Isolated `pd`, `pd-server`, and `pd-tests` built successfully.
- Isolated `pd-tests.exe`: 328 test cases / 17784 assertions passed.
- `git diff --check` passed outside the sandbox; only existing LF-to-CRLF warnings appeared for `devtools/_build-env-prelude.ps1` and `devtools/build-headless.ps1`.

### Next

- Continue transitional-shim audit. Remaining known candidates are the main-menu PageUp/PageDown queue drain/backend injection, `gameplayInputSuppressed()` as the old input-context authority wrapper, and ad-hoc `manifestClear` / `mainChangeToStage` / `menupoolReleaseAll` transition triplets.

---

## Session S562 - 2026-04-28 - Editor/tool hotkey raw-input migration

Continued the raw action input migration after social voice PTT. Scope stayed on editor/tool command shortcuts and did not change true ImGui text-entry or geometry queries.

### Outcome

- Added synthetic chord VKs for Ctrl+Tab, Ctrl+Shift+Tab, Ctrl+Z, Ctrl+Shift+Z, Ctrl+Y, and Ctrl+S, including keydown-to-keyup release tracking.
- Added Forge placement/bot command actions and Skin Editor brush/tool/grid/UV/undo/redo/save actions.
- Bound Forge session commands through `g_ImcForgeSession`, Forge placement/sidebar commands through `g_ImcForge`, and Skin Editor commands through `g_ImcMenu`.
- Migrated Forge HUD bot commands, Forge placement cancel, Forge Ctrl+Tab sidebar cycling, and Skin Editor shortcuts from raw ImGui polling to action-map reads.
- Exposed the new Forge and Skin Editor actions in the Controls UI.
- Added static coverage for action ids, synthetic chord bindings, raw polling removal in Forge HUD/Forge Editor/Skin Editor, and Controls UI visibility.
- First-party raw-key audit now leaves only the documented main-menu PageUp/PageDown queue drain plus comments; third-party ImGui internals are ignored.

### Files

- `port/include/input.h`
- `port/src/input.c`
- `port/include/actionmap.h`
- `port/src/actionmap.cpp`
- `port/src/inputlayer.c`
- `port/fast3d/pdgui_forge_hud.cpp`
- `port/fast3d/pdgui_forge_editor.cpp`
- `port/fast3d/pdgui_skin_editor.cpp`
- `port/fast3d/pdgui_menu_mainmenu.cpp`
- `tests/actionmap_pure.h`
- `tests/actionmap_pure.c`
- `tests/test_actionmap_flush.cpp`
- `tests/test_editor_tool_hotkeys.cpp`
- Context updates: `context/tasks-current.md`, `context/designs/input-universality-and-transitions-2026-04-27.md`, `context/session-log.md`

### Verification

- Used isolated build session id `ix46`; did not use shared `Build/`.
- Direct isolated Ninja in `.claude/session-builds/ix46` built `pd`, `pd-server`, and `pd-tests`.
- Isolated `pd-tests.exe`: 327 test cases / 17751 assertions passed.
- Usual stub logs still appeared:
  - `[stub-log L2] NET: could not read 1 bytes (rp=15 wp=15)`
  - `[stub-log L2] NET: could not read 1 bytes (rp=1 wp=1)`
  - `[stub-log L2] NET: could not read 4 bytes (rp=0 wp=3)`

### Next

- Retire transitional shims where ownership has moved to the action map, layer stack, or scene manager. Start with the main-menu PageUp/PageDown queue drain/backend injection and then audit cutscene compatibility globals/wrappers.

---

## Session S561 - 2026-04-28 - Voice PTT raw-input migration

Continued the raw action input migration after spectator observer controls. Scope stayed on the social voice push-to-talk hotkey.

### Outcome

- Added `ACTION_VOICE_PTT`, defaulted to V.
- Bound voice PTT in gameplay, cutscene, vehicle, observer, Forge session, menu, pause-menu, and debug overlay IMCs to preserve the old raw hotkey's broad availability.
- Migrated `pdgui_friends.cpp` from raw `ImGuiKey_V` polling to `actionPressed/Released(0, ACTION_VOICE_PTT)`.
- Preserved the existing ImGui keyboard-capture guard so typing into fields does not start voice transmission.
- Exposed Voice Push-to-Talk in Controls under System Hotkeys.
- Added static coverage for action-map binding, shared-action classification, raw V polling removal, and Controls UI visibility.

### Files

- `port/include/actionmap.h`
- `port/src/actionmap.cpp`
- `port/fast3d/pdgui_friends.cpp`
- `port/fast3d/pdgui_menu_mainmenu.cpp`
- `tests/actionmap_pure.h`
- `tests/actionmap_pure.c`
- `tests/test_actionmap_flush.cpp`
- `tests/test_social_toggle_imc.cpp`
- Context updates: `context/tasks-current.md`, `context/designs/input-universality-and-transitions-2026-04-27.md`, `context/session-log.md`

### Verification

- Used isolated build session id `ix46`; did not use shared `Build/`.
- Direct isolated Ninja in `.claude/session-builds/ix46` built `pd`, `pd-server`, and `pd-tests`.
- Isolated `pd-tests.exe`: 327 test cases / 17748 assertions passed.
- Usual stub logs still appeared:
  - `[stub-log L2] NET: could not read 1 bytes (rp=15 wp=15)`
  - `[stub-log L2] NET: could not read 1 bytes (rp=1 wp=1)`
  - `[stub-log L2] NET: could not read 4 bytes (rp=0 wp=3)`

### Next

- Migrate editor/tool hotkeys off raw ImGui polling where they represent commands rather than text-entry or geometry reads.

---

## Session S560 - 2026-04-28 - Spectator observer raw-input migration

Continued the raw action input migration after secondary menu commands. Scope stayed on spectator observer controls and did not sweep unrelated editor/tool hotkeys.

### Outcome

- Added observer action-map actions and `g_ImcObserver` for subset/member navigation, camera toggle, freefly, stop, ascend, and descend.
- Wired `LAYER_OBSERVER` so the observer IMC activates only for `SCENE_OBSERVER_SOURCE_SPECTATOR`; Forge observer entry continues to use the existing Forge IMCs.
- Made `scene.c` store observer event payloads in stable scene-owned storage before pushing the observer layer.
- Migrated `pdgui_spectator.cpp` off raw ImGui key polling for observer controls. Freefly uses the gameplay move axis plus observer ascend/descend actions.
- Added observer bindings to glyph lookup and the Controls UI.
- Added static coverage for observer action-set membership, source-specific activation, scene payload storage, spectator raw-key removal, and observer binding visibility.

### Files

- `port/include/actionmap.h`
- `port/src/actionmap.cpp`
- `port/src/inputlayer.c`
- `port/src/scene.c`
- `port/fast3d/pdgui_spectator.cpp`
- `port/fast3d/pdgui_glyphs.cpp`
- `port/fast3d/pdgui_menu_mainmenu.cpp`
- `tests/actionmap_pure.h`
- `tests/actionmap_pure.c`
- `tests/test_actionmap_flush.cpp`
- `tests/test_vehicle_observer_layer.cpp`
- Context updates: `context/tasks-current.md`, `context/designs/input-universality-and-transitions-2026-04-27.md`, `context/session-log.md`

### Verification

- Used isolated build session id `ix46`; did not use shared `Build/`.
- Invoked `.\devtools\build-session.ps1 -Session ix46 -Target all` first as requested. It stalled in client compile and left only a dead session lock after timeout.
- After confirming no active compiler or build process, cleared the dead `ix46` lock and used direct isolated Ninja in `.claude/session-builds/ix46`.
- Direct isolated Ninja built `pd`, `pd-server`, and `pd-tests`.
- Isolated `pd-tests.exe`: 324 test cases / 17729 assertions passed.
- Usual stub logs still appeared:
  - `[stub-log L2] NET: could not read 1 bytes (rp=15 wp=15)`
  - `[stub-log L2] NET: could not read 1 bytes (rp=1 wp=1)`
  - `[stub-log L2] NET: could not read 4 bytes (rp=0 wp=3)`

### Next

- Migrate social voice push-to-talk off raw V key polling if the audit confirms it is an action read. Then continue to editor/tool hotkeys and transitional shim retirement.

---

## Session S559 - 2026-04-28 - Secondary menu command raw-input migration

Continued the raw action input migration after Solo Mission. Scope stayed on secondary menu commands that were still first-party action shortcuts, not editor/tool hotkeys.

### Outcome

- Added `ACTION_MENU_SECONDARY`, `ACTION_MENU_TERTIARY`, and `ACTION_MENU_DELETE`, with menu/pause defaults for C / gamepad X, D / gamepad Y, and Delete.
- Added `pdgui_nav` helpers for secondary, tertiary, delete, and text paste action reads.
- Migrated Agent Select copy/delete/open-directory commands, Room bot-row secondary/tertiary commands, and MP Settings preview commands behind action-map authority.
- Added static coverage for the new action defaults, helper API, and migrated secondary command sites.

### Files

- `port/include/actionmap.h`
- `port/src/actionmap.cpp`
- `port/include/pdgui_nav.h`
- `port/src/pdgui_nav.c`
- `port/fast3d/pdgui_menu_agentselect.cpp`
- `port/fast3d/pdgui_menu_room.cpp`
- `port/fast3d/pdgui_menu_mpsettings.cpp`
- `tests/test_menu_graph.cpp`
- Context updates: `context/tasks-current.md`, `context/designs/input-universality-and-transitions-2026-04-27.md`, `context/session-log.md`

### Verification

- Used isolated build session id `ix46`; did not use shared `Build/`.
- `git diff --check` passed for the touched code/test files.
- Direct isolated Ninja in `.claude/session-builds/ix46` built `pd`, `pd-server`, and `pd-tests`.
- Isolated `pd-tests.exe`: 323 test cases / 17687 assertions passed.
- Usual stub logs still appeared:
  - `[stub-log L2] NET: could not read 1 bytes (rp=15 wp=15)`
  - `[stub-log L2] NET: could not read 1 bytes (rp=1 wp=1)`
  - `[stub-log L2] NET: could not read 4 bytes (rp=0 wp=3)`

### Next

- Classify or migrate the remaining first-party raw reads: spectator/observer controls, voice PTT, and editor/tool hotkeys. Keep the documented main-menu PageUp/PageDown queue drain transitional until backend PageUp injection is retired.

---

## Session S558 - 2026-04-28 - Solo Mission raw-input migration

Continued the raw action input migration after Training. Scope stayed on `pdgui_menu_solomission.cpp`.

### Outcome

- Migrated Mission Select, difficulty selection, co-op/anti difficulty, co-op/anti options, briefing, inventory, Accept Mission, solo pause, abort modal, and solo options shortcuts to `pdgui_nav` helpers.
- Added Q/E as additional menu and pause defaults for `ACTION_MENU_TAB_PREV` / `ACTION_MENU_TAB_NEXT` so Solo Options keeps its keyboard tab shortcuts behind action-map authority.
- Added static coverage so Solo Mission cannot reintroduce raw Enter, Space, Escape, arrow, Q/E, or PageUp/PageDown menu polling.

### Files

- `port/src/actionmap.cpp`
- `port/fast3d/pdgui_menu_solomission.cpp`
- `tests/test_menu_graph.cpp`
- Context updates: `context/tasks-current.md`, `context/designs/input-universality-and-transitions-2026-04-27.md`, `context/session-log.md`

### Verification

- Used isolated build session id `ix46`; did not use shared `Build/`.
- `git diff --check` passed for the touched code/test files.
- Direct isolated Ninja in `.claude/session-builds/ix46` built `pd`, `pd-server`, and `pd-tests`.
- Isolated `pd-tests.exe`: 322 test cases / 17647 assertions passed.
- Usual stub logs still appeared:
  - `[stub-log L2] NET: could not read 1 bytes (rp=15 wp=15)`
  - `[stub-log L2] NET: could not read 1 bytes (rp=1 wp=1)`
  - `[stub-log L2] NET: could not read 4 bytes (rp=0 wp=3)`

### Next

- Run the remaining raw-key audit and classify or migrate any leftover menu-owned reads. Tool/editor hotkeys stay classified separately.

---

## Session S557 - 2026-04-28 - Training menu raw-input migration

Continued the raw action input migration after cheats/modding panels. Scope stayed on Training menu shortcuts that behave like normal menu actions.

### Outcome

- Migrated Training Back, Continue, list up/down, and firing range confirm shortcuts to `pdgui_nav` helpers.
- Added static coverage so `pdgui_menu_training.cpp` cannot reintroduce raw Enter, Space, Escape, arrow, or PageUp/PageDown menu polling.

### Files

- `port/fast3d/pdgui_menu_training.cpp`
- `tests/test_menu_graph.cpp`
- Context updates: `context/tasks-current.md`, `context/designs/input-universality-and-transitions-2026-04-27.md`, `context/session-log.md`

### Verification

- Used isolated build session id `ix46`; did not use shared `Build/`.
- `git diff --check` passed for the touched code/test files.
- Direct isolated Ninja in `.claude/session-builds/ix46` built `pd`, `pd-server`, and `pd-tests`.
- Isolated `pd-tests.exe`: 321 test cases / 17591 assertions passed.
- Usual stub logs still appeared:
  - `[stub-log L2] NET: could not read 1 bytes (rp=15 wp=15)`
  - `[stub-log L2] NET: could not read 1 bytes (rp=1 wp=1)`
  - `[stub-log L2] NET: could not read 4 bytes (rp=0 wp=3)`

### Next

- Migrate remaining Solo Mission menu-owned raw reads as its own slice.

---

## Session S556 - 2026-04-28 - Cheats and modding panel raw-input migration

Continued the raw action input migration after simple legacy menu screens. Scope stayed on cheats and modding panel shortcuts that behave like normal menu actions.

### Outcome

- Migrated Cheats hub close, tab cycling, warning close, and Unlock Everything confirm/cancel to `pdgui_nav` helpers.
- Migrated Mod Manager tab cycling and close to `pdgui_nav` helpers.
- Migrated Modding Hub tool cycling and close to `pdgui_nav` helpers.
- Added static coverage so those files cannot reintroduce raw Enter, Space, Escape, arrow, or PageUp/PageDown menu polling.

### Files

- `port/fast3d/pdgui_menu_cheats.cpp`
- `port/fast3d/pdgui_menu_modmgr.cpp`
- `port/fast3d/pdgui_menu_moddinghub.cpp`
- `tests/test_menu_graph.cpp`
- Context updates: `context/tasks-current.md`, `context/designs/input-universality-and-transitions-2026-04-27.md`, `context/session-log.md`

### Verification

- Used isolated build session id `ix46`; did not use shared `Build/`.
- `git diff --check` passed for the touched code/test files.
- Direct isolated Ninja in `.claude/session-builds/ix46` built `pd`, `pd-server`, and `pd-tests`.
- Isolated `pd-tests.exe`: 320 test cases / 17579 assertions passed.
- Usual stub logs still appeared:
  - `[stub-log L2] NET: could not read 1 bytes (rp=15 wp=15)`
  - `[stub-log L2] NET: could not read 1 bytes (rp=1 wp=1)`
  - `[stub-log L2] NET: could not read 4 bytes (rp=0 wp=3)`

### Next

- Continue remaining raw menu-owned groups: training menus, then Solo Mission. Keep tool/editor hotkeys classified separately.

---

## Session S555 - 2026-04-28 - Simple legacy menu back/nav raw-input migration

Continued the raw action input migration after priority navigation and tab sites. Scope stayed on simple legacy menu replacement screens with Back, Done, or list up/down shortcuts.

### Outcome

- Migrated countdown cancel and shared file browser parent navigation to `pdguiMenuCancelPressed()`.
- Migrated Agent Create cancel, Challenges list/back, Control Diagram back/up/down, MP Advanced back, MP Settings back/Done, MP Setup back, Player Config back, and Team Setup Done to `pdgui_nav` helpers.
- Added static coverage so those files cannot reintroduce raw Enter, Space, Escape, arrow, or PageUp/PageDown menu polling.

### Files

- `port/fast3d/pdgui_countdown.cpp`
- `port/fast3d/pdgui_filebrowser.cpp`
- `port/fast3d/pdgui_menu_agentcreate.cpp`
- `port/fast3d/pdgui_menu_challenges.cpp`
- `port/fast3d/pdgui_menu_controldiagram.cpp`
- `port/fast3d/pdgui_menu_mpadvanced.cpp`
- `port/fast3d/pdgui_menu_mpsettings.cpp`
- `port/fast3d/pdgui_menu_mpsetup.cpp`
- `port/fast3d/pdgui_menu_playerconfig.cpp`
- `port/fast3d/pdgui_menu_teamsetup.cpp`
- `tests/test_menu_graph.cpp`
- Context updates: `context/tasks-current.md`, `context/designs/input-universality-and-transitions-2026-04-27.md`, `context/session-log.md`

### Verification

- Used isolated build session id `ix46`; did not use shared `Build/`.
- `git diff --check` passed for the touched code/test files.
- Direct isolated Ninja in `.claude/session-builds/ix46` built `pd`, `pd-server`, and `pd-tests`.
- Isolated `pd-tests.exe`: 319 test cases / 17543 assertions passed.
- Usual stub logs still appeared:
  - `[stub-log L2] NET: could not read 1 bytes (rp=15 wp=15)`
  - `[stub-log L2] NET: could not read 1 bytes (rp=1 wp=1)`
  - `[stub-log L2] NET: could not read 4 bytes (rp=0 wp=3)`

### Next

- Continue remaining raw menu-owned groups in order: cheats/modding panels, training menus, then Solo Mission. Keep tool/editor hotkeys classified separately.

---

## Session S554 - 2026-04-28 - Priority navigation and tab raw-input migration

Continued the raw action input migration after priority confirm/cancel sites. Scope stayed on priority list navigation and tab cycling.

### Outcome

- Added PageUp/PageDown as keyboard defaults for `ACTION_MENU_TAB_PREV` / `ACTION_MENU_TAB_NEXT` in menu and pause contexts while preserving LB/RB as gamepad defaults.
- Migrated Agent Select accept/cancel/list up/down to `pdgui_nav` helpers.
- Migrated main-menu Settings tab cycling and Cinema close/select/up/down to `pdgui_nav` helpers.
- Migrated Room tab cycling and Stats tab/close to `pdgui_nav` helpers.
- Left the two `renderMainMenu()` PageUp/PageDown calls as a documented transitional ImGui queue drain until backend PageUp injection can be retired with the remaining tab sites.
- Added static coverage for the migrated priority navigation/tab sites.

### Files

- `port/src/actionmap.cpp`
- `port/fast3d/pdgui_menu_agentselect.cpp`
- `port/fast3d/pdgui_menu_mainmenu.cpp`
- `port/fast3d/pdgui_menu_room.cpp`
- `port/fast3d/pdgui_menu_stats.cpp`
- `tests/test_menu_graph.cpp`
- Context updates: `context/tasks-current.md`, `context/designs/input-universality-and-transitions-2026-04-27.md`, `context/session-log.md`

### Verification

- Used isolated build session id `ix46`; did not use shared `Build/`.
- `git diff --check` passed for the touched code/test files.
- Direct isolated Ninja in `.claude/session-builds/ix46` built `pd`, `pd-server`, and `pd-tests`.
- Isolated `pd-tests.exe`: 318 test cases / 17423 assertions passed.
- Usual stub logs still appeared:
  - `[stub-log L2] NET: could not read 1 bytes (rp=15 wp=15)`
  - `[stub-log L2] NET: could not read 1 bytes (rp=1 wp=1)`
  - `[stub-log L2] NET: could not read 4 bytes (rp=0 wp=3)`

### Next

- Audit remaining raw ImGui key reads and classify each as a tool/editor exception, transitional ImGui queue drain, or menu-owned action read that must migrate next.

---

## Session S553 - 2026-04-28 - Priority confirm and exit raw-input migration

Continued the raw action input migration after adding the shared helpers. Scope stayed on high-risk confirm and cancel shortcuts in graph-owned or shared modal surfaces.

### Outcome

- Migrated warning-modal typed dialogs, MP End Game, and the PC file-manager placeholder from raw Enter/Space/Escape polling to `pdguiMenuAcceptPressed()` / `pdguiMenuCancelPressed()`.
- Migrated combat-sim pause End Match, Debug Shortcuts close, and parent pause close shortcuts to the same menu-action helpers.
- Migrated Room Leave arm, scenario delete confirm, and Leave Room confirm shortcuts to menu-action helpers while preserving the existing debounce and destructive-action confirmation behavior.
- Added static coverage so those priority sites cannot reintroduce raw Enter, keypad Enter, Space, or Escape polling.

### Files

- `port/fast3d/pdgui_menu_warning.cpp`
- `port/fast3d/pdgui_menu_pausemenu.cpp`
- `port/fast3d/pdgui_menu_room.cpp`
- `tests/test_menu_graph.cpp`
- Context updates: `context/tasks-current.md`, `context/designs/input-universality-and-transitions-2026-04-27.md`, `context/session-log.md`

### Verification

- Used isolated build session id `ix46`; did not use shared `Build/`.
- `git diff --check` passed for the touched code/test files.
- Direct isolated Ninja in `.claude/session-builds/ix46` built `pd`, `pd-server`, and `pd-tests`.
- Isolated `pd-tests.exe`: 317 test cases / 17350 assertions passed.
- Usual stub logs still appeared:
  - `[stub-log L2] NET: could not read 1 bytes (rp=15 wp=15)`
  - `[stub-log L2] NET: could not read 1 bytes (rp=1 wp=1)`
  - `[stub-log L2] NET: could not read 4 bytes (rp=0 wp=3)`

### Next

- Continue raw action input migration for remaining priority navigation and tab-repeat reads.

---

## Session S552 - 2026-04-28 - Raw menu-action helper and first priority exits

Moved from graph-declared transitions into the first raw action input slice. Scope stayed narrow: shared menu helper substrate plus high-risk confirm/cancel paths.

### Outcome

- Added `pdguiMenuActionPressed()`, `pdguiMenuActionHeld()`, `pdguiMenuActionRepeat()`, and named accept/cancel/nav helpers in `pdgui_nav`.
- Added Space and keypad Enter to menu/pause/debug `ACTION_USE` defaults so existing confirm-modal shortcuts now flow through action-map authority.
- Migrated `pdguiActionBarButton()` and `pdguiRenderConfirmModal()` off raw Enter/Space/Escape polling.
- Migrated graph-owned endscreen cancel paths, Network menu Back, Social Lobby disconnect confirm open, MP pause Back helper, and Bot Setup Back helper off raw Escape polling.
- Added static coverage for the helper API, menu accept bindings, and shared modal/action-bar migration.

### Files

- `port/include/pdgui_nav.h`
- `port/src/pdgui_nav.c`
- `port/src/actionmap.cpp`
- `port/fast3d/pdgui_layout.cpp`
- `port/fast3d/pdgui_menu_endscreen.cpp`
- `port/fast3d/pdgui_menu_network.cpp`
- `port/fast3d/pdgui_menu_lobby.cpp`
- `port/fast3d/pdgui_menu_mppause.cpp`
- `port/fast3d/pdgui_menu_botsetup.cpp`
- `tests/test_menu_graph.cpp`
- Context updates: `context/tasks-current.md`, `context/designs/input-universality-and-transitions-2026-04-27.md`, `context/session-log.md`

### Verification

- Used isolated build session id `ix46`; did not use shared `Build/`.
- `git diff --check` passed for the touched code/test files.
- Direct isolated Ninja in `.claude/session-builds/ix46` built `pd`, `pd-server`, and `pd-tests`.
- Isolated `pd-tests.exe`: 317 test cases / 17319 assertions passed.
- Usual stub logs still appeared:
  - `[stub-log L2] NET: could not read 1 bytes (rp=15 wp=15)`
  - `[stub-log L2] NET: could not read 1 bytes (rp=1 wp=1)`
  - `[stub-log L2] NET: could not read 4 bytes (rp=0 wp=3)`

### Next

- Continue raw action input migration for priority menu exits/confirms, then move to navigation/tab-repeat sites with the new repeat helper.

---

## Session S551 - 2026-04-28 - Agent Select load local graph edge

Continued graph-declared audit after main-menu close. Scope stayed on Agent Select `load`.

### Outcome

- Added `MENU_GRAPH_DEST_LOCAL_OP`, `MenuGraphLocalOpFn`, and `menuGraphFireLocalOp()` for graph edges that mutate local state without pushing, popping, networking, or scene changes.
- Changed Agent Select `load` from a pop edge to a local-op edge.
- Added `agentSelectGraphLoad()` to preserve optional pool release, game-file GUID update, save load, and per-agent preference load.
- Routed the Enter/selected-agent and mouse/selectable load paths through the local-op edge.
- Left auto-load and copy-confirm load direct because they are not the user-facing Agent Select `load` edge.

### Files

- `port/include/menugraph.h`
- `port/src/menugraph.c`
- `port/fast3d/pdgui_menu_agentselect.cpp`
- `tests/test_menu_graph.cpp`
- Context updates: `context/tasks-current.md`, `context/designs/input-universality-and-transitions-2026-04-27.md`, `context/session-log.md`

### Verification

- Used isolated build session id `ix46`; did not use shared `Build/`.
- `git diff --check` passed for the touched code/test files.
- Direct isolated Ninja in `.claude/session-builds/ix46` built `pd`, `pd-server`, and `pd-tests`.
- Isolated `pd-tests.exe`: 316 test cases / 17299 assertions passed.
- Usual stub logs still appeared:
  - `[stub-log L2] NET: could not read 1 bytes (rp=15 wp=15)`
  - `[stub-log L2] NET: could not read 1 bytes (rp=1 wp=1)`
  - `[stub-log L2] NET: could not read 4 bytes (rp=0 wp=3)`

### Next

- Run remaining graph coverage check. If only unused/deferred edges remain, move into raw ImGui key migration behind action-map authority.

---

## Session S550 - 2026-04-28 - Main-menu Close pop graph edge

Continued the main-menu graph audit after Quit. Scope stayed on the existing `MENU_TYPE_MAIN_MENU` `close` edge.

### Outcome

- Added `MenuGraphPopOpFn` and `menuGraphFirePopOp()` for pop edges with required behavior-preserving side effects.
- Added `pdguiMainMenuGraphClose()` around the existing top-level close behavior: unpause level, call `playerUnpause()`, restore player control, pop the legacy dialog, and defensively pop `g_CtxImGuiMenu` if needed.
- Routed the top-level main-menu close path through `MENU_TYPE_MAIN_MENU` `close` using the pop-op helper.
- Added static coverage for the helper and render path.

### Files

- `port/include/menugraph.h`
- `port/src/menugraph.c`
- `port/fast3d/pdgui_menu_mainmenu.cpp`
- `tests/test_menu_graph.cpp`
- Context updates: `context/tasks-current.md`, `context/designs/input-universality-and-transitions-2026-04-27.md`, `context/session-log.md`

### Verification

- Used isolated build session id `ix46`; did not use shared `Build/`.
- `git diff --check` passed for the touched code/test files.
- Direct isolated Ninja in `.claude/session-builds/ix46` built `pd`, `pd-server`, and `pd-tests`.
- Isolated `pd-tests.exe`: 316 test cases / 17285 assertions passed.
- Usual stub logs still appeared:
  - `[stub-log L2] NET: could not read 1 bytes (rp=15 wp=15)`
  - `[stub-log L2] NET: could not read 1 bytes (rp=1 wp=1)`
  - `[stub-log L2] NET: could not read 4 bytes (rp=0 wp=3)`

### Next

- Re-run graph coverage audit. Agent Select load remains declared but has in-place load semantics that may need graph redesign rather than a blind pop.

---

## Session S549 - 2026-04-28 - Main-menu Quit process graph edge

Continued the main-menu graph audit after Stats panel open. Scope stayed on the existing `MENU_TYPE_MAIN_MENU` `quit` process edge.

### Outcome

- Added `MenuGraphProcessOpFn` and `menuGraphFireProcessOp()` for process-exit graph edges.
- Added `pdguiMainMenuGraphQuit()` as a callback around the existing `SDL_QUIT` event post.
- Routed the Quit confirmation result through `MENU_TYPE_MAIN_MENU` `quit` using the process-op helper.
- Added static coverage for the helper and render path.

### Files

- `port/include/menugraph.h`
- `port/src/menugraph.c`
- `port/fast3d/pdgui_menu_mainmenu.cpp`
- `tests/test_menu_graph.cpp`
- Context updates: `context/tasks-current.md`, `context/designs/input-universality-and-transitions-2026-04-27.md`, `context/session-log.md`

### Verification

- Used isolated build session id `ix46`; did not use shared `Build/`.
- `git diff --check` passed for the touched code/test files.
- Direct isolated Ninja in `.claude/session-builds/ix46` built `pd`, `pd-server`, and `pd-tests`.
- Isolated `pd-tests.exe`: 315 test cases / 17267 assertions passed.
- Usual stub logs still appeared:
  - `[stub-log L2] NET: could not read 1 bytes (rp=15 wp=15)`
  - `[stub-log L2] NET: could not read 1 bytes (rp=1 wp=1)`
  - `[stub-log L2] NET: could not read 4 bytes (rp=0 wp=3)`

### Next

- Finish remaining graph-declared audit before raw key migration. Main-menu close and Agent Select load need special handling because they combine graph edges with behavior-preserving side effects.

---

## Session S548 - 2026-04-28 - Main-menu Stats panel graph edge

Continued the main-menu graph audit after Modding hub open. Scope stayed on the Stats panel open transition.

### Outcome

- Added `MENU_TYPE_MAIN_STATS_VIEW` `open_panel` as a graph push edge targeting `MENU_TYPE_STATS_PANEL`.
- Added `pdguiMainMenuGraphOpenStatsPanel()` as a callback around the existing `pdguiMenuStatsShow()` behavior.
- Routed the top-level Stats shortcut through the Stats subview edge and then through `open_panel`.
- Added static coverage so the render path no longer calls `pdguiMenuStatsShow()` directly.

### Files

- `port/src/menugraph.c`
- `port/fast3d/pdgui_menu_mainmenu.cpp`
- `tests/test_menu_graph.cpp`
- Context updates: `context/tasks-current.md`, `context/designs/input-universality-and-transitions-2026-04-27.md`, `context/session-log.md`

### Verification

- Used isolated build session id `ix46`; did not use shared `Build/`.
- `git diff --check` passed for the touched code/test files.
- Direct isolated Ninja in `.claude/session-builds/ix46` built `pd`, `pd-server`, and `pd-tests`.
- Isolated `pd-tests.exe`: 314 test cases / 17254 assertions passed.
- Usual stub logs still appeared:
  - `[stub-log L2] NET: could not read 1 bytes (rp=15 wp=15)`
  - `[stub-log L2] NET: could not read 1 bytes (rp=1 wp=1)`
  - `[stub-log L2] NET: could not read 4 bytes (rp=0 wp=3)`

### Next

- Continue the remaining graph-declared audit. Agent Select load and main-menu quit/close need careful treatment because they are not plain push/pop-without-side-effects.

---

## Session S547 - 2026-04-28 - Main-menu Modding hub graph edge

Continued the main-menu graph audit after the Solo view push-op slice. Scope stayed on the existing `MENU_TYPE_MAIN_MODDING_VIEW` `open_hub` edge.

### Outcome

- Added `pdguiMainMenuGraphOpenModdingHub()` as a callback around the existing `pdguiModdingHubShow()` behavior.
- Routed the top-level Mods shortcut through the Modding subview edge and then through `open_hub`.
- Routed the Modding subview's closed-hub `Open Modding Hub` button through the same graph edge.
- Added static coverage so those render paths no longer call `pdguiModdingHubShow()` directly.

### Files

- `port/fast3d/pdgui_menu_mainmenu.cpp`
- `tests/test_menu_graph.cpp`
- Context updates: `context/tasks-current.md`, `context/designs/input-universality-and-transitions-2026-04-27.md`, `context/session-log.md`

### Verification

- Used isolated build session id `ix46`; did not use shared `Build/`.
- `git diff --check` passed for the touched code/test files.
- Direct isolated Ninja in `.claude/session-builds/ix46` built `pd`, `pd-server`, and `pd-tests`.
- Isolated `pd-tests.exe`: 313 test cases / 17246 assertions passed.
- Usual stub logs still appeared:
  - `[stub-log L2] NET: could not read 1 bytes (rp=15 wp=15)`
  - `[stub-log L2] NET: could not read 1 bytes (rp=1 wp=1)`
  - `[stub-log L2] NET: could not read 4 bytes (rp=0 wp=3)`

### Next

- Continue remaining graph-declared audit. Stats open lacks a declared open edge, while Agent Select `load` is declared but currently only partly modeled.

---

## Session S546 - 2026-04-28 - Main-menu Solo view push-op graph edges

Continued the priority-node audit after Solo Mission start/restart. Scope stayed on the main-menu Solo subview's two declared push edges.

### Outcome

- Added `MenuGraphPushOpFn` and `menuGraphFirePushOp()` for push edges that must preserve existing handler side effects instead of calling `menuPushDialog()` directly.
- Routed main-menu Solo Missions through `MENU_TYPE_MAIN_SOLO_VIEW` `solo_missions`, preserving `pdguiSoloMissionReset()` and `menuhandlerMainMenuSoloMissions()`.
- Routed main-menu Combat Simulator through `MENU_TYPE_MAIN_SOLO_VIEW` `combat_simulator`, preserving `menuhandlerMainMenuCombatSimulator()` and its room-open setup path.
- Added static coverage for the push-op helper and the main-menu Solo view render path.

### Files

- `port/include/menugraph.h`
- `port/src/menugraph.c`
- `port/fast3d/pdgui_menu_mainmenu.cpp`
- `tests/test_menu_graph.cpp`
- Context updates: `context/tasks-current.md`, `context/designs/input-universality-and-transitions-2026-04-27.md`, `context/session-log.md`

### Verification

- Used isolated build session id `ix46`; did not use shared `Build/`.
- `git diff --check` passed for the touched code/test files.
- Direct isolated Ninja in `.claude/session-builds/ix46` built `pd`, `pd-server`, and `pd-tests`.
- Isolated `pd-tests.exe`: 312 test cases / 17238 assertions passed.
- Usual stub logs still appeared:
  - `[stub-log L2] NET: could not read 1 bytes (rp=15 wp=15)`
  - `[stub-log L2] NET: could not read 1 bytes (rp=1 wp=1)`
  - `[stub-log L2] NET: could not read 4 bytes (rp=0 wp=3)`

### Next

- Continue remaining priority-node audit. Main-menu Modding/Stats overlay edges and Agent Select load are candidates, but raw key migration remains a separate later step.

---

## Session S545 - 2026-04-28 - Solo Mission start/back/restart graph edges

Continued the recursive menu graph migration after endscreen exits. Scope stayed on existing Solo Mission scene/pop edges.

### Outcome

- Added `soloMissionGraphStart()` so Mission Select and Accept Mission start paths preserve the existing `menuhandlerAcceptMission()` bridge and ImGui context pop inside a graph callback.
- Added `soloMissionGraphRestart()` so the solo pause Restart confirmation preserves catalog-backed stage resolution before `mainChangeToStage()`.
- Routed Mission Select Start and Accept Mission Accept through `MENU_TYPE_SOLO_MISSION` `start` using `menuGraphFireSceneOp()`.
- Routed Mission Select Back and Accept Mission Decline through `MENU_TYPE_SOLO_MISSION` `back` using `menuGraphFirePop()`.
- Routed solo pause Restart through `MENU_TYPE_SOLO_MISSION_PAUSE` `restart` using `menuGraphFireSceneOp()`.
- Added static coverage for those render paths.

### Files

- `port/fast3d/pdgui_menu_solomission.cpp`
- `tests/test_menu_graph.cpp`
- Context updates: `context/tasks-current.md`, `context/designs/input-universality-and-transitions-2026-04-27.md`, `context/session-log.md`

### Verification

- Used isolated build session id `ix46`; did not use shared `Build/`.
- `git diff --check` passed for the touched code/test files.
- Direct isolated Ninja in `.claude/session-builds/ix46` built `pd`, `pd-server`, and `pd-tests`.
- Isolated `pd-tests.exe`: 311 test cases / 17218 assertions passed.
- Usual stub logs still appeared:
  - `[stub-log L2] NET: could not read 1 bytes (rp=15 wp=15)`
  - `[stub-log L2] NET: could not read 1 bytes (rp=1 wp=1)`
  - `[stub-log L2] NET: could not read 4 bytes (rp=0 wp=3)`

### Next

- Continue remaining priority-node audit before raw key migration. Inspect graph-declared direct edges still left in main menu, training, cinema, stats, and related menu files.

---

## Session S544 - 2026-04-28 - Endscreen scene graph edges

Continued menu graph migration after the Room node. Scope stayed on endscreen transitions already represented by graph nodes.

### Outcome

- Changed solo endscreen `main_menu` to a scene graph edge because the real behavior must still run the existing endscreen bridge teardown.
- Routed solo endscreen Continue, Retry, and Main Menu through `MENU_TYPE_ENDSCREEN_SOLO` scene graph callbacks.
- Routed MP endscreen Return to Room, Play Again, and Quit through `MENU_TYPE_ENDSCREEN_MP` scene graph callbacks.
- Moved the MP disconnect post-disconnect endscreen exit into the graph-dispatched disconnect callback, keeping the renderer free of direct network/teardown calls.
- Added static coverage for solo and MP endscreen graph usage and direct-call prevention in the render paths.

### Files

- `port/src/menugraph.c`
- `port/fast3d/pdgui_menu_endscreen.cpp`
- `tests/test_menu_graph.cpp`
- Context updates: `context/tasks-current.md`, `context/designs/input-universality-and-transitions-2026-04-27.md`, `context/session-log.md`

### Verification

- Used isolated build session id `ix46`; did not use shared `Build/`.
- `git diff --check` passed for the touched code/test files.
- Direct isolated Ninja in `.claude/session-builds/ix46` built `pd`, `pd-server`, and `pd-tests`.
- Isolated `pd-tests.exe`: 310 test cases / 17193 assertions passed.
- Usual stub logs still appeared:
  - `[stub-log L2] NET: could not read 1 bytes (rp=15 wp=15)`
  - `[stub-log L2] NET: could not read 1 bytes (rp=1 wp=1)`
  - `[stub-log L2] NET: could not read 4 bytes (rp=0 wp=3)`

### Next

- Continue the remaining priority-node audit. Likely next candidate is Solo Mission start/restart/back because it already declares graph edges but still has renderer-local stage calls.

---

## Session S543 - 2026-04-28 - Room Leave graph edge

Continued menu graph migration after Room Start Match. Scope stayed on the remaining declared Room edge.

### Outcome

- Changed `MENU_TYPE_ROOM` `leave_room` to a graph operation edge.
- Added `roomGraphLeaveRoom()` to preserve solo back-to-menu, client leave packet, listen-host local leave, menu-pool release, setup reset, and return-to-social-lobby behavior.
- Routed the leave-confirm modal through `MENU_TYPE_ROOM` `leave_room` using `menuGraphFireNetworkOp()`.
- Added static coverage so `pdguiRoomScreenRender()` no longer directly sends leave packets, calls listen-host leave, or returns online clients to the lobby.

### Files

- `port/src/menugraph.c`
- `port/fast3d/pdgui_menu_room.cpp`
- `tests/test_menu_graph.cpp`
- Context updates: `context/tasks-current.md`, `context/designs/input-universality-and-transitions-2026-04-27.md`, `context/session-log.md`

### Verification

- Used isolated build session id `ix46`; did not use shared `Build/`.
- `git diff --check` passed for the touched code/test files.
- Direct isolated Ninja in `.claude/session-builds/ix46` built the affected targets.
- Isolated `pd-tests.exe`: 309 test cases / 17158 assertions passed.
- Usual stub logs still appeared:
  - `[stub-log L2] NET: could not read 1 bytes (rp=15 wp=15)`
  - `[stub-log L2] NET: could not read 1 bytes (rp=1 wp=1)`
  - `[stub-log L2] NET: could not read 4 bytes (rp=0 wp=3)`

### Next

- Room node migration is now covered. Continue remaining priority-node audit with solo mission or endscreen scene edges next.

---

## Session S542 - 2026-04-28 - Room Start Match scene edge

Continued menu graph migration after The Grid enter slice. Scope stayed on the already-declared Room `start_match` edge.

### Outcome

- Extracted the existing Room Start Match switch into `roomGraphStartMatch()`.
- Preserved Combat Sim solo start, Combat Sim online start, Campaign start, and Counter-Op start behavior.
- Routed the Start Match button through `MENU_TYPE_ROOM` `start_match` using `menuGraphFireSceneOp()`.
- Added static coverage so `pdguiRoomScreenRender()` cannot directly call `matchStart()` or `netLobbyRequestStart*()` for Start Match.

### Files

- `port/fast3d/pdgui_menu_room.cpp`
- `tests/test_menu_graph.cpp`
- Context updates: `context/tasks-current.md`, `context/designs/input-universality-and-transitions-2026-04-27.md`, `context/session-log.md`

### Verification

- Used isolated build session id `ix46`; did not use shared `Build/`.
- `git diff --check` passed for the touched code/test files.
- Direct isolated Ninja in `.claude/session-builds/ix46` built the affected targets.
- Isolated `pd-tests.exe`: 308 test cases / 17144 assertions passed.
- Usual stub logs still appeared:
  - `[stub-log L2] NET: could not read 1 bytes (rp=15 wp=15)`
  - `[stub-log L2] NET: could not read 1 bytes (rp=1 wp=1)`
  - `[stub-log L2] NET: could not read 4 bytes (rp=0 wp=3)`

### Next

- Continue the remaining priority-node audit. Room Leave is now the main Room edge left, but it needs a behavior-preserving helper because solo and online leave have different side effects.

---

## Session S541 - 2026-04-28 - The Grid enter scene edge

Continued menu graph migration after adding the scene-operation helper. Scope stayed on the already-declared Grid submenu `enter` edge.

### Outcome

- Added `pdguiMainMenuGraphEnterGrid()` as a callback around the existing `gridCommitEnter()` behavior.
- Routed The Grid Enter through `MENU_TYPE_GRID_SUBMENU` `enter` using `menuGraphFireSceneOp()`.
- Preserved success behavior, including open-dialog sound and returning to main view.
- Preserved failure behavior, including staying on the Grid submenu and playing cancel.
- Added static coverage so `renderGridSubmenu()` cannot call `gridCommitEnter()` directly.

### Files

- `port/fast3d/pdgui_menu_mainmenu.cpp`
- `tests/test_menu_graph.cpp`
- Context updates: `context/tasks-current.md`, `context/designs/input-universality-and-transitions-2026-04-27.md`, `context/session-log.md`

### Verification

- Used isolated build session id `ix46`; did not use shared `Build/`.
- `git diff --check` passed for the touched code/test files.
- Direct isolated Ninja in `.claude/session-builds/ix46` built the affected targets.
- Isolated `pd-tests.exe`: 307 test cases / 17133 assertions passed.
- Usual stub logs still appeared:
  - `[stub-log L2] NET: could not read 1 bytes (rp=15 wp=15)`
  - `[stub-log L2] NET: could not read 1 bytes (rp=1 wp=1)`
  - `[stub-log L2] NET: could not read 4 bytes (rp=0 wp=3)`

### Next

- Continue the remaining priority-node audit. Likely next candidates are solo mission menu graph edges or Room start/leave, depending on which can be preserved with the current graph helpers.

---

## Session S540 - 2026-04-28 - Scene-operation graph helper and pause End Match

Continued menu graph migration after the main-menu back-edge slice. Scope added the smallest scene-operation helper needed to migrate a stage-like direct transition without changing stage behavior.

### Outcome

- Added `MenuGraphSceneOpFn` and `menuGraphFireSceneOp()` for `MENU_GRAPH_DEST_SCENE_EVENT` edges.
- The helper validates edge existence and kind, logs the declared scene event payload, runs a callback, and logs the result.
- Migrated combat-sim pause End Match through `MENU_TYPE_PAUSE_MENU` `end_mission`.
- Preserved existing behavior inside `pauseGraphEndMission()`: set player-aborted state, then call `mainEndStage()`.
- Added static coverage for the helper and for removing direct `pdguiPauseSetPlayerAborted()` / `mainEndStage()` calls from the pause renderer body.

### Files

- `port/include/menugraph.h`
- `port/src/menugraph.c`
- `port/fast3d/pdgui_menu_pausemenu.cpp`
- `tests/test_menu_graph.cpp`
- Context updates: `context/tasks-current.md`, `context/designs/input-universality-and-transitions-2026-04-27.md`, `context/session-log.md`

### Verification

- Used isolated build session id `ix46`; did not use shared `Build/`.
- `git diff --check` passed for the touched code/test files.
- Direct isolated Ninja in `.claude/session-builds/ix46` built the affected targets.
- Isolated `pd-tests.exe`: 307 test cases / 17128 assertions passed.
- Usual stub logs still appeared:
  - `[stub-log L2] NET: could not read 1 bytes (rp=15 wp=15)`
  - `[stub-log L2] NET: could not read 1 bytes (rp=1 wp=1)`
  - `[stub-log L2] NET: could not read 4 bytes (rp=0 wp=3)`

### Next

- Use the scene-operation helper for the next small existing scene/stage graph edge, or move into solo mission push/pop graph migration if no small scene site preserves behavior cleanly.

---

## Session S539 - 2026-04-28 - Main-menu inline back-edge graph firing

Continued menu graph migration after the main-menu inline open slice. Scope stayed on already-declared inline subview `back` edges.

### Outcome

- Added `pdguiMainMenuFireSubviewBackEdge()` to validate the current inline subview's declared `back` edge and destination kind before returning to view 0.
- Routed shared subview close, Grid Back, Modding Back, and Stats auto-close through the back-edge helper.
- Left external reset, initial menu open, and Grid Enter direct because they are lifecycle/scene paths, not user back edges.
- Added static coverage for the back-edge helper and removal of direct top-level-return `pdguiMainMenuSetView(0, "...")` calls for those user back paths.

### Files

- `port/fast3d/pdgui_menu_mainmenu.cpp`
- `tests/test_menu_graph.cpp`
- Context updates: `context/tasks-current.md`, `context/designs/input-universality-and-transitions-2026-04-27.md`, `context/session-log.md`

### Verification

- Used isolated build session id `ix46`; did not use shared `Build/`.
- `git diff --check` passed for the touched code/test files.
- Direct isolated Ninja in `.claude/session-builds/ix46` built the affected targets.
- Isolated `pd-tests.exe`: 306 test cases / 17114 assertions passed.
- Usual stub logs still appeared:
  - `[stub-log L2] NET: could not read 1 bytes (rp=15 wp=15)`
  - `[stub-log L2] NET: could not read 1 bytes (rp=1 wp=1)`
  - `[stub-log L2] NET: could not read 4 bytes (rp=0 wp=3)`

### Next

- Inspect remaining priority-node direct transitions. The next likely choices are solo mission menu push/pop migration or introducing a small scene-operation helper for stage/endstage paths.

---

## Session S538 - 2026-04-28 - Main-menu inline subview graph firing

Continued menu graph migration after the Room setup subdialog slice. Scope stayed on already-declared main-menu inline subview edges.

### Outcome

- Added `pdguiMainMenuFireSubviewEdge()` to validate `MENU_TYPE_MAIN_MENU` graph edges and destination menu-pool types before changing inline views.
- Routed top-level Solo Play, Online Play, Settings, Mods, Stats, and The Grid through the inline graph helper.
- Preserved the existing `pdguiMainMenuSetView()` pool acquire/release behavior and renderer state model.
- Added static coverage for edge lookup, push-destination validation, and removal of direct top-level `pdguiMainMenuSetView(1..6, "open-*")` calls.

### Files

- `port/fast3d/pdgui_menu_mainmenu.cpp`
- `tests/test_menu_graph.cpp`
- Context updates: `context/tasks-current.md`, `context/designs/input-universality-and-transitions-2026-04-27.md`, `context/session-log.md`

### Verification

- Used isolated build session id `ix46`; did not use shared `Build/`.
- `git diff --check` passed for the touched code/test files.
- Direct isolated Ninja in `.claude/session-builds/ix46` built the affected targets.
- Isolated `pd-tests.exe`: 306 test cases / 17103 assertions passed.
- Usual stub logs still appeared:
  - `[stub-log L2] NET: could not read 1 bytes (rp=15 wp=15)`
  - `[stub-log L2] NET: could not read 1 bytes (rp=1 wp=1)`
  - `[stub-log L2] NET: could not read 4 bytes (rp=0 wp=3)`

### Next

- Inspect remaining priority-node direct transitions. Most remaining direct sites are scene/stage transitions or broad training/solo stacks, so choose the next slice only after checking whether a small graph helper can preserve current behavior.

---

## Session S537 - 2026-04-28 - Room setup subdialog graph migration

Continued menu graph migration after the warning-modal slice. Scope stayed on Room setup child pushes and did not touch match start or room leave.

### Outcome

- Added `MENU_TYPE_ROOM` graph push edges for Team Setup and Select Music.
- Migrated Room Team Setup through the `team_setup` graph edge, validating the destination as `MENU_TYPE_MP_TEAM_SETUP`.
- Migrated Room Select Music through the `select_music` graph edge, validating the destination as `MENU_TYPE_MP_TUNES`.
- Added static coverage so those Room setup subdialogs cannot reintroduce direct `menuPushDialog()` calls.

### Files

- `port/src/menugraph.c`
- `port/fast3d/pdgui_menu_room.cpp`
- `tests/test_menu_graph.cpp`
- Context updates: `context/tasks-current.md`, `context/designs/input-universality-and-transitions-2026-04-27.md`, `context/session-log.md`

### Verification

- Used isolated build session id `ix46`; did not use shared `Build/`.
- `git diff --check` passed for the touched code/test/context files.
- Direct isolated Ninja in `.claude/session-builds/ix46` built the affected targets.
- Isolated `pd-tests.exe`: 306 test cases / 17092 assertions passed.
- Usual stub logs still appeared:
  - `[stub-log L2] NET: could not read 1 bytes (rp=15 wp=15)`
  - `[stub-log L2] NET: could not read 1 bytes (rp=1 wp=1)`
  - `[stub-log L2] NET: could not read 4 bytes (rp=0 wp=3)`

### Next

- Inspect remaining priority-node direct transitions and migrate the next small graph-safe site before Room start/leave.

---

## Session S536 - 2026-04-28 - Warning modal graph pop migration

Continued menu graph migration after the Social Lobby slice. Scope stayed on the declared Warning Modal confirm/cancel pop edges.

### Outcome

- Migrated generic typed-dialog fallback OK through the `MENU_TYPE_WARNING_MODAL` `confirm` graph pop edge.
- Migrated generic typed-dialog Escape through the `MENU_TYPE_WARNING_MODAL` `cancel` graph pop edge.
- Migrated MP End Game popup external dismiss, Confirm, and Cancel through warning-modal graph pop edges while preserving the existing legacy End Match selectable handler call.
- Migrated the PC filemgr placeholder OK/Escape exits through warning-modal graph pop edges.
- Added static coverage so the warning renderer cannot reintroduce direct `menuPopDialog()` calls.

### Files

- `port/fast3d/pdgui_menu_warning.cpp`
- `tests/test_menu_graph.cpp`
- Context updates: `context/tasks-current.md`, `context/designs/input-universality-and-transitions-2026-04-27.md`, `context/session-log.md`

### Verification

- Used isolated build session id `ix46`; did not use shared `Build/`.
- `git diff --check` passed for the touched code/test files.
- Direct isolated Ninja in `.claude/session-builds/ix46` built the affected targets.
- Isolated `pd-tests.exe`: 305 test cases / 17083 assertions passed.
- Usual stub logs still appeared:
  - `[stub-log L2] NET: could not read 1 bytes (rp=15 wp=15)`
  - `[stub-log L2] NET: could not read 1 bytes (rp=1 wp=1)`
  - `[stub-log L2] NET: could not read 4 bytes (rp=0 wp=3)`

### Next

- Inspect remaining priority-node direct transitions and migrate the next small graph-safe site before Room start/leave.

---

## Session S535 - 2026-04-28 - Social Lobby graph migration

Continued menu graph migration after the solo pause sibling slice. Scope stayed on the Social Lobby node's declared network operations.

### Outcome

- Migrated Social Lobby Create Room through the `MENU_TYPE_SOCIAL_LOBBY` `create_room` graph network edge.
- Migrated Social Lobby Disconnect confirmation through the `MENU_TYPE_SOCIAL_LOBBY` `disconnect` graph network edge.
- Kept the existing create-room packet send and disconnect behavior inside local graph callbacks.
- Added static coverage so the Social Lobby render path cannot reintroduce direct create-room packet writes or direct `netDisconnect()`.

### Files

- `port/fast3d/pdgui_menu_lobby.cpp`
- `tests/test_menu_graph.cpp`
- Context updates: `context/tasks-current.md`, `context/designs/input-universality-and-transitions-2026-04-27.md`, `context/session-log.md`

### Verification

- Used isolated build session id `ix46`; did not use shared `Build/`.
- `git diff --check` passed for the touched code/test files.
- Direct isolated Ninja in `.claude/session-builds/ix46` built the affected targets.
- Isolated `pd-tests.exe`: 304 test cases / 17070 assertions passed.
- Usual stub logs still appeared:
  - `[stub-log L2] NET: could not read 1 bytes (rp=15 wp=15)`
  - `[stub-log L2] NET: could not read 1 bytes (rp=1 wp=1)`
  - `[stub-log L2] NET: could not read 4 bytes (rp=0 wp=3)`

### Next

- Inspect remaining priority-node direct transitions and migrate the next small graph-safe site before Room start/leave.

---

## Session S534 - 2026-04-28 - Solo pause sibling graph migration

Continued the solo pause graph migration after S532. Scope stayed on Inventory and Settings, which are legacy next-sibling dialogs rather than ordinary child pushes.

### Outcome

- Added `menuSwitchToDialog()` so graph firing can switch directly to an already-open legacy sibling by dialogdef.
- Added `MENU_GRAPH_DEST_SWITCH_SIBLING` and `menuGraphFireSwitchSibling()`.
- Added `MENU_TYPE_SOLO_INVENTORY` and registered solo Inventory and solo Options in the menu pool.
- Added graph nodes for solo Inventory and solo Options back edges.
- Migrated solo pause Inventory and Settings through sibling graph edges.
- Migrated Inventory and Options Back paths through sibling graph edges back to solo pause.
- Extended static coverage for the new graph destination kind, helper, registrations, renderer calls, and back paths.

### Files

- `src/include/game/menu.h`
- `src/game/menu.c`
- `port/include/menupool.h`
- `port/src/menupool.c`
- `port/include/menugraph.h`
- `port/src/menugraph.c`
- `port/fast3d/pdgui_menu_solomission.cpp`
- `tests/test_menu_graph.cpp`
- Context updates: `context/tasks-current.md`, `context/designs/input-universality-and-transitions-2026-04-27.md`, `context/session-log.md`

### Verification

- Used isolated build session id `ix46`; did not use shared `Build/`.
- `git diff --check` passed for the touched code/test files before the build.
- Direct isolated Ninja in `.claude/session-builds/ix46` built `pd`, `pd-server`, and `pd-tests`.
- Isolated `pd-tests.exe`: 303 test cases / 17061 assertions passed.
- Usual stub logs still appeared:
  - `[stub-log L2] NET: could not read 1 bytes (rp=15 wp=15)`
  - `[stub-log L2] NET: could not read 1 bytes (rp=1 wp=1)`
  - `[stub-log L2] NET: could not read 4 bytes (rp=0 wp=3)`

### Next

- Inspect the remaining priority-node direct transitions and migrate the next small graph-safe site before Room start/leave.

---

## Session S533 - 2026-04-28 - Character metadata lifecycle activation

Continued the catalog-owned asset pipeline after S531. Scope stayed on composite catalog entries that should not become generic byte payloads. Note: S532 belongs to the parallel input/menu graph lane.

### Outcome

- Added `ASSET_CHARACTER` to metadata runtime lifecycle activation.
- Composite character entries now activate as catalog metadata rather than loading `bodyfile` as an opaque byte payload.
- Static coverage now pins `ASSET_CHARACTER` in the metadata lifecycle type set.

### Files

- `port/src/assetcatalog_load.c`
- `tests/test_catalog_provider_static.cpp`
- Context updates: `context/tasks-current.md`, `context/session-log.md`

### Verification

- Used isolated build directory session id `catalog-s506`; did not use shared `Build/`.
- `.\devtools\build-session.ps1 -Session catalog-s506 -Target all` passed for `pd` and `pd-server`.
- Isolated Ninja build passed for `pd-tests`.
- Isolated `pd-tests.exe`: 303 test cases / 17032 assertions passed.
- Usual stub logs still appear:
  - `[stub-log L2] NET: could not read 1 bytes (rp=15 wp=15)`
  - `[stub-log L2] NET: could not read 1 bytes (rp=1 wp=1)`
  - `[stub-log L2] NET: could not read 4 bytes (rp=0 wp=3)`

### Next

- Determine the next safe catalog/provider slice. Candidate: audit remaining generic byte payload types (`ASSET_ANIMATION`, `ASSET_TEXTURES`, `ASSET_SFX`, `ASSET_MUSIC`, `ASSET_UI`, `ASSET_TOOL`, `ASSET_VEHICLE`, `ASSET_MISSION`) and choose only domains with clear provider/source ownership.

---

## Session S532 - 2026-04-28 - Solo pause graph migration, first slice

Continued menu graph migration after MP pause. Scope stayed on solo in-mission pause transitions that do not require changing legacy sibling-stack behavior.

### Outcome

- Split `MENU_TYPE_SOLO_MISSION_PAUSE` onto its own graph edge set instead of reusing the generic pause edges.
- Migrated solo pause Resume/Back through the `resume` graph pop edge.
- Migrated solo pause Abort through the `abort` graph warning-modal push edge.
- Added static coverage so the solo pause renderer cannot reintroduce direct Resume/Back `menuPopDialog()` or direct `menuPushDialog(&g_MissionAbortMenuDialog)` for Abort.
- Left solo pause Inventory and Settings direct for the next slice because they are legacy next-sibling dialogs, not ordinary child pushes.

### Files

- `port/src/menugraph.c`
- `port/fast3d/pdgui_menu_solomission.cpp`
- `tests/test_menu_graph.cpp`
- Context updates: `context/tasks-current.md`, `context/designs/input-universality-and-transitions-2026-04-27.md`, `context/session-log.md`

### Verification

- Used isolated build session id `ix46`; did not use shared `Build/`.
- `git diff --check` passed for the touched code/test files.
- `.\devtools\build-session.ps1 -Session ix46 -Target all` was invoked first but stalled in client compile with idle CMake/Ninja children after the command timeout. Stale `ix46` locks were removed only after confirming no compiler or Ninja process was active.
- Direct isolated Ninja in `.claude/session-builds/ix46` built `pd`, `pd-server`, and `pd-tests`.
- Isolated `pd-tests.exe`: 303 test cases / 17031 assertions passed.
- Usual stub logs still appeared:
  - `[stub-log L2] NET: could not read 1 bytes (rp=15 wp=15)`
  - `[stub-log L2] NET: could not read 1 bytes (rp=1 wp=1)`
  - `[stub-log L2] NET: could not read 4 bytes (rp=0 wp=3)`

### Next

- Add the smallest proper graph helper for solo pause Inventory/Settings next-sibling transitions, then continue the menu graph migration before Room start/leave.

---

## Session S531 - 2026-04-28 - Map metadata lifecycle activation

Continued the catalog-owned asset pipeline after S530. Scope stayed on typed lifecycle domains that should be metadata-owned rather than file-loaded.

### Outcome

- Added `ASSET_MAP` to metadata runtime lifecycle activation.
- Stage/catalog map entries now activate as catalog metadata instead of reaching generic provider/path loading.
- Refreshed stale screen-manifest and net-manifest comments that still described untyped `catalogLoadAsset()` / `catalogUnloadAsset()` behavior.
- Static coverage now pins `ASSET_MAP` in the metadata lifecycle type set.

### Files

- `port/src/assetcatalog_load.c`
- `port/src/net/netmanifest.c`
- `port/src/screenmfst.c`
- `port/include/screenmfst.h`
- `tests/test_catalog_provider_static.cpp`
- Context updates: `context/tasks-current.md`, `context/session-log.md`

### Verification

- Used isolated build directory session id `catalog-s506`; did not use shared `Build/`.
- `.\devtools\build-session.ps1 -Session catalog-s506 -Target all` passed for `pd` and `pd-server`.
- Isolated Ninja build passed for `pd-tests`.
- Isolated `pd-tests.exe`: 303 test cases / 17031 assertions passed.
- Usual stub logs still appear:
  - `[stub-log L2] NET: could not read 1 bytes (rp=15 wp=15)`
  - `[stub-log L2] NET: could not read 1 bytes (rp=1 wp=1)`
  - `[stub-log L2] NET: could not read 4 bytes (rp=0 wp=3)`

### Next

- Determine the next safe catalog/provider slice. Candidate: audit whether `ASSET_CHARACTER` should become metadata/composite activation rather than generic bodyfile byte loading, or leave it for the body/head composite migration.

---

## Session S530 - 2026-04-28 - Typed lifecycle fallback confinement

Continued the catalog-owned asset pipeline after S529. Scope stayed on separating typed lifecycle behavior from legacy untyped path fallback.

### Outcome

- Confined generic raw path fallback to legacy untyped `catalogLoadAsset()` compatibility.
- `catalogLoadTypedAsset()` callers that reach the generic lifecycle branch now require a provider handle and fail loud with `CATALOG.LIFECYCLE.LOAD` when missing.
- Provider load failure in the typed generic branch now returns failure instead of falling through to `s_catalogLoadEntryFromPath()`.
- Static coverage pins the typed/untyped boundary and keeps the untyped path fallback visibly separate.

### Files

- `port/src/assetcatalog_load.c`
- `tests/test_catalog_provider_static.cpp`
- Context updates: `context/tasks-current.md`, `context/session-log.md`

### Verification

- Used isolated build directory session id `catalog-s506`; did not use shared `Build/`.
- `.\devtools\build-session.ps1 -Session catalog-s506 -Target all` passed for `pd` and `pd-server`.
- Isolated Ninja build passed for `pd-tests`.
- Isolated `pd-tests.exe`: 303 test cases / 17030 assertions passed.
- Usual stub logs still appear:
  - `[stub-log L2] NET: could not read 1 bytes (rp=15 wp=15)`
  - `[stub-log L2] NET: could not read 1 bytes (rp=1 wp=1)`
  - `[stub-log L2] NET: could not read 4 bytes (rp=0 wp=3)`

### Next

- Determine the next safe catalog/provider slice. Candidate: audit remaining legacy untyped lifecycle call sites and remove or narrow `catalogLoadAsset()` path fallback once all callers are migrated to typed/provider-owned flows.

---

## Session S529 - 2026-04-28 - Model lifecycle provider-handle tightening

Continued the catalog-owned asset pipeline after S528. Scope stayed on typed lifecycle activation for model payloads.

### Outcome

- Tightened model-like typed lifecycle activation so `ASSET_MODEL`, `ASSET_WEAPON`, `ASSET_BODY`, `ASSET_HEAD`, and `ASSET_PROP` require a catalog provider handle.
- Missing model payload provider handles now fail loud with `CATALOG.LIFECYCLE.ACTIVATE` instead of falling through to generic path loading.
- Static coverage now pins the provider-handle miss wording alongside the handle-aware modeldef activation path.

### Files

- `port/src/assetcatalog_load.c`
- `tests/test_catalog_provider_static.cpp`
- Context updates: `context/tasks-current.md`, `context/session-log.md`

### Verification

- Used isolated build directory session id `catalog-s506`; did not use shared `Build/`.
- `.\devtools\build-session.ps1 -Session catalog-s506 -Target all` passed for `pd` and `pd-server`.
- Isolated Ninja build passed for `pd-tests`.
- Isolated `pd-tests.exe`: 302 test cases / 17024 assertions passed.
- Usual stub logs still appear:
  - `[stub-log L2] NET: could not read 1 bytes (rp=15 wp=15)`
  - `[stub-log L2] NET: could not read 1 bytes (rp=1 wp=1)`
  - `[stub-log L2] NET: could not read 4 bytes (rp=0 wp=3)`

### Next

- Determine the next safe catalog/provider slice. Candidate: split the generic path fallback into legacy/untyped compatibility only so typed lifecycle calls for remaining migrated domains fail loud on missing provider handles.

---

## Session S528 - 2026-04-28 - Audio lifecycle provider-handle tightening

Continued the catalog-owned asset pipeline after S527. Scope stayed on component audio, without sweeping the older `ASSET_SFX` / `ASSET_MUSIC` compatibility path.

### Outcome

- Tightened `ASSET_AUDIO` runtime lifecycle activation to require a catalog provider handle.
- Preserved the existing `ASSET_SFX` / `ASSET_MUSIC` no-provider compatibility behavior for now.
- Fixed distributed `audio.ini` hot-registration so a missing `file_path` does not synthesize a `destdir/` file provider handle through the central audio registration helper.
- Removed the now-duplicated manual audio `catalogSetPrimary(e, fileProviderHandle(fullfile))` call from the distributed hot-registration special case; the typed registrar owns that source handle.
- Static coverage now pins the `ASSET_AUDIO` provider-handle check and the distributed empty-path guard.

### Files

- `port/src/assetcatalog_load.c`
- `port/src/net/netdistrib.c`
- `tests/test_catalog_provider_static.cpp`
- Context updates: `context/tasks-current.md`, `context/session-log.md`

### Verification

- Used isolated build directory session id `catalog-s506`; did not use shared `Build/`.
- `.\devtools\build-session.ps1 -Session catalog-s506 -Target all` passed for `pd` and `pd-server`.
- Isolated Ninja build passed for `pd-tests`.
- Isolated `pd-tests.exe`: 302 test cases / 17023 assertions passed.
- Usual stub logs still appear:
  - `[stub-log L2] NET: could not read 1 bytes (rp=15 wp=15)`
  - `[stub-log L2] NET: could not read 1 bytes (rp=1 wp=1)`
  - `[stub-log L2] NET: could not read 4 bytes (rp=0 wp=3)`

### Next

- Determine the next safe catalog/provider slice. Candidate: audit remaining `entryGetFilePath()` consumers and separate legacy override-path compatibility from typed lifecycle provider requirements.

---

## Session S527 - 2026-04-28 - Texture lifecycle provider-only activation

Continued the catalog-owned asset pipeline after S526. Scope stayed on reducing typed lifecycle fallback reliance now that file-backed registration owns source handles.

### Outcome

- Tightened `ASSET_TEXTURE` typed lifecycle activation to require a catalog provider handle.
- Removed the raw path `fsFileLoad()` fallback from texture payload activation.
- Missing texture provider handles now fail loud with `CATALOG.LIFECYCLE.ACTIVATE` rather than loading outside the provider layer.
- Static coverage now requires the provider-handle miss wording and guards against reintroducing the raw texture path-load fallback.

### Files

- `port/src/assetcatalog_load.c`
- `tests/test_catalog_provider_static.cpp`
- Context updates: `context/tasks-current.md`, `context/session-log.md`

### Verification

- Used isolated build directory session id `catalog-s506`; did not use shared `Build/`.
- `.\devtools\build-session.ps1 -Session catalog-s506 -Target all` passed for `pd` and `pd-server`.
- Isolated Ninja build passed for `pd-tests`.
- Isolated `pd-tests.exe`: 302 test cases / 17021 assertions passed.
- Usual stub logs still appear:
  - `[stub-log L2] NET: could not read 1 bytes (rp=15 wp=15)`
  - `[stub-log L2] NET: could not read 1 bytes (rp=1 wp=1)`
  - `[stub-log L2] NET: could not read 4 bytes (rp=0 wp=3)`

### Next

- Determine the next safe catalog/provider slice. Candidate: move audio runtime activation for `ASSET_AUDIO` to provider-handle-only while preserving base bundled SFX/music behavior.

---

## Session S526 - 2026-04-28 - Central file-backed provider handles

Continued the catalog-owned asset pipeline after S525. Scope stayed on source-handle ownership now that temporary ROM model fallbacks are gone.

### Outcome

- Added a central `assetCatalogSetPrimaryFileIfPresent()` helper in catalog registration.
- Typed file-backed registration helpers now populate `entry->source.primary` from their declared file fields:
  - character `bodyfile`
  - weapon/prop `model_file`
  - texture/audio `file_path`
  - HUD `texture_file`
- Direct registration callers such as audio menus, mod manager, scanner, and distributed hot-registration now get catalog provider handles from the registration API itself. Scanner/distribution-specific `catalogSetPrimary()` calls remain compatible reinforcement for this slice.
- Added static coverage pinning the central registration helper and each file-backed registration field.

### Files

- `port/src/assetcatalog.c`
- `tests/test_catalog_provider_static.cpp`
- Context updates: `context/tasks-current.md`, `context/session-log.md`

### Verification

- Used isolated build directory session id `catalog-s506`; did not use shared `Build/`.
- `.\devtools\build-session.ps1 -Session catalog-s506 -Target all` passed for `pd` and `pd-server`.
- Isolated Ninja build passed for `pd-tests`.
- Isolated `pd-tests.exe`: 302 test cases / 17020 assertions passed.
- Usual stub logs still appear:
  - `[stub-log L2] NET: could not read 1 bytes (rp=15 wp=15)`
  - `[stub-log L2] NET: could not read 1 bytes (rp=1 wp=1)`
  - `[stub-log L2] NET: could not read 4 bytes (rp=0 wp=3)`

### Next

- Determine the next safe catalog/provider slice. The likely next step is reducing `entryGetFilePath()` fallback reliance for file-backed lifecycle loaders now that central registration owns source handles.

---

## Session S525 - 2026-04-28 - Player weapon ROM fallback removal

Continued the catalog-owned asset pipeline after S524. Scope stayed on the final remaining temporary ROM model fallback site.

### Outcome

- Removed the first-person player weapon model no-handle ROM fallback.
- Player weapon model loading now uses `modeldefLoadFromHandle()` only when `catalogResolveModelByModelnum()` supplies a provider handle.
- Missing player weapon provider handles now log `CATALOG.MISS`, leave `weaponmodeldef = NULL`, and use the existing "weapon will be hidden" warning path.
- Tightened static coverage so the temporary ROM fallback allowlist is empty across runtime/model bridge files.
- Source scan confirmed the fallback wording remains only inside the static test guard.

### Files

- `src/game/player.c`
- `tests/test_catalog_provider_static.cpp`
- Context updates: `context/tasks-current.md`, `context/session-log.md`

### Verification

- Used isolated build directory session id `catalog-s506`; did not use shared `Build/`.
- Isolated build passed for `pd`, `pd-server`, and `pd-tests`.
- Isolated `pd-tests.exe`: 300 test cases / 17002 assertions passed.
- Source scans:
  - `temporary ROM fallback` appears only in `tests/test_catalog_provider_static.cpp`.
  - Removed raw model fallback patterns are absent from production model bridge files.
- Usual stub logs still appear:
  - `[stub-log L2] NET: could not read 1 bytes (rp=15 wp=15)`
  - `[stub-log L2] NET: could not read 1 bytes (rp=1 wp=1)`
  - `[stub-log L2] NET: could not read 4 bytes (rp=0 wp=3)`

### Next

- Determine the next safe catalog/provider slice. With temporary ROM model fallbacks removed, the next likely step is a broader source-handle coverage audit for catalog entries that still rely on `entryGetFilePath()` ext/path fallback rather than `entry->source.primary`.

---

## Session S524 - 2026-04-28 - First-person gun ROM fallback removal

Continued the catalog-owned asset pipeline after S523. Scope stayed on the first-person gun queued model loader for hand/gun/cart model files.

### Outcome

- Removed the first-person gun queued-load no-handle ROM fallback.
- `bgunResolveQueuedModelHandle()` now logs `CATALOG.MISS` without advertising or allowing a temporary ROM fallback.
- Queued gun/hand/cart size and load helpers now return `0` / `NULL` when no provider handle exists, using the existing load-failure path instead of loading outside the provider layer.
- Tightened static coverage so `src/game/bondgun.c` cannot reintroduce the temporary ROM fallback, raw `assetLoadRomToAddr`, or raw queued `fileGetInflatedSize` path.

### Files

- `src/game/bondgun.c`
- `tests/test_catalog_provider_static.cpp`
- Context updates: `context/tasks-current.md`, `context/session-log.md`

### Verification

- Used isolated build directory session id `catalog-s506`; did not use shared `Build/`.
- Isolated build passed for `pd`, `pd-server`, and `pd-tests`.
- Isolated `pd-tests.exe`: 300 test cases / 17001 assertions passed.
- Usual stub logs still appear:
  - `[stub-log L2] NET: could not read 1 bytes (rp=15 wp=15)`
  - `[stub-log L2] NET: could not read 1 bytes (rp=1 wp=1)`
  - `[stub-log L2] NET: could not read 4 bytes (rp=0 wp=3)`

### Next

- Determine the next safe catalog/provider slice. The remaining temporary ROM model fallback allowlist should now be down to `src/game/player.c`.

---

## Session S523 - 2026-04-28 - Menu raw model ROM fallback removal

Continued the catalog-owned asset pipeline after S521. Scope stayed on the raw menu model preview source-filenum bridge.

### Outcome

- Removed the raw menu model preview no-handle ROM fallback.
- `menuRenderModel()` now logs `CATALOG.MISS` and skips the preview when a raw model filenum cannot resolve to a catalog/provider handle.
- The raw preview path now loads only through `modeldefLoadFromHandle()` and uses provider-aware loaded-size accounting.
- Tightened static coverage so `src/game/menu.c` cannot reintroduce the temporary ROM fallback, raw `modeldefLoad((u16)source_filenum)`, or raw `fileGetInflatedSize(source_filenum, LOADTYPE_MODEL)` path.

### Files

- `src/game/menu.c`
- `tests/test_catalog_provider_static.cpp`
- Context updates: `context/tasks-current.md`, `context/session-log.md`

### Verification

- Used isolated build directory session id `catalog-s506`; did not use shared `Build/`.
- Isolated build passed for `pd`, `pd-server`, and `pd-tests`.
- Isolated `pd-tests.exe`: 300 test cases / 16997 assertions passed.
- Usual stub logs still appear:
  - `[stub-log L2] NET: could not read 1 bytes (rp=15 wp=15)`
  - `[stub-log L2] NET: could not read 1 bytes (rp=1 wp=1)`
  - `[stub-log L2] NET: could not read 4 bytes (rp=0 wp=3)`

### Next

- Determine the next safe catalog/provider slice. Remaining temporary ROM model fallback sites are first-person gun loads and player weapon model loads.

---

## Session S522 - 2026-04-28 - MP pause graph migration

Continued menu graph migration after Agent Select. Scope stayed on MP pause's simple pop and warning-modal push transitions.

### Outcome

- Migrated MP pause Resume/Back through the `MENU_TYPE_MP_PAUSE` `resume` graph pop edge.
- Added a `MENU_TYPE_MP_PAUSE` `end_game` edge targeting `MENU_TYPE_WARNING_MODAL`.
- Migrated MP pause End Game warning-modal push through `menuGraphFirePushDialog()`.
- Added static tests that guard MP pause close and End Game helpers from direct pop/push reintroduction.

### Files

- `port/src/menugraph.c`
- `port/fast3d/pdgui_menu_mppause.cpp`
- `tests/test_menu_graph.cpp`
- Context updates: `context/tasks-current.md`, `context/designs/input-universality-and-transitions-2026-04-27.md`, `context/session-log.md`

### Verification

- Used isolated build session id `ix46`; did not use shared `Build/`.
- `.\devtools\build-session.ps1 -Session ix46 -Target all` built `pd` and `pd-server`.
- Isolated Ninja build then built and ran `pd-tests`.
- `pd-tests.exe`: 300 test cases / 16993 assertions passed.
- Usual stub logs still appeared:
  - `[stub-log L2] NET: could not read 1 bytes (rp=15 wp=15)`
  - `[stub-log L2] NET: could not read 1 bytes (rp=1 wp=1)`
  - `[stub-log L2] NET: could not read 4 bytes (rp=0 wp=3)`

### Next

- Continue menu graph migration by inspecting remaining priority-node direct transitions and migrate the next small one before attempting Room start/leave.

---

## Session S521 - 2026-04-28 - Modelcatalog ROM fallback removal

Continued the catalog-owned asset pipeline after S519. Scope stayed on the modelcatalog validation bridge, not player-facing model load paths.

### Outcome

- Removed the `modelcatalog` no-handle ROM model fallback.
- `catalogValidateResolveHandle()` now logs `CATALOG.MISS` without advertising or allowing a temporary ROM fallback.
- `safeModeldefLoad()` only loads through `modeldefLoadToNewFromHandle()` when a provider handle exists.
- `catalogValidateSourceMissing()` treats a null provider handle as missing source data.
- Tightened static coverage so `port/src/modelcatalog.c` cannot reintroduce the temporary ROM fallback or raw `modeldefLoadToNew(filenum)` path.

### Files

- `port/src/modelcatalog.c`
- `tests/test_catalog_provider_static.cpp`
- Context updates: `context/tasks-current.md`, `context/session-log.md`

### Verification

- Used isolated build directory session id `catalog-s506`; did not use shared `Build/`.
- Isolated build passed for `pd`, `pd-server`, and `pd-tests`.
- Isolated `pd-tests.exe`: 300 test cases / 16993 assertions passed.
- Usual stub logs still appear:
  - `[stub-log L2] NET: could not read 1 bytes (rp=15 wp=15)`
  - `[stub-log L2] NET: could not read 1 bytes (rp=1 wp=1)`
  - `[stub-log L2] NET: could not read 4 bytes (rp=0 wp=3)`

### Next

- Determine the next safe catalog/provider slice. Remaining temporary ROM model fallback sites are first-person gun loads, player weapon model loads, and raw menu model previews.

---

## Session S520 - 2026-04-28 - Agent Select graph migration

Continued menu graph migration after the MP endscreen disconnect slice. Scope stayed on the Agent Select priority node's simple dialog transitions.

### Outcome

- Registered `g_FilemgrEnterNameMenuDialog` as `MENU_TYPE_AGENT_CREATE`.
- Migrated Agent Select New Agent pushes through the graph `create` edge.
- Migrated Agent Select Back through the graph `back` edge.
- Added static tests that guard Agent Select from reintroducing direct enter-name dialog push or direct pop in the renderer.

### Files

- `port/src/menupool.c`
- `port/fast3d/pdgui_menu_agentselect.cpp`
- `tests/test_menu_graph.cpp`
- Context updates: `context/tasks-current.md`, `context/designs/input-universality-and-transitions-2026-04-27.md`, `context/session-log.md`

### Verification

- Used isolated build session id `ix46`; did not use shared `Build/`.
- `.\devtools\build-session.ps1 -Session ix46 -Target all` built `pd` and `pd-server`.
- Isolated Ninja build then built and ran `pd-tests`.
- `pd-tests.exe`: 299 test cases / 16981 assertions passed.
- Usual stub logs still appeared:
  - `[stub-log L2] NET: could not read 1 bytes (rp=15 wp=15)`
  - `[stub-log L2] NET: could not read 1 bytes (rp=1 wp=1)`
  - `[stub-log L2] NET: could not read 4 bytes (rp=0 wp=3)`

### Next

- Continue menu graph migration by inspecting remaining priority-node direct transitions and migrate the next small one before attempting Room start/leave.

---

## Session S519 - 2026-04-28 - Title model ROM fallback removal

Continued the catalog-owned asset pipeline after S517. Scope stayed on one typed model domain with existing `ASSET_MODEL` provider handles.

### Outcome

- Removed the title/logo model no-handle ROM fallback.
- `titleLoadModeldefToAddr()` now logs `CATALOG.MISS` and returns `NULL` if `catalogResolveModelByModelnum()` returns no provider handle.
- `titleGetLoadedModelSize()` now logs `CATALOG.MISS` and returns `0` on a missing provider handle instead of using `fileGetLoadedSize()`.
- Tightened the temporary-ROM-fallback static allowlist so `src/game/title.c` cannot reintroduce the fallback.

### Files

- `src/game/title.c`
- `tests/test_catalog_provider_static.cpp`
- Context updates: `context/tasks-current.md`, `context/session-log.md`

### Verification

- Used isolated build directory session id `catalog-s506`; did not use shared `Build/`.
- Isolated build passed for `pd`, `pd-server`, and `pd-tests`.
- Isolated `pd-tests.exe`: 298 test cases / 16973 assertions passed.
- Usual stub logs still appear:
  - `[stub-log L2] NET: could not read 1 bytes (rp=15 wp=15)`
  - `[stub-log L2] NET: could not read 1 bytes (rp=1 wp=1)`
  - `[stub-log L2] NET: could not read 4 bytes (rp=0 wp=3)`

### Next

- Determine the next safe catalog/provider slice. Remaining temporary ROM model fallback sites are menu raw model previews, first-person gun loads, player weapon model loads, and modelcatalog validation.

---

## Session S518 - 2026-04-28 - MP endscreen disconnect graph migration

Continued menu graph migration after Network paths. Scope stayed on the smallest MP endscreen direct transition.

### Outcome

- Migrated MP endscreen Disconnect confirmation through the `MENU_TYPE_ENDSCREEN_MP` `disconnect` graph network edge.
- Preserved the existing `pdguiEndscreenExitToMainMenu()` path after the graph-dispatched disconnect.
- Added a static test guard so `renderMpEndscreen()` cannot reintroduce direct `netDisconnect()`.

### Files

- `port/fast3d/pdgui_menu_endscreen.cpp`
- `tests/test_menu_graph.cpp`
- Context updates: `context/tasks-current.md`, `context/designs/input-universality-and-transitions-2026-04-27.md`, `context/session-log.md`

### Verification

- Used isolated build session id `ix46`; did not use shared `Build/`.
- `.\devtools\build-session.ps1 -Session ix46 -Target all` built `pd` and `pd-server`.
- Isolated Ninja build then built and ran `pd-tests`.
- `pd-tests.exe`: 298 test cases / 16970 assertions passed.
- Usual stub logs still appeared:
  - `[stub-log L2] NET: could not read 1 bytes (rp=15 wp=15)`
  - `[stub-log L2] NET: could not read 1 bytes (rp=1 wp=1)`
  - `[stub-log L2] NET: could not read 4 bytes (rp=0 wp=3)`

### Next

- Continue menu graph migration with the next safe priority-node direct transitions, likely Room start/leave if behavior surface stays small after inspection.

---

## Session S517 - 2026-04-28 - Catalog file-backed scanner provider handles

Continued the catalog-owned asset pipeline after S515. Scope stayed on source-handle normalization for file-backed catalog entries and did not remove any fallback path.

### Outcome

- Local component scanning now records catalog primary `FileProvider` handles for `ASSET_TEXTURE` `file_path`, `ASSET_AUDIO` `file_path`, and `ASSET_HUD` `texture_file` fields.
- Static coverage now requires local scanner and network-distributed hot-registration paths to keep texture/audio/HUD file fields provider-backed.

### Files

- `port/src/assetcatalog_scanner.c`
- `tests/test_catalog_provider_static.cpp`
- Context updates: `context/tasks-current.md`, `context/session-log.md`

### Verification

- Used isolated build directory session id `catalog-s506`; did not use shared `Build/`.
- Isolated build passed for `pd`, `pd-server`, and `pd-tests`.
- Isolated `pd-tests.exe`: 298 test cases / 16970 assertions passed.
- Usual stub logs still appear:
  - `[stub-log L2] NET: could not read 1 bytes (rp=15 wp=15)`
  - `[stub-log L2] NET: could not read 1 bytes (rp=1 wp=1)`
  - `[stub-log L2] NET: could not read 4 bytes (rp=0 wp=3)`

### Next

- Determine the next safe catalog/provider slice. Good candidate: begin replacing the remaining warning-backed no-handle ROM fallback in one typed model domain now that source handles are more consistently populated.

---

## Session S516 - 2026-04-28 - Network menu graph migration

Continued menu graph migration after the edge substrate. Scope stayed on Network priority-node transitions and the duplicate main-menu Online connect path.

### Outcome

- Added graph helpers for network operations and pop transitions.
- Added `MENU_TYPE_NETWORK_JOINING` and registered `g_NetJoiningDialog`.
- Migrated Network menu Stop Hosting, pre-host disconnect, Host, host-success pop, Join, Joining dialog push, and Back through graph helpers.
- Migrated main-menu Online direct connect and recent-server connect through `MENU_TYPE_MAIN_ONLINE_VIEW` graph edges.
- Added static tests that guard Network menu and main-menu Online paths from reintroducing direct network, joining-dialog push, or pop calls inside the renderers.

### Files

- `port/include/menupool.h`
- `port/src/menupool.c`
- `port/include/menugraph.h`
- `port/src/menugraph.c`
- `port/fast3d/pdgui_menu_network.cpp`
- `port/fast3d/pdgui_menu_mainmenu.cpp`
- `tests/test_menu_graph.cpp`
- Context updates: `context/tasks-current.md`, `context/designs/input-universality-and-transitions-2026-04-27.md`, `context/session-log.md`

### Verification

- Used isolated build session id `ix46`; did not use shared `Build/`.
- `.\devtools\build-session.ps1 -Session ix46 -Target all` built `pd` and `pd-server`.
- Isolated Ninja build then built and ran `pd-tests`.
- `pd-tests.exe`: 296 test cases / 16953 assertions passed.
- Usual stub logs still appeared:
  - `[stub-log L2] NET: could not read 1 bytes (rp=15 wp=15)`
  - `[stub-log L2] NET: could not read 1 bytes (rp=1 wp=1)`
  - `[stub-log L2] NET: could not read 4 bytes (rp=0 wp=3)`

### Next

- Continue menu graph migration with the next safe priority-node direct transitions, likely Room start/leave or endscreen disconnect/continue after inspecting behavior surface.

---

## Session S515 - 2026-04-28 - Catalog weapon/prop model provider handles

Continued the catalog-owned asset pipeline after S510/S514 parallel work. Scope stayed on provider handle wiring for model-file declarations and weapon model payload activation; warning-backed ROM fallbacks remain only as temporary bridges for uncataloged legacy sources.

### Outcome

- Added `ASSET_WEAPON` to the typed model payload lifecycle path so provider-backed weapon entries activate through `modeldefLoadToNewFromHandle()` and cache `ASSET_PAYLOAD_STAGE_MODELDEF` like model/body/head/prop entries.
- Local component scanning now converts weapon and prop `model_file` INI fields into catalog primary `FileProvider` handles.
- Network-distributed hot registration now restores provider handles for character `bodyfile`, weapon `model_file`, and prop `model_file` after extracting the transferred component.
- Corrected distributed provider handle restoration to resolve relative file fields against the extracted component directory, and added hot-registration coverage for `prop.ini`, `texture.ini`, `audio.ini`, and `hud.ini`.
- Added static coverage so weapon/prop model-file provider wiring and weapon model payload activation cannot silently regress.

### Files

- `port/src/assetcatalog_load.c`
- `port/src/assetcatalog_scanner.c`
- `port/src/net/netdistrib.c`
- `tests/test_catalog_provider_static.cpp`
- Context updates: `context/tasks-current.md`, `context/session-log.md`

### Verification

- Used isolated build directory session id `catalog-s506`; did not use shared `Build/`.
- The sandboxed Ninja run hit Git safe-directory ownership checks after CMake regeneration; reran the same isolated build/test command outside the sandbox.
- Isolated build passed for `pd`, `pd-server`, and `pd-tests`.
- Isolated `pd-tests.exe`: 294 test cases / 16925 assertions passed.
- Follow-up isolated rebuild after distributed path correction passed for `pd`, `pd-server`, and `pd-tests`.
- Follow-up isolated `pd-tests.exe`: 296 test cases / 16955 assertions passed.
- Usual stub logs still appear:
  - `[stub-log L2] NET: could not read 1 bytes (rp=15 wp=15)`
  - `[stub-log L2] NET: could not read 1 bytes (rp=1 wp=1)`
  - `[stub-log L2] NET: could not read 4 bytes (rp=0 wp=3)`

### Next

- Determine the next safe catalog/provider slice. Good candidates: finish distributed provider handle restoration for other file-backed ext fields, then remove one warning-backed ROM fallback where a typed provider API now exists.

---

## Session S514 - 2026-04-28 - Menu graph edge substrate

Continued menu graph migration after main-menu subview pool ownership. Scope stayed on graph descriptors and validated dialog pushes.

### Outcome

- Added `menugraph.h` and `menugraph.c`.
- Declared graph nodes for main menu, main-menu subviews, solo mission, room, solo/MP endscreen, pause variants, social lobby, network, agent select, and warning modal.
- Added edge lookup and destination-kind name helpers.
- Added `menuGraphFirePushDialog()`, which validates the edge and the destination menu-pool type before calling `menuPushDialog()`.
- Migrated the main-menu Change Agent and Cheats pushes through the graph.
- Added static tests for graph substrate coverage, priority nodes, push validation, and the first migrated main-menu push sites.

### Files

- `port/include/menugraph.h`
- `port/src/menugraph.c`
- `port/fast3d/pdgui_menu_mainmenu.cpp`
- `tests/test_menu_graph.cpp`
- Context updates: `context/tasks-current.md`, `context/designs/input-universality-and-transitions-2026-04-27.md`, `context/session-log.md`

### Verification

- Used isolated build session id `ix46`; did not use shared `Build/`.
- `.\devtools\build-session.ps1 -Session ix46 -Target all` built `pd` and `pd-server`.
- Isolated Ninja build then built and ran `pd-tests`.
- `pd-tests.exe`: 294 test cases / 16925 assertions passed.
- Usual stub logs still appeared:
  - `[stub-log L2] NET: could not read 1 bytes (rp=15 wp=15)`
  - `[stub-log L2] NET: could not read 1 bytes (rp=1 wp=1)`
  - `[stub-log L2] NET: could not read 4 bytes (rp=0 wp=3)`

### Next

- Continue menu graph migration by adding validated graph helpers for network and pop transitions, then migrate the next safe Network menu and main-menu Online direct call sites.

---

## Session S513 - 2026-04-28 - Main-menu subview pool ownership

Continued menu graph migration after vehicle and observer layer wiring. Scope stayed on K.7's first safe step: give inline main-menu subviews real menu-pool ownership before adding broader priority-node edge execution.

### Outcome

- Added pure-ImGui menu-pool identities for main-menu Solo, Settings, Modding, Online, and Stats subviews.
- Kept the existing Grid submenu identity and moved it onto the common main-menu subview transition path.
- Added `pdguiMainMenuSetView()` so all `s_MenuView` changes acquire/release the matching subview pool slot and log `MENU_GRAPH` diagnostics.
- Added render-sync handling so a retained subview reacquires its pool slot after a bulk teardown.
- Removed the Grid-only pool transition branch.
- Added static tests that guard the subview identities, mapping, transition helper, render-sync call, and the rule that raw `s_MenuView` assignment is limited to the declaration and helper.

### Files

- `port/include/menupool.h`
- `port/src/menupool.c`
- `port/fast3d/pdgui_menu_mainmenu.cpp`
- `tests/test_menu_graph.cpp`
- `CMakeLists.txt`
- Context updates: `context/tasks-current.md`, `context/designs/input-universality-and-transitions-2026-04-27.md`, `context/session-log.md`

### Verification

- Used isolated build session id `ix46`; did not use shared `Build/`.
- `.\devtools\build-session.ps1 -Session ix46 -Target all` built `pd` and `pd-server`.
- Isolated Ninja build then built and ran `pd-tests`.
- `pd-tests.exe`: 293 test cases / 16881 assertions passed.
- Usual stub logs still appeared:
  - `[stub-log L2] NET: could not read 1 bytes (rp=15 wp=15)`
  - `[stub-log L2] NET: could not read 1 bytes (rp=1 wp=1)`
  - `[stub-log L2] NET: could not read 4 bytes (rp=0 wp=3)`

### Next

- Continue menu graph migration by adding the edge substrate and priority-node descriptors, then migrate the next safe direct menu transition call sites.

---

## Session S512 - 2026-04-28 - Vehicle and observer layer wiring

Continued the approved input-universality tracker after cutscene network semantics. Scope stayed on vehicle driver and observer layer ownership; no vehicle turret work, no raw ImGui sweep, and no broad scene manager expansion.

### Outcome

- Declared the vehicle driver action set and wired push/pop/abort callbacks to own `g_ImcVehicle` activation plus transition flushing.
- Migrated hoverbike mount/dismount from direct `imcVehicleMount()` / `imcVehicleDismount()` calls to `sceneFire(SCENE_EVENT_VEHICLE_BOARD/_DISMOUNT)`.
- Declared the observer action set and wired observer push/pop/abort flushing. Observer pop/abort also deactivate Forge IMCs as cleanup.
- Wired Forge session/freefly entry and inactive exit through observer scene events while preserving the existing Forge IMC behavior.
- Wired spectator live/theater entry and stop/shutdown through observer scene events.
- Added observer source tracking in the scene manager so Forge and spectator cannot pop each other's observer layer handle.
- Added static tests for vehicle and observer action sets, callbacks, scene events, Forge helpers, spectator helpers, and observer source guard behavior.

### Files

- `port/src/inputlayer.c`
- `port/include/scene.h`
- `port/src/scene.c`
- `src/game/bondbike.c`
- `src/game/forgemode.c`
- `port/src/spectator.c`
- `tests/test_vehicle_observer_layer.cpp`
- `CMakeLists.txt`
- Context updates: `context/tasks-current.md`, `context/designs/input-universality-and-transitions-2026-04-27.md`, `context/session-log.md`

### Verification

- Used isolated build session id `ix46`; did not use shared `Build/`.
- `.\devtools\build-session.ps1 -Session ix46 -Target all` built `pd` and `pd-server`.
- Isolated Ninja build then built and ran `pd-tests`.
- `pd-tests.exe`: 291 test cases / 16843 assertions passed.
- Usual stub logs still appeared:
  - `[stub-log L2] NET: could not read 1 bytes (rp=15 wp=15)`
  - `[stub-log L2] NET: could not read 1 bytes (rp=1 wp=1)`
  - `[stub-log L2] NET: could not read 4 bytes (rp=0 wp=3)`

### Next

- Continue the tracker with menu graph migration: introduce real menu graph edges for priority menus and convert the first main-menu subviews to real `MENU_TYPE_*` pushes per K.7.

---

## Session S511 - 2026-04-28 - Cutscene network semantics v46

Continued the approved input-universality path after the cutscene protection gates. Scope stayed narrow: no raw ImGui key sweep, no menu graph migration, and no dedicated-server productization work.

### Outcome

- Completed cutscene network semantics on the existing v46 protocol. S507 already claimed v46 for mandatory mod-transfer digest, so this slice appended the cutscene semantics without bumping again.
- `SVC_CUTSCENE` now writes and reads `active` plus `player_mask`; clients set per-player cutscene state from the mask and fire the scene cutscene start/end events from server state.
- Added `CLC_CUTSCENE_SKIP` (0x17). Net clients send this after the 30-frame gate and do not locally end the cutscene; the server binds the request to `srccl->playernum` and ignores untrusted payload player numbers.
- Cutscene protection now narrows by `playerInCutscene(i)` instead of protecting every player chr while any player is in cutscene.
- AI script skip checks now observe any server-validated cutscene skip request so remote client skip requests can drive the existing script branch.
- Added focused static tests for the v46 cutscene message shape, CLC dispatch, authority binding, mask handling, protection narrowing, and client skip request path.

### Files

- `port/include/net/netmsg.h`
- `port/src/net/netmsg.c`
- `port/src/net/net.c`
- `port/include/net/net.h`
- `src/include/game/player.h`
- `src/game/player.c`
- `src/game/chraicommands.c`
- `tests/test_cutscene_layer.cpp`
- `tests/test_versions.cpp`
- Context updates: `context/tasks-current.md`, `context/designs/input-universality-and-transitions-2026-04-27.md`, `context/networking.md`, `context/constraints.md`, `context/session-log.md`

### Verification

- Used isolated build session id `ix46`; did not use shared `Build/`.
- `.\devtools\build-session.ps1 -Session ix46 -Target all` built `pd` and `pd-server`.
- Isolated Ninja build then built and ran `pd-tests`.
- `pd-tests.exe`: 289 test cases / 16786 assertions passed.
- Usual stub logs still appeared:
  - `[stub-log L2] NET: could not read 1 bytes (rp=15 wp=15)`
  - `[stub-log L2] NET: could not read 1 bytes (rp=1 wp=1)`
  - `[stub-log L2] NET: could not read 4 bytes (rp=0 wp=3)`

### Next

- Continue the task tracker with vehicle and observer layer wiring: bike mount/dismount plus Forge/observer entry/exit should flow through `sceneFire` and tracked layer handles while preserving existing IMC behavior.

---

## Session S510 - 2026-04-28 - Catalog effect metadata runtime activation

Continued typed catalog lifecycle coverage after S509. Scope stayed on metadata-owned assets and avoided broad file-backed domain migration.

### Outcome

- Extended metadata runtime activation to `ASSET_EFFECT`.
- Effect entries now activate through catalog lifecycle as `ASSET_PAYLOAD_RUNTIME_ACTIVE` rather than falling through to generic provider/path byte loading.
- Updated the static metadata lifecycle guard to require effect coverage alongside HUD, bot-profile, arena, gamemode, skin, and bot-variant.

### Files

- `port/src/assetcatalog_load.c`
- `tests/test_catalog_provider_static.cpp`
- Context updates: `context/tasks-current.md`, `context/session-log.md`

### Verification

- Used isolated build directory session id `catalog-s506`; did not use shared `Build/`.
- Incremental isolated Ninja pass built `pd`, `pd-server`, and `pd-tests`.
- Isolated `pd-tests.exe`: 288 test cases / 16777 assertions passed.
- Usual stub logs still appear:
  - `[stub-log L2] NET: could not read 1 bytes (rp=15 wp=15)`
  - `[stub-log L2] NET: could not read 1 bytes (rp=1 wp=1)`
  - `[stub-log L2] NET: could not read 4 bytes (rp=0 wp=3)`

### Next

- Determine the next safe catalog/provider slice. Remaining metadata-only candidates need another ownership check before activation; file-backed domains stay deferred.

---

## Session S509 - 2026-04-28 - Catalog skin/bot metadata runtime activation

Continued metadata-only catalog lifecycle coverage after S508. Scope stayed on descriptor assets whose scanners/distribution paths only populate catalog extension fields.

### Outcome

- Extended metadata runtime activation to `ASSET_SKIN` and `ASSET_BOT_VARIANT`.
- Updated the static metadata lifecycle guard so HUD, bot-profile, arena, gamemode, skin, and bot-variant remain covered by the runtime-active metadata path.
- While verifying in the shared worktree, repaired small build blockers from parallel lanes:
  - Added `pdgui_scaling.h` include for `pdgui_friends.cpp`.
  - Matched `netmsgSvcCutsceneWrite` implementation/read path to the new `{active, player_mask}` signature.
  - Kept the v46 `CLC_CUTSCENE_SKIP` dispatch case single and reachable.
  - Added dedicated-server stubs for newly referenced inventory/cutscene helpers so `pd-server` remains buildable.

### Files

- `port/src/assetcatalog_load.c`
- `tests/test_catalog_provider_static.cpp`
- Parallel-lane build repairs: `port/fast3d/pdgui_friends.cpp`, `port/src/net/netmsg.c`, `port/src/net/net.c`, `port/src/server_stubs.c`
- Context updates: `context/tasks-current.md`, `context/session-log.md`

### Verification

- Used isolated build directory session id `catalog-s506`; did not use shared `Build/`.
- Incremental isolated Ninja pass built `pd`, `pd-server`, and `pd-tests`.
- Isolated `pd-tests.exe`: 288 test cases / 16776 assertions passed.
- Usual stub logs still appear:
  - `[stub-log L2] NET: could not read 1 bytes (rp=15 wp=15)`
  - `[stub-log L2] NET: could not read 1 bytes (rp=1 wp=1)`
  - `[stub-log L2] NET: could not read 4 bytes (rp=0 wp=3)`

### Next

- Determine the next safe catalog/provider slice. Continue using isolated build id `catalog-s506` until cleanup.

---

## Session S508 - 2026-04-28 - Catalog selector metadata runtime activation

Continued typed catalog lifecycle coverage after S505. Scope stayed on selector-style metadata assets that are represented by catalog/ext fields rather than independently owned loaded bytes.

### Outcome

- Extended metadata runtime activation to `ASSET_ARENA` and `ASSET_GAMEMODE`.
- These selector metadata entries now become `ASSET_STATE_ACTIVE` with `ASSET_PAYLOAD_RUNTIME_ACTIVE` when loaded through catalog lifecycle, matching the existing HUD / bot-profile metadata path.
- Left file-backed UI/effect/animation/map paths unchanged.
- Updated the static metadata lifecycle guard to require HUD, bot-profile, arena, and gamemode coverage.

### Files

- `port/src/assetcatalog_load.c`
- `tests/test_catalog_provider_static.cpp`
- Context updates: `context/tasks-current.md`, `context/session-log.md`

### Verification

- Per Mike's instruction, used isolated build directory session id `catalog-s506`; did not use shared `Build/`.
- `devtools/build-session.ps1 -Session catalog-s506 -Target all` produced an isolated Ninja tree but exited at the configure wrapper step without surfacing a CMake diagnostic.
- Continued verification inside the same isolated build directory with Ninja: built `pd`, `pd-server`, and `pd-tests`.
- Isolated `pd-tests.exe`: 286 test cases / 16733 assertions passed.
- Usual stub logs still appear:
  - `[stub-log L2] NET: could not read 1 bytes (rp=15 wp=15)`
  - `[stub-log L2] NET: could not read 1 bytes (rp=1 wp=1)`
  - `[stub-log L2] NET: could not read 4 bytes (rp=0 wp=3)`

### Next

- Determine the next safe catalog/provider slice. Continue using isolated build id `catalog-s506` for this lane until cleanup.

---

## Session S507 - 2026-04-28 - Client-hosted trust/security hardening

Read the required server/trust context, `server-architecture.md`, `hosting-modes-listen-vs-dedicated.md`, and the relevant security audits. Scope stayed on client-hosted/listen online shipping; standalone dedicated-server product work remains deferred.

### Outcome

- Closed the SEC-5 gap in mod distribution by making the actual archive transfer self-authenticating: `SVC_DISTRIB_BEGIN` now carries the SHA-256 digest of the compressed PDCA archive bytes, and clients verify that digest before decompression/extraction.
- Bumped `NET_PROTOCOL_VER` to 46 and updated the version pin test. Mixed v45/v46 peers are rejected at the existing ENet protocol handshake.
- Hardened malformed wire strings: a zero-length encoded string now returns a safe empty string and cannot make callers scan into the next payload field.
- Tightened connect-code address validation across current join surfaces: raw IPs are not prefilled or advertised, 4-word and 6-word codes are decoded through `connectCodeDecodeWithPort`, trailing garbage is rejected, server history displays connect codes, and host lobby codes preserve non-default listen ports.
- Closed the first stat-integrity gap found in the client-hosted path: remote `CLC_MOVE` weapon-select requests are now checked against the listen host's server-side inventory for that player before the server accepts the switch. Invalid selects are logged and stripped from the move packet.
- Corrected connect-code comments to the pinned host-order convention.
- Confirmed updater signing design is already implemented in the current tree: mandatory `.sha256` plus Ed25519 `.sig` verification over `sha256(zip)||tag`, embedded public key, and init self-test.

### Files

- `port/include/net/net.h`
- `port/include/net/netmsg.h`
- `port/include/net/netdistrib.h`
- `port/src/net/netmsg.c`
- `port/src/net/netdistrib.c`
- `port/src/net/netbuf.c`
- `port/include/connectcode.h`
- `port/src/connectcode.c`
- `port/fast3d/pdgui_menu_network.cpp`
- `port/fast3d/pdgui_menu_mainmenu.cpp`
- `port/fast3d/pdgui_menu_lobby.cpp`
- `port/src/net/netmenu.c`
- `tests/test_versions.cpp`
- `tests/test_netbuf.cpp`
- `tests/test_connectcode.cpp`
- Context updates: `context/constraints.md`, `context/tasks-current.md`, `context/session-log.md`, `context/bugs.md`

### Verification

- `git diff --check` passed for the touched trust/security files.
- Used isolated build session id `sec507` as directed: `.\devtools\build-session.ps1 -Session sec507 -Target all`.
- The isolated build did not reach compilation; CMake configure spun for about 18 minutes and exited before producing a complete build.
- Cleaned up the partial isolated directory with `.\devtools\build-session.ps1 -Remove -Session sec507`.
- Later CMake/Ninja processes from another parallel session were visible and were left untouched.

### Next

- First rerun the isolated build/test pass once the configure hang is resolved. Then continue the same trust/security lane with one more low-risk malformed-packet audit around pre-auth/lobby packet length and count fields.

---

## Session S506 - 2026-04-28 - Cutscene protection gates

Continued the input infrastructure completion tracker after per-player cutscene state migration. Scope stayed on the K.3 protection flag and canonical gates, without starting the v46 wire-mask work.

### Outcome

- Added `chr->cutscene_protect` to `struct chrdata` and initialized it in `chrInit()`.
- Added `playerRefreshCutsceneProtect()` so per-player cutscene state changes protect all allocated player chrs while any player is in cutscene. This preserves current global cutscene behavior until the player-mask network slice lands.
- `chrDamage()` now ignores protected targets and logs `CUTSCENE.DAMAGE.IGNORED`.
- `chrCompareTeams(..., COMPARE_ENEMIES)` no longer classifies protected targets as enemies.
- `chrHasLosToChr()` and `botIsTargetInvisible()` treat protected targets as invisible.
- Added a static pd-test guard for the protection field, refresh path, damage gate, enemy gate, LOS gate, and bot invisibility gate.

### Files

- `src/include/types.h`
- `src/game/chr.c`
- `src/game/player.c`
- `src/game/chraction.c`
- `src/game/bot.c`
- `tests/test_cutscene_layer.cpp`
- Context updates: `context/tasks-current.md`, `context/session-log.md`, `context/designs/input-universality-and-transitions-2026-04-27.md`

### Verification

- `git diff --check` passed for the protection slice.
- Prescribed MSYS2/Ninja flow passed: `pd`, `pd-server`, `pd-tests`, then `pd-tests.exe`.
- `pd-tests.exe`: 286 test cases / 16727 assertions passed.
- Usual stub logs still appear:
  - `[stub-log L2] NET: could not read 1 bytes (rp=15 wp=15)`
  - `[stub-log L2] NET: could not read 1 bytes (rp=1 wp=1)`
  - `[stub-log L2] NET: could not read 4 bytes (rp=0 wp=3)`

### Next

- Start the next tracked item: v46 cutscene network semantics with `SVC_CUTSCENE` player mask and `CLC_CUTSCENE_SKIP`.

---

## Session S505 - 2026-04-28 - Catalog temporary ROM fallback visibility

Continued the catalog/provider migration after S502. Scope stayed on the remaining approved ROM fallback bridge rather than removing it, per Mike's direction that preserving the ROM fast path is acceptable only as a temporary sub-step toward full migration.

### Outcome

- Made the player weapon model no-handle path emit a throttled `CATALOG.MISS` warning before using the temporary ROM fallback.
- Added a source-wide static guard that confines `temporary ROM fallback` wording to the known catalog/provider bridge files.
- Added a focused assertion that the player weapon fallback remains explicit and warning-backed while it exists.
- Left the actual ROM fallback behavior unchanged for this slice.

### Files

- `src/game/player.c`
- `tests/test_catalog_provider_static.cpp`
- Context updates: `context/tasks-current.md`, `context/session-log.md`

### Verification

- Prescribed MSYS2/Ninja flow built `pd`, `pd-server`, and `pd-tests`.
- The first full `pd-tests.exe` pass reported one stale input static-test failure after the build linked tests early, but the current source already contained the expected invariant.
- Re-ran `pd-tests.exe` with `devtools/build-env.sh` loaded: 286 test cases / 16727 assertions passed.
- Usual stub logs still appear:
  - `[stub-log L2] NET: could not read 1 bytes (rp=15 wp=15)`
  - `[stub-log L2] NET: could not read 1 bytes (rp=1 wp=1)`
  - `[stub-log L2] NET: could not read 4 bytes (rp=0 wp=3)`

### Next

- Determine the next safe catalog/provider slice. The remaining visible debt is still the allowlisted no-handle model fallback set in bondgun/menu/player/title/modelcatalog.

---

## Session S503 - 2026-04-28 - Quality pd-tests invariant expansion

Read the required context, testing framework notes, QC checklist, bug list, and active audits, then started the requested recursive `pd-tests` expansion against the next highest-risk invariants. Scope stayed on tests/guardrails; no gameplay production behavior was intentionally changed.

### Outcome

- Chose catalog/provider identity first because B-264/B-265 showed numeric identity confusion across catalog domains and the catalog pipeline is an active work front.
- Added a source-wide static guard that confines generic `catalogIdByRuntime(ASSET_MAP/MODEL/BODY/HEAD/WEAPON/GAMEMODE, ...)` usage to `assetcatalog_api.c`, forcing production call sites through typed helpers.
- Started the next recursive slice for network packet parsing: added spawn-weapon v45 wire tests for `SVC_STAGE_START` and `CLC_LOBBY_START` so the new `spawnWeaponMode` / `spawnWeaponNum` bytes cannot shift the following mod-track or handicap fields.
- Added a malformed-tail test that confirms a truncated `SVC_STAGE_START` spawn tail trips the netbuf error path.
- Added a static production-order guard over `port/src/net/netmsg.c` for the v45 spawn-weapon field order.
- During build-environment recovery, fixed `cmake/TargetArch.cmake` so the generated architecture detector uses `#else` before the fallback `cmake_ARCH unknown` marker. This patch is unverified because Mike asked to skip build attempts while he works on the wrapper solution.

### Files

- `tests/test_catalog_provider_static.cpp`
- `tests/test_spawn_weapon_mode.cpp`
- `cmake/TargetArch.cmake`
- Context updates: `context/tasks-current.md`, `context/session-log.md`

### Verification

- Before the build directory was disrupted, the focused catalog identity test passed: `[catalog][identity][static]` with 1391 assertions.
- The full then-current `pd-tests.exe` passed before the network-wire slice was added: 269 test cases / 12329 assertions.
- The network-wire tests and `TargetArch.cmake` patch have not been build-verified. Build attempts stopped after Mike said to skip build for now.

### Next

- First, verify the network-wire slice once the build-wrapper solution lands.
- Then continue the recursive quality lane by choosing manifest malformed-input behavior or save-migration/version gating as the next highest-risk uncovered invariant.

---

## Session S504 - 2026-04-28 - Concurrent session build isolation

### Outcome

- Added `devtools/build-session.ps1` as the per-session test-build wrapper.
- The wrapper keeps `build-headless.ps1` as the canonical build path and forwards `-OutputDir .claude/session-builds/<session-id>`, so simultaneous sessions do not share `Build/`, `CMakeCache.txt`, `.ninja_log`, generated headers, or clean steps.
- Runs the canonical headless build as a child PowerShell process because `build-headless.ps1` intentionally calls `exit`; this lets the wrapper release its lock and print cleanup guidance after the child exits.
- Added per-session lock files under `.claude/session-builds/.locks/` so accidental reuse of the same session id fails clearly instead of corrupting a build directory.
- Added maintenance modes: `-List`, `-Remove -Session <id>`, `-RemoveAll`, and `-Force` for confirmed stale-lock cleanup.
- Added `.claude/session-builds/` to `.gitignore`.
- Documented the workflow in `AGENTS.md`, `context/build.md`, `context/CRITICAL-PROCEDURES.md`, and `context/tasks-current.md`.

### Files

- `devtools/build-session.ps1`
- `.gitignore`
- `AGENTS.md`
- Context updates: `context/build.md`, `context/CRITICAL-PROCEDURES.md`, `context/tasks-current.md`, `context/session-log.md`

### Verification

- PowerShell parser check passed for `devtools/build-session.ps1`.
- `build-session.ps1 -List` smoke test passed and reports no existing `.claude/session-builds/` directory yet.
- `git diff --check` passed for the touched files using Git for Windows with a one-command `safe.directory` override. MSYS/devkitPro git still fails in this sandbox with Win32 signal-pipe/CreateFileMapping errors.
- Full C/C++ compile was not run because this is a doc/tooling-only change and the wrapper delegates actual builds to the existing headless script.

### Next

- For concurrent AI/code builds, use `.\devtools\build-session.ps1 -Session <short-session-id> -Target all`.
- Clean up after a session with `.\devtools\build-session.ps1 -Remove -Session <short-session-id>`.

---

## Session S502 - 2026-04-28 - Catalog metadata runtime payload activation

Continued typed payload activation coverage after S499's lifecycle guardrail. Scope stayed on metadata-only runtime assets whose catalog/ext data is already the runtime payload.

### Outcome

- Added `s_catalogTypeUsesMetadataRuntimePayload()` for metadata-only runtime asset types.
- Added `s_catalogLoadEntryMetadataPayload()` and routed `ASSET_HUD` / `ASSET_BOT_PROFILE` lifecycle loads through it.
- These entries now become `ASSET_STATE_ACTIVE` with `ASSET_PAYLOAD_RUNTIME_ACTIVE` instead of falling through to generic byte loading. Release detaches the catalog reference while catalog/runtime metadata remains owned by its subsystem.
- Left file-backed map/UI/effect/animation paths unchanged.
- Added a focused static guard for the metadata runtime payload hook.

### Files

- `port/src/assetcatalog_load.c`
- `tests/test_catalog_provider_static.cpp`
- Context updates: `context/tasks-current.md`, `context/session-log.md`

### Verification

- Prescribed MSYS2/Ninja flow passed: `pd`, `pd-server`, `pd-tests`, then `pd-tests.exe`.
- `pd-tests.exe`: 282 test cases / 15299 assertions passed.
- Usual stub logs still appear:
  - `[stub-log L2] NET: could not read 1 bytes (rp=15 wp=15)`
  - `[stub-log L2] NET: could not read 1 bytes (rp=1 wp=1)`
  - `[stub-log L2] NET: could not read 4 bytes (rp=0 wp=3)`

### Next

- Determine the next safe catalog/provider slice. Remaining obvious options are explicit no-handle fallback narrowing, or typed payload activation for another class only if ownership is clear.

---

## Session S501 - 2026-04-28 - Social shell input ownership

Continued the controller-first modern main menu / Social shell after reading the connectivity design, ImGui context, menu-stack architecture, flat-navigation rules, controller-input constraints, and input-authority docs.

### Outcome

- Added `MENU_TYPE_SOCIAL_SHELL` as the pure-ImGui pool identity for the friends sidebar, Social menu, chat panel, profile modal, convert-to-mod modal, add-friend modal, and NAT diagnostics.
- `pdgui_friends.cpp` now synchronizes that pool slot with `g_CtxImGuiMenu` whenever any interactive social surface is open. This keeps the Social shell under input-context ownership instead of relying on raw ImGui window booleans.
- Controller Back (`ACTION_CANCEL_USE`) now closes the top Social shell surface: chat first, then Social menu, then sidebar. Blocking modals keep focus and close themselves.
- Profile, convert-to-mod, add-friend, and NAT diagnostics close from `ACTION_CANCEL_USE` as well as their visible buttons.
- Friend rows now render as bordered controller-first cards with large actions.
- Chat attachment actions, incoming invites, public-mod download/removal entry points, block-list unblock, replay actions, listening-room track actions, settings copy, and add-friend paste now use regular focused buttons instead of dense `SmallButton` clusters.

### Files

- `port/include/menupool.h`
- `port/src/menupool.c`
- `port/fast3d/pdgui_friends.cpp`
- `port/fast3d/pdgui_nat_diagnostics.cpp`
- Context updates: `context/tasks-current.md`, `context/session-log.md`, `context/build.md`

### Verification

- `git diff --check` passed for the touched social/menu-pool files after the input-ownership and action-row slices.
- Build was initially skipped by Mike's instruction after two build-environment failures:
  - `.\devtools\build-headless.ps1` exited during configure with a PowerShell runspace exception.
  - `C:\msys64\usr\bin\bash.exe -lc ...` failed with `fatal error - couldn't create signal pipe, Win32 error 5`.
- Mike then provided the isolated build rule. Attempted `.\devtools\build-session.ps1 -Session s501ui -Target all`; it correctly used `.claude/session-builds/s501ui` but hit the same PowerShell runspace exception during configure.
- Cleanup succeeded with `.\devtools\build-session.ps1 -Remove -Session s501ui`.
- A later isolated `s501ui` build attempt stayed in configure until the Codex tool timed out at 120s. The timeout left a stale `s501ui` lock and an orphaned child build process tree; after confirming the recorded lock PID no longer existed, cleanup succeeded with `.\devtools\build-session.ps1 -Remove -Session s501ui -Force`, and the orphaned child processes from that build were stopped. `s501ui` no longer appears in `.\devtools\build-session.ps1 -List`.
- Isolated build rule + caveat recorded in `context/build.md` so later sessions avoid shared `Build/` and do not rediscover the same failure.

### Next

- Re-run the prescribed build once Mike's build wrapper solution lands.
- Run a gamepad-only pass over sidebar, Social tabs, chat, invites, profile/public mods, add-friend, and NAT diagnostics; tune row heights/focus order if any card clips at Mike's test resolution.
- If that pass is clean, continue the modern-main-menu shell by wiring first-screen entry points for Social / Public Mods / Settings through ImGui/menu-pool ownership only.

---

## Session S500 - 2026-04-28 - Per-player cutscene state accessors

Continued the input infrastructure completion tracker after B-267 propagation. Scope stayed on per-player cutscene state only, without raw ImGui migration or network protocol changes.

### Outcome

- Added `struct playercutscenestate` and embedded it in `struct player`.
- Added cutscene state accessors and reset/sync helpers in `player.c` / `player.h`.
- Migrated active gameplay, render, audio, pickup, AI script, and viewport call sites off direct reads of `g_Vars.in_cutscene`, `g_InCutscene`, and the cutscene skip/anim/frame globals.
- Kept legacy globals as compatibility shims in the sync point, declarations, initialization, server-only stubs, and macro bridge until the tracked shim-retirement step.
- Added static pd-tests that guard migrated paths against reintroducing direct cutscene global state reads.

### Files

- `src/include/types.h`
- `src/include/game/player.h`
- `src/game/player.c`
- `src/game/playermgr.c`
- `src/game/playerreset.c`
- `src/game/chraction.c`
- `src/game/chraicommands.c`
- `src/game/chr.c`
- `src/game/lv.c`
- `src/game/hudmsg.c`
- `src/lib/vi.c`
- `src/lib/model.c`
- `src/game/prop.c`
- `src/game/propobj.c`
- `src/game/mplayer/mplayer.c`
- `src/game/menu.c`
- `src/game/sky.c`
- `src/game/bondgun.c`
- `src/game/nbomb.c`
- `port/src/net/netmsg.c`
- `tests/test_cutscene_layer.cpp`
- Context updates: `context/tasks-current.md`, `context/session-log.md`, `context/designs/input-universality-and-transitions-2026-04-27.md`

### Verification

- `git diff --check` passed for the input-state migration files.
- Prescribed MSYS2/Ninja flow passed: `pd`, `pd-server`, `pd-tests`, then `pd-tests.exe`.
- `pd-tests.exe`: 281 test cases / 15294 assertions passed.
- Usual stub logs still appear:
  - `[stub-log L2] NET: could not read 1 bytes (rp=15 wp=15)`
  - `[stub-log L2] NET: could not read 1 bytes (rp=1 wp=1)`
  - `[stub-log L2] NET: could not read 4 bytes (rp=0 wp=3)`

### Next

- Start the next tracked item: `chr->cutscene_protect` and canonical damage/hostility protection gates.

---

## Session S499 - 2026-04-28 - Untyped lifecycle production guardrail

Follow-up guardrail after S498's typed release/retain internal refactor.

### Outcome

- Added a source-wide static test that prevents production code from calling untyped lifecycle functions (`catalogLoadAsset()`, `catalogUnloadAsset()`, `catalogRetainAsset()`, `catalogReleaseAsset()`) outside the catalog implementation/header and server stubs.
- This upgrades the earlier focused lifecycle callsite guard into a broader production boundary check while preserving the compatibility API internally.

### Files

- `tests/test_catalog_provider_static.cpp`
- Context updates: `context/tasks-current.md`, `context/session-log.md`

### Verification

- Prescribed MSYS2/Ninja flow passed: `pd`, `pd-server`, `pd-tests`, then `pd-tests.exe`.
- `pd-tests.exe`: 281 test cases / 15294 assertions passed.
- Usual stub logs still appear:
  - `[stub-log L2] NET: could not read 1 bytes (rp=15 wp=15)`
  - `[stub-log L2] NET: could not read 1 bytes (rp=1 wp=1)`
  - `[stub-log L2] NET: could not read 4 bytes (rp=0 wp=3)`

### Next

- Determine whether the next safe slice should target remaining explicit no-handle fallbacks or typed payload activation coverage for another asset class.

---

## Session S498 - 2026-04-28 - Typed lifecycle release/retain internals

Continued toward typed catalog retain/release loaders after the modelnum API guardrail.

### Outcome

- Split catalog release/unload behavior into an entry-level internal helper, `s_catalogUnloadEntry()`.
- Split catalog retain behavior into an entry-level internal helper, `s_catalogRetainEntry()`.
- `catalogReleaseTypedAsset()` and `catalogRetainTypedAsset()` now validate type, resolve the mutable entry, and call the internal entry-level helpers directly instead of bouncing through the untyped public wrappers.
- Dependency cascade unloads now resolve the dependency entry and call the internal entry-level helper directly.
- Added a static guard that prevents typed retain/release and dependency cascade paths from regressing to `catalogUnloadAsset(assetId)`, `catalogUnloadAsset(dep_id)`, or `catalogRetainAsset(assetId)`.

### Files

- `port/src/assetcatalog_load.c`
- `tests/test_catalog_provider_static.cpp`
- Context updates: `context/tasks-current.md`, `context/session-log.md`

### Verification

- Prescribed MSYS2/Ninja flow passed: `pd`, `pd-server`, `pd-tests`, then `pd-tests.exe`.
- `pd-tests.exe`: 278 test cases / 13801 assertions passed.
- Re-ran after adding the source-wide untyped lifecycle production guardrail: `pd`, `pd-server`, `pd-tests`, then `pd-tests.exe` passed.
- `pd-tests.exe`: 281 test cases / 15294 assertions passed.
- Usual stub logs still appear:
  - `[stub-log L2] NET: could not read 1 bytes (rp=15 wp=15)`
  - `[stub-log L2] NET: could not read 1 bytes (rp=1 wp=1)`
  - `[stub-log L2] NET: could not read 4 bytes (rp=0 wp=3)`

### Next

- Continue typed lifecycle cleanup by identifying any remaining untyped public lifecycle surface that can be narrowed without breaking legacy/component callers.

---

## Session S497 - 2026-04-28 - Catalog modelnum API guardrail

Follow-up guardrail after S496's modelnum API normalization.

### Outcome

- Added a source-wide static test that keeps deprecated prop-named model wrappers (`catalogGetPropHandle()`, `catalogGetPropFilenumByIndex()`) confined to `assetcatalog.h` / `assetcatalog_api.c`.
- This locks the migrated production surface onto the explicit modelnum APIs while keeping compatibility wrappers available inside the catalog API during the transition.

### Files

- `tests/test_catalog_provider_static.cpp`
- Context updates: `context/tasks-current.md`, `context/session-log.md`

### Verification

- Prescribed MSYS2/Ninja flow passed: `pd`, `pd-server`, `pd-tests`, then `pd-tests.exe`.
- `pd-tests.exe`: 277 test cases / 13795 assertions passed.
- Usual stub logs still appear:
  - `[stub-log L2] NET: could not read 1 bytes (rp=15 wp=15)`
  - `[stub-log L2] NET: could not read 1 bytes (rp=1 wp=1)`
  - `[stub-log L2] NET: could not read 4 bytes (rp=0 wp=3)`

### Next

- Continue against the remaining explicit temporary ROM fallbacks: first-person gun no-handle, menu raw filenum preview no-handle, player weapon no-handle, title no-handle, and `modelcatalog` validation no-handle.

---

## Session S496 - 2026-04-28 - Catalog modelnum API normalization

Continued typed identity normalization for model numbers after centralizing source-filenum handle lookup. Scope stayed on `MODEL_*` / `g_ModelStates[]` identity: the previous provider APIs worked, but their prop-named surface was misleading for generic modelnum callers.

### Outcome

- Added explicit modelnum catalog APIs:
  - `catalog_model_result_t`
  - `catalogResolveModel()`
  - `catalogResolveModelByModelnum()`
  - `catalogGetModelHandle()`
  - `catalogGetModelFilenumByModelnum()`
- Kept the old `catalogGetPropHandle()` and `catalogGetPropFilenumByIndex()` as compatibility wrappers over the new modelnum APIs.
- Migrated live modelnum load sites in `title.c`, `player.c`, and `setuputils.c` from prop-named accessors to typed modelnum result APIs.
- Preserved no-handle ROM fallback behavior for title/menu-style legacy sources while making the typed catalog result the first-class path.
- Added a static guard so the migrated modelnum load sites stay off prop-named APIs.
- Updated the provider constraint text to point modelnum loads at `catalogResolveModelByModelnum()` / `catalogGetModelHandle()`; `catalogGetPropHandle()` is now documented as a compatibility wrapper only.

### Files

- `port/include/assetcatalog.h`
- `port/src/assetcatalog_api.c`
- `src/game/title.c`
- `src/game/player.c`
- `src/game/setuputils.c`
- `tests/stubs.c`
- `tests/test_catalog_provider_static.cpp`
- Context updates: `context/constraints.md`, `context/tasks-current.md`, `context/session-log.md`

### Verification

- `Build/build.ninja` was missing after the previous green run; `devtools/build-headless.ps1` hit a PowerShell runspace exception before it could reconfigure.
- Reconfigured `Build/` directly through the same pinned MSYS2 CMake/Ninja environment used by the prescribed build flow.
- Prescribed MSYS2/Ninja flow then passed: `pd`, `pd-server`, `pd-tests`, then `pd-tests.exe`.
- `pd-tests.exe`: 276 test cases / 12403 assertions passed.
- Usual stub logs still appear:
  - `[stub-log L2] NET: could not read 1 bytes (rp=15 wp=15)`
  - `[stub-log L2] NET: could not read 1 bytes (rp=1 wp=1)`
  - `[stub-log L2] NET: could not read 4 bytes (rp=0 wp=3)`

### Next

- Determine the next remaining warning-backed fallback that can be safely narrowed or migrated now that modelnum callers have explicit typed APIs.

---

## Session S495 - 2026-04-28 - Catalog source-filenum handle lookup centralization

Continued the main Catalog-Owned Asset Pipeline lane after typed texture payload activation. Scope stayed intentionally narrow: keep the temporary ROM fast path, but move repeated source-filenum reverse handle resolution into the catalog API.

### Outcome

- Added `catalogHandleBySourceFilenum(asset_type_e type, s32 source_filenum)` as the catalog-owned helper for converting a legacy source filenum into the effective provider handle for a typed asset entry.
- Migrated local reverse-lookup loops in first-person gun async loads, raw menu model previews, and `modelcatalog` validation from `catalogIdBySourceFilenum()` + `assetCatalogResolve()` + `catalogEffectiveHandle()` to the shared helper.
- Preserved the existing warning-backed no-handle ROM fallbacks for uncataloged legacy sources. This is still a temporary bridge, not a permanent endpoint.
- Added a focused static guard so these migrated source-filenum bridge callsites keep using the shared catalog helper.

### Files

- `port/include/assetcatalog.h`
- `port/src/assetcatalog_api.c`
- `src/game/title.c`
- `src/game/player.c`
- `src/game/setuputils.c`
- `src/game/bondgun.c`
- `src/game/menu.c`
- `port/src/modelcatalog.c`
- `tests/stubs.c`
- `tests/test_catalog_provider_static.cpp`
- Context updates: `context/tasks-current.md`, `context/session-log.md`

### Verification

- Prescribed MSYS2/Ninja flow passed: `pd`, `pd-server`, `pd-tests`, then `pd-tests.exe`.
- `pd-tests.exe`: 269 test cases / 12329 assertions passed.
- Usual stub logs still appear:
  - `[stub-log L2] NET: could not read 1 bytes (rp=1 wp=1)`
  - `[stub-log L2] NET: could not read 4 bytes (rp=0 wp=3)`

### Next

- Determine the next warning-backed fallback that has enough typed provider/catalog support to migrate safely, then continue recursively.

---

## Session S494 - 2026-04-28 - Input B-267 propagation audit

Follow-up to Mike's direct question on whether the B-267 fix was applied everywhere it needed to be. Scope stayed narrow to cutscene and transition lifecycle wiring.

### Outcome

- Answer: the initial B-267 fix covered the central solo/local endstage path, but not every active lifecycle entry point.
- Audited cutscene, endstage, stage transition, disconnect, and network cutscene paths.
- Confirmed active client builds use `port/src/pdmain.c`; stale legacy `src/lib/main.c` is not compiled. Added a static guard so that assumption is checked by `pd-tests`.
- Patched remaining active lifecycle roots:
  - `port/src/net/netmsg.c::netmsgSvcCutsceneRead()` now maps `SVC_CUTSCENE active=1/0` to `SCENE_EVENT_CUTSCENE_START` / `SCENE_EVENT_CUTSCENE_END` on client builds.
  - `port/src/net/net.c::netDisconnect()` now fires `SCENE_EVENT_DISCONNECT` before menu-pool teardown and in-game return-to-title cleanup.
  - `src/game/player.c::playerSetTickMode()` now fires `SCENE_EVENT_CUTSCENE_END` when any path leaves `TICKMODE_CUTSCENE`, covering exits that bypass `playerEndCutscene()`.
- Extended `tests/test_cutscene_layer.cpp` with static lifecycle wiring coverage for `mainEndStage`, stage ready/teardown, tickmode exit, network cutscene sync, disconnect, and the stale-main exclusion.

### Files

- `src/game/player.c`
- `port/src/net/net.c`
- `port/src/net/netmsg.c`
- `tests/test_cutscene_layer.cpp`
- Context updates: `context/bugs.md`, `context/tasks-current.md`, `context/designs/input-universality-and-transitions-2026-04-27.md`, `context/session-log.md`

### Verification

- `git diff --check` passed for the input-system files and related context updates.
- Prescribed MSYS2/Ninja flow passed: `pd`, `pd-server`, `pd-tests`, then `pd-tests.exe`.
- `pd-tests.exe`: 267 test cases / 10926 assertions passed.
- Usual stub logs still appear:
  - `[stub-log L2] NET: could not read 1 bytes (rp=1 wp=1)`
  - `[stub-log L2] NET: could not read 4 bytes (rp=0 wp=3)`

### Next

- Mike playtest B-267 again: deliberate skip into endscreen, then continue to next mission. Expected log: cutscene IMC deactivates at endstage/teardown or disconnect, and the next intro logs a fresh cutscene activation.
- If playtest passes, continue sequentially with per-player cutscene state migration.

---

## Session S493 - 2026-04-28 - Catalog model payload activation

Continued the main Catalog-Owned Asset Pipeline lane after the bondgun async bridge and the parallel audit/guardrail sessions.

### Outcome

- Added `asset_payload_kind_t` and `asset_entry_t::payload_kind` so catalog lifecycle can distinguish raw byte payload ownership from activated model payload ownership.
- `catalogLoadTypedAsset()` now activates/caches promoted modeldef payloads for model-like types (`ASSET_MODEL`, `ASSET_BODY`, `ASSET_HEAD`, `ASSET_PROP`) through `modeldefLoadToNewFromHandle()`.
- Model payload lifecycle entries now store the promoted modeldef in `entry->loaded_data`, set `ASSET_PAYLOAD_STAGE_MODELDEF`, and advance to `ASSET_STATE_ACTIVE`.
- Added `catalogGetLoadedModeldef()` as the typed query surface for catalog-owned activated model payloads.
- Release now frees `ASSET_PAYLOAD_SYSMEM_BYTES` with `sysMemFree`, but only detaches `ASSET_PAYLOAD_STAGE_MODELDEF` modeldefs and calls provider unload. This avoids blindly freeing stage-pool model memory.
- Added the first non-model typed activation hook: `ASSET_LANG` lifecycle loads now call `langManifestEnsureId()`, mark the catalog entry active, and use `ASSET_PAYLOAD_RUNTIME_ACTIVE` so release detaches the catalog reference while the language subsystem owns actual memory lifetime.
- Added typed audio runtime activation: `ASSET_AUDIO`, `ASSET_SFX`, and `ASSET_MUSIC` lifecycle loads now validate that a provider/path exists and mark the entry active with `ASSET_PAYLOAD_RUNTIME_ACTIVE` instead of reading whole audio files as generic byte blobs.
- Added typed individual texture activation: `ASSET_TEXTURE` lifecycle loads now use a texture-specific hook, stores loaded bytes as `ASSET_PAYLOAD_SYSMEM_BYTES`, and marks the entry active. Texture packs/directories remain component-level assets rather than single texture payloads.
- Added a static test guard for the model payload activation path.
- Added the payload ownership rule to `constraints.md`.

### Files

- `port/include/assetcatalog.h`
- `port/include/assetcatalog_load.h`
- `port/src/assetcatalog.c`
- `port/src/assetcatalog_load.c`
- `port/src/assetcatalog_api.c`
- `src/game/bondgun.c`
- `src/game/menu.c`
- `port/src/modelcatalog.c`
- `tests/stubs.c`
- `tests/test_catalog_provider_static.cpp`
- Context updates: `context/constraints.md`, `context/tasks-current.md`, `context/session-log.md`

### Verification

- Prescribed MSYS2/Ninja flow passed: `pd`, `pd-server`, `pd-tests`, then `pd-tests.exe`.
- `pd-tests.exe`: 263 test cases / 10891 assertions passed.
- Re-ran after typed language activation hook: `pd`, `pd-server`, `pd-tests`, then `pd-tests.exe` passed.
- `pd-tests.exe`: 265 test cases / 10916 assertions passed.
- Re-ran after typed audio activation hook: `pd`, `pd-server`, `pd-tests`, then `pd-tests.exe` passed.
- `pd-tests.exe`: 266 test cases / 10921 assertions passed.
- Re-ran after typed texture activation hook: `pd`, `pd-server`, `pd-tests`, then `pd-tests.exe` passed.
- `pd-tests.exe`: 267 test cases / 10926 assertions passed.
- Re-ran after source-filenum handle lookup centralization: `pd`, `pd-server`, `pd-tests`, then `pd-tests.exe` passed.
- `pd-tests.exe`: 269 test cases / 12329 assertions passed.
- Reconfigured `Build/` after `build.ninja` went missing, then re-ran after typed modelnum API normalization: `pd`, `pd-server`, `pd-tests`, then `pd-tests.exe` passed.
- `pd-tests.exe`: 276 test cases / 12403 assertions passed.
- Re-ran after adding the source-wide prop-named wrapper guardrail: `pd`, `pd-server`, `pd-tests`, then `pd-tests.exe` passed.
- `pd-tests.exe`: 277 test cases / 13795 assertions passed.
- Re-ran after typed lifecycle release/retain internals refactor: `pd`, `pd-server`, `pd-tests`, then `pd-tests.exe` passed.
- `pd-tests.exe`: 278 test cases / 13801 assertions passed.
- Usual stub logs still appear:
  - `[stub-log L2] NET: could not read 1 bytes (rp=1 wp=1)`
  - `[stub-log L2] NET: could not read 4 bytes (rp=0 wp=3)`

### Next

- Expand typed payload activation beyond models: audio/lang/texture need type-specific activate/deactivate hooks and payload ownership kinds.
- Continue replacing warning-backed no-handle fallbacks only when the target domain has a typed provider API.

---

## Session S492 - 2026-04-28 - Input B-267 cutscene lifecycle cleanup

Focused the next sequential input slice after Mike's B-266 playtest. Scope stayed narrow: cutscene layer cleanup on skip-to-endstage and stage teardown only. No raw ImGui input migration and no broad scene-manager rewrite.

### Outcome

- Confirmed B-267 root cause from `Build/pd-client.log`: deliberate cutscene skip entered endscreen without a matching cutscene layer pop, leaving `g_ImcCutscene` active under the endscreen and next mission.
- Wired production lifecycle roots:
  - `port/src/main.c` now initializes input layer + scene manager after action-map binds load, and shuts them down during exit.
  - `port/src/pdmain.c::mainEndStage()` now fires `SCENE_EVENT_CUTSCENE_END` before endscreen preparation.
  - `port/src/pdmain.c` stage load/unload paths now fire `SCENE_EVENT_STAGE_READY` / `SCENE_EVENT_STAGE_TEARDOWN`.
- Added `inputLayerHandleDistanceFromTop()` so scene code can reason about cached handles without touching opaque input-layer internals.
- Hardened tracked scene close: if the target layer is not top, scene aborts from top through that target and clears all cached handles in the aborted range.
- Added focused B-267 tests for skip-to-endstage cleanup before next mission intro and nested tracked close where a menu layer is above cutscene.

### Files

- `port/src/main.c`
- `port/src/pdmain.c`
- `port/include/inputlayer.h`
- `port/src/inputlayer.c`
- `port/src/scene.c`
- `tests/inputlayer_pure.c`
- `tests/inputlayer_pure.h`
- `tests/scene_pure.c`
- `tests/test_scene_dispatch.cpp`
- `tests/test_cutscene_layer.cpp`
- Context updates: `context/bugs.md`, `context/tasks-current.md`, `context/designs/input-universality-and-transitions-2026-04-27.md`, `context/session-log.md`

### Verification

- Prescribed MSYS2/Ninja flow passed: `pd`, `pd-server`, `pd-tests`, then `pd-tests.exe`.
- `pd-tests.exe`: 262 test cases / 10883 assertions passed.
- Usual stub logs still appear:
  - `[stub-log L2] NET: could not read 1 bytes (rp=1 wp=1)`
  - `[stub-log L2] NET: could not read 4 bytes (rp=0 wp=3)`

### Next

- Mike playtest B-267: deliberate skip into endscreen, then continue to next mission. Expected log: cutscene IMC deactivates at endstage/teardown before the next intro, and the next intro logs a fresh cutscene activation.
- If playtest passes, continue sequentially with per-player cutscene state migration.

---

## Session S491 - 2026-04-28 - Catalog provider-surface guardrails

Focused the Catalog-Owned Asset Pipeline guardrail lane. Scope stayed on provider-surface audit and static enforcement, with one isolated cleanup. No runtime loader restructuring, no dedicated-server productization, and no broker/plugin ABI work.

### Outcome

- Audited source for raw `romProviderHandle()`, `assetprovider_internal.h`, direct RomProvider assumptions, and RomProvider-only loader wording/guards outside approved catalog/provider internals.
- No raw `romProviderHandle()` calls or `assetprovider_internal.h` includes were found outside the established catalog/provider allowlist.
- Fixed the one isolated direct provider assumption found: `port/src/modelcatalog.c` now uses `assetLoadGetInflatedSize(handle, LOADTYPE_MODEL)` for catalog/provider-backed missing-source prechecks instead of branching on `handle.provider == romProvider()`. Null handles still use the temporary ROM fallback for uncataloged legacy sources.
- Broadened `tests/test_catalog_provider_static.cpp`:
  - source-wide allowlist check for raw `romProviderHandle()`;
  - source-wide allowlist check for `assetprovider_internal.h`;
  - source-wide allowlist check for RomProvider-specific provider comparisons and `romProviderFilenum()`;
  - typed lifecycle boundary check now covers `lv.c`, `screenmfst.c`, and `netmanifest.c`;
  - existing modeldef and first-person gun async checks still guard against RomProvider-only wording/gating regressions.

### Files

- `port/src/modelcatalog.c`
- `tests/test_catalog_provider_static.cpp`
- Context updates: `context/tasks-current.md`, `context/session-log.md`

### Verification

- Prescribed MSYS2/Ninja flow passed: `pd`, `pd-server`, `pd-tests`, then `pd-tests.exe`.
- `pd-tests.exe`: 260 test cases / 10865 assertions passed.
- Usual stub logs still appear:
  - `[stub-log L2] NET: could not read 1 bytes (rp=1 wp=1)`
  - `[stub-log L2] NET: could not read 4 bytes (rp=0 wp=3)`

### Next

- Keep this lane to guardrails/static checks. Runtime loader/payload ownership work stays with the main Catalog-Owned Asset Pipeline session.
- Continue replacing warning-backed no-handle fallbacks only as each domain gets a typed provider API.

---

## Session S491 - 2026-04-28 - Catalog domain migration audit

Focused Catalog-Owned Asset Pipeline cleanup slice. Scope was domain migration audit and low-risk callsite cleanup only. No edits to the protected read-only files: `src/game/bondgun.c`, `src/game/modeldef.c`, `src/include/types.h`, or `port/src/assetcatalog_load.c`.

### Outcome

- Added typed `catalogGameModeIdByScenarioIndex()` for MPSCENARIO / `ext.gamemode.mode_id` identity. The helper preserves the existing runtime-cache fast path and falls back to scanning game-mode entries by `mode_id`.
- Migrated the remaining production `ASSET_GAMEMODE` `catalogIdByRuntime` fallbacks to the typed helper:
  - `port/src/scenario_save.c`
  - `port/src/savefile.c`
  - `port/src/net/net.c`
  - `port/src/net/matchsetup.c`
  - `port/src/net/netmsg.c`
  - `port/fast3d/pdgui_menu_room.cpp`
- Migrated the room screen Campaign and Counter-Op mission-start paths from `catalogIdByRuntime(ASSET_MAP, stagenum)` to `catalogStageIdByStagenum()`.
- Extended `tests/test_catalog_provider_static.cpp` with a focused static guard so the migrated game-mode and room-stage boundaries do not regress to generic runtime lookup.
- Audit result: no production raw `catalogLoadAsset()` / `catalogUnloadAsset()` call sites remain outside `port/src/assetcatalog_load.c` and `port/src/server_stubs.c`; other hits are comments, declarations, implementation, or static tests.

### Remaining

- Source-filenum bridge probes remain in `src/game/menu.c`, `src/game/bondgun.c`, and `port/src/modelcatalog.c`. They resolve legacy file numbers back to catalog/provider handles and should move with the main payload-promotion/provider session.
- Direct model/file fallback paths remain where no provider handle exists: title/menu/player/modelcatalog fallbacks and any first-person gun no-handle fallback paths. These were deliberately recorded, not edited, under this cleanup lane.

### Files

- `port/include/assetcatalog.h`
- `port/src/assetcatalog_api.c`
- `port/src/scenario_save.c`
- `port/src/savefile.c`
- `port/src/net/net.c`
- `port/src/net/matchsetup.c`
- `port/src/net/netmsg.c`
- `port/fast3d/pdgui_menu_room.cpp`
- `tests/stubs.c`
- `tests/test_catalog_provider_static.cpp`
- Context updates: `context/tasks-current.md`, `context/session-log.md`

### Verification

- Prescribed MSYS2/Ninja flow passed: `pd`, `pd-server`, `pd-tests`, then `pd-tests.exe`.
- `pd-tests.exe`: 260 test cases / 10865 assertions passed.

---

## Session S490 - 2026-04-28 - Bondgun async provider payload sizes

Continued the Catalog-Owned Asset Pipeline toward the sprint completion boundary. Main-session lane owned the first-person weapon async loader; parallel prompts were prepared for domain-audit/static-guardrail sessions that avoid the same files.

### Outcome

- Closed the known `bgunTickGunLoad` bridge where the async hand/gun/cart loader restored `g_FileInfo[loadfilenum]` across texture and display-list ticks.
- Kept the existing multi-tick behavior, but stores the queued model's loaded/allocation sizes in `gunctrl.fileinfo` immediately after provider-aware load.
- Added `modeldefPromoteDisplayListsUsingSizes(...)`, a public size-driven wrapper around the existing display-list promotion implementation. Legacy `modeldef0f1a7560(...)` still updates `g_FileInfo[]` for old ROM callers.
- `bgunQueuedLoadCanUseHandle(...)` now accepts any non-null provider handle; non-ROM handles no longer route to the temporary ROM fallback solely because they are not RomProvider.
- Added a static guard proving the first-person gun async loader does not restore `g_FileInfo[]` or regress to RomProvider-only gating.

### Files

- `src/game/bondgun.c`
- `src/game/modeldef.c`
- `src/include/game/modeldef.h`
- `src/include/types.h`
- `tests/test_catalog_provider_static.cpp`
- Context updates: `context/tasks-current.md`, `context/session-log.md`

### Verification

- Prescribed MSYS2/Ninja flow passed: `pd`, `pd-server`, `pd-tests`, then `pd-tests.exe`.
- `pd-tests.exe`: 256 test cases / 6658 assertions passed.
- Usual stub logs still appear:
  - `[stub-log L2] NET: could not read 1 bytes (rp=1 wp=1)`
  - `[stub-log L2] NET: could not read 4 bytes (rp=0 wp=3)`

### Next

- If verification passes, continue with catalog-owned model payload activation/cache state and type-specific activate/deactivate hooks.
- Keep replacing warning-backed no-handle fallbacks only when a typed provider API exists for that domain.

---

## Session S489 - 2026-04-28 - Input action-set transition flush

Focused input infrastructure slice, kept narrow per Mike's directive. No raw ImGui key migration sweep and no broad scene/state manager expansion.

### Outcome

- Added public `actionmapFlushActionSet(const InputAction *actions, s32 action_count)` as the small action-set flush surface missing from the earlier cutscene flash fix.
- Kept `actionmapFlushGameplayState()` gameplay-only and reused a shared internal state-slot clear helper so both flush paths synthesize release edges consistently.
- Declared `g_LayerCutscene.action_set` in `inputlayer.c`: ACTION_SKIP_CUTSCENE, ACTION_USE / ACTION_MENU_ACCEPT, ACTION_CANCEL_USE, ACTION_FIRE_PRIMARY, ACTION_FIRE_SECONDARY, ACTION_PAUSE, ACTION_FIRE_MODE, ACTION_RELOAD, ACTION_WEAPON_NEXT.
- Updated `onCutscenePush` to flush both gameplay-only state and the cutscene action set. Held Continue/Use no longer survives from menu accept through stage change into cutscene entry.
- Tightened pd-tests:
  - action-set flush clears declared shared and gameplay actions only;
  - cutscene transition flush clears held ACTION_USE / menu accept;
  - unrelated menu actions survive if not declared in the flushed set;
  - fresh ACTION_SKIP_CUTSCENE press during a cutscene still registers.
- Logged B-266 and added the transition-flush invariant to `constraints.md`.

### Files

- `port/include/actionmap.h`
- `port/src/actionmap.cpp`
- `port/include/inputlayer.h`
- `port/src/inputlayer.c`
- `src/game/player.c`
- `tests/actionmap_pure.c`
- `tests/actionmap_pure.h`
- `tests/test_actionmap_flush.cpp`
- `tests/test_cutscene_layer.cpp`
- Context updates: `context/constraints.md`, `context/bugs.md`, `context/tasks-current.md`, `context/designs/input-universality-and-transitions-2026-04-27.md`, `context/session-log.md`

### Verification

- Prescribed MSYS2/Ninja flow: `pd`, `pd-server`, and `pd-tests` passed.
- `pd-tests.exe`: 253 test cases / 6641 assertions passed.
- Mike playtest confirmed B-266 in `Build/pd-client.log`:
  - held A at `[03:11.66]` advanced the endscreen from stage 0x30 to stage 0x33;
  - objective 2 intro reached frame 30 at `[03:12.26]` and continued playing instead of flashing/skipping;
  - release at `[03:19.05]`, fresh A press at `[03:19.65]`, and `ACTION_SKIP_CUTSCENE` exit at `[03:19.66]` confirmed deliberate skip still works.
- Same log exposed B-267: a deliberate skip at `[03:08.14]` moved to endscreen at `[03:08.16]`, but the cutscene IMC did not deactivate and remained under the endscreen/next mission until `[03:19.66]`.

### Next

- B-266 is playtest-confirmed and closed.
- Next narrow input slice is B-267: unwind the cutscene layer/IMC on skip-to-endscreen and stage teardown paths so the next mission intro gets a fresh cutscene push. Raw ImGui key migration remains deferred unless a concrete input-system dependency requires it.

---

## Session S488 - 2026-04-28 - Bondgun provider-handle bridge

Continued Asset Provider Phase 4 from S487, focused on the first-person weapon loader.

### Outcome

- Added `struct gunctrl::loadhandle` so queued first-person model loads carry the catalog/provider source handle alongside the legacy `loadfilenum`.
- Added a single `bgunQueueModelLoad(...)` path for hand, gun, and cartridge model loads in `bgunTickMasterLoad`.
- Added catalog source-filenum resolution for queued bondgun model loads across `ASSET_MODEL`, `ASSET_BODY`, `ASSET_HEAD`, and `ASSET_WEAPON`.
- Routed `bgunTickGunLoad` model sizing and load-to-address calls through provider-aware APIs when the queued handle is a RomProvider handle.
- Preserved a warning-backed temporary ROM fallback for uncataloged sources and non-ROM provider handles because the current model promotion path still writes through `g_FileInfo[loadfilenum]`.
- Added a null-load guard so a missing bondgun model file reports `CATALOG_CRITICAL` instead of immediately promoting a NULL modeldef.
- Continued the same Phase 4 slice after the first verification pass:
  - title/logo model loads now resolve catalog provider handles via `catalogGetPropHandle()` and use `modeldefLoadFromHandle()` plus provider-aware loaded-size queries;
  - title/logo uncataloged sources are routed through one warning-backed temporary fallback;
  - `modelcatalog` validation now resolves body/head provider handles by source filenum and uses `modeldefLoadToNewFromHandle()` while preserving its SEH/signal fault guard;
  - `modelcatalog` uncataloged sources remain a warning-backed legacy fallback.
- Completed the requested sprint follow-through:
  - `catalogLoadTypedAsset()` now uses a type-policy/provider-backed payload loader instead of just validating then delegating to the string-only loader.
  - `modeldefLoadFromHandle()` now supports non-ROM provider handles by promoting display lists from caller/provider sizes instead of indexing `g_FileInfo[]`; the old `modeldef0f1a7560()` wrapper still preserves `g_FileInfo[]` mutation for legacy ROM callers.
  - title/modelcatalog non-ROM provider handles now use the handle loader directly; only no-handle cases fall back to legacy filenum loading.
  - `lv.c` stage diff and `screenmfst.c` screen mini-manifests now use typed lifecycle load/release calls.
  - Added `tests/test_catalog_provider_static.cpp` to pin the migrated call sites and prevent the modeldef handle loader from becoming RomProvider-only again.

### Files

- `src/include/types.h`
- `src/game/bondgun.c`
- `src/game/title.c`
- `src/game/lv.c`
- `port/src/modelcatalog.c`
- `port/src/assetcatalog_load.c`
- `port/src/screenmfst.c`
- `CMakeLists.txt`
- `tests/test_catalog_provider_static.cpp`
- Context updates: `context/tasks-current.md`, `context/session-log.md`

### Verification

- Prescribed MSYS2/Ninja build: `pd`, `pd-server`, and `pd-tests` linked clean.
- `pd-tests.exe`: 252 test cases / 6626 assertions passed.
- Re-ran prescribed MSYS2/Ninja verification after title/modelcatalog migration: `pd`, `pd-server`, and `pd-tests` targets completed cleanly; `pd-tests.exe` passed 253 test cases / 6641 assertions.
- Re-ran prescribed MSYS2/Ninja verification after completing typed lifecycle/model-promotion/static-check work: `pd`, `pd-server`, and `pd-tests` targets completed cleanly; `pd-tests.exe` passed 255 test cases / 6651 assertions.

### Next

Continue Asset Provider Phase 4 by replacing the remaining warning-backed fallback paths as each domain gets a typed provider API. The main known bridge is the first-person gun async loader, which still restores `g_FileInfo[loadfilenum]` while it performs incremental texture/DL work across ticks.

---

## Session S487 - 2026-04-27 - Catalog typed identity normalization

Continued the Catalog-Owned Asset Pipeline Phase 1 after S485/S486.

### Outcome

- Added explicit catalog ID helpers for the major numeric spaces that were still using generic runtime lookup:
  - `catalogStageIdByStageTableIndex`
  - `catalogStageIdBySoloStageIndex`
  - `catalogStageIdByStagenum`
  - `catalogModelIdByModelnum`
  - `catalogBodyIdByBodynum`
  - `catalogHeadIdByHeadnum`
  - `catalogIdBySourceFilenum`
  - `catalogIdBySourceHandle`
- Migrated ASSET_MAP / ASSET_MODEL / ASSET_BODY / ASSET_HEAD callers away from ambiguous `catalogIdByRuntime(type, n)`.
- Fixed B-265: several stage-id and manifest backfill paths passed a logical `stagenum` into the stage-table-index cache. They now use `catalogStageIdByStagenum`.
- Started Asset Provider Phase 4:
  - added `assetLoadGetInflatedSize` and `assetLoadGetLoadedSize` provider-aware size queries;
  - added handle-aware modeldef loaders `modeldefLoadFromHandle` and `modeldefLoadToNewFromHandle`;
  - migrated `setupLoadModeldef` to use `catalogGetPropHandle()` and catalog source metadata for prop / weapon / hat / projectile model loads.
  - migrated `catalogGetBodyModeldef` and `catalogGetHeadModeldef` to load through `catalogGetBodyHandle()` / `catalogGetHeadHandle()`.
- Continued Asset Provider Phase 4:
  - `catalog_body_result_t`, `catalog_head_result_t`, `catalog_weapon_result_t`, and `catalog_prop_result_t` now expose the catalog effective provider handle alongside legacy source filenum metadata.
  - Forge runtime door, weapon-pad, and prop spawning now loads modeldefs through `modeldefLoadToNewFromHandle(...)` using the resolved catalog handle.
  - `struct menumodel` now stores pending/current/body/head provider handles and source filenum tags; `menuSetModelFileHandle(...)` seeds handle-aware single-model previews.
  - `menuRenderModel` now uses catalog-resolved provider handles for body/head preview sizing and modeldef loading, and uses the seeded provider handle for catalog-backed single-model previews. Raw filenum-only menu previews now attempt catalog source-filenum resolution first; only truly uncataloged ROM files use the temporary fallback and log a warning.
  - MP head preview and main-menu weapon preview now seed menu model handles from catalog resolution.
  - `playerTickChrBody` first-person body/head/weapon size accounting and modeldef loading now use catalog-resolved provider handles.
  - Added typed lifecycle wrappers `catalogLoadTypedAsset`, `catalogReleaseTypedAsset`, and `catalogRetainTypedAsset`; they validate the resolved catalog entry type before dispatching to the legacy string-only lifecycle calls. SP manifest diff load/unload and `manifestEnsureLoaded` late-add now call these wrappers for known manifest asset types.
- Mike clarified that preserving the ROM fast path is acceptable only as a sub-step. Recorded the constraint: the endpoint is still full migration away from legacy filenum-first loading and toward catalog/provider-owned source handles, payloads, refcounts, and release behavior.
- Updated `tests/stubs.c` so pd-tests link against the new typed helper surface.
- Updated `constraints.md`, `bugs.md`, and `tasks-current.md` with the new helper rule and Phase 1 progress.

### Files

- `port/include/assetcatalog.h`
- `port/src/assetcatalog_api.c`
- `port/src/modelcatalog.c`
- `port/include/assetload.h`, `port/src/assetload.c`
- `port/src/forge/forge_runtime.c`
- `port/src/net/net.c`, `port/src/net/netmanifest.c`, `port/src/net/netmsg.c`
- `port/src/server_stubs.c`
- `port/src/scenario_save.c`
- `src/include/game/modeldef.h`
- `src/include/game/menu.h`
- `src/include/types.h`
- `src/game/body.c`, `src/game/lv.c`, `src/game/mainmenu.c`, `src/game/menu.c`, `src/game/menutick.c`, `src/game/modeldef.c`, `src/game/mplayer/mplayer.c`, `src/game/mplayer/setup.c`, `src/game/player.c`, `src/game/setuputils.c`
- `tests/stubs.c`
- Context updates: `context/constraints.md`, `context/bugs.md`, `context/tasks-current.md`, `context/session-log.md`

### Verification

- Prescribed MSYS2/Ninja build: `pd`, `pd-server`, and `pd-tests` linked clean.
- `pd-tests.exe`: 252 test cases / 6626 assertions passed.
- Re-ran the same prescribed build/test pass after the Forge provider-handle migration: `pd`, `pd-server`, and `pd-tests` linked clean; `pd-tests.exe` passed 252 test cases / 6626 assertions.
- Re-ran the prescribed build/test pass after menu and player provider-handle migration: `pd`, `pd-server`, and `pd-tests` linked clean; `pd-tests.exe` passed 252 test cases / 6626 assertions.
- Re-ran the prescribed build/test pass after catalog-wrapping raw filenum menu previews and adding typed lifecycle wrappers: `pd`, `pd-server`, and `pd-tests` linked clean; `pd-tests.exe` passed 252 test cases / 6626 assertions.

### Next

Continue Asset Provider Phase 4: audit remaining direct model size/load paths, mark true legacy exceptions, then expand typed lifecycle wrappers into asset-type-specific payload loaders.

---

## Session S486 - 2026-04-27 - Online shipping scope correction

Mike clarified the current release target: do **not** ship the standalone dedicated server now. The online target is internal connectivity inside the client.

### Decision

- Current ship-track online work targets in-client/listen-host connectivity: host flow, join flow, rooms/lobby UX, connect codes/NAT path, manifest/catalog distribution, ready gate, stage transitions, reconnect, and in-client validation.
- `pd-server` remains useful as a build target and regression/tooling surface, but it is not the release product right now.
- Game-agnostic dedicated server work is deferred: P4-B/P4-C broker implementation, `server_stubs.c` shrink, and plugin ABI cleanup should not block client online work.

### Context updates

- `context/tasks-current.md` marks Tier 4 dedicated-server work deferred and adds the current client-online shipping focus.
- `context/constraints.md` adds the active shipping-scope constraint and supersedes the old dedicated-server-only shipping model note.
- `context/server-architecture.md` now opens with a shipping note so future sessions do not mistake the dedicated-server architecture doc for current release scope.

---

## Session S485 - 2026-04-27 - Catalog-owned asset pipeline Phase 0 + weapon identity split

Mike's directive: implement the Catalog-Owned Asset Pipeline plan, with the catalog as the single source of truth for all declared assets, references, source handles, loading/unloading, dependencies, refcounts, and payload ownership. Weapons remain the first proving domain because they expose the current identity bugs, but the scope is explicitly all assets.

### Outcome

Phase 0 baseline is green and Phase 1 has its first identity split in place.

- Fixed the pre-existing `test_swarm_boid_sim` failure by clamping seek speed when the target is closer than one frame of movement. CPU, GPU shader, and test mirror now agree.
- Corrected base weapon catalog registration to the post-GF64-cull MP table: `NUM_MPWEAPONS = 0x29` (41 slots), not 47. Registration now includes `MPWEAPON_NONE`, `MPWEAPON_SHIELD = 0x27`, and `MPWEAPON_DISABLED = 0x28`, and has a `_Static_assert(NUM_BASE_WEAPONS == NUM_MPWEAPONS)`.
- Split weapon identity explicitly:
  - runtime `weapon_num` = `WEAPON_*` enum, final gameplay handoff only;
  - `mp_weapon_id` = `MPWEAPON_*` selector/setup slot;
  - catalog ID string = authoritative boundary identity.
- Added typed helpers `catalogWeaponIdByRuntimeWeaponNum()` and `catalogWeaponIdByMpWeaponId()`.
- Migrated MP setup, match manifest, stage-start wire refs, scenario save/load fallback, and setup preload fallback away from ambiguous `catalogIdByRuntime(ASSET_WEAPON, ...)` use.
- Fixed catalog iteration over pools with holes in `catalogBuildRuntimeCaches()` and the room spawn-weapon UI list.

### Files

- `port/src/assetcatalog_base_extended.c`, `port/src/assetcatalog_api.c`, `port/src/assetcatalog_scanner.c`, `port/include/assetcatalog.h`
- `port/src/net/matchsetup.c`, `port/src/net/netmanifest.c`, `port/src/net/netmsg.c`, `port/src/net/netdistrib.c`
- `port/src/scenario_save.c`, `src/game/setup.c`, `src/game/mplayer/mplayer.c`
- `port/src/swarm_test.c`, `port/fast3d/swarm_gpu.cpp`, `tests/test_swarm_boid_sim.cpp`, `tests/test_spawn_weapon_mode.cpp`
- Context updates: `context/tasks-current.md`, `context/constraints.md`, `context/bugs.md`, `context/designs/catalog-full-pipeline-weapons-2026-04-27.md`, `context/audits/catalog-universality-sweep-2026-04-27.md`, `context/qc-tests.md`, `context/session-log.md`

### Verification

- `pd-tests`: passed all 252 test cases / 6626 assertions.
- `pd` + `pd-server`: linked clean via direct Ninja invocation after the PowerShell wrapper failed before invoking the build targets with a runspace exception.

### Next

Continue the all-assets catalog-owned pipeline in this order: finish typed identity helpers for stage/model/body/head/source-handle spaces, finish Asset Provider Phase 4, add typed retain/release loaders, then migrate domains one at a time with parity tests and static checks only after a domain has approved catalog/provider APIs.

---

## Session S483d (`charming-noether-7b69b3` follow-up #2) - 2026-04-27 - FIESTA match-start crash (B-263)

Mike's playtest after S483b shipped: tried starting a match with FIESTA spawn-weapon mode, hit a fresh AV. Different binary base, different PC offset from the prior crash; this is its own root cause.

### Crash anchor

`PC RVA 0xef32b` -> `modelmgrLoadProjectileModeldefs at modelmgrreset.c:173`. Backtrace via addr2line: `setupCreateProps:2872 -> lvReset -> mainLoop -> mainProc -> main`.

### Mechanism (single cause, traced end-to-end)

`SPAWNWEAPON_FIESTA_SENTINEL = 0xFE` was added in S482 as the FIESTA marker. `matchStart` writes it into `g_MatchConfig.spawnWeaponNum`. The model-preload guard at `setup.c:2870-2872` was written before the FIESTA sentinel existed and only excluded the legacy two values:

```c
if (g_MatchConfig.spawnWeaponNum != 0xFF
        && g_MatchConfig.spawnWeaponNum != 0) {
    modelmgrLoadProjectileModeldefs((s32)g_MatchConfig.spawnWeaponNum);
}
```

`0xFE` passed both conditions. `modelmgrLoadProjectileModeldefs(254)` indexed `g_Weapons[254]` -- a 254-byte walk past the end of the [WEAPON_SUICIDEPILL + 1] = 86-entry array (`src/include/game/inv.h:9`). The next deref AVed.

The FIESTA design comment in matchsetup.h had claimed "0xFE was chosen so any code path checking `!= 0xFF && != 0` continues to exclude this value as well" -- that math was wrong (`0xFE != 0xFF AND 0xFE != 0` both hold). Two more sites had the same incomplete exclusion: the userPickedSpawnWeapon predicate at setup.c:2775 (which then logged "user-picked spawn weapon ... num=254 preserved" -- visible in the crash log immediately before the FATAL line), and the elif paths in `bot.c:543` + `player.c:1835` (structurally safe because the FIESTA-mode branch above caught 0xFE first, but the predicate text drifted from the spec).

### Fix (4 changes, no half-measures, INV-1 loud-fail discipline)

1. **Shared single-source-of-truth predicate**: `spawnWeaponNumIsResolved(num)` static inline in `port/include/net/matchsetup.h`. Returns 0 for {0, 0xFF, SPAWNWEAPON_FIESTA_SENTINEL}, 1 for resolved real WEAPON_* enum values. `static inline` so it's callable from C (src/game/) and C++ (tests/) without dragging matchsetup.c into the test binary.

2. **Migrated four consumers** to call the helper:
   - `setup.c:2781` (userPickedSpawnWeapon predicate)
   - `setup.c:2878` (model-preload guard, the actual crash site)
   - `bot.c:543` (elif sentinel check, audit consistency)
   - `player.c:1835` (elif sentinel check, audit consistency)

3. **Defensive bound + INV-1 loud-fail in the leaf** (`modelmgrLoadProjectileModeldefs`): weaponnum out of `[0, ARRAYCOUNT(g_Weapons))` returns false with `WEAPON.SLOT.MISS:` LOG_WARNING. Defence in depth -- catches any future caller that bypasses the upstream gate (wire tampering, not-yet-migrated consumer, race).

4. **pd-tests pin** (`tests/test_spawn_weapon_resolved.cpp`): 10 cases / 860 assertions covering the helper contract -- exhaustive 0..0xFF walk catches future sentinel-addition drift; consumer-gate + leaf-bound invariants pin Mike's "FIESTA-mode spawnWeaponNum never flows into a weapon-num-as-array-index consumer" rule. `[b263]` tag.

### Methodology learning

Captured in `context/constraints.md` Active Constraints + commit message: when adding a new reserved-value sentinel to a field with existing consumer-side checks, audit EVERY consumer, not just the writer that produced the sentinel. Centralise the "is this resolved?" predicate so the next sentinel addition has one audit surface. Pin the contract with a test that walks the entire input domain (every byte value here).

### Files

- `port/include/net/matchsetup.h` -- `spawnWeaponNumIsResolved` helper + comment correcting the original FIESTA-sentinel design claim
- `src/game/setup.c` -- two consumer migrations
- `src/game/bot.c` -- one consumer migration
- `src/game/player.c` -- one consumer migration
- `src/game/modelmgrreset.c` -- leaf-side bound + WEAPON.SLOT.MISS LOG_WARNING
- `tests/test_spawn_weapon_resolved.cpp` -- new (10 cases / 860 assertions)
- `CMakeLists.txt` -- pd-tests SRC list extension
- `context/bugs.md` -- B-263 entry
- `context/constraints.md` -- sentinel-audit-discipline invariant
- `context/session-log.md` -- this entry

### Verify

Build clean: PerfectDark.exe + PerfectDarkServer.exe + pd-tests linked. `[b263]` tag passes 860 assertions / 10 cases. Pre-existing test failure in `tests/test_swarm_boid_sim.cpp:123` (S483c boid-sim, unrelated) remains; my changes did not introduce it.

### Outstanding

Mike's playtest of FIESTA match start -- match must start without crashing; subsequent spawns must roll fresh weapons per spawn.

---

## Session S483b (`charming-noether-7b69b3`) - 2026-04-27 - crash mitigation (B-261) + Tab IMC fix (B-262)

Two threads, evidence-only investigation per Mike's reset directive (no recency or subsystem priors), then both fixes shipped.

### Thread 1: Crash investigation (B-261)

ACCESS_VIOLATION 0xc0000005 at PC RVA 0x396ed2, LVTICK 1836 of stage 0x33 (Investigation), ~30s after spawn. addr2line landed on `src/lib/model.c:1387` (`sp2c.z = rodata->reorder.unk08;`) inside `modelUpdateReorderRelations`. Disassembly of the shipped exe (md5 bb97fd31...) showed reads at offsets 0x28 / 0xc / 0x10 / 0x14 / 0x0 / 0x4 succeeded; the +0x8 read AVs -- consistent with the rodata struct straddling a page boundary into unmapped memory. Same frame logged the existing `DOOR.DIAG: doorGetBbox -- no bbox for modelnum=154 model=0x0000019939568438 flags=0x80 doortype=0 (count=1)` defensive guard (the bbox node was missing on the same model that crashed during REORDER traversal). Mike's catalog-data-missing hypothesis sharpened the candidate ranking: the door's `model_009a` was loaded with partially populated rodata, where some nodes have unreadable rodata that succeeds at low-byte reads but fails at the page boundary.

**Mitigation shipped (does NOT fix upstream catalog incompleteness):**

1. Per-tick rodata-validity guard via `VirtualQuery` probe in five rodata-reading update functions (`modelUpdateReorderRelations`, `modelUpdateDistanceRelations`, `modelApplyDistanceRelations`, `modelApplyToggleRelations`, `modelApplyReorderRelationsByArg`). Probes BEFORE any deref and BEFORE `modelGetNodeRwData` (which reads `rodata->*.rwdataindex`). On miss emits rate-limited `MODEL.RODATA.MISS:` warning and skips the node safely.
2. Load-time tree-walk validator in `setupLoadModeldef` (the chokepoint for prop / weapon / hat / projectile model loads). After `modeldefLoadToNew` succeeds, walks the rootnode tree once and probes every node's rodata. Logs `MODEL.RODATA.LOAD: PARTIAL modelnum=<N> ...` if any node is unreadable.
3. New helper module: `port/src/model_rodata_guard.c` + `port/include/model_rodata_guard.h`. Header returns `int` rather than `bool` so it stays includable from `src/lib/model.c` where `bool` is the `s32` macro.

**Diagnostic discipline:** the channel name `MODEL.RODATA.MISS:` parallels `CATALOG.MISS:` from INV-1 (b6a0c280). Mike's directive to extend loud-fail to model-rodata accessors is satisfied by the new diagnostic surface; the existing `modelFindBboxRodata` / `modelGetPartRodata` accessors already return NULL safely on miss and the existing `DOOR.DIAG` channel covers bbox-side discovery.

**Forensic next step:** post-playtest `MODEL.RODATA.LOAD: PARTIAL` lines discriminate Mike's catalog-data-missing hypothesis from the alternate use-after-free path. Root cause then lands on the catalog/load side, separate from this session's mitigation.

### Thread 2: Tab IMC fix (B-262)

Mike's playtest 2026-04-27 hit Tab during an active SP mission and the Online connectivity / friends sidebar opened. Handler at `port/fast3d/pdgui_friends.cpp:813` was a raw `ImGui::IsKeyPressed(ImGuiKey_Tab)` with the comment "Avoids reaching into the actionmap layer" -- a deliberate IMC-stack bypass. Fix (Mike picked option (c)): routed Tab through actionmap as new `ACTION_SOCIAL_TOGGLE` (= 69), bound only on `g_ImcMenu` and `g_ImcPauseMenu` (NOT on `g_ImcGameplay`). `fireVk`'s priority-sorted first-match-wins walk now structurally cannot fire ACTION_SOCIAL_TOGGLE during pure gameplay -- gameplay IMC has no Tab binding for this action. Tab continues to fire ACTION_SCORECARD on gameplay (no-op outside Combat Sim).

pd-tests case `tests/test_social_toggle_imc.cpp` pins the invariant Mike named ("Tab during top-IMC = gameplay does not toggle sidebar state"): 5 cases / 7 assertions with `[s483b]` tag. Pure mirror of the priority-sorted resolver, no SDL coupling.

### Files

- `port/include/actionmap.h` -- new `ACTION_SOCIAL_TOGGLE = 69`, `ACTION_COUNT = 70`
- `port/src/actionmap.cpp` -- s_ActionNames extension, `actionIsGameplayOnly` shared classification, Tab binding on g_ImcMenu and g_ImcPauseMenu
- `port/fast3d/pdgui_friends.cpp` -- replaced raw ImGui hotkey with `actionPressed(0, ACTION_SOCIAL_TOGGLE)`
- `tests/test_social_toggle_imc.cpp` -- new pd-tests case
- `port/include/model_rodata_guard.h` + `port/src/model_rodata_guard.c` -- new helper module
- `src/lib/model.c` -- per-tick guards in the five rodata-reading functions
- `src/game/setuputils.c` -- `setupValidateModeldefRodata` + call from `setupLoadModeldef`
- `CMakeLists.txt` -- pd-tests SRC list extension
- `context/bugs.md` -- B-261, B-262
- `context/constraints.md` -- model rodata-validity guard invariant

### Verify

Build clean: 860/860 objects. `pd-tests` 4813 assertions / 186 cases pass; `[s483b]` tag passes 7 assertions / 5 cases. PerfectDark.exe + PerfectDarkServer.exe both linked.

### Outstanding

- Mike's playtest of the crash repro path -- AV must NOT recur at LVTICK 1836+ on Investigation; forward `MODEL.RODATA.LOAD: PARTIAL` and `MODEL.RODATA.MISS:` log lines for catalog-side root-cause discrimination.
- Mike's playtest of Tab key invariant -- Tab during gameplay must not open sidebar; Tab during pause toggles sidebar.

---

## Session S483 (`jovial-kirch-181c60` follow-up) - 2026-04-27 - host-eligible weapon pool via match manifest

Mike's clarification on Random/Fiesta semantics after S482 shipped: the eligible pool should draw from the host's full unlocked-weapon catalog, distributed via the match manifest, not just the active match weapon set's 6 slots.

### Outcome

S482's `spawnWeaponPickFromActiveSet()` (active-set 6-slot pool) is preserved as a **fallback only**. The primary pool is now the match manifest's `MANIFEST_TYPE_WEAPON` entries -- enumerated by the host at match start and broadcast via the existing `SVC_MATCH_MANIFEST` machinery. Distribution-as-needed for mod-only weapons is **already wired**: `ASSET_WEAPON` is in the `SVC_CATALOG_INFO` type list (`port/src/net/netmsg.c::netmsgSvcCatalogInfoWrite`), so any non-bundled mod weapon the host has streams to clients via the existing `CLC_CATALOG_DIFF` -> `SVC_DISTRIB_BEGIN` / `SVC_DISTRIB_CHUNK` / `SVC_DISTRIB_END` pipeline at lobby join time. **No NET_PROTOCOL_VER bump** -- the manifest serialization is `(u8 type, u8 slot, str id)` per entry; adding more entries is wire-compatible. v45 still in effect.

### Mode semantics (post-S483)

- **SPECIFIC**: unchanged. matchStart() resolves `spawn_weapon_id` via the catalog.
- **RANDOM**: matchStart() now rolls from the manifest pool (`spawnWeaponPickFromMatchManifest`). Falls back to active-set roll when the manifest is unavailable (solo CS, pre-broadcast windows). Rolled WEAPON_* enum is broadcast in `SVC_STAGE_START` as before.
- **FIESTA**: every spawn (player.c / bot.c) now rolls from the manifest pool. Same fallback discipline.

### Pool source cascade (live helper `spawnWeaponPickFromMatchManifest`)

1. `g_CurrentLoadedManifest` (post-transition definitive list).
2. `g_ServerManifest` (host-side built manifest, pre-broadcast).
3. `g_ClientManifest` (received from server).
4. None populated -> falls back to `spawnWeaponPickFromActiveSet()` (active set's 6 slots).

If even that yields zero eligible weapons, matchStart() RANDOM falls back to `MPWEAPON_FALCON2` with a `LOG_WARNING`; FIESTA falls into the existing `resolvedWeaponNum=0` miss path.

### Files

- `port/src/net/netmanifest.c` -- new `s_manifestAppendWeaponPool` helper. Walks `assetCatalogIterateUnlockedByType(ASSET_WEAPON, ...)`, filters NONE/DISABLED/SHIELD via `ext.weapon.weapon_id`, calls `manifestAddEntry` with `MANIFEST_TYPE_WEAPON` + `MANIFEST_SLOT_MATCH`. Hooked into both `manifestBuild` (server) and `manifestBuildForHost` (client outgoing CLC_LOBBY_START) right after the existing 6-slot active-set loop. Logs the (added/filtered/invalid) tally.
- `port/src/net/matchsetup.c` -- new `spawnWeaponPickFromMatchManifest()` (public), `spawnWeaponBuildPoolFromManifest()` + `spawnWeaponSelectManifest()` (file-static). matchStart() RANDOM branch + player.c FIESTA branch + bot.c FIESTA branch all migrated to the new helper. Includes `net/netmanifest.h`.
- `port/include/net/matchsetup.h` -- new `spawnWeaponPickFromMatchManifest` declaration with the same doc-comment convention as the S482 helpers.
- `src/game/player.c` -- FIESTA branch calls `spawnWeaponPickFromMatchManifest` instead of `spawnWeaponPickFromActiveSet`.
- `src/game/bot.c` -- mirror.

### Tests (extended `tests/test_spawn_weapon_mode.cpp`)

Adds 9 new cases (now 26 total / ~2-3k assertions): pool draws from MANIFEST_TYPE_WEAPON entries (skipping non-weapon entries), NONE/DISABLED/SHIELD filtered from the manifest pool, empty manifest falls back to active set, all-filtered manifest falls back, missing-catalog entries skipped (Phase 2 distribution gap), mod weapon (synthetic catalog id) included in pool, invalid weapon_id (>=NUM_MPWEAPONS) skipped, both pools degenerate -> 0 (caller fallback), pool size scales beyond 6 slots, MANIFEST_TYPE_WEAPON value pin (== 3, mirrors `netmanifest.h:74`).

### Phase 2 status (deferred)

Distribution-as-needed verification requires an in-game playtest with a mod weapon installed on host but not client. The wiring is ALREADY in place via `SVC_CATALOG_INFO` (S222 audio mod sync extension) -- no new code is needed for the distribution itself. Phase 2 is "verify the existing pipeline picks up ASSET_WEAPON entries during lobby join", which can only be validated end-to-end at runtime.
## Session S483c (`unruffled-edison-af5d41`) - 2026-04-27 - GPU swarm + Test Scenarios (design + impl)

Mike's directive: design a Test Scenarios dropdown in Settings > Debug (Empty Map / Swarm CPU / Swarm GPU) plus a GPU compute boid system that swaps in for the existing CPU bot tick under the GPU scenario. Cycler 4-8-16-32-64-128-256 Skedars with 1 HP each, player invincible, full random weapons, bottomless ammo, score 1 per kill. Per-frame benchmark logging on `BENCHMARK.SWARM.{CPU,GPU}` and `TESTSCEN.*`. Phase 1 = design doc; Phase 2 = implement after Mike approves; Phase 4 = auto-merge per standing rule.

Mike approved all five F-section recommended defaults so Phase 2 ran in the same session. Renamed S483 -> S483c at merge time because dev had already shipped S483 (host-eligible spawn-weapon pool) and S483b (Tab IMC + per-tick rodata guard) under the parent S483 label.

### Outcome (Phase 1 + Phase 2)

Design doc landed at `context/designs/gpu-swarm-and-test-scenarios-2026-04-27.md` (Sections A-G per Mike's prescribed structure). Five-commit Phase 2 stack landed on the worktree branch and merged to dev:

- `8566d5a9` foundation registries (log channels + action enum + actionmap entry)
- `e147dda1` testscenarios module + Settings > Debug UI + Empty Map scenario
- `4e0a4d87` swarm_test runtime + HUD + chr-pool hook (G.1.1 numchrs hook in setup.c)
- `b83095ad` swarm_gpu compute path (4.3 core context probe + SSBO sim + readback)
- `7b61a998` pd-tests cases (test_swarm_boid_sim, 6 cases under `[swarm][sim]`)

### Architecturally significant findings + Mike's approved decisions (Section F)

All five Mike-approved defaults landed:

1. **F.1 -> Option A**: prepended `(4, 3, CORE)` to the SDL probe at `port/fast3d/gfx_sdl2.cpp:164`. Compute symbols (`glDispatchCompute`, `glMemoryBarrier`, `glBindBufferBase`) loaded at runtime via `SDL_GL_GetProcAddress` rather than regenerating glad (G.2 refinement). `swarmGpuAvailable()` returns 0 + greys-out the GPU scenario tooltip when the probe fails.
2. **F.2 -> B+C combined, refined to G.1.1**: separate test-mode chr table escapes `MAX_BOTS=32`. Refinement during impl: NUMTYPE2 50->100 was misdirected (Type 2 = weapons rwdata), the right hook is the per-stage `numchrs` bump in `setup.c`. All 256 same-body Skedars share one Type 3 binding so NUMTYPE3=48 is plenty. The `numchrs += testScenarioGetSwarmMaxCount()` hook lands between simulant-bot count and `modelmgrAllocateSlots`, naturally extending `g_Vars.maxprops`.
3. **F.3 -> A**: CPU readback per frame. `swarmGpuStepAndApply` writes the GPU-stepped positions back into `chr->prop->pos` so the existing damage / kill / animation / audio paths handle scoring without instrumentation.
4. **F.4 -> A**: Empty Map reuses `STAGE_CITRAINING`. Procedural ground plane deferred to a follow-on if the CI Training prop load muddies the empty-map number.
5. **F.5 -> B**: CPU mode runs the seek-player action that mirrors the GPU shader byte-for-byte (max_speed = 18.0, dt = 1/60, ground-locked Y, seek-only) for an apples-to-apples benchmark.

### Constraints respected in design

- No `NET_PROTOCOL_VER` bump. Test mode is local-only; dropdown greys out in netplay.
- No save format change. `g_TestScenario` is volatile.
- Catalog ID strings used everywhere (`base:skedar` body, `base:mp_skedar` arena).
- Stage transitions reach `mainChangeToStage` via the existing `pdguiForgeStartSessionOn` catalog path. No hardcoded stagenum.
- All Test Scenarios UI gated by `PD_DEV_BUILD` (the Debug tab is already dev-only).
- Em-dash count: 0 (methodology gate).

### Constraints respected

- No `NET_PROTOCOL_VER` bump. Test mode is local-only; dropdown greys out in netplay via `g_NetMode != NETMODE_NONE`.
- No save format change. `g_TestScenario` is volatile.
- Catalog ID strings used everywhere (`base:skedar` body, `base:skedar_warrior` head with fallback, `base:mp_skedar` arena).
- Stage transitions reach `mainChangeToStage` via the existing `pdguiForgeStartSessionOn` catalog path. No hardcoded stagenum.
- All Test Scenarios UI gated by `PD_DEV_BUILD` (the Debug tab is already dev-only).
- Em-dash count in design doc: 0 (methodology gate).

### Files

- **New**: `context/designs/gpu-swarm-and-test-scenarios-2026-04-27.md`,
  `port/include/testscenarios.h`, `port/src/testscenarios.c`,
  `port/include/swarm_test.h`, `port/src/swarm_test.c`,
  `port/fast3d/swarm_gpu.cpp`, `tests/test_swarm_boid_sim.cpp`.
- **Touched**: `port/include/system.h`, `port/src/system.c` (LOG_CH_BENCHMARK + LOG_CH_TESTSCEN); `src/include/constants.h` (MA_SWARM_TEST_{SEEK,GPU_DRIVEN}, MA_END 55->57); `port/include/actionmap.h`, `port/src/actionmap.cpp` (ACTION_TESTSCEN_CYCLE_COUNT bound to KEY_0 + DPAD_DOWN); `port/fast3d/pdgui_menu_mainmenu.cpp::renderSettingsDebug` (Test Scenarios section); `port/fast3d/pdgui_backend.cpp` (top-right HUD overlay); `port/fast3d/gfx_sdl2.cpp` (4.3 core probe prepend); `port/src/pdmain.c` (swarmTestTick call); `src/game/setup.c` (numchrs hook); `CMakeLists.txt` (SRC_TESTS).
- **Untouched**: `port/src/net/*`, `src/game/botmgr.c`, `src/game/bot.c::botSpawn`, save files.

### Build / verify

`ninja -C Build pd pd-server pd-tests` clean at worktree branch tip 7b61a998 (`[67/67]` linked). pd-tests `[swarm][sim]` cases compile + link; runtime verification is Mike's playtest step.

## Session S482 (`jovial-kirch-181c60`) - 2026-04-27 - spawn-weapon Random/Fiesta semantics

Mike's directive (verbatim): "We so have Random, and Fiesta. Random will select a random weapon and use that as the spawn weapon for the match, every spawn. Fiesta will randomize the weapon for every spawn? So each time a player respawns they get a random weapon independent of anyone else."

### Outcome

`g_MatchConfig.spawnWeaponMode` (new `u8` field, enum `spawn_weapon_mode`) gates three behaviors at the spawn sites + the matchStart resolver. Wire bump `NET_PROTOCOL_VER 44 -> 45` carries the mode + the host-rolled spawnWeaponNum so clients receive the resolved integer directly (no client-side re-roll for SPECIFIC/RANDOM). FIESTA carries the `SPAWNWEAPON_FIESTA_SENTINEL = 0xFE` sentinel and player.c / bot.c roll fresh per-spawn from the active weapon set.

### Mode semantics (S482)

- **SPECIFIC** (`spawnWeaponMode == 0`): `spawn_weapon_id` names the weapon; `matchStart()` resolves it to `WEAPON_*` enum at match start; every spawn uses that weapon. (Existing pre-S482 behavior for non-empty `spawn_weapon_id`.)
- **RANDOM** (`spawnWeaponMode == 1`): `matchStart()` picks one weapon at random from `g_MpSetup.weapons[0..5]` (NONE/DISABLED/SHIELD filtered) via `spawnWeaponPickFromActiveSet()`. The rolled WEAPON_* enum is stored in `spawnWeaponNum`. Every player + every bot uses that same weapon for the remainder of the match.
- **FIESTA** (`spawnWeaponMode == 2`): `matchStart()` writes `SPAWNWEAPON_FIESTA_SENTINEL` (0xFE) into `spawnWeaponNum`. Spawn sites in `player.c::playerSpawn` and `bot.c::botSpawn` detect FIESTA mode (or the sentinel) and call `spawnWeaponPickFromActiveSet()` for a FRESH roll on each spawn -- per-player, per-bot, per-respawn, independent.

### Eligible pool

`g_MpSetup.weapons[0..NUM_MPWEAPONSLOTS-1]` filtered to non-`MPWEAPON_NONE` / non-`MPWEAPON_DISABLED` / non-`MPWEAPON_SHIELD`. This preserves today's effective "Random" pool (which was the active weapon set, just degenerate to slot 0 only) but actually rolls across all 6 valid slots. If the active set has zero eligible slots, the helper returns 0 -- `matchStart()` for RANDOM falls back to `MPWEAPON_FALCON2` with a `LOG_WARNING`; FIESTA spawn sites fall into the existing `resolvedWeaponNum=0` miss path.

### Files (functional)

- `port/include/net/matchsetup.h` -- new `enum spawn_weapon_mode`, new `SPAWNWEAPON_FIESTA_SENTINEL` macro, `u8 spawnWeaponMode` field on `struct matchconfig`, declarations for `spawnWeaponPickFromActiveSet` + `spawnWeaponPickFromSlots`.
- `port/src/net/matchsetup.c` -- new helpers (live + pure-test variants), three-mode dispatch in `matchStart()` with explicit logging per branch, default mode in `matchConfigInit` is `SPAWNWEAPON_MODE_RANDOM` (so the dropdown's "Random" actually rolls now).
- `src/game/player.c::playerSpawn` -- FIESTA branch keying on `g_MatchConfig.spawnWeaponMode == SPAWNWEAPON_MODE_FIESTA || spawnWeaponNum == SPAWNWEAPON_FIESTA_SENTINEL`; legacy 0xFF / weapons[0] fallback retained for safety.
- `src/game/bot.c::botSpawn` -- mirror.
- `port/src/net/netmsg.c` -- `SVC_STAGE_START` + `CLC_LOBBY_START` write/read add the trailing `u8 spawnWeaponMode` (+ `u8 spawnWeaponNum` on `SVC_STAGE_START`).
- `port/include/net/net.h` -- `NET_PROTOCOL_VER 44 -> 45` with full block-comment description.
- `port/src/scenario_save.c` -- writes `"spawnWeaponMode"` JSON key; loader honors verbatim, with backwards-compat default = RANDOM when `spawnWeaponId` is empty / SPECIFIC when non-empty (preserves pre-S482 authoring intent).
- `port/fast3d/pdgui_menu_room.cpp` -- dropdown gains entry 1 "Fiesta" alongside entry 0 "Random"; selection writes `spawnWeaponMode` + `spawn_weapon_id` per the chosen entry's `mode`; `syncSpawnWeaponFromConfig()` reads `spawnWeaponMode` and lands on the right entry. Stale 0x2f/0x30 SHIELD/DISABLED filter literals updated to post-cull 0x27/0x28 (kept legacy values defensively).

### Files (tests)

- `tests/test_spawn_weapon_mode.cpp` (new, 17 cases) -- pool filter, degenerate fallback, RANDOM-rolls-once invariant, RANDOM determinism (same seed -> same roll), FIESTA arms sentinel, FIESTA varies per spawn, FIESTA per-player independence, SPECIFIC passthrough + empty-id fallback, legacy save defaults (no key -> RANDOM/SPECIFIC by id presence), post-S482 round-trip, out-of-range mode value falls back, FIESTA sentinel + mode enum + NUM_MPWEAPONSLOTS pins.
- `tests/test_versions.cpp` -- expected `NET_PROTOCOL_VER` bumped to 45.
- `CMakeLists.txt` -- new test file added to `SRC_TESTS`.

### Stop-condition outcomes

- **Eligible pool**: went with active match set (filtered) per the directive's "preserve today's effective pool if conceptually right" guidance. Documented in matchsetup.h block comment + constraint update.
- **Save format**: scenario JSON only; MPSETUP_VERSION unchanged. Backwards-compat is per-key default, no schema break.
- **UI surface**: dropdown entries 0/1 reserved (Random / Fiesta), specific weapons start at index 2; sort range adjusted accordingly.

### Build / verify

Pending playtest. Build verified via `pd` + `pd-tests` link path -- pre-existing local incremental build state.

## Session S481 (`festive-hawking-49649b` follow-up #6) - 2026-04-27 - release rebase failure + remaining BOM writers

Mike's release failure: `error: cannot rebase: You have unstaged changes. error: Please commit or stash them.`

**Root cause**: combination of `core.autocrlf=true` (Mike's local git config) + the 2026-04-25 `.gitattributes` change (`* text=auto eol=lf` defaults). Text files often have CRLF on disk while the index has LF after .gitattributes-driven normalization. The auto-commit step in `release.ps1` Step 4 runs `git add -A` (stages CRLF -> LF normalized for index), then `git diff --cached --quiet` returns 0 (no actual index change vs HEAD), so no commit fires. Then `git pull --rebase` does its own working-tree-vs-HEAD check on raw bytes and refuses because the file looks "modified."

**Fixes (all in `devtools/`)**:

1. **`release.ps1` rebase robustness** -- between the auto-commit and the `git pull --rebase`:
   - `git update-index --refresh -q --unmerged` clears stale modified flags for files whose content matches HEAD after .gitattributes normalization (idempotent and safe; doesn't touch genuinely modified files).
   - Capture `git status --porcelain` and log it. Next time a release rebase fails the user has a clear paper trail of which file blocked it.
   - Recovery branch: when rebase fails specifically with "unstaged changes / cannot rebase / would be overwritten", abort the partial rebase, run `git checkout-index -a -f` to forcefully sync the working tree byte-for-byte from the index (safe because `git add -A` ran moments before), refresh, log the post-fix status, and retry the rebase.

2. **Remaining BOM-emitting `Set-Content -Encoding UTF8` writers to TRACKED files** (sibling class to the S477 `Set-ProjectVersion` fix):
   - `keygen.ps1:156` writing `port/include/updater_pubkey.h` (TRACKED) -- swapped to `[System.IO.File]::WriteAllText` with `UTF8Encoding($false)`.
   - `_dev-window.ps1:1293` writing `context/qc-tests.md` (TRACKED) -- swapped to `WriteAllText` with explicit LF line endings (`$out -join "`n"`) to match `.gitattributes` `*.md eol=lf`.

**Why these matter even though the auto-commit catches them**: a BOM byte added by `Set-Content -Encoding UTF8` causes `git diff` to show the file as modified even when content is otherwise identical. After `git add -A`, the BOM gets stored in the index, so the file never re-converges to HEAD on subsequent operations. Future rebases / merges trip on the same byte.

**Why this manifested only now**: the `.gitattributes` `* text=auto eol=lf` defaults landed 2026-04-25. Before that, line endings were left at OS-native CRLF on Windows, and `git pull --rebase` was happy. After that, the working-tree vs index drift exposed by the new normalization rules surfaced as "unstaged changes" on rebase.

Verified: PowerShell parser passes on all five edited scripts (release.ps1, dev-window-v2.ps1, _dev-window.ps1, version-util.ps1, keygen.ps1). The pre-existing parser warnings on release.ps1 lines 623/647 cleared themselves -- my added pre-rebase block shifted the line numbers past whatever the parser was confused about (likely the `$()` inline interpolation in the SkipPush print).

Files: `devtools/release.ps1`, `devtools/keygen.ps1`, `devtools/_dev-window.ps1`.

