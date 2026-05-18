# Bot Jumping in Combat Sim (toggleable, OFF by default)

> Status: SHIPPED 2026-05-17 as D2c v1. 2026-05-18 Skedar swarm
> benchmark work uses a separate benchmark-local helper and leaves the
> normal Combat Sim bot-jumping toggle unchanged.
> Date: 2026-05-17.
> Pillars: physics-collision (D2c), input, networking (MPOPTION bit).
> Trigger: Mike directive 2026-05-17 -- "Finish implementing bot jumping in normal Combat Sim play, and it should be a toggleable option in the Combat Sim that is off by default."

## Context

`pillars/physics-collision.md:92` lists D2c "Bot Jump AI" as "Not started. Depends on D2b stability (capsule sweep). Bot jump pathing requires AI to reason about jumpable gaps, which the capsule sweep makes accurate but no AI consumer is wired yet." D2b shipped at `4e620ae8` (2026-05-15, `PC_CAPSULE_ENABLED = 1`), so the gating dependency is now satisfied. Today's `a3bec4fc` added crouch-jump mid-air lift for the player; bots can adopt the same primitive once they decide to jump.

Bots in CS today use the full `aibot` AI tick from `src/game/bot.c` (driven by `AIBOTCMD_*` commands like `ATTACK / FOLLOW / DEFEND / GETCASE2`). The jump action is conspicuously absent from the command set; no scenario or attack mode currently asks a bot to jump.

## Scope (what "done" looks like)

A new MP option toggle ("Bot Jumping") in Combat Simulator setup, defaulting to **OFF**. When the toggle is ON for a given match:

1. Bots evaluate jump opportunities each AI tick.
2. Bots issue `BUTTON_JUMP` (or the action-layer equivalent) to their movement pipeline at the right moment.
3. Bots clear small obstacles, navigate jump pads, and chase players up onto raised platforms.
4. Crouch-jump (today's player primitive) is available to bots so they can clear slightly-higher surfaces.
5. The toggle is per-match (lives on `g_MpSetup.options`) and serialised on the wire so all peers agree.
6. Listen-host and dedicated-server modes both honor it.
7. Toggle defaults OFF so the existing CS gameplay feel is preserved for users who do not opt in.

## Current code shape (file:line evidence)

- MP option bitmask: `src/include/constants.h:2925-2934` defines `MPOPTION_*` bits up through `0x00000200`. Next free bit is `0x00000400`.
- Bot AI dispatcher: `src/game/bot.c` driven by `chr->aibot->command` (`AIBOTCMD_ATTACK` etc., `bot.c:1914-1946` for setters). Tick site: `chraTick` -> `chraiExecute` per `pillars/connectivity.md` and the design doc `designs/in-flight/gpu-swarm-bot-pipeline.md:33-50` summary.
- Bot movement: `aibot` has `movedata`-style fields (`speedforwards`, `speedsideways`, `speedtheta`, etc.) that feed `bondbike`/`bondwalk` analogs through `chraiExecute`.
- Jump primitive: player path is `wantsjump=true` -> `bwalkUpdateVertical` (`bondwalk.c:902-940`) applies `FIXED_JUMP_IMPULSE = 8.2f` to `bdeltapos.y` when grounded. Bots would set their own `wantsjump` flag in the equivalent of `bwalkUpdateVertical` for AI-driven chrs.
- Capsule sweep: `src/lib/capsule.c::capsuleSweep` (`PC_CAPSULE_ENABLED = 1`) tests swept volume against geometry; useful for "can the bot fit through this gap if it jumps?" pre-flight.
- Combat Sim setup UI: `port/fast3d/pdgui_menu_mpsetup.cpp` houses the Combat Sim options panel. Existing toggles ("One Hit Kills", "Teams Enabled", "No Radar", "No Auto-Aim", ...) follow a uniform pattern -- each binds to one of the `MPOPTION_*` bits in `g_MpSetup.options`.
- Wire sync: `port/src/net/netmsg.c::netmsgSvcMpSettingsRead/Write` (and the matching CLC) carry `options` as part of the standard MP settings broadcast. Adding a new bit is wire-compatible with v49 (no version bump).
- Action map analogue: bots do not go through `port/src/actionmap.cpp`; they bypass into `chr->aibot->movedata` directly. The action map is for human players only.

## Required pieces

### A. The toggle (smallest slice, ~0.5 sess)

1. Add `MPOPTION_BOTJUMP 0x00000400` in `src/include/constants.h:2934+`.
2. Add a toggle row in `port/fast3d/pdgui_menu_mpsetup.cpp` -- "Bot Jumping" label, binds to `g_MpSetup.options & MPOPTION_BOTJUMP`. Place it in the Bot Settings sub-panel adjacent to "Fast Movement" / "No Auto-Aim". Default = unset (OFF).
3. Default seeding in `mpInitMatch` / `mpSetupInitDefaults` (wherever `g_MpSetup.options` is initialised): the new bit stays 0.
4. Wire serialisation: confirmed already covered by the existing `options u32` carried in `CLC_ROOM_SETTINGS_UPDATE` / `SVC_ROOM_SETTINGS`. No netmsg.c change needed.
5. Test pin: extend `tests/test_versions.cpp` (or a new `test_mpoption_botjump.cpp`) with a static assertion that `MPOPTION_BOTJUMP == 0x00000400` so the bit position cannot drift silently.

### B. Bot jump decision (~1 sess)

In the bot AI tick (`src/game/bot.c` per-bot per-frame logic), add a `botShouldJumpThisFrame(chr)` helper called once per bot. The decision logic:

1. **Gate on the MP option**: if `!(g_MpSetup.options & MPOPTION_BOTJUMP)` return false. Cheap early-out.
2. **Already airborne**: return false if `chr` is mid-jump (the bot's `bdeltapos.y` is non-zero or its grounded check fails); avoids double-jump.
3. **Goal-driven jump**: if the bot's current waypoint (`gotopos`) is higher than the bot's current foot Y by a small threshold (say 30-80 units), and the horizontal distance to the waypoint is small enough that a jump arc would land on it, return true. Reuse the player jump impulse arithmetic to bound the threshold.
4. **Obstacle-driven jump**: if the bot's intended forward step is blocked at foot level but clear above the bot's crouch height, jump. A two-tier capsule sweep against the immediate forward step accomplishes this -- swept volume at the bot's current Y blocks; swept volume at Y + 30 clears. The capsule sweep is the same primitive the player jump path uses.
5. **Combat-context jump**: when `chr->aibot->command == AIBOTCMD_ATTACK` and the target is on a different Y level (above), consider jumping to maintain LOS. Lower priority than #3/#4.
6. **Cooldown**: bots cannot re-jump within 0.5s of landing (15-30 ticks). Avoid bunny-hop spam.

### C. Bot jump execution (~0.5 sess)

The bot's per-tick movement path already writes `chr->aibot->movedata.*` -- it just lacks a `wantsjump` flag. Two paths:

- **Option 1**: Add `chr->aibot->wantsjump` (one byte) + handler in the AI-equivalent of `bwalkUpdateVertical`. The handler applies `FIXED_JUMP_IMPULSE` to the bot's `bdeltapos.y` exactly like the player path. Reuses the existing `capsuleSweep` so collision pipeline doesn't need re-derivation.
- **Option 2**: Synthesise a `BUTTON_JUMP` bit into the bot's c1buttons-equivalent buffer (if one exists in chrTick). This would unify the player and bot jump paths but may require a wider refactor.

Option 1 is the lower-risk first slice. Option 2 is a candidate follow-up if the unified path simplifies maintenance.

### D. Crouch-jump for bots (~0.5 sess, optional)

After C lands, extend with the crouch-jump primitive shipped today at `a3bec4fc`:

1. Bot detects a slightly-higher surface (~30-60 units above jump apex) it cannot reach by base jump alone.
2. Bot fires `wantsjump`, then in the same tick (or next) flips `chr->crouchpos = CROUCHPOS_DUCK`.
3. The +1.5 boost from `bondmove.c::g_BondCrouchJumpActive` requires the player path; the equivalent in the bot path is the same `bdeltapos.y += 1.5f` lifted into the AI's vertical handler.

This makes bots feel as capable as the player at clearing obstacles.

### E. Smoke test (~0.5 sess)

`tools/smoke-verify/tests/bot_jump_smoke.json` -- scenario: launch Combat Sim with the `MPOPTION_BOTJUMP` bit set, spawn a bot in front of a known-jumpable obstacle (CITRAINING has a few), run for 30 seconds, assert `BOT: jump fired (chrnum=N target_y=Y delta=Y)` log lines fire and no `EXCEPTION_ACCESS_VIOLATION` / `FATAL:`. Mirror with a second `bot_jump_off_smoke.json` that arms the same scenario WITHOUT the bit and asserts ZERO `BOT: jump fired` lines -- proves the toggle gates correctly.

Use existing `--launch-mp-room` + `--launch-load-agent` CLI fast-paths to script the bot-spawn scenario. The new MP option bit gets passed via the CLI as well (extend `--launch-mp-room` or add a new `--mpoptions <hexmask>` arg).

### F. Wire-format verification (~0.25 sess)

Confirm the new MPOPTION_BOTJUMP bit round-trips cleanly through the existing `CLC_ROOM_SETTINGS_UPDATE` / `SVC_ROOM_SETTINGS` path. The `options` field is `u32`; adding a new bit is wire-compatible with v49. Pin via a `tests/test_mpoption_botjump_roundtrip.cpp` Catch2 case that exercises `netmsgClcRoomSettingsUpdateWrite/Read` with the bit set.

## Total estimate

- Slice A (toggle + UI): 0.5 sess
- Slice B (decision): 1 sess
- Slice C (execution path, Option 1): 0.5 sess
- Slice D (crouch-jump for bots, optional): 0.5 sess
- Slice E (smoke tests): 0.5 sess
- Slice F (wire verification): 0.25 sess

**Total: ~3.25 sessions** to a fully shipped, gated bot-jumping feature with smoke coverage.

## Open questions

1. **Decision frequency**: every AI tick (60 Hz) or sub-sampled? CPU bots already tick AI every frame, so 60 Hz is the natural choice unless playtest shows it's too noisy.
2. **Difficulty scaling**: should harder bot difficulties (DARK / PERFECT) jump more aggressively (e.g., chase targets to higher ground), while easier ones (MEAT / EASY) jump only when blocked? This would mirror the existing `mpbotconfig` difficulty knobs but adds tuning surface.
3. **Telemetry**: should each bot jump emit a `BOT.JUMP:` log line gated under `PD_DEV_BUILD`? Useful for the smoke + first-pass tuning.
4. **Path interpolation**: does the bot path system support arcs (jump trajectory) or only ground-level waypoints? If only the latter, jumping bots may overshoot or undershoot their landing target -- worth a small probe before committing to Slice B.
5. **Crouch-jump unlock criteria**: should bots use crouch-jump only when the obstacle is in the 30-60 unit "just-barely" zone, or always? Probably the former (matches player-feel where crouch-jump is a deliberate technique).

## Risks

- **AI loop performance**: 32 bots at 60 Hz, each running a capsule sweep for jump-decision, ~1900 sweeps/sec extra. The capsule sweep at PC_CAPSULE_ENABLED=1 is cheap (sub-microsecond per sweep on modern CPU) but worth a perf probe at the 32-bot ladder.
- **Net sync**: bot moves are server-authoritative per `pillars/connectivity.md`; the jump impulse needs to land on the server side so clients see authoritative trajectory. The existing `SVC_BOT_AUTHORITY` covers move authority -- jump just rides along.
- **Pathfinding**: bots' existing waypoint pathfinder may produce paths that wander next to a jumpable shortcut without using it. Without rewriting the pathfinder, the jump decision is purely reactive (current-step + obstacle), not planned. Acceptable for v1.

## Where to look during implementation

- `pillars/physics-collision.md:90-103` (current D2c status).
- `pillars/input.md` -- action-map analog for bots is bypassed; bots do not use the action map. Keep this in mind when porting tap/hold patterns.
- `src/game/bot.c` -- main AI tick.
- `src/game/chraction.c` -- chr movement integration.
- `src/lib/capsule.c::capsuleSweep` -- collision primitive for jump pre-flight.
- `src/game/bondwalk.c:902-940` -- the canonical player jump impulse logic to mirror in the AI path.
- `src/include/constants.h:2925-2934` -- MPOPTION bitmask.
- `port/fast3d/pdgui_menu_mpsetup.cpp` -- toggle UI.
- `tools/smoke-verify/tests/swarm_*_smoke.json` -- reference smoke test patterns for bot-in-stage scenarios.

## Out of scope

- GPU swarm bots (c3807): the swarm pipeline is a separate codepath where bots have `chr->aibot = NULL`. Jump behaviour for GPU bots would require the Tier-1 full state-machine port (already filed as a 5-8 session multi-slice in `serialized-sleeping-truffle.md`). The bot-jumping toggle proposed here is CPU-bot only.
- Wall-jump (continuous wall-climb / running up walls): different mechanic; not in scope.
- Player-vs-bot jump-related taunts / SFX: cosmetic, defer.
