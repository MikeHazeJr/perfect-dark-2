# Animated surfaces (room effects, part 1)

Status: built on `week-pd/core` 2026-11-16 (B-1172's last gap). Owner: CORE.

## What it replaces

`src/game/dyntex.c` moves the N64 texture coordinates of some room vertices
every frame: flowing water, a monitor, stepping arrows, the Deep Sea teleport
and the Attack Ship power node. Which vertices move is decided by texture
checks in `src/game/tex.c` while a room's display list loads, plus stage rules
in `dyntexSetCurrentType`. A stage drawn from scenario source never loads
those display lists through `tex.c`, so its water stood still.

## Data (scene.glb)

A material's `extras.pd2_material` may carry:

- `animation`: one of seven presets (below). Absent means still.
- `uv_scale`: `[u, v]`, required with `animation`. With s' and t' the vertex's
  coordinates after the room's texture scale, its runtime UV is
  `u = s' / 32 * u_scale` and `v = 1 - t' / 32 * v_scale`, as the extractor
  converts them.

Every primitive of an animated material carries `_PD_DYNTEX`, a VEC4 float
accessor with one entry per vertex: the N64 texture coordinates s and t before
the room's texture scale, and the scales for s and t (0 to 65535).

The conformance checker refuses an unknown preset, a missing or non-finite
`uv_scale`, an animated material's primitive without `_PD_DYNTEX`, and a
`_PD_DYNTEX` on a material without `animation`.

## Presets

f is `g_Lv80SecIntervalFrac` (0 to 1 over 80 seconds). Coordinates are s16 as
in the original.

| Preset | Motion | Condition |
|---|---|---|
| `river` | t + ((s32)(f x 10 x 4096) mod 4096) | always |
| `monitor` | t - ((s32)(f x 4 x 4096) mod 4096) | always |
| `ocean` | t + trunc(sin(((t mod 22) / 22 + 5f) x tau) x 65), s + trunc(cos((((s + 22) mod 22) / 22 + 5f) x tau) x 65); the modulo is unsigned (dyntex.c's `u32` operands) and tau is the game's `M_BADTAU` | always |
| `arrows` | s + (((s32)((1 - f) x 480) mod 8) x 256) | always |
| `teleportal` | as `river` | moves while stage flag 0x100 is set and 0x10000 is clear (Deep Sea's teleports); otherwise holds |
| `powerring` | as `river` | while stage flag 0x10000 is clear; once set, every vertex goes to s = t = 0 (Attack Ship's engines destroyed) |
| `powerjuice` | as `river` | while stage flag 0x10000 is clear; otherwise holds |

Before the first frame each group (the vertices of one preset in one room)
shifts its base coordinates by 0x2000 when they reach past +-0x5d00, as
`dyntexTickRoom` does on first use.

## What the extractor writes for the base game

The stage checks become data. From `tex.c`: texture 0x06cb `river`, 0x0a6a
`powerjuice`, 0x0a69 `powerring`, 0x06e2 `teleportal`, 0x01c7 and 0x0dae
`river`, 0x029b `monitor`, 0x090f `ocean`, 0x0a42 `arrows`. From
`dyntexSetCurrentType`: nothing on Air Base, no `river` on Investigation or
Villa, and `powerjuice` and `powerring` only on Attack Ship (`river`
elsewhere, Deep Sea's power node).

Known differences from the original, by design: `dyntexSetCurrentType`
returns early on a refused stage rule without changing the current type, so
in a room where another preset loaded first, the refused texture's vertices
could join that preset's group; the scene follows the rule's intent (still).
The original moves a shared vertex slot, so a still triangle that shares a
vertex with an animated one moved too; scene vertices belong to one triangle.

## Runtime

`port/src/scenario_dyntex.c` holds the preset math (unit-tested against
oracles transcribed from `dyntex.c` and the extractor's UV conversion).
`bgRenderScene` passes the interval and the two stage flags each frame
(`scenarioSceneRendererSetDyntexFrame`); the scene renderer recomputes the
runtime UVs (TEXCOORD_1, which the shader samples for both textures of a
`pd2_material`) of the animated vertices and uploads the range that moved.
The renderer logs `SCENARIO.RENDER: animated surfaces` with the counts per
preset when a scene with animated surfaces activates, and the first frame
that moves them.

## Later

The three conditional presets read fixed stage flags, as the original does.
Letting a mission graph switch a surface's animation (an action node) is the
fully converted form; it is not built.
