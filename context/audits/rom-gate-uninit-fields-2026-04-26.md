# ROM-gate uninit field audit (Cohort C, INV-3)

**Audit date:** 2026-04-27 (mystifying-hofstadter-deca99 worktree)
**Driven by:** [`context/designs/player-init-architectural-fixes-2026-04-26.md`](../designs/player-init-architectural-fixes-2026-04-26.md) Cohort C.1
**Upstream pin:** `bed3bf52d0d5095d112940b1327ed6c256e54ea8` (2026-04-25)
**PD2 pin:** `aa9c43b0` (post Cohort B)

## Method

INV-3 invariant: any struct field whose only initialization in upstream
lives inside a `#if VERSION >=` block higher than PD2's compile target
(VERSION_NTSC_FINAL = 2) must be initialized unconditionally in PD2,
because PD2's `mempAlloc(MEMPOOL_STAGE)` does not zero-fill and the
field would otherwise read stale heap garbage.

Two prior incidents established the class:

- `g_Vars.players[index]->visionmode` (gated `>= VERSION_JPN_FINAL` in
  upstream, fixed at [src/game/playermgr.c:413](../../src/game/playermgr.c)
  unconditional `= VISIONMODE_NORMAL`).
- `gunctrl.handmodeldef` and `gunctrl.cartmodeldef` (no upstream init
  at all; PD2 added defensive NULL inits per B-246 r7 at
  [src/game/playermgr.c:425-426](../../src/game/playermgr.c)).

This audit walks every `#if VERSION >= VERSION_JPN_FINAL` and `>= VERSION_PAL_FINAL`
gate in upstream's player-init files, plus does a strict per-function
write-set diff between upstream and PD2 for the four canonical init
functions, to enumerate any MISSING field that PD2 should add an
unconditional init for.

Files audited:

- `src/game/playermgr.c` -- struct player init
- `src/game/bondgunreset.c` -- struct hand + struct gunctrl init
- `src/game/playerreset.c` -- per-life init
- `src/game/chr.c::chrInit` -- struct chrdata init
- `src/game/player.c::playerLoadDefaults` -- per-spawn defaults
- `src/game/bondgun.c::bgunInitHandAnims` -- per-spawn hand-anim init
- `src/game/playerreset.c::playerInitEyespy` -- eyespy init
- `src/game/body.c::bodiesReset` (cross-checked but not in struct-init scope)

## Findings

### F.1 -- VERSION-gated field assignments in upstream init functions

Grep for `VERSION >= VERSION_JPN_FINAL` and `VERSION >= VERSION_PAL_FINAL`
across the audited files, filtered to lines containing `=` and a struct
field accessor:

| File | Line context | Field assigned | PD2 status |
|------|--------------|----------------|------------|
| [playermgr.c](../../src/game/playermgr.c) | `>= JPN_FINAL` | `g_Vars.players[index]->visionmode = VISIONMODE_NORMAL` | FIXED at PD2 line 413 unconditional |
| bondgun.c::bgun0f08bba0 | `>= JPN_FINAL` | local `unk24 = func->unk24` | not a struct field; out of scope |
| bondgun.c::various runtime sites | `>= JPN_FINAL` | `player->hands[handnum].gunroundsspent[index] = ...` | runtime computation in state machine, not init; out of scope |
| player.c | `>= JPN_FINAL` | `var800800f0jf = 0` (file-scope JPN-only var) | PD2 lacks the variable entirely; out of scope |
| body.c::bodyCalculateHeadOffset | `>= JPN_FINAL` | local `offset = 0` | not a struct field; out of scope |

Conclusion: the only struct-field init gated `>= JPN_FINAL` or `>= PAL_FINAL`
in any of the audited init functions is `visionmode`, already fixed.

### F.2 -- Strict write-set diff per init function

For each of the four canonical init functions, this audit produced the
sorted set of `<receiver>-><field> =` write expressions in upstream and
in PD2, then computed the symmetric difference.

#### playermgrAllocatePlayer

Upstream-only writes (potential MISSING in PD2): **none**.

PD2-only writes (PD2 strictly added):
- `g_Vars.players[index]->client = NULL` (net field)
- `g_Vars.players[index]->gunctrl.cartmodeldef = NULL` (B-246 r7 fix)
- `g_Vars.players[index]->gunctrl.handmodeldef = NULL` (B-246 r7 fix)
- `g_Vars.players[index]->isremote = false` (net field)
- `g_Vars.players[index]->jumpconsumed = true` (jump input)
- `g_Vars.players[index]->ucmd = ...` (net + jump)
- `g_Vars.players[index]->wantsjump = false` (jump input)
- targetset loop bound now `ARRAYCOUNT(...)` (semantic preserving)

#### chrInit

Upstream-only writes (potential MISSING in PD2): **none**.

PD2-only writes (PD2 strictly added):
- `chr->generation = ++s_ChrGenerationCounter` (FIX-A.2 stale-pointer
  detection)

#### playerLoadDefaults

Upstream-only writes (potential MISSING in PD2): **none**.

PD2-only writes: diagnostic-related; no behavioral additions in this
function (the spawn-with-weapon edits live in `playerSpawn`, not here).

#### bgunInitHandAnims

Upstream-only writes (potential MISSING in PD2): **none**.

PD2-only writes: only LOG.WPN.DIAG diagnostic READS (the diff appeared
in raw output because the LOG.WPN.DIAG line references hand fields,
but those are reads not writes).

#### playerInitEyespy

No `#if VERSION >=` gates in upstream. PD2 differences are macroized
literal (STAGEINDEX_*) substitutions. Out of uninit-class scope.

#### bondgunreset.c struct hand initializer

Upstream and PD2 use byte-equivalent positional struct hand
initializers. PD2 differences are limited to the IS4MB pool sizing
removal (no field-init impact).

### F.3 -- Class exhaustion conclusion

The systematic audit shows that the visionmode + handmodeldef +
cartmodeldef fixes already in PD2 HEAD are the **complete** PD2
response to the "ROM-gate uninit field" class for the audited init
functions.

No additional MISSING fields are surfaced by either:
- the `>= JPN_FINAL` / `>= PAL_FINAL` grep (F.1), nor
- the strict per-function write-set diff (F.2).

## Decision

Cohort C ships as **doc-only**. No code changes for INV-3 land in this
cohort because the systematic audit shows the class is already
exhausted at HEAD.

The Cohort C plan in the design doc anticipated 5-15 one-line
additions; the audit shows 0. Per the design doc D-3 + the parent
session's directive ("if C.1 finds a very large uninit class, split"),
the opposite outcome (0 fields) does not require Mike's re-approval to
proceed.

The pure-helpers extraction proposed in C.3 is also not landed: with
no MISSING fields to test, the extraction would be infrastructure
without a current customer. If a future runtime incident surfaces a
new uninit case (the way visionmode + handmodeldef did), this audit
doc is the starting point for adding the unconditional init + the
optional pure-helper extraction.

## Surveillance items (out of scope but worth flagging)

The audit confirmed that the following PD2 patterns are NOT in the
ROM-gate uninit class but are still worth keeping in mind:

- `mempAlloc(MEMPOOL_STAGE)` does not zero-fill. Any new struct added
  to PD2 whose fields are read before write is a latent uninit bug.
  Cohort C's audit method (per-function write-set comparison) is the
  template for catching these on future struct additions.
- `g_HeadsAndBodies[]` slots past the catalog-populated count read as
  BSS zeros. The catalog accessors today bounds-check on array
  dimension only, not on slot population. Cohort A's `_Checked`
  variants address this; non-checked accessors remain a hazard if
  used in spawn-critical paths added in the future.
- The `chr->goposhitcount = 0` init in upstream is gated `>= NTSC_1_0`.
  PD2 builds with `VERSION = NTSC_FINAL = 2`, which is `>= NTSC_1_0`,
  so the gate compiles in. No fix needed; flagged to confirm the
  compile-time math.

## References

- Design doc: [`context/designs/player-init-architectural-fixes-2026-04-26.md`](../designs/player-init-architectural-fixes-2026-04-26.md) Cohort C
- Decision: D-3 "doc audit + extract-pure-helpers" (parent session, 2026-04-26)
- Prior audits:
  - [`context/audits/player-init-comparison-upstream-2026-04-26.md`](player-init-comparison-upstream-2026-04-26.md) F-4 / I-2
  - [`context/audits/char-init-weapon-spawn-comparison-opus47-2026-04-26.md`](char-init-weapon-spawn-comparison-opus47-2026-04-26.md) Section G + H-3
  - [`context/audits/char-init-weapon-spawn-comparison-sonnet46-2026-04-26.md`](char-init-weapon-spawn-comparison-sonnet46-2026-04-26.md) Section H-2
- Upstream HEAD: `bed3bf52d0d5095d112940b1327ed6c256e54ea8`
- PD2 HEAD at audit: `aa9c43b0`
