# Engine Phase 5 audit -- polish + telemetry (2026-05-03)

> **Status**: SHIPPED at dev `ea434127` (worktree `hardcore-leavitt-20fefd`).
> **Pillar**: Engine.
> **Phases shipped previously**: Phase 1 path-buffer refactor `19053489`; Phase 2 thread pool + progress channel + boot overlay `78b5008a`; Phase 3 parallel verify pass `f1d670e3`; Phase 4 walker + emitter structural concurrency `55392850`.
> **Inheritance**: `context/designs/engine/startup-acceleration.md` Phase 5 spec.

---

## What shipped

Two targeted improvements on top of the structural concurrency work:

1. **Per-launch weight caching** so the boot progress bar paces from the previous boot's actual timings instead of the hand-calibrated Phase 2 defaults.
2. **`Boot.Telemetry` pd.ini flag** that gates a per-phase elapsed-ms log dump at end of boot, useful for future tuning without polluting normal user logs.

The pool-tuning verification is a no-op pass: the existing `(physical_cores - 2)` formula in `bootPoolInit` reads correctly on Mike's 16-core box (14 workers, confirmed in Phase 3 session log) and floors at 1 worker on minimal hardware. No code change.

The overlay polish pass is also a no-op: the Phase 2 overlay already carries eased progress, full-screen dark navy background, percentage indicator, and theme-driven colors via `pdguiGetActivePaletteRaw`. Per Mike's Q2 ("plain is fine, colored with PD colors") there is nothing to add.

---

## File changes

### `port/src/boot_progress.c`

The const `k_PhaseWeight[]` becomes mutable `s_PhaseWeights[]` so the config layer can write into it on load and recompute from on-the-fly timings.

New static helpers and state:

- `static u64 s_PhaseStartMs[BOOT_PHASE_COUNT]` and `s_PhaseElapsedMs[BOOT_PHASE_COUNT]` capture per-phase begin/end timestamps via `SDL_GetTicks`.
- `static s32 s_TelemetryEnabled` backs the `Boot.Telemetry` config key.
- `static const char *k_PhaseConfigKey[BOOT_PHASE_COUNT]` -- short stable identifiers per phase used to register `Boot.Weight.<key>`. Renaming any of these would invalidate cached weights, so the table is annotated do-not-rename.
- `static void s_registerConfig(void)` -- idempotent: registers all 22 `Boot.Weight.<phase>` floats + `Boot.Telemetry` int once, on first `bootProgressInit`.

Wiring at the existing entry points:

- `bootProgressInit()` calls `s_registerConfig()` so the array picks up cached values from disk (`configReplayPending` in `port/src/config.c::configRegisterFloat` writes pending values into the registered float pointer at registration time).
- `bootProgressBeginPhase(phase)` records `s_PhaseStartMs[phase] = SDL_GetTicks()`.
- `bootProgressEndPhase()` accumulates elapsed into `s_PhaseElapsedMs[phase]` (defensive guard: only updates if start was recorded; phases skipped via different code paths stay at zero).
- `bootProgressMarkComplete()`:
  - Computes total elapsed across all phases.
  - Recomputes per-phase fractions and writes into `s_PhaseWeights[]`. The config layer writes them to pd.ini at process shutdown via the existing `configSave` path; no new save trigger needed.
  - When `s_TelemetryEnabled`, emits a multi-line `BOOT_TELEMETRY:` log dump including total ms, worker count, and per-phase ms + percentage.

### Behavior

- **First launch**: pd.ini has no `Boot.Weight.*` keys, so `configReplayPending` is a no-op and `s_PhaseWeights` keeps the Phase 2 defaults. End of boot computes weights from this run; they get saved.
- **Second launch**: the saved weights load before the boot orchestrator runs, so the bar paces from real measurements. The new measurements overwrite again.
- **Weight churn**: per-launch jitter (cache state, OS scheduling, network presence init, etc.) adds noise. Self-tuning converges within a few launches on a stable workload; unstable boots produce slightly different bars. Acceptable trade for "matches reality" UX.
- **Telemetry**: gated on `Boot.Telemetry=1` (default 0) so normal users never see the chatter. Setting the flag in pd.ini emits one summary block per boot.

### What stays unchanged

- Phase weights array shape (22 entries, one per `boot_phase_t`).
- Public API of `boot_progress.h` -- no new exports, no signature changes.
- The overlay rendering loop in `pdgui_bootoverlay.cpp`.
- Boot pool topology + worker spawning.

---

## Build verify

Clean four-target via `devtools\build-session.ps1 -Session phase5 -Target ...`:

- Client (`pd`, `PerfectDark.exe`): **PASS, 55.5 MB (35s)**
- Updater (`pd-updater`, `Updater.exe`): **PASS, 12.3 MB (2s)**
- Server (`pd-server`, `PerfectDarkServer.exe`): **PASS, 22.4 MB**
- Tests (`pd-tests`, `pd-tests.exe`): **PASS, 24.6 MB (21s)**

No new compile warnings.

---

## Operator notes

To enable telemetry diagnostics, set in `pd.ini`:

```
[Boot]
Telemetry = 1
```

Sample output (numbers illustrative):

```
BOOT_TELEMETRY: total=8412ms workers=14 phases:
BOOT_TELEMETRY:   extract_files     0ms (  0.0%)   <- skipped after first launch
BOOT_TELEMETRY:   verify_files   1230ms ( 14.6%)
BOOT_TELEMETRY:   extract_segs     12ms (  0.1%)
BOOT_TELEMETRY:   verify_segs     145ms (  1.7%)
BOOT_TELEMETRY:   release_rom      11ms (  0.1%)
BOOT_TELEMETRY:   catalog_init    178ms (  2.1%)
BOOT_TELEMETRY:   walker          412ms (  4.9%)
BOOT_TELEMETRY:   emit_wpn        198ms (  2.4%)
...
```

To force the bar back to default weights, delete the `[Boot]` section's `Weight.*` keys from pd.ini.

---

## Where to look

- Config wiring + per-phase capture: `port/src/boot_progress.c::s_registerConfig`, `bootProgressBeginPhase`, `bootProgressEndPhase`, `bootProgressMarkComplete`.
- Phase keys (do-not-rename): `k_PhaseConfigKey[]` in the same file.
- Pool sizing math (unchanged this phase): `port/src/boot_pool.c::s_detectPhysicalCores`, `bootPoolInit`.
- Overlay (unchanged this phase): `port/fast3d/pdgui_bootoverlay.cpp`.

---

## What is now closed

The startup-acceleration arc is complete:

- Phase 1: `fs.c` path-buffer refactor (prerequisite).
- Phase 2: thread pool + progress channel + boot overlay (UX baseline).
- Phase 3: parallel verify pass (recurring boot speedup).
- Phase 4: walker + emitter structural concurrency (first-launch + post-B-318 speedup).
- Phase 5: per-launch weight caching + telemetry hooks (UX matches reality, future tuning).

Next time the design doc is opened it can be moved to `_old/designs-shipped/` per `retention.md`.
