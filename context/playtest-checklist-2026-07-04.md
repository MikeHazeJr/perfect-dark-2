# Playtest checklist -- close the FIXED-PENDING-PLAYTEST backlog (2026-07-04)

**All the code is committed.** These cards are code-complete + build/test-verified; they
sit in the backlog only because they need one on-screen confirmation each. This
checklist groups them so a single play session closes several at once. Run it once,
tick what passes, and move the passing cards to Done (or tell the next session which
regressed). Sessions 2, 3, and 7 are the critical path; the rest are quick smoke gates.

Full repro/root-cause detail for any item lives in `context/bugs.md` under its B-NNN.

---

## Preflight -- B-801 memory safety (do this once before launching)

B-801 (the reason live testing was paused) was host virtual-memory exhaustion from a
**2 GB pagefile**, not an app bug. That is now remediated: this host measured **11 GB
pagefile / ~23 GB free commit** on 2026-07-04, and the smoke harness now carries a
**memory-risk control** -- `tools/smoke-verify/lib/Test-MemorySafety.ps1` gates every
live launch in `run.ps1` and refuses to start the game if commit/pagefile headroom is
low. It runs automatically; you don't have to do anything. To confirm readiness by hand:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/smoke-verify/test-memory-guard.ps1
```

Expect `ALL MEMGUARD CHECKS PASSED` and a "free commit / pagefile" line well above the
floors. Everything else is staged: `.claude/smoke-verify-install/` already has
PerfectDark.exe + the ROM + extracted `data/`. The live-launch go is yours; the machine
crash risk that stopped this before is now controlled, and all session work is pushed to
`origin/dev`, so a worst-case launch cannot lose committed progress.

---

## Session 1 -- Main menu close (30 seconds)

- **c3826 / B-361** -- Start game, main menu appears, press **Esc** (or click the title X).
  PASS: menu closes once with a single cancel sound and stays closed (does not reopen).

## Session 2 -- One Combat Sim match, Grid, 8 bots, jumping ON (covers 2 cards)

- **c083 / B-316** -- Combat Sim -> Grid -> 1 player / 8 bots -> **Bot Jumping ENABLED** -> Start.
  PASS: match starts, no crash / ACCESS_VIOLATION at match start (chic-robot bots render fine).
- **c3815 / B-356** -- let the match reach its score/time limit (or End Match).
  PASS: post-match endscreen appears and is interactive (title X closes it; Quit / Play Again / Return to Room respond); no forced close.

## Session 3 -- CI Training mission, solo (physics)

- **c136 / B-350** -- reach a section with overhead solid blockers + doorways; try to jump
  sideways into/through a blocker edge.
  PASS: cannot side-enter through the overhead blocker; no wall clipping; doorways enter normally.

## Session 4 -- Grid free-roam, hoverbike (vehicle input)

- **c071 / B-298** -- mount the hoverbike; W / RT accelerates, A-D / LS steers, F / X dismounts.
  PASS: all inputs responsive (no more silent no-ops).

## Session 5 -- Any mission with Falcon 2 equipped (weapon render)

- **c3827 / B-362** -- start the mission, watch the weapon equip animation.
  PASS: no stretched beam/geometry toward screen center during equip; laser sight hidden while
  the hand is mid weapon-change.

## Session 6 -- 4-bot Combat Sim, DO NOT MOVE OR FIRE (kill attribution) **[highest value]**

- **B-249** -- 4-bot match, stay completely passive ~30s while bots kill each other, open scorecard.
  PASS: player 0 kill count = **0** (not N>0), kills credited to the actual bot attackers.
  If it FAILS: the `B-249.DIAG` LOG_WARNING already in the build fires the moment slot 0 is
  credited but that slot's chr is a bot -- capture that log line and it names the call site to fix.
  REGRESSION: a second match where you DO fire -- your own kills must still credit correctly.

## Session 7 -- Swarm benchmark, CPU mode (bot-pool scaling)

- **c029 / c080 (B-307 / B-311)** -- Combat Sim -> Swarm Test -> **CPU mode** -> cycle the ladder
  4 -> 8 -> 16 -> 32 -> 64 -> 128 -> 256.
  PASS: every cycle completes, no crash at the 128/256 transitions, healthy alive/kill counts,
  benchmark summary fires.

## Session 8 -- Listen-host Combat Sim start (connectivity)

- **c3845 / B-910** -- Combat Simulator as listen host -> any stage + bots -> **Start Match**.
  PASS: button responsive, match starts (not dead/rejected), no connectivity-gate error.

---

## Quick "does it load/run" smokes (no repro, just confirm it works)

- **c031 / c032 / c033 / c3807** -- Swarm Test, GPU mode + CPU/GPU toggle, cycle to 64; and look
  for the GPU-swarm debug tab in the Swarm Test UI.
- **c3738** -- load a Skedar level; locomotion on sloped/surface terrain looks normal.
- **c3808** -- Modding Hub: the folder -> .pdmod packer entry is present and packs a folder.
- **cmpbhfdir2lxo** -- Campaign auto-runner: start it, let a mission complete unattended, verify
  it advances with unlocks.

---

*Generated 2026-07-04 from context/bugs.md. Ages out per retention.md once the pass is done.*
