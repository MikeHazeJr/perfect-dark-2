/*
 * swarm_sync_quant.h -- Quantized wire format for GPU swarm state sync.
 *
 * Track 2d (c3807, 2026-05-16). Pairs with port/src/net/swarm_sync_quant.c.
 *
 * The raw GPU state texture (port/fast3d/swarm_gpu.cpp) holds 5 RGBA32F
 * rows per bot = 20 floats = 80 bytes per bot. Sent across the wire that
 * would be ~320 KB per 10 Hz broadcast at 4096 bots; the wire format
 * compresses each bot down to 20 bytes (4x reduction; ~80 KB at 4096 bots
 * for the full broadcast, which is chunked across multiple ENet packets).
 *
 * Packed layout per bot (20 bytes; little-endian):
 *
 *   offset  size  field
 *   ------  ----  -----
 *      0     2    pos_x   (s16, cm -- range +/-327.67 m fits arena scale)
 *      2     2    pos_y   (s16, cm)
 *      4     2    pos_z   (s16, cm)
 *      6     2    vel_x   (s16, cm/frame -- max-speed 5.0 u/frame caps at +/-5 cm)
 *      8     2    vel_y   (s16, cm/frame)
 *     10     2    vel_z   (s16, cm/frame)
 *     12     1    up_x    (s8, ratio -- raw_up * 127, range +/-1.0)
 *     13     1    up_y    (s8, ratio)
 *     14     1    up_z    (s8, ratio)
 *     15     1    action_class    (u8, 0=idle 1=seek 2=attack)
 *     16     1    fire_request    (u8, 0/1)
 *     17     2    target_propnum  (u16, low 16 bits)
 *     19     1    anim_key        (u8, 0=stand 1=walk 2=attack)
 *
 * Round-trip tolerance:
 *   - pos: +/-0.5 cm (= 0.5 unit, half a quantization step)
 *   - vel: +/-0.5 cm/frame
 *   - up:  +/-1/127 ~= +/-0.00787 (re-normalised on receive)
 *   - ints: lossless within their dynamic range.
 *
 * Out-of-range pos / vel are SATURATED (clamped to s16 limits). At cm
 * precision that means +/-327 m for pos and +/-327 cm/frame for vel,
 * both well outside any realistic gameplay envelope on PD arenas.
 *
 * pd-server: this module is pure C, no GL / SDL deps. Built into both pd
 * and pd-server so the encode side is always present, even though the
 * dedicated server never originates a broadcast (no GPU compute lives
 * on pd-server). The decode side is also always present so the
 * dispatcher in port/src/net/net.c can read the bytes off the wire
 * without a per-target build guard. The actual GPU texture upload that
 * decoded bytes feed is in port/fast3d/swarm_gpu.cpp (client only).
 */

#ifndef PORT_NET_SWARM_SYNC_QUANT_H
#define PORT_NET_SWARM_SYNC_QUANT_H

#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Wire format constants. Mirrors the per-bot layout documented above. */
#define SWARM_SYNC_QUANT_PACKED_BYTES_PER_BOT  20

/* Encode `count` consecutive bots from raw RGBA32F state-texture float
 * buffer (5 rows * 4 floats = 20 floats per bot) into the caller's packed
 * byte buffer.
 *
 *   raw_rgba32f_buf  -- length must be 20 * count floats (= 80 * count
 *                       bytes). Row order is per the swarmGpuReadback-
 *                       TextureRows contract: pos, vel, surface_up, AI,
 *                       range. AI row's 4 floats are interpreted via
 *                       memcpy as s32 (the GPU shader writes them with
 *                       intBitsToFloat).
 *   count            -- number of bots to encode (>= 0).
 *   out_packed       -- destination byte buffer; length must be
 *                       SWARM_SYNC_QUANT_PACKED_BYTES_PER_BOT * count.
 *
 * Returns number of BYTES written (= SWARM_SYNC_QUANT_PACKED_BYTES_PER_BOT
 * * count). 0 on null-pointer error or negative count.
 *
 * Important: this function takes the FULL 5-row interleaved buffer (the
 * caller is expected to have called swarmGpuReadbackTextureRows with
 * row_start=0, row_count=5).
 */
int swarmSyncQuantEncode(const float *raw_rgba32f_buf, int count,
                         u8 *out_packed);

/* Decode `count` packed bots back into a 5-row RGBA32F float buffer (same
 * layout the GL state texture uses). Lossy on pos / vel / up (see header
 * comment for tolerances). Pad words (the .w channel of each row) are
 * zeroed; the range_to_target row is unused on the receiver and is
 * written as zero.
 *
 *   packed                -- source byte buffer; length must be
 *                            SWARM_SYNC_QUANT_PACKED_BYTES_PER_BOT * count.
 *   count                 -- number of bots to decode (>= 0).
 *   out_rgba32f_buf       -- destination float buffer; length must be
 *                            20 * count floats.
 *
 * Returns number of BOTS decoded (= count) on success, 0 on error.
 */
int swarmSyncQuantDecode(const u8 *packed, int count,
                         float *out_rgba32f_buf);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* PORT_NET_SWARM_SYNC_QUANT_H */
