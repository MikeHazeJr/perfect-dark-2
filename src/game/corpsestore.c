/**
 * corpsestore.c -- Frozen corpse store (c132, 2026-07-03).
 * PC campaign body persistence, bake-to-static foundation. See corpsestore.h.
 *
 * A settled solo-campaign corpse is detached from its chr and rendered as a
 * static frozen-pose model each frame, freeing the chr slot. The render reuses
 * the model's real per-node display lists with the frozen matrices (correct
 * textures / combiner), so this is low visual risk; the death pose is preserved
 * because nothing ticks the detached model's animation.
 */
#include <ultra64.h>
#include "constants.h"
#include "game/corpsestore.h"
#include "game/chr.h"
#include "game/camera.h"
#include "game/gfxmemory.h"
#include "bss.h"
#include "lib/memp.h"
#include "lib/model.h"
#include "system.h"

extern s32 sysArgCheck(const char *arg);

#define CORPSE_STORE_MAX 256
/* Off-screen dwell before freezing: guarantees the death animation has settled
 * and the (harmless) live-chr -> static-model handoff happens out of view. */
#define CORPSE_FREEZE_OFFSCREEN_DELAY TICKS(30)

typedef struct corpse_record {
	struct model *model;    /* detached; MEMPOOL_STAGE-lived rwdatas + frozen anim */
	struct coord pos;       /* death-frame world position */
	f32 yrot;               /* death-frame yaw */
	RoomNum rooms[8];       /* room membership for culling (-1 terminated) */
	u32 envcolour;          /* snapshot render lighting (static corpse) */
	u32 fogcolour;
	s32 unk30;
	s16 nummatrices;
	u8 occupied;
} corpse_record_t;

static corpse_record_t *s_Corpses = NULL;
static s32 s_CorpseCount = 0;
static bool s_Enabled = false;

void corpseStoreSetEnabled(bool enabled)
{
	s_Enabled = enabled;
}

bool corpseStoreIsEnabled(void)
{
	return s_Enabled;
}

s32 corpseStoreGetCount(void)
{
	return s_CorpseCount;
}

void corpseStoreReset(void)
{
	/* MEMPOOL_STAGE is wiped on stage load, so the array (and every detached
	 * model it referenced) is gone; just re-acquire and clear the bookkeeping. */
	s_Corpses = mempAlloc(ALIGN16(sizeof(corpse_record_t) * CORPSE_STORE_MAX), MEMPOOL_STAGE);
	s_CorpseCount = 0;
	if (s_Corpses) {
		s32 i;
		for (i = 0; i < CORPSE_STORE_MAX; i++) {
			s_Corpses[i].occupied = false;
		}
	}
	/* Active by default (Mike, 2026-07-03): solo-campaign corpses freeze into
	 * the store. --no-campaign-corpse-bake opts out to the OG / headroom-cap
	 * path if a corpse issue is ever seen in play. Re-read each stage load. */
	s_Enabled = (sysArgCheck("--no-campaign-corpse-bake") == 0);
}

bool corpseStoreShouldFreeze(struct chrdata *chr)
{
	if (!s_Enabled || s_Corpses == NULL) {
		return false;
	}
	/* Solo campaign only (MP keeps OG fading; the firing range recycles). */
	if (g_Vars.normmplayerisrunning || g_Vars.stagenum == STAGE_CITRAINING) {
		return false;
	}
	if (chr == NULL || chr->model == NULL || chr->prop == NULL) {
		return false;
	}
	if (chr->aibot != NULL) {
		return false;
	}
	if (chr->actiontype != ACT_DEAD) {
		return false;
	}
	/* Not currently fading (fadetimer60 < 0), and off-screen long enough that
	 * the death anim has settled and the handoff is invisible. */
	if (chr->act_dead.fadetimer60 >= 0) {
		return false;
	}
	if (chr->act_dead.invistimer60 < CORPSE_FREEZE_OFFSCREEN_DELAY) {
		return false;
	}
	return true;
}

bool corpseStoreFreeze(struct chrdata *chr)
{
	corpse_record_t *rec = NULL;
	struct model *model;
	s32 i;

	if (!s_Enabled || s_Corpses == NULL || chr == NULL || chr->model == NULL
			|| chr->prop == NULL || chr->model->definition == NULL) {
		return false;
	}

	/* Find a free slot. Full store -> leave this corpse on the OG path. */
	for (i = 0; i < CORPSE_STORE_MAX; i++) {
		if (!s_Corpses[i].occupied) {
			rec = &s_Corpses[i];
			break;
		}
	}
	if (rec == NULL) {
		return false;
	}

	model = chr->model;

	rec->model = model;
	rec->pos = chr->prop->pos;
	rec->nummatrices = model->definition->nummatrices;
	rec->occupied = true;

	/* Snapshot room membership for cull. */
	for (i = 0; i < 8; i++) {
		rec->rooms[i] = chr->prop->rooms[i];
		if (chr->prop->rooms[i] == -1) {
			break;
		}
	}
	for (; i < 8; i++) {
		rec->rooms[i] = -1;
	}

	/* Snapshot the frozen yaw + render lighting from the model's chrinfo rwdata
	 * and the chr shade colour. The corpse is static so this lighting is fixed;
	 * dynamic room-light changes are a later refinement. */
	{
		union modelrwdata *rw = modelGetNodeRwData(model, model->definition->rootnode);
		rec->yrot = rw ? rw->chrinfo.yrot : 0.0f;
	}
	rec->fogcolour = ((u32)chr->shadecol[0] << 24) | ((u32)chr->shadecol[1] << 16)
		| ((u32)chr->shadecol[2] << 8) | (u32)chr->shadecol[3];
	rec->envcolour = 0;
	rec->unk30 = 7;

	/* Drop the model's back-reference to the soon-freed chr (model.c never
	 * derefs it, but do not leave it dangling). chr->model is deliberately left
	 * valid so every tick/render between here and the reap stays NULL-safe; the
	 * reap (chrRemove) consults corpseStoreOwnsModel and SKIPS freeing this
	 * model, handing it to the store. The model's rwdatas live in MEMPOOL_STAGE
	 * and survive; nothing ticks the detached anim, so the pose stays frozen. */
	model->chr = NULL;

	/* Reuse the proven safe reap: it returns the chr slot to the pool and nulls
	 * chr->model itself, but leaves this (corpse-owned) model allocated. */
	chr->hidden |= CHRHFLAG_DELETING;

	s_CorpseCount++;
	sysLogPrintf(LOG_NOTE,
		"CORPSE.FREEZE: stored corpse #%d bodynum=%d pos=(%d,%d,%d) matrices=%d",
		s_CorpseCount, (s32)chr->bodynum,
		(s32)rec->pos.x, (s32)rec->pos.y, (s32)rec->pos.z, (s32)rec->nummatrices);
	return true;
}

bool corpseStoreOwnsModel(struct model *model)
{
	s32 i;
	if (model == NULL || s_Corpses == NULL || s_CorpseCount == 0) {
		return false;
	}
	for (i = 0; i < CORPSE_STORE_MAX; i++) {
		if (s_Corpses[i].occupied && s_Corpses[i].model == model) {
			return true;
		}
	}
	return false;
}

static bool corpseRoomVisible(const corpse_record_t *rec, RoomNum roomnum)
{
	s32 i;
	for (i = 0; i < 8; i++) {
		if (rec->rooms[i] == -1) {
			return false;
		}
		if (rec->rooms[i] == roomnum) {
			return true;
		}
	}
	return false;
}

Gfx *corpseStoreRenderRoom(Gfx *gdl, RoomNum roomnum)
{
	s32 i;

	if (!s_Enabled || s_Corpses == NULL || s_CorpseCount == 0) {
		return gdl;
	}

	for (i = 0; i < CORPSE_STORE_MAX; i++) {
		corpse_record_t *rec = &s_Corpses[i];
		struct model *model;

		if (!rec->occupied || rec->model == NULL) {
			continue;
		}
		if (!corpseRoomVisible(rec, roomnum)) {
			continue;
		}

		model = rec->model;
		if (model->definition == NULL || rec->nummatrices <= 0) {
			continue;
		}

		/* Write the frozen world placement into the chrinfo rwdata so the matrix
		 * builder positions the corpse at its death spot regardless of camera. */
		{
			union modelrwdata *rw = modelGetNodeRwData(model, model->definition->rootnode);
			if (rw) {
				rw->chrinfo.pos = rec->pos;
				rw->chrinfo.yrot = rec->yrot;
			}
		}

		/* Opaque, z-buffered static render, mirroring chrRender's body draw.
		 * Zero-init so no field is left uninitialised (cf. B-253b UB). Frozen
		 * anim -> frozen pose; real display lists -> correct materials. */
		{
			struct modelrenderdata rd = {0};
			rd.unk00 = camGetWorldToScreenMtxf();
			rd.zbufferenabled = true;
			rd.flags = MODELRENDERFLAG_OPA;
			rd.gdl = gdl;
			rd.unk10 = gfxAllocate(rec->nummatrices * sizeof(Mtxf));
			rd.unk30 = rec->unk30;
			rd.envcolour = rec->envcolour;
			rd.fogcolour = rec->fogcolour;
			rd.cullmode = 0;

			if (rd.unk10 == NULL) {
				continue;
			}

			model->matrices = NULL;
			modelSetMatricesWithAnim(&rd, model);
			modelRender(&rd, model);

			gdl = rd.gdl;
		}
	}

	return gdl;
}
