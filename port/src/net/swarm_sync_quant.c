/*
 * swarm_sync_quant.c -- Quantized wire format encode/decode for GPU swarm
 * state sync.
 *
 * Track 2d (c3807, 2026-05-16). See header for layout + tolerances.
 *
 * The raw 5-row RGBA32F state-texture buffer is interleaved across all
 * bots BY ROW: floats [0 .. count*4) are pos rows for bots 0..count-1;
 * floats [count*4 .. 2*count*4) are vel rows; etc. This matches the
 * extraction primitive in port/fast3d/swarm_gpu.cpp (one row of texels
 * is one row of bots' field).
 *
 * Row layout (each row is `count` texels * 4 floats):
 *   row 0  pos.xyz    (vec3 + pad)
 *   row 1  vel.xyz    (vec3 + pad)
 *   row 2  surface_up.xyz (vec3 + pad)
 *   row 3  action_class, fire_request, target_propnum, anim_key
 *          (packed via intBitsToFloat in the shader; we memcpy to s32)
 *   row 4  range_to_target, _pad, _pad, _pad
 *
 * No-allocations. Pure-C. Suitable for linking into both pd and pd-server.
 */

#include <string.h>
#include "net/swarm_sync_quant.h"

/* Per-row stride in floats (4 floats per texel; one row per field). */
#define ROW_STRIDE_FLOATS 4

/* Quantization helpers. The s16-cm scheme assumes 1 game unit ~= 1 cm
 * (PD-PC convention), so we just round-to-nearest the float value.
 * Saturate to s16 range -- out-of-range readings are clamped, but in
 * practice the arenas keep pos within +/-300 m and vel within +/-10
 * units/frame, so saturation will not trigger in steady state. */
static s16 quant_pos_to_s16(float v)
{
	if (v >= 32767.0f) return 32767;
	if (v <= -32768.0f) return -32768;
	/* Round-to-nearest. Avoid floorf to keep pd-tests's minimal libc
	 * link surface narrow; the +/-0.5 trick rounds toward zero for
	 * negatives which is fine for symmetric quantization error. */
	return (s16)((v >= 0.0f) ? (v + 0.5f) : (v - 0.5f));
}

static float dequant_pos_from_s16(s16 v)
{
	return (float)v;
}

/* Vel uses the same s16-cm encoding as pos. Vel magnitude is dominated
 * by max_speed (5.0 units/frame) -- well within s16's range. */
static s16 quant_vel_to_s16(float v)
{
	return quant_pos_to_s16(v);
}

static float dequant_vel_from_s16(s16 v)
{
	return (float)v;
}

/* Surface-up is a unit vector. Encode as signed-byte ratios (+/-127); the
 * receiver re-normalises. -128 is left unused so the encoding is
 * symmetric and round-trip is exact within 1/127. */
static s8 quant_up_to_s8(float v)
{
	float scaled = v * 127.0f;
	if (scaled >= 127.0f)  return 127;
	if (scaled <= -127.0f) return -127;
	return (s8)((scaled >= 0.0f) ? (scaled + 0.5f) : (scaled - 0.5f));
}

static float dequant_up_from_s8(s8 v)
{
	return (float)v / 127.0f;
}

int swarmSyncQuantEncode(const float *raw_rgba32f_buf, int count, u8 *out_packed)
{
	if (raw_rgba32f_buf == NULL || out_packed == NULL) return 0;
	if (count < 0) return 0;
	if (count == 0) return 0;

	const float *row_pos  = raw_rgba32f_buf + 0 * count * ROW_STRIDE_FLOATS;
	const float *row_vel  = raw_rgba32f_buf + 1 * count * ROW_STRIDE_FLOATS;
	const float *row_up   = raw_rgba32f_buf + 2 * count * ROW_STRIDE_FLOATS;
	const float *row_ai   = raw_rgba32f_buf + 3 * count * ROW_STRIDE_FLOATS;
	/* row_range (row 4) ignored on the wire. */

	for (int i = 0; i < count; i++) {
		const float *p  = row_pos + i * ROW_STRIDE_FLOATS;
		const float *v  = row_vel + i * ROW_STRIDE_FLOATS;
		const float *u  = row_up  + i * ROW_STRIDE_FLOATS;
		const float *a4 = row_ai  + i * ROW_STRIDE_FLOATS;

		s16 px = quant_pos_to_s16(p[0]);
		s16 py = quant_pos_to_s16(p[1]);
		s16 pz = quant_pos_to_s16(p[2]);
		s16 vx = quant_vel_to_s16(v[0]);
		s16 vy = quant_vel_to_s16(v[1]);
		s16 vz = quant_vel_to_s16(v[2]);
		s8  ux = quant_up_to_s8(u[0]);
		s8  uy = quant_up_to_s8(u[1]);
		s8  uz = quant_up_to_s8(u[2]);

		/* AI fields decoded from the float row's bit pattern. The
		 * shader writes them via intBitsToFloat so the bytes ARE the
		 * s32 we want; memcpy to avoid strict-aliasing UB. */
		s32 ai_iv[4];
		memcpy(ai_iv, a4, sizeof(ai_iv));
		u8  ac     = (u8)(ai_iv[0] & 0xFF);
		u8  fr     = (u8)(ai_iv[1] & 0xFF);
		u16 tprop  = (u16)(ai_iv[2] & 0xFFFF);
		u8  ak     = (u8)(ai_iv[3] & 0xFF);

		u8 *dst = out_packed + i * SWARM_SYNC_QUANT_PACKED_BYTES_PER_BOT;
		/* Little-endian 16-bit writes -- pack manually so byte order is
		 * portable across hosts (we never run on big-endian today, but
		 * the wire convention is little-endian per the rest of netbuf). */
		dst[0]  = (u8)(px & 0xFF);
		dst[1]  = (u8)((px >> 8) & 0xFF);
		dst[2]  = (u8)(py & 0xFF);
		dst[3]  = (u8)((py >> 8) & 0xFF);
		dst[4]  = (u8)(pz & 0xFF);
		dst[5]  = (u8)((pz >> 8) & 0xFF);
		dst[6]  = (u8)(vx & 0xFF);
		dst[7]  = (u8)((vx >> 8) & 0xFF);
		dst[8]  = (u8)(vy & 0xFF);
		dst[9]  = (u8)((vy >> 8) & 0xFF);
		dst[10] = (u8)(vz & 0xFF);
		dst[11] = (u8)((vz >> 8) & 0xFF);
		dst[12] = (u8)ux;
		dst[13] = (u8)uy;
		dst[14] = (u8)uz;
		dst[15] = ac;
		dst[16] = fr;
		dst[17] = (u8)(tprop & 0xFF);
		dst[18] = (u8)((tprop >> 8) & 0xFF);
		dst[19] = ak;
	}

	return count * SWARM_SYNC_QUANT_PACKED_BYTES_PER_BOT;
}

int swarmSyncQuantDecode(const u8 *packed, int count, float *out_rgba32f_buf)
{
	if (packed == NULL || out_rgba32f_buf == NULL) return 0;
	if (count < 0) return 0;
	if (count == 0) return 0;

	float *row_pos   = out_rgba32f_buf + 0 * count * ROW_STRIDE_FLOATS;
	float *row_vel   = out_rgba32f_buf + 1 * count * ROW_STRIDE_FLOATS;
	float *row_up    = out_rgba32f_buf + 2 * count * ROW_STRIDE_FLOATS;
	float *row_ai    = out_rgba32f_buf + 3 * count * ROW_STRIDE_FLOATS;
	float *row_range = out_rgba32f_buf + 4 * count * ROW_STRIDE_FLOATS;

	for (int i = 0; i < count; i++) {
		const u8 *src = packed + i * SWARM_SYNC_QUANT_PACKED_BYTES_PER_BOT;

		s16 px = (s16)((u16)src[0] | ((u16)src[1] << 8));
		s16 py = (s16)((u16)src[2] | ((u16)src[3] << 8));
		s16 pz = (s16)((u16)src[4] | ((u16)src[5] << 8));
		s16 vx = (s16)((u16)src[6] | ((u16)src[7] << 8));
		s16 vy = (s16)((u16)src[8] | ((u16)src[9] << 8));
		s16 vz = (s16)((u16)src[10] | ((u16)src[11] << 8));
		s8  ux = (s8)src[12];
		s8  uy = (s8)src[13];
		s8  uz = (s8)src[14];
		u8  ac    = src[15];
		u8  fr    = src[16];
		u16 tprop = (u16)((u16)src[17] | ((u16)src[18] << 8));
		u8  ak    = src[19];

		float *p = row_pos + i * ROW_STRIDE_FLOATS;
		float *v = row_vel + i * ROW_STRIDE_FLOATS;
		float *u = row_up  + i * ROW_STRIDE_FLOATS;
		float *a = row_ai  + i * ROW_STRIDE_FLOATS;
		float *r = row_range + i * ROW_STRIDE_FLOATS;

		p[0] = dequant_pos_from_s16(px);
		p[1] = dequant_pos_from_s16(py);
		p[2] = dequant_pos_from_s16(pz);
		p[3] = 0.0f;
		v[0] = dequant_vel_from_s16(vx);
		v[1] = dequant_vel_from_s16(vy);
		v[2] = dequant_vel_from_s16(vz);
		v[3] = 0.0f;
		u[0] = dequant_up_from_s8(ux);
		u[1] = dequant_up_from_s8(uy);
		u[2] = dequant_up_from_s8(uz);
		u[3] = 0.0f;

		/* AI: pack the u8/u16 fields back into s32 word bit-patterns and
		 * memcpy through float (mirrors the shader's intBitsToFloat
		 * encoding so a hypothetical glTexSubImage upload of this buffer
		 * yields the same per-bot AI values when sampled by the
		 * shader). */
		s32 ai_iv[4];
		ai_iv[0] = (s32)ac;
		ai_iv[1] = (s32)fr;
		ai_iv[2] = (s32)tprop;
		ai_iv[3] = (s32)ak;
		memcpy(a, ai_iv, sizeof(ai_iv));

		/* range_to_target is not on the wire -- receiver doesn't need it
		 * (only the host sends fire commands today). Zero for hygiene. */
		r[0] = 0.0f;
		r[1] = 0.0f;
		r[2] = 0.0f;
		r[3] = 0.0f;
	}

	return count;
}
