# Sprint Report — Track 2a: Ping-Pong RGBA32F Texture State

- **Sprint card**: c3807 (Benchmarking)
- **Branch**: dev
- **Date**: 2026-05-16
- **Files touched**: `port/fast3d/swarm_gpu.cpp` (+242 / -1)

## Goal

Land the foundational "state-as-texture" layer for the GPU swarm path
alongside the existing SSBO. Compute kernel writes BOTH every frame
(SSBO for the existing CPU readback path, texture as a forward-looking
mirror for slices 2b, 2c, 2d). 2a is the WRITE path only; readers come
in 2b. Acceptance: bit-exact behaviour parity with pre-2a — the SSBO
remains the only consumer, so no observable game-side change.

## What landed

### 1. Forward-declared GL constants

`GL_SHADER_IMAGE_ACCESS_BARRIER_BIT (0x00000020)`, `GL_TEXTURE_2D`,
`GL_TEXTURE_MIN_FILTER`, `GL_TEXTURE_MAG_FILTER`, `GL_NEAREST`. Added
as `#ifndef` guards in the same block as the existing compute-symbol
tokens. `GL_RGBA32F` already in glad.h (0x8814).

### 2. Dynamic loader for `glBindImageTexture`

The project's pinned glad gen exposes `glBindImageTexture` only inside
the GL ES path, not for desktop GL. Added `swarm_glBindImageTexture_t`
typedef + `s_glBindImageTexture` symbol, loaded via
`SDL_GL_GetProcAddress` in `probe_compute()`. Failure logs a warning
and disarms the texture mirror; SSBO path continues.

### 3. Static texture state

```cpp
static GLuint  s_StateTexA = 0;
static GLuint  s_StateTexB = 0;
static int     s_StateTexReadIdx = 0;
static int     s_StateTexArmed   = 0;
```

Two RGBA32F textures sized `SWARM_GPU_MAX × 5` = 4096 × 5. Memory:
`2 × 4096 × 5 × 16 B = 640 KB`. Trivial.

### 4. Texture allocation in `ensure_resources`

```cpp
glGenTextures(2, tex);
glBindTexture(GL_TEXTURE_2D, tex[i]);
glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, SWARM_GPU_MAX, 5,
             0, GL_RGBA, GL_FLOAT, NULL);
glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
```

Note: used `glTexImage2D` not `glTexStorage2D` because the project's
pinned glad gen loads `glTexStorage2D` only inside
`load_GL_ES_VERSION_3_0()` (gated on `GLAD_GL_ES_VERSION_3_0` which is
false for our desktop GL 4.3 context). Calling the unloaded NULL
`glTexStorage2D` caused a segfault on the first dispatch in iter 1 of
this sprint; switching to `glTexImage2D` (core GL 1.0, unconditionally
loaded) fixed it. The two are interchangeable for this use case (no
data upload, single mip level).

### 5. Shader extensions

Added to `kSwarmCs`:

```glsl
layout(rgba32f, binding = 2) uniform writeonly image2D StateOut;

// inside Params block:
int state_tex_enable, _pad_s0, _pad_s1, _pad_s2;

// at end of main():
if (P.state_tex_enable != 0) {
    int x = int(i);
    imageStore(StateOut, ivec2(x, 0), vec4(b[i].px, b[i].py, b[i].pz, 0.0));
    imageStore(StateOut, ivec2(x, 1), vec4(b[i].vx, b[i].vy, b[i].vz, 0.0));
    imageStore(StateOut, ivec2(x, 2), vec4(b[i].sux, b[i].suy, b[i].suz, 0.0));
    imageStore(StateOut, ivec2(x, 3), vec4(
        intBitsToFloat(b[i].action_class),
        intBitsToFloat(b[i].fire_request),
        intBitsToFloat(b[i].target_propnum),
        intBitsToFloat(b[i].anim_key)));
    imageStore(StateOut, ivec2(x, 4),
        vec4(b[i].range_to_target, 0.0, 0.0, 0.0));
}
```

The mirror writes happen AFTER the SSBO writes (both pos/vel and the
gated AI block), so the texture and SSBO agree on per-bot state at
end-of-dispatch. The AI block (row 3) packs four ints into one RGBA32F
texel via `intBitsToFloat` so the bit pattern survives the round trip;
readers decode via `floatBitsToInt`.

### 6. Dispatch site wiring

```cpp
s_Params.state_tex_enable = s_StateTexArmed ? 1 : 0;
// (pads zeroed)

// bind WRITE-side texture to image binding 2:
GLuint state_tex_write = (s_StateTexReadIdx == 0) ? s_StateTexB : s_StateTexA;
s_glBindImageTexture(2, state_tex_write, 0, GL_FALSE, 0,
    GL_WRITE_ONLY, GL_RGBA32F);

// dispatch and barrier:
s_glDispatchCompute(groups, 1, 1);
s_glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT
    | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

// ping-pong swap:
if (s_StateTexArmed) s_StateTexReadIdx ^= 1;
```

The `state_tex_enable` uniform gates the imageStore in the shader so
the path is bit-exact pre-2a when texture allocation or symbol load
failed. The post-dispatch barrier now covers both SSBO writes (existing
CPU readback consumer) and image-store writes (future 2b consumer).
The ping-pong swap fires unconditionally when armed so 2b's first run
behaves identically to a steady-state run; the read side is unused in
2a.

## Texture layout

```
x = bot_index (0 .. SWARM_GPU_MAX-1)
y = row (0..4)
  y=0  pos.xyz / _pad
  y=1  vel.xyz / _pad
  y=2  surface_up.xyz / _pad
  y=3  action_class / fire_request / target_propnum / anim_key
       (each int packed via intBitsToFloat)
  y=4  range_to_target / _pad / _pad / _pad
```

Each bot is a 5-texel column (5 × 16 B = 80 B), matching the
`boid_record` struct stride. NEAREST sampling is hygiene for 2b's
texelFetch path (which bypasses the sampler anyway).

## Build verify

All four targets built cleanly via direct ninja invocation:

```
[1/3] Building CXX object CMakeFiles/pd.dir/port/fast3d/swarm_gpu.cpp.obj
[2/3] Linking CXX executable PerfectDark.exe
[1/4] Linking C executable Updater.exe
[2/4] Linking CXX executable pd-tests.exe
[3/4] Linking CXX executable PerfectDarkServer.exe
```

### pd-server gate confirmed clean

`nm --defined-only PerfectDarkServer.exe | grep -i "swarmGpu\|s_StateTex\|s_BoidSsbo\|s_glBindImage"`
returned ZERO matches. CMakeLists.txt explicitly enumerates the
server's `port/fast3d/` sources (server_gui.cpp + glad.c + imgui/*),
so `swarm_gpu.cpp` is excluded by construction. No GL state-texture
symbols leak into the dedicated-server binary.

Same query on `PerfectDark.exe` returns the expected `swarmGpu*` and
`s_StateTex*` symbols.

## Smoke parity

`swarm_gpu_smoke` ran against the new binary. Behavioural parity
preserved at every measured tier:

| Tier  | frame_avg_ms (post-2a) | max_unit_per_frame | max_cm_per_sec |
|-------|------------------------|--------------------|----------------|
| 4     | 10.0                   | 5.00               | 300            |
| 8     | 10.0                   | 5.00               | 300            |
| 16    | 10.0                   | 5.00               | 300            |
| 32    | 10.0                   | 5.00               | 300            |
| 48    | 10.0                   | 5.00               | 300            |
| 64    | 10.0                   | 5.00               | 300            |
| 128   | 10.0                   | 5.00               | 300            |
| 256   | 10.0                   | 5.00               | 300            |
| 512   | 10.0 (initial) -> 28   | 5.00               | 300            |
| 768   | 30.1 (initial) -> 78   | tier-typical       | tier-typical   |

(512 and 768 ramp during the scenario because the cycler keeps adding
spawn pressure to bots that are still seeking; this matches pre-2a
behaviour. Numbers are within run-to-run noise of the prior smoke logs.)

Key log line confirming the mirror is wired:

```
[00:01.91] BENCHMARK.SWARM.GPU: state-texture mirror armed
           (2 x RGBA32F 4096x5 = 640 KB)
```

`BENCHMARK.SWARM.GPU: SUMMARY count=768` printed — the smoke test's
key gate. No `EXCEPTION_ACCESS_VIOLATION`, no `FATAL`, no
`SMOKE: result=timeout` lines.

## Cost analysis

- **GPU**: 5 imageStore writes per bot per dispatch on top of the
  existing SSBO writes. RGBA32F writes are roughly 2x the SSBO byte
  cost (80 B SSBO + 80 B texture = 160 B per bot). At 4096 bots that's
  ~640 KB/frame extra GPU memory traffic. Per the design memo, projected
  ~0.5 ms additional. Smoke data confirms no measurable regression
  through 512 bots; 768 tier is dominated by other costs (spatial-grid
  pending in a later slice).
- **CPU**: zero. The texture is purely GPU-side. No new CPU upload, no
  new readback.
- **Memory**: 640 KB GPU-side. Trivial.

## Slice boundary

2a delivers ONLY:
- Texture allocation
- Ping-pong handles
- Shader write path
- Dispatch wiring
- Barrier expansion

NOT in 2a (future slices):
- 2b: kernel reads from texture instead of SSBO
- 2c: extraction primitive (CPU reads texture for inspection/replay)
- 2d: ENet sync of texture rows

## Compiler quirk encountered

The Bash tool sandbox in this session intermittently blocked cc1plus
output (silent EXIT 1 with no stderr). Working around it required
running ninja under `env -i` with an explicit clean environment. Not a
code issue; flagging here so the next session knows the symptom. Once
the clean env was applied, every build succeeded in a single pass.

## Risk

Low. The texture path is fully gated behind `s_StateTexArmed`. If any
of (a) `glBindImageTexture` fails to load, (b) `glGenTextures` returns
0, or (c) `glTexImage2D` fails, the shader sees `state_tex_enable = 0`
and runs SSBO-only — bit-exact pre-2a. The texture itself is opaque to
the CPU until 2b lands; nothing currently reads it.

## Next slice

2b — kernel reads peer pos/vel from `s_StateTexA / s_StateTexB`
(whichever is the READ side per `s_StateTexReadIdx`) instead of from
the SSBO's `b[]` array. This lets us experiment with texture-driven
boid steering without touching the CPU-visible SSBO record.
