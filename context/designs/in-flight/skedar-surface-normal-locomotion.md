# Surface-Normal Locomotion for Skedars (S594h-B scope)

> Status: SCOPED, not started. Filed 2026-05-01 PM after S593h refinement
> bundle and S594h-A spawn-correction shipped.

## Premise (from Mike)

> "Let's implement it."

Reference spec captured in user prompt:

- Skedars walk on walls and ceilings, rotation aligned to surface normal under them.
- Per-tick raycast from chr outward along their current local "down" to find the surface and its normal. Set chr local up = normal. Gravity / movement applied along the surface plane.
- Wall-to-floor and ceiling-to-wall corners: blend the up vector over a few frames so the orientation transition doesn't snap.
- Freeform navigation (no navmesh on walls/ceilings) -- bot picks a direction toward the player, moves along the current surface, hops to adjacent surfaces at edges. Light enough for 256 stacked.
- Behavioural question to surface a design call on: when bot is close to player, do they stay on their surface and shoot, or commit to dropping? Default suggestion: drop if directly above, stay and shoot otherwise.
- Make it a chr-behaviour trait set per chr type: Skedars always have it; can be enabled for other chr types later via scenario flag or mod.
- Animation is chr-local in PD, should "just work" once the transform is rotated.
- Aim / line-of-fire: must project through world space, not assume world-up.

## Why this is filed, not started in S594h

The work is substantial: it touches chr struct, render transform, movement integration, animation, aim path, AI navigation, and edge handling. The existing chr movement assumes world-up gravity throughout (chr->ground, chr->sumground, fallspeed in world Y, prevpos comparisons in world Y, animation root rotation in world Y). Re-flowing that for a per-chr local up-vector is multi-session.

Plus the "all changes apply to both modes" parity directive: GPU mode bots would also need surface-normal locomotion in the GPU compute kernel -- that compounds with the existing gpu-swarm-bot-pipeline gap.

## Sliced architecture (progressive)

### Slice 1: chr-struct plumbing (no visible effect)

- Add fields to `struct chrdata`:
  - `f32 surface_up[3]` -- current local up (normalized).
  - `f32 surface_up_prev[3]` -- previous up, for blend.
  - `s16 surface_blend_frames` -- countdown timer, 0 = no blend.
  - `u8 surface_loco_flags` -- bit 0: enabled, bit 1: blending, bit 2: airborne.
- Initialize to world-up at chrInit.
- Add `chrSurfaceLocoIsEnabled(chr)` helper (returns 1 for chrs whose body type opts in -- catalog flag on g_HeadsAndBodies[].surface_loco or per-body table).
- Skedar entry gets surface_loco=1 in the catalog seed registration.
- Per-tick: when enabled, copy chr->prop->pos's ground normal into surface_up (once: just the world floor normal, no walls yet).

Slice 1 ships invisible: just the data plumbing. Validates the struct change builds clean, doesn't break the chr pool.

### Slice 2: render transform on the integrated head/body

- In chr render path (chrRender or the model root matrix builder), if surface_loco enabled, build a rotation that maps world-up to chr->surface_up and apply it to the chr's root transform.
- This is purely visual at this slice: chr APPEARS tilted to surface, but movement is still world-up.
- Skedar standing on a slope: model leans with the slope. Skedar standing on flat ground: looks identical to current.
- Verify: animation continues to play correctly through the rotated transform (PD animations are chr-local, this should work).

Slice 2 ships visible-but-nonfunctional: the visual is right, the gameplay isn't.

### Slice 3: movement on the surface plane

- When the bot AI computes velocity (aibot->speedmultforwards / sideways), transform the velocity vector through the surface_up rotation before applying it to prop->pos.
- Rewrite chr->fallspeed / chr->ground integration to use surface_up as the gravity direction.
- Per-tick raycast: cast a ray from chr->prop->pos along -surface_up, hit distance >= chr->height means airborne (drop logic kicks in), hit distance < chr->height means on surface.
- Surface-normal update: when the ray hits, take the BG hit normal as the new surface_up target.
- Blend: if new target differs from current surface_up by more than ~5 degrees, save prev_up = current_up, set blend_frames = 8 (or scenario-tunable), lerp toward target each frame.

Slice 3 ships actual locomotion. Skedars now climb walls and ceilings. The ground integration rewrite is the riskiest part -- many code sites assume world-Y gravity.

### Slice 4: aim / line-of-fire projection

- The bot's aim direction is currently produced in world space (yaw/pitch around world-up). With surface_up off-vertical, the bot needs to project its desired aim direction through the surface_up rotation to keep the gun pointing at the player in world space.
- Same for hitscan / projectile spawn vectors.
- The bgun render path needs to know the chr's surface_up so the held weapon tilts with the chr (cosmetic but glaring if missing).

### Slice 5: edge / corner handling

- Wall-to-floor: chr's down ray transitions from "into wall" to "into floor". Detect via the hit normal jumping >90 degrees in one frame; trigger the blend.
- Floor-to-wall ascent: bot wants to climb. Cast ray forward; if forward ray hits a wall whose normal is mostly horizontal, the bot is at the base of a wall. Update surface_up to that wall's normal and blend.
- Ceiling: same logic, surface_up = ceiling normal (pointing down into the room).
- "Drop" decision: when bot is close to player, the AI evaluates `dot(surface_up, world_up)`. If `< 0` (chr is upside-down or sideways) AND target is below: drop (clear surface_up = world_up, mark airborne, fall under gravity until landing). Otherwise: stay on surface and shoot per the default suggestion.

### Slice 6: GPU mode parity

- Implement equivalent of the surface_up state + raycast + transform in the swarm_gpu compute kernel. This is a rider on the GPU bot pipeline scope at `gpu-swarm-bot-pipeline.md`.
- Position writeback now needs surface_up too so the CPU render side can apply the visual rotation.

## Open questions for Mike

1. **Body opt-in mechanism.** Surface-loco trait per-body in `g_HeadsAndBodies[]` (catalog seed flag), via scenario flag, or via .pdmod mod manifest? First two are easiest to ship; mods can wait.
2. **Slope vs wall vs ceiling threshold.** At what surface_up.y value do we trigger surface-loco mode vs world-Y gravity? Suggest: enabled bodies always use surface_loco; the threshold is moot.
3. **Drop heuristic precision.** "Drop if directly above" -- how do we measure "directly above"? Suggest: when bot is on a surface with `dot(surface_up, normalize(player_pos - bot_pos)) > 0.7` (player is "below" along surface_up), drop.
4. **Animation budget.** PD's chr animations use the model root matrix as their reference. Rotating the root means the animation root walks-along-surface naturally, but root_pos still tracks via prop->pos. Need to confirm no animation-system code path bakes world-Y at frame N.
5. **Multiplayer parity.** If a Skedar is wall-walking in MP, do clients see the same surface_up? Wire it through SVC_NPC_MOVE / SVC_BOT_AUTHORITY? Or compute client-side from prop->pos?

## Suggested first session

If Mike approves this scope: ship Slice 1 + 2 (chr struct + visual rotation) as the first session's batch. That gives Mike "Skedars visually tilt to the floor" on slopes, validates the data plumbing, and de-risks Slice 3 (movement integration).

If the visual-only slice is too small to ship alone, bundle Slices 1-3 -- a multi-session effort but produces the actual gameplay payoff.

## Why this is the third merge of S594h

S593h: swarm refinement bundle (random scale, dark agent, no bot-bot collision, power loadout). Shipped.

S594h-A: spawn algorithm wall-correction + height-failure rejection. Shipped (commit c24e1506).

S594h-B: this scope doc. Implementation deferred to confirm direction with Mike before kicking off the multi-slice work.
