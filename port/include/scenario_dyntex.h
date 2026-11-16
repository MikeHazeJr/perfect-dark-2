/*
 * scenario_dyntex.h -- animated surfaces of a scenario-source scene.
 *
 * A scene material may name an animation preset (extras.pd2_material.animation
 * in scene.glb). The presets are the seven of dyntex.c, which moves the N64
 * texture coordinates of room vertices every frame: flowing water, a monitor,
 * stepping arrows, the Deep Sea teleport and the Attack Ship power node. The
 * math here is dyntex.c's, applied to one vertex's coordinates, so the scene
 * renderer animates a stage built from scenario source as the legacy room
 * path does. Design: context/designs/modding/animated-surfaces.md.
 */

#ifndef SCENARIO_DYNTEX_H
#define SCENARIO_DYNTEX_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum scenario_dyntex_kind {
	SCENARIO_DYNTEX_NONE = 0,
	SCENARIO_DYNTEX_RIVER,
	SCENARIO_DYNTEX_MONITOR,
	SCENARIO_DYNTEX_OCEAN,
	SCENARIO_DYNTEX_ARROWS,
	SCENARIO_DYNTEX_TELEPORTAL,
	SCENARIO_DYNTEX_POWERRING,
	SCENARIO_DYNTEX_POWERJUICE,
	SCENARIO_DYNTEX_KIND_COUNT
};

/* The preset's name in scene data ("river", "monitor", "ocean", "arrows",
 * "teleportal", "powerring", "powerjuice"); NULL for none or out of range. */
const char *scenarioDyntexKindName(int kind);

/* The preset a name selects, or -1 when the name is not a preset. */
int scenarioDyntexKindFromName(const char *name);

/* What the base game animates: the preset for room texture texnum on the
 * stage stagenum (STAGE_*), from tex.c's texture checks and the stage rules
 * of dyntexSetCurrentType: nothing on Air Base, no river on Investigation or
 * Villa, and the power node presets only on Attack Ship (river elsewhere).
 * SCENARIO_DYNTEX_NONE when the texture stays still. */
int scenarioDyntexBaseKind(int texnum, int stagenum);

/* dyntexTickRoom's first-use shift of one group (one preset in one room),
 * from the group's coordinate range: 0x2000 up when a coordinate is below
 * -0x5d00, 0x2000 down when one is above 0x5d00 (the second rule wins), so
 * the animated values stay within s16. */
void scenarioDyntexGroupShift(int min_s, int max_s, int min_t, int max_t,
	int *add_s, int *add_t);

/* What the presets read each frame. */
typedef struct scenario_dyntex_frame {
	/* g_Lv80SecIntervalFrac: 0 to 1 over 80 seconds. */
	float interval80;
	/* Deep Sea's teleports are on: stage flag 0x100 set and 0x10000 clear. */
	int teleports_on;
	/* Attack Ship's engines are destroyed: stage flag 0x10000. */
	int engines_destroyed;
} scenario_dyntex_frame_t;

/* One vertex this frame. base_s and base_t are its N64 texture coordinates
 * after the group shift. Returns 1 and fills out_s and out_t (the s16 values
 * dyntex.c writes into the vertex) when the preset moves or resets the
 * vertex, 0 when it holds whatever the vertex had (a conditional preset whose
 * condition is off, or none). */
int scenarioDyntexAnimate(int kind, const scenario_dyntex_frame_t *frame,
	int base_s, int base_t, int16_t *out_s, int16_t *out_t);

/* The scene UV of N64 texture coordinates s and t under the room's texture
 * scales (0 to 0xffff), exactly as the extractor converts them:
 * s' = (s * scale_s) >> 16, u = s' / 32 * u_scale,
 * v = 1 - (1 - (1 - t' / 32)) * v_scale. */
void scenarioDyntexUv(int s, int t, unsigned scale_s, unsigned scale_t,
	float u_scale, float v_scale, float *out_u, float *out_v);

#ifdef __cplusplus
}
#endif

#endif
