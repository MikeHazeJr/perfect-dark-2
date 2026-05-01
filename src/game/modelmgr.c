#include <ultra64.h>
#include "constants.h"
#include "game/modelmgr.h"
#include "bss.h"
#include "lib/memp.h"
#include "lib/model.h"
#include "data.h"
#include "types.h"

#include "system.h"

struct model *g_ModelSlots;
struct anim *g_AnimSlots;
s32 g_ModelNumObjs;
s32 g_ModelNumChrs;
struct modelrwdatabinding *g_ModelRwdataBindings[3];

s32 g_MaxModels = 0;
s32 g_MaxAnims = 0;
bool g_ModelIsLvResetting = false;
s32 g_ModelMostType1 = 0;
s32 g_ModelMostType2 = 0;
s32 g_ModelMostType3 = 0;
s32 g_ModelMostModels = 0;
s32 g_ModelMostAnims = 0;

/*
 * PC port: Increase rwdata binding pools to support up to 32 simultaneous
 * characters (MAX_MPCHRS). Original N64 values were 35/25/20 which only
 * supported ~12 active character models before type 3 slots exhausted.
 *
 * Type 1: small rwdata (<=4 words / 0x10 bytes) - props, simple objects
 * Type 2: medium rwdata (<=52 words / 0xD0 bytes) - weapons, animated objects
 * Type 3: large rwdata (<=256 words / 0x400 bytes) - character body models
 */
/* PC port: pool sizes selected to fit the heaviest concurrent-chr workload
 * the port supports.
 *
 * Originally NUMTYPE1=70 / NUMTYPE2=50 / NUMTYPE3=48 (N64 budget). The
 * Swarm benchmark scenario (S483) spawns up to 256 Skedars at once, each
 * needing a Type 2 chrinfo binding (rwdata ~= 0xd0 bytes). 50 was wildly
 * insufficient: at >50 simultaneously alive chrs, modelmgr falls back to
 * Type 3 dynamic alloc which itself caps at NUMTYPE3=48, so beyond ~98
 * chrs the next bodyAllocateModel returns NULL or the partial init goes
 * on to render incorrectly -- exactly Mike's "bots go invisible after a
 * few count cycles" symptom.
 *
 * The cap is shared with normal gameplay; bumping is safe because the
 * arrays are sized once at level init (mempAlloc out of MEMPOOL_STAGE).
 * Cost at 320 type2: 320 * 0xd0 = 53.25 KB rwdata + 320 * sizeof(binding)
 * for the binding table -- negligible on PC.
 *
 * Sizing margin: 256 (swarm cap) + 32 (MAX_BOTS) + 16 (NPCs in CO/AT
 * stages worst case) + 16 headroom = 320.  See also S483c F.2 for the
 * earlier 50 -> 50 "no change" decision; that was a sizing miss for the
 * benchmark workload and is corrected here.
 */
#define NUMTYPE1() 80
#define NUMTYPE2() 320
/* NUMTYPE3 holds chr body models (rwdatalen up to 256 words / 1024 bytes,
 * or 384 words / 1536 bytes on 64-bit due to the +128-word `extra` field
 * in modelmgrInstantiateModel). Skedar has rwdatalen=330 words which
 * lands here. The S593 bump 48 -> 64 was insufficient for the 256-bot
 * swarm: the playtest log at cycle 128 showed "All rwdata binding pools
 * exhausted ... heap fallback" warnings firing repeatedly because each
 * over-cap chr fell through to mempAlloc. The heap fallback works but
 * leaks across cycles (mempAlloc memory is stage-pool, not freed by
 * chrRemove), so cycling 256 -> 4 -> 256 multiple times exhausted
 * MEMPOOL_STAGE and crashed the next bodyAllocateModel. Bumping
 * NUMTYPE3 to 320 matches NUMTYPE2 and fits the full 256-bot swarm
 * within static bindings. Cost: 320 * (256+128 words * 4 bytes/word)
 * = ~492 KB rwdata. Acceptable for the PC build's stage-pool size. */
#define NUMTYPE3() 320

bool modelmgrCanSlotFitRwdata(struct model *modelslot, struct modeldef *modeldef)
{
	return modeldef->rwdatalen <= 0
		|| (modelslot->rwdatas != NULL && modelslot->rwdatalen >= modeldef->rwdatalen);
}

void modelmgrPrintCounts(void)
{
	s32 i;
	s32 numtype1 = 0;
	s32 numtype2 = 0;
	s32 numtype3 = 0;
	s32 nummodels = 0;
	s32 numanims = 0;

	for (i = 0; i < NUMTYPE1(); i++) {
		if (g_ModelRwdataBindings[0][i].model) {
			numtype1++;
		}
	}

	for (i = 0; i < NUMTYPE2(); i++) {
		if (g_ModelRwdataBindings[1][i].model) {
			numtype2++;
		}
	}

	for (i = 0; i < NUMTYPE3(); i++) {
		if (g_ModelRwdataBindings[2][i].model) {
			numtype3++;
		}
	}

	for (i = 0; i < g_MaxModels; i++) {
		if (g_ModelSlots[i].definition) {
			nummodels++;
		}
	}

	for (i = 0; i < g_MaxAnims; i++) {
		if (g_AnimSlots[i].animnum != -1) {
			numanims++;
		}
	}

	if (numtype1 > g_ModelMostType1) {
		g_ModelMostType1 = numtype1;
	}

	if (numtype2 > g_ModelMostType2) {
		g_ModelMostType2 = numtype2;
	}

	if (numtype3 > g_ModelMostType3) {
		g_ModelMostType3 = numtype3;
	}

	if (nummodels > g_ModelMostModels) {
		g_ModelMostModels = nummodels;
	}

	if (numanims > g_ModelMostAnims) {
		g_ModelMostAnims = numanims;
	}

	osSyncPrintf("MOT : Type 1  = %d/%d (%d)");
	osSyncPrintf("MOT : Type 2  = %d/%d (%d)");
	osSyncPrintf("MOT : Type 3  = %d/%d (%d)");
	osSyncPrintf("MOT : Type OI = %d/%d/%d/%d");
	osSyncPrintf("MOT : Type OA = %d/%d/%d/%d");
	osSyncPrintf("MOT : g_ObjCount = %d");
	osSyncPrintf("MOT : g_AnimCount = %d");
}

struct model *modelmgrInstantiateModel(struct modeldef *modeldef, bool withanim)
{
	/* PC: Guard against NULL modeldef (B-20 fix).
	 * This can happen when a character body fails to load from ROM
	 * (e.g., objective completion triggers spawning with an invalid filenum). */
	if (!modeldef) {
		sysLogPrintf(LOG_ERROR, "MODELMGR: NULL modeldef in modelmgrInstantiateModel -- returning NULL");
		return NULL;
	}

	struct model *model = NULL;
	u32 *rwdatas = NULL;
	s16 datalen = -1;
	s16 extra = 0;
#ifdef PLATFORM_64BIT
	extra = 128;
#endif
	s32 i;

	if (!g_ModelIsLvResetting) {
		// If it's being allocated mid-gameplay, look through all slots
		// and find any slot that's big enough.
		for (i = 0; i < g_MaxModels; i++) {
			if (g_ModelSlots[i].definition == NULL && modelmgrCanSlotFitRwdata(&g_ModelSlots[i], modeldef)) {
				model = &g_ModelSlots[i];
				rwdatas = g_ModelSlots[i].rwdatas;
				datalen = g_ModelSlots[i].rwdatalen;
				break;
			}
		}
	}

	if (model == NULL) {
		// This is lv reset, or gameplay when a suitable slot can't be found

		// Find any spare slot or allocate a new one
		for (i = 0; i < g_MaxModels; i++) {
			if (g_ModelSlots[i].definition == NULL) {
				model = &g_ModelSlots[i];
				break;
			}
		}

		if (model == NULL) {
			osSyncPrintf("Allocating %d bytes for objinst structure\n", ALIGN16(sizeof(struct model)));
			model = mempAlloc(ALIGN16(sizeof(struct model)), MEMPOOL_STAGE);
		}

		if (g_ModelIsLvResetting) {
			if (modeldef->rwdatalen > 0) {
				datalen = modeldef->rwdatalen;
				rwdatas = mempAlloc(ALIGN16(datalen * 4), MEMPOOL_STAGE);
			}
		} else {
			// At this point, it's during gameplay. A model instance slot has
			// been found or allocated, but rwdata needs to be allocated.
			if (modeldef->rwdatalen < 256+extra) {
				bool done = false;
				u32 stack;

				// 4 words (0x10 bytes) or less -> try type 1
				if (modeldef->rwdatalen <= 4) {
					for (i = 0; i < NUMTYPE1(); i++) {
						if (g_ModelRwdataBindings[0][i].model == NULL) {
							osSyncPrintf("MotInst: Using cache entry type 1 %d (0x%08x) - Bytes=%d\n");
							rwdatas = g_ModelRwdataBindings[0][i].rwdata;
							g_ModelRwdataBindings[0][i].model = model;
							done = true;
							break;
						}
					}
				}

				// 52 words (0xd0 bytes) or less -> try type 2
				if (!done && modeldef->rwdatalen <= 52) {
					for (i = 0; i < NUMTYPE2(); i++) {
						if (g_ModelRwdataBindings[1][i].model == NULL) {
							osSyncPrintf("MotInst: Using cache entry type 2 %d (0x%08x) - Bytes=%d\n");
							rwdatas = g_ModelRwdataBindings[1][i].rwdata;
							g_ModelRwdataBindings[1][i].model = model;
							done = true;
							break;
						}
					}
				}

				// 256 words (0x400 bytes) or less -> try type 3
				// First looking for unused slots with an existing rwdata allocation
				if (!done && modeldef->rwdatalen <= 256+extra) {
					for (i = 0; i < NUMTYPE3(); i++) {
						if (g_ModelRwdataBindings[2][i].model == NULL && g_ModelRwdataBindings[2][i].rwdata != NULL) {
							osSyncPrintf("MotInst: Using cache entry type 3 %d (0x%08x) - Bytes=%d\n");
							rwdatas = g_ModelRwdataBindings[2][i].rwdata;
							g_ModelRwdataBindings[2][i].model = model;
							done = true;
							break;
						}
					}
				}

				// Type 3 again, but looking for null rwdata allocations
				if (!done && modeldef->rwdatalen <= 256+extra) {
					for (i = 0; i < NUMTYPE3(); i++) {
						if (g_ModelRwdataBindings[2][i].model == NULL && g_ModelRwdataBindings[2][i].rwdata == NULL) {
							g_ModelRwdataBindings[2][i].rwdata = mempAlloc((256+128) * 4, MEMPOOL_STAGE);
							rwdatas = g_ModelRwdataBindings[2][i].rwdata;
							g_ModelRwdataBindings[2][i].model = model;
							break;
						}
					}
				}
			} else {
				sysLogPrintf(LOG_WARNING, "MODELMGR: rwdatalen=%d exceeds max type 3 capacity (%d words) - cannot use binding pools",
					modeldef->rwdatalen, 256+extra);
			}

			if (rwdatas == NULL && modeldef->rwdatalen > 0 && modeldef->rwdatalen < 256+extra) {
				sysLogPrintf(LOG_WARNING, "MODELMGR: All rwdata binding pools exhausted (type1=%d type2=%d type3=%d) for rwdatalen=%d - heap fallback",
					NUMTYPE1(), NUMTYPE2(), NUMTYPE3(), modeldef->rwdatalen);
			}

			datalen = 256;

			datalen += extra;

			if (datalen < modeldef->rwdatalen) {
				datalen = modeldef->rwdatalen;
			}

			if (rwdatas == NULL) {
				rwdatas = mempAlloc(ALIGN16(datalen * 4), MEMPOOL_STAGE);
			}
		}
	}

	if (model) {
		if (withanim) {
			model->anim = modelmgrInstantiateAnim();

			if (model->anim) {
				animInit(model->anim);
			} else {
				modelmgrFreeModel(model);
				model = NULL;
			}
		} else {
			model->anim = NULL;
		}
	}

	if (model) {
		modelInit(model, modeldef, rwdatas, false);
		model->rwdatalen = datalen;
	}

	osSyncPrintf("***************************************\n");
	osSyncPrintf("***************************************\n");

	return model;
}

struct model *modelmgrInstantiateModelWithoutAnim(struct modeldef *modeldef)
{
	return modelmgrInstantiateModel(modeldef, false);
}

void modelmgrFreeModel(struct model *model)
{
	bool done = false;
	s32 i;

	for (i = 0; i < NUMTYPE1(); i++) {
		if (g_ModelRwdataBindings[0][i].model == model) {
			g_ModelRwdataBindings[0][i].model = NULL;

			model->rwdatas = NULL;
			model->rwdatalen = -1;

			done = true;
			break;
		}
	}

	if (!done) {
		for (i = 0; i < NUMTYPE2(); i++) {
			if (g_ModelRwdataBindings[1][i].model == model) {
				osSyncPrintf("\nMotInst: Freeing type 2 cache entry %d (0x%08x)\n\n");

				g_ModelRwdataBindings[1][i].model = NULL;

				model->rwdatas = NULL;
				model->rwdatalen = -1;

				done = true;
				break;
			}
		}
	}

	if (!done) {
		for (i = 0; i < NUMTYPE3(); i++) {
			if (g_ModelRwdataBindings[2][i].model == model) {
				osSyncPrintf("\nMotInst: Freeing type 3 cache entry %d (0x%08x)\n\n");
				g_ModelRwdataBindings[2][i].model = NULL;

				model->rwdatas = NULL;
				model->rwdatalen = -1;

				done = true;
				break;
			}
		}
	}

	if (!done) {
		osSyncPrintf("MotInst -> Attempt to free item not in cache\n");
	}

	if (model->anim) {
		modelmgrFreeAnim(model->anim);
		model->anim = NULL;
	}

	model->definition = NULL;
}

struct model *modelmgrInstantiateModelWithAnim(struct modeldef *modeldef)
{
	return modelmgrInstantiateModel(modeldef, true);
}

void modelmgrAttachHead(struct model *model, struct modelnode *node, struct modeldef *headmodeldef)
{
	modelAttachHead(model, model->definition, node, headmodeldef);
	modelInitRwData(model, headmodeldef->rootnode);
}

struct anim *modelmgrInstantiateAnim(void)
{
	s32 i;
	struct anim *anim = NULL;

	for (i = 0; i < g_MaxAnims; i++) {
		if (g_AnimSlots[i].animnum == -1) {
			anim = &g_AnimSlots[i];
			break;
		}
	}

	return anim;
}

void modelmgrFreeAnim(struct anim *anim)
{
	anim->animnum = -1;
}
