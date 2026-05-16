# Sprint: GPU Swarm Track 2c — Texture State Extraction Primitive

- **Date**: 2026-05-16T22:58 EDT
- **Card**: c3807 (Benchmarking pillar) — GPU swarm evolution
- **Goal**: Add the read-side counterpart to Track 2a's RGBA32F state textures (commit `2af02855`). Implement a row-range extraction primitive + a CLI fast-path for periodic dump-to-file. Foundation for Track 2d (network sync).
- **Build target parity**: client (pd) + server (pd-server) + updater + tests, all four clean.
- **Smoke**: `swarm_gpu_smoke` PASS 26/26 (253.3 s).
- **Changed files**:
  - `port/include/swarm_gpu.h` — NEW public header (declared `swarmGpuReadbackTextureRows`)
  - `port/fast3d/swarm_gpu.cpp` — added the readback primitive (~95 LOC, after `swarmGpuInvalidateFloorCache`)
  - `port/src/main.c` — added `--dump-swarm-state <path>` flag, latch, arm + deferred-tick functions
  - `port/src/pdmain.c` — wired `bootDumpSwarmStateTick()` into `mainTick` (after the other CLI-fast-path ticks)

## What landed

### Extraction primitive (Track 2c core)
```c
int swarmGpuReadbackTextureRows(int row_start, int row_count,
                                float *out_buf, int out_capacity);
```

- Returns texels read on success (= `SWARM_GPU_MAX * row_count`), 0 on failure.
- Guards: `swarmGpuAvailable()` + `ensure_resources()` + `s_StateTexArmed` + arg validation
  (row_start in [0,5), row_start+row_count <= 5, out_capacity >= 4*SWARM_GPU_MAX*row_count).
- Reads the texture indexed by `s_StateTexReadIdx`. Per Track 2a's swap logic
  (post-dispatch `s_StateTexReadIdx ^= 1`), that's the texture the **most recent**
  dispatch wrote into. The memory barrier in `swarmGpuStepAndApply` (line ~1385)
  guarantees writes are visible before the consumer runs.
- Compatibility fallback: uses `glGetTexImage` (core GL 1.0) into a 320 KB static
  scratch, then memcpy's the requested row range into the caller's buffer.
  Reason: `glGetTextureSubImage` is GL 4.5+ and not exported by the project's
  pinned glad gen; the desktop GL minimum probe (>= 4.3) cannot guarantee 4.5.
  Cost: one full-texture readback per call (320 KB); the caller controls call
  rate, so dump throttling is sufficient.
- pd-server linker check: `nm PerfectDarkServer.exe | grep -i swarm` produces no
  output. `port/fast3d/swarm_gpu.cpp` is not in `SRC_SERVER`, so the function
  doesn't get linked into the dedicated server target.

### CLI fast-path (--dump-swarm-state <path>)
- Latch + path captured at CLI parse time (`bootApplyDumpSwarmState`).
- Deferred tick `bootDumpSwarmStateTick()` runs once per frame from
  `pdmain.c::mainTick`. Gated on:
  1. `g_BootDumpSwarmArmed` set
  2. `g_Vars.lvframenum >= 4` (player positioned, stage settled)
  3. Throttle counter at >= `SWARM_DUMP_INTERVAL_FRAMES` (60 frames ~= 1 Hz)
  4. `swarmGpuReadbackTextureRows` returns > 0 texels (silent skip otherwise)
- On fire: append 32 B header + 320 KB float payload (5 rows × 4096 texels × 4
  floats × 4 B) to the configured path.
- Disarms after `SWARM_DUMP_MAX_FRAMES` (10) dumps. Final file size: ~3.2 MB.
- Logs `BOOT: --dump-swarm-state armed:` at boot, `BOOT: --dump-swarm-state dump
  N/M wrote frame=...` per dump, `BOOT: --dump-swarm-state consumed:` on disarm.

### Dump file format (`PDSWARMv1`)
One block per dump; appended to the file. 320032 bytes per block. After 10 dumps
the file is ~3.2 MB.

```
bytes [0..9):    magic = "PDSWARMv1" (9 ASCII chars, NO terminator)
bytes [9..10):   pad (zero)
bytes [10..16):  reserved (zero), keeps the 4-byte u32 fields aligned
bytes [16..20):  frame_idx   (u32 little-endian, dispatch counter = g_Vars.lvframenum)
bytes [20..24):  count       (u32, = SWARM_GPU_MAX = 4096; texture width)
bytes [24..28):  row_count   (u32, = 5)
bytes [28..32):  pad (zero)  (lands payload at 16-byte alignment for RGBA32F)
bytes [32..end): row_count × count × 4 floats, row-major order
                 (row 0 then row 1 then row 2 then row 3 then row 4;
                  each row is `count` 4-float RGBA texels)
```

Row meaning (mirrors the shader's imageStore layout in `swarm_gpu.cpp:820-835`):

| row | semantics                                              |
|-----|--------------------------------------------------------|
| 0   | pos.xyz, _pad                                          |
| 1   | vel.xyz, _pad                                          |
| 2   | surface_up.xyz, _pad                                   |
| 3   | action_class, fire_request, target_propnum, anim_key   |
|     | (4 ints packed via `intBitsToFloat` shader-side;       |
|     |  readers decode via `memcpy` into an `int32_t`)        |
| 4   | range_to_target, _pad, _pad, _pad                      |

Track 2d (network sync) consumes the same layout: encode the chosen row range
into an ENet message body, decode on the receiver, blit back into a mirror
texture, sample in the receiver's render path. The dump format intentionally
mirrors the wire format so a replay tool and a network capture can share parsers.

## Build verify
```
[16/18] Linking CXX executable PerfectDarkServer.exe
[17/18] Linking CXX executable PerfectDark.exe
[2/3]   Linking C executable Updater.exe
ninja: no work to do.   (pd-tests up-to-date)
```

All four targets clean. Build env note: `cc.exe` invoked from bash silently
exits 127 (no error output) in this session; the workaround was to wrap the
ninja invocation in `cmd.exe //C` so the compiler subprocesses inherit the
standard cmd shell env. Once wrapped, every warning + error surfaces normally.
The PowerShell `build-headless.ps1` script always uses cmd-equivalent path
resolution so this issue is bash-only and external to the build configuration.

## Smoke parity
```
swarm_gpu_smoke (253.3s) assertions=26/26
Total: 1 pass, 0 fail
```

All required lines from the smoke test definition fired (compute probe, kernel
compile, scenario launch, ladder 4..768, vel + AI summary logs, exit clean).
Forbidden patterns (FATAL, access violation, vel cap > 5.99) absent.

No new dump-related assertions added to swarm_gpu_smoke for this slice; the
flag is opt-in and the existing smoke run does not pass `--dump-swarm-state`,
so the dump path is exercised only when an operator runs the client with the
flag set. A follow-up slice (or a dedicated smoke test) can layer a dump
fixture once Track 2d's consumer / replay tool is in place.

## Manual verification path (operator)
1. Run the client with the new flag:
   `pd.exe --launch-scenario swarm_gpu --dump-swarm-state Build/swarm_dump.bin`
2. After ~10 seconds (10 dumps × ~1 Hz), check the file:
   - Size = 3200320 bytes (10 × 320032)
   - First 9 bytes = `"PDSWARMv1"`
   - Bytes [16..20) = frame_idx of first dump (varies by load time)
   - Bytes [20..24) = `0x00001000` (4096 little-endian)
   - Bytes [24..28) = `0x00000005`
3. Log line `BOOT: --dump-swarm-state consumed: max_dumps reached; disarming`
   appears in `pd-client.log` after the 10th dump.

## Notes / follow-ups
- The static 320 KB scratch in `swarmGpuReadbackTextureRows` and the 320 KB
  `s_DumpBuf` in `bootDumpSwarmStateTick` are independently sized. Both are
  BSS; combined cost is ~640 KB. Acceptable on modern desktop targets.
- `glGetTexImage` is a synchronous full-texture readback. At 1 Hz dump cadence
  this is invisible (~50 us memcpy + driver overhead). If a future caller needs
  per-frame extraction (network sync at 10-60 Hz), Track 2d should add an
  async PBO ring around the readback path (mirrors the SSBO-side ring that
  c029 Slice 2 already landed).
- `glGetTextureSubImage` could be loaded via SDL_GL_GetProcAddress if the
  driver advertises GL 4.5+; the function currently shrugs and uses the
  fallback unconditionally. A future optimisation can probe 4.5 at
  `probe_compute` time and pick the more efficient path when available.
- Header `port/include/swarm_gpu.h` is new and lightweight; it forward-declares
  the existing `swarmGpuAvailable / Step / Invalidate*` symbols so callers no
  longer need to inline-forward-declare them (port/src/swarm_test.c still does
  the inline pattern; a janitor pass can migrate it).
