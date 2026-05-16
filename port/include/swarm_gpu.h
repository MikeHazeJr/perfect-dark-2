/*
 * swarm_gpu.h -- Public surface of the GPU swarm benchmark / state
 * extraction layer (port/fast3d/swarm_gpu.cpp).
 *
 * Track 2c (c3807, 2026-05-16): the extraction primitive lives here.
 * Most callers (port/src/swarm_test.c) still forward-declare the older
 * step / invalidate entry points inline; new callers should #include
 * this header instead so the surface is auditable in one place.
 *
 * The extraction API reads rows of the ping-pong RGBA32F state texture
 * that Track 2a (commit 2af02855) wrote alongside the boid SSBO. The
 * texture layout is documented in detail in swarm_gpu.cpp's docblock
 * and re-summarised below.
 */

#ifndef PORT_SWARM_GPU_H
#define PORT_SWARM_GPU_H

#ifdef __cplusplus
extern "C" {
#endif

#include <PR/ultratypes.h>

/* Probe + dispatch entry points (defined in port/fast3d/swarm_gpu.cpp).
 * port/src/swarm_test.c forward-declares these inline today; new
 * callers should #include this header instead. The fully-typed
 * signatures live in the .cpp; the prototypes here intentionally use
 * forward-declared struct types so this header stays light and is
 * safe to include from either C or C++ TUs. */
struct coord;
struct chrdata;

s32  swarmGpuAvailable(void);
void swarmGpuStepAndApply(struct coord *player_pos,
                          struct chrdata **chrs, s32 count,
                          s32 method, s32 player_propnum);
void swarmGpuInvalidateReadback(void);
void swarmGpuInvalidateFloorCache(void);

/* ------------------------------------------------------------------
 * Track 2c (c3807, 2026-05-16): state-texture extraction primitive.
 * ------------------------------------------------------------------
 *
 * Read N consecutive rows from the current READ-side state texture
 * into the caller-supplied float buffer. The state textures are
 * RGBA32F, SWARM_GPU_MAX wide, 5 tall; one bot occupies a 5-texel
 * column. The 5 rows are, in order:
 *   row 0: pos.xyz, _pad
 *   row 1: vel.xyz, _pad
 *   row 2: surface_up.xyz, _pad
 *   row 3: action_class, fire_request, target_propnum, anim_key
 *          (packed via intBitsToFloat in the shader; callers decode
 *          via memcpy into an int32_t).
 *   row 4: range_to_target, _pad, _pad, _pad
 *
 * Arguments:
 *   row_start    -- first row to read, in [0, 5).
 *   row_count    -- how many rows to read; row_start + row_count must
 *                   be <= 5.
 *   out_buf      -- destination float buffer. Each row is
 *                   SWARM_GPU_MAX texels * 4 floats = 16 KB at
 *                   SWARM_GPU_MAX=4096.
 *   out_capacity -- number of FLOATS the buffer can hold. The caller
 *                   is responsible for sizing the buffer >= 4 *
 *                   SWARM_GPU_MAX * row_count.
 *
 * Returns the number of TEXELS actually read on success (=
 * SWARM_GPU_MAX * row_count), 0 on failure or when the GPU swarm
 * texture is not armed (swarmGpuAvailable() == 0, or
 * glBindImageTexture failed to load at probe time, or no compute
 * dispatch has happened yet to populate the textures).
 *
 * pd-server: this entry point is NOT linked into pd-server (the GPU
 * swarm module is client-only by construction; the CMake server
 * target's SRC_SERVER list does not include port/fast3d/swarm_gpu.cpp).
 * Calling this from server code would fail at link time, which is the
 * intended behaviour. */
int swarmGpuReadbackTextureRows(int row_start, int row_count,
                                float *out_buf, int out_capacity);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* PORT_SWARM_GPU_H */
