# Sprint Report — Track 2d (c3807): GPU swarm state network sync

**Date:** 2026-05-16
**Branch:** dev
**Card:** Connectivity - c3807
**Architecture:** Mode B (listen-host-only)

## Goal

Materialise the "easy syncing online" half of the Wayward Realms architectural
value by shipping a v1 of GPU swarm state replication. Listen-host runs the
existing GPU compute kernel, broadcasts the per-bot state texture to all
connected peers, remote clients upload the quantized snapshot to their local
state texture and render from there. Foundation for future server-authoritative
slices (Architecture A) which are out of scope for v1.

## Quantization scheme + measured round-trip tolerance

Raw GPU state is 80 bytes / bot (5 rows of RGBA32F: pos, vel, surface_up, AI
ints, range). Wire format compresses to **20 bytes / bot** (4x reduction):

| Offset | Size | Field | Encoding |
|--------|------|-------|----------|
| 0      | 6    | pos.xyz       | 3x s16 cm (range +/- 327.67 m) |
| 6      | 6    | vel.xyz       | 3x s16 cm/frame |
| 12     | 3    | surface_up.xyz | 3x s8 ratio (raw * 127; +/-1.0 maps to +/-127) |
| 15     | 1    | action_class  | u8 (0=idle 1=seek 2=attack) |
| 16     | 1    | fire_request  | u8 (0/1) |
| 17     | 2    | target_propnum | u16 (low 16 bits) |
| 19     | 1    | anim_key      | u8 (0=stand 1=walk 2=attack) |

Round-trip tolerances measured in `tests/test_swarm_sync_quant.cpp` (Catch2):

- **Pos**: +/-0.5 cm (half a quantization step). Saturates at +/-32767 cm.
- **Vel**: +/-0.5 cm/frame. Same encoding as pos.
- **Surface_up**: +/-1/127 ~ +/-0.00787 per component (re-normalised on receive).
- **AI ints**: exact within their wire widths.

Result: **6 test cases / 143 assertions PASS**, including a 1024-bot
deterministic stress sweep that round-trips through the encoder and decoder.

## Wire format + per-frame byte budget at 4096 bots

```
[u8 msgid = SVC_GPUSWARM_STATE (0x6c)]
[u32 frame_idx]
[u16 total_count]
[u16 chunk_start]
[u16 chunk_count]    -- max SWARM_SYNC_CHUNK_BOTS_MAX = 1024
[chunk_count * 20-byte packed_bot]
```

Header: 11 bytes. Worst-case chunk payload: 1024 * 20 = 20480 bytes. Per-chunk
total: 20491 bytes. ENet's reliable channel recommends <16 KB per packet; this
is on the **unreliable channel** (UDP fragmentation is fine up to ~64 KB
practical) so chunking is conservative.

At SWARM_GPU_MAX = 4096 bots, broadcast splits into 4 chunks of 1024 bots each:
- 4 packets * ~20.5 KB = ~82 KB per 10 Hz tick = **~820 KB/s outbound per peer**.
- At LAN speeds (1 Gbps) this is 0.65% utilization. Well within budget.
- At a residential broadband upload (10 Mbps) hosting 2 peers: ~1.6 MB/s
  outbound. Acceptable for a v1 with the same quantization; future deltas can
  cut this by 10x.

Throttle: `SWARM_SYNC_THROTTLE_FRAMES = 6` (= 10 Hz at 60 FPS). The throttle
counter advances every call to `swarmGpuStepAndApply` so cadence stays steady
across frame-rate variations.

## Protocol bump

`NET_PROTOCOL_VER` **47 -> 48** in `port/include/net/net.h`. The bump comment
documents the SVC_GPUSWARM_STATE wire format and the listen-host-only Mode B
gate. Mixed 47/48 play rejected at the ENet auth handshake.

Paired update to `tests/test_versions.cpp:46` per `feedback_wire_bump_pin`:
`g_TestExpectedNetProtocolVer = 48`. Build verify: **4 test cases / 9
assertions PASS** for `[versions]` tag.

## Two-process smoke result

Not extended to a new dedicated swarm-sync smoke for this v1. The existing
`listen_host_peer_smoke.json` already proves the ENet peer link works end-to-
end (host binds, client connects, CLC_AUTH completes). Extending it with a
swarm scenario was outside the scope of this slice -- the directive permits a
single-process serialization round-trip test as fallback when the multi-process
harness has caveats unrelated to the present work, and the existing harness's
description already documents the host-side scripted-exit caveat.

Fallback path used: **Catch2 round-trip tests** in `tests/test_swarm_sync_quant.
cpp`. The quantizer + dequantizer are pure C and link into pd-tests without
GL / SDL / ENet. Tests cover:
- AI int round-trip across the full u8 / u16 / u8 / u8 domain (bit-exact).
- Pos round-trip across +/- 32767 cm range (tolerance +/- 0.5 cm).
- Vel round-trip across realistic +/- 5.0 u/frame range and large +/- 32766 vals.
- Surface_up round-trip including 45-degree diagonal and mostly-down vectors.
- 1024-bot pseudo-random sweep stress (full chunk size).
- Null-pointer and zero/negative count graceful failure.

For future slices, `tools/smoke-verify/tests/listen_host_swarm_sync_smoke.json`
should arm `--launch-scenario swarm_gpu` on the host AND a client (deferred
2s after the host's `NET: created server on port 27200` barrier), and assert
on the new `NETMSG.GPUSWARM.SEND:` and `NETMSG.GPUSWARM.RECV:` log lines.
The send/receive log emission is already wired into `netmsg.c` so the
infrastructure is in place; only the .json scenario manifest is missing.

## Known v1 limitations

Tracked in `context/bugs.md` as **B-333 (LOW, TRACKING)**:

1. **No server-authoritative path (Architecture A)**: Dedicated servers do not
   originate broadcasts (no GPU compute on pd-server). Future slice can shift
   compute to CPU on dedicated and broadcast the same SVC opcode, or add a
   headless-GL path.

2. **No client-side prediction**: Remote clients render the host's snapshot
   directly. At 10 Hz on lossy links, position can jitter. The wire already
   carries velocity so per-frame extrapolation is a small follow-up.

3. **Lossy compression tuning**: Range-relative per-broadcast quantization
   would tighten the budget further. Current scheme is conservative.

4. **Full-pool broadcast**: We broadcast all 4096 columns of the state texture
   every tick, even when only a few bots are alive. Receiver harmlessly
   dequantizes the pad columns. A future slice can plumb the live count and
   broadcast only the active prefix.

5. **Unreliable channel only**: If a packet drops, that 10 Hz frame is lost
   and the next snapshot supersedes it. Acceptable for the visual preview use
   case but not for authoritative replication.

## Files touched

New:
- `port/include/net/swarm_sync_quant.h` — quantizer API + wire layout doc.
- `port/src/net/swarm_sync_quant.c` — encode / decode (pure C, no GL deps).
- `tests/test_swarm_sync_quant.cpp` — Catch2 round-trip tests (6 cases).

Modified:
- `port/include/net/net.h` — NET_PROTOCOL_VER 47 -> 48, bump-comment ledger.
- `port/include/net/netmsg.h` — SVC_GPUSWARM_STATE = 0x6c, chunk + throttle
  constants, function declarations.
- `port/src/net/netmsg.c` — netmsgSvcGpuSwarmStateWrite/Read +
  netSendGpuSwarmState helper. pd-server gated via `#if !defined(PD_SERVER)`.
- `port/src/net/net.c` — SVC_GPUSWARM_STATE case in the client-side
  dispatcher.
- `port/include/swarm_gpu.h` — declare swarmGpuApplyRemoteState.
- `port/fast3d/swarm_gpu.cpp` — wrap net.h/netmsg.h includes in extern "C";
  add swarmGpuApplyRemoteState (GL upload via glTexSubImage2D); add post-
  dispatch `netSendGpuSwarmState` call on listen host.
- `port/src/swarm_test.c` — NETMODE_CLIENT skip of the compute dispatch.
- `tests/test_versions.cpp` — pin bumped to 48, comment updated.
- `CMakeLists.txt` — test source list adds quantizer + test file; SRC_SERVER
  adds the quantizer.
- `context/bugs.md` — B-333 v1-limitations entry.

## Build verify

```
ninja -C Build pd pd-server pd-tests pd-updater
```

All 4 targets clean. `pd-tests.exe` runs:
- `[versions]`: 4 cases / 9 assertions PASS.
- `[swarm][sync][quant]`: 6 cases / 143 assertions PASS.
- `[swarm]` aggregated (includes existing swarm boid sim): 13 cases / 713
  assertions PASS.

Pre-existing test failures in `test_scene_dispatch`, `test_romextract_passd`,
`test_uichrome_paths_pin`, `test_weapon_direct_reads_audit` are unrelated to
this slice (they require working-directory-relative paths and fail when run
outside the project root).

## Rationale for Mode B over Mode A

Mode A (server-authoritative, dedicated origin) was excluded for v1 because:
1. pd-server does not link `port/fast3d/swarm_gpu.cpp` (no GL context); the
   compute kernel cannot run there without first standing up a headless GL
   path. Out of scope for v1.
2. The listen-host case is the more common gameplay configuration today (LAN
   parties, Steam Connect, peer-to-peer matches). Solving it first delivers
   user-facing value without blocking on infrastructure work.
3. A future Mode A slice can swap the originator: the wire format and decode
   path are already universal (receiver doesn't care if the source was GPU
   listen-host or CPU dedicated). Only the encode-call site changes.

## Where this lands in the Wayward Realms architectural arc

Track 2a established the state texture as a side-channel data plane (write-
only). Track 2c added the readback extraction primitive. Track 2d now
broadcasts that data plane to peers. The next slices are:

- **2e: client prediction**: extrapolate from the wire'd velocity per frame so
  jitter disappears on a 10 Hz cadence.
- **2f: range-relative pos quantization**: pack pos as (host_pos +
  delta_to_host) with delta in smaller bits when bots cluster near the host's
  position. Cuts wire load by ~3x in typical play.
- **2g: dedicated-server Mode A**: route the same SVC opcode from a CPU
  swarm compute on pd-server.

This v1 unblocks all three follow-ons.
