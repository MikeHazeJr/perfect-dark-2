# V-004 physical input validation — 2026-08-08

Verdict remains `not_run`; item status remains `partial`. This receipt advances
only what was physically observed and does not promote enumeration or scripted
controller input into hardware evidence.

## Real controller sampling

- Windows and the ordinary client both enumerated an Xbox-class controller;
  the client logged assignment of `0: (Xbox One Controller)`.
- A read-only 12-second `XInputGetState` sample obtained 707 successful samples.
- The packet counter changed once (`14489458` to `14489460`), but buttons,
  triggers, and both sticks showed no control transition. The sampled values
  remained buttons `0000`, triggers `0/0`, left stick `0/0`, and right stick
  `-182/3569`.
- Therefore controller focus, scroll, confirm, back, contextual actions,
  keyboard-to-controller switching, and live controller glyph change are
  **not run**. Device presence is not controller proof.

## Tooling and ordinary-client evidence

- `port/src/smoke_harness.c` now accepts explicit SDL `mouse_move` and
  `mouse_wheel` events. The events target the live SDL window and reject
  incomplete or zero-delta payloads.
- `tests/test_smoke_input_events_static.cpp`: focused `[v004]` test passed
  2 cases / 16 assertions in isolated session build `v004physical`.
- Isolated all-target build `v004physical` passed for the frozen source.
- `.claude/smoke-verify-runs/results-20260808T231805Z.json`: ordinary client
  passed 11/11 assertions, dispatched 16/16 scheduled real SDL keyboard,
  mouse-motion, mouse-click, and mouse-wheel events, exited with code 0, and
  recorded no access violation, fatal error, or timeout.
- Screenshots are under
  `.claude/smoke-verify-runs/screenshots/20260808T191623-v004_input_validation_smoke`.
  They prove the ordinary client rendered and continued responding while the
  SDL events were delivered. Portable startup entered the new-agent flow, so
  these captures do **not** prove Settings profile save/reload or the intended
  main-menu Settings path.
- Tracked receipt:
  `context/evidence/v004-input-validation-result-20260808T231805Z.json`.
- Tracked client/XInput excerpt:
  `context/evidence/v004-input-validation-client-excerpt-20260808T231805Z.log`.

## Remaining physical proof

- Settings Input profile save, process restart, and reload in a disposable
  portable profile.
- Mouse focus, click, wheel-scroll, and back on the intended Settings/menu
  targets (event delivery alone is insufficient).
- Real controller focus, scroll, confirm, back, and contextual Forge/vehicle
  actions.
- Keyboard-to-controller device switch and immediate live glyph update.
- No lost input through representative modal and context transitions.

The controller portions require a person to actuate the connected controller
during a coordinated capture. No product choice is required.
