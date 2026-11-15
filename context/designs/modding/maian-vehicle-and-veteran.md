# Maian vehicle and Maian veteran: approved concepts and plan (week of 2026-11-15)

Owner: MODS week session, branch `week-pd/mods`. Mike approved the concepts on
2026-11-15: "Concepts approved. Build with the in-game mod tools, commit and
push as you go, no merges to dev." This note holds his brief, what exists in the
engine, and the plan. Everything below is marked READ (seen in source), RUN
(proven by a named run) or OPEN.

## 1. Mike's brief (2026-11-15, his words, lightly condensed)

### Weapon

The crystal pistol session is done (`week-pd/crystal-pistol`, draft PR #6).
Use its output as the weapon mod (`mod_crystal_pistol:crystal_pistol`; see
`maian-crystal-pistol-handoff.md` section 11).

### Vehicle

Maian two-seat hover vehicle: a driver plus a gunner on a rotating turret. The
gunner sits in an armoured cowl and is hittable only through a front opening of
about 70 degrees.

- Boost ramps to full over about 1 s. After about 4 s (a node variable) the
  crystals turn black: boost continues above base but below full speed,
  steering is briefly limited, and the turret cannot fire until the crystals
  are their normal colour again. Recharge is faster when stopped and scales
  with speed.
- Turret: 18 crystals, larger versions of the pistol's, each regrowing per
  shot. Full regrow = fire interval x 18, plus about 10 % scaled by speed; at
  10 % of max speed regrow keeps pace with steady fire, and faster driving
  causes a delay. Normal driving lowers the fire rate, normalised to current
  versus max non-boost speed.
- Lock-on: vehicles only. Shots track until they hit, the gunner releases
  tracking/zoom, or boost ramps. The boost curve weakens tracking; at full
  boost the turret cannot fire. A shot that loses tracking keeps its heading.
- Intent: firing while stopped is strongest, so the crew become sitting ducks.

### Character

Maian veteran: no traits; normal hitbox, speed and health. An older Skedar-war
veteran: weathered grey skin, scars, a worn harness with crystal inlays, a
holstered crystal pistol, three crystal chevrons on one pauldron. Male and
female variants on the Maian rig. Reuse the Maian animations; add driver and
gunner seats and get in/out; a calmer idle if easy. The simulant prefers Maian
weapons, holds cover and engages at range. A single-player ally can drive or
gun.

### Level

The Maian arena is still open: do not start it.

## 2. What exists (survey, 2026-11-15)

OPEN: filled in from the read-only survey of vehicles, characters and
simulants.

## 3. Plan

OPEN: written after the survey.
