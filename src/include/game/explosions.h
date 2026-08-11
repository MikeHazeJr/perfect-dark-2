#ifndef IN_GAME_EXPLOSIONS_H
#define IN_GAME_EXPLOSIONS_H
#include <ultra64.h>
#include "data.h"
#include "types.h"

#define EXPLOSIONTYPE_BASE_COUNT 26

extern struct explosiontype g_ExplosionTypes[EXPLOSIONTYPE_BASE_COUNT];

/* Public .pdeffect profile libraries replace the base table through this
 * accessor. The authored rows remain owned by the effect executor; callers
 * never cache the returned pointer across catalog lifecycle changes. */
const struct explosiontype *explosionTypeFor(s32 type);
void explosionsSetProfileOverride(const struct explosiontype *rows, s32 count);
void explosionsClearProfileOverride(void);

/* Reserve enough currently-unused, pointer-stable explosion slots for one
 * synchronous all-or-nothing effect transaction. This may grow the PC pool,
 * but never emits gameplay or recycles a live explosion. The reservation is
 * valid until the caller returns to the game loop. */
s32 explosionsReserveCreateCount(s32 count);

void explosionsReset(void);

void explosionsStop(void);

bool explosionCreateSimple(struct prop *prop, struct coord *pos, RoomNum *rooms, s16 type, s32 playernum);
bool explosionCreateComplex(struct prop *prop, struct coord *pos, RoomNum *rooms, s16 type, s32 playernum);
bool explosionCreateComplexWithSound(struct prop *prop, struct coord *pos,
	RoomNum *rooms, s16 type, s32 playernum, s16 soundnum);
bool explosionCreateWithSound(struct prop *sourceprop, struct coord *exppos,
	RoomNum *exprooms, s16 type, s32 playernum, bool makescorch,
	struct coord *scorchpos, RoomNum scorchroom, struct coord *scorchdir,
	s16 soundnum);
f32 explosionGetHorizontalRangeAtFrame(struct explosion *exp, s32 frame);
f32 explosionGetVerticalRangeAtFrame(struct explosion *exp, s32 frame);
void explosionGetBboxAtFrame(struct coord *lower, struct coord *upper, s32 frame, struct prop *prop);
void explosionAlertChrs(const f32 *radius, struct coord *noisepos);
bool explosionCreate(struct prop *prop, struct coord *pos, RoomNum *rooms, s16 type, s32 playernum, bool makescorch, struct coord *arg6, RoomNum room, struct coord *arg8);
void explosionsUpdateShake(struct coord *arg0, struct coord *arg1, struct coord *arg2);
bool explosionOverlapsProp(struct explosion *exp, struct prop *prop, struct coord *pos1, struct coord *pos2);
void explosionInflictDamage(struct prop *prop);
u32 explosionTick(struct prop *prop);
u32 explosionTickPlayer(struct prop *prop);
Gfx *explosionRender(struct prop *prop, Gfx *gdl, bool xlupass);
Gfx *explosionRenderPart(struct explosion *exp, struct explosionpart *part, Gfx *gdl, struct coord *coord, s32 arg4);

#endif
