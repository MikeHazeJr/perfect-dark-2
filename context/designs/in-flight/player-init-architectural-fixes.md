---
status: active broader closure under T-ENGINE-004; B-1101/B-1102/B-1067/B-1103/B-1104 production-verified regression gates
authored: 2026-04-27 (mystifying-hofstadter-deca99 worktree)
updated: 2026-08-26 (codex-v1-m1-runner-20260812)
synthesizes: context/audits/player-init-comparison-upstream-2026-04-26.md
             context/audits/char-init-weapon-spawn-comparison-opus47-2026-04-26.md
             context/audits/char-init-weapon-spawn-comparison-sonnet46-2026-04-26.md
matrix-cache-fix: cafbd2ef (Phase B drop) ALREADY LANDED -- not in scope
---

# Player init architectural fixes (post-2026-04-26 audits)

## 1. Findings synthesis

The three audit docs converge on a small number of architectural classes.
Findings deduplicated below with file:line citations against the current
HEAD (post `cafbd2ef`, 2026-04-27). The matrix-cache headline bug is
ALREADY FIXED at HEAD; this doc does not propose to revisit it.

### 1.A Catalog accessors return zero/default silently on miss (CATALOG axis)

Cited by all three audits.

- Opus H-1, H-4, H-5, H-6, H-11, H-12 (Section H of opus47 audit).
- Sonnet H-3 (Section H of sonnet46 audit).
- Prior-session F-2 (Section F of original audit).

Concrete sites in the spawn path:

- [port/src/assetcatalog_api.c:1030-1064](../../port/src/assetcatalog_api.c) MP weapon
  accessors (`catalogGetMpWeaponNum`, `catalogGetMpWeaponPriAmmoType`,
  `catalogGetMpWeaponPriAmmoQty`, `catalogGetMpWeaponSecAmmoType`,
  `catalogGetMpWeaponSecAmmoQty`, `catalogGetMpWeaponUnlockFeature`):
  bounds-check on `mpweapon_idx`, return `0` silently on OOB. No log,
  no failure flag.
- [port/src/assetcatalog_api.c:963-1021](../../port/src/assetcatalog_api.c) body / head
  property accessors (`catalogGetBodyIsMale`, `catalogGetBodyType`,
  `catalogGetBodyHeight`, `catalogGetBodyAnimScale`, `catalogGetBodyCanVaryHeight`,
  `catalogGetBodyIsComplete`, `catalogGetBodyHandFilenum`, parallel head accessors):
  bounds-check on array dim only (`< 152`), no membership check against
  the catalog. Read `g_HeadsAndBodies[bodynum].field` directly. If the
  index is in array bounds but the slot was never registered by the
  catalog, returns whatever is in BSS for that slot.
- [port/src/assetcatalog_api.c:802-817](../../port/src/assetcatalog_api.c)
  `catalogGetBodyScaleByIndex`: returns `1.0f` silently on miss (and only
  sets `g_CatalogFailure` flag for the OOB-index miss; in-bounds
  unpopulated slot is still silent).
- [port/src/assetcatalog_api.c:828-843](../../port/src/assetcatalog_api.c)
  `catalogGetStageResultByIndex`: writes zero-filled `out` and returns
  `0` on miss. Sets the `g_CatalogFailure` flag and logs ERROR, BUT
  callers do not check the return.

Caller-side consequence in spawn path:

- [src/game/player.c:1790-1846](../../src/game/player.c) `playerSpawn`
  spawn-with-weapon resolution. If `catalogGetMpWeaponNum(idx)` returns
  `0`, `resolvedWeaponNum` falls through to the else branch which equips
  `g_DefaultWeapons[]`. In normal MP, [src/game/playerreset.c:225-231](../../src/game/playerreset.c)
  has skipped INTROCMD_WEAPON, so `g_DefaultWeapons[]` is also `0`. Net:
  spawn unarmed. The `SPAWN: ... -- spawnwithweapon set but no valid weapon`
  log line at player.c:1843 is the only signal. This is the B-219 v2
  narrative: "user picked `base:remotemine` but saw Falcon (Silenced)".
- [src/game/bot.c:506-549](../../src/game/bot.c) `botSpawn` parallel.
- [src/game/body.c:183-184](../../src/game/body.c) `body0f02ce8c` reads
  `catalogGetBodyScaleByIndex` and `catalogGetBodyAnimScale`. A scale
  of `0` from an unpopulated slot would zero out chr geometry.
- [src/game/body.c:163-172](../../src/game/body.c) `bodyLoad` calls
  `catalogGetBodyModeldef` which lazy-loads via filenum lookup; the
  `CATALOG_CRITICAL` log line is fired only by the bodyLoad caller.
  Other read-paths for body/head accessor calls do not log misses.

### 1.B Asymmetric MP-mode guards on the spawn-with-weapon vs INTROCMD_WEAPON pair (MP-MODE axis)

Cited by Opus H-2 + Section F-1/F-5/F-8 cluster of prior audit.

- [src/game/player.c:1790](../../src/game/player.c): the SPAWNWITHWEAPON
  branch entry condition is `if (g_MpSetup.options & MPOPTION_SPAWNWITHWEAPON)`
  only.
- [src/game/playerreset.c:225-226](../../src/game/playerreset.c): the
  INTROCMD_WEAPON skip-gate keys on
  `(g_Vars.normmplayerisrunning && (g_MpSetup.options & MPOPTION_SPAWNWITHWEAPON))`.
- [src/game/bot.c:506](../../src/game/bot.c): bot-side mirror of
  player.c:1790, same dropped guard.

Co-Op and Counter-Op Bond consequence:

- [src/game/mplayer/mplayer.c:842](../../src/game/mplayer/mplayer.c)
  `mpInit()` sets `g_MpSetup.options` to a baseline that DOES NOT
  include `MPOPTION_SPAWNWITHWEAPON`. But:
- [port/src/net/matchsetup.c:101](../../port/src/net/matchsetup.c)
  `matchConfigReset()` defaults `g_MatchConfig.options = MPOPTION_SPAWNWITHWEAPON`.
- [port/src/net/matchsetup.c:703](../../port/src/net/matchsetup.c)
  `matchStart()` propagates `g_MpSetup.options = g_MatchConfig.options`
  (i.e. SPAWNWITHWEAPON ON by default for every match config).
- [src/game/mplayer/mplayer.c:550, 884](../../src/game/mplayer/mplayer.c)
  `mpReset()` flips `normmplayerisrunning` to `true` on entry / `false`
  on exit. Co-Op / Counter-Op do NOT call mpReset (they reach
  `g_Vars.mplayerisrunning=true` via [src/game/lv.c:1985](../../src/game/lv.c)
  and friends, with `normmplayerisrunning=false`).

Net result: in Co-Op or Counter-Op Bond, `MPOPTION_SPAWNWITHWEAPON` is
set in `g_MpSetup.options` (propagated from match config), so the
dropped-guard at player.c:1790 fires, AND the gate at playerreset.c:225
keys on `normmplayerisrunning=false` so it does NOT skip INTROCMD_WEAPON.
Both apply: the player gets the mission's intro inventory PLUS gets
equipped to the spawn weapon. The final equip is the spawn weapon, but
the inventory holds both. This is F-8 in prior audit; LIVE conditional
on Co-Op / Counter-Op having SPAWNWITHWEAPON in the active options.

### 1.C ROM-version-gate-strip latent uninit class (ROM-GATE axis)

Cited by all three audits.

- Opus Section G + H-3.
- Sonnet "Change class: uninit bug fixes" in playermgrAllocatePlayer.
- Prior-session F-4 + I-2.

The pattern: a struct field's only initialization in upstream lived
inside a `#if VERSION >= VERSION_*` block. PD2 dropped the gate but did
not add an unconditional replacement. PD2's `mempAlloc(MEMPOOL_STAGE)`
does not zero-fill, so the field reads stale heap garbage.

Confirmed precedents:
- `currentplayer->visionmode` -- fixed at [src/game/playermgr.c:413](../../src/game/playermgr.c)
  unconditional `= VISIONMODE_NORMAL`. Upstream gating: `#if VERSION >= VERSION_JPN_FINAL`.
- `gunctrl.handmodeldef`, `gunctrl.cartmodeldef` -- fixed at
  [src/game/playermgr.c:425-426](../../src/game/playermgr.c) unconditional
  `= NULL`. Round-6 playtest log captured `handmodeldef=0x7c7b663545bbcbd1`.

Class not exhausted. Other `struct player` / `struct gunctrl` /
`struct hand` / `struct chrdata` fields whose only initialization in
upstream was inside a `#if VERSION >=` block are candidates. Audit
obligation defined in cohort C below.

### 1.D B-181 fallback silently mutates user MP options (USER-OPTIONS axis)

Cited by Opus H-8 + prior F-7.

- [src/game/setup.c:2745-2746](../../src/game/setup.c) `setupLoadStage`
  ORs `MPOPTION_SPAWNWITHWEAPON` onto BOTH `g_MatchConfig.options` and
  `g_MpSetup.options` when world pickups are below threshold. The
  user-pick preservation logic at lines 2748-2794 preserves an explicit
  spawn-weapon choice but does NOT record that the OR-set was an engine
  override.

Risk: if the user explicitly disabled spawn-with-weapon via the menu
checkbox, the engine silently re-enables it. The mutation persists
across matches if `g_MpSetup.options` is not re-cleared on subsequent
match start (matchsetup.c:703 reassigns from `g_MatchConfig.options`,
which is itself ALSO mutated by setup.c:2745 -- so the override
propagates to the next match too, until matchConfigReset() runs again).

### 1.E Defensive fallbacks silently substitute identity (FALLBACK-IDENTITY axis)

Cited by Opus H-5, H-6, H-9 + prior F-3.

- [src/game/body.c:177-181](../../src/game/body.c) `body0f02ce8c` clamps
  bad `bodynum` (negative or >= 152) to slot 0 (DJ Bond) with only a
  WARNING log. If a catalog miss ever returns an OOB index, the chr
  silently appears as DJ Bond. Players selecting a mod body see DJ Bond
  with no in-game indication.
- [src/game/bodyreset.c:58-64](../../src/game/bodyreset.c) replaces
  upstream's stage-table override (numeric stage indices for INFILTRATION
  / RESCUE / ESCAPE) with `strcmp(g_MissionConfig.stage_id, "base:infiltration")`
  literal chain. Mod stages or any stage_id not matching the three
  literals silently default to `g_NumActiveHeadsPerGender = 8` instead
  of the intended 5/4/5.
- [src/game/bot.c:506-549](../../src/game/bot.c) does NOT call
  `modelmgrLoadProjectileModeldefs(resolvedWeaponNum)` per-bot before
  `botinvSwitchToWeapon`, unlike player.c:1822. Asymmetric reliance on
  the [src/game/setup.c:2825-2851](../../src/game/setup.c) batch
  preload. Today benign because the batch covers `g_MpSetup.weapons[]`
  + `g_MatchConfig.spawnWeaponNum`; a future per-bot loadout feature
  would silently miss FP models.

### 1.F Out-of-scope findings

These were raised by the audits but resolved at HEAD or out of this doc's scope:

- Sonnet H-1 `HAND_LEFT.unk0dd8` uninit -- benign at HEAD because Phase B
  cafbd2ef removed the cache USE branch entirely. The cached buffer is
  dead state for both hands. Mike directive: do not re-touch the matrix
  cache.
- Sonnet H-2 `gunmodeldef` not cleared in `bgunEnterFlux` -- speculative,
  no observed bug. Out of scope for this pass; flagged as surveillance.
- Sonnet H-4 netPlayersAllocate ordering -- Plausibility LOW per Sonnet,
  no `g_Vars.currentplayer` deref in the netPlayersAllocate path
  observed in the audit. Out of scope.
- Sonnet H-5 visibility-gate change -- this was an INTENTIONAL fix in
  B-246 Issue 1, not a bug. Out of scope.
- Opus H-7 chrInit NULL chr propagation -- silent-fail vs crash tradeoff.
  Already mitigated at chr.c:1139-1142. Out of scope.
- Opus H-10 GE-legacy weapon enum residuals -- propagation grep
  confirms no live references in src/ port/src/ include/ outside of
  comments. CLEAR.
- Opus H-11 catalog stage_id race -- speculative, conditional on
  catalog-readiness invariant. Out of scope; subsumed by 1.A's loud-fail
  treatment of `catalogGetStageResultByIndex`.
- Opus H-13, H-14 -- RULED-OUT in source audit.

---

## 2. Invariants

The findings reduce to five invariants. Each, if enforced across the
audited surface AND propagated to sibling sites, would prevent the
current findings PLUS the analogous future bugs.

### INV-1. Catalog accessors fail loudly, not silently.

Catalog accessors that can fail (OOB index, in-bounds unpopulated slot,
catalog-not-ready) MUST signal failure to callers, not return a
zero/default sentinel that pretends success. Concretely:

- For accessors returning a value that has no in-band sentinel
  (`s32` weapon nums, `f32` scale, `s32` filenum where 0 is a valid
  ROM file id), introduce `_checked` companion functions with
  `(out, *out_value) -> bool` signature.
- For accessors returning a pointer or struct, the existing NULL/empty
  return is acceptable IFF the caller is required to check (and the
  audit confirms callers do).
- Every miss path emits one `LOG_WARNING` (or `LOG_ERROR` if a known
  game-critical caller) with a hierarchical channel `CATALOG.MISS:`
  + the specific accessor name + the failing index + the call-site context.
  The miss log is rate-limited (one per accessor per match start,
  not per frame).

The legacy accessor API stays for non-spawn-critical callers (UI
property reads, etc.). Spawn-critical paths (`playerSpawn`,
`botSpawn`, `body0f02ce8c`, `bgunTickMasterLoad`'s
`catalogGetBodyHandFilenum` call) MUST migrate to the checked variant.

Affected sites per invariant:
- Sites that get the new checked accessors (NEW): `port/src/assetcatalog_api.c`
  -- 5 weapon accessors + 7 body accessors + 5 head accessors + 2
  body modeldef + 1 stage result. Trivially-fixable additive change.
- Spawn-path migrations (CHANGE): [src/game/player.c:1799-1828](../../src/game/player.c),
  [src/game/bot.c:517-535](../../src/game/bot.c),
  [src/game/body.c:183-184](../../src/game/body.c),
  [src/game/bondgun.c HANDS state](../../src/game/bondgun.c) (line near
  `catalogGetBodyHandFilenum` per Sonnet section).
  Trivially-fixable.
- Caller-handle-failure decisions: spawn-path callers that resolve
  `0`/miss MUST take an explicit fallback path (force-equip
  `WEAPON_UNARMED` rather than fall through to `g_DefaultWeapons[0]=0`)
  so the player never spawns truly empty-handed. Requires-design-call
  on which fallback to use.

### INV-2. Sibling MP-mode guards are symmetric across paths that share a precondition.

Two code paths that observe the same precondition (`MPOPTION_SPAWNWITHWEAPON`
set + match mode predicate) MUST use the SAME predicate to gate their
behavior, or the divergence MUST be commented at BOTH sites with a
bidirectional cross-reference.

The specific case: spawn-with-weapon and INTROCMD_WEAPON are mutually
exclusive per spawn. If the SPAWNWITHWEAPON bit is set and a path is
going to fire spawn-with-weapon for a given player, then INTROCMD_WEAPON
MUST NOT also fire for that same player. The existing
[src/game/playerreset.c:225](../../src/game/playerreset.c) gate is
correct; the asymmetry is at [src/game/player.c:1790](../../src/game/player.c)
and [src/game/bot.c:506](../../src/game/bot.c).

Resolution options (one of these MUST be picked):

- **Option A**: Re-add the `g_Vars.normmplayerisrunning &&` outer check
  at player.c:1790 and bot.c:506. Restores symmetry. Cost: in Co-Op /
  Counter-Op, the SPAWNWITHWEAPON bit becomes a no-op (the user's
  intro-loadout wins). Affects only the audit-described corner case;
  most playtests have `iscoop=true` / `isanti=true` use the intro
  weapons anyway, so this matches user expectations.

- **Option B**: Force-clear `MPOPTION_SPAWNWITHWEAPON` from
  `g_MpSetup.options` and `g_MatchConfig.options` on entry to Co-Op /
  Counter-Op. Same observable behavior as Option A, but accomplished by
  scrubbing the input rather than gating the consumer. Requires
  identifying every Co-Op / Counter-Op entry site.

- **Option C**: Extend the playerreset.c:225 gate to also fire in
  Co-Op / Counter-Op (drop `normmplayerisrunning &&`). Symmetric in the
  other direction: SPAWNWITHWEAPON wins, intro inventory loses. Changes
  Co-Op / Counter-Op user-visible behavior (no more campaign-mission
  intro weapons in Co-Op when SPAWNWITHWEAPON is set as the default).
  Higher risk of user-facing surprise.

**Recommended: Option A.** It restores the upstream invariant, has the
smallest behavioral footprint, and is what the playerreset.c:225
comment already assumes ("normal MP" gate). Decision-for-Mike below.

Affected sites:
- [src/game/player.c:1790](../../src/game/player.c) (CHANGE)
- [src/game/bot.c:506](../../src/game/bot.c) (CHANGE)
- [src/game/setup.c:2745-2746](../../src/game/setup.c) B-181 fallback:
  also gate the OR-set on `normmplayerisrunning` so the engine fallback
  cannot leak the bit into Co-Op / Counter-Op match config (CHANGE).
  Cohort overlap with INV-4.

### INV-3. Struct-field initialization is unconditional for the USA-ROM-only target.

Any field whose only initialization in upstream lived inside a
`#if VERSION >= VERSION_*` block MUST be initialized unconditionally in
PD2. PD2 strips all VERSION gates; PD2's `mempAlloc(MEMPOOL_STAGE)` does
not zero-fill; uninitialized struct fields hold heap garbage.

Audit obligation: walk upstream's `#if VERSION >=` blocks against PD2
HEAD across `struct player`, `struct gunctrl`, `struct hand`,
`struct chrdata`. For each gated init in upstream, confirm PD2 has an
unconditional replacement OR the gate is wrapping an entire allocator
that PD2 has dropped (in which case the field is dead and the struct
member can also be dropped).

The audit produces a checklist file
`context/audits/rom-gate-uninit-fields-2026-04-26.md` enumerating
every field, its gated-init line in upstream, and its
unconditional-init site in PD2 (or `MISSING` if no replacement).
Each `MISSING` entry becomes a one-line code change.

Affected sites per invariant:
- Audit checklist file (NEW).
- 5-15 expected one-line additions to [src/game/playermgr.c](../../src/game/playermgr.c)
  / [src/game/bondgunreset.c](../../src/game/bondgunreset.c) /
  [src/game/chr.c](../../src/game/chr.c) per the audit findings
  (CHANGE).

### INV-4. User-set MP options are never silently mutated by engine fallbacks.

Engine code that needs to override a user-set MP option (e.g. B-181
fallback forcing `MPOPTION_SPAWNWITHWEAPON` ON when world pickups are
sparse) MUST:

- Record the fact that the bit was engine-forced in a separate
  state field (either a `g_MatchConfig.options_engine_forced` mask or a
  static `s_SetupForcedMpOptions` parallel to `s_SetupMpCreatedWeaponCount`).
- NOT mutate the persistent `g_MatchConfig.options` directly. Either
  a transient overlay (`g_MpSetup.options |= forced_bits` only, with
  cleanup at match end) OR a clear restore-to-user-original path on
  match-end / mode-transition.
- Surface the override to the user via a one-shot HUD / log line so
  the user knows the engine adjusted their setting.

Affected sites:
- [src/game/setup.c:2745-2746](../../src/game/setup.c) (CHANGE).
- [src/game/setup.c:2746](../../src/game/setup.c) the parallel mutation of
  `g_MpSetup.options` -- decide whether this is the "transient overlay"
  layer (and `g_MatchConfig.options` stays user-clean) or whether a new
  `g_MpSetupOverlay.options` is introduced.
- Match-end / match-cancel paths: ensure engine-forced bits are
  unwound. Affected: [port/src/net/netmsg.c::netmsgSvcStageEndRead](../../port/src/net/netmsg.c)
  + [port/src/net/net.c::netDisconnect](../../port/src/net/net.c) +
  match-end paths in [src/game/menutick.c](../../src/game/menutick.c).
  Requires-design-call on the unwind point.

### INV-5. Defensive fallbacks for catalog misses do not silently substitute identity.

Defensive guards that handle catalog miss / OOB index by substituting a
default identity (e.g. body slot 0 = DJ Bond, head count default = 8)
MUST either:

- Reject and propagate the failure to the caller (return NULL, return
  early with sentinel, set a dirty-flag that the caller checks), OR
- Substitute AND log `LOG_WARNING` AT EVERY MISS (not gated by `if
  (g_Vars.currentplayernum == 0)` or rate limits) so the substitution
  is visible in the player's own log.

Today, defensive substitutions are silent or single-fire-logged; players
who hit the substitution path can spend a session wondering why their
chr is DJ Bond.

Affected sites:
- [src/game/body.c:177-181](../../src/game/body.c) -- bodynum=0 silent
  substitution. Decision: either reject (return NULL model, caller
  shows "asset missing" placeholder OR forces a known-good fallback
  body with explicit log) OR keep substitution but add per-spawn log.
  (CHANGE)
- [src/game/bodyreset.c:58-64](../../src/game/bodyreset.c) -- stage_id
  strcmp literal chain. Replace with a catalog-driven map or assert /
  log on miss. (CHANGE)
- [src/game/bot.c:506-549](../../src/game/bot.c) -- missing per-spawn
  `modelmgrLoadProjectileModeldefs(resolvedWeaponNum)`. Add to mirror
  player.c:1822. Cost: cheap (idempotent no-op if already loaded).
  (CHANGE)
- [src/game/bodyreset.c:40-45](../../src/game/bodyreset.c) -- the
  `g_Vars.normmplayerisrunning` early-return skips the
  `var80062c80 = rngRandom() % g_NumBondBodies` init. Audit whether
  any code path reads `var80062c80` in normmplayerisrunning mode; if
  yes, this is a parallel uninit class (INV-3 territory but caught
  by INV-5's "no silent skip of identity-relevant init" framing).

---

## 3. Implementation cohorts

Five cohorts. Each is one invariant or one tightly-related cluster.
Each commit within a cohort is build-verifiable and bisectable. All new
pd-tests cases land in the same commit as the invariant they enforce.

Recommended order: **A then B then C then E then D**. Rationale: A
sets the catalog API style that subsequent cohorts use; B is the
smallest and addresses the live B-219 narrative most directly; C is the
highest-priority hardening per the LIVE-2 audit ranking; E is medium
and depends on neither A nor B/C; D is the most subjective UX call
(Mike may change Option A/B/C selection mid-cohort).

### Cohort A -- INV-1 catalog accessor loud-fail

Commits (5):

- A.1 (NEW): add `_checked` accessor variants in
  [port/src/assetcatalog_api.c](../../port/src/assetcatalog_api.c)
  for the spawn-critical surface. Signatures:
  ```
  bool catalogGetMpWeaponNumChecked(s32 idx, s32 *out_weaponnum);
  bool catalogGetMpWeaponPriAmmoTypeChecked(s32 idx, s32 *out_type);
  bool catalogGetMpWeaponPriAmmoQtyChecked(s32 idx, s32 *out_qty);
  bool catalogGetBodyScaleChecked(s32 bodynum, f32 *out_scale);
  bool catalogGetBodyAnimScaleChecked(s32 bodynum, f32 *out_scale);
  bool catalogGetBodyHandFilenumChecked(s32 bodynum, s32 *out_filenum);
  bool catalogGetBodyModeldefChecked(s32 bodynum, struct modeldef **out_md);
  bool catalogGetHeadModeldefChecked(s32 headnum, struct modeldef **out_md);
  bool catalogGetStageResultByIndexChecked(s32 stageindex, catalog_stage_result_t *out);
  ```
  Each returns `false` on miss + writes `0` / `1.0f` / `NULL` to
  `*out` + emits `LOG_WARNING "CATALOG.MISS: <accessor> idx=<n> reason=<oob|unpopulated|empty>"`
  rate-limited via a per-accessor static last-warned-frame.
  Build-verify: link the additive functions; no caller change yet.
- A.2 (CHANGE): migrate `playerSpawn` to checked accessors at
  [src/game/player.c:1799-1828](../../src/game/player.c). On any
  miss, force-equip `WEAPON_UNARMED` instead of falling through to
  `g_DefaultWeapons[0]` (which is itself `0` in normal MP). Add
  one-shot HUD line "weapon load failed -- spawning unarmed".
  Build-verify.
- A.3 (CHANGE): migrate `botSpawn` to checked accessors at
  [src/game/bot.c:517-535](../../src/game/bot.c). On miss, log + skip
  bot weapon equip (bot stays unarmed for the spawn; fine for bot AI
  which has its own weapon-acquisition logic). Build-verify.
- A.4 (CHANGE): migrate `body0f02ce8c` to checked accessors at
  [src/game/body.c:183-184](../../src/game/body.c). On scale miss,
  substitute `1.0f` AND log a per-spawn `CATALOG.BODY.MISS` line so
  the silent substitution becomes visible. Build-verify.
- A.5 (TEST): pd-tests cases `tests/test_catalog_accessors.cpp`
  exercising checked-vs-legacy variants on stubbed `g_MpWeapons[]`
  and `g_HeadsAndBodies[]`. Counts: ~10 cases / 50 assertions.
  Tags: `[catalog][accessor][regression]`.

### Cohort B -- INV-2 symmetric MP-mode guards

Commits (3):

- B.1 (CHANGE): re-add the `g_Vars.normmplayerisrunning &&` outer guard
  at [src/game/player.c:1790](../../src/game/player.c) and
  [src/game/bot.c:506](../../src/game/bot.c). Comment cross-reference to
  playerreset.c:225 explaining the symmetric-pair relationship. Update
  [src/game/setup.c:2745-2746](../../src/game/setup.c) B-181 fallback
  to also gate the OR-set on `normmplayerisrunning` so the bit cannot
  leak into Co-Op / Counter-Op. Build-verify.
- B.2 (CHANGE): extract the spawn-with-weapon predicate into a pure
  helper `bool spawnWithWeaponShouldApply(bool normmplayer, u32 mp_options)`
  in `src/include/game/playerreset.h` so the predicate has a single
  source of truth. Wire all three callers (player.c, bot.c,
  playerreset.c gate) through it. Build-verify.
- B.3 (TEST): pd-tests cases `tests/test_spawn_with_weapon_predicate.cpp`
  for the extracted predicate + the mutual-exclusion invariant
  (asserting that for any (normmplayer, options) input, AT MOST one
  of {INTROCMD_WEAPON path, spawn-with-weapon path} fires). Counts:
  ~6 cases / 30 assertions. Tags: `[spawn][weapon][predicate][regression]`.

### Cohort C -- INV-3 ROM-gate uninit class audit + fix

Commits (3):

- C.1 (DOC): write
  `context/audits/rom-gate-uninit-fields-2026-04-26.md` enumerating
  every `#if VERSION >=` field-init block in upstream HEAD
  (`bed3bf52`) for `struct player`, `struct gunctrl`, `struct hand`,
  `struct chrdata`. For each, record the upstream gate condition and
  the PD2 init site OR `MISSING` if no replacement. Drives the
  remaining cohort commits.
- C.2 (CHANGE): apply unconditional inits for every `MISSING` field
  identified in C.1. Concentrated in
  [src/game/playermgr.c](../../src/game/playermgr.c) /
  [src/game/bondgunreset.c](../../src/game/bondgunreset.c) /
  [src/game/chr.c](../../src/game/chr.c). Each addition follows the
  visionmode / handmodeldef pattern: inline init with one comment
  line citing the audit. Expected: 5-15 one-line additions.
  Build-verify per file.
- C.3 (TEST): pd-tests case `tests/test_player_struct_init.cpp` using
  a poisoned-byte allocator stub (`0xCC` fill) + extracted pure
  init-helper functions (`playermgrInitGunctrlDefaults` etc., refactored
  from playermgr.c) that asserts every audit-flagged field has been
  written-through to a non-`0xCC` value. Counts: ~5 cases / 30
  assertions. Tags: `[player][init][rom-gate][regression]`.

  Note: this test requires a small refactor (extract init-helper
  pure functions). If the refactor balloons, fall back to the doc
  audit alone for this round and queue the runtime validator for a
  follow-up. Decision-for-Mike below.

### Cohort D -- INV-4 user MP options preservation

Commits (3):

- D.1 (CHANGE): introduce `g_MatchConfig.options_engine_forced` u32
  (or equivalent overlay variable per Mike's preference). The B-181
  fallback at [src/game/setup.c:2745-2746](../../src/game/setup.c)
  records the forced bits in this mask BEFORE OR-ing into options.
  Consumers (`g_MpSetup.options` propagation, save) read the
  user-clean `g_MatchConfig.options & ~options_engine_forced` for
  user-facing display, and the full `g_MatchConfig.options |
  options_engine_forced` for the engine. Build-verify.
- D.2 (CHANGE): match-end / match-cancel paths in
  [port/src/net/netmsg.c](../../port/src/net/netmsg.c) +
  [port/src/net/net.c](../../port/src/net/net.c) +
  [src/game/menutick.c](../../src/game/menutick.c) clear
  `g_MatchConfig.options_engine_forced` AND clear the corresponding
  bits from `g_MatchConfig.options` so the next match starts with
  user-original options. Add a one-shot HUD or log line on match
  start when `options_engine_forced` is non-zero so the user knows.
  Build-verify.
- D.3 (TEST): pd-tests case `tests/test_user_options_preservation.cpp`
  asserting (a) B-181 fallback marks the bit forced, (b) match-end
  unwinds the forced bit, (c) the user-facing query returns the
  original mask. Counts: ~4 cases / 20 assertions. Tags:
  `[options][user][forced][regression]`.

### Cohort E -- INV-5 defensive fallback identity

Commits (4):

- E.1 (CHANGE): [src/game/body.c:177-181](../../src/game/body.c)
  bodynum-OOB substitution. Replace silent clamp-to-0 with: log
  `LOG_ERROR "BODY.IDENTITY: bodynum=%d OOB, substituting bodynum=0
  (DJ Bond) -- chr will appear as wrong character"` + add a one-shot
  HUD message on the affected player's screen. Caller chrSetup is
  unchanged; the substitution still happens, but the player sees it.
  Build-verify.
- E.2 (CHANGE): [src/game/bodyreset.c:58-64](../../src/game/bodyreset.c)
  stage_id strcmp chain. Replace with a small lookup table
  `s_HeadCountByStageId[]` of `(catalog_id, headcount)` pairs OR a
  catalog-driven query (preferred). On miss, log
  `LOG_WARNING "BODIES.HEADCOUNT: stage_id='%s' not in override map,
  defaulting to 8 -- guards may use unintended head set"`. Build-verify.
- E.3 (CHANGE): [src/game/bot.c:506-549](../../src/game/bot.c) add
  `modelmgrLoadProjectileModeldefs(resolvedWeaponNum)` immediately
  before `botinvSwitchToWeapon` (mirror player.c:1822 pattern). Cost:
  cheap, idempotent. Build-verify.
- E.4 (TEST): pd-tests cases:
  - `tests/test_body_identity_substitution.cpp` asserting
    body0f02ce8c bodynum-OOB returns the substitution AND emits
    the WARNING log via a captured-log stub.
  - `tests/test_bodies_headcount_lookup.cpp` asserting the
    catalog-driven head count returns correct values for known
    stage_ids and logs WARNING on miss.
  - `tests/test_bot_spawn_modeldef_preload.cpp` -- if extractable
    as pure (likely needs stubs), assert the per-spawn preload
    fires before the equip. If not extractable, omit and rely on
    code review.
  Counts: ~6 cases / 30 assertions. Tags:
  `[body][identity][fallback][regression]` etc.

---

## 4. Test coverage shape

| Cohort | Invariant | New test files | New cases | New asserts |
|---|---|---|---|---|
| A | INV-1 catalog loud-fail | `test_catalog_accessors.cpp` | ~10 | ~50 |
| B | INV-2 symmetric guards | `test_spawn_with_weapon_predicate.cpp` | ~6 | ~30 |
| C | INV-3 ROM-gate uninit | `test_player_struct_init.cpp` | ~5 | ~30 |
| D | INV-4 user options | `test_user_options_preservation.cpp` | ~4 | ~20 |
| E | INV-5 defensive identity | `test_body_identity_substitution.cpp` + `test_bodies_headcount_lookup.cpp` (+/- bot one) | ~6 | ~30 |
| **Total** | | **5-6 files** | **~31 cases** | **~160 assertions** |

Baseline at HEAD: 83 cases / 1389 assertions. Post-cohort target:
~114 cases / ~1549 assertions.

Following the existing patterns from `tests/test_bondgun_cache.cpp`
+ `tests/test_savebuffer.cpp` + `tests/test_manifest.cpp`: each
TEST_CASE has a hierarchical tag (`[subsystem][feature][regression]`),
the test file has a header comment citing the bug shape it locks down,
and pure functions are extracted into either `port/src/<name>_pure.c`
files or test-local copies in `tests/<name>_pure.{c,h}` per the
established source-vs-copy split.

---

## 5. Stop conditions / decisions for Mike

These genuinely require Mike's call before implementation begins.
The doc proceeds with my recommended option marked, but Mike can
override at any of these decision points.

### D-1. INV-2 resolution: Option A vs B vs C (Section 2 INV-2)

**Recommended: Option A** (re-add `normmplayerisrunning &&` guard at
player.c:1790 + bot.c:506). Smallest behavioral footprint, restores
upstream invariant, matches playerreset.c:225 author's intent per
its comment.

**Alternative B** (force-clear bit on Co-Op / Counter-Op entry):
slightly more thorough -- guarantees the bit is never set when the
gates are off. But requires identifying every entry site which adds
risk.

**Alternative C** (extend playerreset.c:225 gate to drop normmplayer
guard): makes spawn-with-weapon win in Co-Op / Counter-Op. User-facing
behavior change. NOT recommended without UX product call.

### D-2. INV-1 fallback policy: WEAPON_UNARMED vs original behavior (Cohort A.2)

Today, the spawn-with-weapon failure path falls through to
`g_DefaultWeapons[]` which in normal MP is `0` (because INTROCMD_WEAPON
was skipped). Net: unarmed spawn.

**Recommended**: explicit `bgunEquipWeapon2(HAND_RIGHT, WEAPON_UNARMED)`
+ HUD line "weapon load failed -- spawning unarmed". Same observable
behavior as today but explicit and visible.

**Alternative**: raise to a fatal-config error and refuse to spawn
(player respawn loops on the error message). Higher player-impact, but
catches catalog regressions before they hit live play.

### D-3. INV-3 test approach: doc-only vs runtime validator (Cohort C.3)

**Recommended**: doc audit C.1 + the limited extract-pure-helpers
test C.3. Doc is authoritative; test catches regressions on the
specific helpers we extract.

**Alternative**: full runtime `playermgrValidatePlayerInit()` walker
that checks every byte of the struct against a poisoned-allocator
sentinel. Larger refactor (need to extract every init helper), more
robust. Decide based on how deep C.1 finds the uninit class to be.

### D-4. INV-4 implementation: separate field vs overlay variable (Cohort D.1)

**Recommended**: add `g_MatchConfig.options_engine_forced` field. One
location, easy to reason about. Save migration: ignored on read
(it's transient state, never persisted to save file).

**Alternative**: introduce `g_MpSetupOverlay` struct that engine code
mutates instead of `g_MpSetup` directly. More invasive, more
architecturally satisfying. May be too much surface for this audit
pass; queue as a separate D5R refactor if Mike wants it.

### D-5. Cohort scope decision: ship 5 or split

Five cohorts feels right per Mike's stop-condition framing
(`~6 cohorts`). If any cohort grows in execution beyond ~5 commits,
surface to Mike before pushing the cohort to merge.

If Cohort C's audit C.1 finds a very large uninit class
(say, >25 fields), split C into C1+C2+C3 by struct type and Mike can
prioritize.

If Cohort A's checked-accessor surface ripples into more callers than
the audited spawn path (likely, given how many places use the legacy
accessors), defer the broader migration to a follow-up cohort F that
ships post-greenlight. Cohort A's spawn-path migration is sufficient
for the live B-219 narrative.

---

## 6. Out-of-scope but worth surveillance

These are findings from the audits that the cohorts do not address.
Listed for the active surveillance log so they are not forgotten.

- Sonnet H-2 `gunmodeldef` not cleared in `bgunEnterFlux` -- speculative,
  no observed bug. Worth a propagation-grep follow-up if a stale-FP-model
  bug ever surfaces.
- Sonnet H-4 netPlayersAllocate ordering -- LOW plausibility per audit;
  if any net-init crash ever points back to this site, revisit.
- Opus H-7 chrInit NULL chr propagation -- silent-fail vs crash trade.
  Already mitigated; leave alone.
- Opus H-9 bot per-bot loadout asymmetry -- benign today; revisit if
  per-bot loadouts ever ship.
- Catalog dependency-graph traversal correctness (mentioned in pd-tests
  README "Coverage queued"): orthogonal to this doc's invariants but
  same risk class. Future pd-tests cohort.

---

## 7. Decision summary for Mike

1. Greenlight or hold the cohort plan (Sections 3 + 4).
2. Pick INV-2 resolution: Option A / B / C (Section 5 D-1).
3. Pick INV-1 fallback policy: WEAPON_UNARMED-with-log / fatal-error
   (Section 5 D-2).
4. Pick INV-3 test approach: doc-only / extract-pure-helpers /
   runtime-validator (Section 5 D-3).
5. Pick INV-4 implementation: separate-field / overlay-struct
   (Section 5 D-4).
6. Confirm cohort scope: ship 5 sequential / split if C balloons /
   defer A's broader migration (Section 5 D-5).

After greenlight, implementation proceeds A then B then C then E
then D, sequential bisectable commits, build-verify per step, all
new pd-tests cases land in the same commit as the invariant they
enforce. Post-cohort merges follow standard auto-merge protocol per
the user's `auto-merge-by-default-sequentially` memory.

---

## 8. Decisions logged 2026-04-26 by parent session per delegated authority

Parent session greenlit Phase 2 with all five recommendations accepted.
Logged here for the implementation session's reference and future
audit traceability.

- **D-1 (INV-2 resolution): Option A.** Re-add `g_Vars.normmplayerisrunning &&`
  outer guard at [src/game/player.c:1790](../../src/game/player.c) and
  [src/game/bot.c:506](../../src/game/bot.c) to match the existing
  [src/game/playerreset.c:225](../../src/game/playerreset.c) gate.
  Cohort B implementation proceeds with this option.
- **D-2 (INV-1 fallback policy): explicit `WEAPON_UNARMED` + HUD message
  on catalog miss.** Loud failure, not silent. Cohort A.2 migration
  emits a `LOG_ERROR "SPAWN.CATALOG.MISS: ..."` line + a one-shot
  player-screen HUD message + force-equips `WEAPON_UNARMED` instead of
  falling through to `g_DefaultWeapons[]` (which is `0` in normal MP).
- **D-3 (INV-3 test approach): doc audit + extract-pure-helpers.** Audit
  C.1 produces the field checklist. Pure-helper extraction in C.3 covers
  the testable subset; depth follows what the audit surfaces. If C.1
  finds >25 fields, split the cohort and resurface to parent before
  pushing.
- **D-4 (INV-4 implementation): separate `g_MatchConfig.options_engine_forced`
  field.** Less invasive than the overlay-struct alternative. The full
  overlay-struct refactor (D5R territory) is queued for a future pass.
- **D-5 (cohort scope): 5 cohorts sequential.** Surface to parent if any
  single cohort exceeds 5 commits OR if Cohort C scope balloons beyond
  expectations. Methodology gates remain in effect: possibility framing,
  no half measures (every site in scope migrates per its invariant), no
  em-dashes anywhere, hierarchical log channels for any new diag.
  pd-tests cases land in the same commit as the invariant they enforce.

---

## 9. T-ENGINE-004 closure architecture, 2026-08-12

The original five cohorts are historical foundation, not current completion
proof. Cohorts A, B, C, and the narrow E guards remain valid. Cohort D added an
option ownership field, but the full ownership lifecycle is incomplete. Current
source audits recorded the remaining production defects as B-1064 through
B-1067 and B-1069.

No additional product choice is required. The closure follows the active
catalog-ID, public-source, server-authority, and listen-host constraints.

### 9.1 Transaction rule

Player initialization is a sequence of explicit fallible phases. Each phase
must do one of two things:

1. prepare and validate all candidate state, then commit without another
   fallible lookup or allocation; or
2. reject with a typed error and leave the phase's published state unchanged.

If a later stage phase cannot restore the prior live stage because the stage
pool has already been reset, rollback means clearing every candidate pointer and
relationship, disconnecting an invalid network start when applicable, and
routing through a clean title-stage reset. Abandoned stage-pool bytes are
reclaimed by the next `MEMPOOL_STAGE` reset and must never remain reachable.

The production log contract is:

```text
PLAYER.INIT.PREFLIGHT phase=<match|allocation|network|identity|chrbody|stage>
PLAYER.INIT.COMMIT phase=<match|allocation|network|identity|chrbody|stage>
PLAYER.INIT.ROLLBACK phase=<...> reason=<typed reason> player=<n> id=<catalog id>
```

### 9.2 Exact typed identity plan

One shared `player_identity_plan` resolves body and head catalog IDs and records:

- the exact canonical body and head IDs;
- the private runtime body and head indices;
- optional MP selector indices as derived compatibility caches only; and
- a typed failure reason for empty ID, missing entry, wrong type, unbound runtime
  index, or inconsistent runtime binding.

A nonempty typed ID never falls through to `mpbodynum`, `mpheadnum`, slot zero,
`BODY_DARK_COMBAT`, or `HEAD_DARK_COMBAT`. Defaults are allowed only when an
input boundary deliberately writes the default catalog IDs before preparation.
Once preparation begins, a miss rejects the candidate.

The plan is used by Match Setup, player character selection, network player
allocation, client stage-start application, and preserved-player reconnect.
Legacy numeric fields may be updated after a valid plan commits, but they never
select identity.

### 9.3 Match preparation and commit

`matchStart` first builds a local plan containing the exact stage, scenario,
weapon references, spawn-weapon result, participant descriptors, player config
candidates, bot config candidates, and bot-profile bindings. It rejects:

- invalid or missing typed stage/scenario/weapon/body/head/profile IDs;
- unsupported slot types, duplicate/overflowed participant destinations, or no
  player participant; and
- a custom weapon that has no valid runtime Match Setup binding.

Only after the full plan succeeds may it restore engine-forced options, write
`g_MpSetup`, replace configs, clear/rebuild the participant pool, set role
globals, free solo ROM state, or close the Room UI. The solo Room remains open
when preparation fails.

### 9.4 Player allocation and network linkage

`playermgrAllocatePlayers` validates count and roles, allocates and initializes
every private `struct player` candidate, then asks networking to validate and
bind only that candidate array. `g_Vars.players[]`, current-player pointers, and
Bond/Co-Op/Counter-Op pointers are published only after candidate allocation and
network planning both succeed. Network config/client links may commit first only
because no fallible work remains between that private-candidate commit and the
synchronous global-player publication.

`netPlayersAllocate` becomes prepare plus commit. Preparation assigns unique
player numbers and exact identities into temporary configs. Commit performs the
client-side number swap, config copies, and client/player links without further
catalog lookup. The unused remote-config backup array is removed rather than
retained as an uncalled rollback promise.

Allocation failure returns a typed status to the stage loop. The stage loop logs
rollback, disconnects an invalid network stage, clears all player links, and
re-enters the title stage through a fresh pool reset.

### 9.5 Client stage-start and reconnect

`SVC_STAGE_START` must parse and validate into local candidate state before it
changes ticks, RNG seeds, game mode, mission/match setup, participants, client
states, player configs, bot configs, or menu/stage state. Every body, head,
weapon, profile, scenario, and stage reference is type-checked. A malformed or
missing reference rejects the entire message with no partial publication.

Preserved-player reconnect prepares the exact identity and full config copy
before it changes player number, team, score, config/player links, state, respawn
flags, or the preserved-slot count. Failure keeps the preserved slot active and
rejects the reconnect as a file/catalog mismatch. Fresh mid-match joins remain
explicitly rejected; true drop-in is not part of this task.

### 9.6 Character-body commit and rollback

`playerTickChrBody` does not set `haschrbody`, `model00d4`, or multiplayer chr
pointer tables before success. It first resolves exact identity, source handles,
body/head model definitions, and the optional weapon model. Character, held
weapon, and fireslot attachment then run inside one reversible synchronous
boundary. Only the final nonfallible block publishes:

- `model00d4` and `haschrbody`;
- `g_MpAllChrPtrs` and `g_MpAllChrConfigPtrs`;
- eye/head height and final root/look state.

Any late failure removes the attached character and its held weapon/fireslot,
restores the reusable prop's type, position, room list, room registration, and
pre-existing chr slot, frees an uncommitted multiplayer model, resets
multiplayer chr-pointer slots, releases newly acquired one-player gun memory,
clears `gunmem2`, leaves `haschrbody` false, and emits a typed
`PLAYER.INIT.ROLLBACK`. A chr slot allocated by the attempt is retired instead
of retained. Multiplayer never retries with Dark Combat after an exact identity
or model failure.

Runtime identity preparation accepts `headnum=-1` only when the exact
reverse-resolved body is marked complete by the catalog; modular bodies still
require an exact typed head. Eyespy allocation owns its model and private prop
directly so prop-slot or chr-slot failure cannot strand either candidate. Its
weapon-facing state and player link publish only after Eyespy and player-prop
candidates both exist. Because `propAllocate` initially accounts an unpublished
slot as a generic prop, every pre-tick Eyespy rollback restores that type before
`propFree`; failure cleanup therefore preserves scheduler ownership as well as
memory and pointer ownership.

Chr target identity follows the same transaction boundary. The compatibility
`chrInit` path may inherit only an active, enabled current-stage player prop.
`chrInitWithTargetProp` and `chrSetTargetProp` validate an explicit prop-domain
candidate before converting it to an index. A private player chr targets its own
candidate prop; an Eyespy stays at the unresolved `-2` sentinel until that
player candidate exists, then binds before either global pointer publishes.
No pre-connect or stage-reset constructor may subtract a retained prior-stage
player pointer from the newly allocated prop pool.

`playermgrReset` invalidates every player, current-player, and role pointer before
the stage arena is released. `chrGetTargetProp` treats every unresolved or
out-of-domain target as `NULL`; it never exposes the `-2` sentinel as an array
index. The normal compatibility constructor may inherit only a prop that is
active, enabled, on the active scheduler list, and already owned by a published
player.

### 9.7 Stage-reuse reset contract

One narrow stage-transient helper explicitly re-establishes
`weaponnum = WEAPON_NONE`, `prevweaponnum = -1`,
`prevwasdualwielding = false`, `wantammo = false`, `passivemode = false`,
`wantsjump = false`, and `jumpconsumed = true`. Fresh player allocation and
`bgunReset` both call it; ordinary respawn deliberately does not.

No blanket `memset` is permitted for `struct player`, `struct gunctrl`, or
`struct hand`. Network relationship fields, queued-load ownership, and the
aggregate hand defaults retain their existing intentional lifecycle.

### 9.8 Engine-forced option ownership

`matchOptionsForceBit` records only bits that were off in the user view and were
actually added by the engine. User-facing reads use `matchOptionsUserView`.
Match end, cancel, disconnect, settings replacement, and next-match start all
call one restore or rebase helper so a user-owned bit is never cleared and an
engine-owned bit never leaks into the next match.

### 9.9 Network launch freeze

For network starts, lobby preparation and ready-gate compaction own every
mutable match decision. `SVC_STAGE_START` validation and serialization freeze
the exact stage, scenario, options, weapons, spawn result, RNG, players, teams,
handicaps, bot identities/profiles/teams, and participant mask. Network
`mpStartMatch` consumes those values without participant rebuild, random weapon
application, quick-team generation, profile-local unlock filtering, handicap
normalization, or a fallible stage reverse lookup. Those setup conveniences are
offline-only and run before an offline stage transition.

The v55 lobby boundary has one settings transaction and one handicap owner.
Before a match-scoped session catalog exists, `CLC_SETTINGS` carries exact typed
body/head IDs, team, nonzero handicap, options, FOV, zoom multiplier, and name.
The pure prepare/codec boundary validates the complete candidate before writing
bytes or publishing a server cache. `CLC_LOBBY_START` carries no positional
four-player handicap cache; the server prepares each roster entry from that
exact authenticated client's settings. `SVC_STAGE_START` then carries the final
compacted authoritative roster. Map and arena IDs resolve through an explicit
expected catalog type, so campaign and Combat Simulator can never substitute
across a shared stagenum.

### 9.10 Verification and propagation gates

Focused tests must behaviorally cover exact identity preparation, settings-wire
roundtrip and no-mutation rejection, missing/wrong type/unbound indices,
match-plan no-mutation rejection, player allocation
failure, reconnect preservation on rejection, client stage-start parse before
commit, chrbody rollback, option ownership, stage-reuse defaults, and the live
spawn-predicate call-site wiring.

Source-frozen production verification requires:

- full `pd-tests` after the focused selectors;
- ordinary Campaign and the full 17-mission/restart regression;
- ordinary Combat Simulator start, shots, end, return, and repeat start;
- the Air Base to Air Force One transition canary;
- listen-host and ordinary client match start;
- one two-process disconnect/reconnect receipt preserving player number, team,
  score, cookie, body/head identity, and one chr/prop; and
- retain both already-accepted D-003 authority-first friend-play receipts rather
  than rerunning them; keep V-009 Needler obstruction checks in any new gameplay
  captures.

Every queued receipt is rejected if relevant source changes or the exclusive
resource overlaps before completion.

## 10. Source-connected implementation status (2026-08-23, mixed verification)

The current working tree implements the remaining Campaign/player-init source
boundary as one coherent unit:

- `playermgrAllocatePlayers` constructs private candidates; `netPlayersAllocate`
  consumes that explicit array and never reads `g_Vars.players[]` during prepare.
- `playerIdentityPrepareRuntime` round-trips legacy runtime selectors through
  canonical typed IDs, accepts a missing head only for an exact catalog-complete
  body, and `playerChrBodyPreflight` proves required body/head source and model
  readiness before player-prop allocation.
- `playerReset`, `playerSpawn`, `playerTickChrBody`, and `lvReset` return typed
  outcomes. Both main loops disconnect/reset and return to a fresh title-stage
  load when stage initialization rejects.
- Character-body weapon-model load is complete before chr attachment. Weapon
  attachment and fireslot allocation are mandatory, checked steps; rollback
  removes every relationship they could have published and restores the exact
  reusable prop position, rooms, registration, type, and prior chr-slot owner.
- Eyespy model, prop, and state candidates have explicit cleanup; pre-tick
  cleanup restores generic prop-counter ownership before freeing, and player
  state publishes once after the Eyespy and player-prop candidates all succeed.
- Player and Eyespy chr targets use explicit validated candidate props; the
  legacy constructor inherits only a scheduler-published current-stage player.
- `playerInitStageTransientDefaults` unifies fresh and reused stage defaults
  without clearing transport relationships or respawn-preserved state.

The first source-frozen verification batch passed focused 1,769/25, full
64,427/1,173, the native-source guard, 2,713/400-file no-overlap manifests, Air
Base 25/25, and the complete Campaign plus restart 23/23. The retained batch's
Combat Simulator process stalled after first-cycle CMP150 readiness and is not
accepted.

B-1101 live sampling subsequently placed 16/16 main-thread samples in
`modelasmReadFrameData`. The legacy decoder bounded frame data with the unrelated
animation-header end and could enter a zero-progress `while (v1 > gp)` loop.
Current source removes the unbounded bit-reader API and gives optimized and
generic transforms, `ANIMFIELD_08` root motion, and camera values one bounded
descriptor plus `bytesperframe` contract. It validates metadata, flags, header
length, field widths, payload span, and optimized part capacity; a valid
zero-width/zero-byte field is a no-op, malformed data emits a typed reason and
renders bind pose, and an allocated `animnum=0` object follows the normal bind
pose without a false rejection. Behavioral fixtures cover legacy, F32,
08-plus-camera, conflict, truncation, and exact-boundary layouts.

The final pure `anim_bits.h` boundary and explicit target ownership pass one
isolated source-frozen build, focused 2,007 assertions/33 cases, full
64,681/1,181, and the native-source guard. Product `9592a63d...` (2,715 files),
verifier `c76aadc8...` (401 files), client `A0F46D54...`, and tests
`B4D51B51...` remain unchanged with zero manifest mismatches. T-ENGINE-004 and
B-1064/B-1066/B-1101 remain partial/confirmed only for the replacement Combat
Simulator smoke. B-801 refused the first attempt before launch at 1,885 MiB free
commit below its 2,048 MiB floor; a later read-only probe found 1,604 MiB. No
bypass or runtime claim was made.

## 11. B-1102 exact public-animation framing and independent bind pose

The first exact B-1101 replacement smoke is retained and rejected at 16/56.
It proves the bounded decoder escaped the former zero-progress loop and then
correctly rejected a malformed generated descriptor stream. Schema-v5 public
source stores each part as `flags`, exact `header_hex`, and frame-major raw
field values. The prior compiler treated `flags` as out-of-band metadata: it
summed/copied only `header_hex`, so the private runtime cache lost one required
discriminant per part while retaining the expected frame width. Runtime then
read field-header bytes as flags. The rejection path compounded that producer
error by clearing `model->anim` before invoking generic CHRINFO matrix code that
requires it.

The source-connected repair uses one shared format contract rather than a
fixture-specific patch:

- `animFrameMeasurePartBounded` owns legal flag combinations, exact descriptor
  bytes, field widths, and frame-bit accounting. The complete-stream helper
  consumes one flags byte plus that exact descriptor for every declared part
  and rejects any unconsumed trailing descriptor bytes.
- Schema-v5 parsing is strict and fail-closed. It admits only the known schema,
  canonical range-checked JSON integer/boolean tokens, contiguous part indices,
  coherent declared topology/frame counts, exact row shapes, and the complete
  unsigned 32-bit value domain needed by raw S32/F32 fields. Explicit zero-frame
  placeholders retain both their declared part topology and semantic one-frame
  no-op boundary instead of pretending to carry captured frames.
- Native reconstruction reserves and emits every flags byte before
  `header_hex`, validates the completed descriptor stream, verifies every frame
  packs exactly the measured bits, and refuses lossy GLTF-channel fallback once
  schema-v5 ownership is present.
- Animation cache version 8 invalidates malformed v7 animation products while
  leaving unrelated mesh version 7 and modeldef version 9 caches untouched.
- The optimized runtime reader records the shared expected descriptor and bit
  endpoints and verifies its actual advancement after each part. Decode
  rejection re-enters the complete optimized matrix traversal with an explicit
  null animation input; its POSITION and CHRINFO bind-pose branches execute
  without reading or mutating the live `model->anim` object, and any residual
  translation branch uses a null-safe bind scale.

Pure layout tests contain the real 15-part
`base:animation_two_gun_hold` framing (`0x09` root part plus fourteen `0x01`
parts) and reject the same descriptors with omitted flags, undersized frame
capacity, or an extra required part. Static production-path contracts pin the
strict compiler, completed-stream validation, independent cache version, exact
consumer advancement, and ownership-preserving fallback. Production-linked
pure JSON tests additionally reject signed/unsigned overflow, leading-zero
aliases, numeric prefixes, and boolean prefixes. These contracts now pass an
isolated source-frozen client/updater/tests build, focused 2,057/27, full
65,073/1,186, the native-source guard, and unchanged 2,717/402-file manifests.
Exact client `133A55C6...` then passes the sole replacement two-cycle Combat
Simulator receipt 56/56 with real CMP150 fire/hit, both stats/award cycles, Play
Again, clean exit, zero decoder/crash rejection matches, and no leaked process.
B-1101/B-1102 are retained regression gates. Accepted Campaign, D-003, and
reconnect receipts were retained without rerun; broader T-ENGINE-004 lifecycle
closure remains active.

## 12. B-1067/B-1103/B-1104 prepared-roster and stage-player transaction closure

Current source closes both residual partial-publication boundaries without
introducing a second lobby or player-lifecycle model:

- After complete `CLC_SETTINGS` validation, the authority identifies whether
  the sender is the exact participant frozen in the active ready gate. If so,
  it aborts and restores that transaction before publishing the new settings
  in ordinary lobby state and recomputing team policy. Reconnect handling stays
  separate, malformed packets remain non-mutating, and the smoke trigger calls
  `netClientSettingsChanged` rather than a packet writer or raw transport send.
- `lvReset` records each successful player reset and treats all selected
  players as one stage transaction. Either a later reset failure or a spawn
  failure reverse-unwinds every committed player through the canonical Eyespy,
  chr/model, prop, gun-memory, and MP-binding owners. The teardown accepts both
  model-less reset commits and full chrbody commits and leaves disconnect/title
  recovery with no partially published stage player.
- One exact smoke-only `reset:<player>` / `spawn:<player>` seam is disabled in
  ordinary play, rejects malformed input, and consumes once. It enters the same
  production rollback branch as a real failure instead of duplicating cleanup.
- Mode-aware listen-host autostart reaches the ordinary ready gate for
  multiplayer, co-op, and Counter-Op. Counter-Op assigns the sole remote client
  as Anti and all modes start through `netLobbyRequestStartWithSims`.

Source-linked focused contracts and four bounded fixtures now exist for
positive ordinary co-op, positive ordinary Counter-Op, a preparing-client
settings change, and a player-1 spawn failure after player 0 commits. This is
an implemented but unverified checkpoint: no focused/full/guard result or
production receipt is accepted until the source is frozen and the consolidated
batch passes.

The first frozen runtime batch retained positive co-op and exact later-player
reverse-unwind evidence, but was rejected after Counter-Op crashed on a
model-incomplete NPC orientation. The bounded readiness/serialization correction
then passed frozen focused 816/13, full 65,898/1,198, and the native-source guard.
All four replacement fixtures reached their terminal assertions on exact client
`077FA41F...`, but that receipt is also rejected: Counter-Op logged 256 repeated
missing-syncid 208 resync rejections.

The second failure is not a wire-parser defect. Runtime player slots are
local-first, so the authority runs Bond then Anti while the client runs Anti then
Bond. Numeric player ordering assigns spawn pools 3 and 13 to opposite semantic
roles; `playerSpawnAnti` then removes body/head 94/10 on the authority and 110/45
on the client. The authority also emits NPC checksums/resyncs before the client's
real post-load acknowledgement. The remaining structural unit therefore builds
one immutable stage order from authenticated client IDs, uses it for reset,
spawn, human orchestration, and true reverse rollback, and arms a post-load
replication barrier that retains pending resync ownership until every relevant
peer sends `CLC_STAGE_READY`. The barrier owns release at `netEndFrame`; a
read-only query also gates the sole direct entity-state bypass, GPU-swarm
snapshots, while room/auth/distribution/music/social control traffic remains
available during loading. Disconnected obligations are removed without claiming
a post-load acknowledgement, and release telemetry records both ready and
departed masks. Production status remains unpromoted until one refrozen
four-fixture batch is free of checksum/resync/rejection storms.

The next exact-client batch proved that a zero wait mask still overloaded two
states: no stage existed in the Carrington lobby, or a real stage had released.
It also proved that a periodic checksum over mutable NPC fields had no shared
sample boundary even after a successful full resync. Protocol v58 therefore
replaces the barrier with explicit inactive/waiting/release/active phases and a
nonzero stage epoch carried by START and echoed by READY. The listen authority
owns an independent post-load latch. RELEASE appends one complete baseline,
including a canonical sync-ID-sorted digest immediately after the full NPC
resync it validates, to dedicated reliable packet storage. Pending ownership is
cleared only after the complete room-scoped packet queues; a missing, partial,
or failed packet leaves RELEASE intact for retry. The digest hashes the exact
serialized target and sentinel-terminated room list, not receiver-local target
fallbacks or unsent room-array tails. Only after that queue does the lifecycle
enter ACTIVE for incrementals.
The previous receipt and automation predate this wire change; at that checkpoint
v58 remained source-connected and unverified until the consolidated replacement
batch below.

That replacement automation is now accepted. Product aggregate `03a65922...`
across 2,719 files and final verifier aggregate `f5f9ba01...` across 408 files
remain unchanged through the run; exact client `5DA75BE6...` and final tests
`9E56A336...` come from the isolated queued build. The complete suite passes
66,421 assertions in 1,200 cases and the native-source guard passes. Historical
static expectations for the removed readiness boolean and old stage-send order
were corrected only in verifier source; rejected receipts are retained and the
product binary never changed. Current-v58 production receipts now pass co-op
96/96, Counter-Op 98/98, later-player rollback 60/60, settings rollback 43/43,
initiator authority 214/214, and reconnect 99/99. The reconnect verifier was
made deterministic through ordinary typed match configuration by selecting
`base:cyclone` and asserting actual weapon 11; its focused compiled contract
passes 63 assertions. The integrated invitee-authority fixture reaches 210/218
and proves the route/start/gameplay subset, but the execution desktop exposes
no foreground HWND for B-1085's independent fresh SDL focus witness. One
focus-independent invitee-route smoke therefore separated D-003 route proof
from that unchanged focus/visual gate. Its immutable raw receipt remains
rejected at 169/170 because the original epoch regex required two digits and
rejected valid epoch 1; corrected static coverage passes 108 assertions in 2
cases and separately hashed retained exact-client logs pass 170/170 without a
product change. The accepted route proves one invitee-elected in-client ENet
listen authority, one separately signed typed match-server route, exactly one
initiator join, no probe/relay endpoint handoff, epoch-correct baseline/ACTIVE
publication, stable gameplay, and clean exits. B-1104 is a regression gate;
T-ENGINE-004 remains active only for broader 1.0 base-game lifecycle closure.
