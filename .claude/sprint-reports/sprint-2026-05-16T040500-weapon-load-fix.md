# Sprint 2026-05-16T04:05:00Z — B-328 weapon-load fix

## Mike's report

> "fix the weapon migration / loading / missing when loading in problem"

## Triage

### Symptom localised from log evidence

Walked `.claude/smoke-verify-install/pd-client.log` (the most recent installed run, 9.5 MB)
for any `WPN`/`weapon` signal. Key findings:

- `LOADER.UNIVERSAL.OK: kind=weapon scanned=86 registered=86 envelope_failures=0` — weapon
  pool fine.
- `LOADER.POOL.WEAPON.OK: active=1 weapons=86` — loader active.
- `LOADER.POOL.WEAPON.STORED:` x86 — every weapon stored.
- BUT `LOG.WPN.DIAG: bgunRender enter player=0 ... R(wpn=22 visible=0 inuse=1 state=5 sm=2)`
  with `gunmodeldef=0000000000000000 handmodeldef=0000000000000000` and `masterload=1`.
- Diagnostic `visibility-gate-fail` firing every ~120 ticks with `mode6=1 ... gunmemtype=0
  raw: hand_mode=6 gunmemowner=0 gunmemtype=0 gunmemnew=22 masterload=1`.

That `masterload=1` is **MASTERLOADSTATE_HANDS** (`#define MASTERLOADSTATE_HANDS 1`), not
LOADED (=4). The master loader transitioned FLUX -> HANDS once and never advanced. The
weapon was queued (`gunctrl_wpn=0 switchto=22` -> later `gunctrl_wpn=22`), the master
loader started running, but `bgunQueueModelLoad` never completed for the hand model file.

### Root cause walk

`bondgun.c::bgunTickMasterLoad` line 4393 reads:
```c
s32 hf32 = 0;
(void)catalogGetBodyHandFilenumChecked(bodynum, &hf32);
handfilenum = (u16)hf32;
```

Log line `bgunTickMasterLoad enter player=0 newwpn=22 filenum=907 bodynum=86 handfilenum=0`
proves the catalog returned `handfilenum=0` for body 86 (the player's `base:dark_combat`
body), even though the authoring table `g_BodyData[]` has
`{ "base:dark_combat", 86, 0, 0, 0, HEADBODYTYPE_FEMALE, 159, FILE_CDARK_COMBAT, 1.0f,
 0.9530516267f, FILE_GCOMBATHANDSLOD }` — a non-zero hand filenum.

Traced the data flow:

1. `bodydata_authored.c::g_BodyData[]` has `handfilenum = FILE_GCOMBATHANDSLOD` for body 86.
2. `romextract_pdbody.c::s_emitOneBody` writes `"hand": "FILE_GCOMBATHANDSLOD"` to
   `data/<romid>/bodies/base_dark_combat.pdbody` — confirmed by inspecting the file on
   disk.
3. `loader_pool.c::parseBody` reads the file via `jstream_str_eq(&key, "handfilenum")` —
   **wrong key**. The emitter writes `hand`, the parser reads `handfilenum`. The match
   never fires. `b.handfilenum` stays 0 from `memset`.
4. Loader pool stores body 86 with `handfilenum=0`. Catalog manager returns it. Master
   loader queues a load for filenum=0, which never completes, and the master state
   machine sits in HANDS forever. Weapon model never binds.

Same issue applied to `parseHead`: emitter writes `mesh`, parser reads `filenum`. Heads
loaded with `filenum=0` too, which would cascade into other load failures for any code
reading the head's mesh filenum (less load-critical than the body's hand, but the same
class of bug).

Schema doc `context/designs/catalog/universality-pivot-schemas.md` Sections 2.2 / 2.3
confirms `mesh` and `hand` are canonical. The emitter and schema are in agreement; only
the parser was orphaned at the rename in the BYOR completion landing (2026-05-03).

## Fix

`port/src/loader_pool.c::parseHead` and `parseBody` now accept both names:

```c
// Head
else if (jstream_str_eq(&key, "mesh")
      || jstream_str_eq(&key, "filenum"))   h.filenum   = (u16)jread_enum_or_int(s, JREF_FILE, 0, "head.mesh");

// Body
else if (jstream_str_eq(&key, "mesh")
      || jstream_str_eq(&key, "filenum"))       b.filenum       = (u16)jread_enum_or_int(s, JREF_FILE, 0, "body.mesh");
...
else if (jstream_str_eq(&key, "hand")
      || jstream_str_eq(&key, "handfilenum"))   b.handfilenum   = (u16)jread_enum_or_int(s, JREF_FILE, 0, "body.hand");
```

Canonical schema name (`mesh` / `hand`) is now accepted as the primary key match. The
legacy `filenum` / `handfilenum` aliases stay because the parsing cost is zero and the
forward-compat keeps the loader robust to any stale per-asset file that didn't get
re-emitted from a divergent build.

## Regression coverage

Extended `tools/smoke-verify/tests/boot_smoke.json` with three new `required_lines`:

- `LOADER\.UNIVERSAL\.OK: kind=head scanned=84 registered=84 envelope_failures=0 register_failures=0`
- `LOADER\.UNIVERSAL\.OK: kind=body scanned=68 registered=68 envelope_failures=0 register_failures=0`
- `LOADER\.POOL\.HEAD\.OK: active=1 heads=84 \(expected=152\)`
- `LOADER\.POOL\.BODY\.OK: active=1 bodies=68 \(expected=152\)`

If a future rename / drift in the emitter or parser causes head or body records to fail
to register (envelope_failures != 0) or the pool to flip inactive, the assertion fails.

The numeric counts (`84` for heads, `68` for bodies) are the BYOR base-game totals; they
are the same ones already in the matching `LOADER.UNIVERSAL.SUMMARY: per-kind` line that
the smoke runner reads.

Also added a new `SP-16` entry to `context/systemic-bugs.md` documenting the
"schema / emitter / parser field-name drift" class with the symptom signature and the
canonical audit command for future renames.

## Build verify

```
ninja -C Build pd pd-server pd-tests pd-updater
exit=0
```

All 4 targets clean. PerfectDark.exe relinked at 23:53 with the new loader_pool.c.

## Smoke verify

`PerfectDark.exe --no-update-check --no-sound --smoke boot_smoke.json` against an
existing installed `.claude/smoke-verify-install/` with stale `.pdbody`/`.pdhead` files
(written by the unchanged emitter in a prior session) — confirmed:

- `LOADER.UNIVERSAL.OK: kind=weapon scanned=86 registered=86`
- `LOADER.UNIVERSAL.OK: kind=head scanned=84 registered=84`
- `LOADER.UNIVERSAL.OK: kind=body scanned=68 registered=68`
- `LOADER.POOL.HEAD.OK: active=1 heads=84 (expected=152)`
- `LOADER.POOL.BODY.OK: active=1 bodies=68 (expected=152)`
- Zero forbidden patterns (`EXCEPTION_ACCESS_VIOLATION`, `FATAL:`, `LOUDFAIL.LOAD:
  ...unrecoverable`).

Then ran a multi-scenario sequence (swarm_cpu_smoke followed) which enters Combat Sim
and renders the player weapon. Result lines:

```
LOG.WPN.DIAG: bgunRender enter player=0 frame=227 ... R(wpn=22 visible=1 inuse=1 state=5 sm=2)
  L(wpn=22 visible=0 inuse=0 state=5 sm=2) gunctrl_wpn=22 switchto=-1 passive=0
  gunmodeldef=000001550b3a5d00 handmodeldef=000001550b39f060
  R_handmodel_def=000001550b39f060 R_gunmodel_def=000001550b3a5d00
  masterload=4 gunmemowner=0
```

Critical post-fix evidence:

- `R(wpn=22 visible=1 inuse=1)` — weapon model **visible** on render.
- `gunmodeldef=000001550b3a5d00 handmodeldef=000001550b39f060` — both bound to
  non-NULL pointers.
- `masterload=4` — MASTERLOADSTATE_LOADED. Master loader reached completion.
- Zero `visibility-gate-fail` lines in the entire log.

The "weapon missing when loading in" symptom is gone.

## Commit

`219378c0` on `dev`:
> Catalog - c027: B-328 fix loader_pool head/body parser key drift

Files: `port/src/loader_pool.c`, `tools/smoke-verify/tests/boot_smoke.json`,
`context/bugs.md`, `context/systemic-bugs.md`.

## Out of scope / left for next session

- Mike's other workers are mid-edit on `port/fast3d/swarm_gpu.cpp`,
  `port/src/swarm_test.c`, and `port/include/swarm_test.h`. Those uncommitted modifications
  in the worktree were left untouched per the task's "back off from in-flight files"
  rule. They're independent of this fix.
- The smoke-verify run output JSON files under `.claude/smoke-verify-runs/` (28 new files)
  are pre-existing untracked, not related to this work.
