# SP-stage MP-readiness audit - 2026-04-24

Session S453 (Priority 6 of the foundation pass; extended in P8).  Walks every arena registered in the catalog and classifies MP-readiness.

## P8 static-analysis update (2026-04-24 post-P7)

Counting `lift(` / `lift_door(` / `escastep(` macro invocations in each stage's SP and MP setup C files gives the actual prop inventory per mode.  Results surfaced a diagnosis correction:

**MP setup files for SP-class stages are EMPTY of lifts / escastep.**  `src/setups/mp_setupdish.c` (CI), `mp_setuppete.c` (Chicago), `mp_setupeld.c` (Villa), `mp_setuplue.c` (Infiltration), `mp_setupdepo.c` (G5), `mp_setupdam.c` (Pelagic) all have `lift()=0, lift_door()=0, escastep()=0`.  The SP variants (`setupdish.c`, `setuplue.c`, etc.) DO contain those macros -- CI SP has 2 lifts + 8 lift_doors, Infiltration SP has 4 lifts + 8 lift_doors, Airbase SP has 5 lifts + 16 lift_doors + 40 escasteps, etc.

This means P5's `mptransport_diffflag` relax is a **NOOP on the current codebase**: there are no lifts in the MP setup for the filter to reject in the first place.  The real fix for B-228 requires augmenting the MP setup at load time with the SP setup's transport props ("Option E" in the P5 analysis).  Keeping P5 is still correct as structural insurance -- if a future mod or a shipped fix adds lifts to an MP setup with difficulty / player-count exclusion bits, the relax applies -- but alone it doesn't close B-228.

**B-228 row in `context/bugs.md` reopened with the corrected diagnosis.**  Option E design is sketched in the bug's row and deferred as a follow-up scope pass (loads a second setup blob per stage, filters to LIFT / ESCASTEP, runs them through the existing OBJTYPE switch).

## SP-setup transport-prop inventory (authoritative count)

Numbers are direct macro counts in `src/setups/setup<SLUG>.c`.  They identify which stages have lifts to augment if / when Option E lands.

| Stage                | SP setup file    | lift() | lift_door() | escastep() |
|----------------------|------------------|-------:|------------:|-----------:|
| CITRAINING           | setupdish.c      | 2      | 8           | 0          |
| CHICAGO              | setuppete.c      | 0      | 0           | 0          |
| VILLA                | setupeld.c       | 0      | 0           | 0          |
| INFILTRATION         | setuplue.c       | 4      | 8           | 0          |
| G5BUILDING           | setupdepo.c      | 0      | 0           | 0          |
| PELAGIC              | setupdam.c       | 0      | 0           | 0          |
| DEFECTION            | setupame.c       | 2      | 8           | 0          |
| INVESTIGATION        | setupear.c       | 1      | 0           | 0          |
| AIRBASE              | setupcave.c      | 5      | 16          | 40         |
| AIRFORCEONE          | setuprit.c       | 4      | 1           | 0          |
| CRASHSITE            | setupazt.c       | 0      | 0           | 0          |
| DEEPSEA              | setuppam.c       | 1      | 0           | 0          |
| DEFENSE              | setupimp.c       | 2      | 8           | 0          |
| ATTACKSHIP           | setuplee.c       | 6      | 16          | 0          |
| SKEDARRUINS          | setupsho.c       | 1      | 0           | 0          |

Airbase is the fattest lift target (5 + 16 + 40).  Chicago, Villa, G5Building, Pelagic, and Crashsite have zero lifts in their SP setup either -- B-228 as originally reported ("fire-escape stairs on Chicago") is actually about static bg geometry, not lift / escastep props.  Chicago's fire-escape stairs are bg_pete tiles, not animated objects; they always load.

**Conclusion**: the stages that would benefit from Option E (SP-setup transport-prop augmentation) are, in descending impact order: Airbase, Attackship, AirForceOne, Infiltration, CITraining, Defection, Defense, Investigation, Deepsea, SkedarRuins.  Chicago / Villa / G5Building / Pelagic / CrashSite don't have lifts to augment.

## Rubric

| Status                     | Meaning                                                                                             |
|----------------------------|-----------------------------------------------------------------------------------------------------|
| **NATIVE-MP**              | Originally authored as an MP arena.  No SP scripting; no special-case handling expected.            |
| **SP-IN-MP, P5-RELAXED**   | SP-class stage that P5 explicitly relaxes `OBJTYPE_LIFT` / `OBJTYPE_ESCASTEP` exclusion for.        |
| **SP-IN-MP, PENDING TEST** | SP-class stage NOT yet in P5's relax list.  May or may not have transport props; needs playtest.    |
| **BLACKLISTED**            | Data absent from the shipped build; `stagenumIsPlayableInMp` returns false.                         |
| **SYSTEM**                 | Title / boot / credits stage; not a gameplay arena.                                                 |

Playtest = load the stage via Combat Sim, confirm: (a) no crash, (b) players can traverse all intended areas, (c) any elevator / staircase functions, (d) if hosted as a listen server with 2+ clients, transport props behave consistently.

## Dark (native MP, indices 0-12)

| Arena slug            | stagenum | Status     | Notes                                  |
|-----------------------|---------:|------------|----------------------------------------|
| `mp_skedar`           | 0x1e     | NATIVE-MP  | Skedar-themed arena.                   |
| `mp_pipes`            | 0x15     | NATIVE-MP  |                                        |
| `mp_ravine`           | 0x03     | NATIVE-MP  |                                        |
| `mp_g5building`       | 0x0c     | NATIVE-MP  | Distinct from SP `g5building`.         |
| `mp_sewers`           | 0x32     | NATIVE-MP  |                                        |
| `mp_warehouse`        | 0x2c     | NATIVE-MP  |                                        |
| `mp_grid`             | 0x37     | NATIVE-MP  | The PD "Grid" arena (distinct from "The Grid" feature). |
| `mp_ruins`            | 0x31     | NATIVE-MP  |                                        |
| `mp_area52`           | 0x2b     | NATIVE-MP  |                                        |
| `mp_base`             | 0x29     | NATIVE-MP  |                                        |
| `mp_fortress`         | 0x34     | NATIVE-MP  |                                        |
| `mp_villa`            | 0x35     | NATIVE-MP  | Distinct from SP `villa`.              |
| `mp_carpark`          | 0x2d     | NATIVE-MP  |                                        |

No action needed.  Regression check after P5: confirm these still lift / door identically.

## Solo Missions repurposed as MP (indices 13-26)

| Arena slug       | stagenum           | Status                   | Notes                                                |
|------------------|-------------------:|--------------------------|------------------------------------------------------|
| `defection`      | STAGE_DEFECTION 0x30 | SP-IN-MP, PENDING TEST  | Carrington Villa.  Doors, chrs.  No known lift.      |
| `investigation`  | STAGE_INVESTIGATION 0x33 | SP-IN-MP, PENDING TEST | dataDyne lab.  Probably has escastep / lift; AUDIT. |
| `villa`          | STAGE_VILLA 0x2c   | **SP-IN-MP, P5-RELAXED** | Carrington Estate.  Has lifts; relax applied.        |
| `chicago`        | STAGE_CHICAGO 0x1d | **SP-IN-MP, P5-RELAXED** | Fire-escape stairs + lift.  Relax applied.           |
| `g5building`     | STAGE_G5BUILDING 0x1e | **SP-IN-MP, P5-RELAXED** | G5 elevator.  Relax applied.                         |
| `infiltration`   | STAGE_INFILTRATION 0x2f | **SP-IN-MP, P5-RELAXED** | dataDyne.  Lifts.  Relax applied.                  |
| `airbase`        | STAGE_AIRBASE 0x27 | SP-IN-MP, PENDING TEST   | Air Force 1 base.  AUDIT for lifts.                  |
| `airforceone`    | STAGE_AIRFORCEONE 0x31 | SP-IN-MP, PENDING TEST | AF1 interior.  Likely stair sections.                |
| `crashsite`      | STAGE_CRASHSITE 0x1c | SP-IN-MP, PENDING TEST | Mostly outdoor; no lifts expected.                   |
| `pelagic`        | STAGE_PELAGIC 0x21 | **SP-IN-MP, P5-RELAXED** | Pelagic II sub.  Relax applied.                      |
| `deepsea`        | STAGE_DEEPSEA 0x38 | SP-IN-MP, PENDING TEST   | Alien craft interior.  AUDIT for lifts.              |
| `defense`        | STAGE_DEFENSE 0x2d | SP-IN-MP, PENDING TEST   | CI defense mission.  May have auto-guns / lifts.     |
| `attackship`     | STAGE_ATTACKSHIP 0x34 | SP-IN-MP, PENDING TEST | Skedar attack ship.  Probably stair / lift.          |
| `skedarruins`    | STAGE_SKEDARRUINS 0x2a | SP-IN-MP, PENDING TEST | Distinct from `mp_skedar`.  Outdoor / terrain.       |

**Follow-up**: for every "PENDING TEST" row above that reveals a transport-prop exclusion in playtest, add its stagenum to the P5 relax list in `src/game/setup.c::setupLoadStage`.  The diagnostic log line `SETUP.LIFT: SP-in-MP stagenum=0x%02x ...` is the telemetry for this audit.

## Classic MP (indices 27-31)

| Arena slug       | stagenum            | Status     | Notes                                  |
|------------------|--------------------:|------------|----------------------------------------|
| `mp_temple`      | STAGE_MP_TEMPLE 0x11 | NATIVE-MP | Retro-themed; Classic category.        |
| `mp_complex`     | STAGE_MP_COMPLEX 0x0b | NATIVE-MP | GoldenEye throwback.                  |
| `mp_grid6`       | STAGE_TEST_MP6 0x2e | NATIVE-MP  |                                        |
| `mp_grid2`       | STAGE_TEST_MP2 0x2a | NATIVE-MP  |                                        |
| `mp_felicity`    | STAGE_MP_FELICITY 0x33 | NATIVE-MP |                                      |

No action.

## Bonus (indices 55-70)

| Arena slug       | stagenum            | Status       | Notes                                                 |
|------------------|--------------------:|--------------|-------------------------------------------------------|
| (index 55 NULL)  | STAGE_24 0x10       | BLACKLISTED  | Kakariko Stormy - bg data removed; filtered.          |
| (index 56 NULL)  | STAGE_TEST_MP7 0x2f | BLACKLISTED  | Dark Noon Valley - bg data removed; filtered.         |
| `test_arch`      | STAGE_TEST_ARCH 0x18 | SP-IN-MP, PENDING TEST | Archives.                                   |
| `test_dest`      | STAGE_TEST_DEST 0x1a | SP-IN-MP, PENDING TEST | Destructible test.                          |
| `extra16`-`extra24`, `extra26` | STAGE_EXTRA16..24, 26 | SP-IN-MP, PENDING TEST | All AllInOne-lineage bonus extras; traversal unverified. |
| `test_lam`       | STAGE_TEST_LAM 0x50 | SP-IN-MP, PENDING TEST | Lam test stage.                              |
| (index 70 NULL)  | STAGE_EXTRA25 0x5e  | BLACKLISTED  | Paradox - data removed; filtered.                     |

Mod-authored / extra stages are intrinsically less predictable than the shipped MP arenas.  The P5 relax list does not include them by default because their authoring provenance is mixed.  Recommend per-stage playtest before adding any to the relax list.

## Random

| Arena slug            | stagenum | Status    | Notes                                  |
|-----------------------|---------:|-----------|----------------------------------------|
| `mp_random_multi`     | 0x02     | META      | Wrapper that selects a random arena.   |
| `mp_random_solo`      | 0x03     | META      | Wrapper for solo-style random picks.   |

These synthesize a pick at match start; no stage data of their own.

## Whitelist verification

`stagenumIsPlayableInMp` in `src/game/mplayer/setup.c:217-227` blacklists exactly three stages: `STAGE_24`, `STAGE_TEST_MP7`, `STAGE_EXTRA25`.  All three are confirmed BLACKLISTED above.  No stage in the matrix is erroneously whitelisted that should be blacklisted.

Per the P6 directive "Update `stagenumIsPlayableInMp` if the audit finds stages that were erroneously whitelisted": none found.  The whitelist requires no change in this pass.

## Playtest checklist for foundation-pass close-out

Minimum coverage to validate the P5 change:

- [ ] CI Training via Combat Sim: elevator pads behave (call, open, travel, open).
- [ ] Chicago via Combat Sim: fire-escape stair section is traversable; lifts move.
- [ ] Villa via Combat Sim: interior lifts behave.
- [ ] Grid (native MP) via Combat Sim: regression, lifts / doors behave as before.
- [ ] Skedar Ruins via Combat Sim: regression, doors behave.
- [ ] Log line `SETUP.LIFT: SP-in-MP stagenum=...` fires ONLY for SP-in-MP stages; does NOT fire for native MP arenas.

If any PENDING-TEST row above shows "lifts / staircase missing" in playtest, add that stagenum to the `switch` in `setupLoadStage`'s `mptransport_diffflag` block.

## Cross-cutting observations

1. **Authoring contract still split.** `g_MpArenas[]`, `s_ArenaNames[]`, and langbank must all agree for an arena to render usably.  `stagenumIsPlayableInMp` is a separate fourth table.  A future pass should consolidate into a data-driven probe (see the structural note in `src/game/mplayer/setup.c:210`).  Not tonight's work.

2. **P5 relax list is empirical.** It was built from Mike's B-228 report + adjacent SP stages most commonly hosted as CS arenas.  The PENDING-TEST rows are the next batch to vet.

3. **Lift sync drift is still unverified.** The P5 change ensures lifts load; whether two clients see the lift at the same position when one of them steps on it is covered by the existing server-authoritative prop-sync path but has not been playtested end-to-end on an SP-in-MP stage.  Flagged as a follow-up in the P5 commit message.
