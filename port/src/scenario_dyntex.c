/*
 * scenario_dyntex.c -- dyntex.c's animated surfaces for one vertex.
 *
 * Everything here mirrors src/game/dyntex.c and the texture checks in
 * src/game/tex.c that pick the animated room textures, so the scene renderer
 * moves a scenario-source stage's water, monitors, arrows, teleport and power
 * node exactly as the legacy room path does. Pure: no game state.
 */

#include <stddef.h>
#include <string.h>

#include "scenario_dyntex.h"

/* The game's own sine and cosine (src/lib/modelasm_c.c, declared in
 * PR/gu.h), which the legacy dyntex.c ripple calls; in the game binary they
 * take the place of the C library's. Plain calls, not builtins, so both
 * paths compute the same ripple. */
extern float sinf(float angle);
extern float cosf(float angle);

/* src/include/math.h: the game's own pi and tau, kept for the ocean ripple. */
#define SCENARIO_DYNTEX_BADPI 3.141092641f
#define SCENARIO_DYNTEX_BADTAU (SCENARIO_DYNTEX_BADPI * 2)

/* src/include/constants.h stage numbers (STAGE_*) the stage rules name. */
#define SCENARIO_DYNTEX_STAGE_AIRBASE       0x27
#define SCENARIO_DYNTEX_STAGE_VILLA         0x2c
#define SCENARIO_DYNTEX_STAGE_INVESTIGATION 0x33
#define SCENARIO_DYNTEX_STAGE_ATTACKSHIP    0x34

static const char *const s_KindNames[SCENARIO_DYNTEX_KIND_COUNT] = {
	NULL,
	"river",
	"monitor",
	"ocean",
	"arrows",
	"teleportal",
	"powerring",
	"powerjuice",
};

const char *scenarioDyntexKindName(int kind)
{
	if (kind <= SCENARIO_DYNTEX_NONE || kind >= SCENARIO_DYNTEX_KIND_COUNT) {
		return NULL;
	}
	return s_KindNames[kind];
}

int scenarioDyntexKindFromName(const char *name)
{
	if (!name) {
		return -1;
	}
	for (int kind = SCENARIO_DYNTEX_NONE + 1; kind < SCENARIO_DYNTEX_KIND_COUNT; kind++) {
		if (strcmp(name, s_KindNames[kind]) == 0) {
			return kind;
		}
	}
	return -1;
}

int scenarioDyntexBaseKind(int texnum, int stagenum)
{
	int kind;

	/* tex.c, texLoadFromGdl: the textures that start an animated group. */
	switch (texnum) {
	case 0x06cb: /* Deep Sea: green river under the floor */
		kind = SCENARIO_DYNTEX_RIVER;
		break;
	case 0x0a6a: /* Deep Sea and Attack Ship: juice in the power node */
		kind = SCENARIO_DYNTEX_POWERJUICE;
		break;
	case 0x0a69: /* Deep Sea and Attack Ship: rings around the power node */
		kind = SCENARIO_DYNTEX_POWERRING;
		break;
	case 0x06e2: /* Deep Sea: teleport */
		kind = SCENARIO_DYNTEX_TELEPORTAL;
		break;
	case 0x01c7: /* Air Base distant water, Investigation puddle */
	case 0x0dae: /* Chicago canal, Villa shallow water, Sewers */
		kind = SCENARIO_DYNTEX_RIVER;
		break;
	case 0x029b: /* Air Force One: monitor */
		kind = SCENARIO_DYNTEX_MONITOR;
		break;
	case 0x090f: /* Villa deep water, Complex water */
		kind = SCENARIO_DYNTEX_OCEAN;
		break;
	case 0x0a42: /* Attack Ship: triangular arrows */
		kind = SCENARIO_DYNTEX_ARROWS;
		break;
	default:
		return SCENARIO_DYNTEX_NONE;
	}

	/* dyntex.c, dyntexSetCurrentType: the stage rules. */
	if (stagenum == SCENARIO_DYNTEX_STAGE_AIRBASE) {
		return SCENARIO_DYNTEX_NONE;
	}
	if ((stagenum == SCENARIO_DYNTEX_STAGE_INVESTIGATION
				|| stagenum == SCENARIO_DYNTEX_STAGE_VILLA)
			&& kind == SCENARIO_DYNTEX_RIVER) {
		return SCENARIO_DYNTEX_NONE;
	}
	if (stagenum != SCENARIO_DYNTEX_STAGE_ATTACKSHIP
			&& (kind == SCENARIO_DYNTEX_POWERJUICE || kind == SCENARIO_DYNTEX_POWERRING)) {
		kind = SCENARIO_DYNTEX_RIVER;
	}
	return kind;
}

void scenarioDyntexGroupShift(int min_s, int max_s, int min_t, int max_t,
	int *add_s, int *add_t)
{
	int adds = 0;
	int addt = 0;

	/* dyntexTickRoom, first use of a type: the later rule wins. */
	if (min_s < -0x5d00) {
		adds = 0x2000;
	}
	if (min_t < -0x5d00) {
		addt = 0x2000;
	}
	if (max_s > 0x5d00) {
		adds = -0x2000;
	}
	if (max_t > 0x5d00) {
		addt = -0x2000;
	}
	if (add_s) {
		*add_s = adds;
	}
	if (add_t) {
		*add_t = addt;
	}
}

/* dyntexUpdateLinear: t moves 10 texture widths of 4096 every 80 seconds. */
static int s_LinearOffset(float interval80)
{
	return (int16_t)((int32_t)(interval80 * 10.0f * 4096.0f) % 4096);
}

int scenarioDyntexAnimate(int kind, const scenario_dyntex_frame_t *frame,
	int base_s, int base_t, int16_t *out_s, int16_t *out_t)
{
	float interval80;
	int s;
	int t;

	if (!frame || !out_s || !out_t) {
		return 0;
	}
	interval80 = frame->interval80;

	switch (kind) {
	case SCENARIO_DYNTEX_RIVER:
		s = base_s;
		t = base_t + s_LinearOffset(interval80);
		break;
	case SCENARIO_DYNTEX_MONITOR: {
		int16_t tmp = (int16_t)((int32_t)(interval80 * 4.0f * 4096.0f) % 4096);
		s = base_s;
		t = base_t - tmp;
		break;
	}
	case SCENARIO_DYNTEX_OCEAN: {
		/* dyntexUpdateOcean: modula and ripsize are u32 there, so the
		 * coordinate is taken modulo 22 as an unsigned value. */
		const uint32_t modula = 22;
		const uint32_t ripsize = 65;
		float phase = interval80 * 5.0f;
		float angle;

		angle = (((uint32_t)base_t % modula) / (float)modula + phase) * SCENARIO_DYNTEX_BADTAU;
		t = base_t + (int16_t)(sinf(angle) * ripsize);
		angle = (((uint32_t)(base_s + 22) % modula) / (float)modula + phase) * SCENARIO_DYNTEX_BADTAU;
		s = base_s + (int16_t)(cosf(angle) * ripsize);
		break;
	}
	case SCENARIO_DYNTEX_ARROWS: {
		int32_t tmp = ((int32_t)((1.0f - interval80) * 60.0f * 8.0f) % 8) * 256;
		s = base_s + tmp;
		t = base_t;
		break;
	}
	case SCENARIO_DYNTEX_TELEPORTAL:
		/* Deep Sea: moves while the teleports are on, holds otherwise. */
		if (!frame->teleports_on) {
			return 0;
		}
		s = base_s;
		t = base_t + s_LinearOffset(interval80);
		break;
	case SCENARIO_DYNTEX_POWERRING:
		/* Attack Ship: moves while the engines run; once they are destroyed
		 * every vertex goes to (0, 0) (dyntexUpdateReset). */
		if (frame->engines_destroyed) {
			*out_s = 0;
			*out_t = 0;
			return 1;
		}
		s = base_s;
		t = base_t + s_LinearOffset(interval80);
		break;
	case SCENARIO_DYNTEX_POWERJUICE:
		/* Attack Ship: moves while the engines run, holds once destroyed. */
		if (frame->engines_destroyed) {
			return 0;
		}
		s = base_s;
		t = base_t + s_LinearOffset(interval80);
		break;
	default:
		return 0;
	}

	/* The vertex's s and t are s16 in the original. */
	*out_s = (int16_t)s;
	*out_t = (int16_t)t;
	return 1;
}

void scenarioDyntexUv(int s, int t, unsigned scale_s, unsigned scale_t,
	float u_scale, float v_scale, float *out_u, float *out_v)
{
	/* romextract_pdarena.c: G_VTX applies the room's texture scale, then
	 * s_bgSceneAddTri divides by 32 and s_bgMaterialGlbUv scales into the
	 * material's UV space. Same operations in the same order, so a vertex
	 * that has not moved lands on the extracted UV exactly. */
	int32_t scaled_s = ((int32_t)s * (int32_t)scale_s) >> 16;
	int32_t scaled_t = ((int32_t)t * (int32_t)scale_t) >> 16;
	float raw_u = (float)scaled_s / 32.0f;
	float raw_v = 1.0f - ((float)scaled_t / 32.0f);

	if (out_u) {
		*out_u = raw_u * u_scale;
	}
	if (out_v) {
		*out_v = 1.0f - ((1.0f - raw_v) * v_scale);
	}
}
